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

void R_PushDlights (mnode_t *headnode);
void D3D_AddSkySurfaceToRender (msurface_t *surf, D3DMATRIX *m);
void D3D_DrawWaterSurfaces (void);


cvar_t r_lockpvs ("r_lockpvs", "0");
cvar_t r_maxvertexsubmission ("r_maxvertexsubmission", "65000", CVAR_ARCHIVE);

void D3D_RotateForEntity (entity_t *e);
void D3D_RotateForEntity (entity_t *e, D3DMATRIX *m);


/*
=============================================================

	ALL BRUSH MODELS

=============================================================
*/

CSpaceBuffer *Pool_ModelSurfs = NULL;
d3d_modelsurf_t *d3d_ModelSurfs = NULL;
int d3d_NumModelSurfs = 0;

void D3D_AllocModelSurf (msurface_t *surf, texture_t *tex, D3DMATRIX *matrix)
{
	// ensure that there is space
	Pool_ModelSurfs->Alloc (sizeof (d3d_modelsurf_t));

	// take the next modelsurf
	d3d_modelsurf_t *ms = &d3d_ModelSurfs[d3d_NumModelSurfs++];

	ms->surf = surf;
	ms->tex = tex;
	ms->matrix = matrix;
}


void D3D_DrawModelSurfs (bool fbon)
{
	int cachedlightmap = 0;
	int cachedtexture = 0;

	int numverts = 0;
	int numindexes = 0;

	brushpolyvert_t *brushverts = NULL;
	unsigned short *brushindexes = NULL;

	bool stateset = false;

	for (int i = 0; i < d3d_NumRegisteredTextures; i++)
	{
		d3d_registeredtexture_t *rt = &d3d_RegisteredTextures[i];

		if (!rt->texture) continue;
		if (!rt->surfchain) continue;
		if (!rt->texture->teximage) continue;
		if (rt->surfchain->surf->flags & SURF_DRAWSKY) continue;
		if (rt->surfchain->surf->flags & SURF_DRAWTURB) continue;

		if (fbon && !rt->texture->lumaimage) continue;
		if (!fbon && rt->texture->lumaimage) continue;

		if (!stateset)
		{
			D3D_BeginVertexes ((void **) &brushverts, (void **) &brushindexes, sizeof (brushpolyvert_t));
			stateset = true;
		}

		texture_t *tex = rt->texture;

		for (d3d_modelsurf_t *modelsurf = rt->surfchain; modelsurf; modelsurf = modelsurf->surfchain)
		{
			msurface_t *surf = modelsurf->surf;
			d3d_RenderDef.brush_polys++;

			if (!D3D_CheckVBO (surf->numverts, (surf->numverts - 2) * 3))
			{
				D3D_EndVertexes ();
				D3D_BeginVertexes ((void **) &brushverts, (void **) &brushindexes, sizeof (brushpolyvert_t));
				numindexes = 0;
				numverts = 0;

				// force a texture change too on the new VBO
				cachedtexture = -1;
			}

			// a change of lightmap or of texture needs a new batch
			if ((int) surf->d3d_LightmapTex != cachedlightmap || (int) tex->teximage != cachedtexture)
			{
				if (numverts || numindexes)
				{
					// go to a new vbo mark
					D3D_GotoNewVBOMark ();
				}

				// update textures (D3D_SetTexture will soak up state changes here)
				// this needs to be done here otherwise we won't get correct textures set owing to moving the cache outside the main loop
				if (tex->lumaimage)
				{
					D3D_SetVBOColorMode (1, D3DTOP_ADD, D3DTA_TEXTURE, D3DTA_CURRENT);
					D3D_SetVBOColorMode (2, D3D_OVERBRIGHT_MODULATE, D3DTA_TEXTURE, D3DTA_CURRENT);

					D3D_SetVBOTexture (1, tex->lumaimage->d3d_Texture);
					D3D_SetVBOTexture (2, tex->teximage->d3d_Texture);
				}
				else
				{
					D3D_SetVBOColorMode (1, D3D_OVERBRIGHT_MODULATE, D3DTA_TEXTURE, D3DTA_CURRENT);
					D3D_SetVBOColorMode (2, D3DTOP_DISABLE);

					D3D_SetVBOTexture (1, tex->teximage->d3d_Texture);
				}

				// and the lightmap
				D3D_SetVBOColorMode (0, D3DTOP_SELECTARG1, D3DTA_TEXTURE, D3DTA_CURRENT);
				D3D_SetVBOTexture (0, surf->d3d_LightmapTex);

				// store back cached stuff
				cachedlightmap = (int) surf->d3d_LightmapTex;
				cachedtexture = (int) tex->teximage;
			}

			// write out the matrix to the surf so that it transforms correctly
			surf->matrix = modelsurf->matrix;

			// emit indexes
			for (int v = 2; v < surf->numverts; v++)
			{
				brushindexes[numindexes++] = numverts;
				brushindexes[numindexes++] = numverts + v - 1;
				brushindexes[numindexes++] = numverts + v;
			}

			D3DMATRIX *m = surf->matrix;
			polyvert_t *src = surf->verts;

			// emit vertexes
			for (int v = 0; v < surf->numverts; v++, src++, brushverts++)
			{
				if (m)
				{
					brushverts->xyz[0] = src->basevert[0] * m->_11 + src->basevert[1] * m->_21 + src->basevert[2] * m->_31 + m->_41;
					brushverts->xyz[1] = src->basevert[0] * m->_12 + src->basevert[1] * m->_22 + src->basevert[2] * m->_32 + m->_42;
					brushverts->xyz[2] = src->basevert[0] * m->_13 + src->basevert[1] * m->_23 + src->basevert[2] * m->_33 + m->_43;
				}
				else
				{
					brushverts->xyz[0] = src->basevert[0];
					brushverts->xyz[1] = src->basevert[1];
					brushverts->xyz[2] = src->basevert[2];
				}

				brushverts->st[0] = src->st[0];
				brushverts->st[1] = src->st[1];

				brushverts->lm[0] = src->lm[0];
				brushverts->lm[1] = src->lm[1];
			}

			// explicitly NULL the surface matrix
			surf->matrix = NULL;
			numverts += surf->numverts;

			D3D_UpdateVBOMark (surf->numverts, (surf->numverts - 2) * 3);
		}
	}

	// set indexes for the last primitive (if any)
	// required always so that the buffer will unlock
	if (stateset) D3D_EndVertexes ();
}


void D3D_SetBrushCommonState (int alphaval)
{
	D3D_SetFVF (D3DFVF_XYZ | D3DFVF_TEX2);

	D3D_SetTextureMipmap (0, D3DTEXF_LINEAR, D3DTEXF_LINEAR, D3DTEXF_NONE);
	D3D_SetTextureMipmap (1, d3d_3DFilterMag, d3d_3DFilterMin, d3d_3DFilterMip);
	D3D_SetTextureMipmap (2, d3d_3DFilterMag, d3d_3DFilterMin, d3d_3DFilterMip);

	D3D_SetTextureAddressMode (D3DTADDRESS_CLAMP, D3DTADDRESS_WRAP, D3DTADDRESS_WRAP);
	D3D_SetTexCoordIndexes (1, 0, 0);

	D3D_SetTextureColorMode (0, D3DTOP_SELECTARG1, D3DTA_TEXTURE, D3DTA_CURRENT);
	D3D_SetTextureColorMode (1, D3DTOP_MODULATE2X, D3DTA_TEXTURE, D3DTA_CURRENT);

	// stage 0 is the lightmap (and may be modulated with alpha)
	if (alphaval < 255)
	{
		D3D_SetTextureAlphaMode (0, D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_TFACTOR);
		D3D_SetRenderState (D3DRS_TEXTUREFACTOR, D3DCOLOR_ARGB (BYTE_CLAMP (alphaval), 255, 255, 255));
	}
	else D3D_SetTextureAlphaMode (0, D3DTOP_DISABLE);

	// no alpha ops here irrespective
	D3D_SetTextureAlphaMode (1, D3DTOP_DISABLE);
	D3D_SetTextureColorMode (2, D3DTOP_DISABLE);
	D3D_SetTextureAlphaMode (2, D3DTOP_DISABLE);
}


void D3D_EmitModelSurfToAlpha (d3d_modelsurf_t *modelsurf);

void D3D_AddWorldSurfacesToRender (void)
{
	// this should only ever happen if the scene is filled with water or sky
	if (!d3d_NumModelSurfs) return;

	// common
	D3D_SetBrushCommonState (255);

	// reverse the order to get correct front to back ordering
	d3d_modelsurf_t *modelsurf = &d3d_ModelSurfs[d3d_NumModelSurfs - 1];

	// merge the modelsurfs into the final texture order
	for (int i = 0; i < d3d_NumModelSurfs; i++, modelsurf--)
	{
		// a bmodel will have already been added to the alpha list
		// turb surfaces need to be added at this time
		if (modelsurf->surf->alphaval < 255)
		{
			if (modelsurf->surf->flags & SURF_DRAWTURB)
				D3D_EmitModelSurfToAlpha (modelsurf);

			continue;
		}

		d3d_registeredtexture_t *rt = modelsurf->tex->registration;

		modelsurf->surfchain = rt->surfchain;
		rt->surfchain = modelsurf;
	}

	// now emit from the list of registered textures in two passes for fb/no fb
	D3D_DrawModelSurfs (false);
	D3D_DrawModelSurfs (true);

	// draw opaque water here
	D3D_DrawWaterSurfaces ();
}


/*
=============================================================

	WORLD MODEL BUILDING

=============================================================
*/

extern float r_clipdist;

texture_t *R_TextureAnimation (entity_t *ent, texture_t *base)
{
	int count;
	int relative;
	texture_t *cached = base;

	if (ent->frame)
	{
		if (base->alternate_anims)
			base = base->alternate_anims;
	}

	if (!base->anim_total) return base;

	relative = (int) (cl.time * 10) % base->anim_total;

	count = 0;	

	while (base->anim_min > relative || base->anim_max <= relative)
	{
		base = base->anim_next;

		// prevent crash here
		if (!base) return cached;
		if (++count > 100) return cached;
	}

	return base;
}


__inline void R_MarkLeafSurfs (mleaf_t *leaf, int visframe)
{
	msurface_t **mark = leaf->firstmarksurface;
	int c = leaf->nummarksurfaces;

	if (c)
	{
		do
		{
			(*mark)->visframe = visframe;
			mark++;
		} while (--c);
	}
}


__inline void R_PlaneSide (mplane_t *plane, float *dot, int *side)
{
	// find which side of the node we are on
	switch (plane->type)
	{
	case PLANE_X:
		*dot = d3d_RenderDef.worldentity.modelorg[0] - plane->dist;
		break;
	case PLANE_Y:
		*dot = d3d_RenderDef.worldentity.modelorg[1] - plane->dist;
		break;
	case PLANE_Z:
		*dot = d3d_RenderDef.worldentity.modelorg[2] - plane->dist;
		break;
	default:
		*dot = DotProduct (d3d_RenderDef.worldentity.modelorg, plane->normal) - plane->dist;
		break;
	}

	if (*dot >= 0)
		*side = 0;
	else *side = 1;
}


__inline void D3D_AddSurfToDrawLists (msurface_t *surf)
{
	// world surfs never have explicit alpha
	// (they may pick it up from the value of r_wateralpha)
	surf->alphaval = 255;

	if (surf->flags & SURF_DRAWSKY)
		D3D_AddSkySurfaceToRender (surf, NULL);
	else
	{
		// check for lightmap modifications
		if (surf->d3d_Lightmap) surf->d3d_Lightmap->CheckSurfaceForModification (surf);

		texture_t *tex = R_TextureAnimation (&d3d_RenderDef.worldentity, surf->texinfo->texture);

		// this only ever comes from the world so matrix is always NULL
		D3D_AllocModelSurf (surf, tex, NULL);
	}
}


void R_StoreEfrags (efrag_t **ppefrag);

void R_RecursiveWorldNode (mnode_t *node)
{
	int			side;
	msurface_t	*surf;
	float		dot;

	if (node->contents == CONTENTS_SOLID) return;
	if (node->visframe != d3d_RenderDef.visframecount) return;
	if (R_CullBox (node->minmaxs, node->minmaxs + 3)) return;

	// if a leaf node, draw stuff
	if (node->contents < 0)
	{
		// mark the surfs
		R_MarkLeafSurfs ((mleaf_t *) node, d3d_RenderDef.framecount);

		// add static entities contained in the leaf to the list
		R_StoreEfrags (&((mleaf_t *) node)->efrags);
		return;
	}

	// node is just a decision point, so go down the appropriate sides
	// find which side of the node we are on
	R_PlaneSide (node->plane, &dot, &side);

	// check max dot
	if (dot > r_clipdist) r_clipdist = dot;
	if (-dot > r_clipdist) r_clipdist = -dot;

	// recurse down the children, front side first
	R_RecursiveWorldNode (node->children[side]);

	// add stuff to the draw lists
	if (node->numsurfaces)
	{
		int c = node->numsurfaces;

		if (dot < 0 -BACKFACE_EPSILON)
			side = SURF_PLANEBACK;
		else if (dot > BACKFACE_EPSILON)
			side = 0;

		for (surf = cl.worldmodel->brushhdr->surfaces + node->firstsurface; c; c--, surf++)
		{
			// the SURF_PLANEBACK test never actually evaluates to true with GLQuake as the surf
			// will have the same plane and facing as the node here
			if (surf->visframe != d3d_RenderDef.framecount) continue;
			if (R_CullBox (surf->mins, surf->maxs)) continue;

			D3D_AddSurfToDrawLists (surf);

			// in case a bad BSP overlaps the surfs in it's nodes
			surf->visframe = -1;
		}
	}

	// recurse down the back side
	R_RecursiveWorldNode (node->children[!side]);
}


void R_AutomapSurfaces (void)
{
	// don't bother depth-sorting these
	// some Quake weirdness here
	// this info taken from qbsp source code:
	// leaf 0 is a common solid with no faces
	for (int i = 1; i <= cl.worldmodel->brushhdr->numleafs; i++)
	{
		mleaf_t *leaf = &cl.worldmodel->brushhdr->leafs[i];

		// show all leafs that have previously been seen
		if (!leaf->seen) continue;

		// need to cull here too otherwise we'll get static ents we shouldn't
		if (R_CullBox (leaf->minmaxs, leaf->minmaxs + 3)) continue;

		// mark the surfs
		R_MarkLeafSurfs (leaf, d3d_RenderDef.framecount);

		// add static entities contained in the leaf to the list
		R_StoreEfrags (&leaf->efrags);
	}

	int side;
	float dot;

	// setup modelorg to a point so far above the viewpoint that it may as well be infinite
	d3d_RenderDef.worldentity.modelorg[0] = (cl.worldmodel->mins[0] + cl.worldmodel->maxs[0]) / 2;
	d3d_RenderDef.worldentity.modelorg[1] = (cl.worldmodel->mins[1] + cl.worldmodel->maxs[1]) / 2;
	d3d_RenderDef.worldentity.modelorg[2] = ((cl.worldmodel->mins[2] + cl.worldmodel->maxs[2]) / 2) + 16777216.0f;

	extern cvar_t r_automap_nearclip;
	extern float r_automap_z;

	for (int i = 0; i < cl.worldmodel->brushhdr->numnodes; i++)
	{
		mnode_t *node = &cl.worldmodel->brushhdr->nodes[i];

		// node culling
		if (node->contents == CONTENTS_SOLID) continue;
		if (!node->seen) continue;
		if (R_CullBox (node->minmaxs, node->minmaxs + 3)) continue;

		// find which side of the node we are on
		R_PlaneSide (node->plane, &dot, &side);

		// check max dot
		if (dot > r_clipdist) r_clipdist = dot;
		if (-dot > r_clipdist) r_clipdist = -dot;

		for (int j = 0; j < node->numsurfaces; j++)
		{
			msurface_t *surf = cl.worldmodel->brushhdr->surfaces + node->firstsurface + j;

			// surf culling
			if (surf->visframe != d3d_RenderDef.framecount) continue;
			if (surf->flags & SURF_DRAWSKY) continue;
			if (surf->mins[2] > (r_refdef.vieworg[2] + r_automap_nearclip.integer + r_automap_z)) continue;
			if ((dot < 0) ^ !!(surf->flags & SURF_PLANEBACK)) continue;

			D3D_AddSurfToDrawLists (surf);
		}
	}
}


void D3D_DrawAlphaBrushModel (entity_t *ent)
{
	// model was culled
	if (ent->visframe != d3d_RenderDef.framecount) return;

	// sanity check
	if (!ent->model) return;
	if (!ent->model->brushhdr) return;
	if (ent->model->type != mod_brush) return;

	// we ensured that the entity would only get added if it had appropriate surfs so this is quite safe
	D3D_SetBrushCommonState (ent->alphaval);

	model_t *mod = ent->model;
	msurface_t *surf = mod->brushhdr->surfaces + mod->brushhdr->firstmodelsurface;
	D3DMATRIX *m = &ent->matrix;

	// don't bother with ordering these for now; to be honest i'm quite sick of this code at the moment
	// and not yet ready to give it the restructuring it needs.  i'm going to be maintaining it going forward
	// however so i do need to get back to it some time
	for (int s = 0; s < mod->brushhdr->nummodelsurfaces; s++, surf++)
	{
		// sky and water are dealt with separately
		if (surf->flags & SURF_DRAWSKY) continue;
		if (surf->flags & SURF_DRAWTURB) continue;

		// this doesn't use the optimal rendering path just yet;
		// someday somebody's gonna make a huge towering edifice of glass as a bmodel and i'll need to do it...
		texture_t *tex = R_TextureAnimation (ent, surf->texinfo->texture);

		// these surfs aren't backfaced
		if (tex->lumaimage)
		{
			D3D_SetTextureColorMode (1, D3DTOP_ADD, D3DTA_TEXTURE, D3DTA_CURRENT);
			D3D_SetTextureColorMode (2, D3D_OVERBRIGHT_MODULATE, D3DTA_TEXTURE, D3DTA_CURRENT);

			D3D_SetTexture (1, tex->lumaimage->d3d_Texture);
			D3D_SetTexture (2, tex->teximage->d3d_Texture);
		}
		else
		{
			D3D_SetTextureColorMode (1, D3D_OVERBRIGHT_MODULATE, D3DTA_TEXTURE, D3DTA_CURRENT);
			D3D_SetTextureColorMode (2, D3DTOP_DISABLE);

			D3D_SetTexture (1, tex->teximage->d3d_Texture);
		}

		D3D_SetTexture (0, surf->d3d_LightmapTex);

		// get space for the verts
		Pool_PolyVerts->Rewind ();
		brushpolyvert_t *verts = (brushpolyvert_t *) Pool_PolyVerts->Alloc (surf->numverts * sizeof (brushpolyvert_t));

		polyvert_t *src = surf->verts;
		brushpolyvert_t *dest = verts;

		for (int i = 0; i < surf->numverts; i++, src++, dest++)
		{
			if (m)
			{
				dest->xyz[0] = src->basevert[0] * m->_11 + src->basevert[1] * m->_21 + src->basevert[2] * m->_31 + m->_41;
				dest->xyz[1] = src->basevert[0] * m->_12 + src->basevert[1] * m->_22 + src->basevert[2] * m->_32 + m->_42;
				dest->xyz[2] = src->basevert[0] * m->_13 + src->basevert[1] * m->_23 + src->basevert[2] * m->_33 + m->_43;
			}
			else
			{
				dest->xyz[0] = src->basevert[0];
				dest->xyz[1] = src->basevert[1];
				dest->xyz[2] = src->basevert[2];
			}

			dest->st[0] = src->st[0];
			dest->st[1] = src->st[1];

			dest->lm[0] = src->lm[0];
			dest->lm[1] = src->lm[1];
		}

		d3d_Device->DrawPrimitiveUP (D3DPT_TRIANGLEFAN, surf->numverts - 2, verts, sizeof (brushpolyvert_t));
		d3d_RenderDef.brush_polys++;
	}
}


void D3D_SetupBrushModel (entity_t *ent)
{
	bool rotated;
	vec3_t mins, maxs;
	model_t *mod = ent->model;

	if (ent->angles[0] || ent->angles[1] || ent->angles[2])
	{
		rotated = true;

		for (int i = 0; i < 3; i++)
		{
			mins[i] = ent->origin[i] - mod->radius;
			maxs[i] = ent->origin[i] + mod->radius;
		}
	}
	else
	{
		rotated = false;

		VectorAdd (ent->origin, mod->mins, mins);
		VectorAdd (ent->origin, mod->maxs, maxs);
	}

	// static entities already have the leafs they are in bbox culled
	if (R_CullBox (mins, maxs))
	{
		// mark as not visible
		ent->visframe = -1;
		return;
	}

	// flag the model as having been previously seen
	ent->model->wasseen = true;

	// store transform for model - we need to run this in software as we are potentially submitting
	// multiple brush models in a single batch, all of which will be merged with the world render.
	D3D_LoadIdentity (&ent->matrix);
	ent->angles[0] = -ent->angles[0];	// stupid quake bug
	D3D_RotateForEntity (ent, &ent->matrix);
	ent->angles[0] = -ent->angles[0];	// stupid quake bug

	// anti z-fighting hack
	// this value gets rid of most artefacts without producing too much visual discontinuity
	ent->matrix._43 -= 0.1f;

	// cache data for visible entities for the automap
	if (ent->model->cacheent && ent->model->name[0] == '*')
	{
		// if we're caching models just store out it's last status
		// so that the automap can at least draw it at the position/orientation/etc it was
		// the last time that the player saw it.  this is only done for inline brush models
		entity_t *e2 = ent->model->cacheent;

		// we don't actually need the full contents of the entity_t struct here
		VectorCopy (ent->angles, e2->angles);
		VectorCopy (ent->origin, e2->origin);

		e2->alphaval = ent->alphaval;
		e2->model = ent->model;
		e2->frame = ent->frame;
		e2->nocullbox = ent->nocullbox;

		memcpy (&e2->matrix, &ent->matrix, sizeof (D3DMATRIX));

		e2->visframe = d3d_RenderDef.framecount;

		// set the cached ent back to itself (ensure)
		e2->model->cacheent = e2;
	}

	// get origin vector relative to viewer
	// this is now stored in the entity so we can read it back any time we want
	VectorSubtract (r_refdef.vieworg, ent->origin, ent->modelorg);

	// adjust for rotation
	if (rotated)
	{
		vec3_t temp;
		vec3_t forward, right, up;

		// i fixed this bug for matrix transforms but of course it came back here... sigh...
		VectorCopy (ent->modelorg, temp);
		AngleVectors (ent->angles, forward, right, up);

		ent->modelorg[0] = DotProduct (temp, forward);
		ent->modelorg[1] = -DotProduct (temp, right);
		ent->modelorg[2] = DotProduct (temp, up);
	}

	if (mod->name[0] == '*')
	{
		// calculate dynamic lighting for the inline bmodel
		R_PushDlights (mod->brushhdr->nodes + mod->brushhdr->hulls[0].firstclipnode);
	}

	// if the ent has alpha we need to add it to the alpha list
	// even so we still run through the surfs to check for water and sky which are handled separately
	// (sky never has alpha, water - including on alpha ents - is also handled separately and will get the
	// correct alpha ent water if the ent has alpha during the water rendering pass)
	msurface_t *surf = mod->brushhdr->surfaces + mod->brushhdr->firstmodelsurface;
	int numalphasurfs = 0;

	// don't bother with ordering these for now; we'll sort them by texture later
	for (int s = 0; s < mod->brushhdr->nummodelsurfaces; s++, surf++)
	{
		float dot = DotProduct (ent->modelorg, surf->plane->normal) - surf->plane->dist;

		if (((surf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) ||
			(!(surf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON)) ||
			(ent->alphaval < 255 && !(surf->flags & SURF_DRAWSKY)))
		{
			// the surface needs to inherit an alpha from the entity so that we know how to draw it
			surf->alphaval = ent->alphaval;

			// sky surfaces need to be added immediately
			// other surface types either go on the main list or the alpha list
			if (surf->flags & SURF_DRAWSKY)
				D3D_AddSkySurfaceToRender (surf, &ent->matrix);
			else if (surf->flags & SURF_DRAWTURB)
				D3D_AllocModelSurf (surf, surf->texinfo->texture, &ent->matrix);
			else
			{
				if (mod->name[0] == '*' && surf->d3d_Lightmap)
					surf->d3d_Lightmap->CheckSurfaceForModification (surf);

				if (ent->alphaval < 255)
				{
					// track the number of alpha surfs in this bmodel
					numalphasurfs++;
				}
				else
				{
					// add to the modelsurfs list
					D3D_AllocModelSurf (surf, R_TextureAnimation (ent, surf->texinfo->texture), &ent->matrix);
				}
			}
		}
	}

	if (numalphasurfs)
	{
		// ensure
		ent->visframe = d3d_RenderDef.framecount;
		D3D_AddToAlphaList (ent);
	}
}


void D3D_BuildWorld (void)
{
	// bound to sensible values - this max size is chosen to work with the menu slider
	// it also provides 5000 ish vertexes of headroom for primitives that may overshoot
	if (r_maxvertexsubmission.integer < 1) Cvar_Set (&r_maxvertexsubmission, 1);
	if (r_maxvertexsubmission.integer > 60001) Cvar_Set (&r_maxvertexsubmission, 60001);

	// clear surface chains
	for (int i = 0; i < d3d_NumRegisteredTextures; i++)
		d3d_RegisteredTextures[i].surfchain = NULL;

	if (!Pool_ModelSurfs) Pool_ModelSurfs = new CSpaceBuffer ("Model Surfs", 16, POOL_MAP);

	Pool_ModelSurfs->Rewind ();
	d3d_ModelSurfs = (d3d_modelsurf_t *) Pool_ModelSurfs->Alloc (1);
	d3d_NumModelSurfs = 0;

	// calculate dynamic lighting for the world
	R_PushDlights (cl.worldmodel->brushhdr->nodes);

	// build the world
	VectorCopy (r_refdef.vieworg, d3d_RenderDef.worldentity.modelorg);

	// never go < 4096 as that is the clip dist in regular quake and maps/mods may expect it
	r_clipdist = 4096;

	// assume that the world is always visible ;)
	// (although it might not be depending on the scene...)
	d3d_RenderDef.worldentity.visframe = d3d_RenderDef.framecount;

	// the automap has a different viewpoint so R_RecursiveWorldNode is not valid for it
	if (d3d_RenderDef.automap)
		R_AutomapSurfaces ();
	else R_RecursiveWorldNode (cl.worldmodel->brushhdr->nodes);

	if (r_drawentities.integer)
	{
		// add brushmodels to the render
		for (int i = 0; i < d3d_RenderDef.numvisedicts; i++)
		{
			entity_t *ent = d3d_RenderDef.visedicts[i];

			if (!ent->model) continue;
			if (ent->model->type != mod_brush) continue;

			// brush models have different surface types some of which we can't
			// draw during the alpha pass.  we also need to include it in the lightmap
			// uploads pass, so we need to add the full model at this stage.
			D3D_SetupBrushModel (ent);
		}
	}
}


/*
=============================================================

	VISIBILITY

=============================================================
*/

void R_LeafVisibility (byte *vis)
{
	// leaf 0 is the generic solid leaf; the last leaf overlaps with the first node
	mleaf_t *leaf = &cl.worldmodel->brushhdr->leafs[1];

	// mark leafs and surfaces as visible
	for (int i = 0; i < cl.worldmodel->brushhdr->numleafs; i++, leaf++)
	{
		if (!vis || (vis[i >> 3] & (1 << (i & 7))))
		{
			// note - nodes and leafs need to be in consecutive memory for this to work so
			// that the last leaf will resolve to the first node here; this is set up in Mod_LoadVisLeafsNodes
			// first node always needs to be in the pvs for R_RecursiveWorldNode
			mnode_t *node = (mnode_t *) leaf;

			do
			{
				// already added
				if (node->visframe == d3d_RenderDef.visframecount) break;

				// add it
				node->visframe = d3d_RenderDef.visframecount;
				node->seen = true;
				node = node->parent;
			} while (node);
		}
	}
}


bool R_NearWaterTest (void)
{
	msurface_t **mark = d3d_RenderDef.viewleaf->firstmarksurface;
	int c = d3d_RenderDef.viewleaf->nummarksurfaces;

	if (c)
	{
		do
		{
			if (mark[0]->flags & SURF_DRAWTURB) return true;
			mark++;
		} while (--c);
	}

	return false;
}


void R_MarkLeaves (void)
{
	byte	*vis;
	mnode_t	*node;
	int		i;
	extern	byte *mod_novis;
	static int old_novis = -666;

	// viewleaf hasn't changed, r_novis hasn't changed or we're drawing with a locked PVS
	if (((d3d_RenderDef.oldviewleaf == d3d_RenderDef.viewleaf) && (r_novis.integer == old_novis)) || r_lockpvs.value) return;

	// go to a new visframe
	d3d_RenderDef.visframecount++;
	old_novis = r_novis.integer;

	// add in visible leafs - we always add the fat PVS to ensure that client visibility
	// is the same as that which was used by the server; R_CullBox will take care of unwanted leafs
	if (r_novis.integer)
		R_LeafVisibility (NULL);
	else if (!R_NearWaterTest ())
		R_LeafVisibility (Mod_LeafPVS (d3d_RenderDef.viewleaf, cl.worldmodel));
	else R_LeafVisibility (Mod_FatPVS (r_origin));

	// no old viewleaf so can't make a transition check
	if (!d3d_RenderDef.oldviewleaf) return;

	// check for a contents transition
	if (d3d_RenderDef.oldviewleaf->contents == d3d_RenderDef.viewleaf->contents)
	{
		// if we're still in the same contents we still have the same contents colour
		d3d_RenderDef.viewleaf->contentscolor = d3d_RenderDef.oldviewleaf->contentscolor;
	}
}


