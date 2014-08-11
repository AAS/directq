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
// r_surf.c: surface-related refresh code

#include "quakedef.h"
#include "d3d_quake.h"


void R_PushDlights (mnode_t *headnode);
void EmitSkyPolys (msurface_t *surf);
void D3D_DrawWorld (void);
__inline void R_AddStaticEntitiesForLeaf (mleaf_t *leaf);
void D3D_AutomapRecursiveNode (mnode_t *node);

cvar_t r_lockpvs ("r_lockpvs", "0");

/*
===============
R_TextureAnimation

Returns the proper texture for a given time and base texture
===============
*/
texture_t *R_TextureAnimationInner (texture_t *base)
{
	int		relative;
	int		count;

	if (d3d_RenderDef.currententity->frame)
	{
		if (base->alternate_anims)
			base = base->alternate_anims;
	}

	if (!base->anim_total) return base;

	relative = (int)(cl.time*10) % base->anim_total;

	count = 0;	

	while (base->anim_min > relative || base->anim_max <= relative)
	{
		base = base->anim_next;

		// prevent crash here
		if (!base) return NULL;
		if (++count > 100) return NULL;
	}

	return base;
}


texture_t *R_TextureAnimation (texture_t *base)
{
	// check animation
	texture_t *tex = R_TextureAnimationInner (base);

	// catch errors
	if (!tex) tex = base;

	// update renderflags
	if (tex->d3d_Fullbright && d3d_DeviceCaps.MaxTextureBlendStages > 2)
	{
		d3d_RenderDef.renderflags |= R_RENDERLUMA;
		d3d_RenderDef.brushrenderflags |= R_RENDERLUMA;
	}
	else
	{
		d3d_RenderDef.renderflags |= R_RENDERNOLUMA;
		d3d_RenderDef.brushrenderflags |= R_RENDERNOLUMA;
	}

	// return what we got
	return tex;
}


/*
=============================================================

	WORLD MODEL

=============================================================
*/

extern float r_clipdist;

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
		*dot = modelorg[0] - plane->dist;
		break;
	case PLANE_Y:
		*dot = modelorg[1] - plane->dist;
		break;
	case PLANE_Z:
		*dot = modelorg[2] - plane->dist;
		break;
	default:
		*dot = DotProduct (modelorg, plane->normal) - plane->dist;
		break;
	}

	if (*dot >= 0)
		*side = 0;
	else *side = 1;
}


__inline void R_AddSurfToDrawLists (msurface_t *surf)
{
	// get the correct animation sequence
	texture_t *tex = R_TextureAnimation (surf->texinfo->texture);

	// if sorting by texture, just store it out
	if (surf->flags & SURF_DRAWSKY)
	{
		// link it in (back to front)
		surf->texturechain = d3d_RenderDef.skychain;
		d3d_RenderDef.skychain = surf;
	}
	else if (surf->flags & SURF_DRAWTURB)
	{
		// link it in (back to front)
		surf->texturechain = tex->texturechain;
		tex->texturechain = surf;

		// mark the texture as having been visible
		tex->visframe = d3d_RenderDef.framecount;

		// flag for rendering
		d3d_RenderDef.renderflags |= R_RENDERWATERSURFACE;
	}
	else
	{
		// check under/over water
		if (surf->flags & SURF_UNDERWATER)
			d3d_RenderDef.renderflags |= R_RENDERUNDERWATER;
		else d3d_RenderDef.renderflags |= R_RENDERABOVEWATER;

		// check for lightmap modifications
		if (surf->d3d_Lightmap) surf->d3d_Lightmap->CheckSurfaceForModification (surf);

		// link it in (front to back)
		if (!tex->chaintail)
			tex->texturechain = surf;
		else tex->chaintail->texturechain = surf;

		tex->chaintail = surf;
		surf->texturechain = NULL;
	}
}


/*
================
R_RecursiveWorldNode
================
*/
void R_RecursiveWorldNode (mnode_t *node)
{
	int			side;
	msurface_t	*surf;
	float		dot;

loc0:;
	if (node->contents == CONTENTS_SOLID) return;
	if (node->visframe != d3d_RenderDef.visframecount) return;
	if (R_CullBox (node->minmaxs, node->minmaxs + 3)) return;

	// if a leaf node, draw stuff
	if (node->contents < 0)
	{
		// leaf visframes are never marked?
		((mleaf_t *) node)->visframe = d3d_RenderDef.visframecount;

		// mark the surfs
		R_MarkLeafSurfs ((mleaf_t *) node, d3d_RenderDef.framecount);

		// add static entities contained in the leaf to the list
		R_AddStaticEntitiesForLeaf ((mleaf_t *) node);
		return;
	}

	// node is just a decision point, so go down the appropriate sides
	// find which side of the node we are on
	R_PlaneSide (node->plane, &dot, &side);

	// check max dot
	if (dot > r_clipdist) r_clipdist = dot;
	if (-dot > r_clipdist) r_clipdist = -dot;

	// recurse down the children, front side first (but only if we *have* to
	// note: i'm sure that there's a condition here we can goto on
	if (node->children[side]->contents != CONTENTS_SOLID || node->children[side]->visframe != d3d_RenderDef.visframecount)
		R_RecursiveWorldNode (node->children[side]);

	// add stuff to the draw lists
	if (node->numsurfaces)
	{
		int c = node->numsurfaces;

		if (dot < 0 -BACKFACE_EPSILON)
			side = SURF_PLANEBACK;
		else if (dot > BACKFACE_EPSILON)
			side = 0;

		for (surf = cl.worldbrush->surfaces + node->firstsurface; c; c--, surf++)
		{
			if (surf->visframe != d3d_RenderDef.framecount) continue;
			if ((dot < 0) ^ !!(surf->flags & SURF_PLANEBACK)) continue;

			// add this surf
			R_AddSurfToDrawLists (surf);
		}
	}

	// recurse down the back side
	node = node->children[!side];
	goto loc0;
}


void R_AutomapSurfaces (void)
{
	// don't bother depth-sorting these
	// some Quake weirdness here
	// this info taken from qbsp source code:
	// leaf 0 is a common solid with no faces
	for (int i = 1; i <= cl.worldbrush->numleafs; i++)
	{
		mleaf_t *leaf = &cl.worldbrush->leafs[i];

		// show all leafs that have previously been seen
		if (!leaf->seen) continue;

		// need to cull here too otherwise we'll get static ents we shouldn't
		if (R_CullBox (leaf->minmaxs, leaf->minmaxs + 3)) continue;

		// mark the surfs
		R_MarkLeafSurfs (leaf, d3d_RenderDef.framecount);

		// add static entities contained in the leaf to the list
		R_AddStaticEntitiesForLeaf (leaf);
	}

	int side;
	float dot;

	// setup modelorg to a point so far above the viewpoint that it may as well be infinite
	modelorg[0] = (cl.worldmodel->mins[0] + cl.worldmodel->maxs[0]) / 2;
	modelorg[1] = (cl.worldmodel->mins[1] + cl.worldmodel->maxs[1]) / 2;
	modelorg[2] = ((cl.worldmodel->mins[2] + cl.worldmodel->maxs[2]) / 2) + 16777216.0f;

	extern cvar_t r_automap_nearclip;
	extern float r_automap_z;

	for (int i = 0; i < cl.worldbrush->numnodes; i++)
	{
		mnode_t *node = &cl.worldbrush->nodes[i];

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
			msurface_t *surf = cl.worldbrush->surfaces + node->firstsurface + j;

			// surf culling
			if (surf->visframe != d3d_RenderDef.framecount) continue;
			if (surf->flags & SURF_DRAWSKY) continue;
			if (surf->mins[2] > (r_refdef.vieworg[2] + r_automap_nearclip.integer + r_automap_z)) continue;
			if ((dot < 0) ^ !!(surf->flags & SURF_PLANEBACK)) continue;

			R_AddSurfToDrawLists (surf);
		}
	}

	// restore correct modelorg
	VectorCopy (r_origin, modelorg);
}


/*
=============
D3D_DrawWorld
=============
*/
void R_ClearSurfaceChains (model_t *mod);

void D3D_PrepareWorld (void)
{
	entity_t	ent;

	memset (&ent, 0, sizeof (ent));
	ent.model = cl.worldmodel;
	cl.worldmodel->cacheent = &ent;
	ent.alphaval = 255;
	d3d_RenderDef.currententity = &ent;

	R_ClearSurfaceChains (cl.worldmodel);

	// calculate dynamic lighting for the world
	R_PushDlights (cl.worldbrush->nodes);

	// build the world
	VectorCopy (r_origin, modelorg);

	// never go < 2048
	r_clipdist = 2048;

	if (d3d_RenderDef.automap)
		R_AutomapSurfaces ();
	else R_RecursiveWorldNode (cl.worldbrush->nodes);
}


/*
===============
R_MarkLeaves
===============
*/
void R_LeafVisibility (byte *vis)
{
	mnode_t *node;

	for (int i = 0; i < cl.worldbrush->numleafs; i++)
	{
		if (!vis || (vis[i >> 3] & (1 << (i & 7))))
		{
			// note - nodes and leafs need to be in consecutive memory for this to work so
			// that the last leaf will resolve to the first node here; this is set up in Mod_LoadVisLeafsNodes
			// first node always needs to be in the pvs for R_RecursiveWorldNode
			node = (mnode_t *) &cl.worldbrush->leafs[i + 1];

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


void D3D_InitSurfMinMaxs (msurface_t *surf)
{
	surf->mins[0] = surf->mins[1] = surf->mins[2] = 999999999;
	surf->maxs[0] = surf->maxs[1] = surf->maxs[2] = -999999999;
}


void D3D_CheckSurfMinMaxs (msurface_t *surf, float *v)
{
	if (v[0] < surf->mins[0]) surf->mins[0] = v[0];
	if (v[1] < surf->mins[1]) surf->mins[1] = v[1];
	if (v[2] < surf->mins[2]) surf->mins[2] = v[2];

	if (v[0] > surf->maxs[0]) surf->maxs[0] = v[0];
	if (v[1] > surf->maxs[1]) surf->maxs[1] = v[1];
	if (v[2] > surf->maxs[2]) surf->maxs[2] = v[2];
}


void D3D_BuildSurfaceVertexBuffer (msurface_t *surf, model_t *mod, glpoly_t *polybuf, glpolyvert_t *vertbuf)
{
	int			i, lindex;
	float		*vec;

	surf->polys = polybuf;
	surf->polys->verts = vertbuf;
	surf->polys->numverts = surf->numedges;
	surf->polys->next = NULL;

	glpolyvert_t *v = surf->polys->verts;

	surf->midpoint[0] = surf->midpoint[1] = surf->midpoint[2] = 0;

	// go through all of the verts
	for (i = 0; i < surf->numedges; i++, v++)
	{
		// build vertexes
		lindex = mod->bh->surfedges[surf->firstedge + i];

		if (lindex > 0)
			vec = mod->bh->vertexes[mod->bh->edges[lindex].v[0]].position;
		else vec = mod->bh->vertexes[mod->bh->edges[-lindex].v[1]].position;

		// copy verts in
		v->xyz[0] = vec[0];
		v->xyz[1] = vec[1];
		v->xyz[2] = vec[2];

		// accumulate to midpoint
		surf->midpoint[0] += vec[0];
		surf->midpoint[1] += vec[1];
		surf->midpoint[2] += vec[2];

		// check world extents
		D3D_CheckSurfMinMaxs (surf, vec);

		// build texcoords
		v->st[0] = DotProduct (vec, surf->texinfo->vecs[0]) + surf->texinfo->vecs[0][3];
		v->st[0] /= surf->texinfo->texture->width;

		v->st[1] = DotProduct (vec, surf->texinfo->vecs[1]) + surf->texinfo->vecs[1][3];
		v->st[1] /= surf->texinfo->texture->height;

		if (surf->d3d_Lightmap) surf->d3d_Lightmap->CalcLightmapTexCoords (surf, vec, v->lm);
	}

	// average out midpoint
	surf->midpoint[0] /= surf->numedges;
	surf->midpoint[1] /= surf->numedges;
	surf->midpoint[2] /= surf->numedges;
}


void GL_SubdivideWater (msurface_t *surf, model_t *mod);

void D3D_PutSurfacesInVertexBuffer (void)
{
	int numsurfs = 0;
	int numverts = 0;

	// count numbers of surfs and polys
	for (int j = 1; j < MAX_MODELS; j++)
	{
		model_t *m = cl.model_precache[j];

		if (!m) break;
		if (m->name[0] == '*') continue;
		if (m->type != mod_brush) continue;

		msurface_t *surf = m->bh->surfaces;

		for (int i = 0; i < m->bh->numsurfaces; i++, surf++)
		{
			// skip turbulent because they're subdivided
			if (surf->flags & SURF_DRAWTURB) continue;

			numsurfs++;
			numverts += surf->numedges;
		}
	}

	Con_DPrintf ("%i surfs with %i verts\n", numsurfs, numverts);

	// alloc one time only in bulk instead of once per surf
	glpoly_t *polybuf = (glpoly_t *) Pool_Alloc (POOL_MAP, numsurfs * sizeof (glpoly_t));
	glpolyvert_t *vertbuf = (glpolyvert_t *) Pool_Alloc (POOL_MAP, numverts * sizeof (glpolyvert_t));

	// now do them for real
	for (int j = 1; j < MAX_MODELS; j++)
	{
		model_t *m = cl.model_precache[j];

		if (!m) break;
		if (m->name[0] == '*') continue;
		if (m->type != mod_brush) continue;

		msurface_t *surf = m->bh->surfaces;

		for (int i = 0; i < m->bh->numsurfaces; i++, surf++)
		{
			// initial world extents
			D3D_InitSurfMinMaxs (surf);

			// build the vertex buffer for this surf
			if (surf->flags & SURF_DRAWTURB)
			{
				surf->polys = NULL;
				GL_SubdivideWater (surf, m);
			}
			else
			{
				// write the polys and verts directly into the big one-time buffers
				D3D_BuildSurfaceVertexBuffer (surf, m, polybuf, vertbuf);

				// go to next buffer positions
				polybuf++;
				vertbuf += surf->numedges;
			}
		}
	}
}


/*
==================
GL_BuildLightmaps

Builds the lightmap texture
with all the surfaces from all brush models
==================
*/
void D3D_CreateBlockLights (void);


void GL_BuildLightmaps (void)
{
	Con_DPrintf ("Loading lightmaps... ");

	int		i, j;
	model_t	*m;
	extern int MaxExtents[];

	Con_DPrintf ("Max Surface Extents: %i x %i\n", MaxExtents[0], MaxExtents[1]);
	D3D_CreateBlockLights ();

	MaxExtents[0] = MaxExtents[1] = 0;

	// this really has less connection with dlight cache than it does with general visibility
	d3d_RenderDef.framecount = 1;
	d3d_RenderDef.visframecount = 0;

	// clear down any previous lightmaps
	SAFE_DELETE (d3d_Lightmaps);

	// note - the world (model 0) doesn't actually have a model set in the precache list so we need to start at 1
	for (j = 1; j < MAX_MODELS; j++)
	{
		m = cl.model_precache[j];

		// note - we set the end of the precache list to NULL in cl_parse to ensure this test is valid
		if (!m) break;

		m->cacheent = NULL;

		if (m->type != mod_brush) continue;

		// assume inline until we prove otherwise
		m->bh->brushtype = MOD_BRUSH_INLINE;

		if (m->name[0] == '*')
		{
			// alloc space for caching the entity state
			m->cacheent = (entity_t *) Pool_Alloc (POOL_MAP, sizeof (entity_t));
			continue;
		}

		// set to instanced; the world model is updated during the main refresh
		m->bh->brushtype = MOD_BRUSH_INSTANCED;

		msurface_t *surf = m->bh->surfaces;

		// batch lightmaps by texture so that we can have more efficient index buffer usage
		// first we need to init texture chains for the lightmaps
		for (int i = 0; i < m->bh->numtextures; i++)
		{
			// e2m3 gets this
			if (!m->bh->textures[i]) continue;

			m->bh->textures[i]->texturechain = NULL;
		}

		// now we store the surfs in this model into the chains
		for (int i = 0; i < m->bh->numsurfaces; i++, surf++)
		{
			// skip these
			if (surf->flags & SURF_DRAWTURB) continue;
			if (surf->flags & SURF_DRAWSKY) continue;

			// link it in (back to front)
			surf->texturechain = surf->texinfo->texture->texturechain;
			surf->texinfo->texture->texturechain = surf;
		}

		// finally we create the lightmaps themselves
		for (int i = 0; i < m->bh->numtextures; i++)
		{
			if (!m->bh->textures[i]) continue;
			if (!m->bh->textures[i]->texturechain) continue;

			for (surf = m->bh->textures[i]->texturechain; surf; surf = surf->texturechain)
			{
				// ensure
				surf->d3d_Lightmap = NULL;

				if (surf->flags & SURF_DRAWSKY) continue;
				if (surf->flags & SURF_DRAWTURB) continue;

				// fill in lightmap extents
				surf->smax = (surf->extents[0] >> 4) + 1;
				surf->tmax = (surf->extents[1] >> 4) + 1;

				// max of smax and tmax
				if (surf->tmax > surf->smax)
					surf->maxextent = surf->tmax;
				else surf->maxextent = surf->smax;

				// create the lightmap
				if (d3d_Lightmaps && d3d_Lightmaps->AllocBlock (surf))
					continue;

				// didn't find a lightmap so allocate a new one
				// this will always work as a new block that's big enough will just have become free
				surf->d3d_Lightmap = new CD3DLightmap (surf);
				d3d_Lightmaps->AllocBlock (surf);
			}

			// clean up after ourselves
			m->bh->textures[i]->texturechain = NULL;
		}
	}

	// upload all lightmaps
	d3d_Lightmaps->Upload ();

	// put all the surfaces into vertex buffers
	D3D_PutSurfacesInVertexBuffer ();

	Con_DPrintf ("Done");
}

