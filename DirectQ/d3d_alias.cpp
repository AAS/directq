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
#include "d3d_hlsl.h"

#include <vector>

// r_lightmap 1 uses the grey texture on account of 2 x overbrighting
extern LPDIRECT3DTEXTURE9 r_greytexture;
extern cvar_t r_lightmap;
extern cvar_t r_lightscale;

#define NUMVERTEXNORMALS	162

float	r_avertexnormals[NUMVERTEXNORMALS][3] =
{
#include "anorms.h"
};

typedef struct aliasverts_s
{
	float x, y, z;
	float x1, y1, z1;
	DWORD color;
	float s, t;
	float fl, bl;

	// pad to a multiple of 32 bytes
	byte pad[20];
} aliasverts_t;


aliasverts_t *aliasverts = NULL;
int maxaliasverts = 0;

void R_LightPoint (entity_t *e, float *c);
bool R_CullBox (vec3_t mins, vec3_t maxs);

// up to 16 color translated skins
LPDIRECT3DTEXTURE9 playertextures[16] = {NULL};

void D3D_RotateForEntity (entity_t *e, bool shadow = false);

/*
=================================================================

ALIAS MODEL DISPLAY LIST GENERATION

=================================================================
*/


extern trivertx_t **ThePoseverts;

int	*used = NULL;

// the command list holds counts and s/t values that are valid for
// every frame
std::vector<int> commands;
int		numcommands;

// all frames will have their vertexes rearranged and expanded
// so they are in the order expected by the command list
std::vector<int> vertexorder;
int		numorder;

int		allverts, alltris;

std::vector<int> stripverts;
std::vector<int> striptris;
int		stripcount;

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

	stripverts.clear ();
	stripverts.push_back (last->vertindex[(startv) % 3]);
	stripverts.push_back (last->vertindex[(startv + 1) % 3]);
	stripverts.push_back (last->vertindex[(startv + 2) % 3]);

	striptris.clear ();
	striptris.push_back (starttri);

	stripcount = 1;

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

			stripverts.push_back (check->vertindex[(k + 2) % 3]);
			striptris.push_back (j);
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

	stripverts.clear ();
	stripverts.push_back (last->vertindex[(startv) % 3]);
	stripverts.push_back (last->vertindex[(startv + 1) % 3]);
	stripverts.push_back (last->vertindex[(startv + 2) % 3]);

	striptris.clear ();
	striptris.push_back (starttri);

	stripcount = 1;

	m1 = last->vertindex[(startv+0)%3];
	m2 = last->vertindex[(startv+2)%3];


	// look for a matching triangle
nexttri:
	for (j=starttri+1, check=&triangles[starttri+1] ; j<hdr->numtris ; j++, check++)
	{
		if (check->facesfront != last->facesfront)
			continue;
		for (k=0 ; k<3 ; k++)
		{
			if (check->vertindex[k] != m1)
				continue;
			if (check->vertindex[ (k+1)%3 ] != m2)
				continue;

			// this is the next part of the fan

			// if we can't use this triangle, this tristrip is done
			if (used[j])
				goto done;

			// the new edge
			m2 = check->vertindex[(k + 2) % 3];

			stripverts.push_back (m2);
			striptris.push_back (j);
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

	vertexorder.clear ();
	commands.clear ();

	// setup used tris buffer
	if (used) free (used);
	used = (int *) malloc (hdr->numtris * sizeof (int));
	memset (used, 0, hdr->numtris * sizeof (int));

	for (i = 0; i < hdr->numtris; i++)
	{
		// pick an unused triangle and start the trifan
		if (used[i])
			continue;

		bestlen = 0;
		for (type = 0 ; type < 2 ; type++)
		{
			for (startv =0 ; startv < 3 ; startv++)
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
		for (j=0 ; j<bestlen ; j++)
			used[besttris[j]] = 1;

		if (besttype == 1)
		{
			commands.push_back (bestlen + 2);
			numcommands++;
		}
		else
		{
			commands.push_back (-(bestlen + 2));
			numcommands++;
		}

		for (j = 0; j < bestlen + 2; j++)
		{
			// emit a vertex into the reorder buffer
			k = bestverts[j];

			vertexorder.push_back (k);
			numorder++;

			// emit s/t coords into the commands stream
			s = stverts[k].s;
			t = stverts[k].t;

			if (!triangles[besttris[0]].facesfront && stverts[k].onseam)
				s += hdr->skinwidth / 2;	// on back side

			s = (s + 0.5) / hdr->skinwidth;
			t = (t + 0.5) / hdr->skinheight;

			commands.push_back (*((int *) &s));
			commands.push_back (*((int *) &t));
			numcommands += 2;
		}
	}

	commands.push_back (0);
	numcommands++;

	Con_DPrintf ("%3i tri  %3i vert  %3i cmd\n", hdr->numtris, numorder, numcommands);

	allverts += numorder;
	alltris += hdr->numtris;

	// done with used tris
	free (used);
	used = NULL;
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

	hdr->commands = (int *) Heap_TagAlloc (TAG_ALIASMODELS, numcommands * sizeof (int));

	for (i = 0; i < numcommands; i++)
		hdr->commands[i] = commands[i];

	hdr->posedata = Heap_TagAlloc (TAG_ALIASMODELS, hdr->numposes * hdr->numorder * sizeof (drawvertx_t));
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

	// clear all vectors used
	commands.clear ();
	vertexorder.clear ();
	stripverts.clear ();
	striptris.clear ();

	// not delerped until we establish it is a viewmodel
	hdr->mfdelerp = false;

	// set up alias vertex counts
	D3D_GetAliasVerts (hdr);
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


vec3_t	shadevector;
float shadelight[3];

// precalculated dot products for quantized angles
#define SHADEDOT_QUANT 16
float	r_avertexnormal_dots[SHADEDOT_QUANT][256] =
#include "anorm_dots.h"
;

float *shadedots1 = r_avertexnormal_dots[0];
float *shadedots2 = r_avertexnormal_dots[0];
float lightlerpoffset;

int	lastposenum;


void D3D_DrawAliasFrame (aliashdr_t *hdr, int pose1, int pose2, float backlerp, int alphaval)
{
	float 	l1, l2, l;
	float	diff;

	drawvertx_t	*verts1;
	drawvertx_t	*verts2;

	int		*order;
	int		count;
	int		numverts;

	float frontlerp = 1.0f - backlerp;

	lastposenum = pose1;

	verts1 = verts2 =(drawvertx_t *) hdr->posedata;
	verts1 += pose1 * hdr->numorder;
	verts2 += pose2 * hdr->numorder;

	order = hdr->commands;

	D3DPRIMITIVETYPE PrimitiveType;

	while (1)
	{
		// get the vertex count and primitive type
		count = *order++;

		// check for done
		if (!count) break;

		if (count < 0)
		{
			count = -count;
			PrimitiveType = D3DPT_TRIANGLEFAN;
		}
		else PrimitiveType = D3DPT_TRIANGLESTRIP;

		numverts = 0;

		do
		{
			// texcoords
			aliasverts[numverts].s = ((float *) order)[0];
			aliasverts[numverts].t = ((float *) order)[1];

			// normals and vertexes come from the frame list
			if (verts1->lerpvert)
			{
				l1 = (shadedots1[verts1->lightnormalindex] * frontlerp + shadedots1[verts2->lightnormalindex] * backlerp);
				l2 = (shadedots2[verts1->lightnormalindex] * frontlerp + shadedots2[verts2->lightnormalindex] * backlerp);
			}
			else if (backlerp > frontlerp)
			{
				l1 = shadedots1[verts2->lightnormalindex];
				l2 = shadedots2[verts2->lightnormalindex];
			}
			else
			{
				l1 = shadedots1[verts1->lightnormalindex];
				l2 = shadedots2[verts1->lightnormalindex];
			}

			if (l1 > l2)
			{
				diff = l1 - l2;
				diff *= lightlerpoffset;
				l = l1 - diff;
			}
			else if (l1 < l2)
			{
				diff = l2 - l1;
				diff *= lightlerpoffset;
				l = l1 + diff;
			}
			else l = l1;

			// set light color
			aliasverts[numverts].color = D3DCOLOR_ARGB
			(
				BYTE_CLAMP (alphaval),
				BYTE_CLAMP (l * shadelight[0]),
				BYTE_CLAMP (l * shadelight[1]),
				BYTE_CLAMP (l * shadelight[2])
			);

			// fill in vertexes
			// note - we'll end up storing most of this in a vertex buffer and just passing user data to the shader!
			// amendment - alias models are NOT a bottleneck; the most this would gain is a frame or two, so we won't bother
			aliasverts[numverts].x = verts1->v[0];
			aliasverts[numverts].y = verts1->v[1];
			aliasverts[numverts].z = verts1->v[2];
			aliasverts[numverts].x1 = verts2->v[0];
			aliasverts[numverts].y1 = verts2->v[1];
			aliasverts[numverts].z1 = verts2->v[2];

			if (verts1->lerpvert)
			{
				aliasverts[numverts].fl = frontlerp;
				aliasverts[numverts].bl = backlerp;
			}
			else if (backlerp > frontlerp)
			{
				aliasverts[numverts].fl = 0;
				aliasverts[numverts].bl = 1;
			}
			else
			{
				aliasverts[numverts].fl = 1;
				aliasverts[numverts].bl = 0;
			}

			// increment pointers and counters
			verts1++;
			verts2++;
			numverts++;
			order += 2;
		} while (--count);

		// uncomment this block to test out softwarelike replacement of fans/strips with points
		// needs a distance calc, and potentially moving the point to the middle of each generated
		// triangle.  doesn't really give much perf increase.
		/*
		PrimitiveType = D3DPT_POINTLIST;
		numverts += 2;
		D3D_SetRenderStatef (D3DRS_POINTSIZE, 2.0f);
		*/

		d3d_AliasFX.Draw (PrimitiveType, numverts - 2, aliasverts, sizeof (aliasverts_t));
	}
}


/*
=================
D3D_SetupAliasFrame

=================
*/
extern cvar_t r_lerpframe;

void D3D_SetupAliasFrame (entity_t *e, aliashdr_t *hdr)
{
	int pose, numposes;
	float interval;
	bool lerpmdl = !hdr->nolerp;
	float blend;

	if (maxaliasverts > 0)
	{
		// create the aliasverts cache
		aliasverts = (aliasverts_t *) Heap_TagAlloc (TAG_ALIASMODELS, sizeof (aliasverts_t) * maxaliasverts);

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

	D3D_DrawAliasFrame (hdr, e->pose1, e->pose2, blend, e->alphaval);
}


/*
=================
R_DrawAliasModel

=================
*/
void R_DrawAliasModel (entity_t *e)
{
	int			i;
	model_t		*clmodel;
	vec3_t		mins, maxs;
	aliashdr_t	*hdr;
	float		an;
	int			anim;

	// locate the proper data
	clmodel = currententity->model;
	hdr = currententity->model->ah;

	// revert to full model culling here
	VectorAdd (currententity->origin, e->model->mins, mins);
	VectorAdd (currententity->origin, e->model->maxs, maxs);

	// the gun or the chase model are never culled away
	if (currententity == cl_entities[cl.viewentity] && chase_active.value)
		;	// no bbox culling on certain entities
	else if (currententity->nocullbox)
		;	// no bbox culling on certain entities
	else if (R_CullBox (mins, maxs)) return;

	VectorCopy (currententity->origin, r_entorigin);
	VectorSubtract (r_origin, r_entorigin, modelorg);

	// get lighting information
	R_LightPoint (currententity, shadelight);

	// average light between frames
	for (i = 0; i < 3; i++)
	{
		shadelight[i] = (shadelight[i] + e->last_shadelight[i]) / 2;
		e->last_shadelight[i] = shadelight[i];
	}

	// precomputed shading dot products
	float ang_ceil;
	float ang_floor;

	lightlerpoffset = (e->angles[1] + e->angles[0]) * (SHADEDOT_QUANT / 360.0);

	ang_ceil = ceil (lightlerpoffset);
	ang_floor = floor (lightlerpoffset);

	lightlerpoffset = ang_ceil - lightlerpoffset;

	shadedots1 = r_avertexnormal_dots[(int) ang_ceil & (SHADEDOT_QUANT - 1)];
	shadedots2 = r_avertexnormal_dots[(int) ang_floor & (SHADEDOT_QUANT - 1)];

	// note - this misreports the actual number drawn, it should just ++ for each fan or strip in the
	// renderer, but it's been left this way for consistency with previous versions and other engines.
	c_alias_polys += hdr->numtris;

	d3d_WorldMatrixStack->Push ();
	D3D_RotateForEntity (e);
	d3d_WorldMatrixStack->TranslateLocal (hdr->scale_origin[0], hdr->scale_origin[1], hdr->scale_origin[2]);
	d3d_WorldMatrixStack->ScaleLocal (hdr->scale[0], hdr->scale[1], hdr->scale[2]);

	d3d_AliasFX.SetWPMatrix (&(*(d3d_WorldMatrixStack->GetTop ()) * d3d_PerspectiveMatrix));

	d3d_WorldMatrixStack->Pop ();

	anim = (int) (cl.time * 10) & 3;

	LPDIRECT3DTEXTURE9 tex = (LPDIRECT3DTEXTURE9) hdr->texture[currententity->skinnum][anim];

	// we can't dynamically colormap textures, so they are cached
	// seperately for the players.  Heads are just uncolored.
	if (currententity->colormap != vid.colormap && !gl_nocolors.value)
		if (currententity->entnum >= 1 && currententity->entnum <= cl.maxclients)
			tex = playertextures[currententity->entnum - 1];

	if (currententity->alphaval < 255)
	{
		D3D_SetRenderState (D3DRS_ALPHABLENDENABLE, TRUE);
		D3D_SetRenderState (D3DRS_BLENDOP, D3DBLENDOP_ADD);
		D3D_SetRenderState (D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
		D3D_SetRenderState (D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	}

	// alias models should be affected by r_lightmap 1 too...
	if (r_lightmap.integer)
		d3d_AliasFX.SetTexture (0, r_greytexture);
	else d3d_AliasFX.SetTexture (0, tex);

	if (hdr->fullbright[currententity->skinnum][anim])
	{
		// if the texture is switched for a player model we still use the baseline fullbright map
		d3d_AliasFX.SetTexture (1, (LPDIRECT3DTEXTURE9) hdr->fullbright[currententity->skinnum][anim]);

		if (!r_hlsl.integer)
		{
			// add 0 to the colour arg for the vertex then modulate the result by 1
			D3D_SetTextureStageState (0, D3DTSS_COLOROP, D3DTOP_ADD);
			D3D_SetTextureStageState (1, D3DTSS_COLOROP, D3DTOP_MODULATE);
		}

		// luma pass
		d3d_AliasFX.SwitchToPass (1);
	}
	else
	{
		if (!r_hlsl.integer)
			D3D_SetTextureStageState (0, D3DTSS_COLOROP, D3DTOP_MODULATE);

		d3d_AliasFX.SwitchToPass (0);
	}

	D3D_SetupAliasFrame (currententity, hdr);

	if (currententity->alphaval < 255) D3D_SetRenderState (D3DRS_ALPHABLENDENABLE, FALSE);

	if (!r_hlsl.integer)
	{
		D3D_SetTextureStageState (0, D3DTSS_COLOROP, D3DTOP_MODULATE);
		D3D_SetTextureStageState (1, D3DTSS_COLOROP, D3DTOP_DISABLE);
	}
}


void D3D_DrawAliasModels (void)
{
	if (!r_drawentities.value) return;

	// checks for viewmodel
	bool renderview = true;

	// conditions for switching off view model
	if (!r_drawviewmodel.value) renderview = false;
	if (chase_active.value) renderview = false;
	if (cl.stats[STAT_HEALTH] <= 0) renderview = false;
	if (!cl.viewent.model) renderview = false;

	// update flags
	if (renderview) r_renderflags |= R_RENDERALIAS;

	// check for rendering (note - we expect this to always run in most circumstances)
	if (!(r_renderflags & R_RENDERALIAS)) return;

	// set alias model state
	d3d_Device->SetVertexDeclaration (d3d_AliasVertexDeclaration);

	d3d_AliasFX.BeginRender ();
	d3d_AliasFX.SetScale (r_lightscale.value);
	d3d_AliasFX.SwitchToPass (0);

	if (gl_smoothmodels.value) D3D_SetRenderState (D3DRS_SHADEMODE, D3DSHADE_GOURAUD);

	extern bool chase_nodraw;

	int CullEnts = 0;

	for (int i = 0; i < cl_numvisedicts; i++)
	{
		currententity = cl_visedicts[i];

		// not an alias model
		if (currententity->model->type != mod_alias) continue;

		if (currententity == cl_entities[cl.viewentity] && chase_active.value)
		{
			// don't draw if too close
			if (chase_nodraw) continue;

			// adjust angles (done here instead of chase_update as we only want the angle for rendering to be adjusted)
			currententity->angles[0] *= 0.3;
		}

		/*
		// additional entity culling - doesn't achieve much at all...
		// (despite culling up to 14 ents per frame even with this crude test)
		mleaf_t *leaf = Mod_PointInLeaf (currententity->origin, cl.worldmodel);

		if (leaf->visframe != r_visframecount)
		{
			CullEnts++;
			continue;
		}
		*/

		R_DrawAliasModel (currententity);
	}

	// if (CullEnts) Con_Printf ("Culled %i entities\n", CullEnts);

	// add in the viewmodel here while we have the state up
	if (renderview)
	{
		// select view ent
		currententity = &cl.viewent;

		// matrix for restoring projection
		D3DXMATRIX d3d_RestoreProjection = d3d_PerspectiveMatrix;

		// Y fov in radians
		extern float FovYRadians;

		// hack the depth range to prevent view model from poking into walls
		// of course, we all know Direct3D doesn't support polygon offset... or at least those of us who read
		// something written in 1995 and think that's the way things are gonna be forever more know it...
		D3D_SetRenderStatef (D3DRS_DEPTHBIAS, -0.3f);

		// only adjust if fov goes down (so that viewmodel will properly disappear in zoom mode)
		if (FovYRadians > 1.1989051)
		{
			// adjust the projection matrix to keep the viewmodel the same shape and visibility at all FOVs
			// need to adjust the actual d3d_PerspectiveMatrix as this is what's used in the vertex shader input
			D3DXMatrixIdentity (&d3d_PerspectiveMatrix);
			D3DXMatrixPerspectiveFovRH (&d3d_PerspectiveMatrix, 1.1989051, (float) r_refdef.vrect.width / (float) r_refdef.vrect.height, 4, 4096);
		}

		// check for ring
		if (cl.items & IT_INVISIBILITY)
			currententity->alphaval = 96;
		else currententity->alphaval = 255;

		// delerp muzzleflashes here
		DelerpMuzzleFlashes (currententity->model->ah);

		// never check for bbox culling on the viewmodel
		currententity->nocullbox = true;

		// draw it
		R_DrawAliasModel (currententity);

		// unhack the depth range
		D3D_SetRenderStatef (D3DRS_DEPTHBIAS, 0.0f);
	}

	// take down
	d3d_AliasFX.EndRender ();

	// flat shading
	D3D_SetRenderState (D3DRS_SHADEMODE, D3DSHADE_FLAT);
}


