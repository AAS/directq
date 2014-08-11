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
// gl_mesh.c: triangle model functions

#include "quakedef.h"
#include "d3d_model.h"
#include "d3d_quake.h"
#include "resource.h"

void R_LightPoint (entity_t *e, float *c);

// up to 16 color translated skins
image_t d3d_PlayerSkins[256];

void D3D_RotateForEntity (entity_t *e);
void D3D_RotateForEntity (entity_t *e, D3DMATRIX *m);

typedef struct aliaspolyvert_s
{
	float xyz[3];
	D3DCOLOR color;
	float st[2];
} aliaspolyvert_t;


/*
=================================================================

ALIAS MODEL DISPLAY LIST GENERATION

=================================================================
*/


/*
================
GL_MakeAliasModelDisplayLists

Generate a list of trifans or strips
for the model, which holds for all frames
================
*/
void GL_MakeAliasModelDisplayLists (aliashdr_t *hdr, stvert_t *stverts, dtriangle_t *triangles)
{
	// no verts to begin with
	hdr->meshverts = NULL;
	hdr->nummesh = 0;

	// initialize indexes; these will be filled in as the model is loaded
	hdr->indexes = (unsigned short *) Pool_Cache->Alloc (sizeof (unsigned short) * hdr->numtris * 3);
	hdr->numindexes = 0;

	for (int i = 0; i < hdr->numtris; i++)
	{
		for (int j = 0; j < 3; j++)
		{
			int v;

			// index into hdr->vertexes
			unsigned short vertindex = triangles[i].vertindex[j];

			// basic s/t coords
			int s = stverts[vertindex].s;
			int t = stverts[vertindex].t;

			// check for back side and adjust texcoord s
			if (!triangles[i].facesfront && stverts[vertindex].onseam) s += hdr->skinwidth / 2;

			// see does this vert already exist
			for (v = 0; v < hdr->nummesh; v++)
			{
				// it could use the same xyz but have different s and t
				if (hdr->meshverts[v].vertindex == vertindex && (int) hdr->meshverts[v].s == s && (int) hdr->meshverts[v].t == t)
				{
					// exists; emit an index for it
					hdr->indexes[hdr->numindexes++] = v;
					break;
				}
			}

			if (v == hdr->nummesh)
			{
				// doesn't exist; emit a new vert and index
				// consecutive memory trick here...
				if (!hdr->meshverts)
					hdr->meshverts = (aliasmesh_t *) Pool_Cache->Alloc (sizeof (aliasmesh_t));
				else Pool_Cache->Alloc (sizeof (aliasmesh_t));

				hdr->indexes[hdr->numindexes++] = hdr->nummesh;

				hdr->meshverts[hdr->nummesh].vertindex = vertindex;
				hdr->meshverts[hdr->nummesh].s = s;
				hdr->meshverts[hdr->nummesh++].t = t;
			}
		}
	}

	// tidy up the verts by calculating final s and t
	for (int i = 0; i < hdr->nummesh; i++)
	{
		hdr->meshverts[i].s = ((float) hdr->meshverts[i].s + 0.5f) / (float) hdr->skinwidth;
		hdr->meshverts[i].t = ((float) hdr->meshverts[i].t + 0.5f) / (float) hdr->skinheight;
	}

	// not delerped yet (view models only)
	hdr->mfdelerp = false;
}


/*
=============================================================

  ALIAS MODELS

=============================================================
*/


// precalculated dot products for quantized angles
// instead of doing interpolation at run time we precalc lerped dots
// they're still somewhat quantized but less so (to less than a degree)
#define SHADEDOT_QUANT 16
#define SHADEDOT_QUANT_LERP 512
float **r_avertexnormal_dots_lerp = NULL;


extern cvar_t r_maxvertexsubmission;

float Mesh_ScaleVert (aliashdr_t *hdr, drawvertx_t *invert, int index)
{
	float outvert = invert->v[index];

	outvert *= hdr->scale[index];
	outvert += hdr->scale_origin[index];

	return outvert;
}


/*
===================
DelerpMuzzleFlashes

Done at runtime (once only per model) because there is no guarantee that a viewmodel
will follow the naming convention used by ID.  As a result, the only way we have to
be certain that a model is a viewmodel is when we come to draw the viewmodel.
===================
*/
void DelerpMuzzleFlashes (aliashdr_t *hdr)
{
	// already delerped
	if (hdr->mfdelerp) return;

	// done now
	hdr->mfdelerp = true;

	// get pointers to the verts
	drawvertx_t *vertsf0 = hdr->vertexes[0];
	drawvertx_t *vertsf1 = hdr->vertexes[1];
	drawvertx_t *vertsfi;

	// now go through them and compare.  we expect that (a) the animation is sensible and there's no major
	// difference between the 2 frames to be expected, and (b) any verts that do exhibit a major difference
	// can be assumed to belong to the muzzleflash
	for (int j = 0; j < hdr->vertsperframe; j++, vertsf0++, vertsf1++)
	{
		// get difference in front to back movement
		float vdiff = Mesh_ScaleVert (hdr, vertsf1, 0) - Mesh_ScaleVert (hdr, vertsf0, 0);

		// if it's above a certain treshold, assume a muzzleflash and mark for nolerp
		// 10 is the approx lowest range of visible front to back in a view model, so that seems reasonable to work with
		if (vdiff > 10)
			vertsf0->lerpvert = false;
		else vertsf0->lerpvert = true;
	}

	// now we mark every other vert in the model as per the instructions in the first frame
	for (int i = 1; i < hdr->numframes; i++)
	{
		// get pointers to the verts
		vertsf0 = hdr->vertexes[0];
		vertsfi = hdr->vertexes[i];

		for (int j = 0; j < hdr->vertsperframe; j++, vertsf0++, vertsfi++)
		{
			// just copy it across
			vertsfi->lerpvert = vertsf0->lerpvert;
		}
	}
}



__inline void D3D_LerpVert (aliaspolyvert_t *dest, aliasmesh_t *av, aliasstate_t *aliasstate, drawvertx_t *verts1, drawvertx_t *verts2, D3DMATRIX *m = NULL)
{
	verts1 += av->vertindex;
	verts2 += av->vertindex;

	float vert[3];

	if (verts1->lerpvert)
	{
		vert[0] = (float) verts1->v[0] * aliasstate->frontlerp + (float) verts2->v[0] * aliasstate->backlerp;
		vert[1] = (float) verts1->v[1] * aliasstate->frontlerp + (float) verts2->v[1] * aliasstate->backlerp;
		vert[2] = (float) verts1->v[2] * aliasstate->frontlerp + (float) verts2->v[2] * aliasstate->backlerp;
	}
	else if (aliasstate->backlerp > aliasstate->frontlerp)
	{
		vert[0] = verts2->v[0];
		vert[1] = verts2->v[1];
		vert[2] = verts2->v[2];
	}
	else
	{
		vert[0] = verts1->v[0];
		vert[1] = verts1->v[1];
		vert[2] = verts1->v[2];
	}

	if (m)
	{
		dest->xyz[0] = vert[0] * m->_11 + vert[1] * m->_21 + vert[2] * m->_31 + m->_41;
		dest->xyz[1] = vert[0] * m->_12 + vert[1] * m->_22 + vert[2] * m->_32 + m->_42;
		dest->xyz[2] = vert[0] * m->_13 + vert[1] * m->_23 + vert[2] * m->_33 + m->_43;
	}
	else
	{
		dest->xyz[0] = vert[0];
		dest->xyz[1] = vert[1];
		dest->xyz[2] = vert[2];
	}

	// do texcoords here too
	dest->st[0] = av->s;
	dest->st[1] = av->t;
}


__inline void D3D_LerpLight (aliaspolyvert_t *dest, entity_t *e, aliasmesh_t *av, aliasstate_t *aliasstate, drawvertx_t *verts1, drawvertx_t *verts2)
{
	float l;

	verts1 += av->vertindex;
	verts2 += av->vertindex;

	// pre-interpolated shadedots
	if (verts1->lerpvert)
		l = (aliasstate->shadedots[verts1->lightnormalindex] * aliasstate->frontlerp + aliasstate->shadedots[verts2->lightnormalindex] * aliasstate->backlerp);
	else if (aliasstate->backlerp > aliasstate->frontlerp)
		l = aliasstate->shadedots[verts2->lightnormalindex];
	else l = aliasstate->shadedots[verts1->lightnormalindex];

	// set light color
	dest->color = D3DCOLOR_ARGB
	(
		BYTE_CLAMP (e->alphaval),
		vid.lightmap[BYTE_CLAMP (l * e->shadelight[0])],
		vid.lightmap[BYTE_CLAMP (l * e->shadelight[1])],
		vid.lightmap[BYTE_CLAMP (l * e->shadelight[2])]
	);
}


extern vec3_t lightspot;
extern mplane_t *lightplane;

void D3D_DrawAliasShadows (entity_t **ents, int numents)
{
	// this is a hack to get around a non-zero r_shadows being triggered by a combination of r_shadows 0 and
	// low precision FP rounding errors, thereby causing unnecesary slowdowns.
	byte shadealpha = BYTE_CLAMP (r_shadows.value * 255.0f);

	if (shadealpha < 1) return;

	bool stateset = false;

	DWORD shadecolor = D3DCOLOR_ARGB (shadealpha, 0, 0, 0);

	unsigned short *aliasindexes = NULL;
	aliaspolyvert_t *aliasverts = NULL;

	D3D_BeginVertexes ((void **) &aliasverts, (void **) &aliasindexes, sizeof (aliaspolyvert_t));
	int numverts = 0;
	int numindexes = 0;

	for (int i = 0; i < numents; i++)
	{
		entity_t *ent = ents[i];

		if (ent->alphaval < 255) continue;
		if (ent->visframe != d3d_RenderDef.framecount) continue;

		// easier access
		aliasstate_t *aliasstate = &ent->aliasstate;
		aliashdr_t *hdr = ent->model->aliashdr;

		// don't crash
		if (!aliasstate->lightplane) continue;

		if (!stateset)
		{
			// state for shadows
			D3D_SetRenderState (D3DRS_ZWRITEENABLE, FALSE);
			D3D_SetRenderState (D3DRS_ALPHABLENDENABLE, TRUE);
			D3D_SetRenderState (D3DRS_BLENDOP, D3DBLENDOP_ADD);
			D3D_SetRenderState (D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
			D3D_SetRenderState (D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

			D3D_SetVBOColorMode (0, D3DTOP_DISABLE);
			D3D_SetVBOAlphaMode (0, D3DTOP_MODULATE);

			D3D_SetFVF (D3DFVF_XYZ | D3DFVF_DIFFUSE);

			if (d3d_GlobalCaps.DepthStencilFormat == D3DFMT_D24S8)
			{
				// of course, we all know that Direct3D lacks Stencil Buffer and Polygon Offset support,
				// so what you're looking at here doesn't really exist.  Those of you who froth at the mouth
				// and like to think it's still the 1990s had probably better look away now.
				D3D_SetRenderState (D3DRS_STENCILENABLE, TRUE);
				D3D_SetRenderState (D3DRS_STENCILFUNC, D3DCMP_EQUAL);
				D3D_SetRenderState (D3DRS_STENCILREF, 0x00000001);
				D3D_SetRenderState (D3DRS_STENCILMASK, 0x00000002);
				D3D_SetRenderState (D3DRS_STENCILWRITEMASK, 0xFFFFFFFF);
				D3D_SetRenderState (D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP);
				D3D_SetRenderState (D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP);
				D3D_SetRenderState (D3DRS_STENCILPASS, D3DSTENCILOP_INCRSAT);
			}

			stateset = true;
		}

		if (!D3D_CheckVBO (hdr->nummesh, hdr->numindexes))
		{
			// end this VBO batch and begin a new one
			D3D_EndVertexes ();
			D3D_BeginVertexes ((void **) &aliasverts, (void **) &aliasindexes, sizeof (aliaspolyvert_t));
			numverts = 0;
			numindexes = 0;
		}

		// build the transformation for this entity.
		// we need to run this in software as we are now potentially submitting multiple alias models in a single batch
		D3D_LoadIdentity (&ent->matrix);
		D3D_TranslateMatrix (&ent->matrix, ent->origin[0], ent->origin[1], ent->origin[2]);
		D3D_RotateMatrix (&ent->matrix, 0, 0, 1, ent->angles[1]);

		// the scaling needs to be included at this time
		D3D_TranslateMatrix (&ent->matrix, hdr->scale_origin[0], hdr->scale_origin[1], hdr->scale_origin[2]);
		D3D_ScaleMatrix (&ent->matrix, hdr->scale[0], hdr->scale[1], hdr->scale[2]);

		for (int i = 0; i < hdr->nummesh; i++, aliasverts++)
		{
			D3D_LerpVert (aliasverts, &hdr->meshverts[i], aliasstate, hdr->vertexes[ent->pose1], hdr->vertexes[ent->pose2], &ent->matrix);

			// flatten
			aliasverts->xyz[2] = aliasstate->lightspot[2] + 0.1f;

			// shadow colour is constant
			aliasverts->color = shadecolor;
		}

		for (int i = 0; i < hdr->numindexes; i++)
			aliasindexes[numindexes++] = hdr->indexes[i] + numverts;

		// accumulate vert counts
		numverts += hdr->nummesh;
		D3D_UpdateVBOMark (hdr->nummesh, hdr->numindexes);
		d3d_RenderDef.alias_polys += hdr->numtris;
	}

	// required even if there is nothing so that the buffers will unlock
	D3D_EndVertexes ();

	if (stateset)
	{
		// back to normal
		if (d3d_GlobalCaps.DepthStencilFormat == D3DFMT_D24S8)
			D3D_SetRenderState (D3DRS_STENCILENABLE, FALSE);

		D3D_SetRenderState (D3DRS_ALPHABLENDENABLE, FALSE);
		D3D_SetRenderState (D3DRS_ZWRITEENABLE, TRUE);
		D3D_SetTextureState (0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
		D3D_SetTextureState (0, D3DTSS_COLOROP, D3DTOP_DISABLE);
	}
}


/*
=================
D3D_SetupAliasFrame

=================
*/
extern cvar_t r_lerpframe;

float D3D_SetupAliasFrame (entity_t *e, aliashdr_t *hdr)
{
	int pose, numposes;
	float interval;
	bool lerpmdl = !hdr->nolerp;
	float blend;

	// silently revert to frame 0
	if ((e->frame >= hdr->numframes) || (e->frame < 0)) e->frame = 0;

	pose = hdr->frames[e->frame].firstpose;
	numposes = hdr->frames[e->frame].numposes;

	if (numposes > 1)
	{
		interval = hdr->frames[e->frame].interval;
		pose += (int) (cl.time / interval) % numposes;

		// not really needed for non-interpolated mdls, but does no harm
		e->frame_interval = interval;
	}
	else e->frame_interval = 0.1;

	// conditions for turning lerping off
	if (hdr->nummeshframes == 1) lerpmdl = false;			// only one pose
	if (e == &cl.viewent && e->frame == 0) lerpmdl = false;	// super-nailgun spinning down bug
	if (e->pose1 == e->pose2) lerpmdl = false;				// both frames are identical
	if (!r_lerpframe.value) lerpmdl = false;

	// interpolation - this code is a total BITCH and needs to be replaced with the Q2 interpolation code sometime REAL soon
	if (e->pose1 == e->pose2 && e->pose2 == 0 && e != &cl.viewent)
	{
		// "dying throes" interpolation bug - begin a new sequence with both poses the same
		// this happens when an entity is spawned client-side
		e->frame_start_time = cl.time;
		e->pose1 = e->pose2 = pose;
		blend = 0;
	}
	else if (e->pose2 != pose || !lerpmdl)
	{
		// begin a new interpolation sequence
		e->frame_start_time = cl.time;
		e->pose1 = e->pose2;
		e->pose2 = pose;
		blend = 0;
	}
	else blend = (cl.time - e->frame_start_time) / e->frame_interval;

	// if a viewmodel is switched and the previous had a current frame number higher than the number of frames
	// in the new one, DirectQ will crash so we need to fix that.  this is also a general case sanity check.
	if (e->pose1 >= hdr->nummeshframes) e->pose1 = e->pose2 = 0; else if (e->pose1 < 0) e->pose1 = e->pose2 = hdr->nummeshframes - 1;
	if (e->pose2 >= hdr->nummeshframes) e->pose1 = e->pose2 = 0; else if (e->pose2 < 0) e->pose1 = e->pose2 = hdr->nummeshframes - 1;

	// don't let blend pass 1
	if (cl.paused || blend > 1.0) blend = 1.0;

	return blend;
}


void D3D_DrawAliasModel (entity_t *ent);

void D3D_SetupAliasModel (entity_t *ent)
{
	vec3_t mins, maxs;

	// take pointers for easier access
	aliashdr_t *hdr = ent->model->aliashdr;
	aliasstate_t *aliasstate = &ent->aliasstate;

	// assume that the model has been culled
	ent->visframe = -1;

	// revert to full model culling here
	VectorAdd (ent->origin, ent->model->mins, mins);
	VectorAdd (ent->origin, ent->model->maxs, maxs);

	// the gun or the chase model are never culled away
	if (ent == cl_entities[cl.viewentity] && chase_active.value)
		;	// no bbox culling on certain entities
	else if (ent->nocullbox)
		;	// no bbox culling on certain entities
	else if (R_CullBox (mins, maxs))
	{
		// if it was culled set it's occlusion status to false so that next time it comes into the view frustum
		// it will be visible until such a time as we prove otherwise
		ent->occluded = false;

		// no further setup needed
		return;
	}

	// the model has not been culled now
	ent->visframe = d3d_RenderDef.framecount;
	aliasstate->lightplane = NULL;

	extern bool chase_nodraw;

	if (ent == cl_entities[cl.viewentity] && chase_active.value)
	{
		// don't draw if too close
		if (chase_nodraw) return;

		// adjust angles (done here instead of chase_update as we only want the angle for rendering to be adjusted)
		ent->angles[0] *= 0.3;
	}

	// setup the frame for drawing and store the interpolation blend
	aliasstate->backlerp = D3D_SetupAliasFrame (ent, hdr);
	aliasstate->frontlerp = 1.0f - aliasstate->backlerp;

	// get lighting information
	vec3_t shadelight;
	R_LightPoint (ent, shadelight);

	// average light between frames
	for (int i = 0; i < 3; i++)
	{
		shadelight[i] = (shadelight[i] + ent->shadelight[i]) / 2;
		ent->shadelight[i] = shadelight[i];
	}

	// precomputed and pre-interpolated shading dot products
	aliasstate->shadedots = r_avertexnormal_dots_lerp[((int) (ent->angles[1] * (SHADEDOT_QUANT_LERP / 360.0))) & (SHADEDOT_QUANT_LERP - 1)];

	// store out for shadows
	VectorCopy (lightspot, aliasstate->lightspot);
	aliasstate->lightplane = lightplane;

	// get texturing info
	int anim = (int) (cl.time * 10) & 3;

	// base texture
	aliasstate->teximage = hdr->skins[ent->skinnum].texture[anim];

	// switch player skin (entnum - 1 is the same playernum as is used for calling into D3D_TranslatePlayerSkin so it's valid)
	// need to make sure that it actually *is* a player model in case a head model uses the same entity number
	if (ent->colormap != vid.colormap &&
		!gl_nocolors.value &&
		ent->entnum >= 1 &&
		ent->entnum <= cl.maxclients &&
		(ent->model->flags & EF_PLAYER))
		aliasstate->teximage = &d3d_PlayerSkins[cl.scores[ent->entnum - 1].colors];

	// fullbright texture (can be NULL)
	aliasstate->lumaimage = hdr->skins[ent->skinnum].fullbright[anim];
}


void R_DrawBBox (entity_t *ent)
{
	vec3_t bboxverts[8];

	unsigned short bboxindexes[36] =
	{
		0, 2, 6, 0, 6, 4,
		1, 3, 7, 1, 7, 5,
		0, 1, 3, 0, 3, 2,
		4, 5, 7, 4, 7, 6,
		0, 1, 5, 0, 5, 4,
		2, 3, 7, 2, 7, 6
	};

	bboxverts[0][0] = ent->origin[0] + ent->model->mins[0];
	bboxverts[0][1] = ent->origin[1] + ent->model->mins[1];
	bboxverts[0][2] = ent->origin[2] + ent->model->mins[2];

	bboxverts[1][0] = ent->origin[0] + ent->model->mins[0];
	bboxverts[1][1] = ent->origin[1] + ent->model->mins[1];
	bboxverts[1][2] = ent->origin[2] + ent->model->maxs[2];

	bboxverts[2][0] = ent->origin[0] + ent->model->mins[0];
	bboxverts[2][1] = ent->origin[1] + ent->model->maxs[1];
	bboxverts[2][2] = ent->origin[2] + ent->model->mins[2];

	bboxverts[3][0] = ent->origin[0] + ent->model->mins[0];
	bboxverts[3][1] = ent->origin[1] + ent->model->maxs[1];
	bboxverts[3][2] = ent->origin[2] + ent->model->maxs[2];

	bboxverts[4][0] = ent->origin[0] + ent->model->maxs[0];
	bboxverts[4][1] = ent->origin[1] + ent->model->mins[1];
	bboxverts[4][2] = ent->origin[2] + ent->model->mins[2];

	bboxverts[5][0] = ent->origin[0] + ent->model->maxs[0];
	bboxverts[5][1] = ent->origin[1] + ent->model->mins[1];
	bboxverts[5][2] = ent->origin[2] + ent->model->maxs[2];

	bboxverts[6][0] = ent->origin[0] + ent->model->maxs[0];
	bboxverts[6][1] = ent->origin[1] + ent->model->maxs[1];
	bboxverts[6][2] = ent->origin[2] + ent->model->mins[2];

	bboxverts[7][0] = ent->origin[0] + ent->model->maxs[0];
	bboxverts[7][1] = ent->origin[1] + ent->model->maxs[1];
	bboxverts[7][2] = ent->origin[2] + ent->model->maxs[2];

	d3d_Device->DrawIndexedPrimitiveUP (D3DPT_TRIANGLELIST, 0, 8, 12, bboxindexes, D3DFMT_INDEX16, bboxverts, sizeof (vec3_t));
	d3d_RenderDef.alias_polys++;
}


void D3D_DrawAliasBatch (entity_t **ents, int numents)
{
	aliaspolyvert_t *aliasverts;
	unsigned short *aliasindexes;

	// conditions for a switch
	int cachedtexture = 0;
	int cachedfullbright = 0;
	int numverts = 0;
	int numindexes = 0;

	// even if the ent has alpha we're modulating color with diffuse and diffuse will have alpha so
	// we don't need an explicit alpha mode
	D3D_SetTextureAlphaMode (0, D3DTOP_DISABLE);
	D3D_SetTextureAlphaMode (1, D3DTOP_DISABLE);
	D3D_SetTextureColorMode (2, D3DTOP_DISABLE);
	D3D_SetTextureAlphaMode (2, D3DTOP_DISABLE);

	D3D_SetTextureAddressMode (D3DTADDRESS_CLAMP, D3DTADDRESS_CLAMP);
	D3D_SetTexCoordIndexes (0, 0);
	D3D_SetTextureMipmap (0, d3d_3DFilterMag, d3d_3DFilterMin, d3d_3DFilterMip);
	D3D_SetTextureMipmap (1, d3d_3DFilterMag, d3d_3DFilterMin, d3d_3DFilterMip);

	D3D_SetRenderState (D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
	D3D_SetFVF (D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);

	// begin a new render batch
	D3D_GetVertexBufferSpace ((void **) &aliasverts);
	D3D_GetIndexBufferSpace ((void **) &aliasindexes);

	for (int i = 0; i < numents; i++)
	{
		entity_t *ent = ents[i];
		aliashdr_t *hdr = ent->model->aliashdr;

		// skip conditions
		if (ent->visframe != d3d_RenderDef.framecount) continue;
		if (ent->occluded) continue;

		// take pointers for easier access
		aliasstate_t *aliasstate = &ent->aliasstate;

		// prydon gets this
		if (!aliasstate->teximage) continue;
		if (!aliasstate->teximage->d3d_Texture) continue;

		// a change of texture signifies a new batch
		if ((int) aliasstate->teximage != cachedtexture || (int) aliasstate->lumaimage != cachedfullbright || 
			D3D_AreBuffersFull (numverts + hdr->nummesh, numindexes + hdr->numindexes))
		{
			if (numverts || numindexes)
			{
				D3D_SubmitVertexes (numverts, numindexes, sizeof (aliaspolyvert_t));
				D3D_GetVertexBufferSpace ((void **) &aliasverts);
				D3D_GetIndexBufferSpace ((void **) &aliasindexes);
				numverts = 0;
				numindexes = 0;
			}

			// switch textures (if required) and begin a new batch
			if (aliasstate->lumaimage)
			{
				D3D_SetTextureColorMode (0, D3DTOP_ADD, D3DTA_TEXTURE, D3DTA_DIFFUSE);
				D3D_SetTextureColorMode (1, D3D_OVERBRIGHT_MODULATE, D3DTA_TEXTURE, D3DTA_CURRENT);

				// luma pass
				D3D_SetTexture (0, aliasstate->lumaimage->d3d_Texture);
				D3D_SetTexture (1, aliasstate->teximage->d3d_Texture);
			}
			else
			{
				// straight up modulation
				D3D_SetTextureColorMode (0, D3D_OVERBRIGHT_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE);
				D3D_SetTextureColorMode (1, D3DTOP_DISABLE);

				// bind texture for baseline skin
				D3D_SetTexture (0, aliasstate->teximage->d3d_Texture);
			}

			// reset/rewind/etc
			cachedtexture = (int) aliasstate->teximage;
			cachedfullbright = (int) aliasstate->lumaimage;
		}

		// build the transformation for this entity.
		// we need to run this in software as we are now potentially submitting multiple alias models in a single batch
		D3D_LoadIdentity (&ent->matrix);
		D3D_RotateForEntity (ent, &ent->matrix);

		// submit this entity to the renderer
		for (int i = 0; i < hdr->nummesh; i++, aliasverts++)
		{
			// now if only we could get this properly into HLSL we'd avoid the software matrix transform...
			D3D_LerpLight (aliasverts, ent, &hdr->meshverts[i], aliasstate, hdr->vertexes[ent->pose1], hdr->vertexes[ent->pose2]);
			D3D_LerpVert (aliasverts, &hdr->meshverts[i], aliasstate, hdr->vertexes[ent->pose1], hdr->vertexes[ent->pose2], &ent->matrix);
		}

		for (int i = 0; i < hdr->numindexes; i++)
			aliasindexes[numindexes++] = hdr->indexes[i] + numverts;

		// accumulate vert counts
		numverts += hdr->nummesh;
		d3d_RenderDef.alias_polys += hdr->numtris;
	}

	// required always so that the buffer will unlock
	D3D_SubmitVertexes (numverts, numindexes, sizeof (aliaspolyvert_t));

	// done
	D3D_SetRenderState (D3DRS_SHADEMODE, D3DSHADE_FLAT);
}


entity_t **d3d_AliasEdicts = NULL;
int d3d_NumAliasEdicts = 0;


int D3D_AliasModelSortFunc (entity_t **e1, entity_t **e2)
{
	return (int) (e1[0]->model - e2[0]->model);
}


#define QUERY_IDLE		0
#define QUERY_WAITING	1

// a query is either idle or waiting; makes more sense than d3d's "signalled"/etc nonsense.
// this struct contains the IDirect3DQuery9 object as well as a flag indicating it's state.  
typedef struct d3d_occlusionquery_s
{
	LPDIRECT3DQUERY9 Query;
	int State;
	entity_t *Entity;
} d3d_occlusionquery_t;


CSpaceBuffer *Pool_Occlusions = NULL;
d3d_occlusionquery_t *d3d_OcclusionQueries = NULL;
int d3d_NumOcclusionQueries = 0;
int numoccluded = 0;

void D3D_RegisterOcclusionQuery (entity_t *ent)
{
	// occlusions not supported
	if (!d3d_GlobalCaps.supportOcclusion) return;

	// already has one
	if (ent->occlusion)
	{
		if (ent->occlusion->Entity != ent)
		{
			// if this changes we need to remove the occlusion from the entity
			ent->occlusion = NULL;
			ent->occluded = false;
		}

		// still OK
		if (ent->occlusion) return;
	}

	if (!Pool_Occlusions)
	{
		Pool_Occlusions = new CSpaceBuffer ("Occlusion Queries", 4, POOL_PERMANENT);
		d3d_OcclusionQueries = (d3d_occlusionquery_t *) Pool_Occlusions->Alloc (1);
		d3d_NumOcclusionQueries = 0;
	}
	else
	{
		// look for an idle query object
		for (int i = 0; i < d3d_NumOcclusionQueries; i++)
		{
			if (d3d_OcclusionQueries[i].State == QUERY_IDLE)
			{
				// remove the occlusion from the entity
				d3d_OcclusionQueries[i].Entity->occlusion = NULL;

				// this query can now be reused for this entity
				ent->occlusion = &d3d_OcclusionQueries[i];

				// don't NULL the query object as it already exists
				ent->occlusion->Entity = ent;

				// entity is not occluded
				ent->occluded = false;

				// got it
				return;
			}
		}
	}

	// create a new query
	Pool_Occlusions->Alloc (sizeof (d3d_occlusionquery_t));
	ent->occlusion = &d3d_OcclusionQueries[d3d_NumOcclusionQueries++];

	ent->occlusion->Query = NULL;
	ent->occlusion->State = QUERY_IDLE;
	ent->occlusion->Entity = ent;

	// entity is not occluded be default
	ent->occluded = false;
}


void D3D_ClearOcclusionQueries (void)
{
	if (!Pool_Occlusions) return;
	if (!d3d_GlobalCaps.supportOcclusion) return;

	for (int i = 0; i < d3d_NumOcclusionQueries; i++)
	{
		d3d_OcclusionQueries[i].Entity->occlusion = NULL;
		d3d_OcclusionQueries[i].Entity->occluded = false;
		SAFE_RELEASE (d3d_OcclusionQueries[i].Query);
	}

	d3d_NumOcclusionQueries = 0;
	Pool_Occlusions->Free ();
	Pool_Occlusions->Rewind ();
}


void D3D_IssueQuery (entity_t *ent, d3d_occlusionquery_t *q)
{
	// create on demand
	if (!q->Query)
	{
		// all queries are created idle (if only the rest of life was like that!)
		d3d_Device->CreateQuery (D3DQUERYTYPE_OCCLUSION, &q->Query);
		q->State = QUERY_IDLE;
	}

	// if this is true then the query is currently busy doing something and cannot be issued
	if (q->State != QUERY_IDLE) return;

	// issue the query
	q->Query->Issue (D3DISSUE_BEGIN);
	R_DrawBBox (ent);
	q->Query->Issue (D3DISSUE_END);

	// the query is now waiting
	q->State = QUERY_WAITING;
}


// we need to return -1 because 0 is a valid result and therefore we need
// another means of signalling that the query is not ready yet
int D3D_GetQueryResults (d3d_occlusionquery_t *q)
{
	// create on demand
	if (!q->Query)
	{
		// all queries are created idle (if only the rest of life was like that!)
		d3d_Device->CreateQuery (D3DQUERYTYPE_OCCLUSION, &q->Query);
		q->State = QUERY_IDLE;

		// no valid result to return
		return -1;
	}

	// query was not issued
	if (q->State != QUERY_WAITING) return -1;

	DWORD Visible = 0;

	// peek at the query to get it's result
	hr = q->Query->GetData ((void *) &Visible, sizeof (DWORD), D3DGETDATA_FLUSH);

	if (hr == S_OK)
	{
		// query was completed; go back to idle and return the result
		q->State = QUERY_IDLE;
		return (Visible > 1 ? 1 : 0);
	}
	else if (hr == S_FALSE)
	{
		// still waiting
		return -1;
	}
	else
	{
		// something bad happened; destroy the query
		SAFE_RELEASE (q->Query);
		return -1;
	}
}


void D3D_RunOcclusionQueries (entity_t **ents, int numents)
{
	if (cls.timedemo) return;
	if (!d3d_GlobalCaps.supportOcclusion) return;

	bool stateset = false;
	int occlusion_cutoff = 0;

	// if (numoccluded) Con_Printf ("occluded %i entities\n", numoccluded);

#define MAX_OCCLUSION_CUTOFF 1048576

	// set a dynamic occlusion cutoff point based on the number of tris rendered in the previous frame
	// (we assume this won't change too much from frame to frame) so that lighter scenes aren't bogged
	// down with queries whereas heavier scenes get the benefit of them.  this way performs better than
	// using the number of tris that would have been rendered in this frame if we had no queries.
	// complex view models may give abberant results here, but in practice it doesn't seem to matter.
	if (d3d_RenderDef.last_alias_polys < 1)
		occlusion_cutoff = MAX_OCCLUSION_CUTOFF;
	else
	{
		// value is arbitrary and non-scientific but seems to work well
		occlusion_cutoff = MAX_OCCLUSION_CUTOFF / d3d_RenderDef.last_alias_polys;

		// always cutoff at really small models
		if (occlusion_cutoff < 96) occlusion_cutoff = 96;
	}

	//Con_Printf ("last: %i   cutoff: %i\n", d3d_RenderDef.last_alias_polys, occlusion_cutoff);
	numoccluded = 0;

	// if (d3d_NumOcclusionQueries) Con_Printf ("%i occlusion queries\n", d3d_NumOcclusionQueries);

	for (int i = 0; i < numents; i++)
	{
		// don't bother with entities that are so simple the overhead would be too high
		if (ents[i]->model->aliashdr->numtris < occlusion_cutoff)
		{
			ents[i]->occluded = false;
			continue;
		}

		// register a query for the entity
		D3D_RegisterOcclusionQuery (ents[i]);

		// no occlusion object
		if (!ents[i]->occlusion)
		{
			// not occluded
			ents[i]->occluded = false;
			continue;
		}

		if (!stateset)
		{
			// only change state if we need to
			D3D_SetFVF (D3DFVF_XYZ);
			D3D_SetTextureColorMode (0, D3DTOP_DISABLE);
			D3D_SetRenderState (D3DRS_COLORWRITEENABLE, 0);
			D3D_SetRenderState (D3DRS_ZWRITEENABLE, FALSE);
			D3D_SetRenderState (D3DRS_CULLMODE, D3DCULL_NONE);

			stateset = true;
		}

		// get the last set of results
		int blah = D3D_GetQueryResults (ents[i]->occlusion);

		switch (blah)
		{
		case 0:
			// ent is occluded
			ents[i]->occluded = true;
			numoccluded++;
			break;

		case -1:
			// no results in yet
			break;

		default:
			// entity is not occluded
			ents[i]->occluded = false;
		}

		// issue this set
		D3D_IssueQuery (ents[i], ents[i]->occlusion);
	}

	if (stateset)
	{
		D3D_SetRenderState (D3DRS_COLORWRITEENABLE, 0x0000000F);
		D3D_BackfaceCull (D3DCULL_CCW);
		D3D_SetRenderState (D3DRS_ZWRITEENABLE, TRUE);
	}
}


void D3D_UpdateOcclusionQueries (void)
{
	if (cls.timedemo) return;
	if (!d3d_GlobalCaps.supportOcclusion) return;

	numoccluded = 0;

	// this is run every pass through the main loop even if other updates are skipped (by Host_FilterTime).
	// it needs to be run for entities that are also not in the PVS/frustum so that queries which have expired
	// have the best chance of being available for testing again the next time the entity is visible.
	for (int i = 0; i < d3d_NumOcclusionQueries; i++)
	{
		entity_t *ent = d3d_OcclusionQueries[i].Entity;

		if (!ent->occlusion) continue;

		d3d_occlusionquery_t *q = ent->occlusion;

		// query has not yet been issued
		if (!q->Query) continue;
		if (q->State != QUERY_WAITING) continue;

		DWORD Visible;

		// query status but don't flush
		hr = q->Query->GetData ((void *) &Visible, sizeof (DWORD), 0);

		if (hr == S_OK)
		{
			if (Visible)
				ent->occluded = false;
			else
			{
				ent->occluded = true;
				numoccluded++;
			}

			q->State = QUERY_IDLE;
		}
		else if (hr == S_FALSE)
		{
			// no change
		}
		else
		{
			// auto-destruct
			SAFE_RELEASE (q->Query);
		}
	}
}


void R_SetupAliasModels (void)
{
	if (!r_drawentities.integer) return;

	if (!d3d_AliasEdicts) d3d_AliasEdicts = (entity_t **) Pool_Permanent->Alloc (sizeof (entity_t *) * MAX_VISEDICTS);
	d3d_NumAliasEdicts = 0;

	for (int i = 0; i < d3d_RenderDef.numvisedicts; i++)
	{
		entity_t *ent = d3d_RenderDef.visedicts[i];

		if (!ent->model) continue;
		if (ent->model->type != mod_alias) continue;

		// initial setup
		D3D_SetupAliasModel (ent);

		if (ent->visframe != d3d_RenderDef.framecount) continue;

		if (ent->alphaval < 255)
			D3D_AddToAlphaList (ent);
		else d3d_AliasEdicts[d3d_NumAliasEdicts++] = ent;
	}

	if (!d3d_NumAliasEdicts) return;

	// sort the alias edicts by model
	qsort
	(
		d3d_AliasEdicts,
		d3d_NumAliasEdicts,
		sizeof (entity_t *),
		(int (*) (const void *, const void *)) D3D_AliasModelSortFunc
	);

	// everything that's needed for occlusion queries
	D3D_RunOcclusionQueries (d3d_AliasEdicts, d3d_NumAliasEdicts);
	D3D_DrawAliasBatch (d3d_AliasEdicts, d3d_NumAliasEdicts);
	D3D_DrawAliasShadows (d3d_AliasEdicts, d3d_NumAliasEdicts);
}


void R_DrawViewModel (void)
{
	// conditions for switching off view model
	if (r_drawviewmodel.value < 0.01f) return;
	if (chase_active.value) return;
	if (cl.stats[STAT_HEALTH] <= 0) return;
	if (!cl.viewent.model) return;
	if (!r_drawentities.value) return;
	if (d3d_RenderDef.automap) return;

	// select view ent
	entity_t *ent = &cl.viewent;
	ent->occluded = false;

	// the viewmodel should always be an alias model
	if (ent->model->type != mod_alias) return;

	// never check for bbox culling on the viewmodel
	ent->nocullbox = true;

	// hack the depth range to prevent view model from poking into walls
	D3D_SetRenderStatef (D3DRS_DEPTHBIAS, -0.3f);

	// delerp muzzleflashes here
	DelerpMuzzleFlashes (ent->model->aliashdr);

	if ((cl.items & IT_INVISIBILITY) || r_drawviewmodel.value < 0.99f)
	{
		// enable blending
		D3D_SetRenderState (D3DRS_ALPHABLENDENABLE, TRUE);
		D3D_SetRenderState (D3DRS_BLENDOP, D3DBLENDOP_ADD);
		D3D_SetRenderState (D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
		D3D_SetRenderState (D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
		D3D_SetRenderState (D3DRS_ZWRITEENABLE, FALSE);

		// initial alpha
		ent->alphaval = (int) (r_drawviewmodel.value * 255.0f);

		// adjust for invisibility
		if (cl.items & IT_INVISIBILITY) ent->alphaval >>= 1;

		// final range
		ent->alphaval = BYTE_CLAMP (ent->alphaval);
	}

	// adjust projection to a constant y fov for wide-angle views
	if (d3d_RenderDef.fov_x >= 84)
	{
		D3DXMATRIX fovmatrix;
		D3DXMatrixPerspectiveFovRH (&fovmatrix, D3DXToRadian (68.038704f), ((float) d3d_CurrentMode.Width / (float) d3d_CurrentMode.Height), 4, 4096);
		d3d_Device->SetTransform (D3DTS_PROJECTION, &fovmatrix);
	}

	// add it to the list
	D3D_SetupAliasModel (ent);
	D3D_DrawAliasBatch (&ent, 1);

	// restore original projection
	if (d3d_RenderDef.fov_x >= 84)
		d3d_Device->SetTransform (D3DTS_PROJECTION, &d3d_ProjMatrix);

	if (cl.items & IT_INVISIBILITY)
	{
		// disable blending (done)
		ent->alphaval = 255;
		D3D_SetRenderState (D3DRS_ZWRITEENABLE, TRUE);
		D3D_SetRenderState (D3DRS_ALPHABLENDENABLE, FALSE);
	}

	// restore the hacked depth range
	D3D_SetRenderStatef (D3DRS_DEPTHBIAS, 0.0f);
}



void D3D_CreateShadeDots (void)
{
	// already done
	if (r_avertexnormal_dots_lerp) return;

	float *r_avertexnormal_dots;
	int dotslen = Sys_LoadResourceData (IDR_ANORMDOTS, (void **) &r_avertexnormal_dots);

	if (dotslen != 16384) Sys_Error ("Corrupted dots lump!");

	// create the buffer to hold it
	r_avertexnormal_dots_lerp = (float **) Pool_Permanent->Alloc (sizeof (float *) * SHADEDOT_QUANT_LERP);

	for (int i = 0; i < SHADEDOT_QUANT_LERP; i++)
		r_avertexnormal_dots_lerp[i] = (float *) Pool_Permanent->Alloc (sizeof (float) * 256);

	// now interpolate between them
	for (int i = 0, j = 1; i < SHADEDOT_QUANT; i++, j++)
	{
		int diff = SHADEDOT_QUANT_LERP / SHADEDOT_QUANT;

		for (int dot = 0; dot < 256; dot++)
		{
			// these are the 2 points to interpolate between
			float l1 = r_avertexnormal_dots[(i & (SHADEDOT_QUANT - 1)) * 256 + dot];
			float l2 = r_avertexnormal_dots[(j & (SHADEDOT_QUANT - 1)) * 256 + dot];

			for (int d = 0; d < diff; d++)
			{
				// this is the point we're going to write into
				int p = ((i * diff) + d) & (SHADEDOT_QUANT_LERP - 1);

				// these are the two results of the linear interpolation
				float p1 = (((float) diff - d) / (float) diff) * l1;
				float p2 = ((float) d / (float) diff) * l2;

				// and we write it in
				r_avertexnormal_dots_lerp[p][dot] = p1 + p2;
			}
		}
	}
}


