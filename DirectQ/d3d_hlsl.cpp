/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 3
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/
// r_misc.c

#include "quakedef.h"
#include "d3d_model.h"
#include "d3d_quake.h"
#include "resource.h"


/*
============================================================================================================

		HLSL STUFF

============================================================================================================
*/

// effects
LPD3DXEFFECT d3d_MasterFXWithFog = NULL;
LPD3DXEFFECT d3d_MasterFXNoFog = NULL;
LPD3DXEFFECT d3d_MasterFX = NULL;

// let's see, where is it documented that this must be a NULL-terminated array again?
D3DXMACRO d3d_EnableFogInShaders[] = {{"hlsl_fog", "1"}, {NULL, NULL}};

bool SilentLoad = false;

// keep these global as we'll want to use them in a few places
char *vs_version;
char *ps_version;

// ps2.0 defines at least 8 textures as being available
#define MAX_HLSL_TEXTURES	8

typedef struct d3d_hlslstate_s
{
	float AlphaVal;
	float CurrLerp;
	float LastLerp;

	LPDIRECT3DBASETEXTURE9 newtextures[MAX_HLSL_TEXTURES];
	LPDIRECT3DBASETEXTURE9 oldtextures[MAX_HLSL_TEXTURES];

	bool commitpending;
	int currentpass;
} d3d_hlslstate_t;

d3d_hlslstate_t d3d_HLSLState;


void D3DHLSL_CheckCommit (void)
{
	if (d3d_HLSLState.commitpending)
		d3d_MasterFX->CommitChanges ();

	// now check for texture changes; this is always done even if we don't commit as we need a reset if we switch shaders
	// texture changes are done this way rather than through ID3DXBaseEffect::SetTexture to avoid a computationally expensive
	// AddRef and Release (just profile an app using ID3DXBaseEffect::SetTexture in PIX and you'll see what I mean!)
	// we specified explicit registers for our samplers so that we can safely do this (also see use of SetSamplerState)
	for (int i = 0; i < MAX_HLSL_TEXTURES; i++)
	{
		// note - we specified explicit registers for our samplers so that we can safely do this
		if (d3d_HLSLState.oldtextures[i] != d3d_HLSLState.newtextures[i])
		{
			if (d3d_HLSLState.newtextures[i]) d3d_Device->SetTexture (i, d3d_HLSLState.newtextures[i]);
			d3d_HLSLState.oldtextures[i] = d3d_HLSLState.newtextures[i];
		}
	}

	// always clear the commit flag so that it doesn't incorrectly fire later on
	d3d_HLSLState.commitpending = false;
}


void D3DHLSL_SetPass (int passnum)
{
	if (d3d_HLSLState.currentpass != passnum)
	{
		// end any previous pass we were using (but only if we were using one)
		if (d3d_HLSLState.currentpass != FX_PASS_NOTBEGUN)
		{
			// and now we can end the pass
			d3d_MasterFX->EndPass ();
		}

		// clear the commit flag here because we don't need a commit before a BeginPass
		d3d_HLSLState.commitpending = false;

		// force all textures to recache
		for (int i = 0; i < MAX_HLSL_TEXTURES; i++)
			d3d_HLSLState.oldtextures[i] = NULL;

		// interesting - BeginPass clears the currently set textures... how evil...
		d3d_HLSLState.currentpass = passnum;
		d3d_MasterFX->BeginPass (passnum);
	}
}


void D3DHLSL_SetAlpha (float alphaval)
{
	if (alphaval != d3d_HLSLState.AlphaVal)
	{
		// update the alpha val
		D3DHLSL_SetFloat ("AlphaVal", alphaval);

		// update stored value
		d3d_HLSLState.AlphaVal = alphaval;
	}
}


void D3DHLSL_SetLerp (float curr, float last)
{
	if (curr != d3d_HLSLState.CurrLerp || last != d3d_HLSLState.LastLerp)
	{
		float lerpval[2] = {curr, last};

		D3DHLSL_SetFloatArray ("aliaslerp", lerpval, 2);
		d3d_HLSLState.CurrLerp = curr;
		d3d_HLSLState.LastLerp = last;
	}
}


void D3DHLSL_SetTexture (UINT stage, LPDIRECT3DBASETEXTURE9 tex)
{
	// changing texture no longer forces a commit here but it does store it out for later
	d3d_HLSLState.newtextures[stage] = tex;
}


void D3DHLSL_SetMatrix (D3DXHANDLE h, D3DMATRIX *matrix)
{
	d3d_HLSLState.commitpending = true;
	d3d_MasterFX->SetMatrix (h, D3DMatrix_ToD3DXMatrix (matrix));
}


void D3DHLSL_SetWorldMatrix (D3DMATRIX *worldmatrix)
{
	d3d_HLSLState.commitpending = true;
	d3d_MasterFX->SetMatrix ("WorldMatrix", D3DMatrix_ToD3DXMatrix (worldmatrix));
}


void D3DHLSL_SetEntMatrix (D3DMATRIX *entmatrix)
{
	d3d_HLSLState.commitpending = true;
	d3d_MasterFX->SetMatrix ("EntMatrix", D3DMatrix_ToD3DXMatrix (entmatrix));
}


void D3DHLSL_SetFogMatrix (D3DMATRIX *fogmatrix)
{
	d3d_HLSLState.commitpending = true;
	d3d_MasterFX->SetMatrix ("ModelViewMatrix", D3DMatrix_ToD3DXMatrix (fogmatrix));
}


void D3DHLSL_BeginFrame (void)
{
	UINT numpasses = 0;

	// invalidate any cached states to keep each frame valid
	D3DHLSL_InvalidateState ();

	// the technique is now only set when the shader needs to be changed
	d3d_MasterFX->Begin (&numpasses, D3DXFX_DONOTSAVESTATE);

	// set up anisotropic filtering
	extern cvar_t r_anisotropicfilter;
	int blah;

	// fixme - filter this out...?
	for (blah = 1; blah < r_anisotropicfilter.integer; blah <<= 1);
	if (blah > d3d_DeviceCaps.MaxAnisotropy) blah = d3d_DeviceCaps.MaxAnisotropy;
	if (blah < 1) blah = 1;

	if (blah != r_anisotropicfilter.integer)
		Cvar_Set (&r_anisotropicfilter, (float) blah);

	for (int i = 0; i < MAX_HLSL_TEXTURES; i++)
		D3D_SetSamplerState (i, D3DSAMP_MAXANISOTROPY, r_anisotropicfilter.integer);
}


void D3DHLSL_EndFrame (void)
{
	if (d3d_HLSLState.currentpass != FX_PASS_NOTBEGUN)
	{
		d3d_MasterFX->EndPass ();
		d3d_HLSLState.commitpending = false;
		d3d_HLSLState.currentpass = FX_PASS_NOTBEGUN;

		// unbind all resources used by this FX
		for (int i = 0; i < MAX_HLSL_TEXTURES; i++)
			D3DHLSL_SetTexture (i, NULL);

		d3d_Device->SetVertexShader (NULL);
		d3d_Device->SetPixelShader (NULL);
	}

	d3d_MasterFX->End ();
}


void D3DHLSL_SetFloat (D3DXHANDLE handle, float fl)
{
	d3d_HLSLState.commitpending = true;
	d3d_MasterFX->SetFloat (handle, fl);
}


void D3DHLSL_SetInt (D3DXHANDLE handle, int n)
{
	d3d_HLSLState.commitpending = true;
	d3d_MasterFX->SetInt (handle, n);
}


void D3DHLSL_SetFloatArray (D3DXHANDLE handle, float *fl, int len)
{
	d3d_HLSLState.commitpending = true;
	d3d_MasterFX->SetFloatArray (handle, fl, len);
}


void D3DHLSL_SetVectorArray (D3DXHANDLE handle, D3DXVECTOR4 *vecs, int len)
{
	d3d_HLSLState.commitpending = true;
	d3d_MasterFX->SetVectorArray (handle, vecs, len);
}


void D3DHLSL_EnableFog (bool enable)
{
	LPD3DXEFFECT d3d_DesiredFX = NULL;

	// just switch the shader; these are really the same shader but with different stuff #ifdef'ed in and out
	if (enable && d3d_MasterFXWithFog)
		d3d_DesiredFX = d3d_MasterFXWithFog;
	else if (d3d_MasterFXNoFog)
		d3d_DesiredFX = d3d_MasterFXNoFog;
	else if (d3d_MasterFXWithFog)
		d3d_DesiredFX = d3d_MasterFXWithFog;
	else Sys_Error ("No shaders were successfully loaded!");

	// check for a change in shader state
	if (d3d_MasterFX != d3d_DesiredFX)
	{
		// setting the technique is slow so only do it when the effect changes
		d3d_DesiredFX->SetTechnique ("MasterRefresh");

		// store out globally so that we can run it
		d3d_MasterFX = d3d_DesiredFX;
	}

	// fix me - the shader might change but certain cached params don't - or are we invalidating the
	// caches every frame??????????????????
}


void D3DHLSL_InvalidateState (void)
{
	// this is run at the start of every frame and resets the shader state so that everything is picked up validly
	d3d_HLSLState.AlphaVal = -1;
	d3d_HLSLState.CurrLerp = -1;
	d3d_HLSLState.LastLerp = -1;

	for (int i = 0; i < MAX_HLSL_TEXTURES; i++)
	{
		d3d_HLSLState.oldtextures[i] = NULL;
		d3d_HLSLState.newtextures[i] = NULL;
	}

	d3d_HLSLState.currentpass = FX_PASS_NOTBEGUN;
	d3d_HLSLState.commitpending = false;
}


void D3DHLSL_LoadEffect (char *name, char *EffectString, int Len, LPD3DXEFFECT *eff, D3DXMACRO *d3d_FogEnabled)
{
	LPD3DXBUFFER errbuf = NULL;
	DWORD ShaderFlags = D3DXSHADER_SKIPVALIDATION | D3DXFX_DONOTSAVESTATE;

	// add extra flags if they're available
#ifdef D3DXFX_NOT_CLONEABLE
	ShaderFlags |= D3DXFX_NOT_CLONEABLE;
#endif

#ifdef D3DXSHADER_OPTIMIZATION_LEVEL3
	ShaderFlags |= D3DXSHADER_OPTIMIZATION_LEVEL3;
#endif

#ifdef D3DXSHADER_PACKMATRIX_COLUMNMAJOR
	ShaderFlags |= D3DXSHADER_PACKMATRIX_COLUMNMAJOR;
#endif

	hr = D3DXCreateEffect
	(
		d3d_Device,
		EffectString,
		Len,
		d3d_FogEnabled,
		NULL,
		ShaderFlags,
		NULL,
		eff,
		&errbuf
	);

	if (FAILED (hr))
	{
		char *errstr = (char *) errbuf->GetBufferPointer ();
		Con_SafePrintf ("D3DHLSL_LoadEffect: Fatal error compiling %s\n%s", name, errstr);

#ifdef _DEBUG
		DebugBreak ();
#endif

		errbuf->Release ();
	}
	else if (errbuf)
	{
		char *errstr = (char *) errbuf->GetBufferPointer ();
		Con_SafePrintf ("D3DHLSL_LoadEffect: Non-fatal error compiling %s\n%s", name, errstr);

#ifdef _DEBUG
		DebugBreak ();
#endif

		errbuf->Release ();
	}
}


void D3DHLSL_Init (void)
{
	// no PS with < 3 TMUs
	if (d3d_GlobalCaps.NumTMUs < 3)
		Sys_Error ("Num TMUs < 3");

	// now set up effects
	vs_version = (char *) D3DXGetVertexShaderProfile (d3d_Device);
	ps_version = (char *) D3DXGetPixelShaderProfile (d3d_Device);

	if (!SilentLoad) Con_Printf ("\n");

	if (vs_version)
	{
		if (!SilentLoad) Con_Printf ("Vertex Shader Version: %s\n", vs_version);
	}
	else
	{
		// note - we expect this to never happen as D3D will create vertex shaders in software
		// for devices that don't support them in hardware.
		Sys_Error ("Vertex Shaders Not Available\n");
		return;
	}

	if (ps_version)
	{
		if (!SilentLoad) Con_Printf ("Pixel Shader Version: %s\n", ps_version);
	}
	else
	{
		// note - don't sys_error here as we will implement a software fallback
		Sys_Error ("Pixel Shaders Not Available\n");
		return;
	}

	// D3DSHADER_VERSION_MAJOR is UNDOCUMENTED in the SDK!!!  all that it says about these caps is that
	// they are "two numbers" but there is NOTHING about decoding them.
	int vsvermaj = D3DSHADER_VERSION_MAJOR (d3d_DeviceCaps.VertexShaderVersion);
	int psvermaj = D3DSHADER_VERSION_MAJOR (d3d_DeviceCaps.PixelShaderVersion);

	if (vsvermaj < 2 || psvermaj < 2 || vsvermaj < psvermaj)
	{
		Sys_Error ("Vertex or Pixel Shaders version is too low.\n");
		return;
	}

	// load the resource - note - we don't use D3DXCreateEffectFromResource as importing an RCDATA resource
	// from a text file in Visual Studio doesn't NULL terminate the file, causing it to blow up.
	char *EffectString = NULL;
	int len = Sys_LoadResourceData (IDR_MASTERFX, (void **) &EffectString);

	// copy if off so that we can change it safely (is this even needed???)
	char *RealEffect = (char *) scratchbuf;
	memcpy (RealEffect, EffectString, len);

	// upgrade to SM3 if possible
	if (vsvermaj > 2 && psvermaj > 2)
	{
		for (int i = 0; i < len; i++)
		{
			if (!strncmp (&RealEffect[i], " vs_2_0 ", 8)) RealEffect[i + 4] = '3';
			if (!strncmp (&RealEffect[i], " ps_2_0 ", 8)) RealEffect[i + 4] = '3';
		}
	}

	// and now load 'em
	D3DHLSL_LoadEffect ("Master Shader (No Fog)", RealEffect, len, &d3d_MasterFXNoFog, NULL);
	D3DHLSL_LoadEffect ("Master Shader (With Fog)", RealEffect, len, &d3d_MasterFXWithFog, d3d_EnableFogInShaders);

	// we'll be kind and allow one of them to fail, but at least one must succeed
	if (d3d_MasterFXNoFog)
		d3d_MasterFX = d3d_MasterFXNoFog;
	else if (d3d_MasterFXWithFog)
		d3d_MasterFX = d3d_MasterFXWithFog;
	else Sys_Error ("No shaders were successfully loaded!");

	// now begin the effect by setting the technique
	d3d_MasterFX->SetTechnique ("MasterRefresh");

	if (!SilentLoad) Con_Printf ("Created Shaders OK\n");

	// only display output on the first load
	SilentLoad = true;
}


#define HLSL_RELEASE(s) if (s) \
{ \
	(s)->OnLostDevice (); \
	SAFE_RELEASE (s); \
}

void D3DHLSL_Shutdown (void)
{
	d3d_MasterFX = NULL;

	HLSL_RELEASE (d3d_MasterFXNoFog);
	HLSL_RELEASE (d3d_MasterFXWithFog);

	// invalidate any cached states
	D3DHLSL_InvalidateState ();
}


CD3DDeviceLossHandler d3d_HLSLHandler (D3DHLSL_Shutdown, D3DHLSL_Init);

