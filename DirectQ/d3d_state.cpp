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
#include "d3d_quake.h"

#include "winquake.h"

extern bool d3d_ForceStateUpdates;

void D3D_CheckDirtyMatrixes (void)
{
	d3d_ViewMatrixStack->CheckDirtyState ();
	d3d_ProjMatrixStack->CheckDirtyState ();
	d3d_WorldMatrixStack->CheckDirtyState ();
}


/*
===================================================================================================================

		STATE MANAGEMENT

	Here Direct3D makes things really easy for us by using a separate enum and separate member functions for
	each type of state.  This would be a fucking nightmare under OpenGL...

	Note that the runtime will filter state changes on a non-pure device, but we'll also do it ourselves to
	avoid the overhead of sending them to the runtime in the first place, which may be more optimal

===================================================================================================================
*/

int NumFilteredStates = 0;
int NumChangedStates = 0;


DWORD d3d_RenderStates[256];

void D3D_SetRenderState (D3DRENDERSTATETYPE State, DWORD Value)
{
	if (d3d_RenderStates[(int) State] == Value)
	{
		// filter out rendundant changes before they go to the API
		NumFilteredStates++;
		return;
	}
	else
	{
		d3d_Device->SetRenderState (State, Value);
		d3d_RenderStates[(int) State] = Value;
		NumChangedStates++;
	}
}


void D3D_SetRenderStatef (D3DRENDERSTATETYPE State, float Value)
{
	// some states require float input
	D3D_SetRenderState (State, ((DWORD *) &Value)[0]);
}


DWORD d3d_TextureStageStates[8][64];

void D3D_SetTextureStageState (DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value)
{
	if (d3d_TextureStageStates[Stage][(int) Type] == Value)
	{
		// filter out rendundant changes before they go to the API
		NumFilteredStates++;
		return;
	}
	else
	{
		d3d_Device->SetTextureStageState (Stage, Type, Value);
		d3d_TextureStageStates[Stage][(int) Type] = Value;
		NumChangedStates++;
	}
}


void D3D_SetTextureStageStatef (DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, float Value)
{
	// some states require float input
	D3D_SetTextureStageState (Stage, Type, ((DWORD *) &Value)[0]);
}


DWORD d3d_SamplerStates[8][64];

void D3D_SetSamplerState (DWORD Stage, D3DSAMPLERSTATETYPE Type, DWORD Value)
{
	if (d3d_SamplerStates[Stage][(int) Type] == Value)
	{
		// filter out rendundant changes before they go to the API
		NumFilteredStates++;
		return;
	}
	else
	{
		d3d_Device->SetSamplerState (Stage, Type, Value);
		d3d_SamplerStates[Stage][(int) Type] = Value;
		NumChangedStates++;
	}
}


void D3D_SetSamplerStatef (DWORD Stage, D3DSAMPLERSTATETYPE Type, float Value)
{
	// some states require float input
	D3D_SetSamplerState (Stage, Type, ((DWORD *) &Value)[0]);
}


LPDIRECT3DTEXTURE9 d3d_Textures[8];

void D3D_SetTexture (DWORD Sampler, LPDIRECT3DTEXTURE9 pTexture)
{
	if (d3d_Textures[Sampler] == pTexture)
	{
		// filter out rendundant changes before they go to the API
		NumFilteredStates++;
		return;
	}
	else
	{
		d3d_Device->SetTexture (Sampler, pTexture);
		d3d_Textures[Sampler] = pTexture;
		NumChangedStates++;
	}
}

// init to an impossible FVF to ensure a change is triggered on first use
DWORD d3d_FVF = D3DFVF_XYZ | D3DFVF_XYZRHW;

void D3D_SetFVF (DWORD FVF)
{
	if (d3d_FVF == FVF)
	{
		// filter out rendundant changes before they go to the API
		NumFilteredStates++;
		return;
	}
	else
	{
		d3d_Device->SetFVF (FVF);
		d3d_FVF = FVF;
		NumChangedStates++;
	}
}


void D3D_SetDefaultStates (void)
{
	// clear down old matrix stacks
	if (d3d_ViewMatrixStack) {delete d3d_ViewMatrixStack; d3d_ViewMatrixStack = NULL;}
	if (d3d_ProjMatrixStack) {delete d3d_ProjMatrixStack; d3d_ProjMatrixStack = NULL;}
	if (d3d_WorldMatrixStack) {delete d3d_WorldMatrixStack; d3d_WorldMatrixStack = NULL;}

	// create new matrix stacks
	d3d_ViewMatrixStack = new CD3D_MatrixStack (D3DTS_VIEW);
	d3d_ProjMatrixStack = new CD3D_MatrixStack (D3DTS_PROJECTION);
	d3d_WorldMatrixStack = new CD3D_MatrixStack (D3DTS_WORLD);

	// init all render states
	for (int i = 0; i < 256; i++)
	{
		DWORD rs;

		// we expect this to fail on some of the states as not every number from 0 to 255 is a render state!
		hr = d3d_Device->GetRenderState ((D3DRENDERSTATETYPE) i, &rs);

		if (SUCCEEDED (hr))
		{
			// cache something invalid in there
			d3d_RenderStates[i] = (rs + 1);

			// now send it through the manager to get a valid default forced
			D3D_SetRenderState ((D3DRENDERSTATETYPE) i, rs);
		}
	}

	for (int s = 0; s < 8; s++)
	{
		DWORD ss;

		for (int i = 0; i < 64; i++)
		{
			// texture stage
			hr = d3d_Device->GetTextureStageState (s, (D3DTEXTURESTAGESTATETYPE) i, &ss);

			if (SUCCEEDED (hr))
			{
				// cache something invalid in there
				d3d_TextureStageStates[s][i] = (ss + 1);

				// now send it through the manager to get a valid default forced
				D3D_SetTextureStageState (s, (D3DTEXTURESTAGESTATETYPE) i, ss);
			}

			// sampler
			hr = d3d_Device->GetSamplerState (s, (D3DSAMPLERSTATETYPE) i, &ss);

			if (SUCCEEDED (hr))
			{
				// cache something invalid in there
				d3d_SamplerStates[s][i] = (ss + 1);

				// now send it through the manager to get a valid default forced
				D3D_SetSamplerState (s, (D3DSAMPLERSTATETYPE) i, ss);
			}
		}

		// clear the cached texture too
		d3d_Textures[s] = NULL;
	}

	NumFilteredStates = 0;
	NumChangedStates = 0;

	// now we set all of our states to the dcoumented D3D defaults; this is
	// to protect us against drivers who think they know better than we do.
	// (note: i observed bad defaults coming through on Intel cards here, so the exercise is valid)
	D3D_SetRenderState (D3DRS_ZENABLE, D3DZB_TRUE);
	D3D_SetRenderState (D3DRS_FILLMODE, D3DFILL_SOLID);
	D3D_SetRenderState (D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
	D3D_SetRenderState (D3DRS_ZWRITEENABLE, TRUE);
	D3D_SetRenderState (D3DRS_ALPHATESTENABLE, FALSE);
	D3D_SetRenderState (D3DRS_LASTPIXEL, TRUE);
	D3D_SetRenderState (D3DRS_SRCBLEND, D3DBLEND_ONE);
	D3D_SetRenderState (D3DRS_DESTBLEND, D3DBLEND_ZERO);
	D3D_SetRenderState (D3DRS_CULLMODE, D3DCULL_CCW);
	D3D_SetRenderState (D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	D3D_SetRenderState (D3DRS_ALPHAREF, 0);
	D3D_SetRenderState (D3DRS_ALPHAFUNC, D3DCMP_ALWAYS);
	D3D_SetRenderState (D3DRS_DITHERENABLE, FALSE);
	D3D_SetRenderState (D3DRS_ALPHABLENDENABLE, FALSE);
	D3D_SetRenderState (D3DRS_FOGENABLE, FALSE);
	D3D_SetRenderState (D3DRS_SPECULARENABLE, FALSE);
	D3D_SetRenderState (D3DRS_FOGCOLOR, 0);
	D3D_SetRenderState (D3DRS_FOGTABLEMODE, D3DFOG_NONE);
	D3D_SetRenderStatef (D3DRS_FOGSTART, 0.0f);
	D3D_SetRenderStatef (D3DRS_FOGEND, 1.0f);
	D3D_SetRenderStatef (D3DRS_FOGDENSITY, 1.0f);
	D3D_SetRenderState (D3DRS_RANGEFOGENABLE, FALSE);
	D3D_SetRenderState (D3DRS_STENCILENABLE, FALSE);
	D3D_SetRenderState (D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP);
	D3D_SetRenderState (D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP);
	D3D_SetRenderState (D3DRS_STENCILPASS, D3DSTENCILOP_KEEP);
	D3D_SetRenderState (D3DRS_STENCILFUNC, D3DCMP_ALWAYS);
	D3D_SetRenderState (D3DRS_STENCILREF, 0);
	D3D_SetRenderState (D3DRS_STENCILMASK, 0xFFFFFFFF);
	D3D_SetRenderState (D3DRS_STENCILWRITEMASK, 0xFFFFFFFF);
	D3D_SetRenderState (D3DRS_TEXTUREFACTOR, 0xFFFFFFFF);
	D3D_SetRenderState (D3DRS_WRAP0, 0);
	D3D_SetRenderState (D3DRS_WRAP1, 0);
	D3D_SetRenderState (D3DRS_WRAP2, 0);
	D3D_SetRenderState (D3DRS_WRAP3, 0);
	D3D_SetRenderState (D3DRS_WRAP4, 0);
	D3D_SetRenderState (D3DRS_WRAP5, 0);
	D3D_SetRenderState (D3DRS_WRAP6, 0);
	D3D_SetRenderState (D3DRS_WRAP7, 0);
	D3D_SetRenderState (D3DRS_CLIPPING, TRUE);
	D3D_SetRenderState (D3DRS_LIGHTING, TRUE);
	D3D_SetRenderState (D3DRS_AMBIENT, 0);
	D3D_SetRenderState (D3DRS_FOGVERTEXMODE, D3DFOG_NONE);
	D3D_SetRenderState (D3DRS_COLORVERTEX, TRUE);
	D3D_SetRenderState (D3DRS_LOCALVIEWER, TRUE);
	D3D_SetRenderState (D3DRS_NORMALIZENORMALS, FALSE);
	D3D_SetRenderState (D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_COLOR1);
	D3D_SetRenderState (D3DRS_SPECULARMATERIALSOURCE, D3DMCS_COLOR2);
	D3D_SetRenderState (D3DRS_AMBIENTMATERIALSOURCE, D3DMCS_MATERIAL);
	D3D_SetRenderState (D3DRS_EMISSIVEMATERIALSOURCE, D3DMCS_MATERIAL);
	D3D_SetRenderState (D3DRS_VERTEXBLEND, D3DVBF_DISABLE);
	D3D_SetRenderState (D3DRS_CLIPPLANEENABLE, 0);
	D3D_SetRenderStatef (D3DRS_POINTSIZE, 1.0f);
	D3D_SetRenderStatef (D3DRS_POINTSIZE_MIN, 1.0f);
	D3D_SetRenderState (D3DRS_POINTSPRITEENABLE, FALSE);
	D3D_SetRenderState (D3DRS_POINTSCALEENABLE, FALSE);
	D3D_SetRenderStatef (D3DRS_POINTSCALE_A, 1.0f);
	D3D_SetRenderStatef (D3DRS_POINTSCALE_B, 0.0f);
	D3D_SetRenderStatef (D3DRS_POINTSCALE_C, 0.0f);
	D3D_SetRenderState (D3DRS_MULTISAMPLEANTIALIAS, TRUE);
	D3D_SetRenderState (D3DRS_MULTISAMPLEMASK, 0xFFFFFFFF);
	D3D_SetRenderState (D3DRS_PATCHEDGESTYLE, D3DPATCHEDGE_DISCRETE);
	D3D_SetRenderState (D3DRS_DEBUGMONITORTOKEN, D3DDMT_DISABLE);
	D3D_SetRenderStatef (D3DRS_POINTSIZE_MAX, 64.0f);
	D3D_SetRenderState (D3DRS_INDEXEDVERTEXBLENDENABLE, FALSE);
	D3D_SetRenderState (D3DRS_COLORWRITEENABLE, 0x0000000F);
	D3D_SetRenderStatef (D3DRS_TWEENFACTOR, 0.0f);
	D3D_SetRenderState (D3DRS_BLENDOP, D3DBLENDOP_ADD);
	D3D_SetRenderState (D3DRS_POSITIONDEGREE, D3DDEGREE_CUBIC);
	D3D_SetRenderState (D3DRS_NORMALDEGREE, D3DDEGREE_LINEAR);
	D3D_SetRenderState (D3DRS_SCISSORTESTENABLE, FALSE);
	D3D_SetRenderState (D3DRS_SLOPESCALEDEPTHBIAS, 0);
	D3D_SetRenderState (D3DRS_ANTIALIASEDLINEENABLE, FALSE);
	D3D_SetRenderStatef (D3DRS_MINTESSELLATIONLEVEL, 1.0f);
	D3D_SetRenderStatef (D3DRS_MAXTESSELLATIONLEVEL, 1.0f);
	D3D_SetRenderStatef (D3DRS_ADAPTIVETESS_X, 0.0f);
	D3D_SetRenderStatef (D3DRS_ADAPTIVETESS_Y, 0.0f);
	D3D_SetRenderStatef (D3DRS_ADAPTIVETESS_Z, 1.0f);
	D3D_SetRenderStatef (D3DRS_ADAPTIVETESS_W, 0.0f);
	D3D_SetRenderState (D3DRS_ENABLEADAPTIVETESSELLATION, FALSE);
	D3D_SetRenderState (D3DRS_TWOSIDEDSTENCILMODE, FALSE);
	D3D_SetRenderState (D3DRS_CCW_STENCILFAIL, D3DSTENCILOP_KEEP);
	D3D_SetRenderState (D3DRS_CCW_STENCILZFAIL, D3DSTENCILOP_KEEP);
	D3D_SetRenderState (D3DRS_CCW_STENCILPASS, D3DSTENCILOP_KEEP);
	D3D_SetRenderState (D3DRS_CCW_STENCILFUNC, D3DCMP_ALWAYS);
	D3D_SetRenderState (D3DRS_COLORWRITEENABLE1, 0x0000000f);
	D3D_SetRenderState (D3DRS_COLORWRITEENABLE2, 0x0000000f);
	D3D_SetRenderState (D3DRS_COLORWRITEENABLE3, 0x0000000f);
	D3D_SetRenderState (D3DRS_BLENDFACTOR, 0xffffffff);
	D3D_SetRenderState (D3DRS_SRGBWRITEENABLE, 0);
	D3D_SetRenderStatef (D3DRS_DEPTHBIAS, 0);
	D3D_SetRenderState (D3DRS_WRAP8, 0);
	D3D_SetRenderState (D3DRS_WRAP9, 0);
	D3D_SetRenderState (D3DRS_WRAP10, 0);
	D3D_SetRenderState (D3DRS_WRAP11, 0);
	D3D_SetRenderState (D3DRS_WRAP12, 0);
	D3D_SetRenderState (D3DRS_WRAP13, 0);
	D3D_SetRenderState (D3DRS_WRAP14, 0);
	D3D_SetRenderState (D3DRS_WRAP15, 0);
	D3D_SetRenderState (D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
	D3D_SetRenderState (D3DRS_SRCBLENDALPHA, D3DBLEND_ONE);
	D3D_SetRenderState (D3DRS_DESTBLENDALPHA, D3DBLEND_ZERO);
	D3D_SetRenderState (D3DRS_BLENDOPALPHA, D3DBLENDOP_ADD);

	for (int s = 0; s < 8; s++)
	{
		D3D_SetSamplerState (s, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
		D3D_SetSamplerState (s, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
		D3D_SetSamplerState (s, D3DSAMP_ADDRESSW, D3DTADDRESS_WRAP);
		D3D_SetSamplerState (s, D3DSAMP_BORDERCOLOR, 0x00000000);
		D3D_SetSamplerState (s, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
		D3D_SetSamplerState (s, D3DSAMP_MINFILTER, D3DTEXF_POINT);
		D3D_SetSamplerState (s, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
		D3D_SetSamplerState (s, D3DSAMP_MIPMAPLODBIAS, 0);
		D3D_SetSamplerState (s, D3DSAMP_MAXMIPLEVEL, 0);
		D3D_SetSamplerState (s, D3DSAMP_MAXANISOTROPY, 1);
		D3D_SetSamplerState (s, D3DSAMP_SRGBTEXTURE, 0);
		D3D_SetSamplerState (s, D3DSAMP_ELEMENTINDEX, 0);
		D3D_SetSamplerState (s, D3DSAMP_DMAPOFFSET, 0);

		D3D_SetTextureStageState (s, D3DTSS_COLOROP, s == 0 ? D3DTOP_MODULATE : D3DTOP_DISABLE);
		D3D_SetTextureStageState (s, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		D3D_SetTextureStageState (s, D3DTSS_COLORARG2, D3DTA_CURRENT);
		D3D_SetTextureStageState (s, D3DTSS_ALPHAOP, s == 0 ? D3DTOP_SELECTARG1 : D3DTOP_DISABLE);
		D3D_SetTextureStageState (s, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
		D3D_SetTextureStageState (s, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
		D3D_SetTextureStageStatef (s, D3DTSS_BUMPENVMAT00, 0.0f);
		D3D_SetTextureStageStatef (s, D3DTSS_BUMPENVMAT01, 0.0f);
		D3D_SetTextureStageStatef (s, D3DTSS_BUMPENVMAT10, 0.0f);
		D3D_SetTextureStageStatef (s, D3DTSS_BUMPENVMAT11, 0.0f);
		D3D_SetTextureStageState (s, D3DTSS_TEXCOORDINDEX, s);
		D3D_SetTextureStageStatef (s, D3DTSS_BUMPENVLSCALE, 0.0f);
		D3D_SetTextureStageStatef (s, D3DTSS_BUMPENVLOFFSET, 0.0f);
		D3D_SetTextureStageState (s, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
		D3D_SetTextureStageState (s, D3DTSS_COLORARG0, D3DTA_CURRENT);
		D3D_SetTextureStageState (s, D3DTSS_ALPHAARG0, D3DTA_CURRENT);
		D3D_SetTextureStageState (s, D3DTSS_RESULTARG, D3DTA_CURRENT);
		//D3D_SetTextureStageState (s, D3DTSS_CONSTANT = 32,
	}

	if (NumChangedStates) Con_DPrintf ("%i States set back to correct defaults\n", NumChangedStates);

	NumFilteredStates = 0;
	NumChangedStates = 0;

	// disable lighting
	for (int l = 0; l < 8; l++) d3d_Device->LightEnable (l, FALSE);

	D3D_SetRenderState (D3DRS_LIGHTING, FALSE);

	// force updates of values that are checked per-frame
	d3d_ForceStateUpdates = true;
}


/*
========================================================================================================================

	BATCHED UP STATES

========================================================================================================================
*/


void D3D_EnableAlphaBlend (DWORD blendop, DWORD srcfactor, DWORD dstfactor, bool disablezwrite)
{
	if (disablezwrite)
		D3D_SetRenderState (D3DRS_ZWRITEENABLE, FALSE);

	D3D_SetRenderState (D3DRS_ALPHABLENDENABLE, TRUE);
	D3D_SetRenderState (D3DRS_BLENDOP, blendop);
	D3D_SetRenderState (D3DRS_SRCBLEND, srcfactor);
	D3D_SetRenderState (D3DRS_DESTBLEND, dstfactor);
}


void D3D_DisableAlphaBlend (bool enablezwrite)
{
	if (enablezwrite)
		D3D_SetRenderState (D3DRS_ZWRITEENABLE, TRUE);

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
	D3D_SetTextureStageState (0, D3DTSS_TEXCOORDINDEX, tmu0index);
	D3D_SetTextureStageState (1, D3DTSS_TEXCOORDINDEX, tmu1index);
	D3D_SetTextureStageState (2, D3DTSS_TEXCOORDINDEX, tmu2index);
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
	D3D_SetTextureStageState (0, D3DTSS_TEXTURETRANSFORMFLAGS, tmu0op);
	D3D_SetTextureStageState (1, D3DTSS_TEXTURETRANSFORMFLAGS, tmu1op);
	D3D_SetTextureStageState (2, D3DTSS_TEXTURETRANSFORMFLAGS, tmu2op);
}


void D3D_SetTextureColorMode (DWORD stage, DWORD mode, DWORD arg1, DWORD arg2)
{
	D3D_SetTextureStageState (stage, D3DTSS_COLOROP, mode);
	D3D_SetTextureStageState (stage, D3DTSS_COLORARG1, arg1);
	D3D_SetTextureStageState (stage, D3DTSS_COLORARG2, arg2);
}


void D3D_SetTextureAlphaMode (DWORD stage, DWORD mode, DWORD arg1, DWORD arg2)
{
	D3D_SetTextureStageState (stage, D3DTSS_ALPHAOP, mode);
	D3D_SetTextureStageState (stage, D3DTSS_ALPHAARG1, arg1);
	D3D_SetTextureStageState (stage, D3DTSS_ALPHAARG2, arg2);
}

