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

typedef struct d3d_hlslstate_s
{
	float AlphaVal;
	float DepthBias;
	float CurrLerp;
	float LastLerp;

	LPDIRECT3DBASETEXTURE9 textures[3];
	DWORD addressmodes[3];
	DWORD magfilters[3];
	DWORD minfilters[3];
	DWORD mipfilters[3];

	bool commitpending;
	int currentpass;
} d3d_hlslstate_t;

d3d_hlslstate_t d3d_HLSLState;


void D3DHLSL_CheckCommit (void)
{
	if (d3d_HLSLState.commitpending)
		d3d_MasterFX->CommitChanges ();

	// always clear the commit flag so that it doesn't incorrectly fire later on
	d3d_HLSLState.commitpending = false;
}


void D3DHLSL_SetPass (int passnum)
{
	if (d3d_HLSLState.currentpass != passnum)
	{
		// end any previous pass we were using
		if (d3d_HLSLState.currentpass != FX_PASS_NOTBEGUN)
		{
			// this fixes a D3DX runtime warning
			// D3DHLSL_CheckCommit ();

			// and now we can end the pass
			d3d_MasterFX->EndPass ();
		}

		// clear the commit flag here because we don't need a commit before a BeginPass
		d3d_HLSLState.commitpending = false;

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


void D3DHLSL_SetDepthBias (float depthbias)
{
	if (depthbias != d3d_HLSLState.DepthBias)
	{
		D3DHLSL_SetFloat ("DepthBias", depthbias);
		d3d_HLSLState.DepthBias = depthbias;
	}
}


void D3DHLSL_SetCurrLerp (float val)
{
	if (val != d3d_HLSLState.CurrLerp)
	{
		// 1/200 is used here so that we can get full -1..1 range
		float lerpval[2] = {val, val * 0.005f};

		D3DHLSL_SetFloatArray ("currlerp", lerpval, 2);
		d3d_HLSLState.CurrLerp = val;
	}
}


void D3DHLSL_SetLastLerp (float val)
{
	if (val != d3d_HLSLState.LastLerp)
	{
		// 1/200 is used here so that we can get full -1..1 range
		float lerpval[2] = {val, val * 0.005f};

		D3DHLSL_SetFloatArray ("lastlerp", lerpval, 2);
		d3d_HLSLState.LastLerp = val;
	}
}


D3DXHANDLE d3d_hlslstages[] = {"tmu0Texture", "tmu1Texture", "tmu2Texture"};
D3DXHANDLE d3d_hlsladdressmodestages[] = {"address0", "address1", "address2"};
D3DXHANDLE d3d_magfilterstages[] = {"magfilter0", "magfilter1", "magfilter2"};
D3DXHANDLE d3d_mipfilterstages[] = {"mipfilter0", "mipfilter1", "mipfilter2"};
D3DXHANDLE d3d_minfilterstages[] = {"minfilter0", "minfilter1", "minfilter2"};


void D3DHLSL_SetTexture (UINT stage, LPDIRECT3DBASETEXTURE9 tex)
{
	if (d3d_HLSLState.textures[stage] != tex)
	{
		// don't set a NULL texture but do record that it was changed for the next time
		if (tex) d3d_MasterFX->SetTexture (d3d_hlslstages[stage], tex);

		d3d_HLSLState.commitpending = true;
		d3d_HLSLState.textures[stage] = tex;
	}
}


void D3DHLSL_SetAddressMode (UINT stage, DWORD mode)
{
	if (d3d_HLSLState.addressmodes[stage] != mode)
	{
		D3DHLSL_SetInt (d3d_hlsladdressmodestages[stage], mode);
		d3d_HLSLState.addressmodes[stage] = mode;
	}
}


void D3DHLSL_SetMagFilter (UINT stage, DWORD mode)
{
	if (d3d_HLSLState.magfilters[stage] != mode)
	{
		D3DHLSL_SetInt (d3d_magfilterstages[stage], mode);
		d3d_HLSLState.magfilters[stage] = mode;
	}
}


void D3DHLSL_SetMipFilter (UINT stage, DWORD mode)
{
	if (d3d_HLSLState.mipfilters[stage] != mode)
	{
		D3DHLSL_SetInt (d3d_mipfilterstages[stage], mode);
		d3d_HLSLState.mipfilters[stage] = mode;
	}
}


void D3DHLSL_SetMinFilter (UINT stage, DWORD mode)
{
	if (d3d_HLSLState.minfilters[stage] != mode)
	{
		D3DHLSL_SetInt (d3d_minfilterstages[stage], mode);
		d3d_HLSLState.minfilters[stage] = mode;
	}
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

	d3d_MasterFX->SetTechnique ("MasterRefresh");
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

	// draw MDLs at regular depth by default
	D3DHLSL_SetDepthBias (1.0f);

	D3DHLSL_SetInt ("aniso0", r_anisotropicfilter.integer);
	D3DHLSL_SetInt ("aniso1", r_anisotropicfilter.integer);
	D3DHLSL_SetInt ("aniso2", r_anisotropicfilter.integer);
}


void D3DHLSL_EndFrame (void)
{
	if (d3d_HLSLState.currentpass != FX_PASS_NOTBEGUN)
	{
		d3d_MasterFX->EndPass ();
		d3d_HLSLState.commitpending = false;
		d3d_HLSLState.currentpass = FX_PASS_NOTBEGUN;

		// unbind all resources used by this FX
		D3DHLSL_SetTexture (0, NULL);
		D3DHLSL_SetTexture (1, NULL);
		D3DHLSL_SetTexture (2, NULL);

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


void D3DHLSL_EnableFog (bool enable)
{
	// just switch the shader; these are really the same shader but with different stuff #ifdef'ed in and out
	if (enable && d3d_MasterFXWithFog)
		d3d_MasterFX = d3d_MasterFXWithFog;
	else if (d3d_MasterFXNoFog)
		d3d_MasterFX = d3d_MasterFXNoFog;
	else if (d3d_MasterFXWithFog)
		d3d_MasterFX = d3d_MasterFXWithFog;
	else Sys_Error ("No shaders were successfully loaded!");

	// fix me - the shader might change but certain cached params don't - or are we invalidating the
	// caches every frame??????????????????
}


void D3DHLSL_InvalidateState (void)
{
	// this is run at the start of every frame and resets the shader state so that everything is picked up validly
	d3d_HLSLState.AlphaVal = -1;
	d3d_HLSLState.DepthBias = -1;
	d3d_HLSLState.CurrLerp = -1;
	d3d_HLSLState.LastLerp = -1;

	d3d_HLSLState.textures[0] = NULL;
	d3d_HLSLState.textures[1] = NULL;
	d3d_HLSLState.textures[2] = NULL;

	d3d_HLSLState.addressmodes[0] = 0xffffffff;
	d3d_HLSLState.addressmodes[1] = 0xffffffff;
	d3d_HLSLState.addressmodes[2] = 0xffffffff;

	d3d_HLSLState.magfilters[0] = 0xffffffff;
	d3d_HLSLState.magfilters[1] = 0xffffffff;
	d3d_HLSLState.magfilters[2] = 0xffffffff;

	d3d_HLSLState.mipfilters[0] = 0xffffffff;
	d3d_HLSLState.mipfilters[1] = 0xffffffff;
	d3d_HLSLState.mipfilters[2] = 0xffffffff;

	d3d_HLSLState.minfilters[0] = 0xffffffff;
	d3d_HLSLState.minfilters[1] = 0xffffffff;
	d3d_HLSLState.minfilters[2] = 0xffffffff;

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
		Con_Printf ("D3DHLSL_LoadEffect: Fatal error compiling %s\n%s", name, errstr);
		errbuf->Release ();
	}
	else if (errbuf)
	{
		char *errstr = (char *) errbuf->GetBufferPointer ();
		Con_Printf ("D3DHLSL_LoadEffect: Non-fatal error compiling %s\n%s", name, errstr);
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

	if (!SilentLoad) Con_Printf ("Created Shaders OK\n");

	// only display output on the first load
	SilentLoad = true;
}


void D3DHLSL_Shutdown (void)
{
	d3d_MasterFX = NULL;

	if (d3d_MasterFXNoFog)
	{
		// release any other resources the fx may have created
		d3d_MasterFXNoFog->OnLostDevice ();
		SAFE_RELEASE (d3d_MasterFXNoFog);
	}

	if (d3d_MasterFXWithFog)
	{
		// release any other resources the fx may have created
		d3d_MasterFXWithFog->OnLostDevice ();
		SAFE_RELEASE (d3d_MasterFXWithFog);
	}

	// invalidate any cached states
	D3DHLSL_InvalidateState ();
}


CD3DDeviceLossHandler d3d_HLSLHandler (D3DHLSL_Shutdown, D3DHLSL_Init);

