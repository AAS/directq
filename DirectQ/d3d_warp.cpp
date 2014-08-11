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
#include "d3d_quake.h"
#include "d3d_hlsl.h"

// r_lightmap 1 uses the grey texture on account of 2 x overbrighting
extern LPDIRECT3DTEXTURE9 r_greytexture;
extern cvar_t r_lightmap;

extern	model_t	*loadmodel;

LPDIRECT3DTEXTURE9 underwatertexture = NULL;
LPDIRECT3DSURFACE9 underwatersurface = NULL;
LPDIRECT3DTEXTURE9 simpleskytexture = NULL;
LPDIRECT3DTEXTURE9 solidskytexture = NULL;
LPDIRECT3DTEXTURE9 alphaskytexture = NULL;
LPDIRECT3DTEXTURE9 skyboxtextures[6] = {NULL};
LPDIRECT3DINDEXBUFFER9 d3d_DPSkyIndexes = NULL;
LPDIRECT3DVERTEXBUFFER9 d3d_DPSkyVerts = NULL;

// the menu needs to know the name of the loaded skybox
char CachedSkyBoxName[MAX_PATH] = {0};
bool SkyboxValid = false;

/*
==============================================================================================================================

		UNDERWATER WARP

==============================================================================================================================
*/

bool UnderwaterValid = false;
cvar_t r_waterwarp ("r_waterwarp", 1, CVAR_ARCHIVE);
bool d3d_UpdateWarp = false;

typedef struct uwvert_s
{
	float x, y, z;
	float s1, t1;
	float s2, t2;
} uwvert_t;

uwvert_t *underwaterverts = NULL;

float CompressST (float in)
{
	// compress the texture slightly so that the edges of the screen look right
	float out = in;

	// 0 to 1
	out /= 32.0f;

	// 1 to 99
	out *= 98.0f;
	out += 1.0f;

	// back to 0 to 1 scale
	out /= 100.0f;

	// back to 0 to 32 scale
	out *= 32.0f;

	return out;
}


void D3D_InitUnderwaterTexture (void)
{
	// alloc underwater verts one time only
	// store 2 extra s/t so we can cache the original values for updates
	if (!underwaterverts) underwaterverts = (uwvert_t *) Pool_Alloc (POOL_PERMANENT, 2112 * sizeof (uwvert_t));

	// ensure that it's gone before creating
	SAFE_RELEASE (underwatersurface);
	SAFE_RELEASE (underwatertexture);

	// assume that it's invalid until we prove otherwise
	UnderwaterValid = false;

	// create the update texture as a rendertarget at half screen resolution
	// rendertargets seem to relax power of 2 requirements, best of luck finding THAT in the documentation
	hr = d3d_Device->CreateTexture
	(
		d3d_CurrentMode.Width,
		d3d_CurrentMode.Height,
		1,
		D3DUSAGE_RENDERTARGET,
		d3d_GlobalCaps.supportXRGB ? D3DFMT_X8R8G8B8 : D3DFMT_A8R8G8B8,
		D3DPOOL_DEFAULT,
		&underwatertexture,
		NULL
	);

	// couldn't create it so don't do underwater updates
	if (FAILED (hr)) return;

	// we're good now
	UnderwaterValid = true;

	// tesselation for warps
	uwvert_t *wv = underwaterverts;
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
			wv->s1 = CompressST (s);
			wv->t1 = CompressST (t);
			wv->s2 = wv->s1;
			wv->t2 = wv->t1;
			wv++;

			wv->x = (x + tessw);
			wv->y = y;
			wv->z = 0;
			wv->s1 = CompressST (s + 1);
			wv->t1 = CompressST (t);
			wv->s2 = wv->s1;
			wv->t2 = wv->t1;
			wv++;
		}
	}
}


void D3D_KillUnderwaterTexture (void)
{
	// just release it
	SAFE_RELEASE (underwatersurface);
	SAFE_RELEASE (underwatertexture);
}


void D3D_BeginUnderwaterWarp (void)
{
	// couldn't create the underwater texture or we don't want to warp
	if (!UnderwaterValid || (r_waterwarp.integer != 1)) return;

	// no warps present on these leafs
	if (d3d_RenderDef.viewleaf->contents == CONTENTS_EMPTY ||
		d3d_RenderDef.viewleaf->contents == CONTENTS_SKY ||
		d3d_RenderDef.viewleaf->contents == CONTENTS_SOLID) return;

	extern LPDIRECT3DSURFACE9 d3d_BackBuffer;

	// store out the backbuffer
	hr = d3d_Device->GetRenderTarget (0, &d3d_BackBuffer);

	if (FAILED (hr)) return;

	// get the surface to render to
	// (note - it *might* seem more efficient to keep this open all the time, but it's a HORRENDOUS slowdown,
	// even when we're not rendering any underwater surfs...)
	hr = underwatertexture->GetSurfaceLevel (0, &underwatersurface);

	if (FAILED (hr))
	{
		SAFE_RELEASE (d3d_BackBuffer);
		return;
	}

	// set the underwater surface as the rendertarget
	hr = d3d_Device->SetRenderTarget (0, underwatersurface);

	if (FAILED (hr))
	{
		SAFE_RELEASE (d3d_BackBuffer);
		return;
	}

	// flag that we need to draw the warp update
	d3d_UpdateWarp = true;
}


void D3D_EndUnderwaterWarp (void)
{
	// no warps
	if (!d3d_UpdateWarp) return;

	SAFE_RELEASE (underwatersurface);

	extern LPDIRECT3DSURFACE9 d3d_BackBuffer;

	// restore backbuffer (we expect this to work because we had previously set a rendertarget successfully)
	d3d_Device->SetRenderTarget (0, d3d_BackBuffer);

	// destroy the surfaces
	SAFE_RELEASE (d3d_BackBuffer);
}


cvar_t r_waterwarptime ("r_waterwarptime", 2.0f, CVAR_ARCHIVE);
cvar_t r_waterwarpscale ("r_waterwarpscale", 0.125f, CVAR_ARCHIVE);

void D3D_DrawUnderwaterWarp (void)
{
	// no warps
	if (!d3d_UpdateWarp) return;

	// for the next frame
	d3d_UpdateWarp = false;

	// this causes Z-fighting artefacts on alpha-blended overlay surfs, so send it through T&L
	d3d_WorldMatrixStack->Reset ();
	d3d_WorldMatrixStack->LoadIdentity ();

	d3d_ViewMatrixStack->Reset ();
	d3d_ViewMatrixStack->LoadIdentity ();

	d3d_ProjMatrixStack->Reset ();
	d3d_ProjMatrixStack->LoadIdentity ();
	d3d_ProjMatrixStack->Ortho2D (0, d3d_CurrentMode.Width, d3d_CurrentMode.Height, 0, 0, 1);

	// ensure
	D3D_DisableAlphaBlend ();

	D3D_SetTextureColorMode (0, D3DTOP_SELECTARG1, D3DTA_TEXTURE, D3DTA_DIFFUSE);
	D3D_SetTextureAlphaMode (0, D3DTOP_DISABLE);

	D3D_SetTextureColorMode (1, D3DTOP_DISABLE);
	D3D_SetTextureAlphaMode (1, D3DTOP_DISABLE);

	D3D_SetTextureColorMode (2, D3DTOP_DISABLE);
	D3D_SetTextureAlphaMode (2, D3DTOP_DISABLE);

	// T&L used - see above
	D3D_SetFVF (D3DFVF_XYZ | D3DFVF_TEX1);
	D3D_SetTextureAddressMode (D3DTADDRESS_CLAMP);
	D3D_SetTexture (0, underwatertexture);

	float rdt = cl.time * r_waterwarptime.value * 0.5f;

	for (int i = 0, v = 0; i < 32; i++, v += 66)
	{
		// retrieve the current verts
		uwvert_t *wv = underwaterverts + v;
		uwvert_t *verts = wv;

		// there's a potential optimization in here... we could precalc the warps as up to 4 verts will be shared...
		for (int vv = 0; vv < 66; vv++, verts++)
		{
			// we stashed the originally generated coords in verts 6 and 7 so bring them back to
			// 4 and 5, warping as we go, for the render
			verts->s1 = (verts->s2 + sin (verts->t2 + rdt) * r_waterwarpscale.value) * 0.03125f;
			verts->t1 = (verts->t2 + sin (verts->s2 + rdt) * r_waterwarpscale.value) * 0.03125f;
		}

		D3D_DrawPrimitive (D3DPT_TRIANGLESTRIP, 64, wv, sizeof (uwvert_t));
	}
}


/*
==============================================================================================================================

		WARP SURFACE GENERATION

==============================================================================================================================
*/

typedef struct glwarpvert_s
{
	// this needs to cache the originally generated verts for transforms and the
	// originally generated texcoords for warps
	float xyz[3];
	float st[2];
	float xyz2[3];
	float st2[2];
} glwarpvert_t;

void BoundPoly (int numverts, float *verts, vec3_t mins, vec3_t maxs)
{
	mins[0] = mins[1] = mins[2] = 9999;
	maxs[0] = maxs[1] = maxs[2] = -9999;
	float *v = verts;

	for (int i = 0; i < numverts; i++, v += 3)
	{
		for (int j = 0; j < 3; j++)
		{
			if (v[j] < mins[j]) mins[j] = v[j];
			if (v[j] > maxs[j]) maxs[j] = v[j];
		}
	}
}

void D3D_InitSurfMinMaxs (msurface_t *surf);
void D3D_CheckSurfMinMaxs (msurface_t *surf, float *v);


cvar_t gl_subdivide_size ("gl_subdivide_size", 128, CVAR_ARCHIVE);
float d3d_subdivide_size = 128.0f;

void SubdividePolygon (msurface_t *warpface, int numverts, float *verts)
{
	vec3_t	mins, maxs;
	vec3_t	front[64], back[64];
	float	dist[64];

	if (numverts > 60) Sys_Error ("numverts = %i", numverts);

	BoundPoly (numverts, verts, mins, maxs);

	for (int i = 0; i < 3; i++)
	{
		float m = (mins[i] + maxs[i]) * 0.5;
		m = d3d_subdivide_size * floor (m / d3d_subdivide_size + 0.5);

		if (maxs[i] - m < 8) continue;
		if (m - mins[i] < 8) continue;

		// cut it
		float *v = verts + i;

		for (int j = 0; j < numverts; j++, v += 3)
			dist[j] = *v - m;

		// wrap cases
		dist[numverts] = dist[0];
		v -= i;
		VectorCopy (verts, v);

		int f = 0;
		int b = 0;
		v = verts;

		for (int j = 0; j < numverts; j++, v += 3)
		{
			if (dist[j] >= 0)
			{
				VectorCopy (v, front[f]);
				f++;
			}

			if (dist[j] <= 0)
			{
				VectorCopy (v, back[b]);
				b++;
			}

			if (dist[j] == 0 || dist[j + 1] == 0) continue;

			if ((dist[j] > 0) != (dist[j + 1] > 0))
			{
				// clip point
				float frac = dist[j] / (dist[j] - dist[j + 1]);

				for (int k = 0; k < 3; k++)
					front[f][k] = back[b][k] = v[k] + frac * (v[3 + k] - v[k]);

				f++;
				b++;
			}
		}

		SubdividePolygon (warpface, f, front[0]);
		SubdividePolygon (warpface, b, back[0]);
		return;
	}

	// alloc this poly
	glpoly_t *poly = (glpoly_t *) Pool_Alloc (POOL_MAP, sizeof (glpoly_t));
	glwarpvert_t *warpverts = (glwarpvert_t *) Pool_Alloc (POOL_MAP, numverts * sizeof (glwarpvert_t));

	poly->verts = (glpolyvert_t *) warpverts;
	poly->numverts = numverts;
	poly->next = warpface->polys;
	warpface->polys = poly;

	for (int i = 0; i < numverts; i++, verts += 3, warpverts++)
	{
		VectorCopy (verts, warpverts->xyz);
		VectorCopy (verts, warpverts->xyz2);

		// check these too
		D3D_CheckSurfMinMaxs (warpface, verts);

		float s = DotProduct (verts, warpface->texinfo->vecs[0]);
		float t = DotProduct (verts, warpface->texinfo->vecs[1]);

		// cache the texcoords in both st and lm
		warpverts->st[0] = warpverts->st2[0] = s;
		warpverts->st[1] = warpverts->st2[1] = t;
	}
}


void GL_SubdivideWater (msurface_t *surf, model_t *mod)
{
	vec3_t verts[64];
	float *vec;

	// eval subdivide size - sanity check and store to global for keeping things consistent
	if (gl_subdivide_size.value < 1) Cvar_Set (&gl_subdivide_size, 1.0f);
	d3d_subdivide_size = gl_subdivide_size.value;

	// convert edges back to a normal polygon
	int numverts = 0;

	for (int i = 0; i < surf->numedges; i++)
	{
		int lindex = mod->bh->surfedges[surf->firstedge + i];

		if (lindex > 0)
			vec = mod->bh->vertexes[mod->bh->edges[lindex].v[0]].position;
		else vec = mod->bh->vertexes[mod->bh->edges[-lindex].v[1]].position;

		VectorCopy (vec, verts[numverts]);
		numverts++;
	}

	// min and max for automap
	D3D_InitSurfMinMaxs (surf);

	// now subdivide it
	SubdividePolygon (surf, numverts, verts[0]);

	// the first poly is the undivided one for hlsl rendering so now we need to rebuild that
	glpoly_t *poly = (glpoly_t *) Pool_Alloc (POOL_MAP, sizeof (glpoly_t));
	glwarpvert_t *warpverts = (glwarpvert_t *) Pool_Alloc (POOL_MAP, numverts * sizeof (glwarpvert_t));

	poly->verts = (glpolyvert_t *) warpverts;
	poly->numverts = numverts;
	poly->next = surf->polys;
	surf->polys = poly;

	for (int i = 0; i < surf->numedges; i++, warpverts++)
	{
		int lindex = mod->bh->surfedges[surf->firstedge + i];

		if (lindex > 0)
			vec = mod->bh->vertexes[mod->bh->edges[lindex].v[0]].position;
		else vec = mod->bh->vertexes[mod->bh->edges[-lindex].v[1]].position;

		VectorCopy (vec, warpverts->xyz);
		VectorCopy (vec, warpverts->xyz2);

		// check these too
		D3D_CheckSurfMinMaxs (surf, vec);

		float s = DotProduct (vec, surf->texinfo->vecs[0]);
		float t = DotProduct (vec, surf->texinfo->vecs[1]);

		// cache the texcoords in both st and lm
		warpverts->st[0] = warpverts->st2[0] = s;
		warpverts->st[1] = warpverts->st2[1] = t;
	}
}


/*
==============================================================================================================================

		WATER WARP RENDERING

==============================================================================================================================
*/


/*
================
D3D_DrawAlphaWaterSurfaces
================
*/
cvar_t r_lavaalpha ("r_lavaalpha", 1, CVAR_ARCHIVE);
cvar_t r_telealpha ("r_telealpha", 1, CVAR_ARCHIVE);
cvar_t r_slimealpha ("r_slimealpha", 1, CVAR_ARCHIVE);
cvar_t r_warpspeed ("r_warpspeed", 4, CVAR_ARCHIVE);
cvar_t r_warpscale ("r_warpscale", 8, CVAR_ARCHIVE);
cvar_t r_warpfactor ("r_warpfactor", 2, CVAR_ARCHIVE);

// lock water/slime/tele/lava alpha sliders
cvar_t r_lockalpha ("r_lockalpha", "1", CVAR_ARCHIVE);

// water ripple
cvar_t r_waterripple ("r_waterripple", "0", CVAR_ARCHIVE);

float *warpsin = NULL;
float warptime = 10;
float warpfactor = 1;

void R_InitWarp (void)
{
	int i;

	warpsin = (float *) Pool_Alloc (POOL_PERMANENT, 256 * sizeof (float));

	for (i = 0; i < 256; i++)
	{
		float f = (float) i / 40.743665f;

		// just take the sin so that we can multiply it by a variable warp factor
		warpsin[i] = sin (f);
	}
}


// this has been cvar-ized to allow user control of the warps; the defaults mimic software quake
#define WARPCALC(s,t) ((s + warpsin[(int) ((t * warpfactor) + warptime) & 255] * r_warpscale.value) * 0.015625f)

void D3D_WarpSurfacePolygon (glpoly_t *p)
{
	glwarpvert_t *v = (glwarpvert_t *) p->verts;

	// this has been set up so that the same warpcalc def can be used for both size ranges
	for (int i = 0; i < p->numverts; i++, v++)
	{
		// fixme - put in a lookup!!!
		// this is not currently supported with hlsl owing to the fact that we don't tesselate the surf so much
		if (r_waterripple.value) v->xyz[2] = v->xyz[2] + r_waterripple.value * sin (v->xyz[0] * 0.05 + cl.time * 3) * sin (v->xyz[1] * 0.05 + cl.time * 3);

		// we cached a copy of the texcoords in st2 so that we can do this live
		v->st[0] = WARPCALC (v->st2[0], v->st2[1]);
		v->st[1] = WARPCALC (v->st2[1], v->st2[0]);
	}
}


void D3D_RestoreWarpVerts (glpoly_t *p)
{
	glwarpvert_t *v = (glwarpvert_t *) p->verts;

	for (int i = 0; i < p->numverts; i++, v++)
	{
		v->xyz[0] = v->xyz2[0];
		v->xyz[1] = v->xyz2[1];
		v->xyz[2] = v->xyz2[2];
	}
}


void D3D_TransformWaterSurface (msurface_t *surf)
{
	D3DXMATRIX *m = (D3DXMATRIX *) surf->matrix;

	if (d3d_GlobalCaps.usingPixelShaders)
	{
		// one poly only
		glpoly_t *p = surf->polys;
		glwarpvert_t *v = (glwarpvert_t *) p->verts;

		for (int i = 0; i < p->numverts; i++, v++)
		{
			// transform them and copy out
			v->xyz[0] = v->xyz2[0] * m->_11 + v->xyz2[1] * m->_21 + v->xyz2[2] * m->_31 + m->_41;
			v->xyz[1] = v->xyz2[0] * m->_12 + v->xyz2[1] * m->_22 + v->xyz2[2] * m->_32 + m->_42;
			v->xyz[2] = v->xyz2[0] * m->_13 + v->xyz2[1] * m->_23 + v->xyz2[2] * m->_33 + m->_43;
		}
	}
	else
	{
		// start at the poly after the first
		for (glpoly_t *p = surf->polys->next; p; p = p->next)
		{
			glwarpvert_t *v = (glwarpvert_t *) p->verts;

			for (int i = 0; i < p->numverts; i++, v++)
			{
				// transform them and copy out
				v->xyz[0] = v->xyz2[0] * m->_11 + v->xyz2[1] * m->_21 + v->xyz2[2] * m->_31 + m->_41;
				v->xyz[1] = v->xyz2[0] * m->_12 + v->xyz2[1] * m->_22 + v->xyz2[2] * m->_32 + m->_42;
				v->xyz[2] = v->xyz2[0] * m->_13 + v->xyz2[1] * m->_23 + v->xyz2[2] * m->_33 + m->_43;
			}
		}
	}
}


byte d3d_WaterAlpha = 255;
byte d3d_LavaAlpha = 255;
byte d3d_SlimeAlpha = 255;
byte d3d_TeleAlpha = 255;

void D3D_PrepareWaterSurfaces (void)
{
	// create the sintable (done whether or not water is drawn so that is always happens in frame 0
	// instead of during active gameplay; we should push this off to a one-time setup but we're lazy
	if (!warpsin) R_InitWarp ();

	// prevent state changes if water ain't being drawn
	if (!(d3d_RenderDef.renderflags & R_RENDERWATERSURFACE)) return;

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

	// check for turning translucency on
	if (d3d_WaterAlpha < 255 || d3d_LavaAlpha < 255 || d3d_SlimeAlpha < 255 || d3d_TeleAlpha < 255) d3d_RenderDef.renderflags |= R_RENDERALPHAWATER;
	if (d3d_WaterAlpha == 255 || d3d_LavaAlpha == 255 || d3d_SlimeAlpha == 255 || d3d_TeleAlpha == 255) d3d_RenderDef.renderflags |= R_RENDEROPAQUEWATER;

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

	// warpfactor is only variable for fixed, hlsl always uses r_warpfactor.value
	if (d3d_subdivide_size > 48)
		warpfactor = r_warpfactor.value * 2.546479f;
	else warpfactor = r_warpfactor.value;
}


void R_DrawWaterChain (texture_t *t)
{
	if (d3d_GlobalCaps.usingPixelShaders)
	{
		// liquids should be affected by r_lightmap 1
		if (r_lightmap.integer)
			d3d_LiquidFX.SetTexture ("baseTexture", r_greytexture);
		else d3d_LiquidFX.SetTexture ("baseTexture", (LPDIRECT3DTEXTURE9) t->d3d_Texture);

		// draw all the surfaces
		for (msurface_t *surf = t->texturechain; surf; surf = surf->texturechain)
		{
			// one poly only
			glpoly_t *p = surf->polys;
			d3d_LiquidFX.Draw (D3DPT_TRIANGLEFAN, p->numverts - 2, p->verts, sizeof (glwarpvert_t));
			d3d_RenderDef.brush_polys++;
		}
	}
	else
	{
		// liquids should be affected by r_lightmap 1
		if (r_lightmap.integer)
			D3D_SetTexture (0, r_greytexture);
		else D3D_SetTexture (0, (LPDIRECT3DTEXTURE9) t->d3d_Texture);

		// draw all the surfaces
		for (msurface_t *surf = t->texturechain; surf; surf = surf->texturechain)
		{
			// start at the poly after the first
			for (glpoly_t *p = surf->polys->next; p; p = p->next)
			{
				// note - we can use d3d_LiquidFX.Draw here...
				D3D_WarpSurfacePolygon (p);
				d3d_LiquidFX.Draw (D3DPT_TRIANGLEFAN, p->numverts - 2, p->verts, sizeof (glwarpvert_t));
				D3D_RestoreWarpVerts (p);

				d3d_RenderDef.brush_polys++;
			}
		}
	}
}


void D3D_WaterCommonState (void)
{
	D3D_SetTextureMipmap (0, d3d_3DFilterMag, d3d_3DFilterMin, d3d_3DFilterMip);

	if (d3d_GlobalCaps.usingPixelShaders)
	{
		d3d_Device->SetVertexDeclaration (d3d_LiquidDeclaration);
		d3d_LiquidFX.BeginRender ();

		d3d_LiquidFX.SetMatrix ("WorldMatrix", d3d_WorldMatrixStack->GetTop ());
		d3d_LiquidFX.SetMatrix ("ViewMatrix", d3d_ViewMatrixStack->GetTop ());
		d3d_LiquidFX.SetMatrix ("ProjMatrix", d3d_ProjMatrixStack->GetTop ());

		d3d_LiquidFX.SetFloat ("warptime", warptime);
		d3d_LiquidFX.SetFloat ("warpfactor", r_warpfactor.value);
		d3d_LiquidFX.SetFloat ("warpscale", r_warpscale.value);
	}
	//else
	{
		D3D_SetTextureAddressMode (D3DTADDRESS_WRAP);
		D3D_SetTexCoordIndexes (0);

		D3D_SetTextureColorMode (1, D3DTOP_DISABLE);
		D3D_SetTextureAlphaMode (1, D3DTOP_DISABLE);

		D3D_SetTextureColorMode (2, D3DTOP_DISABLE);
		D3D_SetTextureAlphaMode (2, D3DTOP_DISABLE);
	}
}


void D3D_DrawOpaqueWaterSurfaces (void)
{
	// prevent state changes if opaque water ain't being drawn
	if (!(d3d_RenderDef.renderflags & R_RENDEROPAQUEWATER)) return;

	msurface_t *surf;
	texture_t *t;

	// always do these even with hlsl so that the internal state tracking will update properly
	if (!d3d_GlobalCaps.usingPixelShaders) D3D_SetFVF (D3DFVF_XYZ | D3DFVF_TEX1);

	D3D_DisableAlphaBlend ();
	D3D_WaterCommonState ();

	if (d3d_GlobalCaps.usingPixelShaders)
	{
		d3d_LiquidFX.SetFloat ("Alpha", 1.0f);
		d3d_LiquidFX.SwitchToPass (0);
	}
	//else
	{
		D3D_SetTextureColorMode (0, D3DTOP_SELECTARG1, D3DTA_TEXTURE, D3DTA_DIFFUSE);
		D3D_SetTextureAlphaMode (0, D3DTOP_DISABLE);
	}

	for (int i = 0; i < cl.worldbrush->numtextures; i++)
	{
		// no texture (e2m3 gets this)
		if (!(t = cl.worldbrush->textures[i])) continue;

		// nothing to draw for this texture
		if (!(surf = t->texturechain)) continue;

		// skip over
		if (!(surf->flags & SURF_DRAWTURB)) continue;

		// skip alpha surfaces
		if ((surf->flags & SURF_DRAWLAVA) && d3d_LavaAlpha < 255) continue;
		if ((surf->flags & SURF_DRAWTELE) && d3d_TeleAlpha < 255) continue;
		if ((surf->flags & SURF_DRAWSLIME) && d3d_SlimeAlpha < 255) continue;
		if ((surf->flags & SURF_DRAWWATER) && d3d_WaterAlpha < 255) continue;

		// draw it
		R_DrawWaterChain (t);
	}

	// finish up the hlsl render if using it
	if (d3d_GlobalCaps.usingPixelShaders) d3d_LiquidFX.EndRender ();
}


void D3D_DrawAlphaWaterSurfaces (void)
{
	// prevent state changes if alpha water ain't being drawn
	if (!(d3d_RenderDef.renderflags & R_RENDERALPHAWATER)) return;

	// set up for warping
	// always do these even with hlsl so that the internal state tracking will update properly
	if (!d3d_GlobalCaps.usingPixelShaders) D3D_SetFVF (D3DFVF_XYZ | D3DFVF_TEX1);

	msurface_t *surf;
	texture_t *t;

	// enable translucency
	D3D_EnableAlphaBlend (D3DBLENDOP_ADD, D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA);
	D3D_WaterCommonState ();

	if (d3d_GlobalCaps.usingPixelShaders)
		d3d_LiquidFX.SwitchToPass (0);
	//else
	{
		D3D_SetTextureColorMode (0, D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_CONSTANT);
		D3D_SetTextureAlphaMode (0, D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_CONSTANT);
	}

	for (int i = 0; i < cl.worldbrush->numtextures; i++)
	{
		// no texture (e2m3 gets this)
		if (!(t = cl.worldbrush->textures[i])) continue;

		// nothing to draw for this texture
		if (!(surf = t->texturechain)) continue;

		// skip over
		if (!(surf->flags & SURF_DRAWTURB)) continue;

		// skip opaque surfaces
		if ((surf->flags & SURF_DRAWLAVA) && d3d_LavaAlpha == 255) continue;
		if ((surf->flags & SURF_DRAWTELE) && d3d_TeleAlpha == 255) continue;
		if ((surf->flags & SURF_DRAWSLIME) && d3d_SlimeAlpha == 255) continue;
		if ((surf->flags & SURF_DRAWWATER) && d3d_WaterAlpha == 255) continue;

		if (d3d_GlobalCaps.usingPixelShaders)
		{
			if (surf->flags & SURF_DRAWLAVA)
				d3d_LiquidFX.SetFloat ("Alpha", (float) d3d_LavaAlpha / 255.0f);
			else if (surf->flags & SURF_DRAWTELE)
				d3d_LiquidFX.SetFloat ("Alpha", (float) d3d_TeleAlpha / 255.0f);
			else if (surf->flags & SURF_DRAWSLIME)
				d3d_LiquidFX.SetFloat ("Alpha", (float) d3d_SlimeAlpha / 255.0f);
			else d3d_LiquidFX.SetFloat ("Alpha", (float) d3d_WaterAlpha / 255.0f);
		}
		else
		{
			if (surf->flags & SURF_DRAWLAVA)
				D3D_SetTextureStageState (0, D3DTSS_CONSTANT, D3DCOLOR_ARGB (d3d_LavaAlpha, 255, 255, 255));
			else if (surf->flags & SURF_DRAWTELE)
				D3D_SetTextureStageState (0, D3DTSS_CONSTANT, D3DCOLOR_ARGB (d3d_TeleAlpha, 255, 255, 255));
			else if (surf->flags & SURF_DRAWSLIME)
				D3D_SetTextureStageState (0, D3DTSS_CONSTANT, D3DCOLOR_ARGB (d3d_SlimeAlpha, 255, 255, 255));
			else D3D_SetTextureStageState (0, D3DTSS_CONSTANT, D3DCOLOR_ARGB (d3d_WaterAlpha, 255, 255, 255));
		}

		// draw it
		R_DrawWaterChain (t);
	}

	// take down
	D3D_DisableAlphaBlend ();

	// finish up the hlsl render if using it
	if (d3d_GlobalCaps.usingPixelShaders)
		d3d_LiquidFX.EndRender ();
	//else
	{
		D3D_SetTextureStageState (0, D3DTSS_CONSTANT, 0xffffffff);
	}
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

// let's distinguish properly between preproccessor defines and variables
// (LH apparently doesn't believe in this, but I do)
#define SKYGRID_SIZE 16
#define SKYGRID_SIZE_PLUS_1 (SKYGRID_SIZE + 1)
#define SKYGRID_RECIP (1.0f / (SKYGRID_SIZE))
#define SKYSPHERE_NUMVERTS (SKYGRID_SIZE_PLUS_1 * SKYGRID_SIZE_PLUS_1)
#define SKYSPHERE_NUMTRIS (SKYGRID_SIZE * SKYGRID_SIZE * 2)

typedef struct warpverts_s
{
	float x, y, z;
	float s, t;
} skyverts_t;

cvar_t r_skywarp ("r_skywarp", 1, CVAR_ARCHIVE);
cvar_t r_skybackscroll ("r_skybackscroll", 8, CVAR_ARCHIVE);
cvar_t r_skyfrontscroll ("r_skyfrontscroll", 16, CVAR_ARCHIVE);
cvar_t r_skyalpha ("r_skyalpha", 1, CVAR_ARCHIVE);

// dynamic far clip plane for sky
extern float r_clipdist;

unsigned *skyalphatexels = NULL;

void R_ClipSky (void)
{
	// disable textureing and writes to the color buffer
	D3D_SetRenderState (D3DRS_COLORWRITEENABLE, 0);
	D3D_SetFVF (D3DFVF_XYZ);

	for (msurface_t *surf = d3d_RenderDef.skychain; surf; surf = surf->texturechain)
	{
		// fixme - only need to store 3 floats for these - cut down on geometry going to the card
		D3D_DrawPrimitive (D3DPT_TRIANGLEFAN, surf->polys->numverts - 2, surf->polys->verts, sizeof (glpolyvert_t));
		d3d_RenderDef.brush_polys++;
	}

	// revert state
	D3D_SetRenderState (D3DRS_COLORWRITEENABLE, 0x0000000F);
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
	D3DXLoadSurfaceFromSurface (CopySurf, NULL, NULL, skysurf, NULL, NULL, D3DX_FILTER_NONE, 0);
	CopySurf->LockRect (&Level0Rect, NULL, D3DLOCK_NO_DIRTY_UPDATE);

	// alloc texels
	if (!skyalphatexels)
	{
		skyalphatexels = (unsigned *) Pool_Alloc (POOL_MAP, Level0Desc.Width * Level0Desc.Height * sizeof (unsigned));
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
	D3DXLoadSurfaceFromSurface (skysurf, NULL, NULL, CopySurf, NULL, NULL, D3DX_FILTER_NONE, 0);

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

// for restoring fog
void D3D_PerpareFog (void);

void D3D_DrawSkySphere (float scale)
{
	D3D_UpdateSkyAlpha ();

	if (gl_fogenable.integer && gl_fogsky.integer)
	{
		// switch to the new fog modes for sky - just fog it as if it was further away
		D3D_SetRenderState (D3DRS_FOGVERTEXMODE, D3DFOG_LINEAR);
		D3D_SetRenderState (D3DRS_FOGTABLEMODE, D3DFOG_NONE);
		D3D_SetRenderStatef (D3DRS_FOGEND, gl_fogend.value * 20 / 8192.0f * r_clipdist);
	}

	// darkplaces warp
	// for all that they say "you VILL use ein index buffer, heil Bill", NOWHERE in the SDK is it
	// documented that you need to call SetIndices to tell the device the index buffer to use.  Aside
	// from the entry for SetIndices, of course, but NOWHERE else.  C'mon Microsoft, GET IT FUCKING RIGHT!
	d3d_Device->SetStreamSource (0, d3d_DPSkyVerts, 0, sizeof (skyverts_t));
	d3d_Device->SetIndices (d3d_DPSkyIndexes);

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

	d3d_WorldMatrixStack->Scale (scale, scale, scale);

	// sky should be affected by r_lightmap 1 too
	if (r_lightmap.integer)
	{
		// don't bother with the ops or the scrolling
		D3D_SetTexture (0, r_greytexture);
		D3D_SetTexture (1, r_greytexture);
	}
	else
	{
		float speedscale;
		D3DXMATRIX scrollmatrix;
		D3DXMatrixIdentity (&scrollmatrix);

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
	}

	// all that just for this...
	D3D_DrawPrimitive (D3DPT_TRIANGLELIST, 0, 0, SKYSPHERE_NUMVERTS, 0, SKYSPHERE_NUMTRIS);

	// restore old fog
	if (gl_fogenable.integer && gl_fogsky.integer)
		D3D_PerpareFog ();

	// take down specific stuff
	D3D_SetTextureMatrixOp (D3DTTFF_DISABLE, D3DTTFF_DISABLE);
	D3D_SetTextureStageState (0, D3DTSS_CONSTANT, 0xffffffff);
	D3D_SetTextureStageState (1, D3DTSS_CONSTANT, 0xffffffff);
}


int	st_to_vec[6][3] = 
{
	{3, -1, 2},
	{-3, 1, 2},

	{1, 3, 2},
	{-1, -3, 2},

	{-2, -1, 3},
	{2, -1, -3}
};

float sky_min, sky_max;

typedef struct skyboxquad_s
{
	float v[3];
	float st[2];
} skyboxquad_t;

skyboxquad_t sbquad[4];

void MakeSkyVec (float s, float t, int axis, int vert)
{
	vec3_t b;
	int j, k;

	// fill in verts
	b[0] = s * r_clipdist;
	b[1] = t * r_clipdist;
	b[2] = r_clipdist;

	for (j = 0; j < 3; j++)
	{
		k = st_to_vec[axis][j];

		if (k < 0)
			sbquad[vert].v[j] = -b[-k - 1];
		else sbquad[vert].v[j] = b[k - 1];
	}

	// fill in texcoords
	// avoid bilerp seam
	sbquad[vert].st[0] = (s + 1) * 0.5;
	sbquad[vert].st[1] = (t + 1) * 0.5;

	// clamp
	if (sbquad[vert].st[0] < sky_min) sbquad[vert].st[0] = sky_min; else if (sbquad[vert].st[0] > sky_max) sbquad[vert].st[0] = sky_max;
	if (sbquad[vert].st[1] < sky_min) sbquad[vert].st[1] = sky_min; else if (sbquad[vert].st[1] > sky_max) sbquad[vert].st[1] = sky_max;

	// invert t
	sbquad[vert].st[1] = 1.0 - sbquad[vert].st[1];
}


void R_DrawSkybox ()
{
	int i;
	float skymins[2] = {-1, -1};
	float skymaxs[2] = {1, 1};
	int	skytexorder[6] = {0, 2, 1, 3, 4, 5};

	sky_min = 1.0 / 512;
	sky_max = 511.0 / 512;

	// note - this looks ugly as fuck on skyboxes on account of corners; i've tried it with per-pixel and
	// with exp and exp2 mode, but the effect remains.  the solution seems to be to project the skybox onto
	// a sphere of some kind.  maybe later.
	if (gl_fogenable.integer && gl_fogsky.integer)
	{
		// switch to the new fog modes for sky - just fog it as if it was further away
		D3D_SetRenderState (D3DRS_FOGVERTEXMODE, D3DFOG_LINEAR);
		D3D_SetRenderState (D3DRS_FOGTABLEMODE, D3DFOG_NONE);
		D3D_SetRenderStatef (D3DRS_FOGEND, gl_fogend.value * 20.0f / 8192.0f * r_clipdist);
	}

	D3D_SetFVF (D3DFVF_XYZ | D3DFVF_TEX1);

	for (i = 0; i < 6; i++)
	{
		// we allowed the skybox to load if any of the components were not found (crazy modders!), so we must account for that here
		if (!skyboxtextures[skytexorder[i]]) continue;

		// sky should be affected by r_lightmap 1 too
		if (r_lightmap.integer)
			D3D_SetTexture (0, r_greytexture);
		else D3D_SetTexture (0, skyboxtextures[skytexorder[i]]);

		MakeSkyVec (skymins[0], skymins[1], i, 0);
		MakeSkyVec (skymins[0], skymaxs[1], i, 1);
		MakeSkyVec (skymaxs[0], skymaxs[1], i, 2);
		MakeSkyVec (skymaxs[0], skymins[1], i, 3);

		D3D_DrawPrimitive (D3DPT_TRIANGLEFAN, 2, sbquad, sizeof (skyboxquad_t));
		d3d_RenderDef.brush_polys++;
	}

	// restore old fog
	if (gl_fogenable.integer && gl_fogsky.integer)
		D3D_PerpareFog ();
}


void R_DrawSimpleSky (msurface_t *skychain)
{
	// disable textureing and writes to the color buffer
	D3D_SetTextureColorMode (0, D3DTOP_SELECTARG1, D3DTA_TEXTURE, D3DTA_DIFFUSE);
	D3D_SetTextureAlphaMode (0, D3DTOP_DISABLE);

	D3D_SetTextureColorMode (1, D3DTOP_DISABLE);
	D3D_SetTextureAlphaMode (1, D3DTOP_DISABLE);

	D3D_SetTextureColorMode (2, D3DTOP_DISABLE);
	D3D_SetTextureAlphaMode (2, D3DTOP_DISABLE);

	D3D_SetFVF (D3DFVF_XYZ | D3DFVF_TEX1);
	D3D_SetTexture (0, simpleskytexture);

	for (msurface_t *surf = skychain; surf; surf = surf->texturechain)
	{
		// fixme - only need to store 3 floats for these - cut down on geometry going to the card
		D3D_DrawPrimitive (D3DPT_TRIANGLEFAN, surf->polys->numverts - 2, surf->polys->verts, sizeof (glpolyvert_t));
		d3d_RenderDef.brush_polys++;
	}
}


void D3D_DrawShaderSky (msurface_t *skychain)
{
	float speedscale[] = {cl.time * r_skybackscroll.value, cl.time * r_skyfrontscroll.value, 1};

	speedscale[0] -= (int) speedscale[0] & ~127;
	speedscale[1] -= (int) speedscale[1] & ~127;

	d3d_Device->SetVertexDeclaration (d3d_SkyDeclaration);
	d3d_SkyFX.BeginRender ();

	d3d_SkyFX.SetMatrix ("WorldMatrix", d3d_WorldMatrixStack->GetTop ());
	d3d_SkyFX.SetMatrix ("ViewMatrix", d3d_ViewMatrixStack->GetTop ());
	d3d_SkyFX.SetMatrix ("ProjMatrix", d3d_ProjMatrixStack->GetTop ());

	d3d_SkyFX.SetTexture ("solidLayer", solidskytexture);
	d3d_SkyFX.SetTexture ("alphaLayer", alphaskytexture);

	d3d_SkyFX.SetFloat ("Alpha", r_skyalpha.value);
	d3d_SkyFX.SetFloatArray ("r_origin", r_origin, 3);
	d3d_SkyFX.SetFloatArray ("Scale", speedscale, 3);

	d3d_SkyFX.SwitchToPass (0);

	// not subdivided
	for (msurface_t *surf = skychain; surf; surf = surf->texturechain)
	{
		d3d_SkyFX.Draw (D3DPT_TRIANGLEFAN, surf->polys->numverts - 2, surf->polys->verts, sizeof (glpolyvert_t));
		d3d_RenderDef.brush_polys++;
	}

	d3d_SkyFX.EndRender ();
}


/*
=================
D3D_DrawSkyChain
=================
*/
void D3D_DrawSkyChain (void)
{
	// no sky to draw!
	if (!d3d_RenderDef.skychain) return;

	// baseline projection for simple sky and clipping brushes
	d3d_ProjMatrixStack->LoadIdentity ();
	d3d_ProjMatrixStack->Frustum3D (d3d_RenderDef.fov_x, d3d_RenderDef.fov_y, 4, r_clipdist);

	if (!r_skywarp.value)
	{
		R_DrawSimpleSky (d3d_RenderDef.skychain);
		return;
	}

	if (r_skyalpha.value < 0.0f) Cvar_Set (&r_skyalpha, 0.0f);
	if (r_skyalpha.value > 1.0f) Cvar_Set (&r_skyalpha, 1.0f);

	// hack for sky where mappers do weird and wonderful non-standard things
	float skyclipdist = r_clipdist * 2;

	if (d3d_GlobalCaps.usingPixelShaders && !SkyboxValid)
	{
		D3D_DrawShaderSky (d3d_RenderDef.skychain);
		return;
	}

	// write the regular sky polys into the depth buffer to get a baseline
	R_ClipSky ();

	// flip the depth func so that the regular polys will prevent sphere polys outside their area reaching the framebuffer
	// also disable writing to Z
	D3D_SetRenderState (D3DRS_ZFUNC, D3DCMP_GREATEREQUAL);
	D3D_SetRenderState (D3DRS_ZWRITEENABLE, FALSE);

	// sky layer drawing state
	d3d_WorldMatrixStack->Push ();
	d3d_WorldMatrixStack->Translate (r_origin[0], r_origin[1], r_origin[2]);

	// increase the far clip plane for the actual sky brushes because we're going to draw it farther away
	d3d_ProjMatrixStack->LoadIdentity ();
	d3d_ProjMatrixStack->Frustum3D (d3d_RenderDef.fov_x, d3d_RenderDef.fov_y, 4, skyclipdist);

	if (SkyboxValid)
	{
		// skybox drawing
		R_DrawSkybox ();
	}
	else
	{
		// draw skysphere layers layer
		// the DP sphere is calculated at a scale of 16 so bring it up to the full correct scale
		D3D_DrawSkySphere (r_clipdist / 8);
	}

	// don't bother undoing the projection matrix as we're going to be resetting it for real anyway when we come to draw the world
	d3d_WorldMatrixStack->Pop ();

	// restore the depth func
	D3D_SetRenderState (D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	D3D_SetRenderState (D3DRS_ZWRITEENABLE, TRUE);

	// now write the regular polys one more time to clip world geometry
	// not certain if this is actually necessary...!
	// i'll put it back it i notice any indication it's needed
	// R_ClipSky ();
}


/*
==============================================================================================================================

		SKY INITIALIZATION

	The sphere is drawn from a VertexBuffer.  The sky texture is in two layers, top and bottom, contained in the same
	texture.

==============================================================================================================================
*/


DWORD d3d_BufferUsage = D3DUSAGE_SOFTWAREPROCESSING;

void D3D_InitSkySphere (void)
{
	if (d3d_DPSkyVerts) return;

	int i, j;
	float a, b, x, ax, ay, v[3], length;
	float dx, dy, dz;

	dx = 16;
	dy = 16;
	dz = 16 / 3;

	hr = d3d_Device->CreateVertexBuffer
	(
		SKYSPHERE_NUMVERTS * sizeof (skyverts_t),
		D3DUSAGE_WRITEONLY | d3d_BufferUsage,
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

	skyverts_t *ssv;

	i = SKYSPHERE_NUMVERTS;

	d3d_DPSkyVerts->Lock (0, SKYSPHERE_NUMVERTS * sizeof (skyverts_t), (void **) &ssv, 0);
	skyverts_t *ssv2 = ssv;

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

			ssv2->s = v[0] * length;
			ssv2->t = v[1] * length;
			ssv2->x = v[0];
			ssv2->y = v[1];
			ssv2->z = v[2];

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
		D3DUSAGE_WRITEONLY | d3d_BufferUsage,
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

	int			i, j, p;
	byte		*src;
	unsigned	transpix;
	int			r, g, b;
	unsigned	*rgba;

	// because you never know when a mapper might use a non-standard size...
	unsigned *trans = (unsigned *) Pool_Alloc (POOL_TEMP, mt->width * mt->height * sizeof (unsigned) / 2);

	// copy out
	int transwidth = mt->width / 2;
	int transheight = mt->height;

	// destroy any current textures we might have
	SAFE_RELEASE (simpleskytexture);
	SAFE_RELEASE (solidskytexture);
	SAFE_RELEASE (alphaskytexture);

	src = (byte *) mt + mt->offsets[0];

	// make an average value for the back to avoid
	// a fringe on the top level
	for (i = 0, r = 0, g = 0, b = 0; i < transheight; i++)
	{
		for (j = 0; j < transwidth; j++)
		{
			p = src[i * mt->width + j + transwidth];

			rgba = &d_8to24table[p];

			trans[(i * transwidth) + j] = *rgba;

			r += ((byte *) rgba)[0];
			g += ((byte *) rgba)[1];
			b += ((byte *) rgba)[2];
		}
	}

	((byte *) &transpix)[0] = BYTE_CLAMP (r / (transwidth * transheight));
	((byte *) &transpix)[1] = BYTE_CLAMP (g / (transwidth * transheight));
	((byte *) &transpix)[2] = BYTE_CLAMP (b / (transwidth * transheight));
	((byte *) &transpix)[3] = 0;

	// upload it
	D3D_LoadTexture (&solidskytexture, transwidth, transheight, (byte *) trans, NULL, false, false);

	// bottom layer
	for (i = 0; i < transheight; i++)
	{
		for (j = 0; j < transwidth; j++)
		{
			p = src[i * mt->width + j];

			if (p == 0)
				trans[(i * transwidth) + j] = transpix;
			else trans[(i * transwidth) + j] = d_8to24table[p];
		}
	}

	// upload it
	D3D_LoadTexture (&alphaskytexture, transwidth, transheight, (byte *) trans, NULL, false, true);

	// simple sky
	((byte *) &transpix)[3] = 255;

	for (i = 0; i < transheight; i++)
		for (j = 0; j < transwidth; j++)
			trans[(i * transwidth) + j] = transpix;

	// upload it
	D3D_LoadTexture (&simpleskytexture, transwidth, transheight, (byte *) trans, NULL, false, true);

	// no texels yet
	skyalphatexels = NULL;

	// prevent it happening first time during game play
	D3D_UpdateSkyAlpha ();
}


char *suf[6] = {"rt", "bk", "lf", "ft", "up", "dn"};
char *sbdir[] = {"env", "gfx/env", "gfx", NULL};

void R_LoadSkyBox (char *basename, bool feedback)
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

	// there's no standard for where to keep skyboxes so we try all of /gfx/env, /env and /gfx
	// all of which have been used at some time in the past.  we can add more esoteric locations here if required
	for (int i = 0; ; i++)
	{
		// out of directories
		if (!sbdir[i]) break;

		int numloaded = 0;

		for (int sb = 0; sb < 6; sb++)
		{
			// attempt to load it (sometimes an underscore is expected)
			if (!D3D_LoadExternalTexture (&skyboxtextures[sb], va ("%s/%s%s", sbdir[i], basename, suf[sb]), IMAGE_32BIT | IMAGE_NOCOMPRESS))
				if (!D3D_LoadExternalTexture (&skyboxtextures[sb], va ("%s/%s_%s", sbdir[i], basename, suf[sb]), IMAGE_32BIT | IMAGE_NOCOMPRESS))
					continue;

			// loaded OK
			numloaded++;
		}

		if (numloaded)
		{
			// as FQ is the behaviour modders expect let's allow partial skyboxes (much as it galls me)
			Con_Printf ("Loaded %i skybox components\n", numloaded);

			// the skybox is valid now, no need to search any more
			SkyboxValid = true;
			strcpy (CachedSkyBoxName, basename);
			return;
		}
	}

	Con_Printf ("Failed to load skybox\n");
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


cmd_t Loadsky_Cmd ("loadsky", R_Loadsky);
