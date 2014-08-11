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

void D3DLight_CheckSurfaceForModification (d3d_modelsurf_t *ms);

void R_PushDlights (mnode_t *headnode);
void D3DSky_AddSurfaceToRender (msurface_t *surf, entity_t *ent);
void D3DWarp_DrawWaterSurfaces (d3d_modelsurf_t **modelsurfs, int nummodelsurfs);
void D3DSky_FinalizeSky (void);
void R_MarkLeaves (void);
void D3D_EmitModelSurfToAlpha (d3d_modelsurf_t *modelsurf);
void D3D_AddParticesToAlphaList (void);

cvar_t r_lockpvs ("r_lockpvs", "0");
float r_farclip = 4096.0f;

void D3D_RotateForEntity (entity_t *e);
void D3D_RotateForEntity (entity_t *e, D3DMATRIX *m);

// surface interface
void D3DBrush_Begin (void);
void D3DBrush_End (void);
void D3DBrush_EmitSurface (d3d_modelsurf_t *ms);


typedef struct d3d_texturechain_s
{
	image_t *image;

	// so that we can know the locksizes needed in advance
	int numverts;
	int numindexes;
	int numsurfaces;

	struct d3d_modelsurf_s *chain;
} d3d_texturechain_t;


d3d_texturechain_t *d3d_TextureChains = NULL;
int d3d_NumTextureChains = 0;

void D3DSurf_BeginBuildingTextureChains (void)
{
#if 0
	// just put them in here for now; this has room for > 50000 objects and
	// we assume that no map will ever have that many textures in it
	d3d_TextureChains = (d3d_texturechain_t *) scratchbuf;
	d3d_NumTextureChains = 0;
#endif
}


void D3DSurf_CreateTextureChainObject (image_t *image)
{
#if 0
	image->ChainNumber = d3d_NumTextureChains;
	d3d_TextureChains[d3d_NumTextureChains].image = image;
	d3d_NumTextureChains++;
#endif
}


void D3DSurf_EndBuildingTextureChains (void)
{
#if 0
	d3d_texturechain_t *ch = (d3d_texturechain_t *) MainHunk->Alloc (d3d_NumTextureChains * sizeof (d3d_texturechain_t));
	memcpy (ch, d3d_TextureChains, d3d_NumTextureChains * sizeof (d3d_texturechain_t));
	d3d_TextureChains = ch;

	Con_Printf ("%i chains (of %i)\n", d3d_NumTextureChains, SCRATCHBUF_SIZE / sizeof (d3d_texturechain_t));
#endif
}


void D3DSurf_BeginTextureChains (void)
{
#if 0
	// clear and NULL out everything at the start of a frame
	d3d_texturechain_t *ch = d3d_TextureChains;

	for (int i = 0; i < d3d_NumTextureChains; i++, ch++)
	{
		ch->chain = NULL;
		ch->numindexes = 0;
		ch->numsurfaces = 0;
		ch->numverts = 0;
	}
#endif
}


/*
=============================================================

	ALL BRUSH MODELS

=============================================================
*/

// a BSP cannot contain more than 65536 marksurfaces and we add some headroom for instanced models
#define MAX_MODELSURFS 131072

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
	if (tex->lumaimage && gl_fullbrights.integer)
	{
		ms->textures[TEXTURE_LUMA] = tex->lumaimage->d3d_Texture;
		ms->shaderpass = alpha < 255 ? FX_PASS_WORLD_LUMA_ALPHA : FX_PASS_WORLD_LUMA;
	}
	else if (tex->lumaimage)
	{
		ms->textures[TEXTURE_LUMA] = tex->lumaimage->d3d_Texture;
		ms->shaderpass = alpha < 255 ? FX_PASS_WORLD_LUMA_NOLUMA_ALPHA : FX_PASS_WORLD_LUMA_NOLUMA;
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
	D3DLight_CheckSurfaceForModification (ms);
}


int D3DSurf_ModelSurfsSortFunc (d3d_modelsurf_t **ms1, d3d_modelsurf_t **ms2)
{
	// get all the lumas gathered together
	if (ms1[0]->textures[TEXTURE_LUMA] == ms2[0]->textures[TEXTURE_LUMA])
	{
		// a luma can be NULL so we also need to check diffuse
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


void D3D_AddSurfacesToRender (void)
{
	// this should only ever happen if the scene is filled with water or sky
	if (!d3d_NumModelSurfs) return;

	// just do it nice and fast instead of building up loads of complex chains in multiple passes
	qsort
	(
		d3d_ModelSurfs,
		d3d_NumModelSurfs,
		sizeof (d3d_modelsurf_t *),
		(int (*) (const void *, const void *)) D3DSurf_ModelSurfsSortFunc
	);

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


#define R_MarkLeafSurfs(leaf) \
{ \
	msurface_t **mark = (leaf)->firstmarksurface; \
	int c = (leaf)->nummarksurfaces; \
	if (c) \
	{ \
		do \
		{ \
			(*mark)->intersect = ((leaf)->bops != FULLY_INSIDE_FRUSTUM); \
			(*mark)->visframe = d3d_RenderDef.framecount; \
			mark++; \
		} while (--c); \
	} \
}


__inline float R_PlaneDist (mplane_t *plane, float *org)
{
	switch (plane->type)
	{
	case PLANE_X: return org[0] - plane->dist;
	case PLANE_Y: return org[1] - plane->dist;
	case PLANE_Z: return org[2] - plane->dist;
	default: return DotProduct (org, plane->normal) - plane->dist;
	}

	// never reached
	return 0;
}


void R_StoreEfrags (efrag_t **ppefrag);


void R_AddNodeSurfaces (mnode_t *node)
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
		if (surf->intersect && (node->bops != FULLY_INSIDE_FRUSTUM))
			if (R_CullBox (surf->mins, surf->maxs)) continue;

		float dot = R_PlaneDist (surf->plane, d3d_RenderDef.worldentity.modelorg);

		if (dot > r_farclip) r_farclip = dot;
		if (-dot > r_farclip) r_farclip = -dot;

		// this only ever comes from the world so entity is always NULL and we never have explicit alpha
		D3D_AllocModelSurf (surf, R_TextureAnimation (&d3d_RenderDef.worldentity, surf->texinfo->texture));

		// in case a bad BSP overlaps the surfs in it's nodes
		surf->visframe = -1;
	}
}


__inline bool R_ReturnValidNode (mnode_t *node)
{
	if (node->contents == CONTENTS_SOLID) return false;
	if (node->visframe != d3d_RenderDef.visframecount) return false;
	if (R_CullBox (node)) return false;

	if (node->contents < 0)
	{
		// node is a leaf so add stuff for drawing
		R_MarkLeafSurfs ((mleaf_t *) node);
		R_StoreEfrags (&((mleaf_t *) node)->efrags);
		return false;
	}

	return true;
}


void R_RecursiveWorldNode (mnode_t *node)
{
	// it's assumed that the headnode must always pass so we only check the contents/visframe/leaf on child nodes
	// this way a node will never go through here unless it's already been passed by the level above it.
	while (1)
	{
		// node is just a decision point, so go down the appropriate sides
		node->dot = R_PlaneDist (node->plane, d3d_RenderDef.worldentity.modelorg);
		node->side = (node->dot >= 0 ? 0 : 1);

		// validate both sides
		node->validside[0] = R_ReturnValidNode (node->children[node->side]);
		node->validside[1] = R_ReturnValidNode (node->children[!node->side]);

		if (node->validside[0] && node->validside[1])
		{
			R_RecursiveWorldNode (node->children[node->side]);
			R_AddNodeSurfaces (node);
			node = node->children[!node->side];
		}
		else if (node->validside[0])
		{
			R_RecursiveWorldNode (node->children[node->side]);
			R_AddNodeSurfaces (node);
			return;
		}
		else if (node->validside[1])
		{
			R_AddNodeSurfaces (node);
			node = node->children[!node->side];
		}
		else
		{
			R_AddNodeSurfaces (node);
			return;
		}
	}
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
		if (R_CullBox (leaf->mins, leaf->maxs)) continue;

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
		if (R_CullBox (node->mins, node->maxs)) continue;

		// find which side we're on
		node->dot = R_PlaneDist (node->plane, d3d_RenderDef.worldentity.modelorg);
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

			// this only ever comes from the world so matrix is always NULL and we never have explicit alpha
			D3D_AllocModelSurf (surf, R_TextureAnimation (&d3d_RenderDef.worldentity, surf->texinfo->texture));
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
	model_t *mod = ent->model;

	// static entities already have the leafs they are in bbox culled
	if (R_CullBox (ent->mins, ent->maxs))
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

	// and we only need to calc it's matrix if transforming too
	if (modent)
	{
		// store transform for model - we need to run this in software as we are potentially submitting
		// multiple brush models in a single batch, all of which will be merged with the world render.
		D3DMatrix_Identity (&ent->matrix);
		ent->angles[0] = -ent->angles[0];	// stupid quake bug
		D3D_RotateForEntity (ent, &ent->matrix);
		ent->angles[0] = -ent->angles[0];	// stupid quake bug
	}

	// don't bother with ordering these for now; we'll sort them by texture later
	for (int s = 0; s < mod->brushhdr->nummodelsurfaces; s++, surf++)
	{
		float dot = R_PlaneDist (surf->plane, ent->modelorg);

		// r_farclip should be affected by this too
		if (dot > r_farclip) r_farclip = dot;
		if (-dot > r_farclip) r_farclip = -dot;

		if (((surf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) ||
			(!(surf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON)))
		{
			// add to the modelsurfs list
			D3D_AllocModelSurf (surf, R_TextureAnimation (ent, surf->texinfo->texture), modent, ent->alphaval);
		}
	}
}


void D3D_SetupProjection (float fovx, float fovy, float zn, float zf);

void D3D_BuildWorld (void)
{
	// moved up because the node array builder uses it too and that gets called from r_markleaves
	VectorCopy (r_refdef.vieworg, d3d_RenderDef.worldentity.modelorg);

	// mark visible leafs
	R_MarkLeaves ();

	R_BeginSurfaces (cl.worldmodel);
	D3DSurf_BeginTextureChains ();

	// calculate dynamic lighting for the world
	R_PushDlights (cl.worldmodel->brushhdr->nodes);

	// assume that the world is always visible ;)
	// (although it might not be depending on the scene...)
	d3d_RenderDef.worldentity.visframe = d3d_RenderDef.framecount;
	r_farclip = 4096.0f;	// never go below this

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
			if (ent->Occluded) continue;

			// brush models have different surface types some of which we can't
			// draw during the alpha pass.  we also need to include it in the lightmap
			// uploads pass, so we need to add the full model at this stage.
			D3D_SetupBrushModel (ent);
		}
	}

	// r_farclip so far represents one side of a right-angled triangle with the longest side being what we actually want
	r_farclip = sqrt (r_farclip * r_farclip + r_farclip * r_farclip);

	if (!d3d_RenderDef.automap)
	{
		// set the final projection matrix that we'll actually use
		D3D_SetupProjection (d3d_RenderDef.fov_x, d3d_RenderDef.fov_y, 4, r_farclip);

		// and re-evaluate our MVP
		D3DMatrix_Multiply (&d3d_ModelViewProjMatrix, &d3d_ViewMatrix, &d3d_WorldMatrix);
		D3DMatrix_Multiply (&d3d_ModelViewProjMatrix, &d3d_ProjMatrix);

		// and send it to the shader; this is actually sent twice now; once after the initial rough estimate
		// and once here.  one of these is strictly speaking unnecessary, but to be honest if we're worried
		// about THAT causing performance problems we've got too much time on our hands.
		D3DHLSL_SetWorldMatrix (&d3d_ModelViewProjMatrix);
	}

	// finish up any sky surfs that were added
	D3DSky_FinalizeSky ();

	// add particles here because it's useful work to be doing while waiting on lightmap updates
	D3D_AddParticesToAlphaList ();

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
	static int old_novis = r_novis.integer;
	static int old_lockpvs = r_lockpvs.integer;

	if (r_lockpvs.integer == old_lockpvs)
	{
		// viewleaf hasn't changed, r_novis hasn't changed or we're drawing with a locked PVS
		if (((d3d_RenderDef.oldviewleaf == d3d_RenderDef.viewleaf) && (r_novis.integer == old_novis)) || r_lockpvs.value) return;
	}

	// go to a new visframe
	d3d_RenderDef.visframecount++;
	old_novis = r_novis.integer;
	old_lockpvs = r_lockpvs.integer;

	// add in visible leafs - we always add the fat PVS to ensure that client visibility
	// is the same as that which was used by the server; R_CullBox will take care of unwanted leafs
	if (r_novis.integer)
		R_LeafVisibility (NULL);
	else if (!R_NearWaterTest ())
		R_LeafVisibility (Mod_LeafPVS (d3d_RenderDef.viewleaf, cl.worldmodel));
	else R_LeafVisibility (Mod_FatPVS (r_viewvectors.origin));

	// no old viewleaf so can't make a transition check
	if (!d3d_RenderDef.oldviewleaf) return;

	// check for a contents transition
	if (d3d_RenderDef.oldviewleaf->contents == d3d_RenderDef.viewleaf->contents)
	{
		// if we're still in the same contents we still have the same contents colour
		d3d_RenderDef.viewleaf->contentscolor = d3d_RenderDef.oldviewleaf->contentscolor;
	}
}


