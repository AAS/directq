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

// vid.h -- video driver defs

#define VID_CBITS	6
#define VID_GRADES	(1 << VID_CBITS)

// a pixel can be one, two, or four bytes
typedef byte pixel_t;


typedef struct sizedef_s
{
	unsigned		width;
	unsigned		height;
} sizedef_t;

typedef struct viddef_s
{
	pixel_t			*colormap;		// 256 * VID_GRADES size
	bool			fullbright[256];	// true if a colour at this index is fullbright
	unsigned		rowbytes;	// may be > width if displayed in a window
	float			aspect;		// width / height -- < 0 is taller than wide

	sizedef_t		consize;	// further scaled by scr_conscale
	sizedef_t		sbarsize;	// further scaled by scr_sbarscale
	sizedef_t		menusize;	// further scaled by scr_menuscale
	sizedef_t		ref3dsize;	// size of the 3D refresh window
	sizedef_t		*currsize;	// current size we're using in the 2D refresh

	// number of lines used for the status bar including all scaling factors/etc
	// (so that we don't need to recalc it every time we need it, and so that we can always get it any time we need it)
	int				sbar_lines;

	int				recalc_refdef;	// if true, recalc vid-based stuff
} viddef_t;

extern	viddef_t	vid;				// global video state

// 2D scaling size and offsets
void D3DDraw_SetSize (sizedef_t *size);
void D3DDraw_SetOfs (int x, int y);

void	VID_Shutdown (void);
// Called at shutdown

int VID_SetMode (int modenum, unsigned char *palette);
// sets the mode; only used by the Quake engine for resetting to mode 0 (the
// base mode) on memory allocation failures
