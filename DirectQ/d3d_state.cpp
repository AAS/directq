/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

#include "quakedef.h"
#include "d3d_model.h"
#include "d3d_quake.h"

#include "winquake.h"


/*
===================================================================================================================

		STATE MANAGEMENT

	Here Direct3D makes things really easy for us by using a separate enum and separate member functions for
	each type of state.  This would be a fucking nightmare under OpenGL...

	Note that the runtime will filter state changes on a non-pure device, but we'll also do it ourselves to
	avoid the overhead of sending them to the runtime in the first place, which may be more optimal

===================================================================================================================
*/


DWORD d3d_RenderStates[256];

void D3D_SetRenderState (D3DRENDERSTATETYPE State, DWORD Value)
{
	if (d3d_RenderStates[(int) State] == Value)
	{
		// filter out rendundant changes before they go to the API
		return;
	}
	else
	{
		d3d_Device->SetRenderState (State, Value);
		d3d_RenderStates[(int) State] = Value;
	}
}


void D3D_SetRenderStatef (D3DRENDERSTATETYPE State, float Value)
{
	// some states require float input
	D3D_SetRenderState (State, ((DWORD *) &Value)[0]);
}


DWORD d3d_TextureStageStates[8][64];

void D3D_SetTextureState (DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value)
{
	if (d3d_TextureStageStates[Stage][(int) Type] == Value)
	{
		// filter out rendundant changes before they go to the API
		return;
	}
	else
	{
		d3d_Device->SetTextureStageState (Stage, Type, Value);
		d3d_TextureStageStates[Stage][(int) Type] = Value;
	}
}


void D3D_SetTextureStatef (DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, float Value)
{
	// some states require float input
	D3D_SetTextureState (Stage, Type, ((DWORD *) &Value)[0]);
}


void D3D_SetColorMode (DWORD stage, DWORD mode, DWORD arg1, DWORD arg2)
{
	D3D_SetTextureState (stage, D3DTSS_COLOROP, mode);
	D3D_SetTextureState (stage, D3DTSS_COLORARG1, arg1);
	D3D_SetTextureState (stage, D3DTSS_COLORARG2, arg2);
}


void D3D_SetAlphaMode (DWORD stage, DWORD mode, DWORD arg1, DWORD arg2)
{
	D3D_SetTextureState (stage, D3DTSS_ALPHAOP, mode);
	D3D_SetTextureState (stage, D3DTSS_ALPHAARG1, arg1);
	D3D_SetTextureState (stage, D3DTSS_ALPHAARG2, arg2);
}


DWORD d3d_SamplerStates[8][64];

void D3D_SetSamplerState (DWORD Stage, D3DSAMPLERSTATETYPE Type, DWORD Value)
{
	if (d3d_SamplerStates[Stage][(int) Type] == Value)
	{
		// filter out rendundant changes before they go to the API
		return;
	}
	else
	{
		d3d_Device->SetSamplerState (Stage, Type, Value);
		d3d_SamplerStates[Stage][(int) Type] = Value;
	}
}


void D3D_SetSamplerStatef (DWORD Stage, D3DSAMPLERSTATETYPE Type, float Value)
{
	// some states require float input
	D3D_SetSamplerState (Stage, Type, ((DWORD *) &Value)[0]);
}


LPDIRECT3DBASETEXTURE9 d3d_StageTextures[8];

void D3D_SetTexture (DWORD Sampler, LPDIRECT3DBASETEXTURE9 pTexture)
{
	if (Sampler > d3d_DeviceCaps.MaxTextureBlendStages) return;

	if (d3d_StageTextures[Sampler] == pTexture)
	{
		// filter out rendundant changes before they go to the API
		return;
	}
	else
	{
		if (pTexture) d3d_Device->SetTexture (Sampler, pTexture);
		d3d_StageTextures[Sampler] = pTexture;
	}
}

// init to an impossible FVF to ensure a change is triggered on first use
DWORD d3d_FVF = D3DFVF_XYZ | D3DFVF_XYZRHW;

void D3D_SetFVF (DWORD FVF)
{
	if (d3d_FVF == FVF)
	{
		// filter out rendundant changes before they go to the API
		return;
	}
	else
	{
		d3d_Device->SetFVF (FVF);
		d3d_FVF = FVF;
	}
}


void D3D_SetAllStates (void)
{
	// recover all states back to what they should be
	d3d_Device->SetRenderState (D3DRS_ZENABLE, d3d_RenderStates[(int) D3DRS_ZENABLE]);
	d3d_Device->SetRenderState (D3DRS_FILLMODE, d3d_RenderStates[(int) D3DRS_FILLMODE]);
	d3d_Device->SetRenderState (D3DRS_SHADEMODE, d3d_RenderStates[(int) D3DRS_SHADEMODE]);
	d3d_Device->SetRenderState (D3DRS_ZWRITEENABLE, d3d_RenderStates[(int) D3DRS_ZWRITEENABLE]);
	d3d_Device->SetRenderState (D3DRS_ALPHATESTENABLE, d3d_RenderStates[(int) D3DRS_ALPHATESTENABLE]);
	d3d_Device->SetRenderState (D3DRS_LASTPIXEL, d3d_RenderStates[(int) D3DRS_LASTPIXEL]);
	d3d_Device->SetRenderState (D3DRS_SRCBLEND, d3d_RenderStates[(int) D3DRS_SRCBLEND]);
	d3d_Device->SetRenderState (D3DRS_DESTBLEND, d3d_RenderStates[(int) D3DRS_DESTBLEND]);
	d3d_Device->SetRenderState (D3DRS_CULLMODE, d3d_RenderStates[(int) D3DRS_CULLMODE]);
	d3d_Device->SetRenderState (D3DRS_ZFUNC, d3d_RenderStates[(int) D3DRS_ZFUNC]);
	d3d_Device->SetRenderState (D3DRS_ALPHAREF, d3d_RenderStates[(int) D3DRS_ALPHAREF]);
	d3d_Device->SetRenderState (D3DRS_ALPHAFUNC, d3d_RenderStates[(int) D3DRS_ALPHAFUNC]);
	d3d_Device->SetRenderState (D3DRS_DITHERENABLE, d3d_RenderStates[(int) D3DRS_DITHERENABLE]);
	d3d_Device->SetRenderState (D3DRS_ALPHABLENDENABLE, d3d_RenderStates[(int) D3DRS_ALPHABLENDENABLE]);
	d3d_Device->SetRenderState (D3DRS_FOGENABLE, d3d_RenderStates[(int) D3DRS_FOGENABLE]);
	d3d_Device->SetRenderState (D3DRS_SPECULARENABLE, d3d_RenderStates[(int) D3DRS_SPECULARENABLE]);
	d3d_Device->SetRenderState (D3DRS_FOGCOLOR, d3d_RenderStates[(int) D3DRS_FOGCOLOR]);
	d3d_Device->SetRenderState (D3DRS_FOGTABLEMODE, d3d_RenderStates[(int) D3DRS_FOGTABLEMODE]);
	d3d_Device->SetRenderState (D3DRS_FOGSTART, d3d_RenderStates[(int) D3DRS_FOGSTART]);
	d3d_Device->SetRenderState (D3DRS_FOGEND, d3d_RenderStates[(int) D3DRS_FOGEND]);
	d3d_Device->SetRenderState (D3DRS_FOGDENSITY, d3d_RenderStates[(int) D3DRS_FOGDENSITY]);
	d3d_Device->SetRenderState (D3DRS_RANGEFOGENABLE, d3d_RenderStates[(int) D3DRS_RANGEFOGENABLE]);
	d3d_Device->SetRenderState (D3DRS_STENCILENABLE, d3d_RenderStates[(int) D3DRS_STENCILENABLE]);
	d3d_Device->SetRenderState (D3DRS_STENCILFAIL, d3d_RenderStates[(int) D3DRS_STENCILFAIL]);
	d3d_Device->SetRenderState (D3DRS_STENCILZFAIL, d3d_RenderStates[(int) D3DRS_STENCILZFAIL]);
	d3d_Device->SetRenderState (D3DRS_STENCILPASS, d3d_RenderStates[(int) D3DRS_STENCILPASS]);
	d3d_Device->SetRenderState (D3DRS_STENCILFUNC, d3d_RenderStates[(int) D3DRS_STENCILFUNC]);
	d3d_Device->SetRenderState (D3DRS_STENCILREF, d3d_RenderStates[(int) D3DRS_STENCILREF]);
	d3d_Device->SetRenderState (D3DRS_STENCILMASK, d3d_RenderStates[(int) D3DRS_STENCILMASK]);
	d3d_Device->SetRenderState (D3DRS_STENCILWRITEMASK, d3d_RenderStates[(int) D3DRS_STENCILWRITEMASK]);
	d3d_Device->SetRenderState (D3DRS_TEXTUREFACTOR, d3d_RenderStates[(int) D3DRS_TEXTUREFACTOR]);
	d3d_Device->SetRenderState (D3DRS_WRAP0, d3d_RenderStates[(int) D3DRS_WRAP0]);
	d3d_Device->SetRenderState (D3DRS_WRAP1, d3d_RenderStates[(int) D3DRS_WRAP1]);
	d3d_Device->SetRenderState (D3DRS_WRAP2, d3d_RenderStates[(int) D3DRS_WRAP2]);
	d3d_Device->SetRenderState (D3DRS_WRAP3, d3d_RenderStates[(int) D3DRS_WRAP3]);
	d3d_Device->SetRenderState (D3DRS_WRAP4, d3d_RenderStates[(int) D3DRS_WRAP4]);
	d3d_Device->SetRenderState (D3DRS_WRAP5, d3d_RenderStates[(int) D3DRS_WRAP5]);
	d3d_Device->SetRenderState (D3DRS_WRAP6, d3d_RenderStates[(int) D3DRS_WRAP6]);
	d3d_Device->SetRenderState (D3DRS_WRAP7, d3d_RenderStates[(int) D3DRS_WRAP7]);
	d3d_Device->SetRenderState (D3DRS_CLIPPING, d3d_RenderStates[(int) D3DRS_CLIPPING]);
	d3d_Device->SetRenderState (D3DRS_LIGHTING, d3d_RenderStates[(int) D3DRS_LIGHTING]);
	d3d_Device->SetRenderState (D3DRS_AMBIENT, d3d_RenderStates[(int) D3DRS_AMBIENT]);
	d3d_Device->SetRenderState (D3DRS_FOGVERTEXMODE, d3d_RenderStates[(int) D3DRS_FOGVERTEXMODE]);
	d3d_Device->SetRenderState (D3DRS_COLORVERTEX, d3d_RenderStates[(int) D3DRS_COLORVERTEX]);
	d3d_Device->SetRenderState (D3DRS_LOCALVIEWER, d3d_RenderStates[(int) D3DRS_LOCALVIEWER]);
	d3d_Device->SetRenderState (D3DRS_NORMALIZENORMALS, d3d_RenderStates[(int) D3DRS_NORMALIZENORMALS]);
	d3d_Device->SetRenderState (D3DRS_DIFFUSEMATERIALSOURCE, d3d_RenderStates[(int) D3DRS_DIFFUSEMATERIALSOURCE]);
	d3d_Device->SetRenderState (D3DRS_SPECULARMATERIALSOURCE, d3d_RenderStates[(int) D3DRS_SPECULARMATERIALSOURCE]);
	d3d_Device->SetRenderState (D3DRS_AMBIENTMATERIALSOURCE, d3d_RenderStates[(int) D3DRS_AMBIENTMATERIALSOURCE]);
	d3d_Device->SetRenderState (D3DRS_EMISSIVEMATERIALSOURCE, d3d_RenderStates[(int) D3DRS_EMISSIVEMATERIALSOURCE]);
	d3d_Device->SetRenderState (D3DRS_VERTEXBLEND, d3d_RenderStates[(int) D3DRS_VERTEXBLEND]);
	d3d_Device->SetRenderState (D3DRS_CLIPPLANEENABLE, d3d_RenderStates[(int) D3DRS_CLIPPLANEENABLE]);
	d3d_Device->SetRenderState (D3DRS_POINTSIZE, d3d_RenderStates[(int) D3DRS_POINTSIZE]);
	d3d_Device->SetRenderState (D3DRS_POINTSIZE_MIN, d3d_RenderStates[(int) D3DRS_POINTSIZE_MIN]);
	d3d_Device->SetRenderState (D3DRS_POINTSPRITEENABLE, d3d_RenderStates[(int) D3DRS_POINTSPRITEENABLE]);
	d3d_Device->SetRenderState (D3DRS_POINTSCALEENABLE, d3d_RenderStates[(int) D3DRS_POINTSCALEENABLE]);
	d3d_Device->SetRenderState (D3DRS_POINTSCALE_A, d3d_RenderStates[(int) D3DRS_POINTSCALE_A]);
	d3d_Device->SetRenderState (D3DRS_POINTSCALE_B, d3d_RenderStates[(int) D3DRS_POINTSCALE_B]);
	d3d_Device->SetRenderState (D3DRS_POINTSCALE_C, d3d_RenderStates[(int) D3DRS_POINTSCALE_C]);
	d3d_Device->SetRenderState (D3DRS_MULTISAMPLEANTIALIAS, d3d_RenderStates[(int) D3DRS_MULTISAMPLEANTIALIAS]);
	d3d_Device->SetRenderState (D3DRS_MULTISAMPLEMASK, d3d_RenderStates[(int) D3DRS_MULTISAMPLEMASK]);
	d3d_Device->SetRenderState (D3DRS_PATCHEDGESTYLE, d3d_RenderStates[(int) D3DRS_PATCHEDGESTYLE]);
	d3d_Device->SetRenderState (D3DRS_DEBUGMONITORTOKEN, d3d_RenderStates[(int) D3DRS_DEBUGMONITORTOKEN]);
	d3d_Device->SetRenderState (D3DRS_POINTSIZE_MAX, d3d_RenderStates[(int) D3DRS_POINTSIZE_MAX]);
	d3d_Device->SetRenderState (D3DRS_INDEXEDVERTEXBLENDENABLE, d3d_RenderStates[(int) D3DRS_INDEXEDVERTEXBLENDENABLE]);
	d3d_Device->SetRenderState (D3DRS_COLORWRITEENABLE, d3d_RenderStates[(int) D3DRS_COLORWRITEENABLE]);
	d3d_Device->SetRenderState (D3DRS_TWEENFACTOR, d3d_RenderStates[(int) D3DRS_TWEENFACTOR]);
	d3d_Device->SetRenderState (D3DRS_BLENDOP, d3d_RenderStates[(int) D3DRS_BLENDOP]);
	d3d_Device->SetRenderState (D3DRS_POSITIONDEGREE, d3d_RenderStates[(int) D3DRS_POSITIONDEGREE]);
	d3d_Device->SetRenderState (D3DRS_NORMALDEGREE, d3d_RenderStates[(int) D3DRS_NORMALDEGREE]);
	d3d_Device->SetRenderState (D3DRS_SCISSORTESTENABLE, d3d_RenderStates[(int) D3DRS_SCISSORTESTENABLE]);
	d3d_Device->SetRenderState (D3DRS_SLOPESCALEDEPTHBIAS, d3d_RenderStates[(int) D3DRS_SLOPESCALEDEPTHBIAS]);
	d3d_Device->SetRenderState (D3DRS_ANTIALIASEDLINEENABLE, d3d_RenderStates[(int) D3DRS_ANTIALIASEDLINEENABLE]);
	d3d_Device->SetRenderState (D3DRS_MINTESSELLATIONLEVEL, d3d_RenderStates[(int) D3DRS_MINTESSELLATIONLEVEL]);
	d3d_Device->SetRenderState (D3DRS_MAXTESSELLATIONLEVEL, d3d_RenderStates[(int) D3DRS_MAXTESSELLATIONLEVEL]);
	d3d_Device->SetRenderState (D3DRS_ADAPTIVETESS_X, d3d_RenderStates[(int) D3DRS_ADAPTIVETESS_X]);
	d3d_Device->SetRenderState (D3DRS_ADAPTIVETESS_Y, d3d_RenderStates[(int) D3DRS_ADAPTIVETESS_Y]);
	d3d_Device->SetRenderState (D3DRS_ADAPTIVETESS_Z, d3d_RenderStates[(int) D3DRS_ADAPTIVETESS_Z]);
	d3d_Device->SetRenderState (D3DRS_ADAPTIVETESS_W, d3d_RenderStates[(int) D3DRS_ADAPTIVETESS_W]);
	d3d_Device->SetRenderState (D3DRS_ENABLEADAPTIVETESSELLATION, d3d_RenderStates[(int) D3DRS_ENABLEADAPTIVETESSELLATION]);
	d3d_Device->SetRenderState (D3DRS_TWOSIDEDSTENCILMODE, d3d_RenderStates[(int) D3DRS_TWOSIDEDSTENCILMODE]);
	d3d_Device->SetRenderState (D3DRS_CCW_STENCILFAIL, d3d_RenderStates[(int) D3DRS_CCW_STENCILFAIL]);
	d3d_Device->SetRenderState (D3DRS_CCW_STENCILZFAIL, d3d_RenderStates[(int) D3DRS_CCW_STENCILZFAIL]);
	d3d_Device->SetRenderState (D3DRS_CCW_STENCILPASS, d3d_RenderStates[(int) D3DRS_CCW_STENCILPASS]);
	d3d_Device->SetRenderState (D3DRS_CCW_STENCILFUNC, d3d_RenderStates[(int) D3DRS_CCW_STENCILFUNC]);
	d3d_Device->SetRenderState (D3DRS_COLORWRITEENABLE1, d3d_RenderStates[(int) D3DRS_COLORWRITEENABLE1]);
	d3d_Device->SetRenderState (D3DRS_COLORWRITEENABLE2, d3d_RenderStates[(int) D3DRS_COLORWRITEENABLE2]);
	d3d_Device->SetRenderState (D3DRS_COLORWRITEENABLE3, d3d_RenderStates[(int) D3DRS_COLORWRITEENABLE3]);
	d3d_Device->SetRenderState (D3DRS_BLENDFACTOR, d3d_RenderStates[(int) D3DRS_BLENDFACTOR]);
	d3d_Device->SetRenderState (D3DRS_SRGBWRITEENABLE, d3d_RenderStates[(int) D3DRS_SRGBWRITEENABLE]);
	d3d_Device->SetRenderState (D3DRS_DEPTHBIAS, d3d_RenderStates[(int) D3DRS_DEPTHBIAS]);
	d3d_Device->SetRenderState (D3DRS_WRAP8, d3d_RenderStates[(int) D3DRS_WRAP8]);
	d3d_Device->SetRenderState (D3DRS_WRAP9, d3d_RenderStates[(int) D3DRS_WRAP9]);
	d3d_Device->SetRenderState (D3DRS_WRAP10, d3d_RenderStates[(int) D3DRS_WRAP10]);
	d3d_Device->SetRenderState (D3DRS_WRAP11, d3d_RenderStates[(int) D3DRS_WRAP11]);
	d3d_Device->SetRenderState (D3DRS_WRAP12, d3d_RenderStates[(int) D3DRS_WRAP12]);
	d3d_Device->SetRenderState (D3DRS_WRAP13, d3d_RenderStates[(int) D3DRS_WRAP13]);
	d3d_Device->SetRenderState (D3DRS_WRAP14, d3d_RenderStates[(int) D3DRS_WRAP14]);
	d3d_Device->SetRenderState (D3DRS_WRAP15, d3d_RenderStates[(int) D3DRS_WRAP15]);
	d3d_Device->SetRenderState (D3DRS_SEPARATEALPHABLENDENABLE, d3d_RenderStates[(int) D3DRS_SEPARATEALPHABLENDENABLE]);
	d3d_Device->SetRenderState (D3DRS_SRCBLENDALPHA, d3d_RenderStates[(int) D3DRS_SRCBLENDALPHA]);
	d3d_Device->SetRenderState (D3DRS_DESTBLENDALPHA, d3d_RenderStates[(int) D3DRS_DESTBLENDALPHA]);
	d3d_Device->SetRenderState (D3DRS_BLENDOPALPHA, d3d_RenderStates[(int) D3DRS_BLENDOPALPHA]);

	for (int s = 0; s < 8; s++)
	{
		d3d_Device->SetSamplerState (s, D3DSAMP_ADDRESSU, d3d_SamplerStates[s][(int) D3DSAMP_ADDRESSU]);
		d3d_Device->SetSamplerState (s, D3DSAMP_ADDRESSV, d3d_SamplerStates[s][(int) D3DSAMP_ADDRESSV]);
		d3d_Device->SetSamplerState (s, D3DSAMP_ADDRESSW, d3d_SamplerStates[s][(int) D3DSAMP_ADDRESSW]);
		d3d_Device->SetSamplerState (s, D3DSAMP_BORDERCOLOR, d3d_SamplerStates[s][(int) D3DSAMP_BORDERCOLOR]);
		d3d_Device->SetSamplerState (s, D3DSAMP_MAGFILTER, d3d_SamplerStates[s][(int) D3DSAMP_MAGFILTER]);
		d3d_Device->SetSamplerState (s, D3DSAMP_MINFILTER, d3d_SamplerStates[s][(int) D3DSAMP_MINFILTER]);
		d3d_Device->SetSamplerState (s, D3DSAMP_MIPFILTER, d3d_SamplerStates[s][(int) D3DSAMP_MIPFILTER]);
		d3d_Device->SetSamplerState (s, D3DSAMP_MIPMAPLODBIAS, d3d_SamplerStates[s][(int) D3DSAMP_MIPMAPLODBIAS]);
		d3d_Device->SetSamplerState (s, D3DSAMP_MAXMIPLEVEL, d3d_SamplerStates[s][(int) D3DSAMP_MAXMIPLEVEL]);
		d3d_Device->SetSamplerState (s, D3DSAMP_MAXANISOTROPY, d3d_SamplerStates[s][(int) D3DSAMP_MAXANISOTROPY]);
		d3d_Device->SetSamplerState (s, D3DSAMP_SRGBTEXTURE, d3d_SamplerStates[s][(int) D3DSAMP_SRGBTEXTURE]);
		d3d_Device->SetSamplerState (s, D3DSAMP_ELEMENTINDEX, d3d_SamplerStates[s][(int) D3DSAMP_ELEMENTINDEX]);
		d3d_Device->SetSamplerState (s, D3DSAMP_DMAPOFFSET, d3d_SamplerStates[s][(int) D3DSAMP_DMAPOFFSET]);

		d3d_Device->SetTextureStageState (s, D3DTSS_COLOROP, d3d_TextureStageStates[s][(int) D3DTSS_COLOROP]);
		d3d_Device->SetTextureStageState (s, D3DTSS_COLORARG1, d3d_TextureStageStates[s][(int) D3DTSS_COLORARG1]);
		d3d_Device->SetTextureStageState (s, D3DTSS_COLORARG2, d3d_TextureStageStates[s][(int) D3DTSS_COLORARG2]);
		d3d_Device->SetTextureStageState (s, D3DTSS_ALPHAOP, d3d_TextureStageStates[s][(int) D3DTSS_ALPHAOP]);
		d3d_Device->SetTextureStageState (s, D3DTSS_ALPHAARG1, d3d_TextureStageStates[s][(int) D3DTSS_ALPHAARG1]);
		d3d_Device->SetTextureStageState (s, D3DTSS_ALPHAARG2, d3d_TextureStageStates[s][(int) D3DTSS_ALPHAARG2]);
		d3d_Device->SetTextureStageState (s, D3DTSS_BUMPENVMAT00, d3d_TextureStageStates[s][(int) D3DTSS_BUMPENVMAT00]);
		d3d_Device->SetTextureStageState (s, D3DTSS_BUMPENVMAT01, d3d_TextureStageStates[s][(int) D3DTSS_BUMPENVMAT01]);
		d3d_Device->SetTextureStageState (s, D3DTSS_BUMPENVMAT10, d3d_TextureStageStates[s][(int) D3DTSS_BUMPENVMAT10]);
		d3d_Device->SetTextureStageState (s, D3DTSS_BUMPENVMAT11, d3d_TextureStageStates[s][(int) D3DTSS_BUMPENVMAT11]);
		d3d_Device->SetTextureStageState (s, D3DTSS_TEXCOORDINDEX, d3d_TextureStageStates[s][(int) D3DTSS_TEXCOORDINDEX]);
		d3d_Device->SetTextureStageState (s, D3DTSS_BUMPENVLSCALE, d3d_TextureStageStates[s][(int) D3DTSS_BUMPENVLSCALE]);
		d3d_Device->SetTextureStageState (s, D3DTSS_BUMPENVLOFFSET, d3d_TextureStageStates[s][(int) D3DTSS_BUMPENVLOFFSET]);
		d3d_Device->SetTextureStageState (s, D3DTSS_TEXTURETRANSFORMFLAGS, d3d_TextureStageStates[s][(int) D3DTSS_TEXTURETRANSFORMFLAGS]);
		d3d_Device->SetTextureStageState (s, D3DTSS_COLORARG0, d3d_TextureStageStates[s][(int) D3DTSS_COLORARG0]);
		d3d_Device->SetTextureStageState (s, D3DTSS_ALPHAARG0, d3d_TextureStageStates[s][(int) D3DTSS_ALPHAARG0]);
		d3d_Device->SetTextureStageState (s, D3DTSS_RESULTARG, d3d_TextureStageStates[s][(int) D3DTSS_RESULTARG]);

		d3d_StageTextures[s] = NULL;
	}

	d3d_FVF = D3DFVF_XYZ | D3DFVF_XYZRHW;
}


void D3D_GetAllStates (void)
{
	d3d_Device->GetRenderState (D3DRS_ZENABLE, &d3d_RenderStates[(int) D3DRS_ZENABLE]);
	d3d_Device->GetRenderState (D3DRS_FILLMODE, &d3d_RenderStates[(int) D3DRS_FILLMODE]);
	d3d_Device->GetRenderState (D3DRS_SHADEMODE, &d3d_RenderStates[(int) D3DRS_SHADEMODE]);
	d3d_Device->GetRenderState (D3DRS_ZWRITEENABLE, &d3d_RenderStates[(int) D3DRS_ZWRITEENABLE]);
	d3d_Device->GetRenderState (D3DRS_ALPHATESTENABLE, &d3d_RenderStates[(int) D3DRS_ALPHATESTENABLE]);
	d3d_Device->GetRenderState (D3DRS_LASTPIXEL, &d3d_RenderStates[(int) D3DRS_LASTPIXEL]);
	d3d_Device->GetRenderState (D3DRS_SRCBLEND, &d3d_RenderStates[(int) D3DRS_SRCBLEND]);
	d3d_Device->GetRenderState (D3DRS_DESTBLEND, &d3d_RenderStates[(int) D3DRS_DESTBLEND]);
	d3d_Device->GetRenderState (D3DRS_CULLMODE, &d3d_RenderStates[(int) D3DRS_CULLMODE]);
	d3d_Device->GetRenderState (D3DRS_ZFUNC, &d3d_RenderStates[(int) D3DRS_ZFUNC]);
	d3d_Device->GetRenderState (D3DRS_ALPHAREF, &d3d_RenderStates[(int) D3DRS_ALPHAREF]);
	d3d_Device->GetRenderState (D3DRS_ALPHAFUNC, &d3d_RenderStates[(int) D3DRS_ALPHAFUNC]);
	d3d_Device->GetRenderState (D3DRS_DITHERENABLE, &d3d_RenderStates[(int) D3DRS_DITHERENABLE]);
	d3d_Device->GetRenderState (D3DRS_ALPHABLENDENABLE, &d3d_RenderStates[(int) D3DRS_ALPHABLENDENABLE]);
	d3d_Device->GetRenderState (D3DRS_FOGENABLE, &d3d_RenderStates[(int) D3DRS_FOGENABLE]);
	d3d_Device->GetRenderState (D3DRS_SPECULARENABLE, &d3d_RenderStates[(int) D3DRS_SPECULARENABLE]);
	d3d_Device->GetRenderState (D3DRS_FOGCOLOR, &d3d_RenderStates[(int) D3DRS_FOGCOLOR]);
	d3d_Device->GetRenderState (D3DRS_FOGTABLEMODE, &d3d_RenderStates[(int) D3DRS_FOGTABLEMODE]);
	d3d_Device->GetRenderState (D3DRS_FOGSTART, &d3d_RenderStates[(int) D3DRS_FOGSTART]);
	d3d_Device->GetRenderState (D3DRS_FOGEND, &d3d_RenderStates[(int) D3DRS_FOGEND]);
	d3d_Device->GetRenderState (D3DRS_FOGDENSITY, &d3d_RenderStates[(int) D3DRS_FOGDENSITY]);
	d3d_Device->GetRenderState (D3DRS_RANGEFOGENABLE, &d3d_RenderStates[(int) D3DRS_RANGEFOGENABLE]);
	d3d_Device->GetRenderState (D3DRS_STENCILENABLE, &d3d_RenderStates[(int) D3DRS_STENCILENABLE]);
	d3d_Device->GetRenderState (D3DRS_STENCILFAIL, &d3d_RenderStates[(int) D3DRS_STENCILFAIL]);
	d3d_Device->GetRenderState (D3DRS_STENCILZFAIL, &d3d_RenderStates[(int) D3DRS_STENCILZFAIL]);
	d3d_Device->GetRenderState (D3DRS_STENCILPASS, &d3d_RenderStates[(int) D3DRS_STENCILPASS]);
	d3d_Device->GetRenderState (D3DRS_STENCILFUNC, &d3d_RenderStates[(int) D3DRS_STENCILFUNC]);
	d3d_Device->GetRenderState (D3DRS_STENCILREF, &d3d_RenderStates[(int) D3DRS_STENCILREF]);
	d3d_Device->GetRenderState (D3DRS_STENCILMASK, &d3d_RenderStates[(int) D3DRS_STENCILMASK]);
	d3d_Device->GetRenderState (D3DRS_STENCILWRITEMASK, &d3d_RenderStates[(int) D3DRS_STENCILWRITEMASK]);
	d3d_Device->GetRenderState (D3DRS_TEXTUREFACTOR, &d3d_RenderStates[(int) D3DRS_TEXTUREFACTOR]);
	d3d_Device->GetRenderState (D3DRS_WRAP0, &d3d_RenderStates[(int) D3DRS_WRAP0]);
	d3d_Device->GetRenderState (D3DRS_WRAP1, &d3d_RenderStates[(int) D3DRS_WRAP1]);
	d3d_Device->GetRenderState (D3DRS_WRAP2, &d3d_RenderStates[(int) D3DRS_WRAP2]);
	d3d_Device->GetRenderState (D3DRS_WRAP3, &d3d_RenderStates[(int) D3DRS_WRAP3]);
	d3d_Device->GetRenderState (D3DRS_WRAP4, &d3d_RenderStates[(int) D3DRS_WRAP4]);
	d3d_Device->GetRenderState (D3DRS_WRAP5, &d3d_RenderStates[(int) D3DRS_WRAP5]);
	d3d_Device->GetRenderState (D3DRS_WRAP6, &d3d_RenderStates[(int) D3DRS_WRAP6]);
	d3d_Device->GetRenderState (D3DRS_WRAP7, &d3d_RenderStates[(int) D3DRS_WRAP7]);
	d3d_Device->GetRenderState (D3DRS_CLIPPING, &d3d_RenderStates[(int) D3DRS_CLIPPING]);
	d3d_Device->GetRenderState (D3DRS_LIGHTING, &d3d_RenderStates[(int) D3DRS_LIGHTING]);
	d3d_Device->GetRenderState (D3DRS_AMBIENT, &d3d_RenderStates[(int) D3DRS_AMBIENT]);
	d3d_Device->GetRenderState (D3DRS_FOGVERTEXMODE, &d3d_RenderStates[(int) D3DRS_FOGVERTEXMODE]);
	d3d_Device->GetRenderState (D3DRS_COLORVERTEX, &d3d_RenderStates[(int) D3DRS_COLORVERTEX]);
	d3d_Device->GetRenderState (D3DRS_LOCALVIEWER, &d3d_RenderStates[(int) D3DRS_LOCALVIEWER]);
	d3d_Device->GetRenderState (D3DRS_NORMALIZENORMALS, &d3d_RenderStates[(int) D3DRS_NORMALIZENORMALS]);
	d3d_Device->GetRenderState (D3DRS_DIFFUSEMATERIALSOURCE, &d3d_RenderStates[(int) D3DRS_DIFFUSEMATERIALSOURCE]);
	d3d_Device->GetRenderState (D3DRS_SPECULARMATERIALSOURCE, &d3d_RenderStates[(int) D3DRS_SPECULARMATERIALSOURCE]);
	d3d_Device->GetRenderState (D3DRS_AMBIENTMATERIALSOURCE, &d3d_RenderStates[(int) D3DRS_AMBIENTMATERIALSOURCE]);
	d3d_Device->GetRenderState (D3DRS_EMISSIVEMATERIALSOURCE, &d3d_RenderStates[(int) D3DRS_EMISSIVEMATERIALSOURCE]);
	d3d_Device->GetRenderState (D3DRS_VERTEXBLEND, &d3d_RenderStates[(int) D3DRS_VERTEXBLEND]);
	d3d_Device->GetRenderState (D3DRS_CLIPPLANEENABLE, &d3d_RenderStates[(int) D3DRS_CLIPPLANEENABLE]);
	d3d_Device->GetRenderState (D3DRS_POINTSIZE, &d3d_RenderStates[(int) D3DRS_POINTSIZE]);
	d3d_Device->GetRenderState (D3DRS_POINTSIZE_MIN, &d3d_RenderStates[(int) D3DRS_POINTSIZE_MIN]);
	d3d_Device->GetRenderState (D3DRS_POINTSPRITEENABLE, &d3d_RenderStates[(int) D3DRS_POINTSPRITEENABLE]);
	d3d_Device->GetRenderState (D3DRS_POINTSCALEENABLE, &d3d_RenderStates[(int) D3DRS_POINTSCALEENABLE]);
	d3d_Device->GetRenderState (D3DRS_POINTSCALE_A, &d3d_RenderStates[(int) D3DRS_POINTSCALE_A]);
	d3d_Device->GetRenderState (D3DRS_POINTSCALE_B, &d3d_RenderStates[(int) D3DRS_POINTSCALE_B]);
	d3d_Device->GetRenderState (D3DRS_POINTSCALE_C, &d3d_RenderStates[(int) D3DRS_POINTSCALE_C]);
	d3d_Device->GetRenderState (D3DRS_MULTISAMPLEANTIALIAS, &d3d_RenderStates[(int) D3DRS_MULTISAMPLEANTIALIAS]);
	d3d_Device->GetRenderState (D3DRS_MULTISAMPLEMASK, &d3d_RenderStates[(int) D3DRS_MULTISAMPLEMASK]);
	d3d_Device->GetRenderState (D3DRS_PATCHEDGESTYLE, &d3d_RenderStates[(int) D3DRS_PATCHEDGESTYLE]);
	d3d_Device->GetRenderState (D3DRS_DEBUGMONITORTOKEN, &d3d_RenderStates[(int) D3DRS_DEBUGMONITORTOKEN]);
	d3d_Device->GetRenderState (D3DRS_POINTSIZE_MAX, &d3d_RenderStates[(int) D3DRS_POINTSIZE_MAX]);
	d3d_Device->GetRenderState (D3DRS_INDEXEDVERTEXBLENDENABLE, &d3d_RenderStates[(int) D3DRS_INDEXEDVERTEXBLENDENABLE]);
	d3d_Device->GetRenderState (D3DRS_COLORWRITEENABLE, &d3d_RenderStates[(int) D3DRS_COLORWRITEENABLE]);
	d3d_Device->GetRenderState (D3DRS_TWEENFACTOR, &d3d_RenderStates[(int) D3DRS_TWEENFACTOR]);
	d3d_Device->GetRenderState (D3DRS_BLENDOP, &d3d_RenderStates[(int) D3DRS_BLENDOP]);
	d3d_Device->GetRenderState (D3DRS_POSITIONDEGREE, &d3d_RenderStates[(int) D3DRS_POSITIONDEGREE]);
	d3d_Device->GetRenderState (D3DRS_NORMALDEGREE, &d3d_RenderStates[(int) D3DRS_NORMALDEGREE]);
	d3d_Device->GetRenderState (D3DRS_SCISSORTESTENABLE, &d3d_RenderStates[(int) D3DRS_SCISSORTESTENABLE]);
	d3d_Device->GetRenderState (D3DRS_SLOPESCALEDEPTHBIAS, &d3d_RenderStates[(int) D3DRS_SLOPESCALEDEPTHBIAS]);
	d3d_Device->GetRenderState (D3DRS_ANTIALIASEDLINEENABLE, &d3d_RenderStates[(int) D3DRS_ANTIALIASEDLINEENABLE]);
	d3d_Device->GetRenderState (D3DRS_MINTESSELLATIONLEVEL, &d3d_RenderStates[(int) D3DRS_MINTESSELLATIONLEVEL]);
	d3d_Device->GetRenderState (D3DRS_MAXTESSELLATIONLEVEL, &d3d_RenderStates[(int) D3DRS_MAXTESSELLATIONLEVEL]);
	d3d_Device->GetRenderState (D3DRS_ADAPTIVETESS_X, &d3d_RenderStates[(int) D3DRS_ADAPTIVETESS_X]);
	d3d_Device->GetRenderState (D3DRS_ADAPTIVETESS_Y, &d3d_RenderStates[(int) D3DRS_ADAPTIVETESS_Y]);
	d3d_Device->GetRenderState (D3DRS_ADAPTIVETESS_Z, &d3d_RenderStates[(int) D3DRS_ADAPTIVETESS_Z]);
	d3d_Device->GetRenderState (D3DRS_ADAPTIVETESS_W, &d3d_RenderStates[(int) D3DRS_ADAPTIVETESS_W]);
	d3d_Device->GetRenderState (D3DRS_ENABLEADAPTIVETESSELLATION, &d3d_RenderStates[(int) D3DRS_ENABLEADAPTIVETESSELLATION]);
	d3d_Device->GetRenderState (D3DRS_TWOSIDEDSTENCILMODE, &d3d_RenderStates[(int) D3DRS_TWOSIDEDSTENCILMODE]);
	d3d_Device->GetRenderState (D3DRS_CCW_STENCILFAIL, &d3d_RenderStates[(int) D3DRS_CCW_STENCILFAIL]);
	d3d_Device->GetRenderState (D3DRS_CCW_STENCILZFAIL, &d3d_RenderStates[(int) D3DRS_CCW_STENCILZFAIL]);
	d3d_Device->GetRenderState (D3DRS_CCW_STENCILPASS, &d3d_RenderStates[(int) D3DRS_CCW_STENCILPASS]);
	d3d_Device->GetRenderState (D3DRS_CCW_STENCILFUNC, &d3d_RenderStates[(int) D3DRS_CCW_STENCILFUNC]);
	d3d_Device->GetRenderState (D3DRS_COLORWRITEENABLE1, &d3d_RenderStates[(int) D3DRS_COLORWRITEENABLE1]);
	d3d_Device->GetRenderState (D3DRS_COLORWRITEENABLE2, &d3d_RenderStates[(int) D3DRS_COLORWRITEENABLE2]);
	d3d_Device->GetRenderState (D3DRS_COLORWRITEENABLE3, &d3d_RenderStates[(int) D3DRS_COLORWRITEENABLE3]);
	d3d_Device->GetRenderState (D3DRS_BLENDFACTOR, &d3d_RenderStates[(int) D3DRS_BLENDFACTOR]);
	d3d_Device->GetRenderState (D3DRS_SRGBWRITEENABLE, &d3d_RenderStates[(int) D3DRS_SRGBWRITEENABLE]);
	d3d_Device->GetRenderState (D3DRS_DEPTHBIAS, &d3d_RenderStates[(int) D3DRS_DEPTHBIAS]);
	d3d_Device->GetRenderState (D3DRS_WRAP8, &d3d_RenderStates[(int) D3DRS_WRAP8]);
	d3d_Device->GetRenderState (D3DRS_WRAP9, &d3d_RenderStates[(int) D3DRS_WRAP9]);
	d3d_Device->GetRenderState (D3DRS_WRAP10, &d3d_RenderStates[(int) D3DRS_WRAP10]);
	d3d_Device->GetRenderState (D3DRS_WRAP11, &d3d_RenderStates[(int) D3DRS_WRAP11]);
	d3d_Device->GetRenderState (D3DRS_WRAP12, &d3d_RenderStates[(int) D3DRS_WRAP12]);
	d3d_Device->GetRenderState (D3DRS_WRAP13, &d3d_RenderStates[(int) D3DRS_WRAP13]);
	d3d_Device->GetRenderState (D3DRS_WRAP14, &d3d_RenderStates[(int) D3DRS_WRAP14]);
	d3d_Device->GetRenderState (D3DRS_WRAP15, &d3d_RenderStates[(int) D3DRS_WRAP15]);
	d3d_Device->GetRenderState (D3DRS_SEPARATEALPHABLENDENABLE, &d3d_RenderStates[(int) D3DRS_SEPARATEALPHABLENDENABLE]);
	d3d_Device->GetRenderState (D3DRS_SRCBLENDALPHA, &d3d_RenderStates[(int) D3DRS_SRCBLENDALPHA]);
	d3d_Device->GetRenderState (D3DRS_DESTBLENDALPHA, &d3d_RenderStates[(int) D3DRS_DESTBLENDALPHA]);
	d3d_Device->GetRenderState (D3DRS_BLENDOPALPHA, &d3d_RenderStates[(int) D3DRS_BLENDOPALPHA]);

	for (int s = 0; s < 8; s++)
	{
		d3d_Device->GetSamplerState (s, D3DSAMP_ADDRESSU, &d3d_SamplerStates[s][(int) D3DSAMP_ADDRESSU]);
		d3d_Device->GetSamplerState (s, D3DSAMP_ADDRESSV, &d3d_SamplerStates[s][(int) D3DSAMP_ADDRESSV]);
		d3d_Device->GetSamplerState (s, D3DSAMP_ADDRESSW, &d3d_SamplerStates[s][(int) D3DSAMP_ADDRESSW]);
		d3d_Device->GetSamplerState (s, D3DSAMP_BORDERCOLOR, &d3d_SamplerStates[s][(int) D3DSAMP_BORDERCOLOR]);
		d3d_Device->GetSamplerState (s, D3DSAMP_MAGFILTER, &d3d_SamplerStates[s][(int) D3DSAMP_MAGFILTER]);
		d3d_Device->GetSamplerState (s, D3DSAMP_MINFILTER, &d3d_SamplerStates[s][(int) D3DSAMP_MINFILTER]);
		d3d_Device->GetSamplerState (s, D3DSAMP_MIPFILTER, &d3d_SamplerStates[s][(int) D3DSAMP_MIPFILTER]);
		d3d_Device->GetSamplerState (s, D3DSAMP_MIPMAPLODBIAS, &d3d_SamplerStates[s][(int) D3DSAMP_MIPMAPLODBIAS]);
		d3d_Device->GetSamplerState (s, D3DSAMP_MAXMIPLEVEL, &d3d_SamplerStates[s][(int) D3DSAMP_MAXMIPLEVEL]);
		d3d_Device->GetSamplerState (s, D3DSAMP_MAXANISOTROPY, &d3d_SamplerStates[s][(int) D3DSAMP_MAXANISOTROPY]);
		d3d_Device->GetSamplerState (s, D3DSAMP_SRGBTEXTURE, &d3d_SamplerStates[s][(int) D3DSAMP_SRGBTEXTURE]);
		d3d_Device->GetSamplerState (s, D3DSAMP_ELEMENTINDEX, &d3d_SamplerStates[s][(int) D3DSAMP_ELEMENTINDEX]);
		d3d_Device->GetSamplerState (s, D3DSAMP_DMAPOFFSET, &d3d_SamplerStates[s][(int) D3DSAMP_DMAPOFFSET]);

		d3d_Device->GetTextureStageState (s, D3DTSS_COLOROP, &d3d_TextureStageStates[s][(int) D3DTSS_COLOROP]);
		d3d_Device->GetTextureStageState (s, D3DTSS_COLORARG1, &d3d_TextureStageStates[s][(int) D3DTSS_COLORARG1]);
		d3d_Device->GetTextureStageState (s, D3DTSS_COLORARG2, &d3d_TextureStageStates[s][(int) D3DTSS_COLORARG2]);
		d3d_Device->GetTextureStageState (s, D3DTSS_ALPHAOP, &d3d_TextureStageStates[s][(int) D3DTSS_ALPHAOP]);
		d3d_Device->GetTextureStageState (s, D3DTSS_ALPHAARG1, &d3d_TextureStageStates[s][(int) D3DTSS_ALPHAARG1]);
		d3d_Device->GetTextureStageState (s, D3DTSS_ALPHAARG2, &d3d_TextureStageStates[s][(int) D3DTSS_ALPHAARG2]);
		d3d_Device->GetTextureStageState (s, D3DTSS_BUMPENVMAT00, &d3d_TextureStageStates[s][(int) D3DTSS_BUMPENVMAT00]);
		d3d_Device->GetTextureStageState (s, D3DTSS_BUMPENVMAT01, &d3d_TextureStageStates[s][(int) D3DTSS_BUMPENVMAT01]);
		d3d_Device->GetTextureStageState (s, D3DTSS_BUMPENVMAT10, &d3d_TextureStageStates[s][(int) D3DTSS_BUMPENVMAT10]);
		d3d_Device->GetTextureStageState (s, D3DTSS_BUMPENVMAT11, &d3d_TextureStageStates[s][(int) D3DTSS_BUMPENVMAT11]);
		d3d_Device->GetTextureStageState (s, D3DTSS_TEXCOORDINDEX, &d3d_TextureStageStates[s][(int) D3DTSS_TEXCOORDINDEX]);
		d3d_Device->GetTextureStageState (s, D3DTSS_BUMPENVLSCALE, &d3d_TextureStageStates[s][(int) D3DTSS_BUMPENVLSCALE]);
		d3d_Device->GetTextureStageState (s, D3DTSS_BUMPENVLOFFSET, &d3d_TextureStageStates[s][(int) D3DTSS_BUMPENVLOFFSET]);
		d3d_Device->GetTextureStageState (s, D3DTSS_TEXTURETRANSFORMFLAGS, &d3d_TextureStageStates[s][(int) D3DTSS_TEXTURETRANSFORMFLAGS]);
		d3d_Device->GetTextureStageState (s, D3DTSS_COLORARG0, &d3d_TextureStageStates[s][(int) D3DTSS_COLORARG0]);
		d3d_Device->GetTextureStageState (s, D3DTSS_ALPHAARG0, &d3d_TextureStageStates[s][(int) D3DTSS_ALPHAARG0]);
		d3d_Device->GetTextureStageState (s, D3DTSS_RESULTARG, &d3d_TextureStageStates[s][(int) D3DTSS_RESULTARG]);

		d3d_StageTextures[s] = NULL;
	}

	d3d_FVF = D3DFVF_XYZ | D3DFVF_XYZRHW;
}


void D3D_SetDefaultStates (void)
{
	// init all render states
	D3D_GetAllStates ();

	// disable lighting
	for (int l = 0; l < 8; l++) d3d_Device->LightEnable (l, FALSE);

	D3D_SetRenderState (D3DRS_LIGHTING, FALSE);
}


/*
========================================================================================================================

	BATCHED UP STATES

========================================================================================================================
*/


void D3D_EnableAlphaBlend (DWORD blendop, DWORD srcfactor, DWORD dstfactor)
{
	D3D_SetRenderState (D3DRS_ALPHABLENDENABLE, TRUE);
	D3D_SetRenderState (D3DRS_BLENDOP, blendop);
	D3D_SetRenderState (D3DRS_SRCBLEND, srcfactor);
	D3D_SetRenderState (D3DRS_DESTBLEND, dstfactor);
}


void D3D_DisableAlphaBlend (void)
{
	D3D_SetRenderState (D3DRS_ALPHABLENDENABLE, FALSE);
}


void D3D_BackfaceCull (DWORD D3D_CULLTYPE)
{
	// culling passes through here instead of direct so that we can test the gl_cull cvar
	if (!gl_cull.value)
		D3D_SetRenderState (D3DRS_CULLMODE, D3DCULL_NONE);
	else D3D_SetRenderState (D3DRS_CULLMODE, D3D_CULLTYPE);
}


void D3D_SetTexCoordIndexes (DWORD tmu0index, DWORD tmu1index, DWORD tmu2index)
{
	D3D_SetTextureState (0, D3DTSS_TEXCOORDINDEX, tmu0index);
	D3D_SetTextureState (1, D3DTSS_TEXCOORDINDEX, tmu1index);
	D3D_SetTextureState (2, D3DTSS_TEXCOORDINDEX, tmu2index);
}


void D3D_SetTextureAddressMode (DWORD tmu0mode, DWORD tmu1mode, DWORD tmu2mode)
{
	D3D_SetSamplerState (0, D3DSAMP_ADDRESSU, tmu0mode);
	D3D_SetSamplerState (0, D3DSAMP_ADDRESSV, tmu0mode);

	D3D_SetSamplerState (1, D3DSAMP_ADDRESSU, tmu1mode);
	D3D_SetSamplerState (1, D3DSAMP_ADDRESSV, tmu1mode);

	D3D_SetSamplerState (2, D3DSAMP_ADDRESSU, tmu2mode);
	D3D_SetSamplerState (2, D3DSAMP_ADDRESSV, tmu2mode);
}


void D3D_SetTextureFilter (DWORD stage, D3DSAMPLERSTATETYPE type, D3DTEXTUREFILTERTYPE desired)
{
	D3DTEXTUREFILTERTYPE actual = D3DTEXF_LINEAR;

	switch (type)
	{
	case D3DSAMP_MAGFILTER:
		switch (desired)
		{
		case D3DTEXF_ANISOTROPIC:
			if (d3d_DeviceCaps.TextureFilterCaps & D3DPTFILTERCAPS_MAGFANISOTROPIC)
			{
				actual = D3DTEXF_ANISOTROPIC;
				break;
			}

		case D3DTEXF_LINEAR:
			if (d3d_DeviceCaps.TextureFilterCaps & D3DPTFILTERCAPS_MAGFLINEAR)
			{
				actual = D3DTEXF_LINEAR;
				break;
			}

		default:
			if (d3d_DeviceCaps.TextureFilterCaps & D3DPTFILTERCAPS_MAGFPOINT)
			{
				actual = D3DTEXF_POINT;
				break;
			}

			actual = D3DTEXF_NONE;
		}

		break;

	case D3DSAMP_MINFILTER:
		switch (desired)
		{
		case D3DTEXF_ANISOTROPIC:
			if (d3d_DeviceCaps.TextureFilterCaps & D3DPTFILTERCAPS_MINFANISOTROPIC)
			{
				actual = D3DTEXF_ANISOTROPIC;
				break;
			}

		case D3DTEXF_LINEAR:
			if (d3d_DeviceCaps.TextureFilterCaps & D3DPTFILTERCAPS_MINFLINEAR)
			{
				actual = D3DTEXF_LINEAR;
				break;
			}

		default:
			if (d3d_DeviceCaps.TextureFilterCaps & D3DPTFILTERCAPS_MINFPOINT)
			{
				actual = D3DTEXF_POINT;
				break;
			}

			actual = D3DTEXF_NONE;
		}

		break;


	case D3DSAMP_MIPFILTER:
		switch (desired)
		{
		case D3DTEXF_ANISOTROPIC:
			// there's no such thing as an anisotropic mip filter
		case D3DTEXF_LINEAR:
			if (d3d_DeviceCaps.TextureFilterCaps & D3DPTFILTERCAPS_MIPFLINEAR)
			{
				actual = D3DTEXF_LINEAR;
				break;
			}

		default:
			if (d3d_DeviceCaps.TextureFilterCaps & D3DPTFILTERCAPS_MIPFPOINT)
			{
				actual = D3DTEXF_POINT;
				break;
			}

			actual = D3DTEXF_NONE;
		}

		break;

	default:
		return;
	}

	D3D_SetSamplerState (stage, type, actual);
}


void D3D_SetTextureMipmap (DWORD stage, D3DTEXTUREFILTERTYPE magfilter, D3DTEXTUREFILTERTYPE minfilter, D3DTEXTUREFILTERTYPE mipfilter)
{
	D3D_SetTextureFilter (stage, D3DSAMP_MAGFILTER, magfilter);
	D3D_SetTextureFilter (stage, D3DSAMP_MINFILTER, minfilter);
	D3D_SetTextureFilter (stage, D3DSAMP_MIPFILTER, mipfilter);
}


void D3D_SetTextureMatrixOp (DWORD tmu0op, DWORD tmu1op, DWORD tmu2op)
{
	D3D_SetTextureState (0, D3DTSS_TEXTURETRANSFORMFLAGS, tmu0op);
	D3D_SetTextureState (1, D3DTSS_TEXTURETRANSFORMFLAGS, tmu1op);
	D3D_SetTextureState (2, D3DTSS_TEXTURETRANSFORMFLAGS, tmu2op);
}


void D3D_SetTextureColorMode (DWORD stage, DWORD mode, DWORD arg1, DWORD arg2)
{
	D3D_SetTextureState (stage, D3DTSS_COLOROP, mode);
	D3D_SetTextureState (stage, D3DTSS_COLORARG1, arg1);
	D3D_SetTextureState (stage, D3DTSS_COLORARG2, arg2);
}


void D3D_SetTextureAlphaMode (DWORD stage, DWORD mode, DWORD arg1, DWORD arg2)
{
	D3D_SetTextureState (stage, D3DTSS_ALPHAOP, mode);
	D3D_SetTextureState (stage, D3DTSS_ALPHAARG1, arg1);
	D3D_SetTextureState (stage, D3DTSS_ALPHAARG2, arg2);
}


