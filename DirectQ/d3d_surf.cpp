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


#include "quakedef.h"
#include "d3d_model.h"
#include "d3d_quake.h"

void R_PushDlights (mnode_t *headnode);
void D3DSky_AddSurfaceToRender (msurface_t *surf, entity_t *ent);
void D3DWarp_DrawWaterSurfaces (d3d_modelsurf_t **modelsurfs, int nummodelsurfs);
void D3DSky_FinalizeSky (void);
void R_MarkLeaves (void);
void D3D_EmitModelSurfToAlpha (d3d_modelsurf_t *modelsurf);
void D3D_UploadLightmaps (void);
void D3D_AddParticesToAlphaList (void);

cvar_t r_lockpvs ("r_lockpvs", "0");

void D3D_RotateForEntity (entity_t *e);
void D3D_RotateForEntity (entity_t *e, D3DMATRIX *m);

// surface interface
void D3DBrush_Begin (void);
void D3DBrush_End (void);
void D3DBrush_EmitSurface (d3d_modelsurf_t *ms);


/*
=============================================================

	ALL BRUSH MODELS

=============================================================
*/

#define MAX_MODELSURFS 65536

d3d_modelsurf_t **d3d_ModelSurfs = NULL;
int d3d_NumModelSurfs = 0;


void D3D_ModelSurfsBeginMap (void)
{
	d3d_ModelSurfs = (d3d_modelsurf_t **) MainHunk->Alloc (MAX_MODELSURFS * sizeof (d3d_modelsurf_t *));

	// allocate an initial batch
	for (int i = 0; i < 4096; i++)
		d3d_ModelSurfs[i] = (d3d_modelsurf_t *) MainHunk->Alloc (sizeof (d3d_modelsurf_t));

	d3d_NumModelSurfs = 0;
}


void D3D_AllocModelSurf (msurface_t *surf, texture_t *tex, entity_t *ent, int alpha = 255)
{
	if (surf->flags & SURF_DRAWSKY)
	{
		// sky is rendered immediately as it passes
		D3DSky_AddSurfaceToRender (surf, ent);
		return;
	}

	// everything else is batched up for drawing in the main pass
	// ensure that there is space
	if (d3d_NumModelSurfs >= MAX_MODELSURFS) return;

	// allocate as required
	if (!d3d_ModelSurfs[d3d_NumModelSurfs])
		d3d_ModelSurfs[d3d_NumModelSurfs] = (d3d_modelsurf_t *) MainHunk->Alloc (sizeof (d3d_modelsurf_t));

	// take the next modelsurf
	d3d_modelsurf_t *ms = d3d_ModelSurfs[d3d_NumModelSurfs];

	// addorder is used to maintain sort stability so that we can get proper back-to-front ordering on our chains
	ms->addorder = d3d_NumModelSurfs++;

	// base textures are commnon to all (liquids won't have lightmaps but they'll be NULL anyway)
	ms->textures[TEXTURE_LIGHTMAP] = surf->d3d_LightmapTex;
	ms->textures[TEXTURE_DIFFUSE] = tex->teximage->d3d_Texture;

	// the luma image also decides the shader pass to use
	// (this assumes that the surf is solid, it will be correctly set for liquid elsewhere)
	if (tex->lumaimage)
	{
		ms->textures[TEXTURE_LUMA] = tex->lumaimage->d3d_Texture;
		ms->shaderpass = alpha < 255 ? FX_PASS_WORLD_LUMA_ALPHA : FX_PASS_WORLD_LUMA;
	}
	else
	{
		ms->textures[TEXTURE_LUMA] = NULL;
		ms->shaderpass = alpha < 255 ? FX_PASS_WORLD_NOLUMA_ALPHA : FX_PASS_WORLD_NOLUMA;
	}

	// copy out everything else
	ms->surf = surf;
	ms->ent = ent;
	ms->surfalpha = alpha;
}


int D3DSurf_ModelSurfsSortFunc (d3d_modelsurf_t **ms1, d3d_modelsurf_t **ms2)
{
	// get all the lumas gathered together
	if (ms1[0]->textures[TEXTURE_LUMA] == ms2[0]->textures[TEXTURE_LUMA])
	{
		if (ms1[0]->textures[TEXTURE_DIFFUSE] == ms2[0]->textures[TEXTURE_DIFFUSE])
		{
			// because qsort is an unstable sort we need to maintain the correct f2b addition order from the BSP tree
			// if everything else is equal, so we do our final comparison on addorder
			if (ms1[0]->textures[TEXTURE_LIGHTMAP] == ms2[0]->textures[TEXTURE_LIGHTMAP])
				return ms1[0]->addorder - ms2[0]->addorder;
			else return (int) (ms1[0]->textures[TEXTURE_LIGHTMAP] - ms2[0]->textures[TEXTURE_LIGHTMAP]);
		}
		else return (int) (ms1[0]->textures[TEXTURE_DIFFUSE] - ms2[0]->textures[TEXTURE_DIFFUSE]);
	}
	else return (int) (ms1[0]->textures[TEXTURE_LUMA] - ms2[0]->textures[TEXTURE_LUMA]);
}


// because sorting can be relatively expensive, and because there is some other
// work we can be doing while we sort, let's run it on another thread
HANDLE hSurfSortMutex = NULL;
HANDLE hSurfSortThread = NULL;


DWORD WINAPI D3DSurf_SortProc (LPVOID blah)
{
	while (1)
	{
		// wait until we've been told that we're ready
		WaitForSingleObject (hSurfSortMutex, INFINITE);

		if (d3d_NumModelSurfs)
		{
			qsort
			(
				d3d_ModelSurfs,
				d3d_NumModelSurfs,
				sizeof (d3d_modelsurf_t *),
				(int (*) (const void *, const void *)) D3DSurf_ModelSurfsSortFunc
			);
		}

		// now the sort has completed so let the main thread come back
		ReleaseMutex (hSurfSortMutex);
	}

	return 0;
}


void D3D_AddSurfacesToRender (void)
{
	// this should only ever happen if the scene is filled with water or sky
	if (!d3d_NumModelSurfs) return;

#if 0
	// just do it nice and fast instead of building up loads of complex chains in multiple passes
	qsort
	(
		d3d_ModelSurfs,
		d3d_NumModelSurfs,
		sizeof (d3d_modelsurf_t *),
		(int (*) (const void *, const void *)) D3DSurf_ModelSurfsSortFunc
	);
#endif

	bool stateset = false;

	for (int i = 0; i < d3d_NumModelSurfs; i++)
	{
		d3d_modelsurf_t *ms = d3d_ModelSurfs[i];
		msurface_t *surf = ms->surf;

		if ((surf->flags & SURF_DRAWFENCE) || (ms->surfalpha < 255))
		{
			// store out for drawing later
			D3D_EmitModelSurfToAlpha (ms);
			continue;
		}

		// sky should never happen but turb might
		if (surf->flags & SURF_DRAWSKY) continue;
		if (surf->flags & SURF_DRAWTURB) continue;

		if (!stateset)
		{
			D3DBrush_Begin ();
			stateset = true;
		}

		// submit the surface
		D3DBrush_EmitSurface (ms);
	}

	// draw anything left over
	if (stateset) D3DBrush_End ();
}


/*
=============================================================

	WORLD MODEL BUILDING

=============================================================
*/

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

	relative = (int) (d3d_RenderDef.time * 10) % base->anim_total;
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


#define R_MarkLeafSurfs(leaf) \
{ \
	msurface_t **mark = (leaf)->firstmarksurface; \
	int c = (leaf)->nummarksurfaces; \
	if (c) \
	{ \
		do \
		{ \
			(*mark)->intersect = ((leaf)->bops == FULLY_INTERSECT_FRUSTUM); \
			(*mark)->visframe = d3d_RenderDef.framecount; \
			mark++; \
		} while (--c); \
	} \
}


void R_StoreEfrags (efrag_t **ppefrag);
int numrrwn = 0;

void R_RecursiveWorldNode (mnode_t *node)
{
	numrrwn++;

loc0:;
	// node is just a decision point, so go down the appropriate sides
	// find which side of the node we are on
	switch (node->plane->type)
	{
	case PLANE_X:
		node->dot = d3d_RenderDef.worldentity.modelorg[0] - node->plane->dist;
		break;
	case PLANE_Y:
		node->dot = d3d_RenderDef.worldentity.modelorg[1] - node->plane->dist;
		break;
	case PLANE_Z:
		node->dot = d3d_RenderDef.worldentity.modelorg[2] - node->plane->dist;
		break;
	default:
		node->dot = DotProduct (d3d_RenderDef.worldentity.modelorg, node->plane->normal) - node->plane->dist;
		break;
	}

	// find which side we're on
	node->side = (node->dot >= 0 ? 0 : 1);

	// recurse down the children, front side first
	if (node->children[node->side]->contents == CONTENTS_SOLID) goto rrwnnofront;
	if (node->children[node->side]->visframe != d3d_RenderDef.visframecount) goto rrwnnofront;
	if (R_CullBox (node->children[node->side])) goto rrwnnofront;

	// check for a leaf
	if (node->children[node->side]->contents < 0)
	{
		R_MarkLeafSurfs ((mleaf_t *) node->children[node->side]);
		R_StoreEfrags (&((mleaf_t *) node->children[node->side])->efrags);
		goto rrwnnofront;
	}

	// now we can recurse
	R_RecursiveWorldNode (node->children[node->side]);

rrwnnofront:;
	if (node->numsurfaces)
	{
		msurface_t *surf = cl.worldmodel->brushhdr->surfaces + node->firstsurface;
		int sidebit = (node->dot >= 0 ? 0 : SURF_PLANEBACK);

		// add stuff to the draw lists
		for (int c = node->numsurfaces; c; c--, surf++)
		{
			// the SURF_PLANEBACK test never actually evaluates to true with GLQuake as the surf
			// will have the same plane and facing as the node here.  oh well...
			if (surf->visframe != d3d_RenderDef.framecount) continue;
			if ((surf->flags & SURF_PLANEBACK) != sidebit) continue;

			// only check for culling if both the leaf and the node containing this surf intersect the frustum
			if (surf->intersect && (node->bops == FULLY_INTERSECT_FRUSTUM))
				if (R_CullBox (surf->mins, surf->maxs)) continue;

			// check for lightmap modifications
			if (surf->d3d_Lightmap) surf->d3d_Lightmap->CheckSurfaceForModification (surf);

			texture_t *tex = R_TextureAnimation (&d3d_RenderDef.worldentity, surf->texinfo->texture);

			// this only ever comes from the world so entity is always NULL
			// also we never have explicit alpha
			D3D_AllocModelSurf (surf, tex, NULL);

			// in case a bad BSP overlaps the surfs in it's nodes
			surf->visframe = -1;
		}
	}

	// recurse down the back side
	// the compiler should be performing this optimization anyway
	node = node->children[!node->side];

	// check the back side
	if (node->contents == CONTENTS_SOLID) return;
	if (node->visframe != d3d_RenderDef.visframecount) return;
	if (R_CullBox (node)) return;

	// if a leaf node, draw stuff
	if (node->contents < 0)
	{
		R_MarkLeafSurfs ((mleaf_t *) node);
		R_StoreEfrags (& ((mleaf_t *) node)->efrags);
		return;
	}

	goto loc0;
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
		R_MarkLeafSurfs (leaf);

		// add static entities contained in the leaf to the list
		R_StoreEfrags (&leaf->efrags);
	}

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
		switch (node->plane->type)
		{
		case PLANE_X:
			node->dot = d3d_RenderDef.worldentity.modelorg[0] - node->plane->dist;
			break;
		case PLANE_Y:
			node->dot = d3d_RenderDef.worldentity.modelorg[1] - node->plane->dist;
			break;
		case PLANE_Z:
			node->dot = d3d_RenderDef.worldentity.modelorg[2] - node->plane->dist;
			break;
		default:
			node->dot = DotProduct (d3d_RenderDef.worldentity.modelorg, node->plane->normal) - node->plane->dist;
			break;
		}

		// find which side we're on
		node->side = (node->dot >= 0 ? 0 : 1);

		int sidebit = (node->dot >= 0 ? 0 : SURF_PLANEBACK);

		for (int j = 0; j < node->numsurfaces; j++)
		{
			msurface_t *surf = cl.worldmodel->brushhdr->surfaces + node->firstsurface + j;

			// surf culling
			if (surf->visframe != d3d_RenderDef.framecount) continue;
			if (surf->flags & SURF_DRAWSKY) continue;
			if (surf->mins[2] > (r_refdef.vieworg[2] + r_automap_nearclip.integer + r_automap_z)) continue;
			if ((surf->flags & SURF_PLANEBACK) != sidebit) continue;

			// check for lightmap modifications
			if (surf->d3d_Lightmap) surf->d3d_Lightmap->CheckSurfaceForModification (surf);

			texture_t *tex = R_TextureAnimation (&d3d_RenderDef.worldentity, surf->texinfo->texture);

			// this only ever comes from the world so matrix is always NULL
			// also we never have explicit alpha
			D3D_AllocModelSurf (surf, tex, NULL);
		}
	}
}


void R_BeginSurfaces (model_t *mod)
{
	// clear texture chains
	for (int i = 0; i < mod->brushhdr->numtextures; i++)
	{
		texture_t *tex = mod->brushhdr->textures[i];

		if (!tex) continue;

		// these are only used for lightmap building???
		tex->texturechain = NULL;
	}

	// no modelsurfs yet
	d3d_NumModelSurfs = 0;
}


void D3D_SetupBrushModel (entity_t *ent)
{
	vec3_t mins, maxs;
	model_t *mod = ent->model;

	if (ent->angles[0] || ent->angles[1] || ent->angles[2])
	{
		for (int i = 0; i < 3; i++)
		{
			mins[i] = ent->origin[i] - mod->radius;
			maxs[i] = ent->origin[i] + mod->radius;
		}
	}
	else
	{
		VectorAdd (ent->origin, mod->mins, mins);
		VectorAdd (ent->origin, mod->maxs, maxs);
	}

	// static entities already have the leafs they are in bbox culled
	if (R_CullBox (mins, maxs))
	{
		// mark as not visible
		// also mark as relinked so that it will recache
		ent->visframe = -1;
		ent->brushstate.bmrelinked = true;
		return;
	}

	// visible this frame now
	ent->visframe = d3d_RenderDef.framecount;

	// flag the model as having been previously seen
	ent->model->wasseen = true;

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
	/*
	if (ent->angles[0] || ent->angles[1] || ent->angles[2])
	{
		vec3_t temp;
		vec3_t forward, right, up;

		// i fixed this bug for matrix transforms but of course it came back here... sigh...
		VectorCopy (ent->modelorg, temp);

		// negate angles[0] here too???
		ent->angles[0] = -ent->angles[0];	// stupid quake bug
		AngleVectors (ent->angles, forward, right, up);
		ent->angles[0] = -ent->angles[0];	// stupid quake bug

		ent->modelorg[0] = DotProduct (temp, forward);
		ent->modelorg[1] = -DotProduct (temp, right);
		ent->modelorg[2] = DotProduct (temp, up);
	}
	*/

	if (mod->name[0] == '*')
	{
		// calculate dynamic lighting for the inline bmodel
		R_PushDlights (mod->brushhdr->nodes + mod->brushhdr->hulls[0].firstclipnode);

		// check has the model moved
		if (ent->brushstate.bmrelinked)
		{
			ent->brushstate.bmmoved = true;
			ent->brushstate.bmrelinked = false;
			VectorCopy2 (ent->brushstate.bmoldorigin, ent->origin);
			VectorCopy2 (ent->brushstate.bmoldangles, ent->angles);

			// if the bm needs to be recached it needs to be retransformed too even if it is not rotated or
			// translated as the previously cached version may have been previously transformed
			ent->translated = ent->rotated = true;
		}
		else
		{
			// initially assume that it hasn't moved
			ent->brushstate.bmmoved = false;
			ent->translated = ent->rotated = false;

			// check each of origin and angles so that we know how to transform it
			if (!VectorCompare (ent->origin, ent->brushstate.bmoldorigin))
			{
				ent->brushstate.bmmoved = true;
				ent->translated = true;
				VectorCopy2 (ent->brushstate.bmoldorigin, ent->origin);
			}

			if (!VectorCompare (ent->angles, ent->brushstate.bmoldangles))
			{
				ent->brushstate.bmmoved = true;
				ent->rotated = true;
				VectorCopy2 (ent->brushstate.bmoldangles, ent->angles);
			}
		}
	}
	else
	{
		// instanced models never use the cache because vbframe and iboffset are stored in the surf,
		// which may be shared by multiple entities.  we really should store this in the entity perhaps?
		// (we can't because iboffset is different for each surf)
		ent->brushstate.bmmoved = true;

		// always fully transform instanced models as the previous instance may not have been
		ent->translated = ent->rotated = true;
	}

	if (ent->translated || ent->rotated)
	{
		// store transform for model - we need to run this in software as we are potentially submitting
		// multiple brush models in a single batch, all of which will be merged with the world render.
		D3DMatrix_Identity (&ent->matrix);
		ent->angles[0] = -ent->angles[0];	// stupid quake bug
		D3D_RotateForEntity (ent, &ent->matrix);
		ent->angles[0] = -ent->angles[0];	// stupid quake bug
	}

	msurface_t *surf = mod->brushhdr->surfaces + mod->brushhdr->firstmodelsurface;

	// don't bother with ordering these for now; we'll sort them by texture later
	for (int s = 0; s < mod->brushhdr->nummodelsurfaces; s++, surf++)
	{
		// this bollocking thing is broken AGAIN on me...
		//float dot = DotProduct (ent->modelorg, surf->plane->normal) - surf->plane->dist;

		//if (((surf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) ||
		//	(!(surf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON)))
		{
		// don't restrict this to inlines as the map might need to be updated on a vid_restart or similar
		// this check will also catch sky and water surfs
		if (surf->d3d_Lightmap) surf->d3d_Lightmap->CheckSurfaceForModification (surf);

		// add to the modelsurfs list
		D3D_AllocModelSurf (surf, R_TextureAnimation (ent, surf->texinfo->texture), ent, ent->alphaval);
		}
	}
}


void D3D_BuildWorld (void)
{
	// moved up because the node array builder uses it too and that gets called from r_markleaves
	VectorCopy (r_refdef.vieworg, d3d_RenderDef.worldentity.modelorg);

	// mark visible leafs
	R_MarkLeaves ();

	R_BeginSurfaces (cl.worldmodel);

	// calculate dynamic lighting for the world
	R_PushDlights (cl.worldmodel->brushhdr->nodes);

	// assume that the world is always visible ;)
	// (although it might not be depending on the scene...)
	d3d_RenderDef.worldentity.visframe = d3d_RenderDef.framecount;

	numrrwn = 0;

	// the automap has a different viewpoint so R_RecursiveWorldNode is not valid for it
	if (d3d_RenderDef.automap)
		R_AutomapSurfaces ();
	else R_RecursiveWorldNode (cl.worldmodel->brushhdr->nodes);

	// models are done after the world so that the sort will put them after it too during the render
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

	// create our thread objects if we need to
	if (!hSurfSortMutex) hSurfSortMutex = CreateMutex (NULL, TRUE, NULL);
	if (!hSurfSortThread) hSurfSortThread = CreateThread (NULL, 0x10000, D3DSurf_SortProc, NULL, 0, NULL);

	if (!hSurfSortMutex) Sys_Error ("Failed to create Mutex object");
	if (!hSurfSortThread) Sys_Error ("Failed to create Thread object");

	// now we're ready to sort so let the sorting thread take over while we do some other work here
	ReleaseMutex (hSurfSortMutex);

	// upload any lightmaps that were modified
	// done as early as possible for best parallelism
	D3D_UploadLightmaps ();

	// finish up any sky surfs that were drawn
	D3DSky_FinalizeSky ();

	// add particles here because it's useful work to be doing while waiting on the sort
	D3D_AddParticesToAlphaList ();

	// ensure that the sort has completed and grab the sorting mutex again
	WaitForSingleObject (hSurfSortMutex, INFINITE);

	// finish solid surfaces by adding any such to the solid buffer
	D3D_AddSurfacesToRender ();

	// draw opaque water surfaces here; this includes bmodels and the world merged together;
	// translucent water surfaces are drawn separately during the alpha surfaces pass
	D3DWarp_DrawWaterSurfaces (d3d_ModelSurfs, d3d_NumModelSurfs);
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


extern cvar_t r_lavaalpha;
extern cvar_t r_telealpha;
extern cvar_t r_slimealpha;
extern cvar_t r_lockalpha;

bool R_NearWaterTest (void)
{
	msurface_t **mark = d3d_RenderDef.viewleaf->firstmarksurface;
	int c = d3d_RenderDef.viewleaf->nummarksurfaces;

	if (c)
	{
		do
		{
			if (!r_lockalpha.integer)
			{
				if ((mark[0]->flags & SURF_DRAWLAVA) && (r_lavaalpha.value > 0.99f)) return true;
				if ((mark[0]->flags & SURF_DRAWSLIME) && (r_slimealpha.value > 0.99f)) return true;
				if ((mark[0]->flags & SURF_DRAWTELE) && (r_telealpha.value > 0.99f)) return true;
				if ((mark[0]->flags & SURF_DRAWWATER) && (r_wateralpha.value > 0.99f)) return true;
			}
			else
			{
				// any type and use (r_wateralpha globally
				if ((mark[0]->flags & SURF_DRAWTURB) && (r_wateralpha.value > 0.99f)) return true;
			}

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


// just in case these get fucked...
void R_FixupBModelBBoxes (void)
{
	// note - the player is model 0 in the precache list;
	// the world is model 1
	for (int j = 1; j < MAX_MODELS; j++)
	{
		model_t *mod;

		// note - we set the end of the precache list to NULL in cl_parse to ensure this test is valid
		if (!(mod = cl.model_precache[j])) break;
		if (mod->type != mod_brush) continue;

		brushhdr_t *hdr = mod->brushhdr;
		msurface_t *surf = hdr->surfaces + hdr->firstmodelsurface;

		float mins[3] = {99999999, 99999999, 99999999};
		float maxs[3] = { -99999999, -99999999, -99999999};

		for (int s = 0; s < hdr->nummodelsurfaces; s++, surf++)
		{
			for (int i = 0; i < 3; i++)
			{
				if (surf->mins[i] < mins[i]) mins[i] = surf->mins[i];
				if (surf->maxs[i] > maxs[i]) maxs[i] = surf->maxs[i];
			}
		}

		j = j;

		for (int i = 0; i < 3; i++)
		{
			if (mins[i] < mod->mins[i]) mod->mins[i] = mins[i];
			if (maxs[i] > mod->maxs[i]) mod->maxs[i] = maxs[i];
		}
	}
}

