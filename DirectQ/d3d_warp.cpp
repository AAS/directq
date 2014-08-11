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
// gl_warp.c -- sky and water polygons

#include "quakedef.h"
#include "d3d_model.h"
#include "d3d_quake.h"

extern	model_t	*loadmodel;

LPDIRECT3DTEXTURE9 d3d_3DSceneTexture = NULL;
LPDIRECT3DSURFACE9 d3d_3DSceneSurface = NULL;
LPD3DXRENDERTOSURFACE d3d_RenderToSurface = NULL;
LPDIRECT3DTEXTURE9 simpleskytexture = NULL;
LPDIRECT3DTEXTURE9 solidskytexture = NULL;
LPDIRECT3DTEXTURE9 alphaskytexture = NULL;
LPDIRECT3DTEXTURE9 skyboxtextures[6] = {NULL};

// the menu needs to know the name of the loaded skybox
char CachedSkyBoxName[MAX_PATH] = {0};
bool SkyboxValid = false;

void D3D_AddSurfaceVertToRender (polyvert_t *vert, D3DMATRIX *m = NULL);


/*
==============================================================================================================================

		UNDERWATER WARP

==============================================================================================================================
*/

bool SceneTextureValid = false;
cvar_t r_waterwarp ("r_waterwarp", 1, CVAR_ARCHIVE);
bool d3d_Update3DScene = false;

typedef struct uwvert_s
{
	float x, y, z;
	float s, t;
} uwvert_t;

// some drivers don't obey the stride param to DPUP - sigh.
typedef struct uwbackup_s
{
	float s;
	float t;
} uwbackup_t;

uwvert_t *underwaterverts = NULL;
uwbackup_t *underwaterbackup = NULL;

float CompressST (float in, float tess)
{
	// compress the texture slightly so that the edges of the screen look right
	float out = in;

	// 0 to 1
	out /= tess;

	// 1 to 99
	out *= 98.0f;
	out += 1.0f;

	// back to 0 to 1 scale
	out /= 100.0f;

	// back to 0 to 32 scale
	out *= tess;

	return out;
}


void D3D_Kill3DSceneTexture (void)
{
	// just release it
	SAFE_RELEASE (d3d_3DSceneSurface);
	SAFE_RELEASE (d3d_3DSceneTexture);
	SAFE_RELEASE (d3d_RenderToSurface);
}


void D3D_Init3DSceneTexture (void)
{
	// ensure that it's all gone before creating
	D3D_Kill3DSceneTexture ();

	// assume that it's invalid until we prove otherwise
	SceneTextureValid = false;

	// rendertargets seem to relax power of 2 requirements, best of luck finding THAT in the documentation
	hr = d3d_Device->CreateTexture
	(
		d3d_CurrentMode.Width,
		d3d_CurrentMode.Height,
		1,
		D3DUSAGE_RENDERTARGET,
		d3d_GlobalCaps.supportXRGB ? D3DFMT_X8R8G8B8 : D3DFMT_A8R8G8B8,
		D3DPOOL_DEFAULT,
		&d3d_3DSceneTexture,
		NULL
	);

	// couldn't create it so don't do underwater updates
	if (FAILED (hr))
	{
		// destroy anything that was created
		D3D_Kill3DSceneTexture ();
		return;
	}

	hr = QD3DXCreateRenderToSurface
	(
		d3d_Device,
		d3d_CurrentMode.Width,
		d3d_CurrentMode.Height,
		d3d_CurrentMode.Format,
		TRUE,
		d3d_GlobalCaps.DepthStencilFormat,
		&d3d_RenderToSurface
	);

	// couldn't create it so don't do underwater updates
	if (FAILED (hr))
	{
		// destroy anything that was created
		D3D_Kill3DSceneTexture ();
		return;
	}

	// get the surface that we're going to render to
	hr = d3d_3DSceneTexture->GetSurfaceLevel (0, &d3d_3DSceneSurface);

	// couldn't create it so don't do underwater updates
	if (FAILED (hr))
	{
		// destroy anything that was created
		D3D_Kill3DSceneTexture ();
		return;
	}

	// we're good now
	SceneTextureValid = true;

	// alloc underwater verts one time only
	// store 2 extra s/t so we can cache the original values for updates
	if (!underwaterverts) underwaterverts = (uwvert_t *) Pool_Permanent->Alloc (2112 * sizeof (uwvert_t));
	if (!underwaterbackup) underwaterbackup = (uwbackup_t *) Pool_Permanent->Alloc (2112 * sizeof (uwbackup_t));

	// tesselation for underwater warps
	uwvert_t *wv = underwaterverts;
	uwbackup_t *wb = underwaterbackup;

	float tessw = (float) d3d_CurrentMode.Width / 32.0f;
	float tessh = (float) d3d_CurrentMode.Height / 32.0f;

	// tesselate this so that we can do the sin calcs per vertex
	for (float x = 0, s = 0; x < d3d_CurrentMode.Width; x += tessw, s++)
	{
		for (float y = 0, t = 0; y <= d3d_CurrentMode.Height; y += tessh, t++)
		{
			wv->x = x;
			wv->y = y;
			wv->z = 0;
			wv->s = CompressST (s, 32);
			wv->t = CompressST (t, 32);
			wb->s = wv->s;
			wb->t = wv->t;
			wv++;
			wb++;

			wv->x = (x + tessw);
			wv->y = y;
			wv->z = 0;
			wv->s = CompressST (s + 1, 32);
			wv->t = CompressST (t, 32);
			wb->s = wv->s;
			wb->t = wv->t;
			wv++;
			wb++;
		}
	}
}


bool IsUnderwater (void)
{
	extern cshift_t cshift_empty;

	if (!r_waterwarp.integer) return false;

	// no warps present on these leafs
	if ((d3d_RenderDef.viewleaf->contents == CONTENTS_EMPTY ||
		d3d_RenderDef.viewleaf->contents == CONTENTS_SKY ||
		d3d_RenderDef.viewleaf->contents == CONTENTS_SOLID) &&
		!cl.inwater) return false;

	// additional check for noclipping
	if (d3d_RenderDef.viewleaf->contents == CONTENTS_EMPTY && cshift_empty.percent == 0) return false;

	// skip the first few frames because we get invalid viewleaf contents in them
	if (d3d_RenderDef.framecount < 3) return false;

	// we're underwater now
	return true;
}


bool d3d_IsUnderwater = false;

bool D3D_Begin3DScene (void)
{
	// couldn't create the underwater texture or we don't want to warp
	if (!SceneTextureValid) return false;

	// no viewleaf yet
	if (!d3d_RenderDef.viewleaf) return false;

	d3d_IsUnderwater = IsUnderwater ();

	// no post-processing needed
	if (!v_blend[3] && d3d_GlobalCaps.usingPixelShaders) return false;
	if (!d3d_IsUnderwater && !d3d_GlobalCaps.usingPixelShaders) return false;

	// begin the render to texture scene
	if (FAILED (hr = d3d_RenderToSurface->BeginScene (d3d_3DSceneSurface, NULL))) return false;

	// flag that we need to draw the warp update
	d3d_Update3DScene = true;

	// we're rendering to texture now
	return true;
}


void D3D_End3DScene (void)
{
	// no warps
	if (!d3d_Update3DScene) return;

	d3d_RenderToSurface->EndScene (D3DX_FILTER_NONE);
	d3d_Device->BeginScene ();
	d3d_SceneBegun = true;
}


cvar_t r_waterwarptime ("r_waterwarptime", 2.0f, CVAR_ARCHIVE);
cvar_t r_waterwarpscale ("r_waterwarpscale", 0.125f, CVAR_ARCHIVE);

void D3D_Draw3DSceneToBackbuffer (void)
{
	// no warps
	if (!d3d_Update3DScene) return;

	// for the next frame
	d3d_Update3DScene = false;

	// update time for the underwater warp
	float rdt = cl.time * r_waterwarptime.value * 0.5f;

	// ensure
	D3D_DisableAlphaBlend ();
	D3D_SetRenderState (D3DRS_ZWRITEENABLE, TRUE);

	D3D_SetTextureColorMode (0, D3DTOP_SELECTARG1, D3DTA_TEXTURE, D3DTA_DIFFUSE);
	D3D_SetTextureAlphaMode (0, D3DTOP_DISABLE);

	D3D_SetTextureColorMode (1, D3DTOP_DISABLE);
	D3D_SetTextureAlphaMode (1, D3DTOP_DISABLE);

	D3D_SetTextureColorMode (2, D3DTOP_DISABLE);
	D3D_SetTextureAlphaMode (2, D3DTOP_DISABLE);

	D3D_SetTextureAddressMode (D3DTADDRESS_CLAMP);

	// this is slow by comparison to the fixed pipeline, but we make up for it by getting the underwater blend for free
	if (d3d_GlobalCaps.usingPixelShaders)
	{
		D3D_SetTextureMipmap (0, D3DTEXF_NONE, D3DTEXF_NONE, D3DTEXF_NONE);
		D3D_SetTexCoordIndexes (0, 0, 0);

		UINT numpasses;
		float polyblend[4] = {1, 1, 1, 0};

		// add in the polyblend (gets a fairly large perf boost here)
		if (gl_polyblend.value && v_blend[3])
		{
			polyblend[0] = (float) v_blend[0] / 255.0f;
			polyblend[1] = (float) v_blend[1] / 255.0f;
			polyblend[2] = (float) v_blend[2] / 255.0f;
			polyblend[3] = (float) v_blend[3] / 255.0f;
		}

		// default is a straight passthru with standard texture size
		int passnum = 1;
		int texturescale = 1;

		// using a post-processing shader
		hr = d3d_Device->SetVertexDeclaration (d3d_UnderwaterDeclaration);
		hr = d3d_UnderwaterFX->SetTechnique ("ScreenUpdate");
		hr = d3d_UnderwaterFX->Begin (&numpasses, D3DXFX_DONOTSAVESTATE);
		hr = d3d_UnderwaterFX->SetTexture ("ScreenTexture", d3d_3DSceneTexture);

		if (d3d_IsUnderwater)
		{
			passnum = 0;
			texturescale = 32;

			hr = d3d_UnderwaterFX->SetFloat ("warptime", rdt);
			hr = d3d_UnderwaterFX->SetFloat ("warpscale", r_waterwarpscale.value);
		}
		else if (v_blend[3])
			passnum = 2;

		hr = d3d_UnderwaterFX->SetFloatArray ("polyblend", polyblend, 4);
		hr = d3d_UnderwaterFX->BeginPass (passnum);

		// easier vertex submission
		float verts[6][6] =
		{
			{0, 0, 0, 1, 0, 0},
			{d3d_CurrentMode.Width, 0, 0, 1, texturescale, 0},
			{d3d_CurrentMode.Width, d3d_CurrentMode.Height, 0, 1, texturescale, texturescale},
			{0, 0, 0, 1, 0, 0},
			{d3d_CurrentMode.Width, d3d_CurrentMode.Height, 0, 1, texturescale, texturescale},
			{0, d3d_CurrentMode.Height, 0, 1, 0, texturescale}
		};

		D3D_DrawUserPrimitive (D3DPT_TRIANGLELIST, 2, verts, sizeof (float) * 6);

		d3d_UnderwaterFX->EndPass ();
		d3d_UnderwaterFX->End ();

		d3d_Device->SetVertexShader (NULL);
		d3d_Device->SetPixelShader (NULL);

		D3D_SetFVF (D3DFVF_XYZ | D3DFVF_XYZRHW);
		D3D_SetTexture (0, NULL);

		// disable regular polyblend
		v_blend[3] = 0;

		// done
		return;
	}

	// XYZRHW causes Z-fighting artefacts on alpha-blended overlay surfs, so send it through T&L
	D3DXMATRIX m;
	D3DXMatrixIdentity (&m);
	d3d_Device->SetTransform (D3DTS_VIEW, &m);
	d3d_Device->SetTransform (D3DTS_WORLD, &m);

	QD3DXMatrixOrthoOffCenterRH (&m, 0, d3d_CurrentMode.Width, d3d_CurrentMode.Height, 0, 0, 1);
	d3d_Device->SetTransform (D3DTS_PROJECTION, &m);

	// T&L used - see above
	D3D_SetFVF (D3DFVF_XYZ | D3DFVF_TEX1);
	D3D_SetTexture (0, d3d_3DSceneTexture);

	for (int i = 0, v = 0; i < 32; i++, v += 66)
	{
		// retrieve the current verts
		uwvert_t *wv = underwaterverts + v;
		uwvert_t *verts = wv;

		// and the backup s/t verts
		uwbackup_t *wb = underwaterbackup + v;
		uwbackup_t *backup = wb;

		// there's a potential optimization in here... we could precalc the warps as up to 4 verts will be shared...
		for (int vv = 0; vv < 66; vv++, verts++, backup++)
		{
			// because there are drivers that don't obey the stride param below we keep a backup in a second array.
			verts->s = (backup->s + sin (backup->t + rdt) * r_waterwarpscale.value) * 0.03125f;
			verts->t = (backup->t + sin (backup->s + rdt) * r_waterwarpscale.value) * 0.03125f;
		}

		D3D_DrawUserPrimitive (D3DPT_TRIANGLESTRIP, 64, wv, sizeof (uwvert_t));
	}
}


/*
==============================================================================================================================

		WARP SURFACE GENERATION

==============================================================================================================================
*/

// keep this here but drop it from the archive
cvar_t gl_subdivide_size ("gl_subdivide_size", 128);

void R_GenerateTurbSurface (model_t *mod, msurface_t *surf)
{
	// set up initial verts for the HLSL pass
	polyvert_t *surfverts = (polyvert_t *) Pool_Map->Alloc (surf->numverts * sizeof (polyvert_t));
	surf->verts = surfverts;

	// this pass is also good for getting the midpoint of the surf
	VectorClear (surf->midpoint);

	// copy out base verts for subdividing from
	for (int i = 0; i < surf->numverts; i++, surfverts++)
	{
		int lindex = mod->brushhdr->surfedges[surf->firstedge + i];

		if (lindex > 0)
			surfverts->basevert = mod->brushhdr->vertexes[mod->brushhdr->edges[lindex].v[0]].position;
		else surfverts->basevert = mod->brushhdr->vertexes[mod->brushhdr->edges[-lindex].v[1]].position;

		// setup s/t (don't need to cache a spare copy with these but we do anyway)
		surfverts->st[0] = surfverts->st2[0] = DotProduct (surfverts->basevert, surf->texinfo->vecs[0]);
		surfverts->st[1] = surfverts->st2[1] = DotProduct (surfverts->basevert, surf->texinfo->vecs[1]);

		// accumulate into midpoint
		VectorAdd (surf->midpoint, surfverts->basevert, surf->midpoint);
	}

	// get final midpoint
	VectorScale (surf->midpoint, 1.0f / (float) surf->numverts, surf->midpoint);
}


/*
==============================================================================================================================

		WATER WARP RENDERING

==============================================================================================================================
*/


byte d3d_WaterAlpha = 255;
byte d3d_LavaAlpha = 255;
byte d3d_SlimeAlpha = 255;
byte d3d_TeleAlpha = 255;

cvar_t r_lavaalpha ("r_lavaalpha", 1, CVAR_ARCHIVE);
cvar_t r_telealpha ("r_telealpha", 1, CVAR_ARCHIVE);
cvar_t r_slimealpha ("r_slimealpha", 1, CVAR_ARCHIVE);
cvar_t r_warpspeed ("r_warpspeed", 4, CVAR_ARCHIVE);
cvar_t r_warpscale ("r_warpscale", 8, CVAR_ARCHIVE);
cvar_t r_warpfactor ("r_warpfactor", 2, CVAR_ARCHIVE);

// lock water/slime/tele/lava alpha sliders
cvar_t r_lockalpha ("r_lockalpha", "1", CVAR_ARCHIVE);

float warptime = 10;

float warpsintime;
float warpcostime;

void D3D_InitializeTurb (void)
{
	// sin and cos are calced per vertex for non-HLSL so cache them
	warpsintime = sin (cl.time) * 8.0f;
	warpcostime = cos (cl.time) * 8.0f;

	// store alpha values
	// this needs to be checked first because the actual values we'll use depend on whether or not the sliders are locked
	// multiply by 256 to prevent float rounding errors
	if (r_lockalpha.value)
	{
		// locked sliders
		d3d_WaterAlpha = BYTE_CLAMP (r_wateralpha.value * 256);
		d3d_LavaAlpha = BYTE_CLAMP (r_wateralpha.value * 256);
		d3d_SlimeAlpha = BYTE_CLAMP (r_wateralpha.value * 256);
		d3d_TeleAlpha = BYTE_CLAMP (r_wateralpha.value * 256);
	}
	else
	{
		// independent sliders
		d3d_WaterAlpha = BYTE_CLAMP (r_wateralpha.value * 256);
		d3d_LavaAlpha = BYTE_CLAMP (r_lavaalpha.value * 256);
		d3d_SlimeAlpha = BYTE_CLAMP (r_slimealpha.value * 256);
		d3d_TeleAlpha = BYTE_CLAMP (r_telealpha.value * 256);
	}

	// bound factor
	if (r_warpfactor.value < 0) Cvar_Set (&r_warpfactor, 0.0f);
	if (r_warpfactor.value > 8) Cvar_Set (&r_warpfactor, 8);

	// bound scale
	if (r_warpscale.value < 0) Cvar_Set (&r_warpscale, 0.0f);
	if (r_warpscale.value > 32) Cvar_Set (&r_warpscale, 32);

	// bound speed
	if (r_warpspeed.value < 1) Cvar_Set (&r_warpspeed, 1);
	if (r_warpspeed.value > 32) Cvar_Set (&r_warpspeed, 32);

	// set the warp time and factor (moving this calculation out of a loop)
	warptime = cl.time * 10.18591625f * r_warpspeed.value;
}


warpverts_t *d3d_WarpVerts = NULL;
unsigned short *d3d_WarpIndexes = NULL;

int d3d_NumWarpVerts = 0;
int d3d_NumWarpIndexes = 0;

int previouswarptexture = 0;
int previouswarpalpha = 0;

void D3D_SetupTurbState (void)
{
	D3D_SetTextureMipmap (0, d3d_3DFilterMag, d3d_3DFilterMin, d3d_3DFilterMip);
	D3D_SetTextureAddressMode (D3DTADDRESS_WRAP);
	D3D_SetTexCoordIndexes (0);

	if (d3d_GlobalCaps.usingPixelShaders)
	{
		hr = d3d_Device->SetVertexDeclaration (d3d_LiquidDeclaration);
		d3d_LiquidFX->SetTechnique ("Liquid");

		UINT numpasses;
		d3d_LiquidFX->Begin (&numpasses, D3DXFX_DONOTSAVESTATE);

		d3d_LiquidFX->SetMatrix ("WorldMatrix", D3D_MakeD3DXMatrix (&d3d_WorldMatrix));
		d3d_LiquidFX->SetMatrix ("ProjMatrix", D3D_MakeD3DXMatrix (&d3d_ProjMatrix));

		d3d_LiquidFX->SetFloat ("warptime", warptime);
		d3d_LiquidFX->SetFloat ("warpfactor", r_warpfactor.value);
		d3d_LiquidFX->SetFloat ("warpscale", r_warpscale.value);

		d3d_LiquidFX->SetFloat ("Alpha", 1.0f);

		d3d_LiquidFX->BeginPass (0);
	}
	else
	{
		D3D_SetFVF (D3DFVF_XYZ | D3DFVF_TEX1);

		D3D_SetTextureColorMode (0, D3DTOP_SELECTARG1, D3DTA_TEXTURE, D3DTA_DIFFUSE);
		D3D_SetTextureColorMode (1, D3DTOP_DISABLE);
		D3D_SetTextureAlphaMode (1, D3DTOP_DISABLE);
	}

	// initialize the buffer for holding verts (needed by the alpha pass)
	D3D_GetVertexBufferSpace ((void **) &d3d_WarpVerts);
	D3D_GetIndexBufferSpace ((void **) &d3d_WarpIndexes);

	d3d_NumWarpVerts = 0;
	d3d_NumWarpIndexes = 0;
	previouswarptexture = 0;
	previouswarpalpha = 0;
}


void D3D_TakeDownTurbState (void)
{
	// draw anything left over
	// required always so that the buffer will unlock
	D3D_SubmitVertexes (d3d_NumWarpVerts, d3d_NumWarpIndexes, sizeof (warpverts_t));

	if (d3d_GlobalCaps.usingPixelShaders)
	{
		// end and do the usual thing of disabling shaders fully
		d3d_LiquidFX->EndPass ();
		d3d_LiquidFX->End ();
		d3d_Device->SetVertexShader (NULL);
		d3d_Device->SetPixelShader (NULL);

		D3D_SetFVF (D3DFVF_XYZ | D3DFVF_XYZRHW);
		D3D_SetTexture (0, NULL);
	}
	else D3D_SetRenderState (D3DRS_TEXTUREFACTOR, 0xffffffff);
}


__inline void D3D_EmitWarpVertex (polyvert_t *src, D3DMATRIX *m)
{
	warpverts_t *dest = &d3d_WarpVerts[d3d_NumWarpVerts++];

	if (m)
	{
		dest->v[0] = src->basevert[0] * m->_11 + src->basevert[1] * m->_21 + src->basevert[2] * m->_31 + m->_41;
		dest->v[1] = src->basevert[0] * m->_12 + src->basevert[1] * m->_22 + src->basevert[2] * m->_32 + m->_42;
		dest->v[2] = src->basevert[0] * m->_13 + src->basevert[1] * m->_23 + src->basevert[2] * m->_33 + m->_43;
	}
	else
	{
		dest->v[0] = src->basevert[0];
		dest->v[1] = src->basevert[1];
		dest->v[2] = src->basevert[2];
	}

	if (d3d_GlobalCaps.usingPixelShaders)
	{
		dest->st[0] = src->st[0];
		dest->st[1] = src->st[1];
	}
	else
	{
		dest->st[0] = (src->st[0] + warpsintime) / 64.0f;
		dest->st[1] = (src->st[1] + warpcostime) / 64.0f;
	}
}


__inline void D3D_EmitWarpSurface (msurface_t *surf)
{
	// emit indexes
	for (int i = 2; i < surf->numverts; i++)
	{
		d3d_WarpIndexes[d3d_NumWarpIndexes++] = d3d_NumWarpVerts;
		d3d_WarpIndexes[d3d_NumWarpIndexes++] = d3d_NumWarpVerts + i - 1;
		d3d_WarpIndexes[d3d_NumWarpIndexes++] = d3d_NumWarpVerts + i;
	}

	// emit vertexes
	for (int i = 0; i < surf->numverts; i++)
		D3D_EmitWarpVertex (&surf->verts[i], surf->matrix);

	// explicitly NULL the surface matrix
	surf->matrix = NULL;
}


void D3D_EmitModelSurfToAlpha (d3d_modelsurf_t *modelsurf)
{
	if (modelsurf->matrix)
	{
		// copy out the midpoint for matrix transforms
		float midpoint[3] =
		{
			modelsurf->surf->midpoint[0],
			modelsurf->surf->midpoint[1],
			modelsurf->surf->midpoint[2]
		};

		// keep the code easier to read
		D3DMATRIX *m = modelsurf->matrix;

		// transform the surface midpoint by the modelsurf matrix so that it goes into the proper place
		modelsurf->surf->midpoint[0] = midpoint[0] * m->_11 + midpoint[1] * m->_21 + midpoint[2] * m->_31 + m->_41;
		modelsurf->surf->midpoint[1] = midpoint[0] * m->_12 + midpoint[1] * m->_22 + midpoint[2] * m->_32 + m->_42;
		modelsurf->surf->midpoint[2] = midpoint[0] * m->_13 + midpoint[1] * m->_23 + midpoint[2] * m->_33 + m->_43;

		// now add it
		D3D_AddToAlphaList (modelsurf);

		// restore the original midpoint
		modelsurf->surf->midpoint[0] = midpoint[0];
		modelsurf->surf->midpoint[1] = midpoint[1];
		modelsurf->surf->midpoint[2] = midpoint[2];
	}
	else
	{
		// just add it
		D3D_AddToAlphaList (modelsurf);
	}
}


void D3D_EmitModelSurfsToAlpha (d3d_modelsurf_t *surfchain)
{
	for (d3d_modelsurf_t *modelsurf = surfchain; modelsurf; modelsurf = modelsurf->surfchain)
	{
		// emit this surf (both automatic and explicit alpha)
		D3D_EmitModelSurfToAlpha (modelsurf);
	}
}


void D3D_EmitWarpSurface (d3d_modelsurf_t *modelsurf)
{
	d3d_RenderDef.brush_polys++;

	bool recommit = false;

	byte thisalpha = 255;

	// automatic alpha
	if (modelsurf->surf->flags & SURF_DRAWWATER) thisalpha = d3d_WaterAlpha;
	if (modelsurf->surf->flags & SURF_DRAWLAVA) thisalpha = d3d_LavaAlpha;
	if (modelsurf->surf->flags & SURF_DRAWTELE) thisalpha = d3d_TeleAlpha;
	if (modelsurf->surf->flags & SURF_DRAWSLIME) thisalpha = d3d_SlimeAlpha;

	// explicit alpha
	if (modelsurf->surf->alphaval < 255) thisalpha = modelsurf->surf->alphaval;

	if (thisalpha != previouswarpalpha) recommit = true;
	if ((int) modelsurf->tex->teximage->d3d_Texture != previouswarptexture) recommit = true;

	// check for a full buffer or state change and draw the verts if so
	if (D3D_AreBuffersFull (d3d_NumWarpVerts + modelsurf->surf->numverts, d3d_NumWarpIndexes + modelsurf->surf->numindexes) || recommit)
	{
		D3D_SubmitVertexes (d3d_NumWarpVerts, d3d_NumWarpIndexes, sizeof (warpverts_t));

		// reset the pointers and counters
		d3d_NumWarpVerts = 0;
		d3d_NumWarpIndexes = 0;
		D3D_GetVertexBufferSpace ((void **) &d3d_WarpVerts);
		D3D_GetIndexBufferSpace ((void **) &d3d_WarpIndexes);
	}

	// check for a texture or alpha change
	// (either of these will trigger the recommit above)
	if ((int) modelsurf->tex->teximage->d3d_Texture != previouswarptexture)
	{
		if (d3d_GlobalCaps.usingPixelShaders)
			d3d_LiquidFX->SetTexture ("baseTexture", modelsurf->tex->teximage->d3d_Texture);
		else D3D_SetTexture (0, modelsurf->tex->teximage->d3d_Texture);

		previouswarptexture = (int) modelsurf->tex->teximage->d3d_Texture;
	}

	if (thisalpha != previouswarpalpha)
	{
		if (d3d_GlobalCaps.usingPixelShaders)
			d3d_LiquidFX->SetFloat ("Alpha", (float) thisalpha / 255.0f);
		else
		{
			if (thisalpha < 255)
				D3D_SetTextureAlphaMode (0, D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_TFACTOR);
			else D3D_SetTextureAlphaMode (0, D3DTOP_DISABLE);

			D3D_SetRenderState (D3DRS_TEXTUREFACTOR, D3DCOLOR_ARGB (BYTE_CLAMP (thisalpha), 255, 255, 255));
		}

		previouswarpalpha = thisalpha;
	}

	// HLSL needs to commit a param change
	if (recommit && d3d_GlobalCaps.usingPixelShaders) d3d_LiquidFX->CommitChanges ();

	// store out the matrix to the surf
	modelsurf->surf->matrix = modelsurf->matrix;

	// emit this surface's verts and indexes to the buffer
	D3D_EmitWarpSurface (modelsurf->surf);
}


void D3D_DrawWaterSurfaces (void)
{
	bool stateset = false;

	// initialize the turb surfaces data
	D3D_InitializeTurb ();

	// even if we're fully alpha we still pass through here so that we can add items to the alpha list
	for (int i = 0; i < d3d_NumRegisteredTextures; i++)
	{
		d3d_registeredtexture_t *rt = &d3d_RegisteredTextures[i];

		// chains which are empty or non-turb are skipped over
		if (!rt->surfchain) continue;
		if (!(rt->surfchain->surf->flags & SURF_DRAWTURB)) continue;

		// skip over alpha surfaces - automatic skip unless the surface explicitly overrides it (same path)
		if ((rt->surfchain->surf->flags & SURF_DRAWLAVA) && d3d_LavaAlpha < 255) {D3D_EmitModelSurfsToAlpha (rt->surfchain); continue;}
		if ((rt->surfchain->surf->flags & SURF_DRAWTELE) && d3d_TeleAlpha < 255) {D3D_EmitModelSurfsToAlpha (rt->surfchain); continue;}
		if ((rt->surfchain->surf->flags & SURF_DRAWSLIME) && d3d_SlimeAlpha < 255) {D3D_EmitModelSurfsToAlpha (rt->surfchain); continue;}
		if ((rt->surfchain->surf->flags & SURF_DRAWWATER) && d3d_WaterAlpha < 255) {D3D_EmitModelSurfsToAlpha (rt->surfchain); continue;}

		// now draw all surfaces with this texture
		for (d3d_modelsurf_t *modelsurf = rt->surfchain; modelsurf; modelsurf = modelsurf->surfchain)
		{
			// check for alpha on individual surfs here
			if (modelsurf->surf->alphaval < 255)
			{
				// individual surfs can have alpha at this point
				D3D_EmitModelSurfToAlpha (modelsurf);
				continue;
			}

			// determine if we need a state update; this is just for setting the
			// initial state; we always update the texture on a texture change
			// we check in here because we might be skipping some of these surfaces if they have alpha set
			if (!stateset)
			{
				D3D_SetupTurbState ();
				stateset = true;
			}

			// this one used for both alpha and non-alpha
			D3D_EmitWarpSurface (modelsurf);
		}
	}

	// determine if we need to take down state
	if (stateset) D3D_TakeDownTurbState ();
}


/*
==============================================================================================================================

		SKY WARP RENDERING

	Sky is rendered as a full sphere.  An initial "clipping" pass (writing to the depth buffer only) is made to
	establish the baseline area covered by the sky.  The old sky surfs are used for this
	
	The depth test is then inverted and the sky sphere is rendered as a "Z-fail" pass, meaning that areas where
	the depth buffer was rendered to in the previous pass are the only ones updated.

	A final clipping pass is made with the normal depth buffer to remove any remaining world geometry.
	(Note - this is currently commented out as I'm not certain it's actually necessary.  I remember it being needed
	when I wrote the code first, but that was a good while back and it was an earlier version of code.)

	This gives us a skysphere that is drawn at a distance but still clips world geometry.

	Note: you need either an infinite projection matrix or a very large Z-far for this to work right!

==============================================================================================================================
*/

void R_ClearSkyBox (void);
void R_AddSkyBoxSurface (msurface_t *surf, D3DMATRIX *m);

// let's distinguish properly between preproccessor defines and variables
// (LH apparently doesn't believe in this, but I do)
#define SKYGRID_SIZE 16
#define SKYGRID_SIZE_PLUS_1 (SKYGRID_SIZE + 1)
#define SKYGRID_RECIP (1.0f / (SKYGRID_SIZE))
#define SKYSPHERE_NUMVERTS (SKYGRID_SIZE_PLUS_1 * SKYGRID_SIZE_PLUS_1)
#define SKYSPHERE_NUMTRIS (SKYGRID_SIZE * SKYGRID_SIZE * 2)
#define SKYSPHERE_NUMINDEXES (SKYGRID_SIZE * SKYGRID_SIZE * 6)

typedef struct skysphereverts_s
{
	float xyz[3];
	float st1[2];
	float st2[2];
} skysphereverts_t;


LPDIRECT3DINDEXBUFFER9 d3d_DPSkyIndexes = NULL;
LPDIRECT3DVERTEXBUFFER9 d3d_DPSkyVerts = NULL;

int d3d_NumSkyVerts = 0;
int d3d_NumSkyIndexes = 0;

warpverts_t *d3d_SkyVerts = NULL;
unsigned short *d3d_SkyIndexes = NULL;

cvar_t r_skywarp ("r_skywarp", 1, CVAR_ARCHIVE);
cvar_t r_skybackscroll ("r_skybackscroll", 8, CVAR_ARCHIVE);
cvar_t r_skyfrontscroll ("r_skyfrontscroll", 16, CVAR_ARCHIVE);
cvar_t r_skyalpha ("r_skyalpha", 1, CVAR_ARCHIVE);

// dynamic far clip plane for sky
extern float r_clipdist;
unsigned *skyalphatexels = NULL;

void D3D_InitializeSky (void)
{
	R_ClearSkyBox ();

	D3D_GetVertexBufferSpace ((void **) &d3d_SkyVerts);
	D3D_GetIndexBufferSpace ((void **) &d3d_SkyIndexes);

	d3d_NumSkyVerts = 0;
	d3d_NumSkyIndexes = 0;

	// bound alpha
	if (r_skyalpha.value < 0.0f) Cvar_Set (&r_skyalpha, 0.0f);
	if (r_skyalpha.value > 1.0f) Cvar_Set (&r_skyalpha, 1.0f);

	D3D_SetTextureMipmap (0, d3d_3DFilterMag, d3d_3DFilterMin, D3DTEXF_NONE);
	D3D_SetTextureMipmap (1, d3d_3DFilterMag, d3d_3DFilterMin, D3DTEXF_NONE);
	D3D_SetTexCoordIndexes (0, 1);
	D3D_SetTextureAddressMode (D3DTADDRESS_WRAP, D3DTADDRESS_WRAP);

	if (!r_skywarp.value)
	{
		// simple sky warp
		D3D_SetTextureColorMode (0, D3DTOP_SELECTARG1, D3DTA_TEXTURE, D3DTA_DIFFUSE);
		D3D_SetTextureAlphaMode (0, D3DTOP_DISABLE);

		D3D_SetTextureColorMode (1, D3DTOP_DISABLE);
		D3D_SetTextureAlphaMode (1, D3DTOP_DISABLE);

		D3D_SetFVF (D3DFVF_XYZ | D3DFVF_TEX1);
		D3D_SetTexture (0, simpleskytexture);
	}
	else if (SkyboxValid || !d3d_GlobalCaps.usingPixelShaders)
	{
		// emit a clipping area
		D3D_SetFVF (D3DFVF_XYZ);
		D3D_SetTextureColorMode (0, D3DTOP_DISABLE);
		D3D_SetRenderState (D3DRS_COLORWRITEENABLE, 0);
	}
	else
	{
		// hlsl sky warp
		float speedscale[] = {cl.time * r_skybackscroll.value, cl.time * r_skyfrontscroll.value, 1};

		speedscale[0] -= (int) speedscale[0] & ~127;
		speedscale[1] -= (int) speedscale[1] & ~127;

		d3d_Device->SetVertexDeclaration (d3d_SkyDeclaration);
		d3d_SkyFX->SetTechnique ("SkyTech");

		UINT numpasses;

		d3d_SkyFX->Begin (&numpasses, D3DXFX_DONOTSAVESTATE);

		d3d_SkyFX->SetMatrix ("WorldMatrix", D3D_MakeD3DXMatrix (&d3d_WorldMatrix));
		d3d_SkyFX->SetMatrix ("ProjMatrix", D3D_MakeD3DXMatrix (&d3d_ProjMatrix));

		d3d_SkyFX->SetTexture ("solidLayer", solidskytexture);
		d3d_SkyFX->SetTexture ("alphaLayer", alphaskytexture);

		d3d_SkyFX->SetFloat ("Alpha", r_skyalpha.value);
		d3d_SkyFX->SetFloatArray ("r_origin", r_origin, 3);
		d3d_SkyFX->SetFloatArray ("Scale", speedscale, 3);

		d3d_SkyFX->BeginPass (0);
	}
}


void D3D_UpdateSkyAlpha (void)
{
	// i can't figure the correct mode for skyalpha so i'll just do it this way :p
	static int oldalpha = -666;
	bool copytexels = false;

	if (oldalpha == (int) (r_skyalpha.value * 255) && skyalphatexels) return;

	oldalpha = (int) (r_skyalpha.value * 255);

	D3DSURFACE_DESC Level0Desc;
	D3DLOCKED_RECT Level0Rect;
	LPDIRECT3DSURFACE9 skysurf;

	alphaskytexture->GetLevelDesc (0, &Level0Desc);
	alphaskytexture->GetSurfaceLevel (0, &skysurf);

	// copy it out to an ARGB surface
	LPDIRECT3DSURFACE9 CopySurf;

	d3d_Device->CreateOffscreenPlainSurface (Level0Desc.Width, Level0Desc.Height, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &CopySurf, NULL);
	QD3DXLoadSurfaceFromSurface (CopySurf, NULL, NULL, skysurf, NULL, NULL, D3DX_FILTER_NONE, 0);
	CopySurf->LockRect (&Level0Rect, NULL, D3DLOCK_NO_DIRTY_UPDATE);

	// alloc texels
	if (!skyalphatexels)
	{
		skyalphatexels = (unsigned *) Pool_Map->Alloc (Level0Desc.Width * Level0Desc.Height * sizeof (unsigned));
		copytexels = true;
	}

	for (int i = 0; i < Level0Desc.Width * Level0Desc.Height; i++)
	{
		// copy out the first time
		if (copytexels) skyalphatexels[i] = ((unsigned *) Level0Rect.pBits)[i];

		// copy back first and subsequent times
		((unsigned *) Level0Rect.pBits)[i] = skyalphatexels[i];

		// despite being created as D3DFMT_A8R8G8B8 this is actually bgra.  WTF Microsoft???
		byte *bgra = (byte *) &(((unsigned *) Level0Rect.pBits)[i]);

		float alpha = bgra[3];
		alpha *= r_skyalpha.value;
		bgra[3] = BYTE_CLAMP (alpha);
	}

	CopySurf->UnlockRect ();
	QD3DXLoadSurfaceFromSurface (skysurf, NULL, NULL, CopySurf, NULL, NULL, D3DX_FILTER_NONE, 0);

	CopySurf->Release ();
	skysurf->Release ();
	alphaskytexture->AddDirtyRect (NULL);
}


// for toggling skyfog
extern cvar_t gl_fogenable;
extern cvar_t gl_fogend;
extern cvar_t gl_fogbegin;
extern cvar_t gl_fogdensity;
extern cvar_t gl_fogsky;

void D3D_AddSkySphereToRender (float scale)
{
	D3D_UpdateSkyAlpha ();

	// darkplaces warp
	D3D_VBOSetVBOStream (d3d_DPSkyVerts, d3d_DPSkyIndexes, sizeof (warpverts_t));

	D3D_SetFVF (D3DFVF_XYZ | D3DFVF_TEX1);

	D3D_SetTextureColorMode (0, D3DTOP_SELECTARG1, D3DTA_TEXTURE, D3DTA_DIFFUSE);
	D3D_SetTextureAlphaMode (0, D3DTOP_DISABLE);

	D3D_SetTextureColorMode (1, D3DTOP_BLENDTEXTUREALPHA, D3DTA_TEXTURE, D3DTA_CURRENT);
	D3D_SetTextureAlphaMode (1, D3DTOP_DISABLE);

	D3D_SetTextureColorMode (2, D3DTOP_DISABLE);
	D3D_SetTextureAlphaMode (2, D3DTOP_DISABLE);

	D3D_SetTextureMipmap (0, d3d_3DFilterMag, d3d_3DFilterMin, D3DTEXF_NONE);
	D3D_SetTextureMipmap (1, d3d_3DFilterMag, d3d_3DFilterMin, D3DTEXF_NONE);

	D3D_SetTexCoordIndexes (0, 0);
	D3D_SetTextureMatrixOp (D3DTTFF_COUNT2, D3DTTFF_COUNT2);
	D3D_SetTextureAddressMode (D3DTADDRESS_WRAP, D3DTADDRESS_WRAP);

	D3DMATRIX skymatrix;
	memcpy (&skymatrix, &d3d_WorldMatrix, sizeof (D3DMATRIX));
	D3D_ScaleMatrix (&skymatrix, scale, scale, scale);
	d3d_Device->SetTransform (D3DTS_WORLD, &skymatrix);

	float speedscale;
	D3DMATRIX scrollmatrix;
	D3D_LoadIdentity (&scrollmatrix);

	// use cl.time so that it pauses properly when the console is down (same as everything else)
	speedscale = cl.time * r_skybackscroll.value;
	speedscale -= (int) speedscale & ~127;
	speedscale /= 128.0f;

	// aaaaarrrrggghhhh - direct3D doesn't use standard matrix positions for texture transforms!!!
	// and where *exactly* was that documented again...?
	scrollmatrix._31 = speedscale;
	scrollmatrix._32 = speedscale;
	d3d_Device->SetTransform (D3DTS_TEXTURE0, &scrollmatrix);

	// use cl.time so that it pauses properly when the console is down (same as everything else)
	speedscale = cl.time * r_skyfrontscroll.value;
	speedscale -= (int) speedscale & ~127;
	speedscale /= 128.0f;

	// aaaaarrrrggghhhh - direct3D doesn't use standard matrix positions for texture transforms!!!
	// and where *exactly* was that documented again...?
	scrollmatrix._31 = speedscale;
	scrollmatrix._32 = speedscale;
	d3d_Device->SetTransform (D3DTS_TEXTURE1, &scrollmatrix);

	D3D_SetTexture (0, solidskytexture);
	D3D_SetTexture (1, alphaskytexture);

	// all that just for this...
	d3d_Device->DrawIndexedPrimitive (D3DPT_TRIANGLELIST, 0, 0, SKYSPHERE_NUMVERTS, 0, SKYSPHERE_NUMTRIS);

	// take down specific stuff
	D3D_SetTextureMatrixOp (D3DTTFF_DISABLE, D3DTTFF_DISABLE);
	D3D_SetTextureState (0, D3DTSS_CONSTANT, 0xffffffff);
	D3D_SetTextureState (1, D3DTSS_CONSTANT, 0xffffffff);

	// restore transform
	d3d_Device->SetTransform (D3DTS_WORLD, &d3d_WorldMatrix);
}


void D3D_AddSkySurfaceToRender (msurface_t *surf, D3DMATRIX *m)
{
	d3d_RenderDef.brush_polys++;

	if (d3d_RenderDef.skyframe != d3d_RenderDef.framecount)
	{
		// initialize sky for the frame
		D3D_InitializeSky ();

		// flag sky as having been initialized this frame
		d3d_RenderDef.skyframe = d3d_RenderDef.framecount;
	}

	// check for batch submission
	if (D3D_AreBuffersFull (d3d_NumSkyVerts + surf->numverts, d3d_NumSkyIndexes + (surf->numverts - 2)))
	{
		// draw this batch
		D3D_SubmitVertexes (d3d_NumSkyVerts, d3d_NumSkyIndexes, sizeof (warpverts_t));

		// reset batch
		D3D_GetVertexBufferSpace ((void **) &d3d_SkyVerts);
		D3D_GetIndexBufferSpace ((void **) &d3d_SkyIndexes);
		d3d_NumSkyIndexes = 0;
		d3d_NumSkyVerts = 0;
	}

	// skybox clipping (saves on fillrate and z-comparison for scenes with large sky)
	if (SkyboxValid) R_AddSkyBoxSurface (surf, m);

	// add vertexes
	polyvert_t *src = surf->verts;
	warpverts_t *dest = &d3d_SkyVerts[d3d_NumSkyVerts];

	// add them all
	for (int i = 0; i < surf->numverts; i++, src++, dest++)
	{
		if (m)
		{
			dest->v[0] = src->basevert[0] * m->_11 + src->basevert[1] * m->_21 + src->basevert[2] * m->_31 + m->_41;
			dest->v[1] = src->basevert[0] * m->_12 + src->basevert[1] * m->_22 + src->basevert[2] * m->_32 + m->_42;
			dest->v[2] = src->basevert[0] * m->_13 + src->basevert[1] * m->_23 + src->basevert[2] * m->_33 + m->_43;
		}
		else
		{
			dest->v[0] = src->basevert[0];
			dest->v[1] = src->basevert[1];
			dest->v[2] = src->basevert[2];
		}

		dest->st[0] = src->st[0];
		dest->st[1] = src->st[1];
	}

	// add indexes
	for (int i = 2; i < surf->numverts; i++)
	{
		d3d_SkyIndexes[d3d_NumSkyIndexes++] = d3d_NumSkyVerts;
		d3d_SkyIndexes[d3d_NumSkyIndexes++] = d3d_NumSkyVerts + i - 1;
		d3d_SkyIndexes[d3d_NumSkyIndexes++] = d3d_NumSkyVerts + i;
	}

	// next set of verts
	d3d_NumSkyVerts += surf->numverts;
}


vec3_t skyclip[6] =
{
	{1, 1, 0},
	{1, -1, 0},
	{0, -1, 1},
	{0, 1, 1},
	{1, 0, 1},
	{-1, 0, 1} 
};


int	st_to_vec[6][3] = 
{
	{3, -1, 2},
	{-3, 1, 2},

	{1, 3, 2},
	{-1, -3, 2},

	{-2, -1, 3},
	{2, -1, -3}
};


int	vec_to_st[6][3] =
{
	{-2, 3, 1},
	{2, 3, -1},

	{1, 3, 2},
	{-1, 3, -2},

	{-2, -1, 3},
	{-2, 1, -3}
};


float skymins[2][6], skymaxs[2][6];
float sky_min, sky_max;

void DrawSkyPolygon (int nump, vec3_t vecs)
{
	int		i,j;
	vec3_t	v, av;
	float	s, t, dv;
	int		axis;
	float	*vp;

	// decide which face it maps to
	VectorCopy (vec3_origin, v);

	for (i = 0, vp = vecs; i < nump; i++, vp += 3)
	{
		VectorAdd (vp, v, v);
	}

	av[0] = fabs (v[0]);
	av[1] = fabs (v[1]);
	av[2] = fabs (v[2]);

	if (av[0] > av[1] && av[0] > av[2])
	{
		if (v[0] < 0)
			axis = 1;
		else axis = 0;
	}
	else if (av[1] > av[2] && av[1] > av[0])
	{
		if (v[1] < 0)
			axis = 3;
		else axis = 2;
	}
	else
	{
		if (v[2] < 0)
			axis = 5;
		else axis = 4;
	}

	// project new texture coords
	for (i = 0; i < nump; i++, vecs += 3)
	{
		j = vec_to_st[axis][2];

		if (j > 0)
			dv = vecs[j - 1];
		else dv = -vecs[-j - 1];

		// don't divide by zero
		if (dv < 0.001) continue;

		j = vec_to_st[axis][0];

		if (j < 0)
			s = -vecs[-j -1] / dv;
		else s = vecs[j-1] / dv;

		j = vec_to_st[axis][1];

		if (j < 0)
			t = -vecs[-j -1] / dv;
		else t = vecs[j-1] / dv;

		if (s < skymins[0][axis]) skymins[0][axis] = s;
		if (t < skymins[1][axis]) skymins[1][axis] = t;
		if (s > skymaxs[0][axis]) skymaxs[0][axis] = s;
		if (t > skymaxs[1][axis]) skymaxs[1][axis] = t;
	}
}


#define	SIDE_FRONT		0
#define	SIDE_BACK		1
#define	SIDE_ON			2
#define	ON_EPSILON		0.1			// point on plane side epsilon
#define	MAX_CLIP_VERTS	64


void ClipSkyPolygon (int nump, vec3_t vecs, int stage)
{
	float	*norm;
	float	*v;
	bool	front, back;
	float	d, e;
	float	dists[MAX_CLIP_VERTS];
	int		sides[MAX_CLIP_VERTS];
	vec3_t	newv[2][MAX_CLIP_VERTS];
	int		newc[2];
	int		i, j;

	if (nump > MAX_CLIP_VERTS - 2)
		Sys_Error ("ClipSkyPolygon: MAX_CLIP_VERTS");

	if (stage == 6)
	{
		// fully clipped, so draw it
		DrawSkyPolygon (nump, vecs);
		return;
	}

	front = back = false;
	norm = skyclip[stage];

	for (i = 0, v = vecs; i < nump; i++, v += 3)
	{
		d = DotProduct (v, norm);

		if (d > ON_EPSILON)
		{
			front = true;
			sides[i] = SIDE_FRONT;
		}
		else if (d < -ON_EPSILON)
		{
			back = true;
			sides[i] = SIDE_BACK;
		}
		else sides[i] = SIDE_ON;

		dists[i] = d;
	}

	if (!front || !back)
	{
		// not clipped
		ClipSkyPolygon (nump, vecs, stage + 1);
		return;
	}

	// clip it
	sides[i] = sides[0];
	dists[i] = dists[0];
	VectorCopy (vecs, (vecs + (i * 3)));
	newc[0] = newc[1] = 0;

	for (i = 0, v = vecs; i < nump; i++, v += 3)
	{
		switch (sides[i])
		{
		case SIDE_FRONT:
			VectorCopy (v, newv[0][newc[0]]);
			newc[0]++;
			break;

		case SIDE_BACK:
			VectorCopy (v, newv[1][newc[1]]);
			newc[1]++;
			break;

		case SIDE_ON:
			VectorCopy (v, newv[0][newc[0]]);
			newc[0]++;
			VectorCopy (v, newv[1][newc[1]]);
			newc[1]++;
			break;
		}

		if (sides[i] == SIDE_ON || sides[i + 1] == SIDE_ON || sides[i + 1] == sides[i])
			continue;

		d = dists[i] / (dists[i] - dists[i + 1]);

		for (j = 0; j < 3; j++)
		{
			e = v[j] + d * (v[j + 3] - v[j]);
			newv[0][newc[0]][j] = e;
			newv[1][newc[1]][j] = e;
		}

		newc[0]++;
		newc[1]++;
	}

	// continue
	ClipSkyPolygon (newc[0], newv[0][0], stage + 1);
	ClipSkyPolygon (newc[1], newv[1][0], stage + 1);
}


void R_AddSkyBoxSurface (msurface_t *surf, D3DMATRIX *m)
{
	vec3_t		verts[MAX_CLIP_VERTS];
	polyvert_t	*src = surf->verts;

	// calculate vertex values for sky box
	// (this should be matrix transformed if appropriate)
	for (int i = 0; i < surf->numverts; i++, src++)
	{
		if (m)
		{
			vec3_t tmp =
			{
				src->basevert[0] * m->_11 + src->basevert[1] * m->_21 + src->basevert[2] * m->_31 + m->_41,
				src->basevert[0] * m->_12 + src->basevert[1] * m->_22 + src->basevert[2] * m->_32 + m->_42,
				src->basevert[0] * m->_13 + src->basevert[1] * m->_23 + src->basevert[2] * m->_33 + m->_43
			};

			VectorSubtract (tmp, r_origin, verts[i]);
		}
		else VectorSubtract (src->basevert, r_origin, verts[i]);
	}

	ClipSkyPolygon (surf->numverts, verts[0], 0);
}


void R_ClearSkyBox (void)
{
	for (int i = 0; i < 6; i++)
	{
		skymins[0][i] = skymins[1][i] = 99999999;
		skymaxs[0][i] = skymaxs[1][i] = -99999999;
	}
}


void MakeSkyVec (warpverts_t *vert, float s, float t, int axis)
{
	// fill in verts (draw at double the clipping dist)
	float b[] =
	{
		s * r_clipdist * 2,
		t * r_clipdist * 2,
		r_clipdist * 2
	};

	for (int j = 0; j < 3; j++)
	{
		int k = st_to_vec[axis][j];

		if (k < 0)
			vert->v[j] = -b[-k - 1] + r_origin[j];
		else vert->v[j] = b[k - 1] + r_origin[j];
	}

	// fill in texcoords
	// avoid bilerp seam
	vert->st[0] = (s + 1) * 0.5;
	vert->st[1] = (t + 1) * 0.5;

	// clamp
	if (vert->st[0] < sky_min) vert->st[0] = sky_min; else if (vert->st[0] > sky_max) vert->st[0] = sky_max;
	if (vert->st[1] < sky_min) vert->st[1] = sky_min; else if (vert->st[1] > sky_max) vert->st[1] = sky_max;

	// invert t
	vert->st[1] = 1.0 - vert->st[1];
}


void R_AddSkyboxToRender (void)
{
	int i;
	int	skytexorder[6] = {0, 2, 1, 3, 4, 5};

	sky_min = 1.0 / 512;
	sky_max = 511.0 / 512;

	D3D_SetFVF (D3DFVF_XYZ | D3DFVF_TEX1);

	for (i = 0; i < 6; i++)
	{
		// we allowed the skybox to load if any of the components were not found (crazy modders!), so we must account for that here
		if (!skyboxtextures[skytexorder[i]]) continue;

		// fully clipped away
		if (skymins[0][i] >= skymaxs[0][i] || skymins[1][i] >= skymaxs[1][i]) continue;

		D3D_SetTexture (0, skyboxtextures[skytexorder[i]]);

		// use UP drawing here so that we're not abusing the vertex buffer
		warpverts_t verts[6];

		MakeSkyVec (&verts[0], skymins[0][i], skymins[1][i], i);
		MakeSkyVec (&verts[1], skymins[0][i], skymaxs[1][i], i);
		MakeSkyVec (&verts[2], skymaxs[0][i], skymaxs[1][i], i);

		MakeSkyVec (&verts[3], skymins[0][i], skymins[1][i], i);
		MakeSkyVec (&verts[4], skymaxs[0][i], skymaxs[1][i], i);
		MakeSkyVec (&verts[5], skymaxs[0][i], skymins[1][i], i);

		D3D_DrawUserPrimitive (D3DPT_TRIANGLELIST, 2, verts, sizeof (warpverts_t));
		d3d_RenderDef.brush_polys++;
	}
}


void D3D_FinalizeSky (void)
{
	// no surfs were added
	if (d3d_RenderDef.skyframe != d3d_RenderDef.framecount) return;

	// draw anything that was left over
	// required always so that the buffer will unlock
	D3D_SubmitVertexes (d3d_NumSkyVerts, d3d_NumSkyIndexes, sizeof (warpverts_t));

	if (SkyboxValid || !d3d_GlobalCaps.usingPixelShaders)
	{
		D3D_SetRenderState (D3DRS_COLORWRITEENABLE, 0x0000000F);
		D3D_SetTextureColorMode (0, D3DTOP_SELECTARG1);

		// invert the depth func and add the skybox
		// also disable depth writing so that it retains the z values from the clipping brushes
		D3D_SetRenderState (D3DRS_ZFUNC, D3DCMP_GREATEREQUAL);
		D3D_SetRenderState (D3DRS_ZWRITEENABLE, FALSE);

		// add the skybox
		if (SkyboxValid)
			R_AddSkyboxToRender ();
		else D3D_AddSkySphereToRender (r_clipdist / 8);

		// restore the old depth func
		D3D_SetRenderState (D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
		D3D_SetRenderState (D3DRS_ZWRITEENABLE, TRUE);
	}
	else
	{
		// done
		d3d_SkyFX->EndPass ();
		d3d_SkyFX->End ();

		// take down shaders
		d3d_Device->SetPixelShader (NULL);
		d3d_Device->SetVertexShader (NULL);

		// set fvf to invalid to force an update next time it's used
		D3D_SetFVF (D3DFVF_XYZ | D3DFVF_XYZRHW);
		D3D_SetTexture (0, NULL);
		D3D_SetTexture (1, NULL);
	}
}


/*
==============================================================================================================================

		SKY INITIALIZATION

==============================================================================================================================
*/


void D3D_InitSkySphere (void)
{
	// protect against multiple allocations
	if (d3d_DPSkyVerts && d3d_DPSkyIndexes) return;

	int i, j;
	float a, b, x, ax, ay, v[3], length;
	float dx, dy, dz;

	dx = 16;
	dy = 16;
	dz = 16 / 3;

	hr = d3d_Device->CreateVertexBuffer
	(
		SKYSPHERE_NUMVERTS * sizeof (warpverts_t),
		D3DUSAGE_WRITEONLY,
		0,
		D3DPOOL_MANAGED,
		&d3d_DPSkyVerts,
		NULL
	);

	if (FAILED (hr))
	{
		Sys_Error ("D3D_DPSkySphereCalc: d3d_Device->CreateVertexBuffer failed");
		return;
	}

	warpverts_t *ssv;

	i = SKYSPHERE_NUMVERTS;

	d3d_DPSkyVerts->Lock (0, SKYSPHERE_NUMVERTS * sizeof (warpverts_t), (void **) &ssv, 0);
	warpverts_t *ssv2 = ssv;

	float maxv = 0;
	float minv = 1048576;

	for (j = 0; j <= SKYGRID_SIZE; j++)
	{
		a = j * SKYGRID_RECIP;
		ax = cos (a * M_PI * 2);
		ay = -sin (a * M_PI * 2);

		for (i = 0; i <= SKYGRID_SIZE; i++)
		{
			b = i * SKYGRID_RECIP;
			x = cos ((b + 0.5) * M_PI);

			v[0] = ax * x * dx;
			v[1] = ay * x * dy;
			v[2] = -sin ((b + 0.5) * M_PI) * dz;

			if (v[0] > maxv) maxv = v[0];
			if (v[1] > maxv) maxv = v[1];
			if (v[2] > maxv) maxv = v[2];

			if (v[0] < minv) minv = v[0];
			if (v[1] < minv) minv = v[1];
			if (v[2] < minv) minv = v[2];

			// same calculation as classic Q1 sky but projected onto an actual physical sphere
			// (rather than on flat surfs) and calced as if from an origin of [0,0,0] to prevent
			// the heaving and buckling effect
			length = 3.0f / sqrt (v[0] * v[0] + v[1] * v[1] + (v[2] * v[2] * 9));

			ssv2->st[0] = v[0] * length;
			ssv2->st[1] = v[1] * length;
			ssv2->v[0] = v[0];
			ssv2->v[1] = v[1];
			ssv2->v[2] = v[2];

			ssv2++;
		}
	}

	d3d_DPSkyVerts->Unlock ();
	d3d_DPSkyVerts->PreLoad ();

	Con_DPrintf ("max is %0.3f\n", maxv);
	Con_DPrintf ("min is %0.3f\n", minv);

	hr = d3d_Device->CreateIndexBuffer
	(
		(SKYGRID_SIZE * SKYGRID_SIZE * 6) * sizeof (unsigned short),
		D3DUSAGE_WRITEONLY,
		D3DFMT_INDEX16,
		D3DPOOL_MANAGED,
		&d3d_DPSkyIndexes,
		NULL
	);

	if (FAILED (hr))
	{
		Sys_Error ("D3D_DPSkySphereCalc: d3d_Device->CreateIndexBuffer failed");
		return;
	}

	unsigned short *skyindexes;
	unsigned short *e;

	d3d_DPSkyIndexes->Lock (0, (SKYGRID_SIZE * SKYGRID_SIZE * 6) * sizeof (unsigned short), (void **) &skyindexes, 0);
	e = skyindexes;

	for (j = 0; j < SKYGRID_SIZE; j++)
	{
		for (i = 0; i < SKYGRID_SIZE; i++)
		{
			*e++ =  j * SKYGRID_SIZE_PLUS_1 + i;
			*e++ =  j * SKYGRID_SIZE_PLUS_1 + i + 1;
			*e++ = (j + 1) * SKYGRID_SIZE_PLUS_1 + i;

			*e++ =  j * SKYGRID_SIZE_PLUS_1 + i + 1;
			*e++ = (j + 1) * SKYGRID_SIZE_PLUS_1 + i + 1;
			*e++ = (j + 1) * SKYGRID_SIZE_PLUS_1 + i;
		}
	}

	d3d_DPSkyIndexes->Unlock ();
	d3d_DPSkyIndexes->PreLoad ();
}


/*
=============
R_InitSky

A sky texture is 256*128, with the right side being a masked overlay
==============
*/
void R_InitSky (miptex_t *mt)
{
	// sanity check
	if ((mt->width % 4) || (mt->width < 4) || (mt->height % 2) || (mt->height < 2))
	{
		Host_Error ("R_InitSky: invalid sky dimensions (%i x %i)\n", mt->width, mt->height);
		return;
	}

	// create the sky sphere (one time only)
	D3D_InitSkySphere ();

	// because you never know when a mapper might use a non-standard size...
	unsigned *trans = (unsigned *) Pool_Temp->Alloc (mt->width * mt->height * sizeof (unsigned) / 2);

	// copy out
	int transwidth = mt->width / 2;
	int transheight = mt->height;

	// destroy any current textures we might have
	SAFE_RELEASE (simpleskytexture);
	SAFE_RELEASE (solidskytexture);
	SAFE_RELEASE (alphaskytexture);

	byte *src = (byte *) mt + mt->offsets[0];
	unsigned int transpix, r = 0, g = 0, b = 0;

	// make an average value for the back to avoid a fringe on the top level
	for (int i = 0; i < transheight; i++)
	{
		for (int j = 0; j < transwidth; j++)
		{
			// solid sky can go up as 8 bit
			int p = src[i * mt->width + j + transwidth];
			((byte * ) trans)[(i * transwidth) + j] = p;

			r += ((byte *) &d_8to24table[p])[0];
			g += ((byte *) &d_8to24table[p])[1];
			b += ((byte *) &d_8to24table[p])[2];
		}
	}

	((byte *) &transpix)[0] = BYTE_CLAMP (r / (transwidth * transheight));
	((byte *) &transpix)[1] = BYTE_CLAMP (g / (transwidth * transheight));
	((byte *) &transpix)[2] = BYTE_CLAMP (b / (transwidth * transheight));
	((byte *) &transpix)[3] = 0;

	// upload it - solid sky can go up as 8 bit
	if (!D3D_LoadExternalTexture (&solidskytexture, va ("%s_solid", mt->name), IMAGE_NOCOMPRESS))
		D3D_UploadTexture (&solidskytexture, trans, transwidth, transheight, IMAGE_NOCOMPRESS);

	// bottom layer
	for (int i = 0; i < transheight; i++)
	{
		for (int j = 0; j < transwidth; j++)
		{
			int p = src[i * mt->width + j];

			if (p == 0)
				trans[(i * transwidth) + j] = transpix;
			else trans[(i * transwidth) + j] = d_8to24table[p];
		}
	}

	// upload it - alpha sky needs to go up as 32 bit owing to averaging
	if (!D3D_LoadExternalTexture (&alphaskytexture, va ("%s_alpha", mt->name), IMAGE_NOCOMPRESS | IMAGE_ALPHA))
		D3D_UploadTexture (&alphaskytexture, trans, transwidth, transheight, IMAGE_32BIT | IMAGE_NOCOMPRESS | IMAGE_ALPHA);

	// simple sky just uses a 1 x 1 texture of the average colour
	((byte *) &transpix)[3] = 255;
	D3D_UploadTexture (&simpleskytexture, &transpix, 1, 1, IMAGE_32BIT | IMAGE_NOCOMPRESS);

	// no texels yet
	skyalphatexels = NULL;

	// prevent it happening first time during game play
	D3D_UpdateSkyAlpha ();
}


char *suf[6] = {"rt", "bk", "lf", "ft", "up", "dn"};

void R_UnloadSkybox (void)
{
	// release any skybox textures we might already have
	SAFE_RELEASE (skyboxtextures[0]);
	SAFE_RELEASE (skyboxtextures[1]);
	SAFE_RELEASE (skyboxtextures[2]);
	SAFE_RELEASE (skyboxtextures[3]);
	SAFE_RELEASE (skyboxtextures[4]);
	SAFE_RELEASE (skyboxtextures[5]);

	// the skybox is invalid now so revert to regular warps
	SkyboxValid = false;
	CachedSkyBoxName[0] = 0;
}

char *sbdir[] = {"gfx", "env", "gfx/env", NULL};

void R_LoadSkyBox (char *basename, bool feedback)
{
	// force an unload of the current skybox
	R_UnloadSkybox ();

	int numloaded = 0;

	for (int sb = 0; sb < 6; sb++)
	{
		// attempt to load it (sometimes an underscore is expected)
		if (!D3D_LoadExternalTexture (&skyboxtextures[sb], va ("%s%s", basename, suf[sb]), IMAGE_32BIT | IMAGE_NOCOMPRESS))
			if (!D3D_LoadExternalTexture (&skyboxtextures[sb], va ("%s_%s", basename, suf[sb]), IMAGE_32BIT | IMAGE_NOCOMPRESS))
				continue;

		// loaded OK
		numloaded++;
	}

	if (numloaded)
	{
		// as FQ is the behaviour modders expect let's allow partial skyboxes (much as it galls me)
		if (feedback) Con_Printf ("Loaded %i skybox components\n", numloaded);

		// the skybox is valid now, no need to search any more
		SkyboxValid = true;
		strcpy (CachedSkyBoxName, basename);
		return;
	}

	if (feedback) Con_Printf ("Failed to load skybox\n");
}


void R_Loadsky (void)
{
	if (Cmd_Argc () != 2)
	{
		Con_Printf ("loadsky <skybox> : loads a skybox\n");
		return;
	}

	// send through the common loader
	R_LoadSkyBox (Cmd_Argv (1), true);
}


void R_RevalidateSkybox (void)
{
	// revalidates the currently loaded skybox
	// need to copy out cached name as we're going to change it
	char basename[260];

	strcpy (basename, CachedSkyBoxName);
	R_LoadSkyBox (basename, false);
}


cmd_t Loadsky_Cmd ("loadsky", R_Loadsky);


