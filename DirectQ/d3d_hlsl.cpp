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

#define HLSL_RELEASE(s) if (s) \
	{ \
		(s)->OnLostDevice (); \
		SAFE_RELEASE (s); \
	}


char *d3d_EffectString = 0;
LPD3DXEFFECT d3d_MasterFX = NULL;

#define MAX_D3D_SHADERS 65536

LPD3DXEFFECT d3d_CompiledShaders[MAX_D3D_SHADERS] = {NULL};

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


int d3d_DesiredShader = 0;
void D3DHLSL_LoadEffect (char *name, char *EffectString, int Len, LPD3DXEFFECT *eff, D3DXMACRO *d3d_FogEnabled);

void D3DHLSL_BeginFrame (void)
{
	UINT numpasses = 0;

	// lazy build on demand because there are too many shaders to build up-front
	if (!d3d_CompiledShaders[d3d_DesiredShader])
	{
		// build the shader with the new defines
		D3DXMACRO *macros = (D3DXMACRO *) scratchbuf;
		int nummacros = 0;

		if (d3d_DesiredShader & HLSL_FOG)
		{
			macros[nummacros].Name = "hlsl_fog";
			macros[nummacros].Definition = "1";
			nummacros++;
		}

		// and null term the macro list
		macros[nummacros].Name = NULL;
		macros[nummacros].Definition = NULL;

		// load it anew
		D3DHLSL_LoadEffect (va ("Master Shader %i", d3d_DesiredShader),
			d3d_EffectString,
			strlen (d3d_EffectString),
			&d3d_CompiledShaders[d3d_DesiredShader],
			macros);
	}

	// check for a change in shader state
	if (d3d_MasterFX != d3d_CompiledShaders[d3d_DesiredShader])
	{
		d3d_MasterFX = d3d_CompiledShaders[d3d_DesiredShader];
		d3d_MasterFX->SetTechnique ("MasterRefresh");
	}

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
		Sys_Error ("D3DHLSL_LoadEffect: Fatal error compiling %s\n%s", name, errstr);

#ifdef _DEBUG
		DebugBreak ();
#endif

		errbuf->Release ();
	}
	else if (errbuf)
	{
		char *errstr = (char *) errbuf->GetBufferPointer ();
		Sys_Error ("D3DHLSL_LoadEffect: Non-fatal error compiling %s\n%s", name, errstr);

#ifdef _DEBUG
		DebugBreak ();
#endif

		errbuf->Release ();
	}
}


void D3DHLSL_SelectShader (int desiredshader)
{
	if (desiredshader < 0) desiredshader = 0;
	if (desiredshader >= MAX_D3D_SHADERS) desiredshader = MAX_D3D_SHADERS - 1;

	// just set what we want, set it for real on the next begin frame
	d3d_DesiredShader = desiredshader;
}


void D3DHLSL_Init (void)
{
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
	d3d_EffectString = (char *) MainZone->Alloc (len + 1);
	memcpy (d3d_EffectString, EffectString, len);

	// upgrade to SM3 if possible
	if (vsvermaj > 2 && psvermaj > 2)
	{
		for (int i = 0; i < len; i++)
		{
			if (!strncmp (&d3d_EffectString[i], " vs_2_0 ", 8)) d3d_EffectString[i + 4] = '3';
			if (!strncmp (&d3d_EffectString[i], " ps_2_0 ", 8)) d3d_EffectString[i + 4] = '3';
		}
	}

	// select the plain shader so that we've something valid at startup
	D3DHLSL_SelectShader (HLSL_PLAIN);

	// run one shader frame to force our shaders to cache
	D3DHLSL_BeginFrame ();
	D3DHLSL_EndFrame ();

	// also precache the fog shader because we might have underwater fog enabled and we don't want to
	// stall for a second or so the first time we go underwater; everything else can remain uncached
	D3DHLSL_SelectShader (HLSL_FOG);

	// run one shader frame to force our shaders to cache
	D3DHLSL_BeginFrame ();
	D3DHLSL_EndFrame ();

	// subsequent loads are silent
	SilentLoad = true;
}


void D3DHLSL_Shutdown (void)
{
	for (int i = 0; i < MAX_D3D_SHADERS; i++)
	{
		HLSL_RELEASE (d3d_CompiledShaders[i]);
	}

	// force recache and invalidate any cached states
	d3d_MasterFX = NULL;
	D3DHLSL_InvalidateState ();
}


void D3DHLSL_DeviceLoss (void)
{
	for (int i = 0; i < MAX_D3D_SHADERS; i++)
	{
		// this is all that's actually needed and vid_restart/game change is *much* faster now...
		if (d3d_CompiledShaders[i])
			d3d_CompiledShaders[i]->OnLostDevice ();
	}

	// force recache and invalidate any cached states
	D3DHLSL_InvalidateState ();
}


void D3DHLSL_DeviceRestore (void)
{
	for (int i = 0; i < MAX_D3D_SHADERS; i++)
	{
		// this is all that's actually needed and vid_restart/game change is *much* faster now...
		if (d3d_CompiledShaders[i])
			d3d_CompiledShaders[i]->OnResetDevice ();
	}

	// force recache and invalidate any cached states
	D3DHLSL_InvalidateState ();
}


CD3DDeviceLossHandler d3d_HLSLHandler (D3DHLSL_DeviceLoss, D3DHLSL_DeviceRestore);

