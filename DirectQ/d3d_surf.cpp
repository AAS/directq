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

cvar_t r_zfix ("r_zfix", 0.0f);

void D3DLight_CheckSurfaceForModification (msurface_t *surf);

void R_PushDlights (entity_t *ent, mnode_t *headnode);
void D3DWarp_DrawWaterSurfaces (brushhdr_t *hdr, msurface_t *chain, entity_t *ent);
void D3DSky_DrawSkySurfaces (msurface_t *chain);

void R_MarkLeaves (void);

extern cvar_t r_lockpvs;
extern cvar_t r_lockfrustum;
float r_farclip = 4096.0f;

msurface_t **r_cachedworldsurfaces = NULL;
int r_numcachedworldsurfaces = 0;

mleaf_t **r_cachedworldleafs = NULL;
int r_numcachedworldleafs = 0;

mnode_t **r_cachedworldnodes = NULL;
int r_numcachedworldnodes = 0;

void D3DSurf_BuildWorldCache (void)
{
	r_cachedworldsurfaces = (msurface_t **) RenderZone->Alloc (cl.worldmodel->brushhdr->numsurfaces * sizeof (msurface_t *));
	r_numcachedworldsurfaces = 0;

	r_cachedworldleafs = (mleaf_t **) RenderZone->Alloc ((cl.worldmodel->brushhdr->numleafs + 1) * sizeof (mleaf_t *));
	r_numcachedworldleafs = 0;

	// not doing anything with this yet...
	//r_cachedworldnodes = (mnode_t **) RenderZone->Alloc (cl.worldmodel->brushhdr->numnodes * sizeof (mnode_t *));
	//r_numcachedworldnodes = 0;

	// always rebuild the world
	d3d_RenderDef.rebuildworld = true;
}


/*
=============================================================

	ALL BRUSH MODELS

=============================================================
*/

void D3DSurf_EmitSurfToAlpha (msurface_t *surf, entity_t *ent)
{
	// check the surface for lightmap modification and also ensures that the correct lightmap tex is set
	D3DLight_CheckSurfaceForModification (surf);

	if (ent)
	{
		// copy out the midpoint for matrix transforms
		float midpoint[3];

		// transform the surface midpoint by the modelsurf matrix so that it goes into the proper place
		// (we kept the entity matrix in local space so that we can do this correctly)
		ent->matrix.TransformPoint (surf->midpoint, midpoint);

		// now add it
		D3DAlpha_AddToList (surf, ent, midpoint);
	}
	else
	{
		// just add it
		D3DAlpha_AddToList (surf, ent, surf->midpoint);
	}
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

	if (ent && ent->frame)
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


msurface_t *d3d_SurfSkyChain = NULL;
msurface_t *d3d_SurfWaterChain = NULL;
msurface_t *d3d_SurfLightmapChains[MAX_LIGHTMAPS] = {NULL};

void D3DMain_BBoxForEnt (entity_t *ent);

void D3DSurf_ClearTextureChains (model_t *mod, brushhdr_t *hdr)
{
	for (int i = 0; i < hdr->numtextures; i++)
	{
		if (!hdr->textures[i]) continue;

		hdr->textures[i]->texturechain = NULL;
	}

	d3d_SurfSkyChain = NULL;
	d3d_SurfWaterChain = NULL;
}


void D3DSurf_DrawSolidSurfaces (model_t *mod, brushhdr_t *hdr, entity_t *ent, bool fullbright)
{
	texture_t *tex;
	msurface_t *surf;

	for (int i = 0; i < hdr->numtextures; i++)
	{
		if (!(tex = hdr->textures[i])) continue;
		if (!(surf = tex->texturechain)) continue;

		if (surf->flags & SURF_DRAWSKY) continue;
		if (surf->flags & SURF_DRAWTURB) continue;

		if (tex->lumaimage && !fullbright) continue;
		if (!tex->lumaimage && fullbright) continue;

		// reorder by lightmap for cache coherence
		for (; surf; surf = surf->texturechain)
		{
			surf->lightmapchain = d3d_SurfLightmapChains[surf->LightmapTextureNum];
			d3d_SurfLightmapChains[surf->LightmapTextureNum] = surf;
		}

		// and draw in lightmap order
		// this also inverts the draw order from b2f to f2b (reasonably enough)
		for (int j = 0; j < MAX_LIGHTMAPS; j++)
		{
			// no entity used as this entity has already been transformed
			for (surf = d3d_SurfLightmapChains[j]; surf; surf = surf->lightmapchain)
				D3DBrush_EmitSurface (surf, tex, NULL, 255);

			D3DBrush_FlushSurfaces ();
			d3d_SurfLightmapChains[j] = NULL;
		}

		tex->texturechain = NULL;
	}
}


void D3DLight_UnlockLightmaps (void);

void D3DSurf_DrawTextureChains (model_t *mod, brushhdr_t *hdr, entity_t *ent)
{
	// invalidate the state so we don't have anything hanging over from last time
	D3DBrush_Begin ();

	// we assume that solid surfaces will always be present
	D3DSurf_DrawSolidSurfaces (mod, hdr, ent, false);
	D3DSurf_DrawSolidSurfaces (mod, hdr, ent, true);

	// only opaque water is actually drawn here but this adds alpha surfaces to the alpha list
	if (d3d_SurfWaterChain)
	{
		D3DWarp_DrawWaterSurfaces (hdr, d3d_SurfWaterChain, ent);
		d3d_SurfWaterChain = NULL;
	}

	// do sky last so that we can get early z rejection from it
	if (d3d_SurfSkyChain)
	{
		D3DSky_DrawSkySurfaces (d3d_SurfSkyChain);
		d3d_SurfSkyChain = NULL;
	}
}


void D3DSurf_ChainSurface (msurface_t *surf, entity_t *ent)
{
	if (surf->flags & SURF_DRAWSKY)
	{
		surf->texturechain = d3d_SurfSkyChain;
		d3d_SurfSkyChain = surf;
	}
	else if (surf->flags & SURF_DRAWTURB)
	{
		surf->lightmapchain = d3d_SurfWaterChain;
		d3d_SurfWaterChain = surf;
	}
	else
	{
		texture_t *tex = R_TextureAnimation (ent, surf->texinfo->texture);

		// check the surface for lightmap modification and also ensures that the correct lightmap tex is set
		D3DLight_CheckSurfaceForModification (surf);

		surf->texturechain = tex->texturechain;
		tex->texturechain = surf;
	}
}


__inline float R_PlaneDist (mplane_t *plane, entity_t *ent)
{
	// for axial planes it's quicker to just eval the dist and there's no need to cache;
	// for non-axial we check the cache and use if it current, otherwise calc it and cache it
	if (plane->type < 3)
		return ent->modelorg[plane->type] - plane->dist;
	else if (plane->cacheframe == d3d_RenderDef.framecount && plane->cacheent == ent)
	{
		// Con_Printf ("Cached planedist\n");
		return plane->cachedist;
	}
	else plane->cachedist = DotProduct (ent->modelorg, plane->normal) - plane->dist;

	// cache the result for this plane
	plane->cacheframe = d3d_RenderDef.framecount;
	plane->cacheent = ent;

	// and return what we got
	return plane->cachedist;
}


void R_StoreEfrags (struct efrag_s **ppefrag);

// this now only builds a cache of surfs and leafs for drawing from; the real draw lists are built separately
void R_RecursiveWorldNode (mnode_t *node, int clipflags)
{
	// the compiler should optimize this automatically anyway
tail_recurse:;
	if (node->contents == CONTENTS_SOLID) return;
	if (node->visframe != d3d_RenderDef.visframecount) return;

	if (clipflags)
	{
		for (int c = 0, side = 0; c < 4; c++)
		{
			// a parent is already fully inside the frustum on this side
			if (!(clipflags & (1 << c))) continue;

			// if the box is fully outside on any side then it is fully outside on all sides and there is no need to process children
			if ((side = SphereOnPlaneSide (node->sphere, node->sphere[3], &frustum[c])) == BOX_OUTSIDE_PLANE) return;

			// if the box is fully inside on this side then there is no need to test children on this side (it may be outside on another side)
			if (side == BOX_INSIDE_PLANE) clipflags &= ~(1 << c);
		}
	}

	// if it's a leaf node draw stuff
	if (node->contents < 0)
	{
		// node is a leaf so add stuff for drawing
		mleaf_t *leaf = (mleaf_t *) node;
		msurface_t **mark = leaf->firstmarksurface;
		int c = leaf->nummarksurfaces;

		if (c)
		{
			do
			{
				(*mark)->visframe = d3d_RenderDef.framecount;
				(*mark)->clipflags = clipflags;
				mark++;
			} while (--c);
		}

		if (leaf->efrags)
		{
			// cache this leaf for reuse (only needed for efrags)
			R_StoreEfrags (&leaf->efrags);
			r_cachedworldleafs[r_numcachedworldleafs] = leaf;
			r_numcachedworldleafs++;
		}

		d3d_RenderDef.numleaf++;
		return;
	}

	// node is just a decision point, so go down the appropriate sides
	node->dot = R_PlaneDist (node->plane, &d3d_RenderDef.worldentity);
	node->side = (node->dot >= 0 ? 0 : 1);

	// recurse down the children, front side first
	R_RecursiveWorldNode (node->children[node->side], clipflags);

	// draw stuff
	if (node->numsurfaces)
	{
		msurface_t *surf = node->surfaces;
		int sidebit = (node->dot >= 0 ? 0 : SURF_PLANEBACK);
		int nodesurfs = 0;
		float dot;

		// add stuff to the draw lists
		for (int c = node->numsurfaces; c; c--, surf++)
		{
			// the SURF_PLANEBACK test never actually evaluates to true with GLQuake as the surf
			// will have the same plane and facing as the node here.  oh well...
			if (surf->visframe != d3d_RenderDef.framecount) continue;
			if ((surf->flags & SURF_PLANEBACK) != sidebit) continue;

			// only check for culling if both the node and leaf containing this surf intersect the frustum
			if (clipflags && surf->clipflags)
				if (R_CullSphere (surf->sphere, surf->sphere[3], surf->clipflags)) continue;

			// fixme - cache this dist for each plane (is this the same as the node's plane???)
			dot = (surf->plane == node->plane) ? node->dot : R_PlaneDist (surf->plane, &d3d_RenderDef.worldentity);

			if (dot > r_farclip) r_farclip = dot;
			if (-dot > r_farclip) r_farclip = -dot;

			// add to texture chains
			D3DSurf_ChainSurface (surf, NULL);

			// cache this surface for reuse
			r_cachedworldsurfaces[r_numcachedworldsurfaces] = surf;
			r_numcachedworldsurfaces++;

			nodesurfs++;
		}

		if (nodesurfs)
		{
			// Con_Printf ("node with %i surfaces\n", node->numsurfaces);
			// Con_Printf ("node volume is %f\n", node->volume);
			d3d_RenderDef.numnode++;
		}
	}

	// recurse down the back side (the compiler should optimize this tail recursion)
	node = node->children[!node->side];
	goto tail_recurse;
	//R_RecursiveWorldNode (node->children[!node->side], clipflags);
}


void D3DSurf_DrawBrushModel (entity_t *ent)
{
	model_t *mod = ent->model;

	// catch null models
	if (!mod->brushhdr) return;
	if (!mod->brushhdr->numsurfaces) return;

	// static entities already have the leafs they are in bbox culled
	if (R_CullBox (ent->mins, ent->maxs, frustum))
	{
		// mark as not visible
		ent->visframe = -1;
		return;
	}

	// visible this frame now
	ent->visframe = d3d_RenderDef.framecount;

	// get origin vector relative to viewer
	// this is now stored in the entity so we can read it back any time we want
	VectorSubtract (r_refdef.vieworigin, ent->origin, ent->modelorg);

	// adjust for rotation
	if (ent->angles[0] || ent->angles[1] || ent->angles[2])
	{
		vec3_t temp;
		avectors_t av;

		VectorCopy (ent->modelorg, temp);
		AngleVectors (ent->angles, &av);

		ent->modelorg[0] = DotProduct (temp, av.forward);
		ent->modelorg[1] = -DotProduct (temp, av.right);
		ent->modelorg[2] = DotProduct (temp, av.up);
	}

	ent->matrix.LoadIdentity ();
	ent->matrix.Translate (ent->origin);

	if (ent->angles[1]) ent->matrix.Rotate (0, 0, 1, ent->angles[1]);
	if (ent->angles[0]) ent->matrix.Rotate (0, 1, 0, ent->angles[0]);	// stupid quake bug
	if (ent->angles[2]) ent->matrix.Rotate (1, 0, 0, ent->angles[2]);

	// calculate dynamic lighting for the bmodel (we're lighting instanced as well now)
	// this is done after the matrix is calced so that we can have the proper transform for lighting
	R_PushDlights (ent, mod->brushhdr->nodes + mod->brushhdr->hulls[0].firstclipnode);

	int numsurfaces = 0;
	msurface_t *surf = mod->brushhdr->surfaces + mod->brushhdr->firstmodelsurface;

	D3DSurf_ClearTextureChains (mod, mod->brushhdr);

	// don't bother with ordering these for now; we'll sort them by texture later
	for (int s = 0; s < mod->brushhdr->nummodelsurfaces; s++, surf++)
	{
		// fixme - do this per node?
		float dot = R_PlaneDist (surf->plane, ent);

		if (((surf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) || (!(surf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON)))
		{
			if (surf->flags & SURF_DRAWFENCE)
				D3DSurf_EmitSurfToAlpha (surf, ent);
			else if (ent->alphaval > 0 && ent->alphaval < 255)
				D3DSurf_EmitSurfToAlpha (surf, ent);
			else
			{
				// regular surface
				D3DSurf_ChainSurface (surf, ent);
				numsurfaces++;
			}
		}
	}

	// update modified lightmaps always (in case a surf that was emitted to alpha modifies a lightmap but is not drawn here)
	// this needs to be done per model so that dynamic light on instanced bmodels will be handled properly
	D3DLight_UnlockLightmaps ();

	if (numsurfaces)
	{
		D3DHLSL_UpdateWorldMatrix (&d3d_ModelViewProjMatrix, &ent->matrix);
		D3DSurf_DrawTextureChains (mod, mod->brushhdr, ent);
	}
}


void D3D_SetupProjection (float fovx, float fovy, float zn, float zf);
void R_ModParanoia (entity_t *ent);

void D3D_MergeInlineBModels (void)
{
	for (int i = 0; i < d3d_RenderDef.numvisedicts; i++)
	{
		entity_t *ent = d3d_RenderDef.visedicts[i];
		model_t *mod = ent->model;

		if (!mod) continue;
		if (mod->type != mod_brush) continue;

		// this is only valid for inline bmodels as instanced models may have textures which are not in the world
		if (mod->name[0] != '*') continue;

		// catch null models
		if (!mod->brushhdr) continue;
		if (!mod->brushhdr->numsurfaces) continue;

		// now we check for transforms
		if (ent->angles[0]) continue;
		if (ent->angles[1]) continue;
		if (ent->angles[2]) continue;

		if (ent->origin[0]) continue;
		if (ent->origin[1]) continue;
		if (ent->origin[2]) continue;

		// add entities to the draw lists
		if (!r_drawentities.integer) continue;

		// OK, we have a bmodel that doesn't move so merge it to the world list
		R_ModParanoia (ent);
		D3DMain_BBoxForEnt (ent);
		ent->mergeframe = d3d_RenderDef.framecount;

		// static entities already have the leafs they are in bbox culled
		if (R_CullBox (ent->mins, ent->maxs, frustum))
		{
			// mark as not visible
			ent->visframe = -1;
			continue;
		}

		// now do the merging
		ent->visframe = d3d_RenderDef.framecount;

		// get origin vector relative to viewer
		// this is now stored in the entity so we can read it back any time we want
		VectorSubtract (r_refdef.vieworigin, ent->origin, ent->modelorg);

		msurface_t *surf = mod->brushhdr->surfaces + mod->brushhdr->firstmodelsurface;

		// ensure that there is a valid matrix in the entity for alpha sorting/distance
		// this model hasn't moved at all so it's local space matrix is identity
		ent->matrix.LoadIdentity ();

		// calculate dynamic lighting for the inline bmodel
		// this is done after the matrix is calced so that we can have the proper transform for lighting
		R_PushDlights (NULL, mod->brushhdr->nodes + mod->brushhdr->hulls[0].firstclipnode);

		// don't bother with ordering these for now; we'll sort them by texture later
		for (int s = 0; s < mod->brushhdr->nummodelsurfaces; s++, surf++)
		{
			// prevent surfs from being added more than once (some custom maps get this)
			if (surf->visframe == d3d_RenderDef.framecount) continue;

			// fixme - do this per node?
			float dot = R_PlaneDist (surf->plane, ent);

			// we already checked for ent->alphaval above so we don't need to here
			if (((surf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) || (!(surf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON)))
			{
				if (surf->flags & SURF_DRAWFENCE)
					D3DSurf_EmitSurfToAlpha (surf, ent);
				else if (ent->alphaval > 0 && ent->alphaval < 255)
					D3DSurf_EmitSurfToAlpha (surf, ent);
				else D3DSurf_ChainSurface (surf, ent);
			}

			// prevent surfs from being added more than once (some custom maps get this)
			surf->visframe = d3d_RenderDef.framecount;
		}
	}
}


void D3DSurf_DrawWorld (void)
{
	// Con_Printf ("\n");

	// moved up because the node array builder uses it too and that gets called from r_markleaves
	VectorCopy (r_refdef.vieworigin, d3d_RenderDef.worldentity.modelorg);

//	Con_Printf ("vieworg: %0.2f %0.2f %0.2f\n", r_refdef.vieworigin[0], r_refdef.vieworigin[1], r_refdef.vieworigin[2]);

	// mark visible leafs
	R_MarkLeaves ();

	// assume that the world is always visible ;)
	// (although it might not be depending on the scene...)
	d3d_RenderDef.worldentity.visframe = d3d_RenderDef.framecount;

	// the world entity needs a matrix set up correctly so that trans surfs will transform correctly
	// the world model hasn't moved at all so it's local space matrix is identity
	d3d_RenderDef.worldentity.matrix.LoadIdentity ();

	// calculate dynamic lighting for the world
	// this is done after the matrix is calced so that we can have the proper transform for lighting
	R_PushDlights (NULL, cl.worldmodel->brushhdr->nodes);

	d3d_RenderDef.numnode = 0;
	d3d_RenderDef.numleaf = 0;

	D3DSurf_ClearTextureChains (cl.worldmodel, cl.worldmodel->brushhdr);

	if (d3d_RenderDef.rebuildworld)
	{
		// go to a new cache
		r_numcachedworldsurfaces = 0;
		r_numcachedworldleafs = 0;

		r_farclip = 4096.0f;	// never go below this

		R_RecursiveWorldNode (cl.worldmodel->brushhdr->nodes, 15);
		r_numcachedworldnodes = d3d_RenderDef.numnode;

		// r_farclip so far represents one side of a right-angled triangle with the longest side being what we actually want
		r_farclip = sqrt (r_farclip * r_farclip + r_farclip * r_farclip);

		// mark not to rebuild
		d3d_RenderDef.rebuildworld = false;
	}
	else
	{
		// build the draw lists from an existing cache
		for (int i = 0; i < r_numcachedworldleafs; i++)
			R_StoreEfrags (&r_cachedworldleafs[i]->efrags);

		for (int i = 0; i < r_numcachedworldsurfaces; i++)
		{
			msurface_t *surf = r_cachedworldsurfaces[i];

			D3DSurf_ChainSurface (surf, NULL);
			surf->visframe = d3d_RenderDef.framecount;
		}
	}

	// update r_speeds counters
	d3d_RenderDef.numnode = r_numcachedworldnodes;
	d3d_RenderDef.numleaf = r_numcachedworldleafs;

	// set the final projection matrix that we'll actually use (this is good for most Quake scenes)
	D3D_SetupProjection (d3d_RenderDef.fov_x, d3d_RenderDef.fov_y, 4, r_farclip + 100 * r_zfix.value);

	// and re-evaluate our MVP
	QMATRIX::UpdateMVP (&d3d_ModelViewProjMatrix, &d3d_WorldMatrix, &d3d_ViewMatrix, &d3d_ProjMatrix);

	// and send it to the shader
	D3DHLSL_SetWorldMatrix (&d3d_ModelViewProjMatrix);

	// bmodels which do not move may be merged to the world
	if (!r_zfix.value) D3D_MergeInlineBModels ();

	D3DLight_UnlockLightmaps ();
	D3DSurf_DrawTextureChains (cl.worldmodel, cl.worldmodel->brushhdr, &d3d_RenderDef.worldentity);

	if (r_zfix.value)
	{
		// adjust projection matrix to combat z-fighting (this is good for most Quake scenes)
		D3D_SetupProjection (d3d_RenderDef.fov_x, d3d_RenderDef.fov_y, 4, r_farclip);
		QMATRIX::UpdateMVP (&d3d_ModelViewProjMatrix, &d3d_WorldMatrix, &d3d_ViewMatrix, &d3d_ProjMatrix);
		D3DHLSL_SetWorldMatrix (&d3d_ModelViewProjMatrix);
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
				node = node->parent;
			} while (node);
		}
	}
}


void R_MarkLeaves (void)
{
	// viewleaf hasn't changed or we're drawing with a locked PVS
	if ((d3d_RenderDef.oldviewleaf == d3d_RenderDef.viewleaf) || r_lockpvs.value) return;

	// go to a new visframe
	d3d_RenderDef.visframecount++;

	// rebuild the world lists
	d3d_RenderDef.rebuildworld = true;

	// add in visible leafs - we always add the fat PVS to ensure that client visibility
	// is the same as that which was used by the server; R_CullBox will take care of unwanted leafs
	if (r_novis.integer)
		R_LeafVisibility (NULL);
	else if (d3d_RenderDef.viewleaf->flags & SURF_DRAWTURB)
		R_LeafVisibility (Mod_FatPVS (r_refdef.vieworigin));
	else R_LeafVisibility (Mod_LeafPVS (d3d_RenderDef.viewleaf, cl.worldmodel));

	// no old viewleaf so can't make a transition check
	if (d3d_RenderDef.oldviewleaf)
	{
		// check for a contents transition
		if (d3d_RenderDef.oldviewleaf->contents == d3d_RenderDef.viewleaf->contents)
		{
			// if we're still in the same contents we still have the same contents colour
			d3d_RenderDef.viewleaf->contentscolor = d3d_RenderDef.oldviewleaf->contentscolor;
		}
		else if (!r_novis.integer && ((d3d_RenderDef.viewleaf->flags & SURF_DRAWTURB) || (d3d_RenderDef.oldviewleaf->flags & SURF_DRAWTURB)))
		{
			// we've had a contents transition so merge the old pvs with the new
			R_LeafVisibility (Mod_LeafPVS (d3d_RenderDef.oldviewleaf, cl.worldmodel));
		}
	}

	// we've now completed the PVS change
	switch (d3d_RenderDef.viewleaf->contents)
	{
	case CONTENTS_EMPTY:
	case CONTENTS_WATER:
	case CONTENTS_SLIME:
	case CONTENTS_LAVA:
		d3d_RenderDef.oldviewleaf = d3d_RenderDef.viewleaf;
		break;

	default:
		d3d_RenderDef.oldviewleaf = NULL;
		break;
	}
}


