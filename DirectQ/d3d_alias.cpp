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
#include "d3d_quake.h"

// r_lightmap 1 uses the grey texture on account of 2 x overbrighting
extern LPDIRECT3DTEXTURE9 r_greytexture;
extern cvar_t r_lightmap;

#define NUMVERTEXNORMALS	162

float	r_avertexnormals[NUMVERTEXNORMALS][3] =
{
#include "anorms.h"
};

typedef struct aliasverts_s
{
	float x, y, z;
	DWORD color;
	float s, t;
} aliasverts_t;


aliasverts_t *aliasverts = NULL;
int maxaliasverts = 0;

void R_LightPoint (entity_t *e, float *c);

// up to 16 color translated skins
LPDIRECT3DTEXTURE9 d3d_PlayerSkins[256] = {NULL};

void D3D_RotateForEntity (entity_t *e);
extern DWORD D3D_OVERBRIGHT_MODULATE;

/*
=================================================================

ALIAS MODEL DISPLAY LIST GENERATION

=================================================================
*/


extern trivertx_t **ThePoseverts;

int	*used = NULL;

// the command list holds counts and s/t values that are valid for
// every frame
int *commands;
int	numcommands;

// all frames will have their vertexes rearranged and expanded
// so they are in the order expected by the command list
int *vertexorder = NULL;
int	numorder;

int *stripverts = NULL;
int *striptris = NULL;


/*
================
StripLength
================
*/
int	StripLength (aliashdr_t *hdr, int starttri, int startv)
{
	int			m1, m2;
	int			j;
	mtriangle_t	*last, *check;
	int			k;

	used[starttri] = 2;

	last = &triangles[starttri];

	stripverts[0] = last->vertindex[(startv) % 3];
	stripverts[1] = last->vertindex[(startv + 1) % 3];
	stripverts[2] = last->vertindex[(startv + 2) % 3];
	striptris[0] = starttri;

	int stripcount = 1;

	m1 = last->vertindex[(startv + 2) % 3];
	m2 = last->vertindex[(startv + 1) % 3];

	// look for a matching triangle
nexttri:
	for (j = starttri + 1, check = &triangles[starttri + 1]; j < hdr->numtris; j++, check++)
	{
		if (check->facesfront != last->facesfront) continue;

		for (k = 0; k < 3; k++)
		{
			if (check->vertindex[k] != m1) continue;
			if (check->vertindex[(k + 1) % 3] != m2) continue;

			// this is the next part of the fan
			// if we can't use this triangle, this tristrip is done
			if (used[j]) goto done;

			// the new edge (what is this used for here???)
			if (stripcount & 1)
				m2 = check->vertindex[(k + 2) % 3];
			else
				m1 = check->vertindex[(k + 2) % 3];

			stripverts[stripcount + 2] = check->vertindex[(k + 2) % 3];
			striptris[stripcount] = j;
			stripcount++;

			used[j] = 2;
			goto nexttri;
		}
	}
done:

	// clear the temp used flags
	for (j = starttri + 1; j < hdr->numtris; j++)
		if (used[j] == 2)
			used[j] = 0;

	return stripcount;
}

/*
===========
FanLength
===========
*/
int	FanLength (aliashdr_t *hdr, int starttri, int startv)
{
	int		m1, m2;
	int		j;
	mtriangle_t	*last, *check;
	int		k;

	used[starttri] = 2;

	last = &triangles[starttri];

	stripverts[0] = last->vertindex[(startv) % 3];
	stripverts[1] = last->vertindex[(startv + 1) % 3];
	stripverts[2] = last->vertindex[(startv + 2) % 3];
	striptris[0] = starttri;

	int stripcount = 1;

	m1 = last->vertindex[(startv + 0) % 3];
	m2 = last->vertindex[(startv + 2) % 3];

	// look for a matching triangle
nexttri:
	for (j = starttri + 1, check = &triangles[starttri + 1]; j < hdr->numtris; j++, check++)
	{
		if (check->facesfront != last->facesfront)
			continue;

		for (k = 0; k < 3; k++)
		{
			if (check->vertindex[k] != m1) continue;
			if (check->vertindex[(k + 1) % 3] != m2) continue;

			// this is the next part of the fan
			// if we can't use this triangle, this tristrip is done
			if (used[j]) goto done;

			// the new edge
			m2 = check->vertindex[(k + 2) % 3];

			stripverts[stripcount + 2] = m2;
			striptris[stripcount] = j;
			stripcount++;

			used[j] = 2;
			goto nexttri;
		}
	}
done:

	// clear the temp used flags
	for (j = starttri + 1; j < hdr->numtris; j++)
		if (used[j] == 2)
			used[j] = 0;

	return stripcount;
}


/*
================
BuildTris

Generate a list of trifans or strips
for the model, which holds for all frames
================
*/
void BuildTris (aliashdr_t *hdr)
{
	int		i, j, k;
	int		startv;
	float	s, t;
	int		len, bestlen, besttype;
	int		bestverts[1024];
	int		besttris[1024];
	int		type;

	// build tristrips or fans
	numorder = 0;
	numcommands = 0;

	// setup used tris buffer
	used = (int *) Pool_Alloc (POOL_TEMP, hdr->numtris * sizeof (int));

	// because we reset the pool we need to init used to 0
	memset (used, 0, hdr->numtris * sizeof (int));

	// setup verts and tris buffers; these are the theoretical max sizes if a single mdl
	// was composed of a single primitive of the appropriate type; verts is 2 extra.
	// having these in std::vector arrays was a MAJOR cause of slowdown in map loading
	stripverts = (int *) Pool_Alloc (POOL_TEMP, (hdr->numtris * 3 + 2) * sizeof (int));
	striptris = (int *) Pool_Alloc (POOL_TEMP, hdr->numtris * 3 * sizeof (int));

	// ditto order and commands; not necessarily max sizes here though... but good enough for sdquake...!
	vertexorder = (int *) Pool_Alloc (POOL_TEMP, hdr->numtris * 3 * sizeof (int));
	commands = (int *) Pool_Alloc (POOL_TEMP, (hdr->numtris * 7 + 1) * sizeof (int));

	for (i = 0; i < hdr->numtris; i++)
	{
		// pick an unused triangle and start the trifan
		if (used[i]) continue;

		bestlen = 0;

		for (type = 0; type < 2; type++)
		{
			for (startv = 0 ; startv < 3 ; startv++)
			{
				if (type == 1)
					len = StripLength (hdr, i, startv);
				else
					len = FanLength (hdr, i, startv);

				if (len > bestlen)
				{
					besttype = type;
					bestlen = len;

					for (j = 0; j < bestlen + 2; j++) bestverts[j] = stripverts[j];
					for (j = 0; j < bestlen; j++) besttris[j] = striptris[j];
				}
			}
		}

		// mark the tris on the best strip as used
		for (j = 0; j < bestlen; j++)
			used[besttris[j]] = 1;

		if (besttype == 1)
			commands[numcommands++] = (bestlen + 2);
		else
			commands[numcommands++] = -(bestlen + 2);

		for (j = 0; j < bestlen + 2; j++)
		{
			// emit a vertex into the reorder buffer
			k = bestverts[j];
			vertexorder[numorder++] = k;

			// emit s/t coords into the commands stream
			s = stverts[k].s;
			t = stverts[k].t;

			if (!triangles[besttris[0]].facesfront && stverts[k].onseam)
				s += hdr->skinwidth / 2;	// on back side

			s = (s + 0.5) / hdr->skinwidth;
			t = (t + 0.5) / hdr->skinheight;

			*(float *) &commands[numcommands++] = s;
			*(float *) &commands[numcommands++] = t;
		}
	}

	// end of list marker
	commands[numcommands++] = 0;
}


float Mesh_ScaleVert (aliashdr_t *hdr, drawvertx_t *invert, int index)
{
	float outvert = invert->v[index];

	outvert *= hdr->scale[index];
	outvert += hdr->scale_origin[index];

	return outvert;
}


void D3D_GetAliasVerts (aliashdr_t *hdr)
{
	int *order = hdr->commands;
	int numpolys = 0;

	while (1)
	{
		// get the vertex count and primitive type
		int count = *order++;

		// check for done
		if (!count) break;

		// invert for strip or fan
		if (count < 0) count = -count;

		// check for increase in max
		// this need only be done per primitive, as the struct is reset each time a new primitive is emitted
		if (count > maxaliasverts) maxaliasverts = count;

		do
		{
			// increment pointers and counters
			order += 2;
		} while (--count);

		numpolys++;
	}
}


/*
================
GL_MakeAliasModelDisplayLists
================
*/
void GL_MakeAliasModelDisplayLists (model_t *m, aliashdr_t *hdr)
{
	int		i, j;
	int			*cmds;
	drawvertx_t	*verts;

	// build it from scratch
	BuildTris (hdr);

	// save the data out
	hdr->numorder = numorder;

	// alloc in cache
	hdr->commands = (int *) Pool_Alloc (POOL_CACHE, numcommands * sizeof (int));

	for (i = 0; i < numcommands; i++)
		hdr->commands[i] = commands[i];

	// alloc in cache
	hdr->posedata = Pool_Alloc (POOL_CACHE, hdr->numposes * hdr->numorder * sizeof (drawvertx_t));
	verts = (drawvertx_t *) hdr->posedata;

	// store out the verts
	for (i = 0; i < hdr->numposes; i++)
	{
		for (j = 0; j < hdr->numorder; j++, verts++)
		{
			// fill in
			verts->lightnormalindex = ThePoseverts[i][vertexorder[j]].lightnormalindex;
			verts->v[0] = ThePoseverts[i][vertexorder[j]].v[0];
			verts->v[1] = ThePoseverts[i][vertexorder[j]].v[1];
			verts->v[2] = ThePoseverts[i][vertexorder[j]].v[2];

			// lerp by default
			verts->lerpvert = true;
		}
	}

	// not delerped until we establish it is a viewmodel
	hdr->mfdelerp = false;
}


/*
=============================================================

  ALIAS MODELS

=============================================================
*/


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
	drawvertx_t *vertsf0 = (drawvertx_t *) hdr->posedata;
	drawvertx_t *vertsf1 = (drawvertx_t *) hdr->posedata;

	// set these pointers to the 0th and 1st frames
	vertsf0 += hdr->frames[0].firstpose * hdr->numorder;
	vertsf1 += hdr->frames[1].firstpose * hdr->numorder;

	// now go through them and compare.  we expect that (a) the animation is sensible and there's no major
	// difference between the 2 frames to be expected, and (b) any verts that do exhibit a major difference
	// can be assumed to belong to the muzzleflash
	for (int j = 0; j < hdr->numorder; j++)
	{
		// get difference in front to back movement
		float vdiff = Mesh_ScaleVert (hdr, vertsf1, 0) - Mesh_ScaleVert (hdr, vertsf0, 0);

		// if it's above a certain treshold, assume a muzzleflash and mark for nolerp
		// 10 is the approx lowest range of visible front to back in a view model, so that seems reasonable to work with
		if (vdiff > 10)
			vertsf0->lerpvert = false;
		else vertsf0->lerpvert = true;

		// next set of verts
		vertsf0++;
		vertsf1++;
	}

	// now we mark every other vert in the model as per the instructions in the first frame
	for (int i = 1; i < hdr->numframes; i++)
	{
		// get pointers to the verts
		drawvertx_t *vertsf0 = (drawvertx_t *) hdr->posedata;
		drawvertx_t *vertsfi = (drawvertx_t *) hdr->posedata;

		// set these pointers to the 0th and i'th frames
		vertsf0 += hdr->frames[0].firstpose * hdr->numorder;
		vertsfi += hdr->frames[i].firstpose * hdr->numorder;

		for (int j = 0; j < hdr->numorder; j++)
		{
			// just copy it across
			vertsfi->lerpvert = vertsf0->lerpvert;

			// next set of verts
			vertsf0++;
			vertsfi++;
		}
	}
}


// precalculated dot products for quantized angles
#define SHADEDOT_QUANT 16
float	r_avertexnormal_dots[SHADEDOT_QUANT][256] =
#include "anorm_dots.h"
;

typedef struct aliascache_s
{
	// all the info we need to draw the model is here
	entity_t *entity;
	bool culled;
	float frontlerp;
	float backlerp;
	float *shadedots1;
	float *shadedots2;
	float lightlerpoffset;
	LPDIRECT3DTEXTURE9 d3d_Texture;
	LPDIRECT3DTEXTURE9 d3d_Fullbright;
	vec3_t lightspot;
	mplane_t *lightplane;
} aliascache_t;

// cache for alias models that have been prepared so that the data can be reused
aliascache_t *d3d_PreparedAliasModels = NULL;

int framesize = 0;
extern vec3_t lightspot;
extern mplane_t *lightplane;

void D3D_DrawAliasShadow (aliascache_t *ac)
{
	// easier access
	entity_t *e = ac->entity;

	// set up transforms
	d3d_WorldMatrixStack->Push ();
	d3d_WorldMatrixStack->Translatev (e->origin);
	d3d_WorldMatrixStack->Rotate (0, 0, 1, e->angles[1]);

	// set up the draw lists
	drawvertx_t *verts1 = (drawvertx_t *) e->model->ah->posedata;
	drawvertx_t *verts2 = verts1;
	verts1 += e->pose1 * e->model->ah->numorder;
	verts2 += e->pose2 * e->model->ah->numorder;

	int *order = e->model->ah->commands;

	D3DPRIMITIVETYPE PrimitiveType;
	DWORD shadecolor = D3DCOLOR_ARGB (BYTE_CLAMP (r_shadows.value * 255.0f), 0, 0, 0);

	// lightspot will differ per entity
	float lheight = e->origin[2] - ac->lightspot[2];
	lheight = -lheight + 1.0f;

	// largely the same as before except it removes the need to normalize
	// (but how much of a big deal is that anyway???)
	float theta = -e->angles[1] / 180 * M_PI;

#define SHADE_SCALE		0.70710678118654752440084436210485
	vec3_t shadevector =
	{
		cos (theta) * SHADE_SCALE,
		sin (theta) * SHADE_SCALE,
		SHADE_SCALE
	};

	float s1 = sin (e->angles[1] / 180 * M_PI);
	float c1 = cos (e->angles[1] / 180 * M_PI);

	while (1)
	{
		// get the vertex count and primitive type
		int count = *order++;

		// check for done
		if (!count) break;

		if (count < 0)
		{
			count = -count;
			PrimitiveType = D3DPT_TRIANGLEFAN;
		}
		else PrimitiveType = D3DPT_TRIANGLESTRIP;

		int numverts = 0;
		float l1, l2, l, diff;
		vec3_t xyz;

		do
		{
			// shadow colour is constant
			aliasverts[numverts].color = shadecolor;

			// fill in vertexes
			if (verts1->lerpvert)
			{
				xyz[0] = (float) verts1->v[0] * ac->frontlerp + (float) verts2->v[0] * ac->backlerp;
				xyz[1] = (float) verts1->v[1] * ac->frontlerp + (float) verts2->v[1] * ac->backlerp;
				xyz[2] = (float) verts1->v[2] * ac->frontlerp + (float) verts2->v[2] * ac->backlerp;
			}
			else if (ac->backlerp > ac->frontlerp)
			{
				xyz[0] = verts2->v[0];
				xyz[1] = verts2->v[1];
				xyz[2] = verts2->v[2];
			}
			else
			{
				xyz[0] = verts1->v[0];
				xyz[1] = verts1->v[1];
				xyz[2] = verts1->v[2];
			}

			// scale
			aliasverts[numverts].x = xyz[0] * e->model->ah->scale[0] + e->model->ah->scale_origin[0];
			aliasverts[numverts].y = xyz[1] * e->model->ah->scale[1] + e->model->ah->scale_origin[1];
			aliasverts[numverts].z = xyz[2] * e->model->ah->scale[2] + e->model->ah->scale_origin[2];

			aliasverts[numverts].x -= shadevector[0] * (aliasverts[numverts].z + lheight);
			aliasverts[numverts].y -= shadevector[1] * (aliasverts[numverts].z + lheight);
			aliasverts[numverts].z = lheight;

			aliasverts[numverts].z += ((aliasverts[numverts].y * (s1 * ac->lightplane->normal[0])) - 
						(aliasverts[numverts].x * (c1 * ac->lightplane->normal[0])) - 
						(aliasverts[numverts].x * (s1 * ac->lightplane->normal[1])) - 
						(aliasverts[numverts].y * (c1 * ac->lightplane->normal[1]))) + 
						((1 - ac->lightplane->normal[2]) * 20) + 0.2;

			// increment pointers and counters
			verts1++;
			verts2++;
			numverts++;
			order += 2;

			framesize += sizeof (aliasverts_t);
		} while (--count);

		D3D_DrawPrimitive (PrimitiveType, numverts - 2, aliasverts, sizeof (aliasverts_t));

		// correct r_speeds count
		d3d_RenderDef.alias_polys++;
	}

	d3d_WorldMatrixStack->Pop ();
}


void D3D_DrawAliasFrame (aliascache_t *ac)
{
	// easier access
	entity_t *e = ac->entity;

	// set up texturing
	if (ac->d3d_Fullbright)
	{
		D3D_SetTextureColorMode (0, D3DTOP_ADD, D3DTA_TEXTURE, D3DTA_DIFFUSE);
		D3D_SetTextureColorMode (1, D3D_OVERBRIGHT_MODULATE, D3DTA_TEXTURE, D3DTA_CURRENT);

		// luma pass
		D3D_SetTexture (0, ac->d3d_Fullbright);
		D3D_SetTexture (1, ac->d3d_Texture);
	}
	else
	{
		// straight up modulation
		D3D_SetTextureColorMode (0, D3D_OVERBRIGHT_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE);
		D3D_SetTextureColorMode (1, D3DTOP_DISABLE);

		// bind texture for baseline skin
		D3D_SetTexture (0, ac->d3d_Texture);
	}

	// set up transforms
	d3d_WorldMatrixStack->Push ();
	D3D_RotateForEntity (e);
	d3d_WorldMatrixStack->Translatev (e->model->ah->scale_origin);
	d3d_WorldMatrixStack->Scalev (e->model->ah->scale);

	// set up the draw lists
	drawvertx_t *verts1 = (drawvertx_t *) e->model->ah->posedata;
	drawvertx_t *verts2 = verts1;
	verts1 += e->pose1 * e->model->ah->numorder;
	verts2 += e->pose2 * e->model->ah->numorder;

	int *order = e->model->ah->commands;

	D3DPRIMITIVETYPE PrimitiveType;

	while (1)
	{
		// get the vertex count and primitive type
		int count = *order++;

		// check for done
		if (!count) break;

		if (count < 0)
		{
			count = -count;
			PrimitiveType = D3DPT_TRIANGLEFAN;
		}
		else PrimitiveType = D3DPT_TRIANGLESTRIP;

		int numverts = 0;
		float 	l1, l2, l, diff;

		do
		{
			// texcoords
			aliasverts[numverts].s = ((float *) order)[0];
			aliasverts[numverts].t = ((float *) order)[1];

			// normals and vertexes come from the frame list
			if (verts1->lerpvert)
			{
				l1 = (ac->shadedots1[verts1->lightnormalindex] * ac->frontlerp + ac->shadedots1[verts2->lightnormalindex] * ac->backlerp);
				l2 = (ac->shadedots2[verts1->lightnormalindex] * ac->frontlerp + ac->shadedots2[verts2->lightnormalindex] * ac->backlerp);
			}
			else if (ac->backlerp > ac->frontlerp)
			{
				l1 = ac->shadedots1[verts2->lightnormalindex];
				l2 = ac->shadedots2[verts2->lightnormalindex];
			}
			else
			{
				l1 = ac->shadedots1[verts1->lightnormalindex];
				l2 = ac->shadedots2[verts1->lightnormalindex];
			}

			if (l1 > l2)
			{
				diff = l1 - l2;
				diff *= ac->lightlerpoffset;
				l = l1 - diff;
			}
			else if (l1 < l2)
			{
				diff = l2 - l1;
				diff *= ac->lightlerpoffset;
				l = l1 + diff;
			}
			else l = l1;

			// set light color
			aliasverts[numverts].color = D3DCOLOR_ARGB
			(
				BYTE_CLAMP (e->alphaval),
				vid.lightmap[BYTE_CLAMP (l * e->shadelight[0])],
				vid.lightmap[BYTE_CLAMP (l * e->shadelight[1])],
				vid.lightmap[BYTE_CLAMP (l * e->shadelight[2])]
			);

			// fill in vertexes
			if (verts1->lerpvert)
			{
				aliasverts[numverts].x = (float) verts1->v[0] * ac->frontlerp + (float) verts2->v[0] * ac->backlerp;
				aliasverts[numverts].y = (float) verts1->v[1] * ac->frontlerp + (float) verts2->v[1] * ac->backlerp;
				aliasverts[numverts].z = (float) verts1->v[2] * ac->frontlerp + (float) verts2->v[2] * ac->backlerp;
			}
			else if (ac->backlerp > ac->frontlerp)
			{
				aliasverts[numverts].x = verts2->v[0];
				aliasverts[numverts].y = verts2->v[1];
				aliasverts[numverts].z = verts2->v[2];
			}
			else
			{
				aliasverts[numverts].x = verts1->v[0];
				aliasverts[numverts].y = verts1->v[1];
				aliasverts[numverts].z = verts1->v[2];
			}

			// increment pointers and counters
			verts1++;
			verts2++;
			numverts++;
			order += 2;

			framesize += sizeof (aliasverts_t);
		} while (--count);

		D3D_DrawPrimitive (PrimitiveType, numverts - 2, aliasverts, sizeof (aliasverts_t));

		// correct r_speeds count
		d3d_RenderDef.alias_polys++;
	}

	// revert the matrix
	d3d_WorldMatrixStack->Pop ();
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

	if (maxaliasverts > 0)
	{
		// create the aliasverts cache
		aliasverts = (aliasverts_t *) Pool_Alloc (POOL_MAP, sizeof (aliasverts_t) * maxaliasverts);

		// notify developers only; the user doesn't need to know
		Con_DPrintf ("Alloced %i Alias Verts\n", maxaliasverts);

		// reset the max count
		maxaliasverts = 0;
	}

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
	if (hdr->numposes == 1) lerpmdl = false;				// only one pose
	if (e == &cl.viewent && e->frame == 0) lerpmdl = false;	// super-nailgun spinning down bug
	if (e->pose1 == e->pose2) lerpmdl = false;				// both frames are identical
	if (!r_lerpframe.value) lerpmdl = false;

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

	// don't let blend pass 1
	if (cl.paused || blend > 1.0) blend = 1.0;

	return blend;
}


void D3D_InitAliasModelState (void)
{
	// set alias model state
	D3D_SetFVF (D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);

	D3D_SetTextureAddressMode (D3DTADDRESS_CLAMP, D3DTADDRESS_CLAMP);
	D3D_SetTexCoordIndexes (0, 0);

	D3D_SetTextureMipmap (0, d3d_3DFilterMag, d3d_3DFilterMin, d3d_3DFilterMip);
	D3D_SetTextureMipmap (1, d3d_3DFilterMag, d3d_3DFilterMin, d3d_3DFilterMip);

	// smooth shading
	if (gl_smoothmodels.value) D3D_SetRenderState (D3DRS_SHADEMODE, D3DSHADE_GOURAUD);

	// common texture states
	D3D_SetTextureAlphaMode (0, D3DTOP_DISABLE);
	D3D_SetTextureAlphaMode (1, D3DTOP_DISABLE);
	D3D_SetTextureColorMode (2, D3DTOP_DISABLE);
	D3D_SetTextureAlphaMode (2, D3DTOP_DISABLE);
}


bool D3D_PrepareAliasModel (entity_t *e, aliascache_t *c)
{
	vec3_t		mins, maxs;

	// assume that the model has been culled
	c->culled = true;

	// revert to full model culling here
	VectorAdd (d3d_RenderDef.currententity->origin, e->model->mins, mins);
	VectorAdd (d3d_RenderDef.currententity->origin, e->model->maxs, maxs);

	// the gun or the chase model are never culled away
	if (d3d_RenderDef.currententity == cl_entities[cl.viewentity] && chase_active.value)
		;	// no bbox culling on certain entities
	else if (d3d_RenderDef.currententity->nocullbox)
		;	// no bbox culling on certain entities
	else if (R_CullBox (mins, maxs)) return false;

	// the model has not been culled now
	c->entity = e;
	c->culled = false;
	c->lightplane = NULL;

	// setup the frame for drawing and store the interpolation blend
	c->backlerp = D3D_SetupAliasFrame (e, e->model->ah);
	c->frontlerp = 1.0f - c->backlerp;

	// get lighting information
	vec3_t shadelight;
	R_LightPoint (d3d_RenderDef.currententity, shadelight);

	// average light between frames
	for (int i = 0; i < 3; i++)
	{
		shadelight[i] = (shadelight[i] + e->shadelight[i]) / 2;
		e->shadelight[i] = shadelight[i];
	}

	// precomputed shading dot products
	float ang_ceil;
	float ang_floor;

	c->lightlerpoffset = (e->angles[1] + e->angles[0]) * (SHADEDOT_QUANT / 360.0);

	ang_ceil = ceil (c->lightlerpoffset);
	ang_floor = floor (c->lightlerpoffset);

	c->lightlerpoffset = ang_ceil - c->lightlerpoffset;

	// interpolate lighting between quantized rotation points
	c->shadedots1 = r_avertexnormal_dots[(int) ang_ceil & (SHADEDOT_QUANT - 1)];
	c->shadedots2 = r_avertexnormal_dots[(int) ang_floor & (SHADEDOT_QUANT - 1)];

	// store out for shadows
	VectorCopy (lightspot, c->lightspot);
	c->lightplane = lightplane;

	// get texturing info
	int anim = (int) (cl.time * 10) & 3;

	// base texture
	c->d3d_Texture = (LPDIRECT3DTEXTURE9) e->model->ah->texture[d3d_RenderDef.currententity->skinnum][anim];

	// switch player skin (entnum - 1 is the same playernum as is used for calling into D3D_TranslatePlayerSkin so it's valid)
	if (d3d_RenderDef.currententity->colormap != vid.colormap && !gl_nocolors.value)
		if (d3d_RenderDef.currententity->entnum >= 1 && d3d_RenderDef.currententity->entnum <= cl.maxclients)
			c->d3d_Texture = d3d_PlayerSkins[cl.scores[d3d_RenderDef.currententity->entnum - 1].colors];

	// alias models should be affected by r_lightmap 1 too...
	if (r_lightmap.integer) c->d3d_Texture = r_greytexture;

	// fullbright texture (can be NULL)
	c->d3d_Fullbright = (LPDIRECT3DTEXTURE9) e->model->ah->fullbright[d3d_RenderDef.currententity->skinnum][anim];

	// we have a good entity now
	return true;
}


int maxframesize = 0;

void D3D_PrepareAliasModels (void)
{
	if (framesize > maxframesize) maxframesize = framesize;

	// Con_Printf ("framesize is %0.3f kb   max is %0.3f\n", (float) framesize / 1024, (float) maxframesize / 1024);
	framesize = 0;

	// ensure that this is always set to 0
	int numalias = 0;
	extern bool chase_nodraw;

	// place into permanent memory; store in a pool rather than in a static array so that we get more accurate reporting of memory usage
	// add 1 extra for NULL termination
	if (!d3d_PreparedAliasModels)
		d3d_PreparedAliasModels = (aliascache_t *) Pool_Alloc (POOL_PERMANENT, (MAX_VISEDICTS + 1) * sizeof (aliascache_t));

	// hack to prevent potential double-drawing of gun model
	d3d_PreparedAliasModels[0].entity = NULL;

	// check for rendering (note - we expect this to always run in most circumstances)
	if (!(d3d_RenderDef.renderflags & R_RENDERALIAS)) return;
	if (!r_drawentities.value) return;

	// prepare models for drawing; this pass just precalcs a lot of stuff and caches it
	for (int i = 0; i < d3d_RenderDef.numvisedicts; i++)
	{
		d3d_RenderDef.currententity = d3d_RenderDef.visedicts[i];

		// not an alias model
		if (d3d_RenderDef.currententity->model->type != mod_alias) continue;

		if (d3d_RenderDef.currententity == cl_entities[cl.viewentity] && chase_active.value)
		{
			// don't draw if too close
			if (chase_nodraw) continue;

			// adjust angles (done here instead of chase_update as we only want the angle for rendering to be adjusted)
			d3d_RenderDef.currententity->angles[0] *= 0.3;
		}

		// prepare the model
		if (!D3D_PrepareAliasModel (d3d_RenderDef.currententity, &d3d_PreparedAliasModels[numalias])) continue;

		// sort out into textured and fullbright (should we just sort by texture here?)
		if (!d3d_PreparedAliasModels[numalias].d3d_Fullbright)
			d3d_RenderDef.renderflags |= R_RENDERTEXTUREDALIAS;
		else d3d_RenderDef.renderflags |= R_RENDERFULLBRIGHTALIAS;

		// add a new model and null term the list
		numalias++;
		d3d_PreparedAliasModels[numalias].entity = NULL;
	}
}


void D3D_DrawOpaqueAliasModels (void)
{
	if (d3d_RenderDef.renderflags & R_RENDERALIAS)
	{
		// set up common state
		D3D_InitAliasModelState ();

		if (d3d_RenderDef.renderflags & R_RENDERTEXTUREDALIAS)
		{
			for (int i = 0;; i++)
			{
				if (!d3d_PreparedAliasModels[i].entity) break;
				if (d3d_PreparedAliasModels[i].entity->alphaval < 255) continue;
				if (d3d_PreparedAliasModels[i].d3d_Fullbright) continue;

				D3D_DrawAliasFrame (&d3d_PreparedAliasModels[i]);
			}
		}

		if (d3d_RenderDef.renderflags & R_RENDERFULLBRIGHTALIAS)
		{
			for (int i = 0;; i++)
			{
				if (!d3d_PreparedAliasModels[i].entity) break;
				if (d3d_PreparedAliasModels[i].entity->alphaval < 255) continue;
				if (!d3d_PreparedAliasModels[i].d3d_Fullbright) continue;

				D3D_DrawAliasFrame (&d3d_PreparedAliasModels[i]);
			}
		}

		// back to flat shading
		D3D_SetRenderState (D3DRS_SHADEMODE, D3DSHADE_FLAT);

		if (r_shadows.value > 0.0f)
		{
			bool shadestate = false;

			for (int i = 0;; i++)
			{
				if (!d3d_PreparedAliasModels[i].entity) break;
				if (d3d_PreparedAliasModels[i].entity->alphaval < 255) continue;
				if (!d3d_PreparedAliasModels[i].lightplane) continue;

				if (!shadestate)
				{
					D3D_SetRenderState (D3DRS_ZWRITEENABLE, FALSE);
					D3D_SetRenderState (D3DRS_ALPHABLENDENABLE, TRUE);
					D3D_SetRenderState (D3DRS_BLENDOP, D3DBLENDOP_ADD);
					D3D_SetRenderState (D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
					D3D_SetRenderState (D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
					D3D_SetTextureStageState (0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
					D3D_SetTextureStageState (0, D3DTSS_COLOROP, D3DTOP_DISABLE);

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

					shadestate = true;
				}

				D3D_DrawAliasShadow (&d3d_PreparedAliasModels[i]);
			}

			if (shadestate)
			{
				if (d3d_GlobalCaps.DepthStencilFormat == D3DFMT_D24S8)
					D3D_SetRenderState (D3DRS_STENCILENABLE, FALSE);

				D3D_SetRenderState (D3DRS_ALPHABLENDENABLE, FALSE);
				D3D_SetRenderState (D3DRS_ZWRITEENABLE, TRUE);
				D3D_SetTextureStageState (0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
				D3D_SetTextureStageState (0, D3DTSS_COLOROP, D3DTOP_MODULATE);
			}
		}
	}
}


void D3D_DrawTranslucentAliasModel (entity_t *ent)
{
	// prepare the model for drawing
	if (!D3D_PrepareAliasModel (ent, &d3d_PreparedAliasModels[0])) return;

	extern bool chase_nodraw;

	if (ent == cl_entities[cl.viewentity] && chase_active.value)
	{
		// don't draw if too close
		if (chase_nodraw) return;

		// adjust angles (done here instead of chase_update as we only want the angle for rendering to be adjusted)
		ent->angles[0] *= 0.3;
	}

	// set up common state for alias models
	D3D_InitAliasModelState ();

	// draw it
	D3D_DrawAliasFrame (&d3d_PreparedAliasModels[0]);

	// back to flat shading
	D3D_SetRenderState (D3DRS_SHADEMODE, D3DSHADE_FLAT);
}


void D3D_DrawViewModel (void)
{
	// conditions for switching off view model
	if (!r_drawviewmodel.value) return;
	if (chase_active.value) return;
	if (cl.stats[STAT_HEALTH] <= 0) return;
	if (!cl.viewent.model) return;
	if (!r_drawentities.value) return;

	// select view ent
	d3d_RenderDef.currententity = &cl.viewent;

	// the viewmodel should always be an alias model
	if (d3d_RenderDef.currententity->model->type != mod_alias) return;

	// prepare the view model for drawing
	if (!D3D_PrepareAliasModel (d3d_RenderDef.currententity, &d3d_PreparedAliasModels[0])) return;

	// bring up the state
	D3D_InitAliasModelState ();

	// hack the depth range to prevent view model from poking into walls
	// of course, we all know Direct3D doesn't support polygon offset... or at least those of us who read
	// something written in 1995 and think that's the way things are gonna be forever more know it...
	if (!d3d_RenderDef.automap)
	{
		D3D_SetRenderStatef (D3DRS_DEPTHBIAS, -0.3f);

		// only adjust if fov goes down (so that viewmodel will properly disappear in zoom mode)
		if (r_refdef.fov_y > 68.038704f)
		{
			// adjust the projection matrix to keep the viewmodel the same shape and visibility at all FOVs
			// zfar doesn't need to be > regular GLQuake 4096 here
			d3d_ProjMatrixStack->Push ();
			d3d_ProjMatrixStack->LoadIdentity ();

			// adjust this frustum for warps
			d3d_ProjMatrixStack->Frustum3D
			(
				83.974434f / r_refdef.fov_x * d3d_RenderDef.fov_x,
				68.038704f / r_refdef.fov_y * d3d_RenderDef.fov_y,
				4,
				4096
			);
		}
	}

	// check for ring
	if (cl.items & IT_INVISIBILITY)
	{
		// gimme the ring, kiss'd and toll'd, gimme something that i missed
		D3D_EnableAlphaBlend (D3DBLENDOP_ADD, D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA, false);
		d3d_RenderDef.currententity->alphaval = 96;
	}
	else d3d_RenderDef.currententity->alphaval = 255;

	// delerp muzzleflashes here
	DelerpMuzzleFlashes (d3d_RenderDef.currententity->model->ah);

	// never check for bbox culling on the viewmodel
	d3d_RenderDef.currententity->nocullbox = true;

	// copy out the old model
	model_t *backupmodel = NULL;

	if (d3d_RenderDef.automap)
	{
		// switch the model if drawing the automap
		backupmodel = d3d_RenderDef.currententity->model;
		d3d_RenderDef.currententity->model = Mod_ForName ("progs/player.mdl", true);
	}

	// draw it
	D3D_DrawAliasFrame (&d3d_PreparedAliasModels[0]);

	// this is a hack to prevent shadows from being drawn on this one if there are no other alias models available
	d3d_PreparedAliasModels[0].lightplane = NULL;

	if (d3d_RenderDef.automap)
	{
		// restore the model
		d3d_RenderDef.currententity->model = backupmodel;
	}
	else
	{
		// if we switched the projection matrix for wide fov we must switch it back now
		if (r_refdef.fov_y > 68.038704f) d3d_ProjMatrixStack->Pop ();

		// unhack the depth range
		D3D_SetRenderStatef (D3DRS_DEPTHBIAS, 0.0f);
	}

	// disable alpha if we enabled it previously
	if (cl.items & IT_INVISIBILITY) D3D_DisableAlphaBlend ();

	// back to flat shading
	D3D_SetRenderState (D3DRS_SHADEMODE, D3DSHADE_FLAT);
}


