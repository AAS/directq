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
#include "iqm.h"

void Mod_ClearBoundingBox (float *mins, float *maxs)
{
	mins[0] = mins[1] = mins[2] = 999999999;
	maxs[0] = maxs[1] = maxs[2] = -999999999;
}


void Mod_AccumulateBox (float *bbmins, float *bbmaxs, float *point)
{
	for (int i = 0; i < 3; i++)
	{
		if (point[i] < bbmins[i]) bbmins[i] = point[i];
		if (point[i] > bbmaxs[i]) bbmaxs[i] = point[i];
	}
}


void Mod_AccumulateBox (float *bbmins, float *bbmaxs, float *pmins, float *pmaxs)
{
	for (int i = 0; i < 3; i++)
	{
		if (pmins[i] < bbmins[i]) bbmins[i] = pmins[i];
		if (pmaxs[i] > bbmaxs[i]) bbmaxs[i] = pmaxs[i];
	}
}


void D3DSky_InitTextures (miptex_t *mt, char **paths);
void D3DAlias_MakeAliasMesh (char *name, aliashdr_t *hdr, stvert_t *stverts, dtriangle_t *triangles);

void Mod_LoadIQMModel (model_t *mod, void *buffer, char *path);
bool Mod_FindIQMModel (model_t *mod);
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

model_t	**mod_known = NULL;
int		mod_numknown = 0;


/*
=================
RadiusFromBounds
=================
*/
float RadiusFromBounds (vec3_t mins, vec3_t maxs)
{
	vec3_t extent;
	float radius = 0;

	extent[0] = fabs (maxs[0] - mins[0]) * 0.5f;
	extent[1] = fabs (maxs[1] - mins[1]) * 0.5f;
	extent[2] = fabs (maxs[2] - mins[2]) * 0.5f;

	if (extent[0] > radius) radius = extent[0];
	if (extent[1] > radius) radius = extent[1];
	if (extent[2] > radius) radius = extent[2];

	return radius;
}


void Mod_SphereFromBounds (float *mins, float *maxs, float *sphere)
{
	D3DXVECTOR3 points[2];
	D3DXVECTOR3 center;
	FLOAT radius;

	for (int i = 0; i < 3; i++)
	{
		points[0][i] = mins[i];
		points[1][i] = maxs[i];
	}

	D3DXComputeBoundingSphere (points, 2, sizeof (D3DXVECTOR3), &center, &radius);

	sphere[0] = center.x;
	sphere[1] = center.y;
	sphere[2] = center.z;
	sphere[3] = radius;
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
	mod_novis = (byte *) ModelZone->Alloc ((mod->brushhdr->numleafs + 7) / 8);
	memset (mod_novis, 0xff, (mod->brushhdr->numleafs + 7) / 8);
	checkpvs = (byte *) ModelZone->Alloc ((mod->brushhdr->numleafs + 7) / 8);

	fatbytes = (mod->brushhdr->numleafs + 31) >> 3;
	fatpvs = (byte *) ModelZone->Alloc (fatbytes);
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


int Mod_CompressVis (byte *vis, byte *dest, model_t *model)
{
	int		j;
	int		rep;
	byte	*dest_p;
	int row = (model->brushhdr->numleafs + 7) >> 3;

	dest_p = dest;

	for (j = 0; j < row; j++)
	{
		*dest_p++ = vis[j];

		if (vis[j])
			continue;

		rep = 1;

		for (j++; j < row; j++)
		{
			if (vis[j] || rep == 255)
				break;
			else rep++;
		}

		*dest_p++ = rep;
		j--;
	}

	return dest_p - dest;
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


// fixme - is this now the same as the server-side version?
void Mod_AddToFatPVS (vec3_t org, mnode_t *node)
{
	// optimized recursion without a goto! whee!
	while (1)
	{
		// if this is a leaf, accumulate the pvs bits
		if (node->contents < 0)
		{
			if (node->contents != CONTENTS_SOLID)
			{
				byte *pvs = Mod_LeafPVS ((mleaf_t *) node, cl.worldmodel);

				for (int i = 0; i < fatbytes; i++) fatpvs[i] |= pvs[i];
			}

			return;
		}

		float d = SV_PlaneDist (node->plane, org);

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
	fatbytes = (cl.worldmodel->brushhdr->numleafs + 31) >> 3;
	memset (fatpvs, 0, fatbytes);

	sv_frame--;
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

	SAFE_DELETE (ModelZone);
	ModelZone = new CQuakeZone ();

	// NULL the structs
	for (i = 0; i < MAX_MOD_KNOWN; i++)
		mod_known[i] = NULL;

	// note - this was a nasty memory leak which I'm sure was inherited from the original code.
	// the models array never went down, so if over MAX_MOD_KNOWN unique models get loaded it's crash time.
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
		mod_known[i] = (model_t *) ModelZone->Alloc (sizeof (model_t));

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
void Mod_TouchModel (char *name) {Mod_FindName (name);}


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
	if (!mod->needload)
	{
		mod->RegistrationSequence = d3d_RenderDef.RegistrationSequence;
		mod->radius = RadiusFromBounds (mod->mins, mod->maxs);
		Mod_SphereFromBounds (mod->mins, mod->maxs, mod->sphere);
		return mod;
	}

	// look for a cached copy
	model_t *cachemod = (model_t *) MainCache->Check (mod->name);

	if (cachemod)
	{
		// did we find it?
		memcpy (mod, cachemod, sizeof (model_t));

		// don't need to load it
		mod->needload = false;
		mod->RegistrationSequence = d3d_RenderDef.RegistrationSequence;
		mod->radius = RadiusFromBounds (mod->mins, mod->maxs);
		Mod_SphereFromBounds (mod->mins, mod->maxs, mod->sphere);

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
	mod->iqmheader = NULL;

	switch (((unsigned *) buf)[0])
	{
	case IQM_FOURCC:
		Mod_LoadIQMModel (mod, buf, mod->name);
		break;

	case IDPOLYHEADER:
		Mod_LoadAliasModel (mod, buf);
		break;

	case IDSPRITEHEADER:
		Mod_LoadSpriteModel (mod, buf);
		break;

	case Q1_BSPVERSION:
	case PR_BSPVERSION:
	case BSPVERSIONRMQ:
		// bsp files don't have a header ident... sigh...
		// the version seems good for use though
		Mod_LoadBrushModel (mod, buf);
		break;

	default:
		// we can't host_error here as this will dirty the model cache
		Sys_Error ("Unknown model type for %s ('%c' '%c' '%c' '%c')\n", mod->name,
			((char *) buf)[0], ((char *) buf)[1], ((char *) buf)[2], ((char *) buf)[3]);
		break;
	}

	// eval the radius to get a bounding sphere
	mod->radius = RadiusFromBounds (mod->mins, mod->maxs);
	Mod_SphereFromBounds (mod->mins, mod->maxs, mod->sphere);

	Zone_Free (buf);

	mod->RegistrationSequence = d3d_RenderDef.RegistrationSequence;
	return mod;
}


/*
==================
Mod_ForName

Loads in a model for the given name
==================
*/
model_t *Mod_ForName (char *name, bool crash) {return Mod_LoadModel (Mod_FindName (name), crash);}


bool HasLumaTexels (byte *data, int size)
{
	for (int i = 0; i < size; i++)
	{
		if (data[i] == 255) continue;
		if (data[i] < 224) continue;

		return true;
	}

	return false;
}


/*
===============================================================================

					BRUSHMODEL LOADING

===============================================================================
*/


/*
=================
Mod_LoadEdges

Yayy C++
=================
*/
template <typename edgetype_t>
void Mod_LoadEdges (model_t *mod, byte *mod_base, lump_t *l)
{
	typename edgetype_t *in = (typename edgetype_t *) (mod_base + l->fileofs);

	if (l->filelen % sizeof (typename edgetype_t))
		Host_Error ("Mod_LoadBrushModel: LUMP_EDGES funny lump size in %s", mod->name);

	int count = l->filelen / sizeof (typename edgetype_t);
	medge_t *out = (medge_t *) ModelZone->Alloc ((count + 1) * sizeof (medge_t));

	mod->brushhdr->edges = out;
	mod->brushhdr->numedges = count;

	for (int i = 0; i < count; i++, in++, out++)
	{
		out->v[0] = in->v[0];
		out->v[1] = in->v[1];
	}
}


/*
=================
Mod_LoadTextures
=================
*/
char *Mod_ParseWadName (char *data)
{
	char key[128];
	static char *value = (char *) scratchbuf;

	if (!(data = COM_Parse (data))) return NULL;
	if (com_token[0] != '{') return NULL;

	while (1)
	{
		if (!(data = COM_Parse (data))) return NULL;
		if (com_token[0] == '}') return NULL;

		strcpy (key, com_token);
		_strlwr (key);

		if (!(data = COM_Parse (data))) return NULL; // error

		if (!strcmp (key, "wad"))
		{
			strcpy (value, com_token);
			return value;
		}
	}
}


// http://stackoverflow.com/questions/122616/how-do-i-trim-leading-trailing-whitespace-in-a-standard-way
// but we don't care about the memory issues mentioned there as we're allocating these on the hunk
// note however that this means that this may not be safe to call from elsewhere!!!
char *trimwhitespace (char *str)
{
	char *end;

	// Trim leading space
	while (isspace (*str)) str++;

	if (*str == 0)  // All spaces?
		return str;

	// Trim trailing space
	end = str + strlen (str) - 1;
	while (end > str && isspace (*end)) end--;

	// Write new null terminator
	*(end + 1) = 0;

	return str;
}


void Mod_LoadTextures (model_t *mod, byte *mod_base, lump_t *l, lump_t *e)
{
	int		i, j, num, max, altmax;
	miptex_t	*mt;
	texture_t	*tx, *tx2;
	texture_t	*anims[10];
	texture_t	*altanims[10];
	dmiptexlump_t *m;

	char texpath[64];
	char modelpath[128];
	char *paths[256] = {NULL};

	int hunkmark = MainHunk->GetLowMark ();

	strcpy (texpath, "textures/");
	sprintf (modelpath, "textures/%s", &mod->name[5]);

	for (i = strlen (modelpath) - 1; i; i--)
	{
		if (modelpath[i] == '.')
		{
			modelpath[i] = '/';
			modelpath[i + 1] = 0;
			break;
		}
	}

	// extract the wad names from the entities lump so that they can be added to the search paths
	int Wad = 0;
	char *wadnames = Mod_ParseWadName ((char *) (mod_base + e->fileofs));

	// some maps don't have a "wad" key
	if (wadnames)
	{
		// now parse individual wads out of the names (need to steal some QBSP code for this...)
		int NoOfWads = 1;

		for (i = 0; ; i++)
		{
			if (!wadnames[i]) break;
			if (wadnames[i] == ';') NoOfWads++;
		}

		// save space for the default paths (this is a little loose but we'll never have this many WADS anyway
		if (NoOfWads > 250) NoOfWads -= 3;

		char *Ptr = wadnames;

		for (i = 0; i < NoOfWads; i++)
		{
			char *Ptr2 = Ptr;

			// Extract current WAD file name
			while (*Ptr2 != ';' && *Ptr2 != '\0') Ptr2++;

			paths[Wad++] = Ptr;

			if (*Ptr2 != '\0') Ptr = Ptr2 + 1;
		}

		for (i = 0; i < Wad; i++)
		{
			// find first delimiter or end of string
			for (j = 0; ; j++)
			{
				if (paths[i][j] == ';') paths[i][j] = 0;
				if (paths[i][j] == 0) break;
			}

			// count back to remove extension and prepended paths
			for (; j; j--)
			{
				if (paths[i][j] == '.') paths[i][j] = 0;

				if (paths[i][j] == '/' || paths[i][j] == '\\')
				{
					// we have the name of the wad now
					paths[i] = &paths[i][j + 1];
					break;
				}
			}

			// trim leading and trailing spaces
			trimwhitespace (paths[i]);

			// set up for real
			// the final path needs to go on the hunk as scratchbuf may be unsafe to use
			// note that no world content actually goes on the hunk anymore so this can be just thrown out when done
			Ptr = paths[i];
			paths[i] = (char *) MainHunk->Alloc (strlen (Ptr) + 12);	// one or two extra bytes!  oh noes!  wasting memory!
			sprintf (paths[i], "textures/%s/", Ptr);
		}
	}

	// append the rest of the paths
	paths[Wad++] = modelpath;
	paths[Wad++] = texpath;
	paths[Wad++] = NULL;

	// check if these directories exist and if they don't then don't bother searching for externals in them
	// because we support 7 different image types a one-time search of all paths upfront should be faster than 7 * numpaths * numtextures
	COM_ValidatePaths (paths);

	if (!l->filelen)
	{
		mod->brushhdr->textures = NULL;
		return;
	}

	m = (dmiptexlump_t *) (mod_base + l->fileofs);

	mod->brushhdr->numtextures = m->nummiptex;
	mod->brushhdr->textures = (texture_t **) ModelZone->Alloc (mod->brushhdr->numtextures * sizeof (texture_t *));
	tx = (texture_t *) ModelZone->Alloc (sizeof (texture_t) * mod->brushhdr->numtextures);

	for (i = 0; i < m->nummiptex; i++, tx++)
	{
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

		tx->size[0] = mt->width;
		tx->size[1] = mt->height;

		// check for water
		if (mt->name[0] == '*')
		{
			int size = mt->width * mt->height;

			tx->contentscolor[0] = 0;
			tx->contentscolor[1] = 0;
			tx->contentscolor[2] = 0;

			for (j = 0; j < size; j++)
			{
				PALETTEENTRY bgra = d3d_QuakePalette.standard[mt->texels[j]];

				tx->contentscolor[0] += bgra.peRed;
				tx->contentscolor[1] += bgra.peGreen;
				tx->contentscolor[2] += bgra.peBlue;
			}

			// bring to approximate scale of the cshifts
			tx->contentscolor[0] /= (size / 3);
			tx->contentscolor[1] /= (size / 3);
			tx->contentscolor[2] /= (size / 3);
		}
		else
		{
			tx->contentscolor[0] = 255;
			tx->contentscolor[1] = 255;
			tx->contentscolor[2] = 255;
		}

		if (!_strnicmp (mt->name, "sky", 3))
		{
			D3DSky_InitTextures (mt, paths);

			// set correct dimensions for texcoord building
			tx->size[0] = 64.0f;
			tx->size[1] = 64.0f;
		}
		else
		{
			int texflags = IMAGE_MIPMAP | IMAGE_BSP;
			int lumaflags = IMAGE_MIPMAP | IMAGE_BSP | IMAGE_LUMA;

			if (mt->name[0] == '*') texflags |= IMAGE_LIQUID;
			if (mt->name[0] == '{') texflags |= IMAGE_FENCE;

			tx->teximage = D3DTexture_Load (mt->name, mt->width, mt->height, mt->texels, texflags, paths);
			tx->lumaimage = D3DTexture_Load (mt->name, mt->width, mt->height, mt->texels, lumaflags, paths);
		}
	}

	MainHunk->FreeToLowMark (hunkmark);

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
bool Mod_LoadLITFile (model_t *mod, lump_t *l)
{
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

	// expand size to 3 component
	mod->brushhdr->lightdata = (byte *) ModelZone->Alloc (l->filelen * 3);

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
	mod->brushhdr->entities = (char *) ModelZone->Alloc (l->filelen + 1);
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

	if (l->filelen % sizeof (dmodel_t))
		Host_Error ("Mod_LoadBrushModel: LUMP_SUBMODELS funny lump size in %s", mod->name);

	count = l->filelen / sizeof (dmodel_t);
	out = (dmodel_t *) ModelZone->Alloc (count * sizeof (*out));

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
		Host_Error ("Mod_LoadBrushModel: LUMP_TEXINFO funny lump size in %s", mod->name);

	count = l->filelen / sizeof (texinfo_t);
	out = (mtexinfo_t *) ModelZone->Alloc (count * sizeof (mtexinfo_t));

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
void Mod_LoadSurfaceVertexes (model_t *mod, msurface_t *surf)
{
	// let's calc bounds and extents here too...!
	float mins[2] = {99999999.0f, 99999999.0f};
	float maxs[2] = {-99999999.0f, -99999999.0f};

	Mod_ClearBoundingBox (surf->mins, surf->maxs);

	for (int i = 0; i < surf->numvertexes; i++)
	{
		int lindex = mod->brushhdr->dsurfedges[surf->firstedge + i];
		float *vec;

		if (lindex > 0)
			vec = mod->brushhdr->dvertexes[mod->brushhdr->edges[lindex].v[0]].point;
		else vec = mod->brushhdr->dvertexes[mod->brushhdr->edges[-lindex].v[1]].point;

		// bounds
		Mod_AccumulateBox (surf->mins, surf->maxs, vec);

		// extents
		for (int j = 0; j < 2; j++)
		{
			float st = DotProduct (vec, surf->texinfo->vecs[j]) + surf->texinfo->vecs[j][3];

			if (st < mins[j]) mins[j] = st;
			if (st > maxs[j]) maxs[j] = st;
		}
	}

	// midpoint
	for (int i = 0; i < 3; i++)
	{
		// expand the bbox by 1 unit in each direction to ensure that marginal surfs don't get culled
		// (needed for R_RecursiveWorldNode avoidance)
		surf->mins[i] -= 1.0f;
		surf->maxs[i] += 1.0f;

		// get final mindpoint
		surf->midpoint[i] = surf->mins[i] + (surf->maxs[i] - surf->mins[i]) * 0.5f;
	}

	Mod_SphereFromBounds (surf->mins, surf->maxs, surf->sphere);

	// no extents
	if ((surf->flags & SURF_DRAWTURB) || (surf->flags & SURF_DRAWSKY)) return;

	// now do extents
	for (int i = 0; i < 2; i++)
	{
		int bmins = floor (mins[i] / 16);
		int bmaxs = ceil (maxs[i] / 16);

		surf->texturemins[i] = bmins * 16;
		surf->extents[i] = (bmaxs - bmins) * 16;

		// to all practical purposes this will never be hit; even on 3DFX it's 4080 which is well in excess of the
		// max allowed by stock ID Quake.  just clamping it may result in weird lightmaps in extreme maps, but in
		// practice it doesn't even happen for reasons previously outlined.
		if (surf->extents[i] > d3d_GlobalCaps.MaxExtents) surf->extents[i] = d3d_GlobalCaps.MaxExtents;
	}
}


/*
=================
Mod_LoadSurfaces

Yayy C++
=================
*/
template <typename facetype_t>
void Mod_LoadSurfaces (model_t *mod, byte *mod_base, lump_t *l)
{
	typename facetype_t *face = (typename facetype_t *) (mod_base + l->fileofs);

	if (l->filelen % sizeof (typename facetype_t))
	{
		Host_Error ("Mod_LoadSurfaces: LUMP_FACES funny lump size in %s", mod->name);
		return;
	}

	int count = l->filelen / sizeof (typename facetype_t);
	msurface_t *surf = (msurface_t *) ModelZone->Alloc (count * sizeof (msurface_t));

	mod->brushhdr->surfaces = surf;
	mod->brushhdr->numsurfaces = count;
	mod->brushhdr->numsurfvertexes = 0;

	for (int surfnum = 0; surfnum < count; surfnum++, face++, surf++)
	{
		surf->model = mod;

		// verts/etc
		surf->firstedge = face->firstedge;

		if (mod->brushhdr->bspversion == BSPVERSIONRMQ)
		{
			surf->numvertexes = face->numedges;
			surf->plane = mod->brushhdr->planes + face->planenum;
			surf->texinfo = mod->brushhdr->texinfo + face->texinfo;
		}
		else
		{
			surf->numvertexes = (unsigned short) face->numedges;
			surf->plane = mod->brushhdr->planes + (unsigned short) face->planenum;
			surf->texinfo = mod->brushhdr->texinfo + (unsigned short) face->texinfo;
		}

		surf->numindexes = (surf->numvertexes - 2) * 3;
		mod->brushhdr->numsurfvertexes += surf->numvertexes;
		surf->flags = 0;

		if (face->side) surf->flags |= SURF_PLANEBACK;

		// lighting info
		for (int i = 0; i < MAX_SURFACE_STYLES; i++)
			surf->styles[i] = face->styles[i];

		// expand offsets for pre-expanded light
		if (face->lightofs < 0)
			surf->samples = NULL;
		else surf->samples = mod->brushhdr->lightdata + (face->lightofs * 3);

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

		Mod_LoadSurfaceVertexes (mod, surf);
	}
}


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
Mod_LoadVisLeafsNodes

handles visibility, leafs and nodes

Yayy C++
=================
*/
template <typename nodetype_t, typename leaftype_t>
void Mod_LoadVisLeafsNodes (model_t *mod, byte *mod_base, lump_t *v, lump_t *l, lump_t *n)
{
	leaftype_t 	*lin;
	mleaf_t 	*lout;
	nodetype_t	*nin;
	mnode_t		*nout;
	int			i, j, leafcount, p;
	int			nodecount;

	// initial in pointers for leafs and nodes
	lin = (leaftype_t *) (mod_base + l->fileofs);
	nin = (nodetype_t *) (mod_base + n->fileofs);

	if (l->filelen % sizeof (leaftype_t)) Host_Error ("Mod_LoadBrushModel: LUMP_LEAFS funny lump size in %s", mod->name);
	if (n->filelen % sizeof (nodetype_t)) Host_Error ("Mod_LoadBrushModel: LUMP_NODES funny lump size in %s", mod->name);

	leafcount = l->filelen / sizeof (leaftype_t);
	nodecount = n->filelen / sizeof (nodetype_t);

	mod->brushhdr->numleafs = leafcount;
	mod->brushhdr->numnodes = nodecount;

	// this will crash in-game, so better to take down as gracefully as possible before that
	// note that this is a map format limitation owing to the use of signed shorts for leaf/node numbers
	// (but not necessarily; numleafs + numnodes must not exceed 65536 actually)
	if (mod->brushhdr->numleafs > 65536 && mod->brushhdr->bspversion != BSPVERSIONRMQ)
	{
		Host_Error ("Mod_LoadLeafs: mod->brushhdr->numleafs > 65536 without BSPVERSIONRMQ");
		return;
	}

	if (mod->brushhdr->numnodes > 65536 && mod->brushhdr->bspversion != BSPVERSIONRMQ)
	{
		Host_Error ("Mod_LoadNodes: mod->brushhdr->numleafs > 65536 without BSPVERSIONRMQ");
		return;
	}

	if (mod->brushhdr->numnodes + mod->brushhdr->numleafs > 65536 && mod->brushhdr->bspversion != BSPVERSIONRMQ)
	{
		Host_Error ("Mod_LoadNodes: mod->brushhdr->numnodes + mod->brushhdr->numleafs > 65536 without BSPVERSIONRMQ");
		return;
	}

	// set up vis data buffer and load it in
	byte *visdata = NULL;

	if (v->filelen)
	{
		visdata = (byte *) ModelZone->Alloc (v->filelen);
		memcpy (visdata, mod_base + v->fileofs, v->filelen);
	}

	// nodes and leafs need to be in consecutive memory - see comment in R_LeafVisibility
	lout = (mleaf_t *) ModelZone->Alloc ((leafcount * sizeof (mleaf_t)) + (nodecount * sizeof (mnode_t)));
	mod->brushhdr->leafs = lout;

	for (i = 0; i < leafcount; i++, lin++, lout++)
	{
		// correct number for SV_ processing
		lout->num = i - 1;

		for (j = 0; j < 3; j++)
		{
			lout->mins[j] = lin->mins[j];
			lout->maxs[j] = lin->maxs[j];
		}

		Mod_SphereFromBounds (lout->mins, lout->maxs, lout->sphere);

		p = lin->contents;
		lout->contents = p;

		if (mod->brushhdr->bspversion == BSPVERSIONRMQ)
		{
			lout->firstmarksurface = mod->brushhdr->marksurfaces + lin->firstmarksurface;
			lout->nummarksurfaces = lin->nummarksurfaces;
		}
		else
		{
			// cast to unsigned short to conform to BSP file spec
			lout->firstmarksurface = mod->brushhdr->marksurfaces + (unsigned short) lin->firstmarksurface;
			lout->nummarksurfaces = (unsigned short) lin->nummarksurfaces;
		}

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

		for (j = 0; j < lout->nummarksurfaces; j++)
		{
			if (lout->contents == CONTENTS_WATER || lout->contents == CONTENTS_LAVA || lout->contents == CONTENTS_SLIME)
			{
				// set contents colour for this leaf
				if ((lout->firstmarksurface[j]->flags & SURF_DRAWWATER) && lout->contents == CONTENTS_WATER)
					lout->contentscolor = lout->firstmarksurface[j]->texinfo->texture->contentscolor;
				else if ((lout->firstmarksurface[j]->flags & SURF_DRAWLAVA) && lout->contents == CONTENTS_LAVA)
					lout->contentscolor = lout->firstmarksurface[j]->texinfo->texture->contentscolor;
				else if ((lout->firstmarksurface[j]->flags & SURF_DRAWSLIME) && lout->contents == CONTENTS_SLIME)
					lout->contentscolor = lout->firstmarksurface[j]->texinfo->texture->contentscolor;
			}

			// duplicate surf flags into the leaf
			lout->flags |= lout->firstmarksurface[j]->flags;
		}

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

		Mod_SphereFromBounds (nout->mins, nout->maxs, nout->sphere);

		p = nin->planenum;
		nout->plane = mod->brushhdr->planes + p;

		nout->visframe = -1;

		// the old sexy code i had for setting parent and children at the same time was really fucking with world interaction.
		// things work now.  DO NOT TRY TO SEX THIS UP.
		if (mod->brushhdr->bspversion == BSPVERSIONRMQ)
		{
			nout->surfaces = mod->brushhdr->surfaces + nin->firstface;
			nout->numsurfaces = nin->numfaces;

			// set children and parents here too
			// note - the memory has already been allocated and this field won't be overwritten during node loading
			// so even if a node hasn't yet been loaded it's safe to do this; leafs of course have all been loaded.
			// what the fuck was i smoking when i wrote this...?
			for (j = 0; j < 2; j++)
			{
				if ((p = nin->children[j]) >= 0)
					nout->children[j] = mod->brushhdr->nodes + p;
				else nout->children[j] = (mnode_t *) (mod->brushhdr->leafs + (-1 - p));
			}
		}
		else
		{
			nout->surfaces = mod->brushhdr->surfaces + (unsigned short) nin->firstface;
			nout->numsurfaces = (unsigned short) nin->numfaces;

			for (j = 0; j < 2; j++)
			{
				p = (unsigned short) nin->children[j];

				if (p < nodecount)
					nout->children[j] = mod->brushhdr->nodes + p;
				else
				{
					// note this uses 65535 intentionally, -1 is leaf 0
					p = 65535 - p;

					if (p < mod->brushhdr->numleafs)
						nout->children[j] = (mnode_t *) (mod->brushhdr->leafs + p);
					else
					{
						// map it to the solid leaf
						nout->children[j] = (mnode_t *) (mod->brushhdr->leafs);
						Con_DPrintf ("Mod_LoadNodes: invalid leaf index %i (file has only %i leafs)\n", p, mod->brushhdr->numleafs);
					}
				}
			}
		}
	}

	// first node has no parent
	Mod_SetParent (mod->brushhdr->nodes, NULL);
}


/*
=================
Mod_LoadClipnodes

Yayy C++
=================
*/
template <typename clipnode_t>
void Mod_LoadClipnodes (model_t *mod, byte *mod_base, lump_t *l)
{
	typename clipnode_t *in = (typename clipnode_t *) (mod_base + l->fileofs);

	if (l->filelen % sizeof (typename clipnode_t))
		Host_Error ("Mod_LoadBrushModel: LUMP_CLIPNODES funny lump size in %s", mod->name);

	int count = l->filelen / sizeof (typename clipnode_t);
	mclipnode_t *out = (mclipnode_t *) ModelZone->Alloc (count * sizeof (mclipnode_t));

	mod->brushhdr->clipnodes = out;
	mod->brushhdr->numclipnodes = count;

	mod->brushhdr->hulls[1].clipnodes = out;
	mod->brushhdr->hulls[1].firstclipnode = 0;
	mod->brushhdr->hulls[1].lastclipnode = count - 1;
	mod->brushhdr->hulls[1].planes = mod->brushhdr->planes;

	mod->brushhdr->hulls[1].clip_mins[0] = -16;
	mod->brushhdr->hulls[1].clip_mins[1] = -16;
	mod->brushhdr->hulls[1].clip_mins[2] = -24;

	mod->brushhdr->hulls[1].clip_maxs[0] = 16;
	mod->brushhdr->hulls[1].clip_maxs[1] = 16;
	mod->brushhdr->hulls[1].clip_maxs[2] = 32;

	mod->brushhdr->hulls[2].clipnodes = out;
	mod->brushhdr->hulls[2].firstclipnode = 0;
	mod->brushhdr->hulls[2].lastclipnode = count - 1;
	mod->brushhdr->hulls[2].planes = mod->brushhdr->planes;

	mod->brushhdr->hulls[2].clip_mins[0] = -32;
	mod->brushhdr->hulls[2].clip_mins[1] = -32;
	mod->brushhdr->hulls[2].clip_mins[2] = -24;

	mod->brushhdr->hulls[2].clip_maxs[0] = 32;
	mod->brushhdr->hulls[2].clip_maxs[1] = 32;
	mod->brushhdr->hulls[2].clip_maxs[2] = 64;

	for (int i = 0; i < count; i++, out++, in++)
	{
		out->planenum = in->planenum;

		if (mod->brushhdr->bspversion == BSPVERSIONRMQ)
		{
			out->children[0] = in->children[0];
			out->children[1] = in->children[1];
		}
		else
		{
			out->children[0] = (unsigned short) in->children[0];
			out->children[1] = (unsigned short) in->children[1];

			// support > 32k clipnodes
			if (out->children[0] >= count) out->children[0] -= 65536;
			if (out->children[1] >= count) out->children[1] -= 65536;

			if (out->children[0] < CONTENTS_CLIP) out->children[0] += 65536;
			if (out->children[1] < CONTENTS_CLIP) out->children[1] += 65536;
		}
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
	mnode_t *in = mod->brushhdr->nodes;
	int count = mod->brushhdr->numnodes;
	mclipnode_t *out = (mclipnode_t *) ModelZone->Alloc (count * sizeof (mclipnode_t));

	mod->brushhdr->hulls[0].clipnodes = out;
	mod->brushhdr->hulls[0].firstclipnode = 0;
	mod->brushhdr->hulls[0].lastclipnode = count - 1;
	mod->brushhdr->hulls[0].planes = mod->brushhdr->planes;

	for (int i = 0; i < count; i++, out++, in++)
	{
		out->planenum = in->plane - mod->brushhdr->planes;

		for (int j = 0; j < 2; j++)
		{
			mnode_t *child = in->children[j];

			if (child->contents < 0)
				out->children[j] = child->contents;
			else out->children[j] = child - mod->brushhdr->nodes;
		}
	}
}

/*
=================
Mod_LoadMarksurfaces

Yayy C++
=================
*/
template <typename marksurf_t>
void Mod_LoadMarksurfaces (model_t *mod, byte *mod_base, lump_t *l)
{
	typename marksurf_t *in = (typename marksurf_t *) (mod_base + l->fileofs);

	if (l->filelen % sizeof (typename marksurf_t))
		Host_Error ("Mod_LoadBrushModel: LUMP_MARKSURFACES funny lump size in %s", mod->name);

	int count = l->filelen / sizeof (typename marksurf_t);
	msurface_t **out = (msurface_t **) ModelZone->Alloc (count * sizeof (msurface_t *));

	mod->brushhdr->marksurfaces = out;
	mod->brushhdr->nummarksurfaces = count;

	for (int i = 0; i < count; i++)
	{
		if (in[i] >= mod->brushhdr->numsurfaces)
			Host_Error ("Mod_ParseMarksurfaces: bad surface number");

		out[i] = mod->brushhdr->surfaces + in[i];
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
	dplane_t 	*in;
	mplane_t	*out;
	int			count;
	int			bits;

	in = (dplane_t *) (mod_base + l->fileofs);

	if (l->filelen % sizeof (dplane_t))
		Host_Error ("Mod_LoadBrushModel: LUMP_PLANES funny lump size in %s", mod->name);

	count = l->filelen / sizeof (dplane_t);

	// was count * 2; i believe this was to make extra space for a possible expansion of planes * 2 in
	// order to precache both orientations of each plane and not have to use the SURF_PLANEBACK stuff.
	out = (mplane_t *) ModelZone->Alloc (count * sizeof (mplane_t));

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


void Mod_RecalcNodeBBox (mnode_t *node)
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
			Mod_ClearBoundingBox (leaf->mins, leaf->maxs);

			do
			{
				Mod_AccumulateBox (leaf->mins, leaf->maxs, (*mark)->mins, (*mark)->maxs);
				mark++;
			} while (--c);
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

	Mod_SphereFromBounds (node->mins, node->maxs, node->sphere);
}


void *Mod_CopyLump (dheader_t *header, int lump)
{
	void *data = ModelZone->Alloc (header->lumps[lump].filelen);
	byte *mod_base = (byte *) header;

	memcpy (data, mod_base + header->lumps[lump].fileofs, header->lumps[lump].filelen);

	return data;
}


void Mod_CalcBModelBBox (model_t *mod, brushhdr_t *hdr)
{
	// qbsp is goddam SLOPPY with bboxes so let's do them right
	msurface_t *surf = hdr->surfaces + hdr->firstmodelsurface;

	Mod_ClearBoundingBox (hdr->bmins, hdr->bmaxs);

	for (int i = 0; i < hdr->nummodelsurfaces; i++, surf++)
		Mod_AccumulateBox (hdr->bmins, hdr->bmaxs, surf->mins, surf->maxs);
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
	if (i != PR_BSPVERSION && i != Q1_BSPVERSION && i != BSPVERSIONRMQ)
	{
		Host_Error ("Mod_LoadBrushModel: %s has wrong version number\n(%i should be %i or %i or %i)",
			mod->name, i, PR_BSPVERSION, Q1_BSPVERSION, BSPVERSIONRMQ);
		return;
	}

	byte *mod_base = (byte *) header;

	// alloc space for a brush header
	mod->brushhdr = (brushhdr_t *) ModelZone->Alloc (sizeof (brushhdr_t));

	// store the version for correct hull checking
	mod->brushhdr->bspversion = header->version;

	if (!d3d_RenderDef.WorldModelLoaded)
	{
		d3d_RenderDef.WorldModelLoaded = true;
		mod->flags |= MOD_WORLD;
	}
	else mod->flags |= MOD_BMODEL;

	mod->brushhdr->dvertexes = (dvertex_t *) Mod_CopyLump (header, LUMP_VERTEXES);
	mod->brushhdr->dsurfedges = (int *) Mod_CopyLump (header, LUMP_SURFEDGES);

	// load into heap (these are the only lumps we need to leave hanging around)
	Mod_LoadTextures (mod, mod_base, &header->lumps[LUMP_TEXTURES], &header->lumps[LUMP_ENTITIES]);
	Mod_LoadLighting (mod, mod_base, &header->lumps[LUMP_LIGHTING]);
	Mod_LoadPlanes (mod, mod_base, &header->lumps[LUMP_PLANES]);
	Mod_LoadTexinfo (mod, mod_base, &header->lumps[LUMP_TEXINFO]);

	// Yayy C++
	if (header->version == BSPVERSIONRMQ)
	{
		Mod_LoadEdges<dedge29a_t> (mod, mod_base, &header->lumps[LUMP_EDGES]);
		Mod_LoadSurfaces<dface29a_t> (mod, mod_base, &header->lumps[LUMP_FACES]);
		Mod_LoadMarksurfaces<int> (mod, mod_base, &header->lumps[LUMP_MARKSURFACES]);
		Mod_LoadVisLeafsNodes<dnode29a_t, dleaf29a_t> (mod, mod_base, &header->lumps[LUMP_VISIBILITY], &header->lumps[LUMP_LEAFS], &header->lumps[LUMP_NODES]);
		Mod_LoadClipnodes<dclipnode29a_t> (mod, mod_base, &header->lumps[LUMP_CLIPNODES]);
	}
	else
	{
		Mod_LoadEdges<dedge_t> (mod, mod_base, &header->lumps[LUMP_EDGES]);
		Mod_LoadSurfaces<dface_t> (mod, mod_base, &header->lumps[LUMP_FACES]);
		Mod_LoadMarksurfaces<unsigned short> (mod, mod_base, &header->lumps[LUMP_MARKSURFACES]);
		Mod_LoadVisLeafsNodes<dnode_t, dleaf_t>  (mod, mod_base, &header->lumps[LUMP_VISIBILITY], &header->lumps[LUMP_LEAFS], &header->lumps[LUMP_NODES]);
		Mod_LoadClipnodes<dclipnode_t> (mod, mod_base, &header->lumps[LUMP_CLIPNODES]);
	}

	Mod_LoadEntities (mod, mod_base, &header->lumps[LUMP_ENTITIES]);
	Mod_LoadSubmodels (mod, mod_base, &header->lumps[LUMP_MODELS]);

	// if it's cheaper to just draw the model we just draw it
	if (mod->brushhdr->numsurfaces < 6) mod->flags |= EF_NOOCCLUDE;

	// set up the model for the same usage as submodels and correct it's bbox
	// (note - correcting the bbox can result in QC crashes in PF_setmodel)
	mod->brushhdr->firstmodelsurface = 0;
	mod->brushhdr->nummodelsurfaces = mod->brushhdr->numsurfaces;

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
	brushhdr_t *smheaders = (brushhdr_t *) ModelZone->Alloc (sizeof (brushhdr_t) * mod->brushhdr->numsubmodels);

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

		// if it's cheaper to just draw the model we just draw it
		if (mod->brushhdr->nummodelsurfaces < 6) mod->flags |= EF_NOOCCLUDE;

		// leafs
		mod->brushhdr->numleafs = bm->visleafs;

		// bounding box
		// (note - correcting this can result in QC crashes in PF_setmodel)
		VectorCopy2 (mod->maxs, bm->maxs);
		VectorCopy2 (mod->mins, bm->mins);

		// correct physics cullboxes so that outlying clip brushes on doors and stuff are handled right
		VectorCopy2 (mod->clipmins, mod->mins);
		VectorCopy2 (mod->clipmaxs, mod->maxs);

		// radius
		mod->radius = RadiusFromBounds (mod->mins, mod->maxs);
		mod->flags |= MOD_BMODEL;

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


extern float r_avertexnormals[162][3];

void Mod_LoadAliasBBoxes (model_t *mod, aliashdr_t *hdr)
{
	// compute bounding boxes per-frame as well as for the whole model
	// per-frame bounding boxes are used in the renderer for frustum culling and occlusion queries
	// whole model bounding boxes are used on the server
	aliasbbox_t *framebboxes = (aliasbbox_t *) MainCache->Alloc (hdr->nummeshframes * sizeof (aliasbbox_t));
	hdr->bboxes = framebboxes;

	Mod_ClearBoundingBox (mod->mins, mod->maxs);

	for (int i = 0; i < hdr->nummeshframes; i++, framebboxes++)
	{
		Mod_ClearBoundingBox (framebboxes->mins, framebboxes->maxs);

		for (int v = 0; v < hdr->vertsperframe; v++)
		{
			// we can't put this through our box accumulator as these are byte vectors
			for (int n = 0; n < 3; n++)
			{
				if (hdr->vertexes[i][v].v[n] < framebboxes->mins[n]) framebboxes->mins[n] = hdr->vertexes[i][v].v[n];
				if (hdr->vertexes[i][v].v[n] > framebboxes->maxs[n]) framebboxes->maxs[n] = hdr->vertexes[i][v].v[n];
			}
		}

		for (int n = 0; n < 3; n++)
		{
			// don't bother putting this through our accumulator as we need extra ops here
			framebboxes->mins[n] = framebboxes->mins[n] * hdr->scale[n] + hdr->scale_origin[n];
			framebboxes->maxs[n] = framebboxes->maxs[n] * hdr->scale[n] + hdr->scale_origin[n];

			if (framebboxes->mins[n] < mod->mins[n]) mod->mins[n] = framebboxes->mins[n];
			if (framebboxes->maxs[n] > mod->maxs[n]) mod->maxs[n] = framebboxes->maxs[n];
		}
	}

	// absolute clamp so that we don't go into the wrong clipping hull
	for (int i = 0; i < 3; i++)
	{
		if (mod->mins[i] > -16) mod->mins[i] = -16;
		if (mod->maxs[i] < 16) mod->maxs[i] = 16;
	}
}


void Mod_LoadFrameVerts (aliashdr_t *hdr, trivertx_t *verts)
{
	drawvertx_t *vertexes = (drawvertx_t *) MainCache->Alloc (hdr->vertsperframe * sizeof (drawvertx_t));
	hdr->vertexes[hdr->nummeshframes] = vertexes;

	for (int i = 0; i < hdr->vertsperframe; i++, vertexes++, verts++)
	{
		vertexes->v[0] = verts->v[0];
		vertexes->v[1] = verts->v[1];
		vertexes->v[2] = verts->v[2];
		vertexes->v[3] = 1;	// provide a default w coord for vertex buffer input
		vertexes->lightnormalindex = verts->lightnormalindex;
		vertexes->lerpvert = true;	// assume that the vertex will be lerped by default
	}

	hdr->nummeshframes++;
}


/*
=================
Mod_LoadAliasFrame
=================
*/
daliasframetype_t *Mod_LoadAliasFrame (aliashdr_t *hdr, daliasframe_t *pdaliasframe, maliasframedesc_t *frame)
{
	Q_strncpy (frame->name, pdaliasframe->name, 16);
	frame->firstpose = hdr->nummeshframes;
	frame->numposes = 1;

	for (int i = 0; i < 3; i++)
	{
		frame->bboxmin.v[i] = pdaliasframe->bboxmin.v[i];
		frame->bboxmax.v[i] = pdaliasframe->bboxmax.v[i];
	}

	trivertx_t *verts = (trivertx_t *) (pdaliasframe + 1);

	// load the frame vertexes
	Mod_LoadFrameVerts (hdr, verts);

	return (daliasframetype_t *) (verts + hdr->vertsperframe);
}


/*
=================
Mod_LoadAliasGroup
=================
*/
daliasframetype_t *Mod_LoadAliasGroup (aliashdr_t *hdr, daliasgroup_t *pingroup, maliasframedesc_t *frame)
{
	frame->firstpose = hdr->nummeshframes;
	frame->numposes = pingroup->numframes;

	for (int i = 0; i < 3; i++)
	{
		frame->bboxmin.v[i] = pingroup->bboxmin.v[i];
		frame->bboxmax.v[i] = pingroup->bboxmax.v[i];
	}

	// let's do frame intervals properly
	daliasinterval_t *pin_intervals = (daliasinterval_t *) (pingroup + 1);
	frame->intervals = (float *) MainCache->Alloc ((pingroup->numframes + 1) * sizeof (float));

	// store an extra interval so that we can determine the correct frame difference between two
	// (intervals[pose + 1] - intervals[pose]
	frame->intervals[pingroup->numframes] = pin_intervals[pingroup->numframes - 1].interval + pin_intervals[0].interval;

	void *ptemp = (void *) (pin_intervals + pingroup->numframes);

	for (int i = 0; i < pingroup->numframes; i++)
	{
		frame->intervals[i] = pin_intervals[i].interval;
		Mod_LoadFrameVerts (hdr, (trivertx_t *) ((daliasframe_t *) ptemp + 1));
		ptemp = (trivertx_t *) ((daliasframe_t *) ptemp + 1) + hdr->vertsperframe;
	}

	return (daliasframetype_t *) ptemp;
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


LPDIRECT3DTEXTURE9 Mod_LoadPlayerColormap (char *name, model_t *mod, aliashdr_t *hdr, byte *texels)
{
	int s = hdr->skinwidth * hdr->skinheight;
	unsigned int *rgba = (unsigned int *) MainZone->Alloc (s * 4);
	byte *pixels = (byte *) rgba;

	for (int j = 0; j < s; j++, pixels += 4)
	{
		bool base = false;
		bool shirt = false;
		bool pants = false;

		if (texels[j] < 16)
			base = true;
		else if (texels[j] < 32)
			shirt = true;
		else if (texels[j] < 96)
			base = true;
		else if (texels[j] < 112)
			pants = true;
		else base = true;

		// these are greyscale colours so we can just take the red channel (the others are equal anyway)
		pixels[2] = shirt ? 255 : 0;
		pixels[1] = shirt ? d3d_QuakePalette.standard[texels[j] - 16].peRed : 0;
		pixels[0] = pants ? d3d_QuakePalette.standard[texels[j] - 96].peRed : 0;
		pixels[3] = pants ? 255 : 0;
	}

	LPDIRECT3DTEXTURE9 cm = D3DTexture_Load (va ("%s_colormap", name),
		hdr->skinwidth, hdr->skinheight,
		(byte *) rgba, IMAGE_ALPHA | IMAGE_MIPMAP | IMAGE_ALIAS | IMAGE_32BIT);

	// SCR_WriteDataToTGA ("playerskin.tga", (byte *) rgba, hdr->skinwidth, hdr->skinheight, 32, 32);

	MainZone->Free (rgba);

	return cm;
}


/*
===============
Mod_LoadAllSkins
===============
*/
void *Mod_LoadAllSkins (model_t *mod, aliashdr_t *hdr, daliasskintype_t *pskintype)
{
	int		i, j, k;
	char	name[32];
	int		s;
	byte	*skin;
	daliasskingroup_t		*pinskingroup;
	int		groupskins;
	daliasskininterval_t	*pinskinintervals;

	skin = (byte *) (pskintype + 1);

	if (hdr->numskins < 1) Host_Error ("Mod_LoadAliasModel: Invalid # of skins: %d\n", hdr->numskins);

	hdr->skins = (aliasskin_t *) MainCache->Alloc (hdr->numskins * sizeof (aliasskin_t));
	s = hdr->skinwidth * hdr->skinheight;

	LPDIRECT3DTEXTURE9 tex;
	LPDIRECT3DTEXTURE9 luma;
	LPDIRECT3DTEXTURE9 cmap;
	byte *texels;

	// don't remove the extension here as Q1 has s_light.mdl and s_light.spr, so we need to differentiate them
	// dropped skin padding because there are too many special cases
	for (i = 0; i < hdr->numskins; i++)
	{
		if (pskintype->type == ALIAS_SKIN_SINGLE)
		{
			// fixme - is this even needed any more (due to padding?)
			Mod_FloodFillSkin (skin, hdr->skinwidth, hdr->skinheight);

			_snprintf (name, 32, "%s_%i", mod->name, i);

			texels = (byte *) (pskintype + 1);

			int texflags = IMAGE_MIPMAP | IMAGE_ALIAS;
			int lumaflags = IMAGE_MIPMAP | IMAGE_ALIAS | IMAGE_LUMA;

			// default paths are good for these
			tex = D3DTexture_Load (name, hdr->skinwidth, hdr->skinheight, texels, texflags);
			luma = D3DTexture_Load (name, hdr->skinwidth, hdr->skinheight, texels, lumaflags);

			if (!_stricmp (mod->name, "progs/player.mdl"))
			{
				cmap = Mod_LoadPlayerColormap (name, mod, hdr, texels);
				mod->flags |= EF_PLAYER;
			}
			else cmap = NULL;

			for (s = 0; s < 4; s++)
			{
				hdr->skins[i].lumaimage[s] = luma;
				hdr->skins[i].teximage[s] = tex;
				hdr->skins[i].cmapimage[s] = cmap;
			}

			pskintype = (daliasskintype_t *) ((byte *) (pskintype + 1) + (hdr->skinwidth * hdr->skinheight));
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
				Mod_FloodFillSkin (skin, hdr->skinwidth, hdr->skinheight);

				_snprintf (name, 32, "%s_%i_%i", mod->name, i, j);

				texels = (byte *) (pskintype);

				int texflags = IMAGE_MIPMAP | IMAGE_ALIAS;
				int lumaflags = IMAGE_MIPMAP | IMAGE_ALIAS | IMAGE_LUMA;

				// default paths are good for these
				tex = D3DTexture_Load (name, hdr->skinwidth, hdr->skinheight, texels, texflags);
				luma = D3DTexture_Load (name, hdr->skinwidth, hdr->skinheight, texels, lumaflags);

				if (!_stricmp (mod->name, "progs/player.mdl"))
				{
					cmap = Mod_LoadPlayerColormap (name, mod, hdr, texels);
					mod->flags |= EF_PLAYER;
				}
				else cmap = NULL;

				// this tries to catch models with > 4 group skins
				hdr->skins[i].teximage[j & 3] = tex;
				hdr->skins[i].lumaimage[j & 3] = luma;
				hdr->skins[i].cmapimage[j & 3] = cmap;

				pskintype = (daliasskintype_t *) ((byte *) (pskintype) + (hdr->skinwidth * hdr->skinheight));
			}

			k = j;

			// fill in any skins that weren't loaded
			for (; j < 4; j++)
			{
				hdr->skins[i].teximage[j & 3] = hdr->skins[i].teximage[j - k];
				hdr->skins[i].lumaimage[j & 3] = hdr->skins[i].lumaimage[j - k];
				hdr->skins[i].cmapimage[j & 3] = hdr->skins[i].cmapimage[j - k];
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
	mdl_t *pinmodel = (mdl_t *) buffer;

	// look for an IQM to replace it
	if (Mod_FindIQMModel (mod))
	{
		// copy across the flags from the model it replaced
		mod->flags = pinmodel->flags;
		return;
	}

	if (pinmodel->version != ALIAS_VERSION)
		Host_Error ("%s has wrong version number (%i should be %i)", mod->name, pinmodel->version, ALIAS_VERSION);

	// alloc the header in the cache
	aliashdr_t *hdr = (aliashdr_t *) MainCache->Alloc (sizeof (aliashdr_t));

	mod->flags = pinmodel->flags;
	mod->type = mod_alias;

	// darkplaces extends model flags so here we must clear the extra ones so that we can safely add our own
	// (this is only relevant if an MDL was made with extended DP flags)
	mod->flags &= 255;

	// even if we alloced from the cache we still fill it in
	// endian-adjust and copy the data, starting with the alias model header
	hdr->boundingradius = pinmodel->boundingradius;
	hdr->numskins = pinmodel->numskins;
	hdr->skinwidth = pinmodel->skinwidth;
	hdr->skinheight = pinmodel->skinheight;
	hdr->vertsperframe = pinmodel->numverts;
	hdr->numtris = pinmodel->numtris;
	hdr->numframes = pinmodel->numframes;

	// validate the setup
	// Sys_Error seems a little harsh here...
	if (hdr->numframes < 1) Host_Error ("Mod_LoadAliasModel: Model %s has invalid # of frames: %d\n", mod->name, hdr->numframes);
	if (hdr->numtris <= 0) Host_Error ("model %s has no triangles", mod->name);
	if (hdr->vertsperframe <= 0) Host_Error ("model %s has no vertices", mod->name);

	hdr->size = pinmodel->size * ALIAS_BASE_SIZE_RATIO;
	mod->synctype = (synctype_t) pinmodel->synctype;
	mod->numframes = hdr->numframes;

	for (int i = 0; i < 3; i++)
	{
		hdr->scale[i] = pinmodel->scale[i];
		hdr->scale_origin[i] = pinmodel->scale_origin[i];
	}

	// load the skins
	daliasskintype_t *pskintype = (daliasskintype_t *) &pinmodel[1];
	pskintype = (daliasskintype_t *) Mod_LoadAllSkins (mod, hdr, pskintype);

	// load base s and t vertices
	stvert_t *pinstverts = (stvert_t *) pskintype;

	// load triangle lists
	dtriangle_t *pintriangles = (dtriangle_t *) &pinstverts[hdr->vertsperframe];

	// load the frames
	daliasframetype_t *pframetype = (daliasframetype_t *) &pintriangles[hdr->numtris];
	hdr->frames = (maliasframedesc_t *) MainCache->Alloc (hdr->numframes * sizeof (maliasframedesc_t));

	hdr->nummeshframes = 0;

	// because we don't know how many frames we need in advance we take a copy to the scratch buffer initially
	// the size of the scratch buffer is compatible with the max number of frames allowed by protocol 666
	hdr->vertexes = (drawvertx_t **) scratchbuf;

	for (int i = 0; i < hdr->numframes; i++)
	{
		aliasframetype_t frametype = (aliasframetype_t) pframetype->type;

		if (frametype == ALIAS_SINGLE)
		{
			daliasframe_t *frame = (daliasframe_t *) (pframetype + 1);
			pframetype = Mod_LoadAliasFrame (hdr, frame, &hdr->frames[i]);
		}
		else
		{
			daliasgroup_t *group = (daliasgroup_t *) (pframetype + 1);
			pframetype = Mod_LoadAliasGroup (hdr, group, &hdr->frames[i]);
		}
	}

	// copy framepointers from the scratch buffer to the final cached copy
	drawvertx_t **hdrvertexes = (drawvertx_t **) MainCache->Alloc (hdr->nummeshframes * sizeof (drawvertx_t *));

	for (int i = 0; i < hdr->nummeshframes; i++)
		hdrvertexes[i] = hdr->vertexes[i];

	hdr->vertexes = hdrvertexes;

	Mod_LoadAliasBBoxes (mod, hdr);

	// build the draw lists
	D3DAlias_MakeAliasMesh (mod->name, hdr, pinstverts, pintriangles);

	// set the final header
	mod->aliashdr = hdr;

	// cheaper to just always draw the model
	if (hdr->nummesh < 6 || hdr->numindexes < 36)
		mod->flags |= EF_NOOCCLUDE;

	// muzzleflash colour flags and other hard-coded crapness
	if (!strncmp (&mod->name[6], "wizard", 6)) mod->flags |= EF_WIZARDFLASH;
	if (!strncmp (&mod->name[6], "shalrath", 6)) mod->flags |= EF_SHALRATHFLASH;
	if (!strncmp (&mod->name[6], "shambler", 6)) mod->flags |= EF_SHAMBLERFLASH;

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


