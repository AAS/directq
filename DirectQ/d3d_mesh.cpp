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

void R_LightPoint (entity_t *e, int *c);
bool R_CullBox (vec3_t mins, vec3_t maxs);
void D3D_RotateForEntity (entity_t *e);

// up to 16 color translated skins
LPDIRECT3DTEXTURE9 playertextures[16];

void D3D_RotateForEntity (entity_t *e, bool shadow);

/*
=================================================================

ALIAS MODEL DISPLAY LIST GENERATION

=================================================================
*/

int	used[8192];

// the command list holds counts and s/t values that are valid for
// every frame
int		commands[8192];
int		numcommands;

// all frames will have their vertexes rearranged and expanded
// so they are in the order expected by the command list
int		vertexorder[8192];
int		numorder;

int		allverts, alltris;

int		stripverts[128];
int		striptris[128];
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

	stripverts[0] = last->vertindex[(startv)%3];
	stripverts[1] = last->vertindex[(startv+1)%3];
	stripverts[2] = last->vertindex[(startv+2)%3];

	striptris[0] = starttri;
	stripcount = 1;

	m1 = last->vertindex[(startv+2)%3];
	m2 = last->vertindex[(startv+1)%3];

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
			if (stripcount & 1)
				m2 = check->vertindex[ (k+2)%3 ];
			else
				m1 = check->vertindex[ (k+2)%3 ];

			stripverts[stripcount+2] = check->vertindex[ (k+2)%3 ];
			striptris[stripcount] = j;
			stripcount++;

			used[j] = 2;
			goto nexttri;
		}
	}
done:

	// clear the temp used flags
	for (j=starttri+1 ; j<hdr->numtris ; j++)
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

	stripverts[0] = last->vertindex[(startv)%3];
	stripverts[1] = last->vertindex[(startv+1)%3];
	stripverts[2] = last->vertindex[(startv+2)%3];

	striptris[0] = starttri;
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
			m2 = check->vertindex[ (k+2)%3 ];

			stripverts[stripcount+2] = m2;
			striptris[stripcount] = j;
			stripcount++;

			used[j] = 2;
			goto nexttri;
		}
	}
done:

	// clear the temp used flags
	for (j=starttri+1 ; j<hdr->numtris ; j++)
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
	memset (used, 0, sizeof(used));

	for (i=0 ; i<hdr->numtris ; i++)
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
					for (j=0 ; j<bestlen+2 ; j++)
						bestverts[j] = stripverts[j];
					for (j=0 ; j<bestlen ; j++)
						besttris[j] = striptris[j];
				}
			}
		}

		// mark the tris on the best strip as used
		for (j=0 ; j<bestlen ; j++)
			used[besttris[j]] = 1;

		if (besttype == 1)
			commands[numcommands++] = (bestlen+2);
		else
			commands[numcommands++] = -(bestlen+2);

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

	commands[numcommands++] = 0;		// end of list marker

	Con_DPrintf ("%3i tri  %3i vert  %3i cmd\n", hdr->numtris, numorder, numcommands);

	allverts += numorder;
	alltris += hdr->numtris;
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
	Con_DPrintf ("meshing %s...\n",m->name);

	BuildTris (hdr);		// trifans or lists

	// save the data out
	hdr->poseverts = numorder;

	int start = Hunk_LowMark ();

	cmds = (int *) Hunk_Alloc (numcommands * 4);
	hdr->commands = (byte *) cmds - (byte *) hdr;
	memcpy (cmds, commands, numcommands * 4);

	verts = (drawvertx_t *) Hunk_Alloc (hdr->numposes * hdr->poseverts * sizeof (drawvertx_t));

	hdr->posedata = (byte *) verts - (byte *) hdr;

	// store out the verts
	for (i = 0; i < hdr->numposes; i++)
	{
		for (j = 0; j < numorder; j++, verts++)
		{
			// fill in
			verts->lightnormalindex = poseverts[i][vertexorder[j]].lightnormalindex;

			// prescale
			verts->v[0] = poseverts[i][vertexorder[j]].v[0] * hdr->scale[0] + hdr->scale_origin[0];
			verts->v[1] = poseverts[i][vertexorder[j]].v[1] * hdr->scale[1] + hdr->scale_origin[1];
			verts->v[2] = poseverts[i][vertexorder[j]].v[2] * hdr->scale[2] + hdr->scale_origin[2];
		}
	}

	Con_DPrintf ("%s uses %i k\n", m->name, (Hunk_LowMark () - start) / 1024);

	// set up alias vertex structures
	if (!aliasverts) aliasverts = (aliasverts_t *) malloc (sizeof (aliasverts_t) * MAXALIASVERTS);
}


/*
=============================================================

  ALIAS MODELS

=============================================================
*/


vec3_t	shadevector;
int shadelight;

// precalculated dot products for quantized angles
#define SHADEDOT_QUANT 16
float	r_avertexnormal_dots[SHADEDOT_QUANT][256] =
#include "anorm_dots.h"
;

float	*shadedots = r_avertexnormal_dots[0];

int	lastposenum;

/*
=============
GL_DrawAliasFrame
=============
*/
void GL_DrawAliasFrame (aliashdr_t *hdr, int posenum)
{
	int 	l;
	drawvertx_t	*verts;
	int		*order;
	int		count;
	int		numverts;

	lastposenum = posenum;

	verts = (drawvertx_t *) ((byte *) hdr + hdr->posedata);
	verts += posenum * hdr->poseverts;
	order = (int *) ((byte *) hdr + hdr->commands);

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
			l = (shadedots[verts->lightnormalindex] * shadelight);

			// clamp lighting
			if (l > 255) l = 255;

			// set light color
			aliasverts[numverts].color = D3DCOLOR_XRGB (l, l, l);

			// scale and translate vertexes
			aliasverts[numverts].x = verts->v[0];
			aliasverts[numverts].y = verts->v[1];
			aliasverts[numverts].z = verts->v[2];

			// increment pointers and counters
			verts++;
			numverts++;
			order += 2;
		} while (--count);

		d3d_Device->DrawPrimitiveUP (PrimitiveType, numverts - 2, aliasverts, sizeof (aliasverts_t));
	}
}


/*
=============
GL_DrawAliasShadow
=============
*/
extern	vec3_t			lightspot;

void GL_DrawAliasShadow (aliashdr_t *hdr, int posenum)
{
	drawvertx_t	*verts;
	int		*order;
	vec3_t	point;
	float	height, lheight;
	int		count;

	lheight = currententity->origin[2] - lightspot[2];

	height = 0;
	verts = (drawvertx_t *) ((byte *) hdr + hdr->posedata);
	verts += posenum * hdr->poseverts;
	order = (int *) ((byte *) hdr + hdr->commands);

	height = -lheight + 1.0;

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

		int numverts = 0;

		do
		{
			// texture coordinates come from the draw list (skipped for shadows)
			order += 2;

			// set light color
			aliasverts[numverts].color = D3DCOLOR_ARGB (128, 0, 0, 0);

			// scale and translate vertexes
			aliasverts[numverts].x = verts->v[0] - shadevector[0] * (verts->v[2] + lheight);
			aliasverts[numverts].y = verts->v[1] - shadevector[1] * (verts->v[2] + lheight);
			aliasverts[numverts].z = height;

			verts++;
			numverts++;
		} while (--count);

		d3d_Device->DrawPrimitiveUP (PrimitiveType, numverts - 2, aliasverts, sizeof (aliasverts_t));
	}
}



/*
=================
R_SetupAliasFrame

=================
*/
void R_SetupAliasFrame (int frame, aliashdr_t *hdr)
{
	int				pose, numposes;
	float			interval;

	if ((frame >= hdr->numframes) || (frame < 0))
	{
		Con_DPrintf ("R_AliasSetupFrame: no such frame %d\n", frame);
		frame = 0;
	}

	pose = hdr->frames[frame].firstpose;
	numposes = hdr->frames[frame].numposes;

	if (numposes > 1)
	{
		interval = hdr->frames[frame].interval;
		pose += (int)(cl.time / interval) % numposes;
	}

	GL_DrawAliasFrame (hdr, pose);
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

	clmodel = currententity->model;

	VectorAdd (currententity->origin, clmodel->mins, mins);
	VectorAdd (currententity->origin, clmodel->maxs, maxs);

	if (R_CullBox (mins, maxs)) return;

	VectorCopy (currententity->origin, r_entorigin);
	VectorSubtract (r_origin, r_entorigin, modelorg);

	// get lighting information
	R_LightPoint (currententity, &shadelight);

	// allways give the gun some light
	if (e == &cl.viewent && shadelight < 24)
		shadelight = 24;

	// ZOID: never allow players to go totally black
	i = currententity - cl_entities;

	if (i >= 1 && i <= cl.maxclients)
		if (shadelight < 8)
			shadelight = 8;

	// HACK HACK HACK -- no fullbright colors, so make torches full light
	if (!strcmp (clmodel->name, "progs/flame2.mdl") || !strcmp (clmodel->name, "progs/flame.mdl"))
		shadelight = 256;

	// always give pickups some light (bmodel pickups have light, so this would be intentional)
	if (currententity->model->flags & EF_ROTATE)
	{
		// i made it an int, so...
		shadelight += 64;
		shadelight *= 3;
		shadelight /= 4;
	}

	shadedots = r_avertexnormal_dots[((int)(e->angles[1] * (SHADEDOT_QUANT / 360.0))) & (SHADEDOT_QUANT - 1)];

	extern D3DLIGHT9 d3d_Light0;

	d3d_Light0.Diffuse.r = shadelight;
	d3d_Light0.Diffuse.g = shadelight;
	d3d_Light0.Diffuse.b = shadelight;

	// set up lighting for the model
	d3d_Device->SetLight (0, &d3d_Light0);

	// locate the proper data
	hdr = (aliashdr_t *) Mod_Extradata (currententity->model);

	c_alias_polys += hdr->numtris;

	d3d_WorldMatrixStack->Push ();
	D3D_RotateForEntity (e);

	anim = (int) (cl.time * 10) & 3;

	LPDIRECT3DTEXTURE9 tex = (LPDIRECT3DTEXTURE9) hdr->texture[currententity->skinnum][anim];

	// we can't dynamically colormap textures, so they are cached
	// seperately for the players.  Heads are just uncolored.
	if (currententity->colormap != vid.colormap && !gl_nocolors.value)
	{
		i = currententity - cl_entities;

		if (i >= 1 && i <= cl.maxclients) tex = playertextures[i - 1];
	}

	D3D_BindTexture (tex);

	R_SetupAliasFrame (currententity->frame, hdr);

	d3d_WorldMatrixStack->Pop ();

	if (r_shadows.value)
	{
		// r_shadows 1 mode is meant to be neither stable nor robust,
		// so we won't really bother with optimising it or anything for now...
		an = e->angles[1] / 180 * M_PI;
		shadevector[0] = cos (-an);
		shadevector[1] = sin (-an);
		shadevector[2] = 1;
		VectorNormalize (shadevector);

		D3D_SetFVFStateManaged (D3DFVF_XYZ | D3DFVF_DIFFUSE);
		d3d_EnableAlphaBlend->Apply ();
		d3d_Device->SetTextureStageState (0, D3DTSS_COLOROP, D3DTOP_DISABLE);

		d3d_WorldMatrixStack->Push ();
		D3D_RotateForEntity (e, true);

		GL_DrawAliasShadow (hdr, lastposenum);

		d3d_WorldMatrixStack->Pop ();

		D3D_SetFVFStateManaged (D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
		d3d_Device->SetTextureStageState (0, D3DTSS_COLOROP, D3DTOP_MODULATE);
		d3d_DisableAlphaBlend->Apply ();
	}
}


