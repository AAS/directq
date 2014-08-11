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
void D3DSky_AddSurfaceToRender (msurface_t *surf, entity_t *ent);
void D3DWarp_DrawWaterSurfaces (d3d_modelsurf_t **chains, int numchains);

void D3DSky_Clear (void);
void D3DSky_FinalizeSky (void);
void D3DSky_MarkWorld (void);
void D3DSky_SetEntities (void);

void R_MarkLeaves (void);
void D3D_EmitModelSurfToAlpha (d3d_modelsurf_t *modelsurf);
void D3D_AddParticesToAlphaList (void);

extern cvar_t r_lockpvs;
extern cvar_t r_lockfrustum;
float r_farclip = 4096.0f;

cvar_t r_shownodedepth ("r_shownodedepth", "0");
cvar_t r_shownodes ("r_shownodes", "0");
cvar_t r_drawflat ("r_drawflat", "0");

// surface interface
void D3DBrush_Begin (void);
void D3DBrush_End (void);
void D3DBrush_EmitSurface (d3d_modelsurf_t *ms);
void D3DBrush_TransferMainSurfaces (d3d_modelsurf_t **lightchains);
void D3DBrush_DrawVBOSurfaces (void);

void D3DBrush_ShowNodesBegin (void);
void D3DBrush_ShowNodesDrawSurfaces (mnode_t *node, int depth);
void D3DBrush_ShowLeafsDrawSurfaces (mleaf_t *leaf);

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

	r_cachedworldleafs = (mleaf_t **) RenderZone->Alloc (cl.worldmodel->brushhdr->numleafs * sizeof (mleaf_t *));
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

d3d_modelsurf_t **d3d_TextureChains;
int d3d_NumTextureChains = 0;


void D3DSurf_RegisterTextureChain (image_t *image)
{
	image->ChainNumber = d3d_NumTextureChains;
	d3d_NumTextureChains++;
}


void D3DSurf_FinishTextureChains (void)
{
	d3d_TextureChains = (d3d_modelsurf_t **) RenderZone->Alloc (d3d_NumTextureChains * sizeof (d3d_modelsurf_t *));
	// Con_Printf ("registered %i texture chains\n", d3d_NumTextureChains);
}


void D3DSurf_ClearTextureChains (void)
{
	for (int i = 0; i < d3d_NumTextureChains; i++)
		d3d_TextureChains[i] = NULL;
}


// a BSP cannot contain more than 65536 marksurfaces and we add some headroom for instanced models
#define MAX_MODELSURFS 131072

d3d_modelsurf_t **d3d_ModelSurfs = NULL;
int d3d_NumModelSurfs = 0;


void D3D_ModelSurfsBeginMap (void)
{
	d3d_ModelSurfs = (d3d_modelsurf_t **) RenderZone->Alloc (MAX_MODELSURFS * sizeof (d3d_modelsurf_t *));

	// allocate an initial batch
	for (int i = 0; i < 4096; i++)
		d3d_ModelSurfs[i] = (d3d_modelsurf_t *) RenderZone->Alloc (sizeof (d3d_modelsurf_t));

	d3d_NumModelSurfs = 0;
}


void D3D_SetupModelSurf (d3d_modelsurf_t *ms, msurface_t *surf, texture_t *tex, entity_t *ent = NULL, int alpha = 255)
{
	// base textures are commnon to all (liquids won't have lightmaps but they'll be NULL anyway)
	ms->textures[TEXTURE_LIGHTMAP] = surf->d3d_LightmapTex;
	ms->textures[TEXTURE_DIFFUSE] = tex->teximage->d3d_Texture;

	// the luma image also decides the shader pass to use
	// (this assumes that the surf is solid, it will be correctly set for liquid elsewhere)
	if (tex->lumaimage)
	{
		if (gl_fullbrights.integer)
			ms->shaderpass = alpha < 255 ? FX_PASS_WORLD_LUMA_ALPHA : FX_PASS_WORLD_LUMA;
		else ms->shaderpass = alpha < 255 ? FX_PASS_WORLD_LUMA_NOLUMA_ALPHA : FX_PASS_WORLD_LUMA_NOLUMA;

		ms->textures[TEXTURE_LUMA] = tex->lumaimage->d3d_Texture;
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

	// check for lightmap modifications
	D3DLight_CheckSurfaceForModification (ms->surf);

	// chain the modelsurf in it's proper texture chain for proper f2b ordering
	// (this actually gives b2f but the reversal of the chain per-lightmap will return it to f2b)
	ms->next = d3d_TextureChains[tex->teximage->ChainNumber];
	d3d_TextureChains[tex->teximage->ChainNumber] = ms;
}


void D3D_AllocModelSurf (msurface_t *surf, texture_t *tex, entity_t *ent = NULL, int alpha = 255)
{
	if (surf->flags & SURF_DRAWSKY)
	{
		// sky is rendered immediately as it passes
		D3DSky_AddSurfaceToRender (surf, ent);
		return;
	}

	// catch surfaces without textures
	// (done after sky as sky surfaces don't have textures...)
	if (!tex->teximage) return;

	// everything else is batched up for drawing in the main pass
	// ensure that there is space
	if (d3d_NumModelSurfs >= MAX_MODELSURFS) return;

	// allocate as required
	if (!d3d_ModelSurfs[d3d_NumModelSurfs])
		d3d_ModelSurfs[d3d_NumModelSurfs] = (d3d_modelsurf_t *) RenderZone->Alloc (sizeof (d3d_modelsurf_t));

	// take the next modelsurf
	d3d_modelsurf_t *ms = d3d_ModelSurfs[d3d_NumModelSurfs];
	d3d_NumModelSurfs++;

	D3D_SetupModelSurf (ms, surf, tex, ent, alpha);
}


d3d_modelsurf_t *d3d_LightChains[MAX_LIGHTMAPS];

void D3DSurf_DrawSurfaces (void)
{
	for (int i = 0; i < d3d_NumTextureChains; i++)
	{
		if (!d3d_TextureChains[i]) continue;

		for (d3d_modelsurf_t *ms = d3d_TextureChains[i]; ms; ms = ms->next)
		{
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

			// the original texture chains were built from back-to-front with entities at the start of the lists 
			// and this operation reverses them into front-to-back with entities at the end of the list,
			// which is the correct final order for drawing in.
			ms->chain = d3d_LightChains[surf->LightmapTextureNum];
			d3d_LightChains[surf->LightmapTextureNum] = ms;

			// NULL the chain here if it was used for drawing in this pass
			// needs to be done here otherwise we won't know for certain if it actually was used...
			d3d_TextureChains[i] = NULL;
		}
	}

	// and transfer them for drawing with
	D3DBrush_TransferMainSurfaces (d3d_LightChains);
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
			// of it's child nodes are also guaranteed to be inside on the same side
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

		// cache this leaf for reuse (only needed for efrags)
		r_cachedworldleafs[r_numcachedworldleafs] = leaf;
		r_numcachedworldleafs++;

		if (leaf->efrags)
			R_StoreEfrags (&leaf->efrags);

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

			// this only ever comes from the world so entity is always NULL and we never have explicit alpha
			D3D_AllocModelSurf (surf, R_TextureAnimation (&d3d_RenderDef.worldentity, surf->texinfo->texture));

			// cache this surface for reuse
			r_cachedworldsurfaces[r_numcachedworldsurfaces] = surf;
			r_numcachedworldsurfaces++;

			// because multiple surfs can share the same mesh we need to invalidate it's visframe to prevent it
			// from being added multiple times; this marks the surface as being visible in the next frame too
			// which resolves a LOT of problems with crossing water boundaries
			surf->visframe = d3d_RenderDef.framecount + 1;
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


void D3DSurf_RecursiveShowNodes (mnode_t *node, int clipflags, int depth)
{
	if (node->contents == CONTENTS_SOLID) return;
	if (node->visframe != d3d_RenderDef.visframecount) return;

	if (clipflags)
	{
		for (int c = 0, side = 0; c < 4; c++)
		{
			// clipflags is a 4 bit mask, for each bit 0 = auto accept on this side, 1 = need to test on this side
			// BOPS returns 0 (intersect), 1 (inside), or 2 (outside).  if a node is inside on any side then all
			// of it's child nodes are also guaranteed to be inside on the same side
			if (!(clipflags & (1 << c))) continue;
			if ((side = BOX_ON_PLANE_SIDE (node->mins, node->maxs, &frustum[c])) == 2) return;
			if (side == 1) clipflags &= ~(1 << c);
		}
	}

	// if it's a leaf node draw stuff
	if (node->contents < 0)
	{
		// node is a leaf so add stuff for drawing
		msurface_t **mark = ((mleaf_t *) node)->firstmarksurface;
		int c = ((mleaf_t *) node)->nummarksurfaces;

		if (c)
		{
			do
			{
				(*mark)->visframe = d3d_RenderDef.framecount;
				mark++;
			} while (--c);
		}

		// D3DBrush_ShowLeafsDrawSurfaces ((mleaf_t *) node);
		return;
	}

	// node is just a decision point, so go down the appropriate sides
	node->dot = R_PlaneDist (node->plane, &d3d_RenderDef.worldentity);
	node->side = (node->dot >= 0 ? 0 : 1);

	// recurse down the children, front side first
	D3DSurf_RecursiveShowNodes (node->children[node->side], clipflags, depth + 1);

	// draw stuff
	D3DBrush_ShowNodesDrawSurfaces (node, depth);

	// recurse down the back side (the compiler should optimize this tail recursion)
	D3DSurf_RecursiveShowNodes (node->children[!node->side], clipflags, depth + 1);
}


void D3DSurf_ShowNodes (void)
{
	D3DBrush_ShowNodesBegin ();
	D3D_SetRenderState (D3DRS_ZWRITEENABLE, FALSE);
	D3DSurf_RecursiveShowNodes (cl.worldmodel->brushhdr->nodes, 15, 1);
	D3D_SetRenderState (D3DRS_ZWRITEENABLE, TRUE);
}


void D3D_SetupBrushModel (entity_t *ent)
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

	msurface_t *surf = mod->brushhdr->surfaces + mod->brushhdr->firstmodelsurface;
	entity_t *modent = NULL;

	// we only need to specify an ent if the model needs to be transformed
	if (ent->angles[0] || ent->angles[1] || ent->angles[2]) modent = ent;
	if (ent->origin[0] || ent->origin[1] || ent->origin[2]) modent = ent;

	// always load identity even if we're not transforming it
	D3DMatrix_Identity (&ent->matrix);

	// and we only need to calc it's matrix if transforming too
	if (modent)
	{
		// store transform for model - we need to run this in software as we are potentially submitting
		// multiple brush models in a single batch, all of which will be merged with the world render.
		ent->angles[0] = -ent->angles[0];	// stupid quake bug
		D3D_RotateForEntity (ent, &ent->matrix, ent->origin, ent->angles);
		ent->angles[0] = -ent->angles[0];	// stupid quake bug
	}

	// prevent addition of the same surf for the same entity more than once
	// (and enforce addition of the same surf for multiple entities more than once - instanced bmodels still suck)
	d3d_RenderDef.entframecount++;

	ent->update_type = 0;

	// don't bother with ordering these for now; we'll sort them by texture later
	for (int s = 0; s < mod->brushhdr->nummodelsurfaces; s++, surf++)
	{
		if (surf->visframe == d3d_RenderDef.entframecount) continue;

		float dot = R_PlaneDist (surf->plane, ent);

		// r_farclip should be affected by this too
		if (dot > r_farclip) r_farclip = dot;
		if (-dot > r_farclip) r_farclip = -dot;

		if (((surf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) ||
			(!(surf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON)))
		{
			// add to the modelsurfs list
			// (most surfaces will be deferred to later for drawind in static VBOs)
			if (ent->alphaval < 255)
				D3D_AllocModelSurf (surf, R_TextureAnimation (ent, surf->texinfo->texture), modent, ent->alphaval);
			else if (surf->flags & SURF_DRAWTURB)
				D3D_AllocModelSurf (surf, surf->texinfo->texture, modent, ent->alphaval);
			else if (surf->flags & SURF_DRAWSKY)
				D3D_AllocModelSurf (surf, surf->texinfo->texture, modent, ent->alphaval);
			else if (!mod->brushhdr->NumDrawCalls)
				D3D_AllocModelSurf (surf, R_TextureAnimation (ent, surf->texinfo->texture), modent, ent->alphaval);
			else
			{
				if (mod->name[0] == '*') D3DLight_CheckSurfaceForModification (surf);
				ent->update_type = 1;
			}

			surf->visframe = d3d_RenderDef.entframecount;
		}
	}
}


void D3D_SetupProjection (float fovx, float fovy, float zn, float zf);

void D3D_BuildWorld (void)
{
	// Con_Printf ("\n");

	// moved up because the node array builder uses it too and that gets called from r_markleaves
	VectorCopy (r_refdef.vieworigin, d3d_RenderDef.worldentity.modelorg);

//	Con_Printf ("vieworg: %0.2f %0.2f %0.2f\n", r_refdef.vieworigin[0], r_refdef.vieworigin[1], r_refdef.vieworigin[2]);

	// mark visible leafs
	R_MarkLeaves ();
	D3DSurf_ClearTextureChains ();

	// assume that the world is always visible ;)
	// (although it might not be depending on the scene...)
	d3d_RenderDef.worldentity.visframe = d3d_RenderDef.framecount;

	// calculate dynamic lighting for the world
	R_PushDlights (cl.worldmodel->brushhdr->nodes);

	d3d_NumModelSurfs = 0;

	d3d_RenderDef.numnode = 0;
	d3d_RenderDef.numleaf = 0;

	if (d3d_RenderDef.rebuildworld)
	{
		// go to a new cache
		r_numcachedworldsurfaces = 0;
		r_numcachedworldleafs = 0;
		r_farclip = 4096.0f;	// never go below this
		R_RecursiveWorldNode (cl.worldmodel->brushhdr->nodes, 15);
		d3d_RenderDef.rebuildworld = false;

		// r_farclip so far represents one side of a right-angled triangle with the longest side being what we actually want
		r_farclip = sqrt (r_farclip * r_farclip + r_farclip * r_farclip);
	}
	else
	{
		// reuse from the existing cache
		for (int i = 0; i < r_numcachedworldleafs; i++)
		{
			if (r_cachedworldleafs[i]->efrags)
			{
				R_StoreEfrags (&r_cachedworldleafs[i]->efrags);
				d3d_RenderDef.numleaf++;
			}
		}

		for (int i = 0; i < r_numcachedworldsurfaces; i++)
		{
			msurface_t *surf = r_cachedworldsurfaces[i];

			D3D_AllocModelSurf (surf, R_TextureAnimation (&d3d_RenderDef.worldentity, surf->texinfo->texture));
			surf->visframe = d3d_RenderDef.framecount + 1;
		}
	}
}


void D3DSurf_DrawWorld (void)
{
	// update lightmaps as early as possible
	D3DLight_UpdateLightmaps ();

	// set the final projection matrix that we'll actually use
	D3D_SetupProjection (d3d_RenderDef.fov_x, d3d_RenderDef.fov_y, 4, r_farclip);

	// and re-evaluate our MVP
	D3DMatrix_Multiply (&d3d_ModelViewProjMatrix, &d3d_ViewMatrix, &d3d_WorldMatrix);
	D3DMatrix_Multiply (&d3d_ModelViewProjMatrix, &d3d_ProjMatrix);

	// and send it to the shader; this is actually sent twice now; once after the initial rough estimate
	// and once here.  one of these is strictly speaking unnecessary, but to be honest if we're worried
	// about THAT causing performance problems we've got too much time on our hands.
	D3DHLSL_SetWorldMatrix (&d3d_ModelViewProjMatrix);

	// finish up any sky surfs that were added
	D3DSky_FinalizeSky ();

	// add particles here because it's useful work to be doing while waiting on lightmap updates
	D3D_AddParticesToAlphaList ();

	if (kurok)
	{
		D3D_SetRenderState (D3DRS_ALPHATESTENABLE, TRUE);
		D3D_SetRenderState (D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL);
		D3D_SetRenderState (D3DRS_ALPHAREF, (DWORD) 0x000000aa);
	}

	D3DSurf_DrawSurfaces ();

	D3DBrush_DrawVBOSurfaces ();

	if (kurok) D3D_SetRenderState (D3DRS_ALPHATESTENABLE, FALSE);

	if (r_shownodes.value || r_drawflat.value || r_shownodedepth.value)
		D3DSurf_ShowNodes ();

	// draw opaque water surfaces here; this includes bmodels and the world merged together;
	// translucent water surfaces are drawn separately during the alpha surfaces pass
	D3DWarp_DrawWaterSurfaces (d3d_TextureChains, d3d_NumTextureChains);
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
	byte	*vis;
	mnode_t	*node;
	int		i;
	extern	byte *mod_novis;
	msurface_t **mark;

	// viewleaf hasn't changed or we're drawing with a locked PVS
	if ((d3d_RenderDef.oldviewleaf == d3d_RenderDef.viewleaf) || r_lockpvs.value) return;

	// go to a new visframe
	d3d_RenderDef.visframecount++;

	// rebuild the world lists
	d3d_RenderDef.rebuildworld = true;

	bool nearwaterportal = false;
	mleaf_t *leaf = d3d_RenderDef.viewleaf;

	for (i = 0, mark = leaf->firstmarksurface; i < leaf->nummarksurfaces; i++, mark++)
	{
		if ((*mark)->flags & SURF_DRAWTURB)
		{
			// one is all we need
			nearwaterportal = true;
			break;
		}
	}

	// add in visible leafs - we always add the fat PVS to ensure that client visibility
	// is the same as that which was used by the server; R_CullBox will take care of unwanted leafs
	if (r_novis.integer)
		R_LeafVisibility (NULL);
	else if (nearwaterportal)
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
		else if (!r_novis.integer && !nearwaterportal)
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


