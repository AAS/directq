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

// used in the surface refresh
extern D3DXMATRIX *CachedMatrix;
extern int NumMatrixSwaps;

// r_lightmap 1 uses the grey texture on account of 2 x overbrighting
extern LPDIRECT3DTEXTURE9 r_greytexture;
extern cvar_t r_lightmap;

extern	model_t	*loadmodel;
extern int r_renderflags;

LPDIRECT3DTEXTURE9 underwatertexture = NULL;
LPDIRECT3DSURFACE9 underwatersurface = NULL;
LPDIRECT3DTEXTURE9 solidskytexture = NULL;
LPDIRECT3DTEXTURE9 alphaskytexture = NULL;
LPDIRECT3DTEXTURE9 skyboxtextures[6] = {NULL};
LPDIRECT3DVERTEXBUFFER9 d3d_SkySphereVerts = NULL;
LPDIRECT3DINDEXBUFFER9 d3d_DPSkyIndexes = NULL;
LPDIRECT3DVERTEXBUFFER9 d3d_DPSkyVerts = NULL;

// the menu needs to know the name of the loaded skybox
char CachedSkyBoxName[MAX_PATH] = {0};
bool SkyboxValid = false;

bool UnderwaterValid = false;
cvar_t r_waterwarp ("r_waterwarp", 1, CVAR_ARCHIVE);
bool d3d_UpdateWarp = false;
LPDIRECT3DVERTEXBUFFER9 d3d_WarpVerts = NULL;

typedef struct warpverts_s
{
	float x, y, z;
	float s, t;
} warpverts_t;

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
	// ensure that it's gone before creating
	SAFE_RELEASE (underwatersurface);
	SAFE_RELEASE (underwatertexture);
	SAFE_RELEASE (d3d_WarpVerts);

	// create the update texture as a rendertarget at half screen resolution
	// rendertargets seem to relax power of 2 requirements, best of luck finding THAT in the documentation
	HRESULT hr = d3d_Device->CreateTexture
	(
		d3d_CurrentMode.Width,
		d3d_CurrentMode.Height,
		1,
		D3DUSAGE_RENDERTARGET,
		D3DFMT_X8R8G8B8,
		D3DPOOL_DEFAULT,
		&underwatertexture,
		NULL
	);

	// couldn't create it so don't do underwater updates
	if (FAILED (hr)) return;

	// we're good now
	UnderwaterValid = true;

	// note - change the buffer size if you ever change the tess factor!!!
	d3d_Device->CreateVertexBuffer (2112 * sizeof (warpverts_t), d3d_VertexBufferUsage, 0, D3DPOOL_DEFAULT, &d3d_WarpVerts, NULL);

	warpverts_t *wv;

	d3d_WarpVerts->Lock (0, 0, (void **) &wv, 0);

	float tessw = (float) d3d_CurrentMode.Width / 32.0f;
	float tessh = (float) d3d_CurrentMode.Height / 32.0f;
	int numverts = 0;

	// tesselate this so that we can do the sin calcs in the vertex shader instead of the pixel shader
	for (float x = 0, s = 0; x < d3d_CurrentMode.Width; x += tessw, s++)
	{
		for (float y = 0, t = 0; y <= d3d_CurrentMode.Height; y += tessh, t++)
		{
			wv[numverts].x = x;
			wv[numverts].y = y;
			wv[numverts].z = 0;
			wv[numverts].s = CompressST (s);
			wv[numverts].t = CompressST (t);

			numverts++;

			wv[numverts].x = (x + tessw);
			wv[numverts].y = y;
			wv[numverts].z = 0;
			wv[numverts].s = CompressST (s + 1);
			wv[numverts].t = CompressST (t);

			numverts++;
		}
	}

	d3d_WarpVerts->Unlock ();
}


void D3D_KillUnderwaterTexture (void)
{
	// just release it
	SAFE_RELEASE (underwatersurface);
	SAFE_RELEASE (underwatertexture);
	SAFE_RELEASE (d3d_WarpVerts);
}


void D3D_BeginUnderwaterWarp (void)
{
	// couldn't create the underwater texture or we don't want to warp
	if (!UnderwaterValid || !r_waterwarp.value) return;

	// no warps present on these leafs (unless we run the extra special super dooper always warp secret mode)
	if ((r_viewleaf->contents == CONTENTS_EMPTY || r_viewleaf->contents == CONTENTS_SOLID) && r_waterwarp.integer != 666) return;

	extern LPDIRECT3DSURFACE9 d3d_BackBuffer;

	// store out the backbuffer
	d3d_Device->GetRenderTarget (0, &d3d_BackBuffer);

	// get the surface to render to
	// (note - it *might* seem more efficient to keep this open all the time, but it's a HORRENDOUS slowdown,
	// even when we're not rendering any underwater surfs...)
	underwatertexture->GetSurfaceLevel (0, &underwatersurface);

	// set the underwater surface as the rendertarget
	d3d_Device->SetRenderTarget (0, underwatersurface);

	// flag that we need to draw the warp update
	d3d_UpdateWarp = true;
}


void D3D_EndUnderwaterWarp (void)
{
	// no warps
	if (!d3d_UpdateWarp) return;

	SAFE_RELEASE (underwatersurface);

	extern LPDIRECT3DSURFACE9 d3d_BackBuffer;

	// restore backbuffer
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

	// projection matrix
	D3DXMatrixIdentity (&d3d_OrthoMatrix);
	D3DXMatrixOrthoOffCenterRH (&d3d_OrthoMatrix, 0, d3d_CurrentMode.Width, d3d_CurrentMode.Height, 0, 0, 1);
	d3d_Device->SetTransform (D3DTS_PROJECTION, &d3d_OrthoMatrix);

	d3d_Device->SetStreamSource (0, d3d_WarpVerts, 0, sizeof (warpverts_t));
	d3d_UnderwaterFX.BeginRender ();
	d3d_UnderwaterFX.SetWPMatrix (&((*d3d_WorldMatrixStack->GetTop ()) * d3d_OrthoMatrix));
	d3d_UnderwaterFX.SetTime ((float) (cl.time * r_waterwarptime.value));
	d3d_UnderwaterFX.SetScale (r_waterwarpscale.value);
	d3d_UnderwaterFX.SwitchToPass (0);

	d3d_UnderwaterFX.SetTexture (underwatertexture);

	for (int i = 0, v = 0; i < 32; i++, v += 66)
		d3d_UnderwaterFX.Draw (D3DPT_TRIANGLESTRIP, v, 64);

	d3d_UnderwaterFX.EndRender ();
}


typedef struct glskyvert_s
{
	// sky verts are only used for clipping the sky sphere, so they don't need texcoords
	float xyz[3];
	float xyz2[3];
} glskyvert_t;

typedef struct glskypoly_s
{
	int numverts;
	glskyvert_t *verts;
} glskypoly_t;

typedef struct skysphere_s
{
	float xyz[3];
	float st[2];
} skysphere_t;

msurface_t	*warpface;

/*
==============================================================================================================================

		WARP SURFACE GENERATION

		No longer subdivided :D

==============================================================================================================================
*/

void GL_DontSubdivideSky (msurface_t *surf)
{
	int			i;
	int			lindex;
	float		*vec;

	if (surf->flags & SURF_DRAWTURB)
	{
		Sys_Error ("GL_DontSubdivideSky with (surf->flags & SURF_DRAWTURB)");
		return;
	}

	// alloc the poly in memory
	glskypoly_t *poly = (glskypoly_t *) Heap_TagAlloc (TAG_BRUSHMODELS, sizeof (glskypoly_t));
	poly->verts = (glskyvert_t *) Heap_TagAlloc (TAG_BRUSHMODELS, surf->numedges * sizeof (glskyvert_t));
	poly->numverts = surf->numedges;

	// link it into the chain (there is no chain!)
	surf->polys = poly;

	for (i = 0; i < poly->numverts; i++)
	{
		lindex = loadmodel->bh->surfedges[surf->firstedge + i];

		if (lindex > 0)
			vec = loadmodel->bh->vertexes[loadmodel->bh->edges[lindex].v[0]].position;
		else
			vec = loadmodel->bh->vertexes[loadmodel->bh->edges[-lindex].v[1]].position;

		// take a second copy for transformations
		poly->verts[i].xyz[0] = poly->verts[i].xyz2[0] = vec[0];
		poly->verts[i].xyz[1] = poly->verts[i].xyz2[1] = vec[1];
		poly->verts[i].xyz[2] = poly->verts[i].xyz2[2] = vec[2];
	}
}


/*
==============================================================================================================================

			SOFTWARE TRANSFORMATION FOR WARP SURFACES ON INLINE BMODELS

		The two different methods could be merged as the struct layouts are identical for the
		required data, although v++ might throw it a bit.  No big deal.

==============================================================================================================================
*/

void D3D_TransformWarpSurface (msurface_t *surf, D3DMATRIX *m)
{
	if (surf->flags & SURF_DRAWTURB)
	{
		// not used any more as water in now in a shader...
	}
	else
	{
		glskypoly_t *p = (glskypoly_t *) surf->polys;
		glskyvert_t *v = p->verts;

		for (int i = 0; i < p->numverts; i++, v++)
		{
			// transform by the matrix then copy back.  we need to retain the original verts unmodified
			// as otherwise this will become invalid
			v->xyz[0] = v->xyz2[0] * m->_11 + v->xyz2[1] * m->_21 + v->xyz2[2] * m->_31 + m->_41;
			v->xyz[1] = v->xyz2[0] * m->_12 + v->xyz2[1] * m->_22 + v->xyz2[2] * m->_32 + m->_42;
			v->xyz[2] = v->xyz2[0] * m->_13 + v->xyz2[1] * m->_23 + v->xyz2[2] * m->_33 + m->_43;
		}
	}
}


/*
==============================================================================================================================

		WATER WARP RENDERING

	Surface subdivision and warpsin updates are removed, water uses a dynamically updating warp texture.

==============================================================================================================================
*/


/*
================
R_DrawWaterSurfaces
================
*/
cvar_t r_lavaalpha ("r_lavaalpha", 1, CVAR_ARCHIVE);
cvar_t r_telealpha ("r_telealpha", 1, CVAR_ARCHIVE);
cvar_t r_slimealpha ("r_slimealpha", 1, CVAR_ARCHIVE);
cvar_t r_warpspeed ("r_warpspeed", 4, CVAR_ARCHIVE);

// lock water/slime/tele/lava alpha sliders
cvar_t r_lockalpha ("r_lockalpha", "1", CVAR_ARCHIVE);

void R_DrawWaterSurfaces (void)
{
	if (!(r_renderflags & R_RENDERWATERSURFACE)) return;

	int i;
	msurface_t *surf;
	texture_t *t;
	float wateralpha;
	float lavaalpha;
	float slimealpha;
	float telealpha;
	bool transon = false;
	bool allowtrans = false;

	// check if translucency is allowed on this map/scene
	if (!cl.worldbrush->transwater) allowtrans = true;
	if (!r_novis.value) allowtrans = true;
	if (!(r_renderflags & R_RENDERUNDERWATER) || !(r_renderflags & R_RENDERABOVEWATER)) allowtrans = false;

	// check for turning translucency on
	if ((r_wateralpha.value < 1.0f || r_lavaalpha.value < 1.0f || r_slimealpha.value < 1.0f || r_telealpha.value < 1.0f) && allowtrans)
	{
		// store alpha values
		if (r_lockalpha.value)
		{
			// locked sliders
			wateralpha = D3D_TransformColourSpace (r_wateralpha.value);
			lavaalpha = D3D_TransformColourSpace (r_wateralpha.value);
			slimealpha = D3D_TransformColourSpace (r_wateralpha.value);
			telealpha = D3D_TransformColourSpace (r_wateralpha.value);
		}
		else
		{
			// independent sliders
			wateralpha = D3D_TransformColourSpace (r_wateralpha.value);
			lavaalpha = D3D_TransformColourSpace (r_lavaalpha.value);
			slimealpha = D3D_TransformColourSpace (r_slimealpha.value);
			telealpha = D3D_TransformColourSpace (r_telealpha.value);
		}

		// don't go < 0
		if (wateralpha < 0) wateralpha = 0;
		if (lavaalpha < 0) lavaalpha = 0;
		if (slimealpha < 0) slimealpha = 0;
		if (telealpha < 0) telealpha = 0;

		// enable translucency
		d3d_Device->SetRenderState (D3DRS_ALPHABLENDENABLE, TRUE);
		d3d_Device->SetRenderState (D3DRS_BLENDOP, D3DBLENDOP_ADD);
		d3d_Device->SetRenderState (D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
		d3d_Device->SetRenderState (D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
		d3d_Device->SetRenderState (D3DRS_ZWRITEENABLE, FALSE);

		// flag we're going translucent
		transon = true;
	}
	else
		wateralpha = lavaalpha = slimealpha = telealpha = 1.0f;

	// bound speed
	if (r_warpspeed.value < 1) Cvar_Set (&r_warpspeed, 1);
	if (r_warpspeed.value > 10) Cvar_Set (&r_warpspeed, 10);

	// set the vertex buffer stream
	d3d_Device->SetStreamSource (0, d3d_BrushModelVerts, 0, sizeof (worldvert_t));
	d3d_Device->SetVertexDeclaration (d3d_V3ST4Declaration);

	d3d_LiquidFX.BeginRender ();
	d3d_LiquidFX.SetWPMatrix (&(d3d_WorldMatrix * d3d_PerspectiveMatrix));
	d3d_LiquidFX.SetTime ((cl.time * r_warpspeed.value) / 4);
	d3d_LiquidFX.SwitchToPass (0);

	CachedMatrix = NULL;

	for (i = 0; i < cl.worldbrush->numtextures; i++)
	{
		// no texture (e2m3 gets this)
		if (!(t = cl.worldbrush->textures[i])) continue;

		// nothing to draw for this texture
		if (!(surf = t->texturechain)) continue;

		// skip over
		if (!(surf->flags & SURF_DRAWTURB)) continue;

		// allow users to specify whether they want r_wateralpha to also apply to lava and teleports
		if (!transon)
			d3d_LiquidFX.SetAlpha (1.0f);
		else if (!strncmp (t->name, "*lava", 5))
			d3d_LiquidFX.SetAlpha (lavaalpha);
		else if (!strncmp (t->name, "*tele", 5))
			d3d_LiquidFX.SetAlpha (telealpha);
		else if (!strncmp (t->name, "*slime", 6))
			d3d_LiquidFX.SetAlpha (slimealpha);
		else d3d_LiquidFX.SetAlpha (wateralpha);

		// liquids should be affected by r_lightmap 1
		if (r_lightmap.integer)
			d3d_LiquidFX.SetTexture (r_greytexture);
		else d3d_LiquidFX.SetTexture ((LPDIRECT3DTEXTURE9) t->d3d_Texture);

		// draw all the surfaces
		for (; surf; surf = surf->texturechain)
		{
			c_brush_polys++;

			if ((D3DXMATRIX *) surf->model->matrix != CachedMatrix)
			{
				d3d_LiquidFX.SetEntMatrix ((D3DXMATRIX *) surf->model->matrix);
				CachedMatrix = (D3DXMATRIX *) surf->model->matrix;
				NumMatrixSwaps++;
			}

			d3d_LiquidFX.Draw (D3DPT_TRIANGLEFAN, surf->vboffset, surf->numedges - 2);
		}
	}

	d3d_LiquidFX.EndRender ();

	// turn off it it was turned on
	// (save me having to do that if test again here...)
	if (transon)
	{
		d3d_Device->SetRenderState (D3DRS_ALPHABLENDENABLE, FALSE);
		d3d_Device->SetRenderState (D3DRS_ZWRITEENABLE, TRUE);
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

	Note: you meed either an infinite projection matrix or a very large Z-far for this to work right!

==============================================================================================================================
*/

// let's distinguish properly between preproccessor defines and variables
// (LH apparently doesn't believe in this, but I do)
// 16 works well in most cases, but 32 is needed for aberrant cases like the start room in e4m2
#define DP_SKYGRID_SIZE 32
#define DP_SKYGRID_SIZE_PLUS_1 (DP_SKYGRID_SIZE + 1)
#define DP_SKYGRID_RECIP (1.0f / (DP_SKYGRID_SIZE))
#define DP_SKYSPHERE_NUMVERTS (DP_SKYGRID_SIZE_PLUS_1 * DP_SKYGRID_SIZE_PLUS_1)
#define DP_SKYSPHERE_NUMTRIS (DP_SKYGRID_SIZE * DP_SKYGRID_SIZE * 2)

cvar_t r_skywarp ("r_skywarp", 1, CVAR_ARCHIVE);
cvar_t r_skybackscroll ("r_skybackscroll", 8, CVAR_ARCHIVE);
cvar_t r_skyfrontscroll ("r_skyfrontscroll", 16, CVAR_ARCHIVE);
cvar_t r_skyalpha ("r_skyalpha", 1, CVAR_ARCHIVE);

void R_ClipSky (msurface_t *skychain)
{
	// disable textureing and writes to the color buffer
	d3d_Device->SetVertexDeclaration (d3d_V3NoSTDeclaration);
	d3d_Device->SetRenderState (D3DRS_COLORWRITEENABLE, 0);
	d3d_SkyFX.SetWPMatrix (&(d3d_WorldMatrix * d3d_PerspectiveMatrix));
	d3d_SkyFX.SwitchToPass (0);

	for (msurface_t *surf = skychain; surf; surf = surf->texturechain)
	{
		c_brush_polys++;

		// no longer subdivided
		glskypoly_t *p = (glskypoly_t *) surf->polys;

		// fixme - only need to store 3 floats for these - cut down on geometry going to the card
		d3d_SkyFX.Draw (D3DPT_TRIANGLEFAN, p->numverts - 2, p->verts, sizeof (glskyvert_t));
	}

	// revert state
	d3d_Device->SetRenderState (D3DRS_COLORWRITEENABLE, 0x0000000F);
}


void R_DrawSkySphere (float scale)
{
	// darkplaces warp
	// for all that they say "you VILL use ein index buffer, heil Bill", NOWHERE in the SDK is it
	// documented that you need to call SetIndices to tell the device the index buffer to use.  Aside
	// from the entry for SetIndices, of course, but NOWHERE else.  C'mon Microsoft, GET IT FUCKING RIGHT!
	d3d_Device->SetStreamSource (0, d3d_DPSkyVerts, 0, sizeof (warpverts_t));
	d3d_Device->SetIndices (d3d_DPSkyIndexes);

	d3d_Device->SetVertexDeclaration (d3d_V3ST2Declaration);
	d3d_WorldMatrixStack->ScaleLocal (scale, scale, scale);
	d3d_SkyFX.SetWPMatrix (&(*(d3d_WorldMatrixStack->GetTop ()) * d3d_PerspectiveMatrix));

	// sky should be affected by r_lightmap 1 too
	if (r_lightmap.integer)
	{
		// don't bother with the ops or the scrolling
		d3d_SkyFX.SetTexture (0, r_greytexture);
		d3d_SkyFX.SetTexture (1, r_greytexture);
	}
	else
	{
		// eval the scroll in software rather than doing it using the ugly d3d texture matrix
		// use cl.time so that it pauses properly when the console is down (same as everything else)
		float speedscale = cl.time * r_skybackscroll.value;
		speedscale -= (int) speedscale & ~127;
		speedscale /= 128.0f;

		float speedscale2 = cl.time * r_skyfrontscroll.value;
		speedscale2 -= (int) speedscale2 & ~127;
		speedscale2 /= 128.0f;

		d3d_SkyFX.SetScale (speedscale);
		d3d_SkyFX.SetTime (speedscale2);

		d3d_SkyFX.SetTexture (0, solidskytexture);
		d3d_SkyFX.SetTexture (1, alphaskytexture);
	}

	d3d_SkyFX.SwitchToPass (1);
	d3d_SkyFX.Draw (D3DPT_TRIANGLELIST, 0, 0, DP_SKYSPHERE_NUMVERTS, 0, DP_SKYSPHERE_NUMTRIS);
}


void R_DrawSkySphere (float rotatefactor, float scale, LPDIRECT3DTEXTURE9 skytexture)
{
	// MHQuake warp
	d3d_WorldMatrixStack->Push ();
	d3d_WorldMatrixStack->ScaleLocal (scale, scale, scale / 2);
	d3d_WorldMatrixStack->RotateAxisLocal (&XVECTOR, D3DXToRadian (90));
	d3d_WorldMatrixStack->RotateAxisLocal (&YVECTOR, D3DXToRadian (22));
	d3d_WorldMatrixStack->RotateAxisLocal (&ZVECTOR, D3DXToRadian (rotatefactor));

	d3d_SkyFX.SetWPMatrix (&(*(d3d_WorldMatrixStack->GetTop ()) * d3d_PerspectiveMatrix));
	d3d_SkyFX.SwitchToPass (2);

	// sky should be affected by r_lightmap 1 too
	if (r_lightmap.integer)
		d3d_SkyFX.SetTexture (r_greytexture);
	else d3d_SkyFX.SetTexture (skytexture);

	for (int i = 0; i < 220; i += 22)
		d3d_SkyFX.Draw (D3DPT_TRIANGLESTRIP, i, 20);

	d3d_WorldMatrixStack->Pop ();
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
	b[0] = s * 100000;
	b[1] = t * 100000;
	b[2] = 100000;

	for (j = 0; j < 3; j++)
	{
		k = st_to_vec[axis][j];

		if (k < 0)
			sbquad[vert].v[j] = -b[-k - 1];
		else sbquad[vert].v[j] = b[k - 1];

		// center it on the player's position
		sbquad[vert].v[j] += r_origin[j];
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


void R_DrawSkybox (void)
{
	int i;
	float skymins[2] = {-1, -1};
	float skymaxs[2] = {1, 1};
	int	skytexorder[6] = {0, 2, 1, 3, 4, 5};

	sky_min = 1.0 / 512;
	sky_max = 511.0 / 512;

	d3d_Device->SetVertexDeclaration (d3d_V3ST2Declaration);
	d3d_SkyFX.SwitchToPass (3);

	for (i = 0; i < 6; i++)
	{
		// we allowed the skybox to load if any of the components were not found (crazy modders!), so we must account for that here
		if (!skyboxtextures[skytexorder[i]]) continue;

		// sky should be affected by r_lightmap 1 too
		if (r_lightmap.integer)
			d3d_SkyFX.SetTexture (r_greytexture);
		else d3d_SkyFX.SetTexture (skyboxtextures[skytexorder[i]]);

		MakeSkyVec (skymins[0], skymins[1], i, 0);
		MakeSkyVec (skymins[0], skymaxs[1], i, 1);
		MakeSkyVec (skymaxs[0], skymaxs[1], i, 2);
		MakeSkyVec (skymaxs[0], skymins[1], i, 3);

		d3d_SkyFX.Draw (D3DPT_TRIANGLEFAN, 2, sbquad, sizeof (skyboxquad_t));
	}
}


/*
=================
R_DrawSkyChain
=================
*/
void R_DrawSkyChain (msurface_t *skychain)
{
	// no sky to draw!
	if (!skychain) return;

	if (r_skyalpha.value < 0.0f) Cvar_Set (&r_skyalpha, 0.0f);
	if (r_skyalpha.value > 1.0f) Cvar_Set (&r_skyalpha, 1.0f);

	// approximately replicate the speed of the old sky warp
	float rotateBack = anglemod (cl.time / 4.0f * r_skybackscroll.value);
	float rotateFore = anglemod (cl.time / 4.0f * r_skyfrontscroll.value);

	d3d_SkyFX.BeginRender ();
	d3d_SkyFX.SetWPMatrix (&(d3d_WorldMatrix * d3d_PerspectiveMatrix));

	// write the regular sky polys into the depth buffer to get a baseline
	R_ClipSky (skychain);

	// flip the depth func so that the regular polys will prevent sphere polys outside their area reaching the framebuffer
	// also disable writing to Z
	d3d_Device->SetRenderState (D3DRS_ZFUNC, D3DCMP_GREATEREQUAL);
	d3d_Device->SetRenderState (D3DRS_ZWRITEENABLE, FALSE);

	if (SkyboxValid)
	{
		// skybox drawing
		R_DrawSkybox ();
	}
	else if (r_skywarp.integer)
	{
		// sky layer drawing state
		d3d_WorldMatrixStack->Push ();
		d3d_WorldMatrixStack->TranslateLocal (r_origin[0], r_origin[1], r_origin[2]);

		d3d_SkyFX.SetAlpha (r_skyalpha.value);

		// draw back layer
		R_DrawSkySphere (100000);
	}
	else
	{
		// sky layer drawing state
		d3d_Device->SetStreamSource (0, d3d_SkySphereVerts, 0, sizeof (skysphere_t));
		d3d_Device->SetVertexDeclaration (d3d_V3ST2Declaration);

		d3d_WorldMatrixStack->Push ();
		d3d_WorldMatrixStack->TranslateLocal (r_origin[0], r_origin[1], r_origin[2]);
		d3d_WorldMatrixStack->TranslateLocal (0, 0, -8000);

		// draw back layer
		d3d_SkyFX.SetAlpha (1.0f);
		R_DrawSkySphere (rotateBack, 10, solidskytexture);

		// draw front layer
		d3d_Device->SetRenderState (D3DRS_ALPHABLENDENABLE, TRUE);
		d3d_Device->SetRenderState (D3DRS_BLENDOP, D3DBLENDOP_ADD);
		d3d_Device->SetRenderState (D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
		d3d_Device->SetRenderState (D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
		d3d_SkyFX.SetAlpha (r_skyalpha.value);
		R_DrawSkySphere (rotateFore, 8, alphaskytexture);
		d3d_Device->SetRenderState (D3DRS_ALPHABLENDENABLE, FALSE);
	}

	// restore the depth func
	d3d_Device->SetRenderState (D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	d3d_Device->SetRenderState (D3DRS_ZWRITEENABLE, TRUE);

	// go back to the world matrix
	d3d_WorldMatrixStack->Pop ();
	d3d_WorldMatrixStack->LoadMatrix (&d3d_WorldMatrix);
	d3d_Device->SetTransform (D3DTS_WORLD, d3d_WorldMatrixStack->GetTop ());

	// now write the regular polys one more time to clip world geometry
	// not certain if this is actually necessary...!
	// i'll put it back it i notice any indication it's needed
	// R_ClipSky (skychain);

	d3d_SkyFX.EndRender ();
}


/*
==============================================================================================================================

		SKY INITIALIZATION

	The sphere is drawn from a VertexBuffer.  The sky texture is in two layers, top and bottom, contained in the same
	texture.

==============================================================================================================================
*/

static void D3D_DPSkySphereCalc (void)
{
	int i, j;
	float a, b, x, ax, ay, v[3], length;
	float dx, dy, dz;

	HRESULT hr;

	dx = 16;
	dy = 16;
	dz = 16 / 3;

	hr = d3d_Device->CreateVertexBuffer
	(
		DP_SKYSPHERE_NUMVERTS * sizeof (warpverts_t),
		D3DUSAGE_WRITEONLY | d3d_VertexBufferUsage,
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

	d3d_DPSkyVerts->Lock (0, DP_SKYSPHERE_NUMVERTS * sizeof (warpverts_t), (void **) &ssv, 0);
	warpverts_t *ssv2 = ssv;

	for (j = 0; j <= DP_SKYGRID_SIZE; j++)
	{
		a = j * DP_SKYGRID_RECIP;
		ax = cos (a * M_PI * 2);
		ay = -sin (a * M_PI * 2);

		for (i = 0; i <= DP_SKYGRID_SIZE; i++)
		{
			b = i * DP_SKYGRID_RECIP;
			x = cos ((b + 0.5) * M_PI);

			v[0] = ax * x * dx;
			v[1] = ay * x * dy;
			v[2] = -sin ((b + 0.5) * M_PI) * dz;

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

	hr = d3d_Device->CreateIndexBuffer
	(
		(DP_SKYGRID_SIZE * DP_SKYGRID_SIZE * 6) * sizeof (unsigned short),
		D3DUSAGE_WRITEONLY | d3d_VertexBufferUsage,
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

	d3d_DPSkyIndexes->Lock (0, (DP_SKYGRID_SIZE * DP_SKYGRID_SIZE * 6) * sizeof (unsigned short), (void **) &skyindexes, 0);
	e = skyindexes;

	for (j = 0; j < DP_SKYGRID_SIZE; j++)
	{
		for (i = 0; i < DP_SKYGRID_SIZE; i++)
		{
			*e++ =  j * DP_SKYGRID_SIZE_PLUS_1 + i;
			*e++ =  j * DP_SKYGRID_SIZE_PLUS_1 + i + 1;
			*e++ = (j + 1) * DP_SKYGRID_SIZE_PLUS_1 + i;

			*e++ =  j * DP_SKYGRID_SIZE_PLUS_1 + i + 1;
			*e++ = (j + 1) * DP_SKYGRID_SIZE_PLUS_1 + i + 1;
			*e++ = (j + 1) * DP_SKYGRID_SIZE_PLUS_1 + i;
		}
	}

	d3d_DPSkyIndexes->Unlock ();
	d3d_DPSkyIndexes->PreLoad ();
}


void D3D_InitSkySphere (void)
{
	// already initialized
	if (d3d_SkySphereVerts) return;

	D3D_DPSkySphereCalc ();

	// set up the sphere vertex buffer
	HRESULT hr = d3d_Device->CreateVertexBuffer
	(
		220 * sizeof (skysphere_t),
		D3DUSAGE_WRITEONLY | d3d_VertexBufferUsage,
		0,
		D3DPOOL_MANAGED,
		&d3d_SkySphereVerts,
		NULL
	);

	// fixme - fall back on DrawPrimitiveUP if this happens!
	if (FAILED (hr)) Sys_Error ("D3D_InitSkySphere: d3d_Device->CreateVertexBuffer failed");

	skysphere_t *verts;

	// lock the full buffers
	d3d_SkySphereVerts->Lock (0, 0, (void **) &verts, 0);

	// fill it in
	float drho = 0.3141592653589;
	float dtheta = 0.6283185307180;

	float ds = 0.1;
	float dt = 0.1;

	float t = 1.0f;	
	float s = 0.0f;

	int i;
	int j;

	int vertnum = 0;

	for (i = 0; i < 10; i++)
	{
		float rho = (float) i * drho;
		float srho = (float) (sin (rho));
		float crho = (float) (cos (rho));
		float srhodrho = (float) (sin (rho + drho));
		float crhodrho = (float) (cos (rho + drho));

		s = 0.0f;

		for (j = 0; j <= 10; j++)
		{
			float theta = (j == 10) ? 0.0f : j * dtheta;
			float stheta = (float) (-sin (theta));
			float ctheta = (float) (cos (theta));

			verts[vertnum].st[0] = s * 24;
			verts[vertnum].st[1] = t * 12;

			verts[vertnum].xyz[0] = stheta * srho * 4096.0;
			verts[vertnum].xyz[1] = ctheta * srho * 4096.0;
			verts[vertnum].xyz[2] = crho * 4096.0;

			vertnum++;

			verts[vertnum].st[0] = s * 24;
			verts[vertnum].st[1] = (t - dt) * 12;

			verts[vertnum].xyz[0] = stheta * srhodrho * 4096.0;
			verts[vertnum].xyz[1] = ctheta * srhodrho * 4096.0;
			verts[vertnum].xyz[2] = crhodrho * 4096.0;

			vertnum++;
			s += ds;
		}

		t -= dt;
	}

	// unlock
	d3d_SkySphereVerts->Unlock ();
	d3d_SkySphereVerts->PreLoad ();
}


/*
=============
R_InitSky

A sky texture is 256*128, with the right side being a masked overlay
==============
*/
void R_InitSky (miptex_t *mt)
{
	// create the sky sphere (one time only)
	D3D_InitSkySphere ();

	int			i, j, p;
	byte		*src;
	unsigned	trans[128 * 128];
	unsigned	transpix;
	int			r, g, b;
	unsigned	*rgba;

	// destroy any current textures we might have
	SAFE_RELEASE (solidskytexture);
	SAFE_RELEASE (alphaskytexture);

	src = (byte *) mt + mt->offsets[0];

	// make an average value for the back to avoid
	// a fringe on the top level
	for (i = 0, r = 0, g = 0, b = 0; i < 128; i++)
	{
		for (j = 0; j < 128; j++)
		{
			p = src[i * 256 + j + 128];

			rgba = &d_8to24table[p];

			trans[(i * 128) + j] = *rgba;

			r += ((byte *) rgba)[0];
			g += ((byte *) rgba)[1];
			b += ((byte *) rgba)[2];
		}
	}

	((byte *) &transpix)[0] = r / (128 * 128);
	((byte *) &transpix)[1] = g / (128 * 128);
	((byte *) &transpix)[2] = b / (128 * 128);
	((byte *) &transpix)[3] = 0;

	// upload it
	D3D_LoadTexture (&solidskytexture, 128, 128, (byte *) trans, NULL, false, false);

	// bottom layer
	for (i = 0; i < 128; i++)
	{
		for (j = 0; j < 128; j++)
		{
			p = src[i * 256 + j];

			if (p == 0)
				trans[(i * 128) + j] = transpix;
			else
				trans[(i * 128) + j] = d_8to24table[p];
		}
	}

	// upload it
	D3D_LoadTexture (&alphaskytexture, 128, 128, (byte *) trans, NULL, false, true);
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
			if (!D3D_LoadExternalTexture (&skyboxtextures[sb], va ("%s/%s%s", sbdir[i], basename, suf[sb]), 0))
				if (!D3D_LoadExternalTexture (&skyboxtextures[sb], va ("%s/%s_%s", sbdir[i], basename, suf[sb]), 0)) continue;

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
