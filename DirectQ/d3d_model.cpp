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
// models.c -- model loading and caching

// models are the only shared resource between a client and server running
// on the same machine.

#include "quakedef.h"
#include "d3d_model.h"
#include "d3d_quake.h"

void D3DSky_InitTextures (miptex_t *mt);
void D3DAlias_MakeAliasMesh (char *name, byte *hash, aliashdr_t *hdr, stvert_t *stverts, dtriangle_t *triangles);

void Mod_LoadSpriteModel (model_t *mod, void *buffer);
void Mod_LoadBrushModel (model_t *mod, void *buffer);
void Mod_LoadAliasModel (model_t *mod, void *buffer);
model_t *Mod_LoadModel (model_t *mod, bool crash);

// imports from sv_main
extern byte *fatpvs;
extern int fatbytes;
extern cvar_t sv_pvsfat;

// imported from pr_cmds
extern byte *checkpvs;

// local
byte	*mod_novis;

#define	MAX_MOD_KNOWN	8192
model_t	**mod_known = NULL;
int		mod_numknown = 0;

#define BBEXPAND	0.001f

/*
=================
RadiusFromBounds
=================
*/
float RadiusFromBounds (vec3_t mins, vec3_t maxs)
{
	int		i;
	vec3_t	corner;

	for (i = 0; i < 3; i++)
		corner[i] = fabs (mins[i]) > fabs (maxs[i]) ? fabs (mins[i]) : fabs (maxs[i]);

	return Length (corner);
}


/*
===============
Mod_Init

===============
*/
void Mod_Init (void)
{
	mod_known = (model_t **) Zone_Alloc (MAX_MOD_KNOWN * sizeof (model_t *));
	memset (mod_known, 0, MAX_MOD_KNOWN * sizeof (model_t *));
}


/*
===============
Mod_InitForMap

===============
*/
void Mod_InitForMap (model_t *mod)
{
	// only alloc as much as we actually need
	mod_novis = (byte *) MainHunk->Alloc ((mod->brushhdr->numleafs + 7) / 8, FALSE);
	memset (mod_novis, 0xff, (mod->brushhdr->numleafs + 7) / 8);
	checkpvs = (byte *) MainHunk->Alloc ((mod->brushhdr->numleafs + 7) / 8, FALSE);

	fatbytes = (mod->brushhdr->numleafs + 31) >> 3;
	fatpvs = (byte *) MainHunk->Alloc (fatbytes, FALSE);
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

	if (!model || !model->brushhdr->nodes)
		Host_Error ("Mod_PointInLeaf: bad model");

	node = model->brushhdr->nodes;

	while (1)
	{
		if (node->contents < 0)
			return (mleaf_t *) node;

		plane = node->plane;
		d = DotProduct (p, plane->normal) - plane->dist;

		if (d > 0)
			node = node->children[0];
		else node = node->children[1];
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

	int row = (model->brushhdr->numleafs + 7) >> 3;
	byte *out = scratchbuf;

	if (!in)
	{
		// no vis info, so make all visible
		while (row)
		{
			*out++ = 0xff;
			row--;
		}

		return scratchbuf;
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
	} while (out - scratchbuf < row);

	return scratchbuf;
}


byte *Mod_LeafPVS (mleaf_t *leaf, model_t *model)
{
	if (leaf == model->brushhdr->leafs)
		return mod_novis;

	return Mod_DecompressVis (leaf->compressed_vis, model);
}


void Mod_AddToFatPVS (vec3_t org, mnode_t *node)
{
	int		i;
	byte	*pvs;
	mplane_t	*plane;
	float	d;

	// optimized recursion without a goto! whee!
	while (1)
	{
		// if this is a leaf, accumulate the pvs bits
		if (node->contents < 0)
		{
			if (node->contents != CONTENTS_SOLID)
			{
				pvs = Mod_LeafPVS ((mleaf_t *) node, cl.worldmodel);

				for (i = 0; i < fatbytes; i++) fatpvs[i] |= pvs[i];
			}

			return;
		}

		plane = node->plane;

		d = DotProduct (org, plane->normal) - plane->dist;

		// add extra fatness here as the server-side fatness is not sufficient
		if (d > 120 + sv_pvsfat.value)
			node = node->children[0];
		else if (d < - (120 + sv_pvsfat.value))
			node = node->children[1];
		else
		{
			// go down both
			Mod_AddToFatPVS (org, node->children[0]);
			node = node->children[1];
		}
	}
}


byte *Mod_FatPVS (vec3_t org)
{
	// it's expected that cl.worldmodel will be the same as sv.worldmodel. ;)
	memset (fatpvs, 0, fatbytes);
	Mod_AddToFatPVS (org, cl.worldmodel->brushhdr->nodes);

	return fatpvs;
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
		// this is also done for cached models as the mod_known array is managed separately
		mod_known[i] = (model_t *) MainHunk->Alloc (sizeof (model_t));

		Q_strncpy (mod_known[i]->name, name, 63);
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
void Mod_TouchModel (char *name) {model_t *mod = Mod_FindName (name);}


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

	// look for a cached copy
	model_t *cachemod = (model_t *) MainCache->Check (mod->name);

	if (cachemod)
	{
		// did we find it?
		memcpy (mod, cachemod, sizeof (model_t));

		// don't need to load it
		mod->needload = false;

		// return the mod we got
		return mod;
	}

	// load the file
	if (!(buf = (unsigned *) COM_LoadFile (mod->name)))
	{
		if (crash)
			Host_Error ("Mod_LoadModel: %s not found", mod->name);

		return NULL;
	}

	// call the apropriate loader
	mod->needload = false;

	// set all header pointers initially NULL
	mod->aliashdr = NULL;
	mod->brushhdr = NULL;
	mod->spritehdr = NULL;

	char headerver[5];

	memcpy (headerver, (unsigned *) buf, 4);
	headerver[4] = 0;

	switch (((unsigned *) buf)[0])
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

	// eval the radius to get a bounding sphere
	mod->radius = RadiusFromBounds (mod->mins, mod->maxs);

	Zone_Free (buf);
	return mod;
}


/*
==================
Mod_ForName

Loads in a model for the given name
==================
*/
model_t *Mod_ForName (char *name, bool crash) {return Mod_LoadModel (Mod_FindName (name), crash);}


/*
===============================================================================

					BRUSHMODEL LOADING

===============================================================================
*/


// the BSP as it's stored on disk
// this struct is only valid while the current model is being loaded and is used for stuff that we only
// need a temporary pointer to rather than a permanent one (like vertexes, edges, surfedges, etc)
typedef struct dbspmodel_s
{
	dvertex_t *vertexes;
	int numvertexes;

	int *surfedges;
	int numsurfedges;

	dedge_t *edges;
	int numedges;
} dbspmodel_t;

dbspmodel_t d_bspmodel;



/*
=================
Mod_LoadTextures
=================
*/
miptex_t *W_LoadTextureFromHLWAD (char *wadname, char *texname, miptex_t **mipdata);

void Mod_LoadTextures (model_t *mod, byte *mod_base, lump_t *l, lump_t *e)
{
#define MAX_HL_WADS	64
	int		i, j, num, max, altmax;
	miptex_t	*mt;
	texture_t	*tx, *tx2;
	texture_t	*anims[10];
	texture_t	*altanims[10];
	dmiptexlump_t *m;
	char *hlWADs[MAX_HL_WADS] = {NULL};

	if (!l->filelen)
	{
		mod->brushhdr->textures = NULL;
		return;
	}

	// no wads yet
	for (i = 0; i < MAX_HL_WADS; i++) hlWADs[i] = NULL;

	// look for WADS in the entities lump
	if (mod->brushhdr->bspversion == HL_BSPVERSION && e->filelen)
	{
		// get a pointer to the start of the lump
		char *data = (char *) mod_base + e->fileofs;
		char key[40];

		// parse the opening brace
		data = COM_Parse (data);

		if (com_token[0] == '{')
		{
			while (1)
			{
				// parse the key
				data = COM_Parse (data);

				// there is no key (end of worldspawn)
				if (!data) break;
				if (com_token[0] == '}') break;

				// allow keys with a leading _
				if (com_token[0] == '_')
					Q_strncpy (key, &com_token[1], 39);
				else Q_strncpy (key, com_token, 39);

				// remove trailing spaces
				while (key[strlen (key) - 1] == ' ') key[strlen (key) - 1] = 0;

				// parse the value
				data = COM_Parse (data);

				// likewise should never happen (has already been successfully parsed server-side and any errors that
				// were going to happen would have happened then; we only check to guard against pointer badness)
				if (!data) break;

				// check the key for a wad
				if (!stricmp (key, "wad"))
				{
					// store out
					hlWADs[0] = com_token;

					// done
					break;
				}
			}
		}

		// did we get any wads?
		if (hlWADs[0])
		{
			for (i = 0, j = 1;; i++)
			{
				// end of the list
				if (!hlWADs[0][i]) break;

				// semi-colon delimited
				if (hlWADs[0][i] == ';')
				{
					hlWADs[j++] = &hlWADs[0][i + 1];
					hlWADs[0][i] = 0;
				}
			}
		}
	}

	m = (dmiptexlump_t *) (mod_base + l->fileofs);

	mod->brushhdr->numtextures = m->nummiptex;
	mod->brushhdr->textures = (texture_t **) MainHunk->Alloc (mod->brushhdr->numtextures * sizeof (texture_t *));
	tx = (texture_t *) MainHunk->Alloc (sizeof (texture_t) * mod->brushhdr->numtextures);

	for (i = 0; i < m->nummiptex; i++, tx++)
	{
		miptex_t *hlmip = NULL;

		if (m->dataofs[i] == -1)
		{
			// set correct notexture here
			mod->brushhdr->textures[i] = r_notexture_mip;
			continue;
		}

		mt = (miptex_t *) ((byte *) m + m->dataofs[i]);

		// store out
		mod->brushhdr->textures[i] = tx;

		// fix 16 char texture names
		tx->name[16] = 0;
		memcpy (tx->name, mt->name, sizeof (mt->name));

		tx->width = mt->width;
		tx->height = mt->height;

		if (mt->offsets[0] == 0 && mod->brushhdr->bspversion == HL_BSPVERSION)
		{
			// texture from a WAD
			for (j = 0; j < MAX_HL_WADS; j++)
			{
				if (!hlWADs[j]) continue;
				if (!hlWADs[j][0]) continue;

				// did we get it
				if (W_LoadTextureFromHLWAD (hlWADs[j], mt->name, &hlmip))
				{
					mt = hlmip;
					break;
				}
			}
		}

		// switch water indicator for halflife
		if (mod->brushhdr->bspversion == HL_BSPVERSION && mt->name[0] == '!')
		{
			mt->name[0] = '*';
			tx->name[0] = '*';
		}

		byte *texels = (byte *) (mt + 1);

		// check for water
		if (mt->name[0] == '*')
		{
			tx->contentscolor[0] = 0;
			tx->contentscolor[1] = 0;
			tx->contentscolor[2] = 0;

			for (j = 0; j < (tx->width * tx->height); j++)
			{
				PALETTEENTRY bgra = d3d_QuakePalette.standard[texels[j]];

				tx->contentscolor[0] += bgra.peRed;
				tx->contentscolor[1] += bgra.peGreen;
				tx->contentscolor[2] += bgra.peBlue;
			}

			// bring to approximate scale of the cshifts
			tx->contentscolor[0] /= ((tx->width * tx->height) / 3);
			tx->contentscolor[1] /= ((tx->width * tx->height) / 3);
			tx->contentscolor[2] /= ((tx->width * tx->height) / 3);
		}
		else
		{
			tx->contentscolor[0] = 255;
			tx->contentscolor[1] = 255;
			tx->contentscolor[2] = 255;
		}

		if (!strnicmp (mt->name, "sky", 3))
		{
			D3DSky_InitTextures (mt);

			// set correct dimensions for texcoord building
			tx->width = 64;
			tx->height = 64;
		}
		else
		{
			if (mod->brushhdr->bspversion == Q1_BSPVERSION || mod->brushhdr->bspversion == PR_BSPVERSION)
			{
				int texflags = IMAGE_MIPMAP | IMAGE_BSP;
				int lumaflags = IMAGE_MIPMAP | IMAGE_BSP | IMAGE_LUMA;

				// load the luma first so that we know if we have it
				if ((tx->lumaimage = D3D_LoadTexture (mt->name, mt->width, mt->height, texels, lumaflags)) != NULL)
					texflags |= IMAGE_NOCOMPRESS;

				tx->teximage = D3D_LoadTexture (mt->name, mt->width, mt->height, texels, texflags);
			}
			else
			{
				// no lumas in halflife
				tx->teximage = D3D_LoadTexture (mt->name, mt->width, mt->height, texels, IMAGE_MIPMAP | IMAGE_BSP | IMAGE_HALFLIFE);
				tx->lumaimage = NULL;
			}
		}

		if (hlmip) Zone_Free (hlmip);
	}

	// sequence the animations
	// this was evil - it crashed to the console if there was an error.  Now it just removes the animation and devprints a message
	for (i = 0; i < m->nummiptex; i++)
	{
		tx = mod->brushhdr->textures[i];

		if (!tx || tx->name[0] != '+') continue;
		if (tx->anim_next) continue;

		// find the number of frames in the animation
		memset (anims, 0, sizeof (anims));
		memset (altanims, 0, sizeof (altanims));

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
		else
		{
			Con_DPrintf ("Invalid animation name - ");
			goto bad_anim_cleanup;
		}

		for (j = i + 1; j < m->nummiptex; j++)
		{
			tx2 = mod->brushhdr->textures[j];

			if (!tx2 || tx2->name[0] != '+') continue;
			if (strcmp (tx2->name + 2, tx->name + 2)) continue;

			num = tx2->name[1];

			if (num >= 'a' && num <= 'z') num -= 'a' - 'A';

			if (num >= '0' && num <= '9')
			{
				num -= '0';
				anims[num] = tx2;

				if (num + 1 > max) max = num + 1;
			}
			else if (num >= 'A' && num <= 'J')
			{
				num = num - 'A';
				altanims[num] = tx2;

				if (num + 1 > altmax) altmax = num + 1;
			}
			else
			{
				Con_DPrintf ("Invalid animation name - ");
				goto bad_anim_cleanup;
			}
		}

#define	ANIM_CYCLE	2

		// link them all together
		for (j = 0; j < max; j++)
		{
			tx2 = anims[j];

			if (!tx2)
			{
				Con_DPrintf ("Missing frame %i - ", j);
				goto bad_anim_cleanup;
			}

			tx2->anim_total = max * ANIM_CYCLE;
			tx2->anim_min = j * ANIM_CYCLE;
			tx2->anim_max = (j + 1) * ANIM_CYCLE;
			tx2->anim_next = anims[(j + 1) % max];

			if (altmax) tx2->alternate_anims = altanims[0];
		}

		for (j = 0; j < altmax; j++)
		{
			tx2 = altanims[j];

			if (!tx2)
			{
				Con_DPrintf ("Missing frame %i - ", j);
				goto bad_anim_cleanup;
			}

			tx2->anim_total = altmax * ANIM_CYCLE;
			tx2->anim_min = j * ANIM_CYCLE;
			tx2->anim_max = (j + 1) * ANIM_CYCLE;
			tx2->anim_next = altanims[(j + 1) % altmax];

			if (max) tx2->alternate_anims = anims[0];
		}

		// if we got this far the animating texture is good
		continue;

bad_anim_cleanup:;
		// the animation is unclean so clean it
		// "...why don't we tell some leper jokes like we did when we were in boston?"
		Con_DPrintf ("Bad animating texture %s", tx->name);

		// switch to non-animating - remove all animation data
		tx->name[0] = '$';	// this just needs to replace the "+" or "-" on the name with something, anything
		tx->alternate_anims = NULL;
		tx->anim_max = 0;
		tx->anim_min = 0;
		tx->anim_next = NULL;
		tx->anim_total = 0;
	}
}


/*
=================
Mod_LoadLighting
=================
*/
extern cvar_t r_coloredlight;

bool Mod_LoadLITFile (model_t *mod, lump_t *l)
{
	if (!r_coloredlight.value) return false;
	if (mod->brushhdr->bspversion != Q1_BSPVERSION && mod->brushhdr->bspversion != PR_BSPVERSION) return false;

	HANDLE lithandle = INVALID_HANDLE_VALUE;
	char litname[128];

	// take a copy to work on
	Q_strncpy (litname, mod->name, 127);

	// fixme - we use this in a number of places so we should refactor it out
	for (int i = strlen (litname) - 1; i; i--)
	{
		if (litname[i] == '.')
		{
			strcpy (&litname[i + 1], "lit");
			break;
		}
	}

	// we can't use COM_LoadFile here as we need to retrieve the file length too....
	int filelen = COM_FOpenFile (litname, &lithandle);

	// didn't find one
	if (lithandle == INVALID_HANDLE_VALUE) return false;

	// validate the file length; a valid lit should have 3 x the light data size of the BSP.
	// OK, we can still clash, but with the limited format and differences between the two,
	// at least this is better than nothing.  some day someone's gonna invent one of these
	// formats that actually includes a FUCKING CHECKSUM but i suspect satan will be getting
	// his winter woolies out before that happens.  sheesh, people.
	if ((filelen - 8) != l->filelen * 3)
	{
		COM_FCloseFile (&lithandle);
		return false;
	}

	// read and validate the header
	int litheader[2];
	int rlen = COM_FReadFile (lithandle, litheader, sizeof (int) * 2);

	if (rlen != sizeof (int) * 2)
	{
		// something bad happened
		COM_FCloseFile (&lithandle);
		return false;
	}

	if (litheader[0] != 0x54494C51 || litheader[1] != 1)
	{
		// invalid format
		COM_FCloseFile (&lithandle);
		return false;
	}

	// read from the lit file
	rlen = COM_FReadFile (lithandle, mod->brushhdr->lightdata, l->filelen * 3);
	COM_FCloseFile (&lithandle);

	// return success or failure
	return (rlen == l->filelen * 3);
}


void Mod_LoadLighting (model_t *mod, byte *mod_base, lump_t *l)
{
	int i;
	byte *src;
	byte *dst;

	if (!l->filelen)
	{
		mod->brushhdr->lightdata = NULL;
		return;
	}

	if (mod->brushhdr->bspversion == HL_BSPVERSION)
	{
		// read direct with no LIT file or no expansion
		mod->brushhdr->lightdata = (byte *) MainHunk->Alloc (l->filelen, FALSE);
		memcpy (mod->brushhdr->lightdata, mod_base + l->fileofs, l->filelen);
		return;
	}

	// expand size to 3 component
	mod->brushhdr->lightdata = (byte *) MainHunk->Alloc (l->filelen * 3, FALSE);

	// check for a lit file
	if (Mod_LoadLITFile (mod, l)) return;

	// copy to end of the buffer
	memcpy (&mod->brushhdr->lightdata[l->filelen * 2], mod_base + l->fileofs, l->filelen);

	// set source and dest pointers
	src = &mod->brushhdr->lightdata[l->filelen * 2];
	dst = mod->brushhdr->lightdata;

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
Mod_LoadEntities
=================
*/
void Mod_LoadEntities (model_t *mod, byte *mod_base, lump_t *l)
{
	if (!l->filelen)
	{
		mod->brushhdr->entities = NULL;
		return;
	}

	// resolve missing NULL term in entities lump; VPA will automatically 0 the memory so no need to do so ourselves.
	mod->brushhdr->entities = (char *) MainHunk->Alloc (l->filelen + 1);
	memcpy (mod->brushhdr->entities, mod_base + l->fileofs, l->filelen);
}


/*
=================
Mod_LoadSubmodels
=================
*/
void Mod_LoadSubmodels (model_t *mod, byte *mod_base, lump_t *l)
{
	dmodel_t	*in;
	dmodel_t	*out;
	int			i, j, count;

	in = (dmodel_t *) (mod_base + l->fileofs);

	if (l->filelen % sizeof (*in))
		Host_Error ("MOD_LoadBmodel: LUMP_SUBMODELS funny lump size in %s", mod->name);

	count = l->filelen / sizeof (*in);
	out = (dmodel_t *) MainHunk->Alloc (count * sizeof (*out));

	mod->brushhdr->submodels = out;
	mod->brushhdr->numsubmodels = count;

	for (i = 0; i < count; i++, in++, out++)
	{
		for (j = 0; j < 3; j++)
		{
			// spread the mins / maxs by a pixel
			out->mins[j] = in->mins[j] - 1;
			out->maxs[j] = in->maxs[j] + 1;
			out->origin[j] = in->origin[j];
		}

		for (j = 0; j < MAX_MAP_HULLS; j++)
			out->headnode[j] = in->headnode[j];

		out->visleafs = in->visleafs;
		out->firstface = in->firstface;
		out->numfaces = in->numfaces;
	}
}


/*
=================
Mod_LoadTexinfo
=================
*/
void Mod_LoadTexinfo (model_t *mod, byte *mod_base, lump_t *l)
{
	texinfo_t *in;
	mtexinfo_t *out;
	int 	i, j, k, count;
	int		miptex;
	float	len1, len2;

	in = (texinfo_t *) (mod_base + l->fileofs);

	if (l->filelen % sizeof (texinfo_t))
		Host_Error ("MOD_LoadBmodel: LUMP_TEXINFO funny lump size in %s", mod->name);

	count = l->filelen / sizeof (texinfo_t);
	out = (mtexinfo_t *) MainHunk->Alloc (count * sizeof (mtexinfo_t));

	mod->brushhdr->texinfo = out;
	mod->brushhdr->numtexinfo = count;

	for (i = 0; i < count; i++, in++, out++)
	{
		for (k = 0; k < 2; k++)
			for (j = 0; j < 4; j++)
				out->vecs[k][j] = in->vecs[k][j];

		len1 = Length (out->vecs[0]);
		len2 = Length (out->vecs[1]);
		len1 = (len1 + len2) / 2;

		if (len1 < 0.32)
			out->mipadjust = 4;
		else if (len1 < 0.49)
			out->mipadjust = 3;
		else if (len1 < 0.99)
			out->mipadjust = 2;
		else out->mipadjust = 1;

		miptex = in->miptex;
		out->flags = in->flags;

		if (!mod->brushhdr->textures)
		{
			out->texture = r_notexture_mip;	// checkerboard texture
			out->flags = 0;
		}
		else if (miptex >= mod->brushhdr->numtextures || miptex < 0)
		{
			Con_DPrintf ("miptex >= mod->brushhdr->numtextures\n");
			out->texture = r_notexture_mip; // texture not found
			out->flags = 0;
		}
		else
		{
			out->texture = mod->brushhdr->textures[miptex];

			if (!out->texture)
			{
				Con_DPrintf ("!out->texture\n");
				out->texture = r_notexture_mip; // texture not found
				out->flags = 0;
			}
		}
	}
}


/*
=================
Mod_LoadSurfaceVertexes

=================
*/
int MaxExtents[2] = {0, 0};

void Mod_LoadSurfaceVertexes (model_t *mod, msurface_t *surf, brushpolyvert_t *verts, unsigned short *ndx)
{
	// just set these up for now, we generate them in the next step
	surf->verts = verts;
	surf->indexes = ndx;

	VectorClear (surf->midpoint);

	// let's calc bounds and extents here too...!
	float mins[2] = {99999999, 99999999};
	float maxs[2] = {-99999999, -99999999};

	surf->mins[0] = surf->mins[1] = surf->mins[2] = 99999999;
	surf->maxs[0] = surf->maxs[1] = surf->maxs[2] = -99999999;

	// removing colinear points does *nothing* for performance with this engine.  i know, i tested it.
	for (int i = 0, j = surf->numverts; i < surf->numverts; i++, j--)
	{
		int lindex = d_bspmodel.surfedges[surf->firstedge + i];
		int stripdst = i ? (i * 2 - 1) : 0;

		if (stripdst >= surf->numverts) stripdst = j * 2;

		verts = &surf->verts[stripdst];

		if (lindex > 0)
		{
			verts->xyz[0] = d_bspmodel.vertexes[d_bspmodel.edges[lindex].v[0]].point[0];
			verts->xyz[1] = d_bspmodel.vertexes[d_bspmodel.edges[lindex].v[0]].point[1];
			verts->xyz[2] = d_bspmodel.vertexes[d_bspmodel.edges[lindex].v[0]].point[2];
		}
		else
		{
			verts->xyz[0] = d_bspmodel.vertexes[d_bspmodel.edges[-lindex].v[1]].point[0];
			verts->xyz[1] = d_bspmodel.vertexes[d_bspmodel.edges[-lindex].v[1]].point[1];
			verts->xyz[2] = d_bspmodel.vertexes[d_bspmodel.edges[-lindex].v[1]].point[2];
		}

		if (surf->mins[0] > verts->xyz[0]) surf->mins[0] = verts->xyz[0];
		if (surf->mins[1] > verts->xyz[1]) surf->mins[1] = verts->xyz[1];
		if (surf->mins[2] > verts->xyz[2]) surf->mins[2] = verts->xyz[2];

		if (surf->maxs[0] < verts->xyz[0]) surf->maxs[0] = verts->xyz[0];
		if (surf->maxs[1] < verts->xyz[1]) surf->maxs[1] = verts->xyz[1];
		if (surf->maxs[2] < verts->xyz[2]) surf->maxs[2] = verts->xyz[2];

		// accumulate into midpoint
		VectorAdd (surf->midpoint, verts->xyz, surf->midpoint);

		// extents
		// (can we not just cache this and reuse it below???) (done)
		float st[2] =
		{
			(DotProduct (verts->xyz, surf->texinfo->vecs[0]) + surf->texinfo->vecs[0][3]),
			(DotProduct (verts->xyz, surf->texinfo->vecs[1]) + surf->texinfo->vecs[1][3])
		};

		if (st[0] > maxs[0]) maxs[0] = st[0]; if (st[0] < mins[0]) mins[0] = st[0];
		if (st[1] > maxs[1]) maxs[1] = st[1]; if (st[1] < mins[1]) mins[1] = st[1];

		// texcoords
		if (surf->flags & SURF_DRAWTURB)
		{
			// no division by texture size to keep the warp valid
			verts->st[0] = verts->st2[0] = st[0] - surf->texinfo->vecs[0][3];
			verts->st[1] = verts->st2[1] = st[1] - surf->texinfo->vecs[1][3];
		}
		else
		{
			verts->st[0] = st[0] / (float) surf->texinfo->texture->width;
			verts->st[1] = st[1] / (float) surf->texinfo->texture->height;
		}
	}

	// get final mindpoint
	VectorScale (surf->midpoint, 1.0f / (float) surf->numverts, surf->midpoint);

	// convert the polygon to a triangle strip layout which may be faster on some hardware
	for (int i = 2; i < surf->numverts; i++, ndx += 3)
	{
		ndx[0] = i - 2;
		ndx[1] = (i & 1) ? i : (i - 1);
		ndx[2] = (i & 1) ? (i - 1) : i;
	}

	// no extents
	if ((surf->flags & SURF_DRAWTURB) || (surf->flags & SURF_DRAWSKY)) return;

	// now do extents
	for (int i = 0; i < 2; i++)
	{
		int bmins = floor (mins[i] / 16);
		int bmaxs = ceil (maxs[i] / 16);

		surf->texturemins[i] = bmins * 16;
		surf->extents[i] = (bmaxs - bmins) * 16;

		// find max allowable extents (fixme - precalc and store in d3d_GlobalCaps)
		int maxextents = ((d3d_DeviceCaps.MaxTextureWidth < d3d_DeviceCaps.MaxTextureHeight ? d3d_DeviceCaps.MaxTextureWidth : d3d_DeviceCaps.MaxTextureHeight) << 4) - 16;

		// to all practical purposes this will never be hit; even on 3DFX it's 4080 which is well in excess of the
		// max allowed by stock ID Quake.  just clamping it may result in weird lightmaps in extreme maps, but in
		// practice it doesn't even happen for reasons previously outlined.
		if (surf->extents[i] > maxextents) surf->extents[i] = maxextents;
	}

	// check surface extents against max extents
	if (surf->extents[0] > MaxExtents[0]) MaxExtents[0] = surf->extents[0];
	if (surf->extents[1] > MaxExtents[1]) MaxExtents[1] = surf->extents[1];
}


/*
=================
Mod_LoadSurfaces
=================
*/
void Mod_LoadSurfaces (model_t *mod, byte *mod_base, lump_t *l)
{
	dface_t *face = (dface_t *) (mod_base + l->fileofs);

	if (l->filelen % sizeof (*face))
	{
		Host_Error ("Mod_LoadSurfaces: LUMP_FACES funny lump size in %s", mod->name);
		return;
	}

	int count = l->filelen / sizeof (*face);
	msurface_t *surf = (msurface_t *) MainHunk->Alloc (count * sizeof (msurface_t));

	mod->brushhdr->surfaces = surf;
	mod->brushhdr->numsurfaces = count;

	int totalverts = 0;
	int totalindexes = 0;

	for (int surfnum = 0; surfnum < count; surfnum++, face++, surf++)
	{
		// we need this for sorting
		surf->surfnum = surfnum;

		// verts/etc
		surf->firstedge = face->firstedge;
		surf->numverts = (unsigned short) face->numedges;
		surf->numindexes = (surf->numverts - 2) * 3;
		surf->flags = 0;

		if (face->side) surf->flags |= SURF_PLANEBACK;

		surf->plane = mod->brushhdr->planes + (unsigned short) face->planenum;
		surf->texinfo = mod->brushhdr->texinfo + (unsigned short) face->texinfo;

		// lighting info
		for (int i = 0; i < MAXLIGHTMAPS; i++)
			surf->styles[i] = face->styles[i];

		if (mod->brushhdr->bspversion == Q1_BSPVERSION || mod->brushhdr->bspversion == PR_BSPVERSION)
		{
			// expand offsets for pre-expanded light
			if (face->lightofs < 0)
				surf->samples = NULL;
			else surf->samples = mod->brushhdr->lightdata + (face->lightofs * 3);
		}
		else
		{
			// already rgb
			if (face->lightofs < 0)
				surf->samples = NULL;
			else surf->samples = mod->brushhdr->lightdata + face->lightofs;
		}

		// set the drawing flags flag
		if (!strncmp (surf->texinfo->texture->name, "sky", 3))
			surf->flags |= SURF_DRAWSKY;
		else if (surf->texinfo->texture->name[0] == '{')
			surf->flags |= SURF_DRAWFENCE;
		else if (surf->texinfo->texture->name[0] == '*')
		{
			// set contents flags
			if (!strncmp (surf->texinfo->texture->name, "*lava", 5))
				surf->flags |= SURF_DRAWLAVA;
			else if (!strncmp (surf->texinfo->texture->name, "*tele", 5))
				surf->flags |= SURF_DRAWTELE;
			else if (!strncmp (surf->texinfo->texture->name, "*slime", 6))
				surf->flags |= SURF_DRAWSLIME;
			else surf->flags |= SURF_DRAWWATER;

			// generic turb marker
			surf->flags |= SURF_DRAWTURB;
		}

		totalverts += surf->numverts;
		totalindexes += surf->numindexes;
	}

	// now we do another pass over the surfs to create their vertexes and indexes;
	// this is done as a second pass so that we can batch alloc the buffers for vertexes and indexes
	// which will load maps faster than if we do lots of small allocations
	// note that in DirectQ there are no longer any differences between surface types at this stage
	brushpolyvert_t *verts = (brushpolyvert_t *) MainHunk->Alloc (totalverts * sizeof (brushpolyvert_t), FALSE);
	unsigned short *indexes = (unsigned short *) MainHunk->Alloc (totalindexes * sizeof (unsigned short), FALSE);

	surf = mod->brushhdr->surfaces;

	for (int surfnum = 0; surfnum < count; surfnum++, surf++)
	{
		// load all of the vertexes and calc bounds and extents for frustum culling
		Mod_LoadSurfaceVertexes (mod, surf, verts, indexes);

		verts += surf->numverts;
		indexes += surf->numindexes;
	}
}


/*
=================
Mod_LoadVisLeafsNodes

handles visibility, leafs and nodes

note: this is not really necessary anymore as we have contiguous memory again
=================
*/
void Mod_LoadVisLeafsNodes (model_t *mod, byte *mod_base, lump_t *v, lump_t *l, lump_t *n)
{
	dleaf_t 	*lin;
	mleaf_t 	*lout;
	dnode_t		*nin;
	mnode_t		*nout;
	int			i, j, leafcount, p;
	int			nodecount;

	// initial in pointers for leafs and nodes
	lin = (dleaf_t *) (mod_base + l->fileofs);
	nin = (dnode_t *) (mod_base + n->fileofs);

	if (l->filelen % sizeof (dleaf_t)) Host_Error ("MOD_LoadBmodel: LUMP_LEAFS funny lump size in %s", mod->name);
	if (n->filelen % sizeof (dnode_t)) Host_Error ("MOD_LoadBmodel: LUMP_NODES funny lump size in %s", mod->name);

	leafcount = l->filelen / sizeof (dleaf_t);
	nodecount = n->filelen / sizeof (dnode_t);

	mod->brushhdr->numleafs = leafcount;
	mod->brushhdr->numnodes = nodecount;

	// this will crash in-game, so better to take down as gracefully as possible before that
	// note that this is a map format limitation owing to the use of signed shorts for leaf/node numbers
	// (but not necessarily; numleafs + numnodes must not exceed 65536 actually)
	if (mod->brushhdr->numleafs > 65536)
	{
		Host_Error ("Mod_LoadLeafs: mod->brushhdr->numleafs > 65536");
		return;
	}

	if (mod->brushhdr->numnodes > 65536)
	{
		Host_Error ("Mod_LoadNodes: mod->brushhdr->numleafs > 65536");
		return;
	}

	if (mod->brushhdr->numnodes + mod->brushhdr->numleafs > 65536)
	{
		Host_Error ("Mod_LoadNodes: mod->brushhdr->numnodes + mod->brushhdr->numleafs > 65536");
		return;
	}

	// set up vis data buffer and load it in
	byte *visdata = NULL;

	if (v->filelen)
	{
		visdata = (byte *) MainHunk->Alloc (v->filelen, FALSE);
		memcpy (visdata, mod_base + v->fileofs, v->filelen);
	}

	// nodes and leafs need to be in consecutive memory - see comment in R_LeafVisibility
	lout = (mleaf_t *) MainHunk->Alloc ((leafcount * sizeof (mleaf_t)) + (nodecount * sizeof (mnode_t)));
	mod->brushhdr->leafs = lout;

	for (i = 0; i < leafcount; i++, lin++, lout++)
	{
		lout->num = i;

		for (j = 0; j < 3; j++)
		{
			lout->mins[j] = lin->mins[j];
			lout->maxs[j] = lin->maxs[j];
		}

		p = lin->contents;
		lout->contents = p;

		// cast to unsigned short to conform to BSP file spec
		lout->firstmarksurface = mod->brushhdr->marksurfaces + (unsigned short) lin->firstmarksurface;
		lout->nummarksurfaces = (unsigned short) lin->nummarksurfaces;

		// no visibility yet
		lout->visframe = -1;

		p = lin->visofs;

		if (p == -1 || !v->filelen)
			lout->compressed_vis = NULL;
		else lout->compressed_vis = visdata + p;

		for (j = 0; j < 4; j++)
			lout->ambient_sound_level[j] = lin->ambient_level[j];

		// null the contents
		lout->contentscolor = NULL;

		// gl underwater warp (this is not really used any more but is retained for future use)
		if (lout->contents == CONTENTS_WATER || lout->contents == CONTENTS_LAVA || lout->contents == CONTENTS_SLIME)
		{
			for (j = 0; j < lout->nummarksurfaces; j++)
			{
				// set contents colour for this leaf
				if ((lout->firstmarksurface[j]->flags & SURF_DRAWWATER) && lout->contents == CONTENTS_WATER)
					lout->contentscolor = lout->firstmarksurface[j]->texinfo->texture->contentscolor;
				else if ((lout->firstmarksurface[j]->flags & SURF_DRAWLAVA) && lout->contents == CONTENTS_LAVA)
					lout->contentscolor = lout->firstmarksurface[j]->texinfo->texture->contentscolor;
				else if ((lout->firstmarksurface[j]->flags & SURF_DRAWSLIME) && lout->contents == CONTENTS_SLIME)
					lout->contentscolor = lout->firstmarksurface[j]->texinfo->texture->contentscolor;

				// set underwater flag
				lout->firstmarksurface[j]->flags |= SURF_UNDERWATER;

				// duplicate surf flags into the leaf
				lout->flags |= lout->firstmarksurface[j]->flags;
			}
		}
		else
		{
			for (j = 0; j < lout->nummarksurfaces; j++)
			{
				// duplicate surf flags into the leaf
				lout->flags |= lout->firstmarksurface[j]->flags;
			}
		}

		// static entities
		lout->efrags = NULL;

		// not seen yet
		lout->seen = false;
	}

	// set up nodes in contiguous memory - see comment in R_LeafVisibility
	mod->brushhdr->nodes = nout = (mnode_t *) lout;

	// load the nodes
	for (i = 0; i < nodecount; i++, nin++, nout++)
	{
		nout->num = i;

		for (j = 0; j < 3; j++)
		{
			nout->mins[j] = nin->mins[j];
			nout->maxs[j] = nin->maxs[j];
		}

		p = nin->planenum;
		nout->plane = mod->brushhdr->planes + p;

		nout->firstsurface = (unsigned short) nin->firstface;
		nout->numsurfaces = (unsigned short) nin->numfaces;

		nout->visframe = -1;
		nout->seen = false;

		// set children and parents here too
		// note - the memory has already been allocated and this field won't be overwritten during node loading
		// so even if a node hasn't yet been loaded it's safe to do this; leafs of course have all been loaded.
		for (j = 0; j < 2; j++)
		{
			p = nin->children[j];

			// hacky fix
			if (p < -mod->brushhdr->numleafs) p += 65536;

			if (p >= 0)
			{
				if (p >= mod->brushhdr->numnodes) p = mod->brushhdr->numnodes - 1;

				nout->children[j] = mod->brushhdr->nodes + p;
				(mod->brushhdr->nodes + p)->parent = nout;
			}
			else
			{
				if ((-1 - p) >= mod->brushhdr->numleafs) p = -mod->brushhdr->numleafs;

				nout->children[j] = (mnode_t *) (mod->brushhdr->leafs + (-1 - p));
				((mnode_t *) (mod->brushhdr->leafs + (-1 - p)))->parent = nout;
			}
		}
	}

	// first node has no parent
	mod->brushhdr->nodes->parent = NULL;
}


/*
=================
Mod_LoadClipnodes
=================
*/
void Mod_LoadClipnodes (model_t *mod, byte *mod_base, lump_t *l)
{
	dclipnode_t *in;
	mclipnode_t *out;
	int			i, count;
	hull_t		*hull;

	in = (dclipnode_t *) (mod_base + l->fileofs);

	if (l->filelen % sizeof (dclipnode_t))
		Host_Error ("MOD_LoadBmodel: LUMP_CLIPNODES funny lump size in %s", mod->name);

	count = l->filelen / sizeof (dclipnode_t);
	out = (mclipnode_t *) MainHunk->Alloc (count * sizeof (mclipnode_t));

	mod->brushhdr->clipnodes = out;
	mod->brushhdr->numclipnodes = count;

	hull = &mod->brushhdr->hulls[1];
	hull->clipnodes = out;
	hull->firstclipnode = 0;
	hull->lastclipnode = count - 1;
	hull->planes = mod->brushhdr->planes;
	hull->clip_mins[0] = -16;
	hull->clip_mins[1] = -16;
	hull->clip_mins[2] = -24;
	hull->clip_maxs[0] = 16;
	hull->clip_maxs[1] = 16;
	hull->clip_maxs[2] = 32;

	hull = &mod->brushhdr->hulls[2];
	hull->clipnodes = out;
	hull->firstclipnode = 0;
	hull->lastclipnode = count - 1;
	hull->planes = mod->brushhdr->planes;
	hull->clip_mins[0] = -32;
	hull->clip_mins[1] = -32;
	hull->clip_mins[2] = -24;
	hull->clip_maxs[0] = 32;
	hull->clip_maxs[1] = 32;
	hull->clip_maxs[2] = 64;

	for (i = 0; i < count; i++, out++, in++)
	{
		out->planenum = in->planenum;

		out->children[0] = (unsigned short) in->children[0];
		out->children[1] = (unsigned short) in->children[1];

		// support > 32k clipnodes
		if (out->children[0] >= count) out->children[0] -= 65536;
		if (out->children[1] >= count) out->children[1] -= 65536;
	}
}

/*
=================
Mod_MakeHull0

Duplicate the drawing hull structure as a clipping hull
=================
*/
void Mod_MakeHull0 (model_t *mod)
{
	mnode_t		*in, *child;
	mclipnode_t *out;
	int			i, j, count;
	hull_t		*hull;

	hull = &mod->brushhdr->hulls[0];

	in = mod->brushhdr->nodes;
	count = mod->brushhdr->numnodes;
	out = (mclipnode_t *) MainHunk->Alloc (count * sizeof (mclipnode_t));

	hull->clipnodes = out;
	hull->firstclipnode = 0;
	hull->lastclipnode = count - 1;
	hull->planes = mod->brushhdr->planes;

	for (i = 0; i < count; i++, out++, in++)
	{
		out->planenum = in->plane - mod->brushhdr->planes;

		for (j = 0; j < 2; j++)
		{
			child = in->children[j];

			if (child->contents < 0)
				out->children[j] = child->contents;
			else out->children[j] = child - mod->brushhdr->nodes;
		}
	}
}

/*
=================
Mod_LoadMarksurfaces
=================
*/
void Mod_LoadMarksurfaces (model_t *mod, byte *mod_base, lump_t *l)
{
	int		i, j, count;
	short		*in;
	msurface_t **out;

	in = (short *) (mod_base + l->fileofs);

	if (l->filelen % sizeof (*in))
		Host_Error ("MOD_LoadBmodel: LUMP_MARKSURFACES funny lump size in %s", mod->name);

	count = l->filelen / sizeof (*in);
	out = (msurface_t **) MainHunk->Alloc (count * sizeof (*out), FALSE);

	mod->brushhdr->marksurfaces = out;
	mod->brushhdr->nummarksurfaces = count;

	for (i = 0; i < count; i++)
	{
		j = (unsigned short) in[i];

		if (j >= mod->brushhdr->numsurfaces)
			Host_Error ("Mod_ParseMarksurfaces: bad surface number");

		out[i] = mod->brushhdr->surfaces + j;
	}
}


/*
=================
Mod_LoadPlanes
=================
*/
void Mod_LoadPlanes (model_t *mod, byte *mod_base, lump_t *l)
{
	int			i, j;
	mplane_t	*out;
	dplane_t 	*in;
	int			count;
	int			bits;

	in = (dplane_t *) (mod_base + l->fileofs);

	if (l->filelen % sizeof (*in))
		Host_Error ("MOD_LoadBmodel: LUMP_PLANES funny lump size in %s", mod->name);

	count = l->filelen / sizeof (*in);

	// why double the number of planes???
	out = (mplane_t *) MainHunk->Alloc (count * 2 * sizeof (mplane_t));

	mod->brushhdr->planes = out;
	mod->brushhdr->numplanes = count;

	for (i = 0; i < count; i++, in++, out++)
	{
		bits = 0;

		for (j = 0; j < 3; j++)
		{
			out->normal[j] = in->normal[j];

			if (out->normal[j] < 0)
				bits |= 1 << j;
		}

		out->dist = in->dist;
		out->type = in->type;
		out->signbits = bits;
	}
}


static void Mod_RecalcNodeBBox (mnode_t *node)
{
	if (!node->plane || node->contents < 0)
	{
		// node is a leaf
		mleaf_t *leaf = (mleaf_t *) node;

		// build a tight bbox around the leaf
		msurface_t **mark = leaf->firstmarksurface;
		int c = leaf->nummarksurfaces;

		if (c)
		{
			leaf->mins[0] = leaf->mins[1] = leaf->mins[2] = 99999999;
			leaf->maxs[0] = leaf->maxs[1] = leaf->maxs[2] = -99999999;

			do
			{
				if ((*mark)->mins[0] < leaf->mins[0]) leaf->mins[0] = (*mark)->mins[0];
				if ((*mark)->mins[1] < leaf->mins[1]) leaf->mins[1] = (*mark)->mins[1];
				if ((*mark)->mins[2] < leaf->mins[2]) leaf->mins[2] = (*mark)->mins[2];

				if ((*mark)->maxs[0] > leaf->maxs[0]) leaf->maxs[0] = (*mark)->maxs[0];
				if ((*mark)->maxs[1] > leaf->maxs[1]) leaf->maxs[1] = (*mark)->maxs[1];
				if ((*mark)->maxs[2] > leaf->maxs[2]) leaf->maxs[2] = (*mark)->maxs[2];

				mark++;
			} while (--c);

			// expand the bbox a little to ensure that it's a real box
			leaf->mins[0] -= BBEXPAND;
			leaf->mins[1] -= BBEXPAND;
			leaf->mins[2] -= BBEXPAND;

			leaf->maxs[0] += BBEXPAND;
			leaf->maxs[1] += BBEXPAND;
			leaf->maxs[2] += BBEXPAND;
		}

		return;
	}

	// calculate children first
	Mod_RecalcNodeBBox (node->children[0]);
	Mod_RecalcNodeBBox (node->children[1]);

	// make combined bounding box from children
	node->mins[0] = min (node->children[0]->mins[0], node->children[1]->mins[0]);
	node->mins[1] = min (node->children[0]->mins[1], node->children[1]->mins[1]);
	node->mins[2] = min (node->children[0]->mins[2], node->children[1]->mins[2]);

	node->maxs[0] = max (node->children[0]->maxs[0], node->children[1]->maxs[0]);
	node->maxs[1] = max (node->children[0]->maxs[1], node->children[1]->maxs[1]);
	node->maxs[2] = max (node->children[0]->maxs[2], node->children[1]->maxs[2]);
}


void Mod_CorrectBModelBBox (model_t *mod)
{
	// build a tight bbox around the mod
	mod->mins[0] = mod->mins[1] = mod->mins[2] = 99999999;
	mod->maxs[0] = mod->maxs[1] = mod->maxs[2] = -99999999;

	msurface_t *surf = mod->brushhdr->surfaces + mod->brushhdr->firstmodelsurface;

	for (int c = 0; c < mod->brushhdr->nummodelsurfaces; c++, surf++)
	{
		if (surf->mins[0] < mod->mins[0]) mod->mins[0] = surf->mins[0];
		if (surf->mins[1] < mod->mins[1]) mod->mins[1] = surf->mins[1];
		if (surf->mins[2] < mod->mins[2]) mod->mins[2] = surf->mins[2];

		if (surf->maxs[0] > mod->maxs[0]) mod->maxs[0] = surf->maxs[0];
		if (surf->maxs[1] > mod->maxs[1]) mod->maxs[1] = surf->maxs[1];
		if (surf->maxs[2] > mod->maxs[2]) mod->maxs[2] = surf->maxs[2];
	}

	// expand the bbox a little to ensure that it's a real box
	mod->mins[0] -= BBEXPAND;
	mod->mins[1] -= BBEXPAND;
	mod->mins[2] -= BBEXPAND;

	mod->maxs[0] += BBEXPAND;
	mod->maxs[1] += BBEXPAND;
	mod->maxs[2] += BBEXPAND;
}


void Mod_SetupBModelFromDisk (dheader_t *header)
{
	// set up pointers for anything that can just come directly from the BSP file itself but
	// doesn't need to be kept around after loading.
	memset (&d_bspmodel, 0, sizeof (dbspmodel_t));

	byte *mod_base = (byte *) header;

	d_bspmodel.numvertexes = header->lumps[LUMP_VERTEXES].filelen / sizeof (dvertex_t);
	d_bspmodel.vertexes = (dvertex_t *) (mod_base + header->lumps[LUMP_VERTEXES].fileofs);

	d_bspmodel.numsurfedges = header->lumps[LUMP_SURFEDGES].filelen / sizeof (int);
	d_bspmodel.surfedges = (int *) (mod_base + header->lumps[LUMP_SURFEDGES].fileofs);

	d_bspmodel.numedges = header->lumps[LUMP_EDGES].filelen / sizeof (dedge_t);
	d_bspmodel.edges = (dedge_t *) (mod_base + header->lumps[LUMP_EDGES].fileofs);
}


/*
=================
Mod_LoadBrushModel
=================
*/
void Mod_LoadBrushModel (model_t *mod, void *buffer)
{
	mod->type = mod_brush;

	dheader_t *header = (dheader_t *) buffer;

	int i = header->version;

	// drop to console
	if (i != PR_BSPVERSION && i != Q1_BSPVERSION && i != HL_BSPVERSION)
	{
		Host_Error ("Mod_LoadBrushModel: %s has wrong version number\n(%i should be %i, %i or %i)", mod->name, i, PR_BSPVERSION, Q1_BSPVERSION, HL_BSPVERSION);
		return;
	}

	byte *mod_base = (byte *) header;

	// load the model from disk (this just sets up pointers)
	Mod_SetupBModelFromDisk (header);

	// alloc space for a brush header
	mod->brushhdr = (brushhdr_t *) MainHunk->Alloc (sizeof (brushhdr_t));

	// store the version for correct hull checking
	mod->brushhdr->bspversion = header->version;

	// load into heap (these are the only lumps we need to leave hanging around)
	Mod_LoadTextures (mod, mod_base, &header->lumps[LUMP_TEXTURES], &header->lumps[LUMP_ENTITIES]);
	Mod_LoadLighting (mod, mod_base, &header->lumps[LUMP_LIGHTING]);
	Mod_LoadPlanes (mod, mod_base, &header->lumps[LUMP_PLANES]);
	Mod_LoadTexinfo (mod, mod_base, &header->lumps[LUMP_TEXINFO]);
	Mod_LoadSurfaces (mod, mod_base, &header->lumps[LUMP_FACES]);
	Mod_LoadMarksurfaces (mod, mod_base, &header->lumps[LUMP_MARKSURFACES]);
	Mod_LoadVisLeafsNodes (mod, mod_base, &header->lumps[LUMP_VISIBILITY], &header->lumps[LUMP_LEAFS], &header->lumps[LUMP_NODES]);
	Mod_LoadClipnodes (mod, mod_base, &header->lumps[LUMP_CLIPNODES]);
	Mod_LoadEntities (mod, mod_base, &header->lumps[LUMP_ENTITIES]);
	Mod_LoadSubmodels (mod, mod_base, &header->lumps[LUMP_MODELS]);

	Mod_RecalcNodeBBox (mod->brushhdr->nodes);

	// set up the model for the same usage as submodels and correct it's bbox
	mod->brushhdr->firstmodelsurface = 0;
	mod->brushhdr->nummodelsurfaces = mod->brushhdr->numsurfaces;
	Mod_CorrectBModelBBox (mod);

	Mod_MakeHull0 (mod);

	// regular and alternate animation
	mod->numframes = 2;

	// set up the submodels (FIXME: this is confusing)
	// (this should never happen as each model will be it's own first submodel)
	if (!mod->brushhdr->numsubmodels) return;

	// first pass fills in for the world (which is it's own first submodel), then grabs a submodel slot off the list.
	// subsequent passes fill in the submodel slot grabbed at the end of the previous pass.
	// the last pass doesn't need to grab a submodel slot as everything is already filled in
	// fucking hell, he wasn't joking!
	brushhdr_t *smheaders = (brushhdr_t *) MainHunk->Alloc (sizeof (brushhdr_t) * mod->brushhdr->numsubmodels);

	for (i = 0; i < mod->brushhdr->numsubmodels; i++)
	{
		// retrieve the submodel (submodel 0 will be the world)
		dmodel_t *bm = &mod->brushhdr->submodels[i];

		// fill in submodel specific stuff
		mod->brushhdr->hulls[0].firstclipnode = bm->headnode[0];

		for (int j = 1; j < MAX_MAP_HULLS; j++)
		{
			// clipnodes
			mod->brushhdr->hulls[j].firstclipnode = bm->headnode[j];
			mod->brushhdr->hulls[j].lastclipnode = mod->brushhdr->numclipnodes - 1;
		}

		// first surf in the inline model and number of surfs in it
		mod->brushhdr->firstmodelsurface = bm->firstface;
		mod->brushhdr->nummodelsurfaces = bm->numfaces;

		// leafs
		mod->brushhdr->numleafs = bm->visleafs;

		// bounding box
		VectorCopy2 (mod->maxs, bm->maxs);
		VectorCopy2 (mod->mins, bm->mins);

		Mod_CorrectBModelBBox (mod);

		// radius
		mod->radius = RadiusFromBounds (mod->mins, mod->maxs);

		// grab a submodel slot for the next pass through the loop
		if (i < mod->brushhdr->numsubmodels - 1)
		{
			// duplicate the basic information
			char name[10];

			// build the name
			_snprintf (name, 10, "*%i", i + 1);

			// get a slot from the models allocation
			model_t *inlinemod = Mod_FindName (name);

			// duplicate the data
			memcpy (inlinemod, mod, sizeof (model_t));

			// allocate a new header for the model
			inlinemod->brushhdr = smheaders;
			smheaders++;

			// copy the header data from the original model
			memcpy (inlinemod->brushhdr, mod->brushhdr, sizeof (brushhdr_t));

			// write in the name
			Q_strncpy (inlinemod->name, name, 63);

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


// FIXME - store per frame rather than for the entire model???
float aliasbboxmins[3], aliasbboxmaxs[3];
extern float r_avertexnormals[162][3];

void Mod_LoadFrameVerts (aliashdr_t *pheader, trivertx_t *verts)
{
	drawvertx_t *vertexes = (drawvertx_t *) MainCache->Alloc (pheader->vertsperframe * sizeof (drawvertx_t));
	pheader->vertexes[pheader->nummeshframes] = vertexes;

	for (int i = 0; i < pheader->vertsperframe; i++, vertexes++, verts++)
	{
		for (int n = 0; n < 3; n++)
		{
			vertexes->v[n] = verts->v[n];

			// compress normals so that we can assign them directly and save vertex submission overhead
			// 200 is used here so that we can get full -1..1 range
			float f = ((r_avertexnormals[verts->lightnormalindex][n] + 1.0f) * 200.0f);
			vertexes->normal4ub[n] = BYTE_CLAMP (f);
		}

		vertexes->v[3] = 1;	// provide a default w coord for vertex buffer input
		vertexes->normal4ub[3] = 1;
		vertexes->lerpvert = true;
	}

	pheader->nummeshframes++;
}


/*
=================
Mod_LoadAliasFrame
=================
*/
void *Mod_LoadAliasFrame (aliashdr_t *pheader, void *pin, maliasframedesc_t *frame)
{
	trivertx_t		*verts;
	daliasframe_t	*pdaliasframe;

	pdaliasframe = (daliasframe_t *) pin;

	Q_strncpy (frame->name, pdaliasframe->name, 16);
	frame->firstpose = pheader->nummeshframes;
	frame->numposes = 1;

	for (int i = 0; i < 3; i++)
	{
		frame->bboxmin.v[i] = pdaliasframe->bboxmin.v[i];
		frame->bboxmax.v[i] = pdaliasframe->bboxmax.v[i];

		if (frame->bboxmin.v[i] < aliasbboxmins[i]) aliasbboxmins[i] = frame->bboxmin.v[i];
		if (frame->bboxmax.v[i] > aliasbboxmaxs[i]) aliasbboxmaxs[i] = frame->bboxmax.v[i];
	}

	verts = (trivertx_t *) (pdaliasframe + 1);

	// load the frame vertexes
	Mod_LoadFrameVerts (pheader, verts);
	verts += pheader->vertsperframe;

	return (void *) verts;
}


/*
=================
Mod_LoadAliasGroup
=================
*/
void *Mod_LoadAliasGroup (aliashdr_t *pheader, void *pin, maliasframedesc_t *frame)
{
	daliasgroup_t		*pingroup;
	int					i, numframes;
	daliasinterval_t	*pin_intervals;
	void				*ptemp;

	pingroup = (daliasgroup_t *) pin;
	numframes = pingroup->numframes;
	frame->firstpose = pheader->nummeshframes;
	frame->numposes = numframes;

	for (i = 0; i < 3; i++)
	{
		frame->bboxmin.v[i] = pingroup->bboxmin.v[i];
		frame->bboxmax.v[i] = pingroup->bboxmax.v[i];

		if (frame->bboxmin.v[i] < aliasbboxmins[i]) aliasbboxmins[i] = frame->bboxmin.v[i];
		if (frame->bboxmax.v[i] > aliasbboxmaxs[i]) aliasbboxmaxs[i] = frame->bboxmax.v[i];
	}

	pin_intervals = (daliasinterval_t *) (pingroup + 1);
	frame->interval = pin_intervals->interval;
	pin_intervals += numframes;
	ptemp = (void *) pin_intervals;

	for (i = 0; i < numframes; i++)
	{
		Mod_LoadFrameVerts (pheader, (trivertx_t *) ((daliasframe_t *) ptemp + 1));
		ptemp = (trivertx_t *) ((daliasframe_t *) ptemp + 1) + pheader->vertsperframe;
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


// must be a power of 2
#define FLOODFILL_FIFO_SIZE 0x1000
#define FLOODFILL_FIFO_MASK (FLOODFILL_FIFO_SIZE - 1)

#define FLOODFILL_STEP(off, dx, dy) \
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
	byte				fillcolor = skin[0]; // assume this is the pixel to fill
	floodfill_t			fifo[FLOODFILL_FIFO_SIZE];
	int					inpt = 0, outpt = 0;
	int					filledcolor = -1;
	int					i;

	// this will always be true
	// opaque black doesn't exist in the gamma scaled palette (this is also true of GLQuake)
	// so just use the darkest colour in the palette instead
	if (filledcolor == -1) filledcolor = d3d_QuakePalette.darkindex;

	// can't fill to filled color or to transparent color (used as visited marker)
	if ((fillcolor == filledcolor) || (fillcolor == 255))
	{
		// hmm - this happens more often than one would like...
		// Con_DPrintf ("not filling skin from %d to %d\n", fillcolor, filledcolor);
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

		if (x > 0)				FLOODFILL_STEP (-1, -1, 0);
		if (x < skinwidth - 1)	FLOODFILL_STEP (1, 1, 0);
		if (y > 0)				FLOODFILL_STEP (-skinwidth, 0, -1);
		if (y < skinheight - 1)	FLOODFILL_STEP (skinwidth, 0, 1);

		skin[x + skinwidth *y] = fdc;
	}
}

/*
===============
Mod_LoadAllSkins
===============
*/
void *Mod_LoadAllSkins (model_t *mod, aliashdr_t *pheader, daliasskintype_t *pskintype)
{
	int		i, j, k;
	char	name[32];
	int		s;
	byte	*skin;
	daliasskingroup_t		*pinskingroup;
	int		groupskins;
	daliasskininterval_t	*pinskinintervals;

	skin = (byte *) (pskintype + 1);

	if (pheader->numskins < 1) Host_Error ("Mod_LoadAliasModel: Invalid # of skins: %d\n", pheader->numskins);

	pheader->skins = (aliasskin_t *) MainCache->Alloc (pheader->numskins * sizeof (aliasskin_t));
	s = pheader->skinwidth * pheader->skinheight;

	image_s *tex, *luma;
	byte *texels;

	// don't remove the extension here as Q1 has s_light.mdl and s_light.spr, so we need to differentiate them
	// dropped skin padding because there are too many special cases
	for (i = 0; i < pheader->numskins; i++)
	{
		// no saved texels
		pheader->skins[i].texels = NULL;

		if (pskintype->type == ALIAS_SKIN_SINGLE)
		{
			// fixme - is this even needed any more (due to padding?)
			Mod_FloodFillSkin (skin, pheader->skinwidth, pheader->skinheight);

			// save 8 bit texels for the player model to remap
			// progs/player.mdl is already hardcoded in sv_main so it's ok here too
			if (!stricmp (mod->name, "progs/player.mdl"))
			{
				pheader->skins[i].texels = (byte *) MainCache->Alloc (s);
				memcpy (pheader->skins[i].texels, (byte *) (pskintype + 1), s);
				mod->flags |= EF_PLAYER;
			}

			_snprintf (name, 32, "%s_%i", mod->name, i);

			texels = (byte *) (pskintype + 1);

			int texflags = IMAGE_MIPMAP | IMAGE_ALIAS;
			int lumaflags = IMAGE_MIPMAP | IMAGE_ALIAS | IMAGE_LUMA;

			if ((luma = D3D_LoadTexture (name, pheader->skinwidth, pheader->skinheight, texels, lumaflags)) != NULL)
				texflags |= IMAGE_NOCOMPRESS;

			tex = D3D_LoadTexture (name, pheader->skinwidth, pheader->skinheight, texels, texflags);

			// load fullbright
			pheader->skins[i].lumaimage[0] = pheader->skins[i].lumaimage[1] = pheader->skins[i].lumaimage[2] = pheader->skins[i].lumaimage[3] = luma;
			pheader->skins[i].teximage[0] = pheader->skins[i].teximage[1] = pheader->skins[i].teximage[2] = pheader->skins[i].teximage[3] = tex;

			pskintype = (daliasskintype_t *) ((byte *) (pskintype + 1) + s);
		}
		else
		{
			// animating skin group.  yuck.
			pskintype++;
			pinskingroup = (daliasskingroup_t *) pskintype;
			groupskins = pinskingroup->numskins;
			pinskinintervals = (daliasskininterval_t *) (pinskingroup + 1);

			pskintype = (daliasskintype_t *) (pinskinintervals + groupskins);

			for (j = 0; j < groupskins; j++)
			{
				Mod_FloodFillSkin (skin, pheader->skinwidth, pheader->skinheight);

				if (j == 0 && !stricmp (mod->name, "progs/player.mdl"))
				{
					// saving this out doesn't really work as each skin in the group will
					// overwrite the last.
					pheader->skins[i].texels = (byte *) MainCache->Alloc (s);
					memcpy (pheader->skins[i].texels, (byte *) (pskintype), s);
					mod->flags |= EF_PLAYER;
				}

				_snprintf (name, 32, "%s_%i_%i", mod->name, i, j);

				texels = (byte *) (pskintype);

				int texflags = IMAGE_MIPMAP | IMAGE_ALIAS;
				int lumaflags = IMAGE_MIPMAP | IMAGE_ALIAS | IMAGE_LUMA;

				if ((luma = D3D_LoadTexture (name, pheader->skinwidth, pheader->skinheight, texels, lumaflags)) != NULL)
					texflags |= IMAGE_NOCOMPRESS;

				tex = D3D_LoadTexture (name, pheader->skinwidth, pheader->skinheight, texels, texflags);

				// this tries to catch models with > 4 group skins
				pheader->skins[i].teximage[j & 3] = tex;
				pheader->skins[i].lumaimage[j & 3] = luma;
				pskintype = (daliasskintype_t *) ((byte *) (pskintype) + s);
			}

			k = j;

			// fill in any skins that weren't loaded
			for (; j < 4; j++)
			{
				pheader->skins[i].teximage[j & 3] = pheader->skins[i].teximage[j - k];
				pheader->skins[i].lumaimage[j & 3] = pheader->skins[i].lumaimage[j - k];
			}
		}
	}

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
	// take a checksum of the model for .ms3 checking
	byte modelhash[16];
	COM_HashData (modelhash, buffer, com_filesize);

	mdl_t *pinmodel = (mdl_t *) buffer;

	if (pinmodel->version != ALIAS_VERSION)
		Host_Error ("%s has wrong version number (%i should be %i)", mod->name, pinmodel->version, ALIAS_VERSION);

	// alloc the header in the cache
	aliashdr_t *pheader = (aliashdr_t *) MainCache->Alloc (sizeof (aliashdr_t));

	mod->flags = pinmodel->flags;
	mod->type = mod_alias;

	// even if we alloced from the cache we still fill it in
	// endian-adjust and copy the data, starting with the alias model header
	pheader->boundingradius = pinmodel->boundingradius;
	pheader->numskins = pinmodel->numskins;
	pheader->skinwidth = pinmodel->skinwidth;
	pheader->skinheight = pinmodel->skinheight;
	pheader->vertsperframe = pinmodel->numverts;
	pheader->numtris = pinmodel->numtris;
	pheader->numframes = pinmodel->numframes;

	// validate the setup
	// Sys_Error seems a little harsh here...
	if (pheader->numframes < 1) Host_Error ("Mod_LoadAliasModel: Model %s has invalid # of frames: %d\n", mod->name, pheader->numframes);
	if (pheader->numtris <= 0) Host_Error ("model %s has no triangles", mod->name);
	if (pheader->vertsperframe <= 0) Host_Error ("model %s has no vertices", mod->name);

	pheader->size = pinmodel->size * ALIAS_BASE_SIZE_RATIO;
	mod->synctype = (synctype_t) pinmodel->synctype;
	mod->numframes = pheader->numframes;

	for (int i = 0; i < 3; i++)
	{
		pheader->scale[i] = pinmodel->scale[i];
		pheader->scale_origin[i] = pinmodel->scale_origin[i];
	}

	if (!stricmp (mod->name, "progs/eyes.mdl"))
	{
		// double size of eyes
		for (int i = 0; i < 3; i++)
			pheader->scale[i] *= 2.0f;

		pheader->scale_origin[2] -= (22 + 8);
	}

	// load the skins
	daliasskintype_t *pskintype = (daliasskintype_t *) &pinmodel[1];
	pskintype = (daliasskintype_t *) Mod_LoadAllSkins (mod, pheader, pskintype);

	// load base s and t vertices
	stvert_t *pinstverts = (stvert_t *) pskintype;

	// load triangle lists
	dtriangle_t *pintriangles = (dtriangle_t *) &pinstverts[pheader->vertsperframe];

	// load the frames
	daliasframetype_t *pframetype = (daliasframetype_t *) &pintriangles[pheader->numtris];
	pheader->frames = (maliasframedesc_t *) MainCache->Alloc (pheader->numframes * sizeof (maliasframedesc_t));

	// initial bbox
	aliasbboxmins[0] = aliasbboxmins[1] = aliasbboxmins[2] = 9999999;
	aliasbboxmaxs[0] = aliasbboxmaxs[1] = aliasbboxmaxs[2] = -9999999;
	pheader->nummeshframes = 0;

	// because we don't know how many frames we need in advance we take a copy to the scratch buffer initially
	// the size of the scratch buffer is compatible with the max number of frames allowed by protocol 666
	pheader->vertexes = (drawvertx_t **) scratchbuf;

	for (int i = 0; i < pheader->numframes; i++)
	{
		aliasframetype_t frametype = (aliasframetype_t) pframetype->type;

		if (frametype == ALIAS_SINGLE)
			pframetype = (daliasframetype_t *) Mod_LoadAliasFrame (pheader, pframetype + 1, &pheader->frames[i]);
		else pframetype = (daliasframetype_t *) Mod_LoadAliasGroup (pheader, pframetype + 1, &pheader->frames[i]);
	}

	// copy framepointers from the scratch buffer to the final cached copy
	pheader->vertexes = (drawvertx_t **) MainCache->Alloc (pheader->vertexes, pheader->nummeshframes * sizeof (drawvertx_t *));

	// set correct bbox
	for (int i = 0; i < 3; i++)
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
	D3DAlias_MakeAliasMesh (mod->name, modelhash, pheader, pinstverts, pintriangles);

	// set the final header
	mod->aliashdr = pheader;

	// copy it out to the cache
	MainCache->Alloc (mod->name, mod, sizeof (model_t));
}


//=============================================================================

/*
=================
Mod_LoadSpriteFrame
=================
*/
void *Mod_LoadSpriteFrame (model_t *mod, msprite_t *thespr, void *pin, mspriteframe_t **ppframe, int framenum)
{
	dspriteframe_t		*pinframe;
	mspriteframe_t		*pspriteframe;
	int					width, height, size, origin[2];
	char				name[64];

	pinframe = (dspriteframe_t *) pin;

	width = pinframe->width;
	height = pinframe->height;

	size = width * height * (thespr->version == SPR32_VERSION ? 4 : 1);

	pspriteframe = (mspriteframe_t *) MainCache->Alloc (sizeof (mspriteframe_t));

	memset (pspriteframe, 0, sizeof (mspriteframe_t));

	*ppframe = pspriteframe;

	pspriteframe->width = width;
	pspriteframe->height = height;
	origin[0] = pinframe->origin[0];
	origin[1] = pinframe->origin[1];

	pspriteframe->up = origin[1];
	pspriteframe->down = origin[1] - height;
	pspriteframe->left = origin[0];
	pspriteframe->right = width + origin[0];

	_snprintf (name, 64, "%s_%i", mod->name, framenum);

	if (thespr->version == SPR32_VERSION)
	{
		// swap to bgra
		byte *data = (byte *) (pinframe + 1);

		for (int i = 0; i < width * height; i++, data += 4)
		{
			byte tmp = data[0];
			data[0] = data[2];
			data[2] = tmp;
		}
	}

	pspriteframe->texture = D3D_LoadTexture
							(
								name,
								width,
								height,
								(byte *) (pinframe + 1),
								IMAGE_MIPMAP | IMAGE_ALPHA | (thespr->version == SPR32_VERSION ? (IMAGE_SPRITE | IMAGE_32BIT) : IMAGE_SPRITE)
							);

	pspriteframe->s = 1;
	pspriteframe->t = 1;

	return (void *) ((byte *) pinframe + sizeof (dspriteframe_t) + size);
}


/*
=================
Mod_LoadSpriteGroup
=================
*/
void *Mod_LoadSpriteGroup (model_t *mod, msprite_t *thespr, void *pin, mspriteframe_t **ppframe, int framenum)
{
	dspritegroup_t		*pingroup;
	mspritegroup_t		*pspritegroup;
	int					i, numframes;
	dspriteinterval_t	*pin_intervals;
	float				*poutintervals;
	void				*ptemp;

	pingroup = (dspritegroup_t *) pin;
	numframes = pingroup->numframes;

	pspritegroup = (mspritegroup_t *) MainCache->Alloc (sizeof (mspritegroup_t) +
				   (numframes - 1) * sizeof (pspritegroup->frames[0]));

	pspritegroup->numframes = numframes;
	*ppframe = (mspriteframe_t *) pspritegroup;
	pin_intervals = (dspriteinterval_t *) (pingroup + 1);
	poutintervals = (float *) MainCache->Alloc (numframes * sizeof (float));
	pspritegroup->intervals = poutintervals;

	for (i = 0; i < numframes; i++)
	{
		*poutintervals = pin_intervals->interval;

		if (*poutintervals <= 0.0)
			Host_Error ("Mod_LoadSpriteGroup: interval<=0");

		poutintervals++;
		pin_intervals++;
	}

	ptemp = (void *) pin_intervals;

	for (i = 0; i < numframes; i++)
		ptemp = Mod_LoadSpriteFrame (mod, thespr, ptemp, &pspritegroup->frames[i], framenum * 100 + i);

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

	version = pin->version;

	if (version != SPRITE_VERSION && version != SPR32_VERSION)
		Host_Error ("%s has wrong version number (%i should be %i or %i)", mod->name, version, SPRITE_VERSION, SPR32_VERSION);

	numframes = pin->numframes;

	size = sizeof (msprite_t) +	(numframes - 1) * sizeof (psprite->frames);

	psprite = (msprite_t *) MainCache->Alloc (size);

	mod->spritehdr = psprite;

	psprite->type = pin->type;
	psprite->version = version;
	psprite->maxwidth = pin->width;
	psprite->maxheight = pin->height;
	psprite->beamlength = pin->beamlength;
	mod->synctype = (synctype_t) pin->synctype;
	psprite->numframes = numframes;

	mod->mins[0] = mod->mins[1] = -psprite->maxwidth / 2;
	mod->maxs[0] = mod->maxs[1] = psprite->maxwidth / 2;
	mod->mins[2] = -psprite->maxheight / 2;
	mod->maxs[2] = psprite->maxheight / 2;

	// load the frames
	if (numframes < 1)
		Host_Error ("Mod_LoadSpriteModel: Invalid # of frames: %d\n", numframes);

	mod->numframes = numframes;

	pframetype = (dspriteframetype_t *) (pin + 1);

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

		frametype = (spriteframetype_t) pframetype->type;
		psprite->frames[i].type = frametype;

		if (frametype == SPR_SINGLE)
			pframetype = (dspriteframetype_t *) Mod_LoadSpriteFrame (mod, psprite, pframetype + 1, &psprite->frames[i].frameptr, i);
		else pframetype = (dspriteframetype_t *) Mod_LoadSpriteGroup (mod, psprite, pframetype + 1, &psprite->frames[i].frameptr, i);
	}

	// restore the extension so that model reuse works
	strcat (mod->name, ".spr");

	mod->type = mod_sprite;

	// copy it out to the cache
	MainCache->Alloc (mod->name, mod, sizeof (model_t));
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

		Con_Printf ("%8p : %s\n", mod_known[i]->aliashdr, mod_known[i]->name);
	}
}


