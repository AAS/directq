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
#include "d3d_hlsl.h"

// number of verts
int d3d_VertexBufferVerts = 0;

LPDIRECT3DVERTEXBUFFER9 d3d_BrushModelVerts = NULL;
D3DFORMAT d3d_BrushIndexFormat = D3DFMT_INDEX16;

int r_renderflags = 0;

void R_PushDlights (mnode_t *headnode);
void EmitSkyPolys (msurface_t *surf);
void R_DrawSkyChain (msurface_t *skychain);
bool R_CullBox (vec3_t mins, vec3_t maxs);
void D3D_RotateForEntity (entity_t *e, bool shadow = false);
void D3D_MakeLightmapTexCoords (msurface_t *surf, float *v, float *st);
void D3D_DrawWorld (void);
void D3D_AddInlineBModelsToTextureChains (void);
void R_AddStaticEntitiesForLeaf (mleaf_t *leaf);

msurface_t  *skychain = NULL;

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

	if (currententity->frame)
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
	if (tex->d3d_Fullbright)
		r_renderflags |= R_RENDERLUMA;
	else r_renderflags |= R_RENDERNOLUMA;

	// return what we got
	return tex;
}


/*
=============================================================

	WORLD MODEL

=============================================================
*/


void R_MarkLeafSurfs (mleaf_t *leaf, int visframe)
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


void R_PlaneSide (mplane_t *plane, double *dot, int *side)
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


/*
================
R_RecursiveWorldNode
================
*/
void R_RecursiveWorldNode (mnode_t *node)
{
	int			side;
	mplane_t	*plane;
	msurface_t	*surf;
	double		dot;

	if (node->contents == CONTENTS_SOLID) return;

	if (node->visframe != r_visframecount) return;
	if (R_CullBox (node->minmaxs, node->minmaxs + 3)) return;

	// if a leaf node, draw stuff
	if (node->contents < 0)
	{
		// leaf visframes are never marked?
		((mleaf_t *) node)->visframe = r_visframecount;

		// mark the surfs
		R_MarkLeafSurfs ((mleaf_t *) node, r_framecount);

		// add static entities contained in the leaf to the list
		R_AddStaticEntitiesForLeaf ((mleaf_t *) node);
		return;
	}

	// node is just a decision point, so go down the appropriate sides
	// find which side of the node we are on
	R_PlaneSide (node->plane, &dot, &side);

	// recurse down the children, front side first
	R_RecursiveWorldNode (node->children[side]);

	// add stuff to the draw lists
	int c = node->numsurfaces;

	if (c)
	{
		surf = cl.worldbrush->surfaces + node->firstsurface;

		if (dot < 0 -BACKFACE_EPSILON)
			side = SURF_PLANEBACK;
		else if (dot > BACKFACE_EPSILON)
			side = 0;

		for (; c; c--, surf++)
		{
			if (surf->visframe != r_framecount) continue;
			if (((dot < 0) ^ !!(surf->flags & SURF_PLANEBACK))) continue;

			// get the correct animation sequence
			texture_t *tex = R_TextureAnimation (surf->texinfo->texture);

			// if sorting by texture, just store it out
			if (surf->flags & SURF_DRAWSKY)
			{
				// link it in (back to front)
				surf->texturechain = skychain;
				skychain = surf;
			}
			else if (surf->flags & SURF_DRAWTURB)
			{
				// link it in (back to front)
				surf->texturechain = tex->texturechain;
				tex->texturechain = surf;

				// mark the texture as having been visible
				tex->visframe = r_framecount;

				// flag for rendering
				r_renderflags |= R_RENDERWATERSURFACE;
			}
			else
			{
				// check under/over water
				if (surf->flags & SURF_UNDERWATER)
					r_renderflags |= R_RENDERUNDERWATER;
				else r_renderflags |= R_RENDERABOVEWATER;

				// check for lightmap modifications
				D3D_CheckLightmapModification (surf);

				// link it in (front to back)
				if (!tex->chaintail)
					tex->texturechain = surf;
				else tex->chaintail->texturechain = surf;

				tex->chaintail = surf;
				surf->texturechain = NULL;
			}
		}
	}

	// recurse down the back side
	R_RecursiveWorldNode (node->children[!side]);
}


/*
=============
R_DrawWorld
=============
*/
void R_DrawWorld (void)
{
	entity_t	ent;

	memset (&ent, 0, sizeof(ent));
	ent.model = cl.worldmodel;

	VectorCopy (r_refdef.vieworg, modelorg);
	currententity = &ent;

	// calculate dynamic lighting for the world
	R_PushDlights (cl.worldbrush->nodes);

	// build the world
	R_RecursiveWorldNode (cl.worldbrush->nodes);

	// add inline models
	D3D_AddInlineBModelsToTextureChains ();

	// unlock all lightmaps that were previously locked
	D3D_UnlockLightmaps ();

	// draw sky first, otherwise the z-fail technique will flood the framebuffer with sky
	// rather than just drawing it where skybrushes appear
	R_DrawSkyChain (skychain);

	D3D_DrawWorld ();
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
			node = (mnode_t *) &cl.worldbrush->leafs[i + 1];

			do
			{
				// already added
				if (node->visframe == r_visframecount) break;

				// add it
				node->visframe = r_visframecount;
				node->seen = true;
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

	// viewleaf hasn't changed
	if ((r_oldviewleaf == r_viewleaf && !r_novis.value) || r_lockpvs.value) return;

	// go to a new visframe
	r_visframecount++;

	if (r_novis.value)
	{
		// add them all
		R_LeafVisibility (NULL); //mod_novis);

		// done
		return;
	}

	// add viewleaf to visible lists
	R_LeafVisibility (Mod_LeafPVS (r_viewleaf, cl.worldmodel));
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


void D3D_BuildWarpSurfVB (msurface_t *surf, model_t *mod, worldvert_t *vdest)
{
	int			i;
	int			lindex;
	float		*vec;

	for (i = 0; i < surf->numedges; i++, vdest++)
	{
		lindex = mod->bh->surfedges[surf->firstedge + i];

		if (lindex > 0)
			vec = mod->bh->vertexes[mod->bh->edges[lindex].v[0]].position;
		else vec = mod->bh->vertexes[mod->bh->edges[-lindex].v[1]].position;

		// verts
		vdest->xyz[0] = vec[0];
		vdest->xyz[1] = vec[1];
		vdest->xyz[2] = vec[2];

		// check world extents
		D3D_CheckSurfMinMaxs (surf, vec);

		// texcoords
		vdest->st[0] = DotProduct (vec, surf->texinfo->vecs[0]) / 4;
		vdest->st[1] = DotProduct (vec, surf->texinfo->vecs[1]) / 4;

		// store in lm texcoords to speed up the pixel shader
		vdest->lm[0] = vdest->st[0] / 4;
		vdest->lm[1] = vdest->st[1] / 4;
	}
}


void D3D_BuildSurfaceVertexBuffer (msurface_t *surf, model_t *mod, worldvert_t *vdest)
{
	int			i, lindex;
	float		*vec;

	// go through all of the verts
	for (i = 0; i < surf->numedges; i++, vdest++)
	{
		// build vertexes
		lindex = mod->bh->surfedges[surf->firstedge + i];

		if (lindex > 0)
			vec = mod->bh->vertexes[mod->bh->edges[lindex].v[0]].position;
		else vec = mod->bh->vertexes[mod->bh->edges[-lindex].v[1]].position;

		// copy verts in
		vdest->xyz[0] = vec[0];
		vdest->xyz[1] = vec[1];
		vdest->xyz[2] = vec[2];

		// check world extents
		D3D_CheckSurfMinMaxs (surf, vec);

		// build texcoords
		vdest->st[0] = DotProduct (vec, surf->texinfo->vecs[0]) + surf->texinfo->vecs[0][3];
		vdest->st[0] /= surf->texinfo->texture->width;

		vdest->st[1] = DotProduct (vec, surf->texinfo->vecs[1]) + surf->texinfo->vecs[1][3];
		vdest->st[1] /= surf->texinfo->texture->height;

		// make lightmap coords
		D3D_MakeLightmapTexCoords (surf, vec, vdest->lm);
	}
}


void D3D_PutSurfacesInVertexBuffer (void)
{
	// release it if it already exists
	// do this even if rendering in software
	SAFE_RELEASE (d3d_BrushModelVerts);

	// create our new vertex buffer
	HRESULT hr;

	hr = d3d_Device->CreateVertexBuffer
	(
		d3d_VertexBufferVerts * sizeof (worldvert_t),
		D3DUSAGE_WRITEONLY | d3d_VertexBufferUsage,
		0,
		D3DPOOL_MANAGED,
		&d3d_BrushModelVerts,
		NULL
	);

	// fixme - fall back on DrawPrimitiveUP if this happens!
	if (FAILED (hr)) Sys_Error ("D3D_PutSurfacesInVertexBuffer: d3d_Device->CreateVertexBuffer failed");

	worldvert_t *verts;

	// lock the full buffers
	d3d_BrushModelVerts->Lock (0, 0, (void **) &verts, 0);

	int vboffset = 0;

	for (int j = 1; j < MAX_MODELS; j++)
	{
		model_t *m = cl.model_precache[j];

		if (!m) break;
		if (m->name[0] == '*') continue;
		if (m->type != mod_brush) continue;

		msurface_t *surf = m->bh->surfaces;

		for (int i = 0; i < m->bh->numsurfaces; i++, surf++)
		{
			// skip
			if (surf->flags & SURF_DRAWSKY) continue;

			// initial world extents
			D3D_InitSurfMinMaxs (surf);

			// build the vertex buffer for this surf
			if (surf->flags & SURF_DRAWTURB)
				D3D_BuildWarpSurfVB (surf, m, verts);
			else D3D_BuildSurfaceVertexBuffer (surf, m, verts);

			// set offset
			surf->vboffset = vboffset;

			// advance pointers
			verts += surf->numedges;

			// advance offset counter
			vboffset += surf->numedges;
		}
	}

	// unlock and preload, baby!
	d3d_BrushModelVerts->Unlock ();
	d3d_BrushModelVerts->PreLoad ();
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
	int		i, j;
	model_t	*m;
	extern int MaxExtents[];

	Con_DPrintf ("Max Surface Extents: %i x %i\n", MaxExtents[0], MaxExtents[1]);
	D3D_CreateBlockLights ();

	MaxExtents[0] = MaxExtents[1] = 0;

	// this really has less connection with dlight cache than it does with general visibility
	r_framecount = 1;
	r_visframecount = 0;

	// clear down any previous lightmaps
	D3D_ReleaseLightmaps ();

	// no verts yet
	d3d_VertexBufferVerts = 0;

	for (j = 1; j < MAX_MODELS; j++)
	{
		m = cl.model_precache[j];

		if (!m) break;
		if (m->name[0] == '*') continue;
		if (m->type != mod_brush) continue;

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
			// increment vertex count
			d3d_VertexBufferVerts += surf->numedges;

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
				// create the lightmap
				D3D_CreateSurfaceLightmap (surf);

				// no polys on these surfs
				surf->polys = NULL;
			}

			// clean up after ourselves
			m->bh->textures[i]->texturechain = NULL;
		}
	}

	// put all the surfaces into vertex buffers
	D3D_PutSurfacesInVertexBuffer ();

	// upload all lightmaps
	D3D_UploadLightmaps ();
}

