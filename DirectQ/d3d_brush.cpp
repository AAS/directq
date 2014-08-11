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
#include "d3d_hlsl.h"

// OK, it happens in GLQuake which is argument for 0, but it didn't happen in
// software, which is a bigger argument for 1.  So 1 it is.
cvar_t r_zhack ("r_zfighthack", "1", CVAR_ARCHIVE);
extern cvar_t r_warpspeed;
extern cvar_t r_lightscale;
extern bool d3d_AutomapDraw;

// r_lightmap 1 uses the grey texture on account of 2 x overbrighting
extern LPDIRECT3DTEXTURE9 r_greytexture;
extern cvar_t r_lightmap;

texture_t *R_TextureAnimation (texture_t *base);
void R_LightPoint (entity_t *e, float *c);
bool R_CullBox (vec3_t mins, vec3_t maxs);
void D3D_RotateForEntity (entity_t *e, bool shadow = false);
void R_PushDlights (mnode_t *headnode);

extern msurface_t *skychain;


// make code make sense!!!
#define PASS_NOTRANSNOLUMA		0
#define PASS_NOTRANSLUMA		1
#define PASS_LIQUIDINSTANCED	2

bool R_SetupBrushEntity (entity_t *e)
{
	bool rotated;
	vec3_t mins, maxs;
	model_t *clmodel = e->model;

	if (e->angles[0] || e->angles[1] || e->angles[2])
	{
		rotated = true;

		for (int i = 0; i < 3; i++)
		{
			mins[i] = e->origin[i] - clmodel->radius;
			maxs[i] = e->origin[i] + clmodel->radius;
		}
	}
	else
	{
		rotated = false;

		VectorAdd (e->origin, clmodel->mins, mins);
		VectorAdd (e->origin, clmodel->maxs, maxs);
	}

	// static entities already have the leafs they are in bbox culled
	if (!d3d_AutomapDraw)
	{
		if (!e->nocullbox)
			if (R_CullBox (mins, maxs))
				return true;
	}
	else
	{
		if (mins[2] > r_refdef.vieworg[2]) return true;
	}

	// set up modelorg and matrix
	VectorSubtract (r_refdef.vieworg, e->origin, modelorg);

	if (rotated)
	{
		vec3_t	temp;
		vec3_t	forward, right, up;

		VectorCopy (modelorg, temp);
		AngleVectors (e->angles, forward, right, up);
		modelorg[0] = DotProduct (temp, forward);
		modelorg[1] = -DotProduct (temp, right);
		modelorg[2] = DotProduct (temp, up);
	}

	if (r_zhack.value && clmodel->name[0] == '*')
	{
		// hack the origin to prevent bmodel z-fighting
		e->origin[0] -= DIST_EPSILON;
		e->origin[1] -= DIST_EPSILON;
		e->origin[2] -= DIST_EPSILON;
	}

	// store transform for model
	if (rotated)
	{
		d3d_WorldMatrixStack->Push ();
		d3d_WorldMatrixStack->LoadIdentity ();
		D3D_RotateForEntity (e);
		memcpy (clmodel->matrix, d3d_WorldMatrixStack->GetTop (), sizeof (D3DXMATRIX));
		d3d_WorldMatrixStack->Pop ();
	}
	else
	{
		// lightweight translation-only
		D3DXMatrixIdentity ((D3DXMATRIX *) clmodel->matrix);
		((D3DXMATRIX *) clmodel->matrix)->_41 = e->origin[0];
		((D3DXMATRIX *) clmodel->matrix)->_42 = e->origin[1];
		((D3DXMATRIX *) clmodel->matrix)->_43 = e->origin[2];
	}

	if (r_zhack.value && clmodel->name[0] == '*')
	{
		// un-hack the origin
		e->origin[0] += DIST_EPSILON;
		e->origin[1] += DIST_EPSILON;
		e->origin[2] += DIST_EPSILON;
	}

	// not culled
	return false;
}


/*
====================
D3D_DrawInstancedBrushModels

Draws instanced brush models using shaders to handle origin and fullbrights.

Because we're letting users specify their own shaders we need to validate these to ensure that the
expected techniques, passes and parameters are present and correct.  We could move it all to load
time but then the risk is that people won't notice it, so instead we spam the console at run time.
====================
*/
void D3D_DrawInstancedBrushModels (void)
{
	if (!r_drawentities.value) return;
	if (!(r_renderflags & R_RENDERINSTANCEDBRUSH)) return;

	// set the vertex buffer stream
	d3d_Device->SetStreamSource (0, d3d_BrushModelVerts, 0, sizeof (worldvert_t));

	// set up shaders
	d3d_Device->SetVertexDeclaration (d3d_V3ST2Declaration);

	d3d_InstancedBrushFX.BeginRender ();
	d3d_InstancedBrushFX.SetWPMatrix (&(d3d_WorldMatrix * d3d_PerspectiveMatrix));
	d3d_InstancedBrushFX.SetTime ((cl.time * r_warpspeed.value));
	d3d_InstancedBrushFX.SetScale (r_lightscale.value);

	for (int i = 0; i < cl_numvisedicts; i++)
	{
		// R_TextureAnimation needs this
		currententity = cl_visedicts[i];

		// only doing instanced brush models here
		if (currententity->model->type != mod_brush) continue;
		if (currententity->model->name[0] == '*') continue;

		// cullbox test and other setup
		if (R_SetupBrushEntity (currententity)) continue;

		float bsplight[4];

		// get lighting information
		R_LightPoint (currententity, bsplight);

		// set origin in the vertex shader
		d3d_InstancedBrushFX.SetEntMatrix ((D3DXMATRIX *) currententity->model->matrix);
		d3d_InstancedBrushFX.SetColor4f (bsplight);

		// draw the instanced model
		int s;
		msurface_t *surf;

		for (s = 0, surf = currententity->model->bh->surfaces; s < currententity->model->bh->numsurfaces; s++, surf++)
		{
			// set the model correctly (is this used anymore???)
			surf->model = currententity->model;

			// note - glquake allows these so we probably should too...
			if (surf->flags & SURF_DRAWSKY) continue;

			// get the correct texture
			texture_t *tex = R_TextureAnimation (surf->texinfo->texture);

			// bind it - we don't bother sorting these by chain as they normally have at most 3-4 textures.  likewise
			// with backface culling tests/etc (takes longer to perform the test than it does to just render the blimmin thing)
			if (surf->flags & SURF_DRAWTURB)
				d3d_InstancedBrushFX.SwitchToPass (PASS_LIQUIDINSTANCED);
			else if (tex->d3d_Fullbright)
			{
				d3d_InstancedBrushFX.SetTexture (1, (LPDIRECT3DTEXTURE9) tex->d3d_Fullbright);
				d3d_InstancedBrushFX.SwitchToPass (PASS_NOTRANSNOLUMA);
			}
			else d3d_InstancedBrushFX.SwitchToPass (PASS_NOTRANSLUMA);

			if (r_lightmap.integer)
				d3d_InstancedBrushFX.SetTexture (0, r_greytexture);
			else d3d_InstancedBrushFX.SetTexture (0, (LPDIRECT3DTEXTURE9) tex->d3d_Texture);

			c_brush_polys++;

			// draw the surface directly from the vertex buffer
			d3d_InstancedBrushFX.Draw (D3DPT_TRIANGLEFAN, surf->vboffset, surf->numedges - 2);
		}
	}

	// take down the effect
	d3d_InstancedBrushFX.EndRender ();
}


D3DXMATRIX *CachedMatrix = NULL;
int NumMatrixSwaps = 0;


void D3D_RunBrushPass (int PassNum)
{
	msurface_t *surf;
	texture_t *tex;

	d3d_BrushFX.SwitchToPass (PassNum);

	for (int i = 0; i < cl.worldbrush->numtextures; i++)
	{
		// no texture (e2m3 gets this)
		if (!(tex = cl.worldbrush->textures[i])) continue;

		// skip over passes
		if (PassNum == PASS_NOTRANSNOLUMA && tex->d3d_Fullbright) continue;
		if (PassNum == PASS_NOTRANSLUMA && !tex->d3d_Fullbright) continue;

		// nothing to draw for this texture
		if (!(surf = tex->texturechain)) continue;

		// skip over
		if (surf->flags & SURF_DRAWSKY) continue;
		if (surf->flags & SURF_DRAWTURB) continue;

		// bind regular texture
		if (r_lightmap.integer)
			d3d_BrushFX.SetTexture (0, r_greytexture);
		else d3d_BrushFX.SetTexture (0, (LPDIRECT3DTEXTURE9) tex->d3d_Texture);

		// bind luma texture
		if (tex->d3d_Fullbright) d3d_BrushFX.SetTexture (2, (LPDIRECT3DTEXTURE9) tex->d3d_Fullbright);

		// we need to track the currently cached lightmap in order to know when to render a primitive batch
		// primitive batches are rendered when any of (1) main texture, (2) lightmap or (3) matrix changes
		LPDIRECT3DTEXTURE9 CachedLightmap = NULL;

		for (; surf; surf = surf->texturechain)
		{
			c_brush_polys++;

			// bind the lightmap
			// we need to send this through a getter as only d3d_rlight knows about the lightmap data type
			LPDIRECT3DTEXTURE9 CurrentLightmap = D3D_GetLightmap (surf->d3d_Lightmap);

			if (CurrentLightmap != CachedLightmap)
			{
				d3d_BrushFX.SetTexture (1, CurrentLightmap);
				CachedLightmap = CurrentLightmap;
			}

			if ((D3DXMATRIX *) surf->model->matrix != CachedMatrix)
			{
				d3d_BrushFX.SetEntMatrix ((D3DXMATRIX *) surf->model->matrix);
				CachedMatrix = (D3DXMATRIX *) surf->model->matrix;

				NumMatrixSwaps++;
			}

			if (d3d_BrushModelVerts)
			{
				// draw the surface directly from the vertex buffer
				d3d_BrushFX.Draw (D3DPT_TRIANGLEFAN, surf->vboffset, surf->numedges - 2);
			}
			else
			{
				// draw from surf->polys
			}
		}
	}
}


void D3D_DrawWorld (void)
{
	// set up shaders
	d3d_Device->SetStreamSource (0, d3d_BrushModelVerts, 0, sizeof (worldvert_t));
	d3d_Device->SetVertexDeclaration (d3d_V3ST4Declaration);

	d3d_BrushFX.BeginRender ();
	d3d_BrushFX.SetWPMatrix (&(d3d_WorldMatrix * d3d_PerspectiveMatrix));
	d3d_BrushFX.SetScale ((d3d_GlobalCaps.AllowA16B16G16R16 && r_64bitlightmaps.integer) ? 4.0f * r_lightscale.value : 2.0f * r_lightscale.value);

	CachedMatrix = NULL;
	NumMatrixSwaps = 0;

	// the passes we need to run will depend on what we are drawing
	if (r_renderflags & R_RENDERNOLUMA) D3D_RunBrushPass (PASS_NOTRANSNOLUMA);
	if (r_renderflags & R_RENDERLUMA) D3D_RunBrushPass (PASS_NOTRANSLUMA);

	d3d_BrushFX.EndRender ();
}


void D3D_InitSurfMinMaxs (msurface_t *surf);
void D3D_CheckSurfMinMaxs (msurface_t *surf, float *v);


void D3D_AddInlineBModelsToTextureChains (void)
{
	if (!r_drawentities.value) return;
	if (!(r_renderflags & R_RENDERINLINEBRUSH)) return;

	for (int x = 0; x < cl_numvisedicts; x++)
	{
		// R_TextureAnimation needs this
		entity_t *e = cl_visedicts[x];
		model_t *clmodel = e->model;

		// only doing brushes here
		if (clmodel->type != mod_brush) continue;

		// only do inline models
		if (clmodel->name[0] != '*') continue;

		// set up, cull, matrix calc, etc
		if (R_SetupBrushEntity (e)) continue;

		// R_TextureAnimation needs this
		currententity = e;

		if (d3d_AutomapDraw)
			;
		else
		{
			// calculate dynamic lighting for bmodel
			R_PushDlights (clmodel->bh->nodes + clmodel->bh->hulls[0].firstclipnode);
		}

		int i;
		msurface_t *surf;

		// mark the surfaces
		for (i = 0, surf = &clmodel->bh->surfaces[clmodel->bh->firstmodelsurface]; i < clmodel->bh->nummodelsurfaces; i++, surf++)
		{
			// prevent surfs from being added twice, causing the texture chain to loop around on itself.
			// this only happens during SCR_UpdateScreen calls from modal dialogs or the loading plaque,
			// still not entirely certain why.  It's a "mods only" thing, by the way, and doesn't happen
			// AT ALL in ID1.  This is one one the reasons why I HATE mods.
			if (surf->visframe == r_framecount) continue;
			surf->visframe = r_framecount;

			// set the model correctly (is this used anymore???)
			// (yes - on the instanced brush transforms)
			surf->model = clmodel;

			// find which side of the node we are on
			float dot = DotProduct (modelorg, surf->plane->normal) - surf->plane->dist;

			// draw the polygon
			if ((((surf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) || (!(surf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON))))
			{
				texture_t *tex = R_TextureAnimation (surf->texinfo->texture);

				if (surf->flags & SURF_DRAWTURB)
				{
					// add to the appropriate chain (back to front)
					surf->texturechain = tex->texturechain;
					tex->texturechain = surf;

					// flag for rendering
					r_renderflags |= R_RENDERWATERSURFACE;
				}
				else if (surf->flags & SURF_DRAWSKY)
				{
					if (!d3d_AutomapDraw)
					{
						// add to the appropriate chain (back to front)
						surf->texturechain = skychain;
						skychain = surf;
					}
				}
				else
				{
					// check for lightmap modifications
					if (!d3d_AutomapDraw) D3D_CheckLightmapModification (surf);

					// link it in (add to the end of the surf texture chains so that they can be depth-clipped)
					if (!tex->chaintail)
						tex->texturechain = surf;
					else tex->chaintail->texturechain = surf;

					tex->chaintail = surf;
					surf->texturechain = NULL;
				}
			}
		}
	}
}

