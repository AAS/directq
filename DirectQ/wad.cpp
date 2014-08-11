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

	wad_numlumps = header->numlumps;
	infotableofs = header->infotableofs;
	wad_lumps = (lumpinfo_t *) (wad_base + infotableofs);

	for (i = 0, lump_p = wad_lumps; i < wad_numlumps; i++, lump_p++)
	{
		lump_p->filepos = lump_p->filepos;
		lump_p->size = lump_p->size;
		W_CleanupName (lump_p->name, lump_p->name);
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


miptex_t *W_ValidateHLWAD (HANDLE fh, char *texname)
{
	wadinfo_t header;
	static miptex_t texmip;

	// get the starting point of the wad so that we can get the correct lump position
	int wadstart = SetFilePointer (fh, 0, NULL, FILE_CURRENT);

	if (COM_FReadFile (fh, &header, sizeof (wadinfo_t)) != sizeof (wadinfo_t)) return NULL;
	if (memcmp (header.identification, "WAD3", 4)) return NULL;

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


// palettes need to be reloaded on every game change so do it here
// this should really move to vidnt.cpp
void D3D_MakeQuakePalettes (byte *palette);
void PaletteFromColormap (byte *pal, byte *map);

cvar_t idgamma_gamma ("idgamma_gamma", "1", CVAR_ARCHIVE | CVAR_RESTART);
cvar_t idgamma_intensity ("idgamma_intensity", "1", CVAR_ARCHIVE | CVAR_RESTART);
cvar_t idgamma_saturation ("idgamma_saturation", "1", CVAR_ARCHIVE | CVAR_RESTART);
cvar_t idgamma_modifyfullbrights ("idgamma_modifyfullbrights", "1", CVAR_ARCHIVE | CVAR_RESTART);

void W_ApplyIDGamma (byte *palette)
{
	// no change
	if (idgamma_gamma.value == 1.0f && idgamma_intensity.value == 1.0f) return;

	// trap division by zero
	if (idgamma_gamma.value < 0.001f) return;
	if (idgamma_intensity.value < 0.001f) return;

	// http://vbutuzov.selfip.com/Games/QUAKE/ID1/IDGAMMA.BAS
	float ax = (255.0f * 3.0f) / (2.0f + idgamma_intensity.value);

	float M = (255.0f - ax) / (255.0f - ax / (idgamma_intensity.value));
	float Bx = 255.0f * (1.0f - M);

	float offset = (255.0f - (255.0f * idgamma_gamma.value)) / (3 * idgamma_intensity.value);

	for (int i = 0; i < 256; i++, palette += 3)
	{
		float r = palette[0];
		float g = palette[1];
		float b = palette[2];

		if ((i < 224 || idgamma_modifyfullbrights.value) && i != 255)
		{
			r += offset;
			g += offset;
			b += offset;

			float Max = r;

			if (g > Max) Max = g;
			if (b > Max) Max = b;

			float Mult = idgamma_intensity.value;

			Max *= idgamma_intensity.value;

			// trap division by zero
			if (Max < 0.001) continue;

			if (Max > ax && idgamma_saturation.integer < 3)
			{
				if (idgamma_saturation.integer == 2)
				{
					if (Max > 255)
					{
						Mult = Mult * (255.0f + Max + Max) / (Max + Max + Max);
					}
				}
				else
				{
					float Avg1 = (r + g + b) / 3.0f;
					float Avg2 = idgamma_intensity.value * Avg1;

					if (Avg2 > ax && Avg1 > 0)
					{
						Avg2 = M * (Avg1 / idgamma_intensity.value) + Bx;
						Mult = Avg2 / Avg1;
					}
				}
			}

			// correct integer rounding
			r = (r * Mult) + 0.5f;
			g = (g * Mult) + 0.5f;
			b = (b * Mult) + 0.5f;
		}

		palette[0] = BYTE_CLAMP (r);
		palette[1] = BYTE_CLAMP (g);
		palette[2] = BYTE_CLAMP (b);
	}
}


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
		W_ApplyIDGamma (palette);
		D3D_MakeQuakePalettes (palette);

		return true;
	}

	// failed to load either
	return false;
}

