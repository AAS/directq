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
// models.c -- model loading and caching

// models are the only shared resource between a client and server running
// on the same machine.

#include "quakedef.h"
#include "d3d_quake.h"
#include <vector>

void R_InitSky (miptex_t *mt);
void GL_DontSubdivideSky (msurface_t *surf);
void GL_MakeAliasModelDisplayLists (model_t *m, aliashdr_t *hdr);

model_t	*loadmodel;
brushhdr_t *brushmodel;

char	loadname[32];	// for hunk tags

void Mod_LoadSpriteModel (model_t *mod, void *buffer);
void Mod_LoadBrushModel (model_t *mod, void *buffer);
void Mod_LoadAliasModel (model_t *mod, void *buffer);
model_t *Mod_LoadModel (model_t *mod, bool crash);

byte	mod_novis[(MAX_MAP_LEAFS + 7) / 8];
byte	decompressed[(MAX_MAP_LEAFS + 7) / 8];

#define	MAX_MOD_KNOWN	8192
model_t	*mod_known[MAX_MOD_KNOWN];
int		mod_numknown = 0;

// bspfile structs
dnode_t *dnodes;
dleaf_t *dleafs;
dplane_t *dplanes;

// tracing
void MakeTnodes (int numnodes);

/*
===============
Mod_Init
===============
*/
void Mod_Init (void)
{
	memset (mod_novis, 0xff, sizeof (mod_novis));
}


/*
===============
Mod_PointInLeaf
===============
*/
mleaf_t *Mod_PointInLeaf (vec3_t p, model_t *model)
{
	mnode_t		*node;
	float		d;
	mplane_t	*plane;
	
	if (!model || !model->bh->nodes)
		Host_Error ("Mod_PointInLeaf: bad model");

	node = model->bh->nodes;
	while (1)
	{
		if (node->contents < 0)
			return (mleaf_t *)node;
		plane = node->plane;
		d = DotProduct (p,plane->normal) - plane->dist;
		if (d > 0)
			node = node->children[0];
		else
			node = node->children[1];
	}
	
	return NULL;	// never reached
}


/*
===================
Mod_DecompressVis
===================
*/
byte *Mod_DecompressVis (byte *in, model_t *model)
{
	int		c;
	byte	*out;
	int		row;

	row = (model->bh->numleafs + 7) >> 3;	
	out = decompressed;

	if (!in)
	{
		// no vis info, so make all visible
		while (row)
		{
			*out++ = 0xff;
			row--;
		}

		return decompressed;		
	}

	do
	{
		if (*in)
		{
			*out++ = *in++;
			continue;
		}

		c = in[1];
		in += 2;

		while (c)
		{
			*out++ = 0;
			c--;
		}
	} while (out - decompressed < row);

	return decompressed;
}

byte *Mod_LeafPVS (mleaf_t *leaf, model_t *model)
{
	if (leaf == model->bh->leafs)
		return mod_novis;
	return Mod_DecompressVis (leaf->compressed_vis, model);
}

/*
===================
Mod_ClearAll
===================
*/
void Mod_ClearAll (void)
{
	int		i;

	// NULL the structs
	for (i = 0; i < MAX_MOD_KNOWN; i++)
		mod_known[i] = NULL;

	// note - this was a nasty memory leak which I'm sure was inherited from the original code.
	// the models array never went down, so if over 512 unique models get loaded it's crash time.
	// very unlikely to happen, but it was there all the same...
	mod_numknown = 0;
}

/*
==================
Mod_FindName

==================
*/
model_t *Mod_FindName (char *name)
{
	int		i;

	if (!name[0])
	{
		Host_Error ("Mod_ForName: NULL name");
		return NULL;
	}

	// search the currently loaded models
	for (i = 0; i < mod_numknown; i++)
		if (!strcmp (mod_known[i]->name, name))
			break;

	if (i == mod_numknown)
	{
		if (mod_numknown == MAX_MOD_KNOWN)
			Host_Error ("mod_numknown == MAX_MOD_KNOWN");

		// allocate a model
		mod_known[i] = (model_t *) Heap_TagAlloc (TAG_LOADMODELS, sizeof (model_t));

		strcpy (mod_known[i]->name, name);
		mod_known[i]->needload = true;
		mod_numknown++;
	}

	// return the model we got
	return mod_known[i];
}

/*
==================
Mod_TouchModel

==================
*/
void Mod_TouchModel (char *name)
{
	model_t	*mod;

	mod = Mod_FindName (name);
}

/*
==================
Mod_LoadModel

Loads a model into the cache
==================
*/
model_t *Mod_LoadModel (model_t *mod, bool crash)
{
	unsigned *buf;

	// already loaded
	if (!mod->needload) return mod;

	// load the file
	// note - a 1K stack buffer will always send it through here!!!
	if (!(buf = (unsigned *) COM_LoadTempFile (mod->name)))
	{
		if (crash) Host_Error ("Mod_NumForName: %s not found", mod->name);
		return NULL;
	}

	// allocate a new model
	COM_FileBase (mod->name, loadname);

	loadmodel = mod;

	// call the apropriate loader
	mod->needload = false;

	// set all header pointers initially NULL
	mod->ah = NULL;
	mod->bh = NULL;
	mod->sh = NULL;

	switch (LittleLong (*(unsigned *) buf))
	{
	case IDPOLYHEADER:
		Mod_LoadAliasModel (mod, buf);
		break;

	case IDSPRITEHEADER:
		Mod_LoadSpriteModel (mod, buf);
		break;

	default:
		// bsp files don't have a header ident... sigh...
		Mod_LoadBrushModel (mod, buf);
		break;
	}

	return mod;
}

/*
==================
Mod_ForName

Loads in a model for the given name
==================
*/
model_t *Mod_ForName (char *name, bool crash)
{
	model_t	*mod;

	mod = Mod_FindName (name);

	return Mod_LoadModel (mod, crash);
}


/*
===============================================================================

					BRUSHMODEL LOADING

===============================================================================
*/

byte	*mod_base;


/*
=================
Mod_LoadTextures
=================
*/
void Mod_LoadTextures (lump_t *l)
{
	int		i, j, pixels, num, max, altmax;
	miptex_t	*mt;
	texture_t	*tx, *tx2;
	texture_t	*anims[10];
	texture_t	*altanims[10];
	dmiptexlump_t *m;

	if (!l->filelen)
	{
		brushmodel->textures = NULL;
		return;
	}

	m = (dmiptexlump_t *) (mod_base + l->fileofs);

	m->nummiptex = LittleLong (m->nummiptex);

	brushmodel->numtextures = m->nummiptex;
	brushmodel->textures = (texture_t **) Heap_TagAlloc (TAG_BRUSHMODELS, m->nummiptex * sizeof (*brushmodel->textures));

	for (i = 0; i < m->nummiptex; i++)
	{
		m->dataofs[i] = LittleLong(m->dataofs[i]);

		if (m->dataofs[i] == -1) continue;

		mt = (miptex_t *)((byte *)m + m->dataofs[i]);
		mt->width = LittleLong (mt->width);
		mt->height = LittleLong (mt->height);
		for (j=0 ; j<MIPLEVELS ; j++)
			mt->offsets[j] = LittleLong (mt->offsets[j]);
		
		if ( (mt->width & 15) || (mt->height & 15) )
			Host_Error ("Texture %s is not 16 aligned", mt->name);
		pixels = mt->width*mt->height/64*85;
		tx = (texture_t *) Heap_TagAlloc (TAG_BRUSHMODELS, sizeof(texture_t));
		brushmodel->textures[i] = tx;

		memcpy (tx->name, mt->name, sizeof(tx->name));
		tx->width = mt->width;
		tx->height = mt->height;

		if (!Q_strncmp(mt->name,"sky",3))	
			R_InitSky (mt);
		else
		{
			tx->d3d_Texture = D3D_LoadTexture (mt, IMAGE_MIPMAP | IMAGE_BSP);
			tx->d3d_Fullbright = D3D_LoadTexture (mt, IMAGE_MIPMAP | IMAGE_BSP | IMAGE_LUMA);
		}
	}

	// sequence the animations
	for (i=0 ; i<m->nummiptex ; i++)
	{
		tx = brushmodel->textures[i];
		if (!tx || tx->name[0] != '+')
			continue;
		if (tx->anim_next)
			continue;	// allready sequenced

		// find the number of frames in the animation
		memset (anims, 0, sizeof(anims));
		memset (altanims, 0, sizeof(altanims));

		max = tx->name[1];
		altmax = 0;

		if (max >= 'a' && max <= 'z') max -= 'a' - 'A';

		if (max >= '0' && max <= '9')
		{
			max -= '0';
			altmax = 0;
			anims[max] = tx;
			max++;
		}
		else if (max >= 'A' && max <= 'J')
		{
			altmax = max - 'A';
			max = 0;
			altanims[altmax] = tx;
			altmax++;
		}
		else Host_Error ("Bad animating texture %s", tx->name);

		for (j=i+1 ; j<m->nummiptex ; j++)
		{
			tx2 = brushmodel->textures[j];
			if (!tx2 || tx2->name[0] != '+')
				continue;
			if (strcmp (tx2->name+2, tx->name+2))
				continue;

			num = tx2->name[1];
			if (num >= 'a' && num <= 'z')
				num -= 'a' - 'A';
			if (num >= '0' && num <= '9')
			{
				num -= '0';
				anims[num] = tx2;
				if (num+1 > max)
					max = num + 1;
			}
			else if (num >= 'A' && num <= 'J')
			{
				num = num - 'A';
				altanims[num] = tx2;
				if (num+1 > altmax)
					altmax = num+1;
			}
			else Host_Error ("Bad animating texture %s", tx->name);
		}
		
#define	ANIM_CYCLE	2
		// link them all together
		for (j=0 ; j<max ; j++)
		{
			tx2 = anims[j];
			if (!tx2)
				Host_Error ("Missing frame %i of %s",j, tx->name);
			tx2->anim_total = max * ANIM_CYCLE;
			tx2->anim_min = j * ANIM_CYCLE;
			tx2->anim_max = (j+1) * ANIM_CYCLE;
			tx2->anim_next = anims[ (j+1)%max ];
			if (altmax)
				tx2->alternate_anims = altanims[0];
		}
		for (j=0 ; j<altmax ; j++)
		{
			tx2 = altanims[j];
			if (!tx2)
				Host_Error ("Missing frame %i of %s",j, tx->name);
			tx2->anim_total = altmax * ANIM_CYCLE;
			tx2->anim_min = j * ANIM_CYCLE;
			tx2->anim_max = (j+1) * ANIM_CYCLE;
			tx2->anim_next = altanims[ (j+1)%altmax ];
			if (max)
				tx2->alternate_anims = anims[0];
		}
	}
}


/*
=================
Mod_LoadLighting
=================
*/
extern cvar_t r_monolight;

bool Mod_LoadLITFile (lump_t *l)
{
	if (r_monolight.value) return false;

	FILE *litfile = NULL;
	char litname[128];

	// take a copy to work on
	strncpy (litname, loadmodel->name, 127);

	// fixme - we use this in a number of places so we should refactor it out
	for (int i = strlen (litname) - 1; i; i--)
	{
		if (litname[i] == '.')
		{
			strcpy (&litname[i + 1], "lit");
			break;
		}
	}

	int filelen = COM_FOpenFile (litname, &litfile);

	// didn't find one
	if (!litfile) return false;

	// validate the file length; a valid lit should have 3 x the light data size of the BSP.
	// OK, we can still clash, but with the limited format and differences between the two,
	// at least this is better than nothing.
	if ((filelen - 8) != l->filelen * 3)
	{
		fclose (litfile);
		return false;
	}

	// read and validate the header
	int litheader[2];

	fread (litheader, sizeof (int), 2, litfile);

	if (litheader[0] != 0x54494C51 || litheader[1] != 1)
	{
		// invalid format
		fclose (litfile);
		return false;
	}

	// read from the lit file
	fread (brushmodel->lightdata, l->filelen * 3, 1, litfile);

	// done
	fclose (litfile);

	return true;
}


void Mod_LoadLighting (lump_t *l)
{
	int i;
	byte *src;
	byte *dst;

	if (!l->filelen)
	{
		brushmodel->lightdata = NULL;
		return;
	}

	// expand size to 3 component
	brushmodel->lightdata = (byte *) Heap_TagAlloc (TAG_BRUSHMODELS, l->filelen * 3);

	// check for a lit file
	if (Mod_LoadLITFile (l)) return;

	// copy to end of the buffer
	memcpy (&brushmodel->lightdata[l->filelen * 2], mod_base + l->fileofs, l->filelen);

	// set source and dest pointers
	src = &brushmodel->lightdata[l->filelen * 2];
	dst = brushmodel->lightdata;

	// fill in the rest
	for (i = 0; i < l->filelen; i++)
	{
		*dst++ = *src;
		*dst++ = *src;
		*dst++ = *src++;
	}
}


/*
=================
Mod_LoadVisibility
=================
*/
void Mod_LoadVisibility (lump_t *l)
{
	if (!l->filelen)
	{
		brushmodel->visdata = NULL;
		return;
	}

	brushmodel->visdata = (byte *) Heap_TagAlloc (TAG_BRUSHMODELS, l->filelen);	
	memcpy (brushmodel->visdata, mod_base + l->fileofs, l->filelen);
}


/*
=================
Mod_LoadEntities
=================
*/
void Mod_LoadEntities (lump_t *l)
{
	if (!l->filelen)
	{
		brushmodel->entities = NULL;
		return;
	}

	brushmodel->entities = (char *) Heap_TagAlloc (TAG_BRUSHMODELS, l->filelen);	
	memcpy (brushmodel->entities, mod_base + l->fileofs, l->filelen);
}


/*
=================
Mod_LoadVertexes
=================
*/
void Mod_LoadVertexes (lump_t *l)
{
	dvertex_t	*in;
	mvertex_t	*out;
	int			i, count;

	in = (dvertex_t *)(mod_base + l->fileofs);

	if (l->filelen % sizeof(*in))
		Host_Error ("MOD_LoadBmodel: funny lump size in %s", loadmodel->name);

	count = l->filelen / sizeof (*in);
	out = (mvertex_t *) Heap_TagAlloc (TAG_BRUSHMODELS, count * sizeof (*out));	

	brushmodel->vertexes = out;
	brushmodel->numvertexes = count;

	for (i = 0; i < count; i++, in++, out++)
	{
		out->position[0] = LittleFloat (in->point[0]);
		out->position[1] = LittleFloat (in->point[1]);
		out->position[2] = LittleFloat (in->point[2]);
	}
}

/*
=================
Mod_LoadSubmodels
=================
*/
void Mod_LoadSubmodels (lump_t *l)
{
	dmodel_t	*in;
	dmodel_t	*out;
	int			i, j, count;

	in = (dmodel_t *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Host_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = (dmodel_t *) Heap_TagAlloc (TAG_BRUSHMODELS, count*sizeof(*out));	

	brushmodel->submodels = out;
	brushmodel->numsubmodels = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		for (j=0 ; j<3 ; j++)
		{	// spread the mins / maxs by a pixel
			out->mins[j] = LittleFloat (in->mins[j]) - 1;
			out->maxs[j] = LittleFloat (in->maxs[j]) + 1;
			out->origin[j] = LittleFloat (in->origin[j]);
		}
		for (j=0 ; j<MAX_MAP_HULLS ; j++)
			out->headnode[j] = LittleLong (in->headnode[j]);
		out->visleafs = LittleLong (in->visleafs);
		out->firstface = LittleLong (in->firstface);
		out->numfaces = LittleLong (in->numfaces);
	}
}

/*
=================
Mod_LoadEdges
=================
*/
void Mod_LoadEdges (lump_t *l)
{
	dedge_t *in;
	medge_t *out;
	int 	i, count;

	in = (dedge_t *) (mod_base + l->fileofs);

	if (l->filelen % sizeof (*in))
		Host_Error ("MOD_LoadBmodel: funny lump size in %s", loadmodel->name);

	count = l->filelen / sizeof (*in);
	out = (medge_t *) Heap_TagAlloc (TAG_BRUSHMODELS, (count + 1) * sizeof (*out));	

	brushmodel->edges = out;
	brushmodel->numedges = count;

	for (i = 0; i < count; i++, in++, out++)
	{
		out->v[0] = (unsigned short) LittleShort (in->v[0]);
		out->v[1] = (unsigned short) LittleShort (in->v[1]);
	}
}

/*
=================
Mod_LoadTexinfo
=================
*/
void Mod_LoadTexinfo (lump_t *l)
{
	texinfo_t *in;
	mtexinfo_t *out;
	int 	i, j, k, count;
	int		miptex;
	float	len1, len2;

	in = (texinfo_t *) (mod_base + l->fileofs);

	if (l->filelen % sizeof (texinfo_t))
		Host_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);

	count = l->filelen / sizeof (texinfo_t);
	out = (mtexinfo_t *) Heap_TagAlloc (TAG_BRUSHMODELS, count * sizeof (mtexinfo_t));

	brushmodel->texinfo = out;
	brushmodel->numtexinfo = count;

	for (i = 0; i < count; i++, in++, out++)
	{
		for (k = 0; k < 2; k++)
			for (j = 0; j < 4; j++)
				out->vecs[k][j] = LittleFloat (in->vecs[k][j]);

		len1 = Length (out->vecs[0]);
		len2 = Length (out->vecs[1]);
		len1 = (len1 + len2) / 2;

		if (len1 < 0.32)
			out->mipadjust = 4;
		else if (len1 < 0.49)
			out->mipadjust = 3;
		else if (len1 < 0.99)
			out->mipadjust = 2;
		else
			out->mipadjust = 1;

		miptex = LittleLong (in->miptex);
		out->flags = LittleLong (in->flags);

		if (!brushmodel->textures)
		{
			out->texture = r_notexture_mip;	// checkerboard texture
			out->flags = 0;
		}
		else
		{
			if (miptex >= brushmodel->numtextures)
				Host_Error ("miptex >= brushmodel->numtextures");
			out->texture = brushmodel->textures[miptex];

			if (!out->texture)
			{
				out->texture = r_notexture_mip; // texture not found
				out->flags = 0;
			}
		}
	}
}


/*
================
CalcSurfaceExtents

Fills in s->texturemins[] and s->extents[]
================
*/
int MaxExtents[2] = {0, 0};

void CalcSurfaceExtents (msurface_t *s)
{
	float	mins[2], maxs[2], val;
	int		i, j, lindex;
	float	*vec;
	int		bmins[2], bmaxs[2];

	mins[0] = mins[1] = 99999999;
	maxs[0] = maxs[1] = -99999999;

	for (i = 0; i < s->numedges; i++)
	{
		lindex = brushmodel->surfedges[s->firstedge + i];

		// reconstruct verts
		if (lindex >= 0)
			vec = brushmodel->vertexes[brushmodel->edges[lindex].v[0]].position;
		else
			vec = brushmodel->vertexes[brushmodel->edges[-lindex].v[1]].position;

		// reconstruct s/t
		for (j = 0; j < 2; j++)
		{
			val = DotProduct (vec, s->texinfo->vecs[j]) + s->texinfo->vecs[j][3];

			if (val < mins[j]) mins[j] = val;
			if (val > maxs[j]) maxs[j] = val;
		}
	}

	for (i = 0; i < 2; i++)
	{	
		bmins[i] = floor (mins[i] / 16);
		bmaxs[i] = ceil (maxs[i] / 16);

		// minimum s/t * 16
		s->texturemins[i] = bmins[i] * 16;

		// extent of s/t * 16
		// extents are only used for lighting info, so we don't bother with checking against a max
		s->extents[i] = (bmaxs[i] - bmins[i]) * 16;

		// the lightmap size only allows extents up to 2032 so clamp at that
		if (s->extents[i] > 2032) s->extents[i] = 2032;
	}
}


/*
=================
Mod_LoadFaces
=================
*/
void Mod_LoadFaces (lump_t *l)
{
	dface_t		*in;
	msurface_t 	*out;
	int			i, count, surfnum;
	int			planenum, side;

	in = (dface_t *) (mod_base + l->fileofs);

	if (l->filelen % sizeof (*in))
	{
		Host_Error ("Mod_LoadFaces: funny lump size in %s", loadmodel->name);
		return;
	}

	count = l->filelen / sizeof (*in);
	out = (msurface_t *) Heap_TagAlloc (TAG_BRUSHMODELS, count * sizeof (*out));	

	brushmodel->surfaces = out;
	brushmodel->numsurfaces = count;

	for (surfnum = 0; surfnum < count; surfnum++, in++, out++)
	{
		out->firstedge = LittleLong (in->firstedge);
		out->numedges = LittleShort (in->numedges);		
		out->flags = 0;

		planenum = LittleShort (in->planenum);
		side = LittleShort (in->side);

		if (side) out->flags |= SURF_PLANEBACK;			

		out->plane = brushmodel->planes + planenum;

		out->texinfo = brushmodel->texinfo + LittleShort (in->texinfo);

		CalcSurfaceExtents (out);

		// lighting info
		for (i = 0; i < MAXLIGHTMAPS; i++)
			out->styles[i] = in->styles[i];

		i = LittleLong (in->lightofs);

		if (i < 0)
			out->samples = NULL;
		else
			out->samples = brushmodel->lightdata + (i * 3);

		// ensure that every surface loaded has a valid model set
		out->model = loadmodel;

		// initialindexes
		out->numindexes = 0;
		out->indexes = NULL;

		// set the drawing flags flag
		if (!Q_strncmp (out->texinfo->texture->name, "sky", 3))
		{
			// sky surfaces are not subdivided but they store verts in main memory and they don't evaluate texcoords
			out->flags |= (SURF_DRAWSKY | SURF_DRAWTILED);
			GL_DontSubdivideSky (out);
			continue;
		}

		if (out->texinfo->texture->name[0] == '*')
		{
			out->flags |= (SURF_DRAWTURB | SURF_DRAWTILED);

			for (i = 0; i < 2; i++)
			{
				out->extents[i] = 16384;
				out->texturemins[i] = -8192;
			}

			// allocated in main vertex buffer
			continue;
		}

		// check extents against max extents
		if (out->extents[0] > MaxExtents[0]) MaxExtents[0] = out->extents[0];
		if (out->extents[1] > MaxExtents[1]) MaxExtents[1] = out->extents[1];
	}
}


/*
=================
Mod_SetParent
=================
*/
void Mod_SetParent (mnode_t *node, mnode_t *parent)
{
	node->parent = parent;
	if (node->contents < 0)
		return;
	Mod_SetParent (node->children[0], node);
	Mod_SetParent (node->children[1], node);
}

/*
=================
Mod_LoadNodes
=================
*/
void Mod_LoadNodes (lump_t *l)
{
	int			i, j, count, p;
	dnode_t		*in;
	mnode_t 	*out;

	in = (dnode_t *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Host_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = (mnode_t *) Heap_TagAlloc (TAG_BRUSHMODELS, count*sizeof(*out));	

	brushmodel->nodes = out;
	brushmodel->numnodes = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		for (j=0 ; j<3 ; j++)
		{
			out->minmaxs[j] = LittleShort (in->mins[j]);
			out->minmaxs[3+j] = LittleShort (in->maxs[j]);
		}
	
		p = LittleLong(in->planenum);
		out->plane = brushmodel->planes + p;

		out->firstsurface = (unsigned short) LittleShort (in->firstface);
		out->numsurfaces = (unsigned short) LittleShort (in->numfaces);

		out->visframe = -1;
		out->previousvisframe = false;
		
		for (j=0 ; j<2 ; j++)
		{
			p = LittleShort (in->children[j]);
			if (p >= 0)
				out->children[j] = brushmodel->nodes + p;
			else
				out->children[j] = (mnode_t *)(brushmodel->leafs + (-1 - p));
		}
	}
	
	Mod_SetParent (brushmodel->nodes, NULL);	// sets nodes and leafs
}

/*
=================
Mod_LoadLeafs
=================
*/
void Mod_LoadLeafs (lump_t *l)
{
	dleaf_t 	*in;
	mleaf_t 	*out;
	int			i, j, count, p;

	in = (dleaf_t *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Host_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);

	brushmodel->numleafs = count;

	// this will crash in-game, so better to take down as gracefully as possible before that
	if (brushmodel->numleafs > MAX_MAP_LEAFS)
	{
		Host_Error ("Mod_LoadLeafs: brushmodel->numleafs > MAX_MAP_LEAFS");
		return;
	}

	// deferred so excessive leaf counts don't choke the PC
	out = (mleaf_t *) Heap_TagAlloc (TAG_BRUSHMODELS, count * sizeof (mleaf_t));

	brushmodel->leafs = out;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		for (j=0 ; j<3 ; j++)
		{
			out->minmaxs[j] = LittleShort (in->mins[j]);
			out->minmaxs[3+j] = LittleShort (in->maxs[j]);
		}

		p = LittleLong(in->contents);
		out->contents = p;

		out->firstmarksurface = brushmodel->marksurfaces + (unsigned short) LittleShort (in->firstmarksurface);
		out->nummarksurfaces = (unsigned short) LittleShort (in->nummarksurfaces);
		
		out->visframe = -1;
		out->previousvisframe = false;

		p = LittleLong(in->visofs);

		if (p == -1)
			out->compressed_vis = NULL;
		else
			out->compressed_vis = brushmodel->visdata + p;

		for (j = 0; j < 4; j++)
			out->ambient_sound_level[j] = in->ambient_level[j];

		// static entities
		out->statics = NULL;

		// gl underwater warp
		if (out->contents != CONTENTS_EMPTY)
		{
			for (j=0 ; j<out->nummarksurfaces ; j++)
				out->firstmarksurface[j]->flags |= SURF_UNDERWATER;
		}
	}	
}


/*
=================
Mod_LoadClipnodes
=================
*/
void Mod_LoadClipnodes (lump_t *l)
{
	dclipnode_t *in, *out;
	int			i, count;
	hull_t		*hull;

	in = (dclipnode_t *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Host_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = (dclipnode_t *) Heap_TagAlloc (TAG_BRUSHMODELS, count*sizeof(*out));	

	brushmodel->clipnodes = out;
	brushmodel->numclipnodes = count;

	hull = &brushmodel->hulls[1];
	hull->clipnodes = out;
	hull->firstclipnode = 0;
	hull->lastclipnode = count-1;
	hull->planes = brushmodel->planes;
	hull->clip_mins[0] = -16;
	hull->clip_mins[1] = -16;
	hull->clip_mins[2] = -24;
	hull->clip_maxs[0] = 16;
	hull->clip_maxs[1] = 16;
	hull->clip_maxs[2] = 32;

	hull = &brushmodel->hulls[2];
	hull->clipnodes = out;
	hull->firstclipnode = 0;
	hull->lastclipnode = count-1;
	hull->planes = brushmodel->planes;
	hull->clip_mins[0] = -32;
	hull->clip_mins[1] = -32;
	hull->clip_mins[2] = -24;
	hull->clip_maxs[0] = 32;
	hull->clip_maxs[1] = 32;
	hull->clip_maxs[2] = 64;

	for (i=0 ; i<count ; i++, out++, in++)
	{
		out->planenum = LittleLong(in->planenum);
		out->children[0] = LittleShort(in->children[0]);
		out->children[1] = LittleShort(in->children[1]);
	}
}

/*
=================
Mod_MakeHull0

Duplicate the drawing hull structure as a clipping hull
=================
*/
void Mod_MakeHull0 (void)
{
	mnode_t		*in, *child;
	dclipnode_t *out;
	int			i, j, count;
	hull_t		*hull;
	
	hull = &brushmodel->hulls[0];	
	
	in = brushmodel->nodes;
	count = brushmodel->numnodes;
	out = (dclipnode_t *) Heap_TagAlloc (TAG_BRUSHMODELS, count*sizeof(*out));	

	hull->clipnodes = out;
	hull->firstclipnode = 0;
	hull->lastclipnode = count-1;
	hull->planes = brushmodel->planes;

	for (i=0 ; i<count ; i++, out++, in++)
	{
		out->planenum = in->plane - brushmodel->planes;
		for (j=0 ; j<2 ; j++)
		{
			child = in->children[j];
			if (child->contents < 0)
				out->children[j] = child->contents;
			else
				out->children[j] = child - brushmodel->nodes;
		}
	}
}

/*
=================
Mod_LoadMarksurfaces
=================
*/
void Mod_LoadMarksurfaces (lump_t *l)
{	
	int		i, j, count;
	short		*in;
	msurface_t **out;
	
	in = (short *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Host_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = (msurface_t **) Heap_TagAlloc (TAG_BRUSHMODELS, count*sizeof(*out));	

	brushmodel->marksurfaces = out;
	brushmodel->nummarksurfaces = count;

	for ( i=0 ; i<count ; i++)
	{
		j = (unsigned short) LittleShort (in[i]);
		if (j >= brushmodel->numsurfaces)
			Host_Error ("Mod_ParseMarksurfaces: bad surface number");
		out[i] = brushmodel->surfaces + j;
	}
}

/*
=================
Mod_LoadSurfedges
=================
*/
void Mod_LoadSurfedges (lump_t *l)
{	
	int		i, count;
	int		*in, *out;
	
	in = (int *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Host_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = (int *) Heap_TagAlloc (TAG_BRUSHMODELS, count*sizeof(*out));	

	brushmodel->surfedges = out;
	brushmodel->numsurfedges = count;

	for ( i=0 ; i<count ; i++)
		out[i] = LittleLong (in[i]);
}


/*
=================
Mod_LoadPlanes
=================
*/
void Mod_LoadPlanes (lump_t *l)
{
	int			i, j;
	mplane_t	*out;
	dplane_t 	*in;
	int			count;
	int			bits;
	
	in = (dplane_t *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Host_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = (mplane_t *) Heap_TagAlloc (TAG_BRUSHMODELS, count*2*sizeof(*out));	
	
	brushmodel->planes = out;
	brushmodel->numplanes = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		bits = 0;
		for (j=0 ; j<3 ; j++)
		{
			out->normal[j] = LittleFloat (in->normal[j]);
			if (out->normal[j] < 0)
				bits |= 1<<j;
		}

		out->dist = LittleFloat (in->dist);
		out->type = LittleLong (in->type);
		out->signbits = bits;
	}
}

/*
=================
RadiusFromBounds
=================
*/
float RadiusFromBounds (vec3_t mins, vec3_t maxs)
{
	int		i;
	vec3_t	corner;

	for (i=0 ; i<3 ; i++)
	{
		corner[i] = fabs(mins[i]) > fabs(maxs[i]) ? fabs(mins[i]) : fabs(maxs[i]);
	}

	return Length (corner);
}


void *CopyLump (lump_t *l)
{
	byte *data = (byte *) Heap_TagAlloc (TAG_BRUSHMODELS, l->filelen);
	memcpy (data, mod_base + l->fileofs, l->filelen);

	return data;
}


/*
=================
Mod_LoadBrushModel
=================
*/
void Mod_LoadBrushModel (model_t *mod, void *buffer)
{
	int			i, j;
	dheader_t	*header;
	dmodel_t 	*bm;

	loadmodel->type = mod_brush;

	header = (dheader_t *) buffer;

	i = LittleLong (header->version);

	// drop to console
	if (i != BSPVERSION)
		Host_Error ("Mod_LoadBrushModel: %s has wrong version number (%i should be %i)", mod->name, i, BSPVERSION);

	// swap all the lumps
	mod_base = (byte *) header;

	for (i = 0; i < sizeof (dheader_t) / 4; i++)
		((int *) header)[i] = LittleLong (((int *) header)[i]);

	// alloc space for a brush header
	mod->bh = (brushhdr_t *) Heap_TagAlloc (TAG_BRUSHMODELS, sizeof (brushhdr_t));
	brushmodel = mod->bh;

	// set up a matrix for the model
	mod->matrix = Heap_TagAlloc (TAG_BRUSHMODELS, sizeof (D3DXMATRIX));

	// set to identity (this will change for brush models but needs to be retained for the world)
	D3DXMatrixIdentity ((D3DXMATRIX *) mod->matrix);

	// load into heap
	Mod_LoadVertexes (&header->lumps[LUMP_VERTEXES]);
	Mod_LoadEdges (&header->lumps[LUMP_EDGES]);
	Mod_LoadSurfedges (&header->lumps[LUMP_SURFEDGES]);
	Mod_LoadTextures (&header->lumps[LUMP_TEXTURES]);
	Mod_LoadLighting (&header->lumps[LUMP_LIGHTING]);
	Mod_LoadPlanes (&header->lumps[LUMP_PLANES]);
	Mod_LoadTexinfo (&header->lumps[LUMP_TEXINFO]);
	Mod_LoadFaces (&header->lumps[LUMP_FACES]);
	Mod_LoadMarksurfaces (&header->lumps[LUMP_MARKSURFACES]);
	Mod_LoadVisibility (&header->lumps[LUMP_VISIBILITY]);
	Mod_LoadLeafs (&header->lumps[LUMP_LEAFS]);
	Mod_LoadNodes (&header->lumps[LUMP_NODES]);
	Mod_LoadClipnodes (&header->lumps[LUMP_CLIPNODES]);
	Mod_LoadEntities (&header->lumps[LUMP_ENTITIES]);
	Mod_LoadSubmodels (&header->lumps[LUMP_MODELS]);

	Mod_MakeHull0 ();

	for (i = 0; i < header->lumps[LUMP_ENTITIES].filelen; i++)
	{
		char *entpt = (char *) (mod_base + header->lumps[LUMP_ENTITIES].fileofs + i);

		if (!strnicmp (entpt, "info_player_", 12))
		{
			Con_DPrintf ("Making tnodes for %s\n", loadmodel->name);

			// copy out the disk lumps we need for tracing
			dnodes = (dnode_t *) CopyLump (&header->lumps[LUMP_NODES]);
			dplanes = (dplane_t *) CopyLump (&header->lumps[LUMP_PLANES]);
			dleafs = (dleaf_t *) CopyLump (&header->lumps[LUMP_LEAFS]);

			// make the trace nodes
			MakeTnodes (loadmodel->bh->numnodes);

			break;
		}
	}

	// regular and alternate animation
	mod->numframes = 2;

	// set up the submodels (FIXME: this is confusing)
	// first pass fills in for the world (which is it's own first submodel), then grabs a submodel slot off the list.
	// subsequent passes fill in the submodel slot grabbed at the end of the previous pass.
	// the last pass doesn't need to grab a submodel slot as everything is already filled in
	// fucking hell, he wasn't joking!
	for (i = 0; i < mod->bh->numsubmodels; i++)
	{
		// retrieve the submodel
		bm = &mod->bh->submodels[i];

		// fill in submodel specific stuff
		mod->bh->hulls[0].firstclipnode = bm->headnode[0];

		for (j = 1; j < MAX_MAP_HULLS; j++)
		{
			// clipnodes
			mod->bh->hulls[j].firstclipnode = bm->headnode[j];
			mod->bh->hulls[j].lastclipnode = mod->bh->numclipnodes - 1;
		}

		// first surf in the inline model and number of surfs in it
		mod->bh->firstmodelsurface = bm->firstface;
		mod->bh->nummodelsurfaces = bm->numfaces;

		// leafs
		mod->bh->numleafs = bm->visleafs;

		// bounding box
		VectorCopy (bm->maxs, mod->maxs);
		VectorCopy (bm->mins, mod->mins);

		// radius
		mod->radius = RadiusFromBounds (mod->mins, mod->maxs);

		if (i < mod->bh->numsubmodels - 1)
		{
			// duplicate the basic information
			char name[10];

			// build the name
			sprintf (name, "*%i", i + 1);

			// get a slot from the models allocation
			model_t *inlinemod = Mod_FindName (name);

			// duplicate the data
			memcpy (inlinemod, mod, sizeof (model_t));

			// allocate a new header for the model
			inlinemod->bh = (brushhdr_t *) Heap_TagAlloc (TAG_BRUSHMODELS, sizeof (brushhdr_t));

			// copy the header data from the original model
			memcpy (inlinemod->bh, mod->bh, sizeof (brushhdr_t));

			// also do the matrix
			inlinemod->matrix = Heap_TagAlloc (TAG_BRUSHMODELS, sizeof (D3DXMATRIX));
			D3DXMatrixIdentity ((D3DXMATRIX *) inlinemod->matrix);

			// write in the name
			strcpy (inlinemod->name, name);

			// point mod to the inline model we just got for filling in at the next iteration
			mod = inlinemod;
		}
	}
}


/*
==============================================================================

ALIAS MODELS

==============================================================================
*/

aliashdr_t	*pheader;

stvert_t	*stverts = NULL;
mtriangle_t	*triangles;

// a pose is a single set of vertexes.  a frame may be
// an animating sequence of poses
trivertx_t **ThePoseverts = NULL;

int			posenum;

byte		**player_8bit_texels_tbl;
byte		*player_8bit_texels;

// FIXME - store per frame rather than for the entire model???
float aliasbboxmins[3], aliasbboxmaxs[3];


/*
=================
Mod_LoadAliasFrame
=================
*/
void *Mod_LoadAliasFrame (void *pin, maliasframedesc_t *frame)
{
	trivertx_t		*pinframe;
	int				i;
	daliasframe_t	*pdaliasframe;
	
	pdaliasframe = (daliasframe_t *) pin;

	strcpy (frame->name, pdaliasframe->name);
	frame->firstpose = posenum;
	frame->numposes = 1;

	for (i = 0; i < 3; i++)
	{
		frame->bboxmin.v[i] = pdaliasframe->bboxmin.v[i];
		frame->bboxmax.v[i] = pdaliasframe->bboxmax.v[i];

		if (frame->bboxmin.v[i] < aliasbboxmins[i]) aliasbboxmins[i] = frame->bboxmin.v[i];
		if (frame->bboxmax.v[i] > aliasbboxmaxs[i]) aliasbboxmaxs[i] = frame->bboxmax.v[i];
	}

	pinframe = (trivertx_t *) (pdaliasframe + 1);

	ThePoseverts[posenum] = pinframe;
	posenum++;

	pinframe += pheader->numverts;

	return (void *)pinframe;
}


void *Mod_CountAliasFrame (void *pin)
{
	daliasframe_t *pdaliasframe = (daliasframe_t *) pin;
	trivertx_t *pinframe = (trivertx_t *) (pdaliasframe + 1);

	posenum++;

	return (void *) (pinframe + pheader->numverts);
}


/*
=================
Mod_LoadAliasGroup
=================
*/
void *Mod_LoadAliasGroup (void *pin, maliasframedesc_t *frame)
{
	daliasgroup_t		*pingroup;
	int					i, numframes;
	daliasinterval_t	*pin_intervals;
	void				*ptemp;

	pingroup = (daliasgroup_t *) pin;

	numframes = LittleLong (pingroup->numframes);

	frame->firstpose = posenum;
	frame->numposes = numframes;

	for (i = 0; i < 3; i++)
	{
		frame->bboxmin.v[i] = pingroup->bboxmin.v[i];
		frame->bboxmax.v[i] = pingroup->bboxmax.v[i];

		if (frame->bboxmin.v[i] < aliasbboxmins[i]) aliasbboxmins[i] = frame->bboxmin.v[i];
		if (frame->bboxmax.v[i] > aliasbboxmaxs[i]) aliasbboxmaxs[i] = frame->bboxmax.v[i];
	}

	pin_intervals = (daliasinterval_t *) (pingroup + 1);

	frame->interval = LittleFloat (pin_intervals->interval);

	pin_intervals += numframes;

	ptemp = (void *) pin_intervals;

	for (i = 0; i < numframes; i++)
	{
		ThePoseverts[posenum] = (trivertx_t *) ((daliasframe_t *) ptemp + 1);
		posenum++;

		ptemp = (trivertx_t *) ((daliasframe_t *) ptemp + 1) + pheader->numverts;
	}

	return ptemp;
}


void *Mod_CountAliasGroup (void *pin)
{
	daliasgroup_t *pingroup = (daliasgroup_t *) pin;
	int numframes = LittleLong (pingroup->numframes);

	daliasinterval_t *pin_intervals = (daliasinterval_t *) (pingroup + 1);
	pin_intervals += numframes;

	void *ptemp = (void *) pin_intervals;

	for (int i = 0; i < numframes; i++)
	{
		posenum++;
		ptemp = (trivertx_t *) ((daliasframe_t *) ptemp + 1) + pheader->numverts;
	}

	return ptemp;
}

//=========================================================

/*
=================
Mod_FloodFillSkin

Fill background pixels so mipmapping doesn't have haloes - Ed
=================
*/

typedef struct
{
	short		x, y;
} floodfill_t;

extern unsigned d_8to24table[];

// must be a power of 2
#define FLOODFILL_FIFO_SIZE 0x1000
#define FLOODFILL_FIFO_MASK (FLOODFILL_FIFO_SIZE - 1)

#define FLOODFILL_STEP( off, dx, dy ) \
{ \
	if (pos[off] == fillcolor) \
	{ \
		pos[off] = 255; \
		fifo[inpt].x = x + (dx), fifo[inpt].y = y + (dy); \
		inpt = (inpt + 1) & FLOODFILL_FIFO_MASK; \
	} \
	else if (pos[off] != 255) fdc = pos[off]; \
}

void Mod_FloodFillSkin (byte *skin, int skinwidth, int skinheight)
{
	byte				fillcolor = *skin; // assume this is the pixel to fill
	floodfill_t			fifo[FLOODFILL_FIFO_SIZE];
	int					inpt = 0, outpt = 0;
	int					filledcolor = -1;
	int					i;

	if (filledcolor == -1)
	{
		filledcolor = 0;
		// attempt to find opaque black
		for (i = 0; i < 256; ++i)
			if (d_8to24table[i] == (255 << 0)) // alpha 1.0
			{
				filledcolor = i;
				break;
			}
	}

	// can't fill to filled color or to transparent color (used as visited marker)
	if ((fillcolor == filledcolor) || (fillcolor == 255))
	{
		//printf( "not filling skin from %d to %d\n", fillcolor, filledcolor );
		return;
	}

	fifo[inpt].x = 0, fifo[inpt].y = 0;
	inpt = (inpt + 1) & FLOODFILL_FIFO_MASK;

	while (outpt != inpt)
	{
		int			x = fifo[outpt].x, y = fifo[outpt].y;
		int			fdc = filledcolor;
		byte		*pos = &skin[x + skinwidth * y];

		outpt = (outpt + 1) & FLOODFILL_FIFO_MASK;

		if (x > 0)				FLOODFILL_STEP( -1, -1, 0 );
		if (x < skinwidth - 1)	FLOODFILL_STEP( 1, 1, 0 );
		if (y > 0)				FLOODFILL_STEP( -skinwidth, 0, -1 );
		if (y < skinheight - 1)	FLOODFILL_STEP( skinwidth, 0, 1 );
		skin[x + skinwidth * y] = fdc;
	}
}

/*
===============
Mod_LoadAllSkins
===============
*/
void *Mod_LoadAllSkins (int numskins, daliasskintype_t *pskintype)
{
	int		i, j, k;
	char	name[32];
	int		s;
	byte	*skin;
	byte	*texels;
	daliasskingroup_t		*pinskingroup;
	int		groupskins;
	daliasskininterval_t	*pinskinintervals;
	
	skin = (byte *) (pskintype + 1);

	if (numskins < 1 || numskins > MAX_SKINS)
		Host_Error ("Mod_LoadAliasModel: Invalid # of skins: %d\n", numskins);

	s = pheader->skinwidth * pheader->skinheight;

	// remove the extension so that textures load properly
	for (i = strlen (loadmodel->name); i; i--)
	{
		if (loadmodel->name[i] == '.')
		{
			loadmodel->name[i] = 0;
			break;
		}
	}

	for (i = 0; i < numskins; i++)
	{
		if (pskintype->type == ALIAS_SKIN_SINGLE)
		{
			Mod_FloodFillSkin( skin, pheader->skinwidth, pheader->skinheight );

			// save 8 bit texels for the player model to remap
			// NOTE - we have no guarantee that the player model will be called "player.mdl"
	//		if (!strcmp(loadmodel->name,"progs/player.mdl"))
	//		{
				texels = (byte *) Heap_TagAlloc (TAG_ALIASMODELS, s);
				pheader->texels[i] = texels - (byte *)pheader;
				memcpy (texels, (byte *)(pskintype + 1), s);
	//		}

			sprintf (name, "%s_%i", loadmodel->name, i);

			pheader->texture[i][0] =
			pheader->texture[i][1] =
			pheader->texture[i][2] =
			pheader->texture[i][3] = D3D_LoadTexture
			(
				name,
				pheader->skinwidth, 
				pheader->skinheight,
				(byte *) (pskintype + 1),
				IMAGE_MIPMAP | IMAGE_ALIAS
			);

			// load fullbright
			pheader->fullbright[i][0] =
			pheader->fullbright[i][1] =
			pheader->fullbright[i][2] =
			pheader->fullbright[i][3] = D3D_LoadTexture
			(
				name,
				pheader->skinwidth, 
				pheader->skinheight,
				(byte *) (pskintype + 1),
				IMAGE_MIPMAP | IMAGE_ALIAS | IMAGE_LUMA
			);

			pskintype = (daliasskintype_t *)((byte *)(pskintype+1) + s);
		}
		else
		{
			// animating skin group.  yuck.
			pskintype++;
			pinskingroup = (daliasskingroup_t *)pskintype;
			groupskins = LittleLong (pinskingroup->numskins);
			pinskinintervals = (daliasskininterval_t *)(pinskingroup + 1);

			pskintype = (daliasskintype_t *)(pinskinintervals + groupskins);

			for (j = 0; j < groupskins; j++)
			{
				Mod_FloodFillSkin( skin, pheader->skinwidth, pheader->skinheight );

				if (j == 0)
				{
					texels = (byte *) Heap_TagAlloc (TAG_ALIASMODELS, s);
					pheader->texels[i] = texels - (byte *)pheader;
					memcpy (texels, (byte *)(pskintype), s);
				}

				sprintf (name, "%s_%i_%i", loadmodel->name, i,j);

				pheader->texture[i][j & 3] = D3D_LoadTexture 
				(
					name,
					pheader->skinwidth, 
					pheader->skinheight,
					(byte *) (pskintype),
					IMAGE_MIPMAP | IMAGE_ALIAS
				);

				pheader->fullbright[i][j & 3] = D3D_LoadTexture 
				(
					name,
					pheader->skinwidth, 
					pheader->skinheight,
					(byte *) (pskintype),
					IMAGE_MIPMAP | IMAGE_ALIAS | IMAGE_LUMA
				);

				pskintype = (daliasskintype_t *)((byte *)(pskintype) + s);
			}

			k = j;

			// fill in any skins that weren't loaded
			for (; j < 4; j++)
			{
				pheader->texture[i][j & 3] = pheader->texture[i][j - k]; 
				pheader->fullbright[i][j & 3] = pheader->fullbright[i][j - k]; 
			}
		}
	}

	// restore the extension so that model reuse works
	strcat (loadmodel->name, ".mdl");

	return (void *) pskintype;
}

//=========================================================================

/*
=================
Mod_LoadAliasModel
=================
*/
void Mod_LoadAliasModel (model_t *mod, void *buffer)
{
	int					i, j;
	mdl_t				*pinmodel;
	stvert_t			*pinstverts;
	dtriangle_t			*pintriangles;
	int					version, numframes;
	int					size;
	daliasframetype_t	*pframetype;
	daliasskintype_t	*pskintype;
	int					start, end, total;

	pinmodel = (mdl_t *) buffer;

	version = LittleLong (pinmodel->version);

	if (version != ALIAS_VERSION)
		Host_Error ("%s has wrong version number (%i should be %i)", mod->name, version, ALIAS_VERSION);

	// allocate space for a working header, plus all the data except the frames,
	// skin and group info
	size = 	sizeof (aliashdr_t) + (LittleLong (pinmodel->numframes) - 1) * sizeof (maliasframedesc_t);

	pheader = (aliashdr_t *) Heap_TagAlloc (TAG_ALIASMODELS, size);

	mod->flags = LittleLong (pinmodel->flags);

	// endian-adjust and copy the data, starting with the alias model header
	pheader->boundingradius = LittleFloat (pinmodel->boundingradius);
	pheader->numskins = LittleLong (pinmodel->numskins);
	pheader->skinwidth = LittleLong (pinmodel->skinwidth);
	pheader->skinheight = LittleLong (pinmodel->skinheight);
	pheader->numverts = LittleLong (pinmodel->numverts);
	pheader->numtris = LittleLong (pinmodel->numtris);
	pheader->numframes = LittleLong (pinmodel->numframes);
	numframes = pheader->numframes;

	// validate the setup
	// Sys_Error seems a little harsh here...
	if (numframes < 1) Host_Error ("Mod_LoadAliasModel: Invalid # of frames: %d\n", numframes);
	if (pheader->numtris <= 0) Host_Error ("model %s has no triangles", mod->name);
	if (pheader->numverts <= 0) Host_Error ("model %s has no vertices", mod->name);

	pheader->size = LittleFloat (pinmodel->size) * ALIAS_BASE_SIZE_RATIO;
	mod->synctype = (synctype_t) LittleLong (pinmodel->synctype);
	mod->numframes = pheader->numframes;

	for (i=0 ; i<3 ; i++)
	{
		pheader->scale[i] = LittleFloat (pinmodel->scale[i]);
		pheader->scale_origin[i] = LittleFloat (pinmodel->scale_origin[i]);
		pheader->eyeposition[i] = LittleFloat (pinmodel->eyeposition[i]);
	}

	// load the skins
	pskintype = (daliasskintype_t *)&pinmodel[1];
	pskintype = (daliasskintype_t *) Mod_LoadAllSkins (pheader->numskins, pskintype);

	// load base s and t vertices
	pinstverts = (stvert_t *) pskintype;
	stverts = (stvert_t *) Heap_QMalloc (pheader->numverts * sizeof (stvert_t));

	for (i = 0; i < pheader->numverts; i++)
	{
		stverts[i].onseam = LittleLong (pinstverts[i].onseam);
		stverts[i].s = LittleLong (pinstverts[i].s);
		stverts[i].t = LittleLong (pinstverts[i].t);
	}

	// load triangle lists
	pintriangles = (dtriangle_t *) &pinstverts[pheader->numverts];
	triangles = (mtriangle_t *) Heap_QMalloc (pheader->numtris * sizeof (mtriangle_t));

	for (i = 0; i < pheader->numtris; i++)
	{
		triangles[i].facesfront = LittleLong (pintriangles[i].facesfront);

		for (j = 0; j < 3; j++) triangles[i].vertindex[j] = LittleLong (pintriangles[i].vertindex[j]);
	}

	// count the frames
	// we need to do this as a separate pass as group frames will mean that pheader->numframes will not
	// always be valid here...
	pframetype = (daliasframetype_t *) &pintriangles[pheader->numtris];

	for (i = 0, posenum = 0; i < numframes; i++)
	{
		aliasframetype_t frametype = (aliasframetype_t) LittleLong (pframetype->type);

		if (frametype == ALIAS_SINGLE)
			pframetype = (daliasframetype_t *) Mod_CountAliasFrame (pframetype + 1);
		else pframetype = (daliasframetype_t *) Mod_CountAliasGroup (pframetype + 1);
	}

	// now that we know how many frames we need we can alloc verts for them
	if (ThePoseverts) free (ThePoseverts);
	ThePoseverts = (trivertx_t **) Heap_QMalloc (posenum * sizeof (trivertx_t *));

	// now load the frames for real
	pframetype = (daliasframetype_t *) &pintriangles[pheader->numtris];

	// initial bbox
	aliasbboxmins[0] = aliasbboxmins[1] = aliasbboxmins[2] = 9999999;
	aliasbboxmaxs[0] = aliasbboxmaxs[1] = aliasbboxmaxs[2] = -9999999;

	for (i = 0, posenum = 0; i < numframes; i++)
	{
		aliasframetype_t frametype = (aliasframetype_t) LittleLong (pframetype->type);

		if (frametype == ALIAS_SINGLE)
			pframetype = (daliasframetype_t *) Mod_LoadAliasFrame (pframetype + 1, &pheader->frames[i]);
		else pframetype = (daliasframetype_t *) Mod_LoadAliasGroup (pframetype + 1, &pheader->frames[i]);
	}

	pheader->numposes = posenum;
	mod->type = mod_alias;

	for (i = 0; i < 3; i++)
	{
		mod->mins[i] = aliasbboxmins[i] * pheader->scale[i] + pheader->scale_origin[i];
		mod->maxs[i] = aliasbboxmaxs[i] * pheader->scale[i] + pheader->scale_origin[i];

		// clamp to absolutes
		if (mod->mins[i] > -16) mod->mins[i] = -16;
		if (mod->maxs[i] < 16) mod->maxs[i] = 16;
	}

	// take higher of maxs[0] and [1] and lower of mins[0] and [1] as final values for both
	// this resolves issues with rotating weapon models being clipped when they shouldn't
	if (mod->maxs[0] > mod->maxs[1]) mod->maxs[1] = mod->maxs[0];
	if (mod->maxs[1] > mod->maxs[0]) mod->maxs[0] = mod->maxs[1];
	if (mod->mins[0] < mod->mins[1]) mod->mins[1] = mod->mins[0];
	if (mod->mins[1] < mod->mins[0]) mod->mins[0] = mod->mins[1];

	// build the draw lists
	GL_MakeAliasModelDisplayLists (mod, pheader);

	// always lerp it until we determine otherwise
	pheader->nolerp = false;

	// no caching
	mod->ah = pheader;

	// free our temp lists
	Heap_QFree (ThePoseverts);
	Heap_QFree (stverts);
	Heap_QFree (triangles);
}


//=============================================================================

/*
=================
Mod_LoadSpriteFrame
=================
*/
void *Mod_LoadSpriteFrame (void * pin, mspriteframe_t **ppframe, int framenum)
{
	dspriteframe_t		*pinframe;
	mspriteframe_t		*pspriteframe;
	int					width, height, size, origin[2];
	char				name[64];

	pinframe = (dspriteframe_t *)pin;

	width = LittleLong (pinframe->width);
	height = LittleLong (pinframe->height);
	size = width * height;

	pspriteframe = (mspriteframe_t *) Heap_TagAlloc (TAG_SPRITEMODELS, sizeof (mspriteframe_t));

	Q_memset (pspriteframe, 0, sizeof (mspriteframe_t));

	*ppframe = pspriteframe;

	pspriteframe->width = width;
	pspriteframe->height = height;
	origin[0] = LittleLong (pinframe->origin[0]);
	origin[1] = LittleLong (pinframe->origin[1]);

	pspriteframe->up = origin[1];
	pspriteframe->down = origin[1] - height;
	pspriteframe->left = origin[0];
	pspriteframe->right = width + origin[0];

	sprintf (name, "%s_%i", loadmodel->name, framenum);

	pspriteframe->texture = D3D_LoadTexture
	(
		name,
		width,
		height,
		(byte *) (pinframe + 1),
		IMAGE_MIPMAP | IMAGE_ALPHA | IMAGE_SPRITE
	);

	return (void *) ((byte *) pinframe + sizeof (dspriteframe_t) + size);
}


/*
=================
Mod_LoadSpriteGroup
=================
*/
void *Mod_LoadSpriteGroup (void * pin, mspriteframe_t **ppframe, int framenum)
{
	dspritegroup_t		*pingroup;
	mspritegroup_t		*pspritegroup;
	int					i, numframes;
	dspriteinterval_t	*pin_intervals;
	float				*poutintervals;
	void				*ptemp;

	pingroup = (dspritegroup_t *)pin;

	numframes = LittleLong (pingroup->numframes);

	pspritegroup = (mspritegroup_t *) Heap_TagAlloc (TAG_SPRITEMODELS, sizeof (mspritegroup_t) +
				(numframes - 1) * sizeof (pspritegroup->frames[0]));

	pspritegroup->numframes = numframes;

	*ppframe = (mspriteframe_t *)pspritegroup;

	pin_intervals = (dspriteinterval_t *)(pingroup + 1);

	poutintervals = (float *) Heap_TagAlloc (TAG_SPRITEMODELS, numframes * sizeof (float));

	pspritegroup->intervals = poutintervals;

	for (i=0 ; i<numframes ; i++)
	{
		*poutintervals = LittleFloat (pin_intervals->interval);
		if (*poutintervals <= 0.0)
			Host_Error ("Mod_LoadSpriteGroup: interval<=0");

		poutintervals++;
		pin_intervals++;
	}

	ptemp = (void *)pin_intervals;

	for (i=0 ; i<numframes ; i++)
	{
		ptemp = Mod_LoadSpriteFrame (ptemp, &pspritegroup->frames[i], framenum * 100 + i);
	}

	return ptemp;
}


/*
=================
Mod_LoadSpriteModel
=================
*/
void Mod_LoadSpriteModel (model_t *mod, void *buffer)
{
	int					i;
	int					version;
	dsprite_t			*pin;
	msprite_t			*psprite;
	int					numframes;
	int					size;
	dspriteframetype_t	*pframetype;
	
	pin = (dsprite_t *) buffer;

	version = LittleLong (pin->version);
	if (version != SPRITE_VERSION) Host_Error ("%s has wrong version number (%i should be %i)", mod->name, version, SPRITE_VERSION);

	numframes = LittleLong (pin->numframes);

	size = sizeof (msprite_t) +	(numframes - 1) * sizeof (psprite->frames);

	psprite = (msprite_t *) Heap_TagAlloc (TAG_SPRITEMODELS, size);

	mod->sh = psprite;

	psprite->type = LittleLong (pin->type);
	psprite->maxwidth = LittleLong (pin->width);
	psprite->maxheight = LittleLong (pin->height);
	psprite->beamlength = LittleFloat (pin->beamlength);
	mod->synctype = (synctype_t) LittleLong (pin->synctype);
	psprite->numframes = numframes;

	mod->mins[0] = mod->mins[1] = -psprite->maxwidth/2;
	mod->maxs[0] = mod->maxs[1] = psprite->maxwidth/2;
	mod->mins[2] = -psprite->maxheight/2;
	mod->maxs[2] = psprite->maxheight/2;

	// load the frames
	if (numframes < 1)
		Host_Error ("Mod_LoadSpriteModel: Invalid # of frames: %d\n", numframes);

	mod->numframes = numframes;

	pframetype = (dspriteframetype_t *)(pin + 1);

	// remove the extension so that textures load properly
	for (i = strlen (mod->name); i; i--)
	{
		if (mod->name[i] == '.')
		{
			mod->name[i] = 0;
			break;
		}
	}

	for (i = 0; i < numframes; i++)
	{
		spriteframetype_t	frametype;

		frametype = (spriteframetype_t) LittleLong (pframetype->type);
		psprite->frames[i].type = frametype;

		if (frametype == SPR_SINGLE)
			pframetype = (dspriteframetype_t *) Mod_LoadSpriteFrame (pframetype + 1, &psprite->frames[i].frameptr, i);
		else pframetype = (dspriteframetype_t *) Mod_LoadSpriteGroup (pframetype + 1, &psprite->frames[i].frameptr, i);
	}

	// restore the extension so that model reuse works
	strcat (mod->name, ".spr");

	mod->type = mod_sprite;
}

//=============================================================================

/*
================
Mod_Print
================
*/
void Mod_Print (void)
{
	int		i;

	Con_Printf ("Cached models:\n");

	for (i = 0; i < mod_numknown; i++)
	{
		if (mod_known[i]->type != mod_alias) continue;

		Con_Printf ("%8p : %s\n", mod_known[i]->ah, mod_known[i]->name);
	}
}


