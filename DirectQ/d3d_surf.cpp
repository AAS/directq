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

void D3DLight_CheckSurfaceForModification (msurface_t *surf);
void D3DLight_UpdateLightmaps (void);

void R_PushDlights (mnode_t *headnode);
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
	if (ent)
	{
		// copy out the midpoint for matrix transforms
		float midpoint[3] =
		{
			surf->midpoint[0],
			surf->midpoint[1],
			surf->midpoint[2]
		};

		// transform the surface midpoint by the modelsurf matrix so that it goes into the proper place
		D3DMatrix_TransformPoint (&ent->matrix, midpoint, surf->midpoint);

		// now add it
		D3DAlpha_AddToList (surf, ent);

		// restore the original midpoint
		surf->midpoint[0] = midpoint[0];
		surf->midpoint[1] = midpoint[1];
		surf->midpoint[2] = midpoint[2];
	}
	else
	{
		// just add it
		D3DAlpha_AddToList (surf, ent);
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

		// animate only one texture at a time
		texture_t *anim = R_TextureAnimation (ent, tex);

		if (anim->lumaimage && !fullbright) continue;
		if (!anim->lumaimage && fullbright) continue;

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
				D3DBrush_EmitSurface (surf, anim, NULL, 255);

			D3DBrush_FlushSurfaces ();
			d3d_SurfLightmapChains[j] = NULL;
		}

		tex->texturechain = NULL;
	}
}


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


void D3DSurf_ChainSurface (msurface_t *surf)
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
		surf->texturechain = surf->texinfo->texture->texturechain;
		surf->texinfo->texture->texturechain = surf;
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
			// clipflags is a 4 bit mask, for each bit 0 = auto accept on this side, 1 = need to test on this side
			// BOPS returns 0 (intersect), 1 (inside), or 2 (outside).  if a node is inside on any side then all
			// of it's child nodes are also guaranteed to be inside on the same side (ditto outside, rejected by return)
			if (!(clipflags & (1 << c))) continue;
			if ((side = BOX_ON_PLANE_SIDE (node->mins, node->maxs, &frustum[c])) == 2) return;
			if (side == 1) clipflags &= ~(1 << c);
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

		// add stuff to the draw lists
		for (int c = node->numsurfaces; c; c--, surf++)
		{
			// the SURF_PLANEBACK test never actually evaluates to true with GLQuake as the surf
			// will have the same plane and facing as the node here.  oh well...
			if (surf->visframe != d3d_RenderDef.framecount) continue;
			if ((surf->flags & SURF_PLANEBACK) != sidebit) continue;

			// only check for culling if both the node and leaf containing this surf intersect the frustum
			if (clipflags && surf->clipflags)
				if (R_CullBox (surf->mins, surf->maxs, frustum)) continue;

			// fixme - cache this dist for each plane (is this the same as the node's plane???)
			float dot = R_PlaneDist (surf->plane, &d3d_RenderDef.worldentity);

			if (dot > r_farclip) r_farclip = dot;
			if (-dot > r_farclip) r_farclip = -dot;

			// add to texture chains
			D3DSurf_ChainSurface (surf);

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

	// calculate dynamic lighting for the inline bmodel
	if (mod->name[0] == '*')
		R_PushDlights (mod->brushhdr->nodes + mod->brushhdr->hulls[0].firstclipnode);

	ent->angles[0] = -ent->angles[0];	// stupid quake bug

	D3DMatrix_Identity (&ent->matrix);
	D3D_RotateForEntity (ent, &ent->matrix, ent->origin, ent->angles);

	ent->angles[0] = -ent->angles[0];	// stupid quake bug

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
			if (ent->alphaval > 0 && ent->alphaval < 255)
				D3DSurf_EmitSurfToAlpha (surf, ent);
			else
			{
				// regular surface
				D3DSurf_ChainSurface (surf);
				numsurfaces++;
			}
		}
	}

	// and now multiply it out
	D3DMatrix_Multiply (&ent->matrix, &d3d_ModelViewProjMatrix);

	if (numsurfaces)
	{
		// only if any surfs came through
		D3DHLSL_SetWorldMatrix (&ent->matrix);
		D3DSurf_DrawTextureChains (mod, mod->brushhdr, ent);
	}
}


void D3D_SetupProjection (float fovx, float fovy, float zn, float zf);
void R_ModParanoia (entity_t *ent);
void D3DAlias_AddModelToList (entity_t *ent);

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

		// now we check for transforms
		if (ent->angles[0]) continue;
		if (ent->angles[1]) continue;
		if (ent->angles[2]) continue;

		if (ent->origin[0]) continue;
		if (ent->origin[1]) continue;
		if (ent->origin[2]) continue;

		// add entities to the draw lists
		if (!r_drawentities.integer) continue;
		if (ent->alphaval > 0 && ent->alphaval < 255) continue;

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

		R_PushDlights (mod->brushhdr->nodes + mod->brushhdr->hulls[0].firstclipnode);

		msurface_t *surf = mod->brushhdr->surfaces + mod->brushhdr->firstmodelsurface;

		// ensure that there is a valid matrix in the entity for alpha sorting/distance
		D3DMatrix_Identity (&ent->matrix);

		// don't bother with ordering these for now; we'll sort them by texture later
		for (int s = 0; s < mod->brushhdr->nummodelsurfaces; s++, surf++)
		{
			// prevent surfs from being added more than once (some custom maps get this)
			if (surf->visframe == d3d_RenderDef.framecount) continue;

			// fixme - do this per node?
			float dot = R_PlaneDist (surf->plane, ent);

			// we already checked for ent->alphaval above so we don't need to here
			if (((surf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) || (!(surf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON)))
				D3DSurf_ChainSurface (surf);

			// prevent surfs from being added more than once (some custom maps get this)
			surf->visframe = d3d_RenderDef.framecount;
		}

		// and now multiply it by the world matrix for the final entity matrix
		D3DMatrix_Multiply (&ent->matrix, &d3d_ModelViewProjMatrix);
	}
}


void D3D_SetupBrushModel (entity_t *ent)
{
}


void D3D_BuildWorld (void)
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

	// calculate dynamic lighting for the world
	R_PushDlights (cl.worldmodel->brushhdr->nodes);

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

			D3DSurf_ChainSurface (surf);
			surf->visframe = d3d_RenderDef.framecount;
		}
	}

	// update r_speeds counters
	d3d_RenderDef.numnode = r_numcachedworldnodes;
	d3d_RenderDef.numleaf = r_numcachedworldleafs;

	// set the final projection matrix that we'll actually use
	D3D_SetupProjection (d3d_RenderDef.fov_x, d3d_RenderDef.fov_y, 4, r_farclip);

	// and re-evaluate our MVP
	D3DMatrix_Multiply (&d3d_ModelViewProjMatrix, &d3d_ViewMatrix, &d3d_WorldMatrix);
	D3DMatrix_Multiply (&d3d_ModelViewProjMatrix, &d3d_ProjMatrix);

	// and send it to the shader; this is actually sent twice now; once after the initial rough estimate
	// and once here.  one of these is strictly speaking unnecessary, but to be honest if we're worried
	// about THAT causing performance problems we've got too much time on our hands.
	D3DHLSL_SetWorldMatrix (&d3d_ModelViewProjMatrix);

	// the world entity needs a matrix set up correctly so that trans surfs will transform correctly
	memcpy (&d3d_RenderDef.worldentity.matrix, &d3d_ModelViewProjMatrix, sizeof (D3DMATRIX));

	// kurok needs alpha testing on all surfs to do leaves/etc (this is ugly)
	if (kurok) D3DState_SetAlphaTest (TRUE, D3DCMP_GREATEREQUAL, (DWORD) 0x000000aa);

	// bmodels which do not move may be merged to the world
	D3D_MergeInlineBModels ();

	// update lightmaps as late as possible before using them
	D3DLight_UpdateLightmaps ();

	D3DBrush_Begin ();
	D3DSurf_DrawTextureChains (cl.worldmodel, cl.worldmodel->brushhdr, &d3d_RenderDef.worldentity);

	// deferred until after the world so that we get statics on the list too
	for (int i = 0; i < d3d_RenderDef.numvisedicts; i++)
	{
		entity_t *ent = d3d_RenderDef.visedicts[i];

		if (!ent->model) continue;

		// this ent had it's model merged to the world
		if (ent->mergeframe == d3d_RenderDef.framecount) continue;

		// add entities to the draw lists
		if (!r_drawentities.integer) continue;

		R_ModParanoia (ent);
		D3DMain_BBoxForEnt (ent);

		// to save on extra passes through the list we setup alias and sprite models here too
		if (ent->model->type == mod_alias)
			D3DAlias_AddModelToList (ent);
		else if (ent->model->type == mod_brush)
			D3DSurf_DrawBrushModel (ent);
		else if (ent->model->type == mod_sprite)
			D3DAlpha_AddToList (ent);
		else Con_Printf ("Unknown model type for entity: %i\n", ent->model->type);
	}

	// reset to the original world matrix
	D3DHLSL_SetWorldMatrix (&d3d_ModelViewProjMatrix);

	if (kurok) D3DState_SetAlphaTest (FALSE);
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


