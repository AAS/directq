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

// number of verts
int d3d_VertexBufferVerts = 0;

// draw in 2 streams
LPDIRECT3DVERTEXBUFFER9 d3d_BrushModelVerts = NULL;


typedef struct worldvert_s
{
	float xyz[3];
	float st[2];
	float lm[2];
} worldvert_t;

void R_PushDlights (mnode_t *headnode);
void EmitWaterPolys (msurface_t *fa);
void EmitSkyPolys (msurface_t *fa);
void R_DrawSkyChain (msurface_t *skychain);
bool R_CullBox (vec3_t mins, vec3_t maxs);
void R_MarkLights (dlight_t *light, int bit, mnode_t *node);
void D3D_RotateForEntity (entity_t *e);
void R_StoreEfrags (efrag_t **ppefrag);
void D3D_MakeLightmapTexCoords (msurface_t *surf, float *v, float *st);
void D3D_TransformWarpSurface (msurface_t *surf, D3DMATRIX *m);


// For gl_texsort 0
msurface_t  *skychain = NULL;


/*
===============
R_TextureAnimation

Returns the proper texture for a given time and base texture
===============
*/
texture_t *R_TextureAnimation (texture_t *base)
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

		if (!base) Sys_Error ("R_TextureAnimation: broken cycle");
		if (++count > 100) Sys_Error ("R_TextureAnimation: infinite cycle");
	}

	return base;
}


/*
=============================================================

	BRUSH MODELS

=============================================================
*/


void DrawGLWaterPoly (glpoly_t *p);
void DrawGLWaterPolyLightmap (glpoly_t *p);


/*
================
DrawGLWaterPoly

Warp the vertex coordinates
================
*/
void DrawGLWaterPoly (glpoly_t *p)
{
	int		i;
	float	*v;
	vec3_t	nv;

	/*
	glBegin (GL_TRIANGLE_FAN);
	v = p->verts[0];
	for (i=0 ; i<p->numverts ; i++, v+= VERTEXSIZE)
	{
		glTexCoord2f (v[3], v[4]);

		nv[0] = v[0] + 8*sin(v[1]*0.05+realtime)*sin(v[2]*0.05+realtime);
		nv[1] = v[1] + 8*sin(v[0]*0.05+realtime)*sin(v[2]*0.05+realtime);
		nv[2] = v[2];

		glVertex3fv (nv);
	}
	glEnd ();
	*/
}

void DrawGLWaterPolyLightmap (glpoly_t *p)
{
	/*
	int		i;
	float	*v;
	vec3_t	nv;

	glBegin (GL_TRIANGLE_FAN);
	v = p->verts[0];
	for (i=0 ; i<p->numverts ; i++, v+= VERTEXSIZE)
	{
		glTexCoord2f (v[5], v[6]);

		nv[0] = v[0] + 8*sin(v[1]*0.05+realtime)*sin(v[2]*0.05+realtime);
		nv[1] = v[1] + 8*sin(v[0]*0.05+realtime)*sin(v[2]*0.05+realtime);
		nv[2] = v[2];

		glVertex3fv (nv);
	}
	glEnd ();
	*/
}


/*
================
R_MirrorChain
================
*/
void R_MirrorChain (msurface_t *s)
{
	if (mirror)
		return;
	mirror = true;
	mirror_plane = s->plane;
}


/*
================
R_DrawWaterSurfaces
================
*/
void R_DrawWaterSurfaces (void)
{
	int			i;
	msurface_t	*s;
	texture_t	*t;

	// pass 1 renders static surfaces from the vertex buffers
	D3D_SetFVFStateManaged (D3DFVF_XYZ | D3DFVF_TEX1);

	for (i = 0; i < cl.worldmodel->numtextures; i++)
	{
		// no texture (e2m3 gets this)
		if (!(t = cl.worldmodel->textures[i])) continue;

		// nothing to draw for this texture
		if (!(s = t->texturechain)) continue;

		// skip over
		if (!(s->flags & SURF_DRAWTURB)) continue;

		D3D_BindTexture (0, (LPDIRECT3DTEXTURE9) t->d3d_Texture);

		for (; s; s = s->texturechain)
		{
			EmitWaterPolys (s);
		}
	}
}


void D3D_DrawBrushModel (entity_t *e)
{
	vec3_t		mins, maxs;
	int			i;
	msurface_t	*surf;
	model_t		*clmodel;

	// needed for R_TextureAnimation
	currententity = e;

	clmodel = e->model;

	// entity was culled
	if (clmodel->name[0] == '*' && e->visframe != r_framecount)
		return;

	// hack the origin to prevent bmodel z-fighting
	e->origin[0] -= DIST_EPSILON;
	e->origin[1] -= DIST_EPSILON;
	e->origin[2] -= DIST_EPSILON;

	d3d_WorldMatrixStack->Push ();
	D3D_RotateForEntity (e);

	// un-hack the origin
	e->origin[0] += DIST_EPSILON;
	e->origin[1] += DIST_EPSILON;
	e->origin[2] += DIST_EPSILON;

	// draw the surfs
	for (i = 0, surf = &clmodel->surfaces[clmodel->firstmodelsurface]; i < clmodel->nummodelsurfaces; i++, surf++)
	{
		// check for surface culling
		if (clmodel->name[0] == '*' && surf->visframe != r_framecount) continue;

		// handled separately
		if (surf->flags & SURF_DRAWTURB) continue;
		if (surf->flags & SURF_DRAWSKY) continue;

		// get the correct animation frame
		texture_t *t = R_TextureAnimation (surf->texinfo->texture);

		// because these are simple models we don't bother with chaining or batching
		D3D_BindTexture (0, (LPDIRECT3DTEXTURE9) t->d3d_Texture);
		D3D_BindLightmap (1, surf->d3d_Lightmap);

		// draw the surface
		d3d_Device->DrawPrimitive (D3DPT_TRIANGLEFAN, surf->vboffset, surf->numedges - 2);
	}

	d3d_WorldMatrixStack->Pop ();
}


void D3D_DrawBrushModels (void)
{
	int i;
	bool RestoreWorldMatrix = false;

	if (!r_drawentities.value) return;

	// draw sprites seperately, because of alpha blending
	for (i = 0; i < cl_numvisedicts; i++)
	{
		// R_TextureAnimation needs this
		currententity = cl_visedicts[i];

		// only doing brushes here
		if (currententity->model->type == mod_brush)
		{
			RestoreWorldMatrix = true;
			D3D_DrawBrushModel (currententity);
		}
	}

	// restore the world matrix
	if (RestoreWorldMatrix) d3d_Device->SetTransform (D3DTS_WORLD, d3d_WorldMatrix);
}


/*
================
DrawTextureChains
================
*/
void DrawTextureChains (void)
{
	int		i;
	msurface_t	*s;
	texture_t	*t;

	// pass 1 renders static surfaces from the vertex buffers
	D3D_SetFVFStateManaged (D3DFVF_XYZ | D3DFVF_TEX2);

	// set the vertex buffer stream
	d3d_Device->SetStreamSource (0, d3d_BrushModelVerts, 0, sizeof (worldvert_t));

	// first stage is the texture
	d3d_Device->SetTextureStageState (0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	d3d_Device->SetTextureStageState (0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	d3d_Device->SetTextureStageState (0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	d3d_Device->SetTextureStageState (0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

	// second stage is the lightmap (set the modulate mode according to the lightmap format we're using
	d3d_Device->SetTextureStageState (1, D3DTSS_COLOROP, d3d_GlobalCaps.AllowA16B16G16R16 ? D3DTOP_MODULATE4X : D3DTOP_MODULATE2X);
	d3d_Device->SetTextureStageState (1, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	d3d_Device->SetTextureStageState (1, D3DTSS_COLORARG2, D3DTA_CURRENT);

	// disable subsequent stages
	d3d_Device->SetTextureStageState (2, D3DTSS_COLOROP, D3DTOP_DISABLE);
	d3d_Device->SetTextureStageState (2, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

	for (i = 0; i < cl.worldmodel->numtextures; i++)
	{
		// no texture (e2m3 gets this)
		if (!(t = cl.worldmodel->textures[i])) continue;

		// nothing to draw for this texture
		if (!(s = t->texturechain)) continue;

		// skip over
		if (s->flags & SURF_DRAWTURB) continue;
		if (s->flags & SURF_DRAWSKY) continue;

		D3D_BindTexture (0, (LPDIRECT3DTEXTURE9) t->d3d_Texture);

		for (; s; s = s->texturechain)
		{
			// bind the lightmap
			// we can't send it through bind texture as only d3d_rlight knows about the lightmap data type
			D3D_BindLightmap (1, s->d3d_Lightmap);

			// draw the surface
			d3d_Device->DrawPrimitive (D3DPT_TRIANGLEFAN, s->vboffset, s->numedges - 2);
		}
	}

	// now we draw all of the brush models while we have the correct state up
	D3D_DrawBrushModels ();

	// disable stages after 1
	d3d_Device->SetTextureStageState (1, D3DTSS_COLOROP, D3DTOP_DISABLE);
	d3d_Device->SetTextureStageState (1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
}


void R_ClearTextureChains (void)
{
	texture_t *t;
	int i;

	for (i = 0; i < cl.worldmodel->numtextures; i++)
	{
		if (!(t = cl.worldmodel->textures[i])) continue;

		// null it
		t->texturechain = NULL;
	}

	// sky as well
	skychain = NULL;
}


void D3D_AddInlineBmodelsToDrawLists (void)
{
	if (!r_drawentities.value) return;

	for (int x = 0; x < cl_numvisedicts; x++)
	{
		// R_TextureAnimation needs this
		entity_t *e = cl_visedicts[x];

		// only doing brushes here
		if (e->model->type != mod_brush) continue;

		// don't do inline models during this pass - they will have already been merged into the world render
		if (e->model->name[0] != '*') continue;

		int i;
		model_t *clmodel = e->model;
		bool rotated;
		vec3_t mins, maxs;
		msurface_t *surf;

		if (e->angles[0] || e->angles[1] || e->angles[2])
		{
			rotated = true;

			for (i = 0; i < 3; i++)
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

		if (R_CullBox (mins, maxs))
		{
			e->visframe = -1;
			continue;
		}

		// the entity is visible now
		e->visframe = r_framecount;

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

		// calculate dynamic lighting for bmodel
		R_PushDlights (clmodel->nodes + clmodel->hulls[0].firstclipnode);

		bool warptransform = false;

		// mark the surfaces
		for (i = 0, surf = &clmodel->surfaces[clmodel->firstmodelsurface]; i < clmodel->nummodelsurfaces; i++, surf++)
		{
			// initially not visible
			surf->visframe = -1;

			// find which side of the node we are on
			float dot = DotProduct (modelorg, surf->plane->normal) - surf->plane->dist;

			// draw the polygon
			if (((surf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) || (!(surf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON)))
			{
				// mark as visible
				surf->visframe = r_framecount;

				// no lightmaps on these
				if ((surf->flags & SURF_DRAWTURB) || (surf->flags & SURF_DRAWSKY))
				{
					// mark that we need to transform warps
					warptransform = true;
					continue;
				}

				// check for lightmap modifications
				D3D_CheckLightmapModification (surf);
			}
		}

		// now transform any warps we got
		if (!warptransform) continue;

		// get the transformed matrix for this model
		d3d_WorldMatrixStack->Push ();
		d3d_WorldMatrixStack->LoadIdentity ();
		d3d_WorldMatrixStack->TranslateLocal (e->origin[0], e->origin[1], e->origin[2]);
		d3d_WorldMatrixStack->RotateAxisLocal (&ZVECTOR, D3DXToRadian (e->angles[1]));
		d3d_WorldMatrixStack->RotateAxisLocal (&YVECTOR, D3DXToRadian (-e->angles[0]));
		d3d_WorldMatrixStack->RotateAxisLocal (&XVECTOR, D3DXToRadian (e->angles[2]));
		D3DMATRIX *m = d3d_WorldMatrixStack->GetTop ();
		d3d_WorldMatrixStack->Pop ();

		// do another pass through the surfs to transform them
		for (i = 0, surf = &clmodel->surfaces[clmodel->firstmodelsurface]; i < clmodel->nummodelsurfaces; i++, surf++)
		{
			// not visible
			if (surf->visframe != r_framecount) continue;

			if (surf->flags & SURF_DRAWTURB)
			{
				// transform the surface
				D3D_TransformWarpSurface (surf, m);

				// get the correct texture for it
				texture_t *t = R_TextureAnimation (surf->texinfo->texture);

				// add to the appropriate chain
				surf->texturechain = t->texturechain;
				t->texturechain = surf;
			}

			if (surf->flags & SURF_DRAWSKY)
			{
				// transform the surface
				D3D_TransformWarpSurface (surf, m);

				// add to the appropriate chain
				surf->texturechain = skychain;
				skychain = surf;
			}
		}
	}
}


/*
=============================================================

	WORLD MODEL

=============================================================
*/

/*
================
R_RecursiveWorldNode
================
*/
void R_RecursiveWorldNode (mnode_t *node)
{
	int			c, side;
	mplane_t	*plane;
	msurface_t	*surf, **mark;
	mleaf_t		*pleaf;
	double		dot;

	if (node->contents == CONTENTS_SOLID)
		return;		// solid

	if (node->visframe != r_visframecount)
		return;
	if (R_CullBox (node->minmaxs, node->minmaxs+3))
		return;
	
// if a leaf node, draw stuff
	if (node->contents < 0)
	{
		pleaf = (mleaf_t *)node;

		mark = pleaf->firstmarksurface;
		c = pleaf->nummarksurfaces;

		if (c)
		{
			do
			{
				(*mark)->visframe = r_framecount;
				mark++;
			} while (--c);
		}

	// deal with model fragments in this leaf
		if (pleaf->efrags)
			R_StoreEfrags (&pleaf->efrags);

		return;
	}

// node is just a decision point, so go down the apropriate sides

// find which side of the node we are on
	plane = node->plane;

	switch (plane->type)
	{
	case PLANE_X:
		dot = modelorg[0] - plane->dist;
		break;
	case PLANE_Y:
		dot = modelorg[1] - plane->dist;
		break;
	case PLANE_Z:
		dot = modelorg[2] - plane->dist;
		break;
	default:
		dot = DotProduct (modelorg, plane->normal) - plane->dist;
		break;
	}

	if (dot >= 0)
		side = 0;
	else
		side = 1;

// recurse down the children, front side first
	R_RecursiveWorldNode (node->children[side]);

// draw stuff
	c = node->numsurfaces;

	if (c)
	{
		surf = cl.worldmodel->surfaces + node->firstsurface;

		if (dot < 0 -BACKFACE_EPSILON)
			side = SURF_PLANEBACK;
		else if (dot > BACKFACE_EPSILON)
			side = 0;
		{
			for ( ; c ; c--, surf++)
			{
				if (surf->visframe != r_framecount)
					continue;

				// don't backface underwater surfaces, because they warp
				if ( !(surf->flags & SURF_UNDERWATER) && ( (dot < 0) ^ !!(surf->flags & SURF_PLANEBACK)) )
					continue;		// wrong side

				// get the correct animation sequence
				texture_t *tex = R_TextureAnimation (surf->texinfo->texture);

				// if sorting by texture, just store it out
				if (surf->flags & SURF_DRAWSKY)
				{
					// link it in
					surf->texturechain = skychain;
					skychain = surf;
				}
				else if (surf->flags & SURF_DRAWTURB)
				{
					// link it in
					surf->texturechain = tex->texturechain;
					tex->texturechain = surf;
				}
				else
				{
					// check for lightmap modifications
					D3D_CheckLightmapModification (surf);

					// link it in
					surf->texturechain = tex->texturechain;
					tex->texturechain = surf;
				}
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
	currenttexture = -1;

	// clear down the texture chains
	R_ClearTextureChains ();

	// calculate dynamic lighting for the world
	R_PushDlights (cl.worldmodel->nodes);

	// build the world
	R_RecursiveWorldNode (cl.worldmodel->nodes);

	// add inline models
	D3D_AddInlineBmodelsToDrawLists ();

	// unlock all lightmaps that were previously locked
	D3D_UnlockLightmaps ();

	// draw sky
	R_DrawSkyChain (skychain);

	// draw textures
	DrawTextureChains ();
}


/*
===============
R_MarkLeaves
===============
*/
void R_MarkLeaves (void)
{
	byte	*vis;
	mnode_t	*node;
	int		i;
	byte	solid[4096];

	if (r_oldviewleaf == r_viewleaf && !r_novis.value)
		return;

	if (mirror)
		return;

	r_visframecount++;
	r_oldviewleaf = r_viewleaf;

	if (r_novis.value)
	{
		vis = solid;
		memset (solid, 0xff, (cl.worldmodel->numleafs+7)>>3);
	}
	else
		vis = Mod_LeafPVS (r_viewleaf, cl.worldmodel);
		
	for (i=0 ; i<cl.worldmodel->numleafs ; i++)
	{
		if (vis[i>>3] & (1<<(i&7)))
		{
			node = (mnode_t *)&cl.worldmodel->leafs[i+1];
			do
			{
				if (node->visframe == r_visframecount)
					break;
				node->visframe = r_visframecount;
				node = node->parent;
			} while (node);
		}
	}
}


void D3D_BuildSurfaceVertexBuffer (msurface_t *surf, model_t *mod, worldvert_t *vdest)
{
	int			i, lindex, lnumverts;
	medge_t		*pedges, *r_pedge;
	float		*vec;
	float		s, t;
	mvertex_t	*vertbase = mod->vertexes;

	// reconstruct the polygon
	pedges = mod->edges;
	lnumverts = surf->numedges;

	// go through all of the verts
	for (i = 0; i < surf->numedges; i++)
	{
		// build vertexes
		lindex = mod->surfedges[surf->firstedge + i];

		if (lindex > 0)
		{
			r_pedge = &pedges[lindex];
			vec = vertbase[r_pedge->v[0]].position;
		}
		else
		{
			r_pedge = &pedges[-lindex];
			vec = vertbase[r_pedge->v[1]].position;
		}

		// copy verts in
		vdest->xyz[0] = vec[0];
		vdest->xyz[1] = vec[1];
		vdest->xyz[2] = vec[2];

		// build texcoords
		s = DotProduct (vec, surf->texinfo->vecs[0]) + surf->texinfo->vecs[0][3];
		s /= surf->texinfo->texture->width;

		t = DotProduct (vec, surf->texinfo->vecs[1]) + surf->texinfo->vecs[1][3];
		t /= surf->texinfo->texture->height;

		// copy them in
		vdest->st[0] = s;
		vdest->st[1] = t;

		// make lightmap coords
		D3D_MakeLightmapTexCoords (surf, vec, vdest->lm);

		// go to next
		vdest++;
	}
}


void D3D_PutSurfacesInVertexBuffer (void)
{
	// release it if it already exists
	SAFE_RELEASE (d3d_BrushModelVerts);

	// create our new vertex buffer
	HRESULT hr = d3d_Device->CreateVertexBuffer
	(
		d3d_VertexBufferVerts * sizeof (worldvert_t),
		0,
		(D3DFVF_XYZ | D3DFVF_TEX2),
		D3DPOOL_MANAGED,
		&d3d_BrushModelVerts,
		NULL
	);

	// fixme - fall back on DrawPrimitiveUP if this happens!
	if (FAILED (hr)) Sys_Error ("D3D_CreateMeAVertexBuffer: d3d_Device->CreateVertexBuffer failed");

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

		msurface_t *surf = m->surfaces;

		for (int i = 0; i < m->numsurfaces; i++, surf++)
		{
			// skip
			if (surf->flags & SURF_DRAWTURB) continue;
			if (surf->flags & SURF_DRAWSKY) continue;

			// build the vertex buffer for this surf
			D3D_BuildSurfaceVertexBuffer (surf, m, verts);

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
void GL_BuildLightmaps (void)
{
	int		i, j;
	model_t	*m;

	r_framecount = 1;		// no dlightcache

	// clear down previous lightmaps
	D3D_ReleaseLightmaps ();

	// no verts yet
	d3d_VertexBufferVerts = 0;

	for (j = 1; j < MAX_MODELS; j++)
	{
		m = cl.model_precache[j];

		if (!m) break;
		if (m->name[0] == '*') continue;
		if (m->type != mod_brush) continue;

		msurface_t *surf = m->surfaces;

		for (int i = 0; i < m->numsurfaces; i++, surf++)
		{
			// skip these
			if (surf->flags & SURF_DRAWTURB) continue;
			if (surf->flags & SURF_DRAWSKY) continue;

			// create the lightmap for the surface
			D3D_CreateSurfaceLightmap (surf);

			// increment vertex count
			d3d_VertexBufferVerts += surf->numedges;

			// no polys on these surfs
			surf->polys = NULL;
		}
	}

	// put all the surfaces into vertex buffers
	D3D_PutSurfacesInVertexBuffer ();

	// upload all lightmaps
	D3D_UploadLightmaps ();
}

