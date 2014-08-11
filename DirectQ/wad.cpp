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
// wad.c

#include "quakedef.h"
#include "d3d_model.h"
#include "d3d_quake.h"

int			wad_numlumps;
lumpinfo_t	*wad_lumps;
byte		*wad_base = NULL;

void SwapPic (qpic_t *pic);

/*
==================
W_CleanupName

Lowercases name and pads with spaces and a terminating 0 to the length of
lumpinfo_t->name.
Used so lumpname lookups can proceed rapidly by comparing 4 chars at a time
Space padding is so names can be printed nicely in tables.
Can safely be performed in place.
==================
*/
void W_CleanupName (char *in, char *out)
{
	int		i;
	int		c;

	for (i = 0; i < 16; i++)
	{
		c = in[i];

		if (!c)
			break;

		if (c >= 'A' && c <= 'Z')
			c += ('a' - 'A');

		out[i] = c;
	}

	for (; i < 16; i++)
		out[i] = 0;
}



/*
====================
W_LoadWadFile
====================
*/
bool W_LoadWadFile (char *filename)
{
	lumpinfo_t		*lump_p;
	wadinfo_t		*header;
	unsigned		i;
	int				infotableofs;

	if (wad_base) Zone_Free (wad_base);

	if (!(wad_base = COM_LoadFile (filename))) return false;

	header = (wadinfo_t *) wad_base;

	if (header->identification[0] != 'W' || header->identification[1] != 'A' ||
			header->identification[2] != 'D' || header->identification[3] != '2')
		Sys_Error ("Wad file %s doesn't have WAD2 id\n", filename);

	wad_numlumps = LittleLong (header->numlumps);
	infotableofs = LittleLong (header->infotableofs);
	wad_lumps = (lumpinfo_t *) (wad_base + infotableofs);

	for (i = 0, lump_p = wad_lumps; i < wad_numlumps; i++, lump_p++)
	{
		lump_p->filepos = LittleLong (lump_p->filepos);
		lump_p->size = LittleLong (lump_p->size);
		W_CleanupName (lump_p->name, lump_p->name);

		if (lump_p->type == TYP_QPIC)
		{
			qpic_t *pic = (qpic_t *) (wad_base + lump_p->filepos);
			SwapPic (pic);
		}
	}

	return true;
}


void W_DumpWADLumps (void)
{
	int i;
	lumpinfo_t *lump_p;

	for (i = 0, lump_p = wad_lumps; i < wad_numlumps; i++, lump_p++)
	{
		if (lump_p->type == TYP_QPIC)
		{
			qpic_t *pic = (qpic_t *) (wad_base + lump_p->filepos);
			SCR_WriteDataToTGA (va ("%s.tga", lump_p->name), pic->data, pic->width, pic->height, 8, 24);
		}
	}
}


/*
=============
W_GetLumpinfo
=============
*/
lumpinfo_t	*W_GetLumpinfo (char *name)
{
	int		i;
	lumpinfo_t	*lump_p;
	char	clean[16];

	W_CleanupName (name, clean);

	for (lump_p = wad_lumps, i = 0; i < wad_numlumps; i++, lump_p++)
	{
		if (!strcmp (clean, lump_p->name))
			return lump_p;
	}

	return NULL;
}

void *W_GetLumpName (char *name)
{
	lumpinfo_t	*lump;

	lump = W_GetLumpinfo (name);

	// didn't find it
	if (!lump) return NULL;

	return (void *) (wad_base + lump->filepos);
}

void *W_GetLumpNum (int num)
{
	lumpinfo_t	*lump;

	if (num < 0 || num > wad_numlumps)
		return NULL;

	lump = wad_lumps + num;

	return (void *) (wad_base + lump->filepos);
}

/*
=============================================================================

automatic byte swapping

=============================================================================
*/

void SwapPic (qpic_t *pic)
{
	pic->width = LittleLong (pic->width);
	pic->height = LittleLong (pic->height);
}


miptex_t *W_ValidateHLWAD (HANDLE fh, char *texname)
{
	wadinfo_t header;
	static miptex_t texmip;

	// get the starting point of the wad so that we can get the correct lump position
	int wadstart = SetFilePointer (fh, 0, NULL, FILE_CURRENT);

	if (COM_FReadFile (fh, &header, sizeof (wadinfo_t)) != sizeof (wadinfo_t)) return NULL;

	if (memcmp (header.identification, "WAD3", 4)) return NULL;

	header.numlumps = LittleLong (header.numlumps);
	header.infotableofs = LittleLong (header.infotableofs);

	// seek to the info table (this is offset from the beginning of the file, NOT the current position)
	// (do it this way so that it's PAK file friendly)
	SetFilePointer (fh, header.infotableofs - sizeof (wadinfo_t), NULL, FILE_CURRENT);

	for (int i = 0; i < header.numlumps; i++)
	{
		lumpinfo_t lump;

		// if something went wrong with the read we don't bother checking any more
		if (COM_FReadFile (fh, &lump, sizeof (lumpinfo_t)) != sizeof (lumpinfo_t)) return NULL;

		// lump with no name
		if (lump.name[0] == 0) continue;

		// invalid attributes
		if (lump.compression) continue;

		if (lump.type != 'C') continue;

		// clean the name/etc
		W_CleanupName (lump.name, lump.name);
		lump.filepos = LittleLong (lump.filepos);
		lump.size = LittleLong (lump.size);

		// check the name
		if (!stricmp (lump.name, texname))
		{
			// found it; read in the miptex (if anything goes wrong during this procedure we need to return NULL
			// as the pointer, infotable, etc are all out of whack)
			SetFilePointer (fh, wadstart + lump.filepos, NULL, FILE_BEGIN);

			if (COM_FReadFile (fh, &texmip, sizeof (miptex_t)) != sizeof (miptex_t)) return NULL;

			return &texmip;
		}
	}

	// found nothing
	return NULL;
}


miptex_t *W_LoadTextureFromHLWAD (char *wadname, char *texname, miptex_t **mipdata)
{
	if (!mipdata) return NULL;

	// hl WAD files contain an absolute path to the dev directory of whoever built it,
	// which isn't much use for loading for real, so jump backwards through the path
	// looking for one that does actually exist until we either find one or run out.
	for (int i = strlen (wadname); i >= 0; i--)
	{
		if (wadname[i] == '/' || wadname[i] == '\\')
		{
			HANDLE fh = INVALID_HANDLE_VALUE;

			COM_FOpenFile (&wadname[i + 1], &fh);

			if (fh != INVALID_HANDLE_VALUE)
			{
				// found one; now look for the texture in it
				miptex_t *texmip = W_ValidateHLWAD (fh, texname);

				if (!texmip)
				{
					// didn't find the texture
					COM_FCloseFile (&fh);
					continue;
				}

				// found it!!!  alloc a buffer to hold the new texture - this must be big enough for 4
				// miplevels plus the palette
				mipdata[0] = (miptex_t *) Zone_Alloc (sizeof (miptex_t) + ((texmip->width * texmip->height * 85) >> 6) + 770);
				memcpy (mipdata[0], texmip, sizeof (miptex_t));

				// now read the rest of the data
				COM_FReadFile (fh, (byte *) (mipdata[0] + 1), ((texmip->width * texmip->height * 85) >> 6) + 770);

				COM_FCloseFile (&fh);
				return mipdata[0];
			}
		}
	}

	return NULL;
}


// palettes need to be reloaded on every game change so do it here
// this should really move to vidnt.cpp
void D3D_MakeQuakePalettes (byte *palette);
void PaletteFromColormap (byte *pal, byte *map);

bool W_LoadPalette (void)
{
	// these need to be statics so that they can be freed OK
	static byte *palette = NULL;
	static byte *colormap = NULL;

	if (palette) Zone_Free (palette);
	if (colormap) Zone_Free (colormap);

	palette = (byte *) COM_LoadFile ("gfx/palette.lmp");
	colormap = (byte *) COM_LoadFile ("gfx/colormap.lmp");

	if (palette && colormap)
	{
		vid.colormap = colormap;
		PaletteFromColormap (palette, vid.colormap);
		D3D_MakeQuakePalettes (palette);

		return true;
	}

	// failed to load either
	return false;
}

