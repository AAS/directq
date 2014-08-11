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

// eeewwww - this code is disgusting
// NONE OF THIS IS SATISFACTORILY RESOLVED RIGHT NOW - wait for 1.8
#include "quakedef.h"
#include "d3d_quake.h"

void D3D_TransformWaterSurface (msurface_t *surf);
void R_TransformModelSurface (model_t *mod, msurface_t *surf);

// OK, it happens in GLQuake which is argument for 0, but it didn't happen in
// software, which is a bigger argument for 1.  So 1 it is.
cvar_t r_zhack ("r_zfighthack", "1", CVAR_ARCHIVE);
cvar_t r_instancedlight ("r_instancedlight", "1", CVAR_ARCHIVE);
extern cvar_t r_warpspeed;

// r_lightmap 1 uses the grey texture on account of 2 x overbrighting
extern LPDIRECT3DTEXTURE9 r_greytexture;
extern LPDIRECT3DTEXTURE9 r_blacktexture;
extern cvar_t r_lightmap;

texture_t *R_TextureAnimation (texture_t *base);
void R_LightPoint (entity_t *e, float *c);
void D3D_RotateForEntity (entity_t *e);
void R_PushDlights (mnode_t *headnode);
__inline void R_AddSurfToDrawLists (msurface_t *surf);
extern DWORD D3D_OVERBRIGHT_MODULATE;

/*
=======================
R_MergeBrushModelToWorld

merges the surfs in a brushmodel with the world
=======================
*/
bool R_MergeBrushModelToWorld (entity_t *e)
{
	// set currententity for R_TextureAnimation and retrieve the model pointer for easier access
	d3d_RenderDef.currententity = e;
	model_t *clmodel = e->model;

	// setup for backface culling check
	//VectorSubtract (r_refdef.vieworg, e->origin, modelorg);
	//VectorSubtract (e->origin, r_refdef.vieworg, modelorg);

	// setup
	int drawsurfs = 0;
	msurface_t *surf = &clmodel->bh->surfaces[clmodel->bh->firstmodelsurface];

	// firstly we add all models to texture chains
	for (int i = 0; i < clmodel->bh->nummodelsurfaces; i++, surf++)
	{
		// opaque water surfs can get an infinite texturechain on a changelevel in some maps.
		// this ensures that each surf is only chained once.  this will be fixed properly in 1.8
		if (surf->visframe == d3d_RenderDef.framecount) continue;

		// find which side of the node we are on
		mplane_t *pplane = surf->plane;
		float dot = DotProduct (modelorg, pplane->normal) - pplane->dist;

		// check the surface (note - this doesn't behave itself properly right now)
		// we're gonna hope that there won't be too many of these...!
		// if (((surf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) || (!(surf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON)))
		{
			// store out the surface matrix
			surf->matrix = e->matrix;

			// transform the surf by the matrix stored for the model containing it
			// sky and liquid surfs on ALL inline bmodels get merged to the world render.  normal surfs on non-alpha
			// inline bmodels get merged to the world render.  only normal surfs on alpha bmodels get drawn separately.
			if (surf->flags & SURF_DRAWSKY)
			{
				R_TransformModelSurface (clmodel, surf);
				R_AddSurfToDrawLists (surf);
			}
			else if (surf->flags & SURF_DRAWTURB)
			{
				D3D_TransformWaterSurface (surf);
				R_AddSurfToDrawLists (surf);
			}
			else if (e->alphaval > 254)
			{
				R_TransformModelSurface (clmodel, surf);
				R_AddSurfToDrawLists (surf);
			}
			else drawsurfs++;

			// prevent infinite texturechain; see above
			surf->visframe = d3d_RenderDef.framecount;
		}
	}

	// no surfs got added
	return !!(drawsurfs);
}


/*
=======================
R_PrepareBrushEntity

Prepares a brushmodel entity for rendering; calculating transforms, dynamic lighting, etc.

!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
Note: it's not safe to store ANYTHING in either e->model or clmodel or members thereof for
instanced bmodels going through here as these models may be shared by more than one entity!
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
=======================
*/
bool R_PrepareBrushEntity (entity_t *e)
{
	// just in case...
	d3d_RenderDef.currententity = e;

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
	if (!e->nocullbox)
		if (R_CullBox (mins, maxs))
			return false;

	// set up modelorg and matrix (fixme - move modelorg to entity_t)
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

	if (r_zhack.value && clmodel->bh->brushtype == MOD_BRUSH_INLINE)
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
		d3d_WorldMatrixStack->GetMatrix ((D3DXMATRIX *) e->matrix);
		d3d_WorldMatrixStack->Pop ();
	}
	else
	{
		// lightweight translation-only
		D3DXMatrixIdentity ((D3DXMATRIX *) e->matrix);
		((D3DXMATRIX *) e->matrix)->_41 = e->origin[0];
		((D3DXMATRIX *) e->matrix)->_42 = e->origin[1];
		((D3DXMATRIX *) e->matrix)->_43 = e->origin[2];
	}

	if (r_zhack.value && clmodel->bh->brushtype == MOD_BRUSH_INLINE)
	{
		// un-hack the origin
		e->origin[0] += DIST_EPSILON;
		e->origin[1] += DIST_EPSILON;
		e->origin[2] += DIST_EPSILON;
	}

	// set up lighting for the model
	if (clmodel->bh->brushtype == MOD_BRUSH_INLINE)
	{
		// calculate dynamic lighting for bmodel if it's not an instanced model
		R_PushDlights (clmodel->bh->nodes + clmodel->bh->hulls[0].firstclipnode);

		// for an inline brushmodel we merge all of it's surfaces with the world, unless it's translucent, in which
		// case we only merge sky and liquid surfs in it with the world.  the global r_wateralpha cvar takes precedence
		// over entity translucency here (that's tough shit, but a decision has to be made and this is mine).
		return R_MergeBrushModelToWorld (e);
	}
	else
	{
		// get lighting information for an instanced model
		R_LightPoint (e, e->shadelight);
	}

	// not culled
	return true;
}


/*
=======================
R_SetInstancedStage0

split out to keep code cleaner

to do - lerp between lightmap value and shadelight for intermediate values of r_instancedlight
=======================
*/
void R_SetInstancedStage0 (entity_t *e)
{
	DWORD bsplight = D3DCOLOR_ARGB
	(
		BYTE_CLAMP (e->alphaval),
		vid.lightmap[BYTE_CLAMP (e->shadelight[0])],
		vid.lightmap[BYTE_CLAMP (e->shadelight[1])],
		vid.lightmap[BYTE_CLAMP (e->shadelight[2])]
	);

	D3D_SetTextureColorMode (0, D3DTOP_SELECTARG2, D3DTA_TEXTURE, D3DTA_CONSTANT);
	D3D_SetTextureStageState (0, D3DTSS_CONSTANT, bsplight);
}


/*
=======================
R_SetBrushSurfaceStates

Setup states for rendering brush surfaces depending on the requested flags
Note: some flags are mutually exclusive and should not be used together unless you want weird things to happen
=======================
*/
void R_SetBrushSurfaceStates (entity_t *e, int flag)
{
	// don't mipmap lightmaps, mipmap the world and fullbrights
	D3D_SetTextureMipmap (0, D3DTEXF_LINEAR, D3DTEXF_LINEAR, D3DTEXF_NONE);
	D3D_SetTextureMipmap (1, d3d_3DFilterMag, d3d_3DFilterMin, d3d_3DFilterMip);
	D3D_SetTextureMipmap (2, d3d_3DFilterMag, d3d_3DFilterMin, d3d_3DFilterMip);

	D3D_SetTextureAddressMode (D3DTADDRESS_CLAMP, D3DTADDRESS_WRAP, D3DTADDRESS_WRAP);
	D3D_SetTexCoordIndexes (1, 0, 0);

	// set up shaders
	D3D_SetFVF (D3DFVF_XYZ | D3DFVF_TEX2);

	// first stage is the lightmap (common to everything)
	// determine the correct blend type for it - this is a bit ugly here....
	if (e->model->bh->brushtype == MOD_BRUSH_WORLD)
	{
		// enforce replace mode for the world model
		D3D_SetTextureColorMode (0, D3DTOP_SELECTARG1, D3DTA_TEXTURE, D3DTA_DIFFUSE);
		D3D_SetTextureAlphaMode (0, D3DTOP_DISABLE);
	}
	else if (e->alphaval < 255)
	{
		if (e->model->bh->brushtype == MOD_BRUSH_INSTANCED && r_instancedlight.value)
		{
			R_SetInstancedStage0 (e);
			D3D_SetTextureAlphaMode (0, D3DTOP_SELECTARG1, D3DTA_CONSTANT, D3DTA_DIFFUSE);
		}
		else
		{
			D3D_SetTextureColorMode (0, D3DTOP_SELECTARG1, D3DTA_TEXTURE, D3DTA_DIFFUSE);
			D3D_SetTextureAlphaMode (0, D3DTOP_SELECTARG1, D3DTA_CONSTANT, D3DTA_DIFFUSE);
			D3D_SetTextureStageState (0, D3DTSS_CONSTANT, D3DCOLOR_ARGB (BYTE_CLAMP (e->alphaval), 255, 255, 255));
		}
	}
	else if (e->model->bh->brushtype == MOD_BRUSH_INSTANCED && r_instancedlight.value)
	{
		R_SetInstancedStage0 (e);
		D3D_SetTextureAlphaMode (0, D3DTOP_DISABLE);
	}
	else
	{
		// default
		D3D_SetTextureColorMode (0, D3DTOP_SELECTARG1, D3DTA_TEXTURE, D3DTA_DIFFUSE);
		D3D_SetTextureAlphaMode (0, D3DTOP_DISABLE);
	}

	if (flag & R_RENDERNOLUMA)
	{
		// second stage is the texture
		D3D_SetTextureColorMode (1, D3D_OVERBRIGHT_MODULATE, D3DTA_TEXTURE, D3DTA_CURRENT);

		// ensure this is down because we can make multiple successive passes
		D3D_SetTextureColorMode (2, D3DTOP_DISABLE);
	}
	else if (flag & R_RENDERLUMA)
	{
		// second stage is the luma
		D3D_SetTextureColorMode (1, D3DTOP_ADD, D3DTA_TEXTURE, D3DTA_CURRENT);

		// third stage is the texture
		D3D_SetTextureColorMode (2, D3D_OVERBRIGHT_MODULATE, D3DTA_TEXTURE, D3DTA_CURRENT);
	}

	// common
	D3D_SetTextureAlphaMode (1, D3DTOP_DISABLE);
	D3D_SetTextureAlphaMode (2, D3DTOP_DISABLE);
}


/*
=======================
R_RefreshSurfaces

runs a surface refresh on surfaces that match the specified flag
=======================
*/
void R_RefreshSurfaces (entity_t *e, int flag)
{
	model_t *mod = e->model;
	msurface_t *surf;
	texture_t *tex;

	R_SetBrushSurfaceStates (e, flag);

	// draw 'em all
	for (int i = 0; i < mod->bh->numtextures; i++)
	{
		// no texture (e2m3 gets this) (fix me - get rid of this...!)
		if (!(tex = mod->bh->textures[i])) continue;

		// nothing to draw for this texture
		if (!(surf = tex->texturechain)) continue;

		// skip over
		if (surf->flags & SURF_DRAWSKY) continue;
		if (surf->flags & SURF_DRAWTURB) continue;

		// check luma
		if ((flag & R_RENDERNOLUMA) && tex->d3d_Fullbright) continue;
		if ((flag & R_RENDERLUMA) && !tex->d3d_Fullbright) continue;

		if (flag & R_RENDERNOLUMA)
		{
			// bind regular texture
			if (r_lightmap.integer)
				D3D_SetTexture (1, r_greytexture);
			else D3D_SetTexture (1, (LPDIRECT3DTEXTURE9) tex->d3d_Texture);
		}
		else if (flag & R_RENDERLUMA)
		{
			// bind luma texture - the check above ensures that it exists!
			D3D_SetTexture (1, (LPDIRECT3DTEXTURE9) tex->d3d_Fullbright);

			// bind regular texture
			if (r_lightmap.integer)
				D3D_SetTexture (2, r_greytexture);
			else D3D_SetTexture (2, (LPDIRECT3DTEXTURE9) tex->d3d_Texture);
		}

		for (; surf; surf = surf->texturechain)
		{
			// bind the lightmap
			surf->d3d_Lightmap->BindLightmap (0);

			// draw the surface from surf->polys
			D3D_DrawPrimitive (D3DPT_TRIANGLEFAN, surf->polys->numverts - 2, surf->polys->verts, sizeof (glpolyvert_t));
			d3d_RenderDef.brush_polys++;
		}
	}

	// take down the constant
	D3D_SetTextureStageState (0, D3DTSS_CONSTANT, 0xffffffff);

	// it's possible to come out of here with an alpha op selected so take it down too
	D3D_SetTextureColorMode (0, D3DTOP_SELECTARG1, D3DTA_TEXTURE, D3DTA_DIFFUSE);
	D3D_SetTextureAlphaMode (0, D3DTOP_DISABLE);
}


/*
=======================
R_ClearSurfaceChains

Clears all texture chains used by this given model and sets flags back to 0
=======================
*/
void R_ClearSurfaceChains (model_t *mod)
{
	texture_t *t;

	for (int i = 0; i < mod->bh->numtextures; i++)
	{
		// no texture (e2m3 gets this)
		if (!(t = mod->bh->textures[i]))
		{
			Con_Printf ("missing texture in %s\n", mod->name);
			continue;
		}

		// clear the chain
		t->texturechain = NULL;
		t->chaintail = NULL;
		t->visframe = -1;
	}

	// clear flags (move this to d3d_RenderDef)
	d3d_RenderDef.brushrenderflags = 0;
}


/*
=======================
R_TransformModelSurface

For bmodels this transforms a surface by the given matrix, recreating the verts for it
=======================
*/
void R_TransformModelSurface (model_t *mod, msurface_t *surf)
{
	// no matrix stored so don't transform it
	if (!surf->matrix) return;

	glpolyvert_t *p = surf->polys->verts;
	D3DXMATRIX *m = (D3DXMATRIX *) surf->matrix;

	for (int i = 0; i < surf->polys->numverts; i++, p++)
	{
		// rebuild the verts
		float *vec;
		int lindex = mod->bh->surfedges[surf->firstedge + i];

		if (lindex > 0)
			vec = mod->bh->vertexes[mod->bh->edges[lindex].v[0]].position;
		else vec = mod->bh->vertexes[mod->bh->edges[-lindex].v[1]].position;

		// transform them and copy out
		p->xyz[0] = vec[0] * m->_11 + vec[1] * m->_21 + vec[2] * m->_31 + m->_41;
		p->xyz[1] = vec[0] * m->_12 + vec[1] * m->_22 + vec[2] * m->_32 + m->_42;
		p->xyz[2] = vec[0] * m->_13 + vec[1] * m->_23 + vec[2] * m->_33 + m->_43;
	}
}


/*
=======================
R_DrawBrushModel

Draws either an inline or an instanced brush model
=======================
*/
bool R_DrawBrushModel (entity_t *e)
{
	// set currententity for R_TextureAnimation and retrieve the model pointer for easier access
	d3d_RenderDef.currententity = e;
	model_t *clmodel = e->model;

	// setup for backface culling check
	VectorSubtract (r_refdef.vieworg, e->origin, modelorg);

	// clear all texture chains in use by this model
	R_ClearSurfaceChains (clmodel);

	// setup
	int drawsurfs = 0;
	msurface_t *surf = &clmodel->bh->surfaces[clmodel->bh->firstmodelsurface];

	// firstly we add all models to texture chains
	for (int i = 0; i < clmodel->bh->nummodelsurfaces; i++, surf++)
	{
		// this code path is only used for alpha inline bmodels and instanced bmodels; non-alpha inline bmodels
		// get merged to the world render.  sky surfs and turb surfs on alpha inline bmodels also get merged to
		// the world render.  we don't support sky and turb surfs on instanced bmodels.
		if (surf->flags & SURF_DRAWSKY) continue;
		if (surf->flags & SURF_DRAWTURB) continue;

		// store out the surface matrix
		surf->matrix = e->matrix;

		// chain the surf
		texture_t *tex = R_TextureAnimation (surf->texinfo->texture);

		// link it in (add to the end of the surf texture chains so that they can be depth-clipped)
		if (!tex->chaintail)
			tex->texturechain = surf;
		else tex->chaintail->texturechain = surf;

		tex->chaintail = surf;
		surf->texturechain = NULL;

		// check flags
		if (tex->d3d_Fullbright && d3d_DeviceCaps.MaxTextureBlendStages > 2)
			d3d_RenderDef.brushrenderflags |= R_RENDERLUMA;
		else d3d_RenderDef.brushrenderflags |= R_RENDERNOLUMA;

		// check for modification to this lightmap; done even for instanced as they may have animating lightstyles
		if (surf->d3d_Lightmap) surf->d3d_Lightmap->CheckSurfaceForModification (surf);

		// transform the surf by the matrix stored for the model containing it
		R_TransformModelSurface (clmodel, surf);

		// accumulate counts
		drawsurfs++;
	}

	// no surfs got added
	if (!drawsurfs) return false;

	// draw the chained surfaces
	if (d3d_RenderDef.brushrenderflags & R_RENDERNOLUMA) R_RefreshSurfaces (e, R_RENDERNOLUMA);
	if (d3d_RenderDef.brushrenderflags & R_RENDERLUMA) R_RefreshSurfaces (e, R_RENDERLUMA);

	return true;
}


void D3D_DrawWorld (void)
{
	entity_t ent;
	memset (&ent, 0, sizeof (entity_t));
	ent.model = cl.worldmodel;
	ent.alphaval = 255;
	ent.model->bh->brushtype = MOD_BRUSH_WORLD;

	extern float r_clipdist;

	// now update the projection matrix for the world
	// note - DirectQ dirty matrix lazy transform setting won't actually send a matrix to Direct3D until it's actually used
	// so you can update matrixes as many time as you want between renders without any performance impact beyond a few CPU cycles
	if (!d3d_RenderDef.automap)
	{
		// standard perspective
		d3d_ProjMatrixStack->LoadIdentity ();
		d3d_ProjMatrixStack->Frustum3D (d3d_RenderDef.fov_x, d3d_RenderDef.fov_y, 4, r_clipdist);
	}

	if (d3d_RenderDef.brushrenderflags & R_RENDERNOLUMA) R_RefreshSurfaces (&ent, R_RENDERNOLUMA);
	if (d3d_RenderDef.brushrenderflags & R_RENDERLUMA) R_RefreshSurfaces (&ent, R_RENDERLUMA);
}


