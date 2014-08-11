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

// draw.c -- this is the only file outside the refresh that touches the
// vid buffer

#include "quakedef.h"
#include "d3d_quake.h"
#include "d3d_hlsl.h"
#include <vector>
#include <algorithm>

#define	D3DQUAKE_VERSION		0.01

cvar_t		gl_nobind ("gl_nobind", "0");
cvar_t		gl_conscale ("gl_conscale", "0", CVAR_ARCHIVE);

byte		*draw_chars;				// 8*8 graphic characters
qpic_t		*draw_disc;
qpic_t		*draw_backtile;

int			translate_texture;

LPDIRECT3DTEXTURE9 char_texture = NULL;
LPDIRECT3DTEXTURE9 R_PaletteTexture = NULL;
LPDIRECT3DTEXTURE9 crosshairtexture = NULL;

// faster lookup for char and palette coords
typedef struct quadcood_s
{
	float s;
	float t;
	float smax;
	float tmax;
} quadcoord_t;

quadcoord_t quadcoords[256];

typedef struct
{
	LPDIRECT3DTEXTURE9 tex;
	float	sl, tl, sh, th;
} glpic_t;


qpic_t *conback;

int		texels;


typedef struct vert_2d_s
{
	float xyz[3];
	float st[2];
} vert_2d_t;


//=============================================================================
/* Support Routines */

typedef struct cachepic_s
{
	char		name[MAX_QPATH];
	qpic_t		pic;
	byte		padding[32];	// for appended glpic

	struct cachepic_s *next;
} cachepic_t;

#define	MAX_CACHED_PICS		256

cachepic_t *menu_cachepics = NULL;

byte		menuplyr_pixels[4096];

int		pic_texels;
int		pic_count;

// failsafe pic for when a load from the wad fails
// (prevent crashes here during mod dev cycles)
byte *failsafedata = NULL;
qpic_t *draw_failsafe = NULL;


qpic_t *Draw_PicFromWad (char *name)
{
	qpic_t	*p;
	glpic_t	*gl;

	p = (qpic_t *) W_GetLumpName (name);

	if (!p) p = draw_failsafe;

	if (p->width < 1 || p->height < 1)
	{
		// this occasionally gets hosed, dunno why yet...
		// seems preferable to crashing
		Con_Printf ("Draw_PicFromWad: p->width < 1 || p->height < 1 for %s\n(I fucked up - sorry)\n", name);
		p = draw_failsafe;

#ifdef _DEBUG
		// prevent an exception in release builds
		DebugBreak ();
#endif
	}

	gl = (glpic_t *) p->data;

	gl->tex = D3D_LoadTexture (name, p->width, p->height, p->data, false, true);
	gl->sl = 0;
	gl->sh = 1;
	gl->tl = 0;
	gl->th = 1;

	return p;
}


/*
================
Draw_CachePic
================
*/
qpic_t *Draw_CachePic (char *path)
{
	cachepic_t	*pic;
	cachepic_t	*freepic;
	int			i;
	qpic_t		*dat;
	glpic_t		*gl;

	for (pic = menu_cachepics, freepic = NULL; pic; pic = pic->next)
	{
		if (!pic->name[0]) freepic = pic;
		if (!stricmp (path, pic->name)) return &pic->pic;
	}

	// if a free slot is available we use that, otherwise we alloc a new one
	if (freepic)
		pic = freepic;
	else
	{
		// add a new pic
		pic = (cachepic_t *) malloc (sizeof (cachepic_t));
		pic->next = menu_cachepics;
		menu_cachepics = pic;
	}

	strcpy (pic->name, path);

	// load the pic from disk
	dat = (qpic_t *) COM_LoadTempFile (path);

	if (!dat)
	{
		Con_Printf ("Draw_CachePic: failed to load %s", path);
		dat = draw_failsafe;
	}
	else
	{
		// don't swap draw_failsafe!!!
		SwapPic (dat);
	}

	if (dat->width < 1 || dat->height < 1)
	{
		// this occasionally gets hosed, dunno why yet...
		// seems preferable to crashing
		Con_Printf ("Draw_CachePic: dat->width < 1 || dat->height < 1 for %s\n(I fucked up - sorry)\n", path);
		dat = draw_failsafe;

#ifdef _DEBUG
		// prevent an exception in release builds
		DebugBreak ();
#endif
	}

	// HACK HACK HACK --- we need to keep the bytes for
	// the translatable player picture just for the menu
	// configuration dialog
	if (!strcmp (path, "gfx/menuplyr.lmp"))
		memcpy (menuplyr_pixels, dat->data, dat->width * dat->height);

	pic->pic.width = dat->width;
	pic->pic.height = dat->height;

	gl = (glpic_t *) pic->pic.data;

	gl->tex = D3D_LoadTexture (path, dat->width, dat->height, dat->data, false, true);
	gl->sl = 0;
	gl->sh = 1;
	gl->tl = 0;
	gl->th = 1;

	return &pic->pic;
}


void Draw_SpaceOutCharSet (byte *data, int w, int h)
{
	byte *buf = data;
	byte *newchars;
	int i, j, c;

	// allocate memory for a new charset
	newchars = (byte *) Heap_QMalloc (w * h * 4);

	// clear to all alpha
	memset (newchars, 0, w * h * 4);

	// copy them in with better spacing
	for (i = 0, j = 0; i < w * h; )
	{
		int small_stride = w >> 4;
		int big_stride = w >> 3;
		int imodtest;
		int jstride;

		imodtest = w * small_stride;
		jstride = imodtest * 2;

		// jump down a row
		// 1024 = 16 (num across) * 8 (width) * 8 (height)
		// j = double that
		if (i && !(i % imodtest)) j += jstride;

		for (c = 0; c < small_stride; c++)
		{
			newchars[j + c] = buf[i + c];
		}

		i += small_stride;
		j += big_stride;
	}

	// original individual char size
	c = w / 16;

	// lock the new size
	w *= 2;
	h *= 2;

	// now this is evil.
	// some of the chars are meant as consecutive dividers and the like, so we don;t want them spaced.
	// because we're spacing by the width of a char, we just copy the relevant pixels in
	for (i = (c * 2); i < (c * 3); i++)
	{
		for (j = (c * 28); j < (c * 29); j++)
		{
			newchars[i * w + j - c] = newchars[i * w + j];
			newchars[i * w + j + c] = newchars[i * w + j];
		}
	}

	for (i = (c * 18); i < (c * 19); i++)
	{
		for (j = (c * 28); j < (c * 29); j++)
		{
			newchars[i * w + j - c] = newchars[i * w + j];
			newchars[i * w + j + c] = newchars[i * w + j];
		}
	}

	for (i = (c * 16); i < (c * 17); i++)
	{
		for (j = (c * 2); j < (c * 3); j++)
		{
			newchars[i * w + j - c] = newchars[i * w + j];
			newchars[i * w + j + c] = newchars[i * w + j];
		}
	}

	for (i = (c * 16); i < (c * 17); i++)
	{
		newchars[i * w + (c * 6) - 1] = newchars[i * w + (c * 3) - 1];
		newchars[i * w + (c * 7)] = newchars[i * w + (c * 2)];
	}

	// set proper alpha colour
	for (i = 0; i < (256 * 256); i++)
		if (newchars[i] == 0)
			newchars[i] = 255;

	// now turn them into textures
	char_texture = D3D_LoadTexture ("charset", 256, 256, newchars, false, true);

	// free the working memory we used
	Heap_QFree (newchars);
}


/*
===============
Draw_Init
===============
*/
extern cvar_t gl_texturedir;
int D3D_PowerOf2Size (int size);

void Draw_Init (void)
{
	qpic_t	*cb;
	byte	*dest;
	int		x, y;
	char	ver[40];
	glpic_t	*gl;
	int		start;
	byte	*ncdata;

	// free cache pics
	for (cachepic_t *pic = menu_cachepics; pic; pic = pic->next)
		pic->name[0] = 0;

	// setup failsafe pic
	// free previous allocation (on game change)
	if (failsafedata) Heap_QFree (failsafedata);

	// this is a qpic_t that's used in the event of any qpic_t failing to load!!!
	// we alloc enough memory for the glpic_t that draw_failsafe->data is casted to.
	// this crazy-assed shit prevents an "ambiguous call to overloaded function" error.
	// add 1 cos of integer round down.  do it this way in case we ever change the glpic_t struct
	int failsafedatasize = D3D_PowerOf2Size ((int) sqrt ((float) (sizeof (glpic_t))) + 1);

	// persist in memory
	failsafedata = (byte *) Heap_QMalloc (sizeof (int) * 2 + (failsafedatasize * failsafedatasize));
	draw_failsafe = (qpic_t *) failsafedata;
	draw_failsafe->height = failsafedatasize;
	draw_failsafe->width = failsafedatasize;

	// lookup table for charset and palette texcoords
	for (int i = 0; i < 256; i++)
	{
		int row, col;
		float frow, fcol, size;

		// get position in the grid
		row = i >> 4;
		col = i & 15;

		// base texcoords relative to grid
		frow = row * 0.0625;
		fcol = col * 0.0625;

		// half size because we space out the chars to prevent bilerp overlap
		size = 0.03125;

		quadcoords[i].s = fcol;
		quadcoords[i].t = frow;
		quadcoords[i].smax = fcol + size;
		quadcoords[i].tmax = frow + size;
	}

	draw_chars = (byte *) W_GetLumpName ("conchars");

	if (!draw_chars)
	{
		Sys_Error ("Draw_Init: failed to load draw_chars");
		return;
	}

	Draw_SpaceOutCharSet (draw_chars, 128, 128);

	// load the console background
	conback = Draw_CachePic ("gfx/conback.lmp");

	// set it's correct dimensions
	conback->width = vid.width;
	conback->height = vid.height;

	// get the other pics we need
	// draw_disc is also used on the sbar so we need to retain it
	draw_disc = Draw_PicFromWad ("disc");
	draw_backtile = Draw_PicFromWad ("backtile");
}



/*
================
Draw_Character

Draws one 8*8 graphics character with 0 being transparent.
It can be clipped to the top of the screen to allow the console to be
smoothly scrolled off.
================
*/
void Draw_Character (int x, int y, int num)
{
	num &= 255;

	// don't draw spaces
	if (num == 32) return;

	// check for offscreen
	if (y <= -8) return;
	if (y >= vid.height) return;
	if (x <= -8) return;
	if (x >= vid.width) return;

	d3d_Flat2DFX.SetTexture (char_texture);

	vert_2d_t verts[] =
	{
		{x, y, 0, quadcoords[num].s, quadcoords[num].t},
		{x + 8, y, 0, quadcoords[num].smax, quadcoords[num].t},
		{x + 8, y + 8, 0, quadcoords[num].smax, quadcoords[num].tmax},
		{x, y + 8, 0, quadcoords[num].s, quadcoords[num].tmax}
	};

	d3d_Flat2DFX.Draw (D3DPT_TRIANGLEFAN, 2, verts, sizeof (vert_2d_t));
}


void Draw_InvertCharacter (int x, int y, int num)
{
	num &= 255;

	// don't draw spaces
	if (num == 32) return;

	// check for offscreen
	if (y <= -8) return;
	if (y >= vid.height) return;
	if (x <= -8) return;
	if (x >= vid.width) return;

	d3d_Flat2DFX.SetTexture (char_texture);

	vert_2d_t verts[] =
	{
		{x, y, 0, quadcoords[num].s, quadcoords[num].tmax},
		{x + 8, y, 0, quadcoords[num].smax, quadcoords[num].tmax},
		{x + 8, y + 8, 0, quadcoords[num].smax, quadcoords[num].t},
		{x, y + 8, 0, quadcoords[num].s, quadcoords[num].t}
	};

	d3d_Flat2DFX.Draw (D3DPT_TRIANGLEFAN, 2, verts, sizeof (vert_2d_t));
}


void Draw_BackwardsCharacter (int x, int y, int num)
{
	num &= 255;

	// don't draw spaces
	if (num == 32) return;

	// check for offscreen
	if (y <= -8) return;
	if (y >= vid.height) return;
	if (x <= -8) return;
	if (x >= vid.width) return;

	d3d_Flat2DFX.SetTexture (char_texture);

	vert_2d_t verts[] =
	{
		{x, y, 0, quadcoords[num].smax, quadcoords[num].t},
		{x + 8, y, 0, quadcoords[num].s, quadcoords[num].t},
		{x + 8, y + 8, 0, quadcoords[num].s, quadcoords[num].tmax},
		{x, y + 8, 0, quadcoords[num].smax, quadcoords[num].tmax}
	};

	d3d_Flat2DFX.Draw (D3DPT_TRIANGLEFAN, 2, verts, sizeof (vert_2d_t));
}


void Draw_RotateCharacter (int x, int y, int num)
{
	num &= 255;

	// don't draw spaces
	if (num == 32) return;

	// check for offscreen
	if (y <= -8) return;
	if (y >= vid.height) return;
	if (x <= -8) return;
	if (x >= vid.width) return;

	d3d_Flat2DFX.SetTexture (char_texture);

	vert_2d_t verts[] =
	{
		{x, y, 0, quadcoords[num].s, quadcoords[num].tmax},
		{x + 8, y, 0, quadcoords[num].s, quadcoords[num].t},
		{x + 8, y + 8, 0, quadcoords[num].smax, quadcoords[num].t},
		{x, y + 8, 0, quadcoords[num].smax, quadcoords[num].tmax}
	};

	d3d_Flat2DFX.Draw (D3DPT_TRIANGLEFAN, 2, verts, sizeof (vert_2d_t));
}


void Draw_VScrollBar (int x, int y, int height, int pos, int scale)
{
	int i;
	float f;

	// take to next lowest multiple of 8
	int h = height & 0xfff8;

	// body
	for (i = y; i < (y + h); i += 8)
		Draw_RotateCharacter (x, i, 129);

	// fill in the last if necessary
	if (h < height) Draw_RotateCharacter (x, y + height - 8, 129);

	// nothing in the list
	if (scale < 2) return;

	// get the position
	f = (height - 8) * pos;
	f /= (float) (scale - 1);
	f += (y);

	// position indicator
	Draw_RotateCharacter (x, f, 131);
}


/*
================
Draw_String
================
*/
void Draw_String (int x, int y, char *str)
{
	while (*str)
	{
		Draw_Character (x, y, *str);
		str++;
		x += 8;
	}
}

/*
================
Draw_DebugChar

Draws a single character directly to the upper right corner of the screen.
This is for debugging lockups by drawing different chars in different parts
of the code.
================
*/
void Draw_DebugChar (char num)
{
}

/*
=============
Draw_AlphaPic
=============
*/
void Draw_AlphaPic (int x, int y, qpic_t *pic, float alpha)
{
	glpic_t *gl = (glpic_t *) pic->data;

	float AlphaColour[] = {1, 1, 1, D3D_TransformColourSpace (alpha)};

	d3d_Flat2DFX.SwitchToPass (2);
	d3d_Flat2DFX.SetColor4f (AlphaColour);

	d3d_Flat2DFX.SetTexture (gl->tex);

	vert_2d_t verts[] =
	{
		{x, y, 0, gl->sl, gl->tl},
		{x + pic->width, y, 0, gl->sh, gl->tl},
		{x + pic->width, y + pic->height, 0, gl->sh, gl->th},
		{x, y + pic->height, 0, gl->sl, gl->th}
	};

	d3d_Flat2DFX.Draw (D3DPT_TRIANGLEFAN, 2, verts, sizeof (vert_2d_t));
	d3d_Flat2DFX.SwitchToPass (0);
}


/*
=============
Draw_Pic
=============
*/
void Draw_Pic (int x, int y, qpic_t *pic)
{
	glpic_t *gl = (glpic_t *) pic->data;

	d3d_Flat2DFX.SetTexture (gl->tex);

	vert_2d_t verts[] =
	{
		{x, y, 0, gl->sl, gl->tl},
		{x + pic->width, y, 0, gl->sh, gl->tl},
		{x + pic->width, y + pic->height, 0, gl->sh, gl->th},
		{x, y + pic->height, 0, gl->sl, gl->th}
	};

	d3d_Flat2DFX.Draw (D3DPT_TRIANGLEFAN, 2, verts, sizeof (vert_2d_t));
}


void D3D_TranslateAlphaTexture (int r, int g, int b, LPDIRECT3DTEXTURE9 tex);

bool crosshair_recache = false;

void Draw_Crosshair (int x, int y, int size)
{
	// we don't know about these cvars
	extern cvar_t crosshair;
	extern cvar_t scr_crosshaircolor;

	// no custom crosshair
	if (crosshair.integer < 3) return;

	static int savedcolor = 666;

	// if the colour space changes we also need to rebuild the crosshair texture
	if (scr_crosshaircolor.integer != savedcolor || crosshair_recache)
	{
		// bound colour
		if (scr_crosshaircolor.integer < 0) scr_crosshaircolor.integer = 0;
		if (scr_crosshaircolor.integer > 13) scr_crosshaircolor.integer = 13;

		// set back
		Cvar_Set (&scr_crosshaircolor, scr_crosshaircolor.integer);

		// because we don't have a vertex type for texture * color in the 2D render, and we don't wanna
		// complicate things by creating one (more declarations, shaders, switching passes, etc) we just
		// translate the texture by the chosen colour if changes.  first we need to find what the correct
		// rgb components are... take full range (and handle backwards ranges)
		int cindex = scr_crosshaircolor.integer * 16 + (scr_crosshaircolor.integer < 8 ? 15 : 0);

		// bound
		if (cindex < 0) cindex = 0;
		if (cindex > 255) cindex = 255;

		// translate
		Con_DPrintf ("Translating crosshair to %i [%i] [%i] [%i]\n", cindex, ((byte *) &d_8to24table[cindex])[2], ((byte *) &d_8to24table[cindex])[1], ((byte *) &d_8to24table[cindex])[0]);

		D3D_TranslateAlphaTexture
		(
			((byte *) &d_8to24table[cindex])[2],
			((byte *) &d_8to24table[cindex])[1],
			((byte *) &d_8to24table[cindex])[0],
			crosshairtexture
		);

		// store back
		savedcolor = scr_crosshaircolor.integer;
		crosshair_recache = false;
	}

	// 1 and 2 are the regular '+' symbols
	int xhairimage = crosshair.integer - 3;

	// 0 to 15 scale
	while (xhairimage > 15) xhairimage -= 16;

	// full texcoords for each value
	float xhairs[] = {0, 0.25, 0.5, 0.75, 0, 0.25, 0.5, 0.75, 0, 0.25, 0.5, 0.75, 0, 0.25, 0.5, 0.75};
	float xhairt[] = {0, 0, 0, 0, 0.25, 0.25, 0.25, 0.25, 0.5, 0.5, 0.5, 0.5, 0.75, 0.75, 0.75, 0.75};

	// actual coords we'll use
	float s = xhairimage[xhairs];
	float t = xhairimage[xhairt];

	// bind it
	d3d_Flat2DFX.SetTexture (crosshairtexture);

	vert_2d_t verts[] =
	{
		{x, y, 0, s, t},
		{x + size, y, 0, s + 0.25, t},
		{x + size, y + size, 0, s + 0.25, t + 0.25},
		{x, y + size, 0, s, t + 0.25}
	};

	d3d_Flat2DFX.Draw (D3DPT_TRIANGLEFAN, 2, verts, sizeof (vert_2d_t));
}


void Draw_HalfPic (int x, int y, qpic_t *pic)
{
	glpic_t *gl = (glpic_t *) pic->data;

	d3d_Flat2DFX.SetTexture (gl->tex);

	vert_2d_t verts[] =
	{
		{x, y, 0, gl->sl, gl->tl},
		{x + pic->width / 2, y, 0, gl->sh, gl->tl},
		{x + pic->width / 2, y + pic->height / 2, 0, gl->sh, gl->th},
		{x, y + pic->height / 2, 0, gl->sl, gl->th}
	};

	d3d_Flat2DFX.Draw (D3DPT_TRIANGLEFAN, 2, verts, sizeof (vert_2d_t));
}


void Draw_TextBox (int x, int y, int width, int height)
{
	// cache the frequently used qpic_t's for faster access
	qpic_t *box_ml = Draw_CachePic ("gfx/box_ml.lmp");
	qpic_t *box_mr = Draw_CachePic ("gfx/box_mr.lmp");
	qpic_t *box_tm = Draw_CachePic ("gfx/box_tm.lmp");
	qpic_t *box_bm = Draw_CachePic ("gfx/box_bm.lmp");
	qpic_t *box_mm2 = Draw_CachePic ("gfx/box_mm2.lmp");

	// corners
	Draw_Pic (x, y, Draw_CachePic ("gfx/box_tl.lmp"));
	Draw_Pic (x + width + 8, y, Draw_CachePic ("gfx/box_tr.lmp"));
	Draw_Pic (x, y + height + 8, Draw_CachePic ("gfx/box_bl.lmp"));
	Draw_Pic (x + width + 8, y + height + 8, Draw_CachePic ("gfx/box_br.lmp"));

	// left and right sides
	for (int i = 8; i < height; i += 8)
	{
		Draw_Pic (x, y + i, box_ml);
		Draw_Pic (x + width + 8, y + i, box_mr);
	}

	// top and bottom sides
	for (int i = 16; i < width; i += 8)
	{
		Draw_Pic (x + i - 8, y, box_tm);
		Draw_Pic (x + i - 8, y + height + 8, box_bm);
	}

	// finish off
	Draw_Pic (x + width - 8, y, box_tm);
	Draw_Pic (x + width - 8, y + height + 8, box_bm);
	Draw_Pic (x, y + height, box_ml);
	Draw_Pic (x + width + 8, y + height, box_mr);

	// fill (ugly as a box of frogs...)
	for (int i = 16; i < width; i += 8)
	{
		for (int j = 8; j < height; j += 8)
		{
			if (i == 16)
				Draw_Pic (x + width - 8, y + j, box_mm2);

			Draw_Pic (x + i - 8, y + j, box_mm2);
		}

		Draw_Pic (x + i - 8, y + height, box_mm2);
	}

	Draw_Pic (x + width - 8, y + height, box_mm2);
}


char cached_name[256] = {0};
LPDIRECT3DTEXTURE9 d3d_MapshotTexture = NULL;

void Draw_InvalidateMapshot (void)
{
	// just invalidate the cached name to force a reload in case the shot changes
	cached_name[0] = 0;
}


void Draw_MapshotTexture (LPDIRECT3DTEXTURE9 mstex, int x, int y)
{
	Draw_TextBox (x - 8, y - 8, 128, 128);

	d3d_Flat2DFX.SetTexture (mstex);

	vert_2d_t verts[] =
	{
		{x, y, 0, 0, 0},
		{x + 128, y, 0, 1, 0},
		{x + 128, y + 128, 0, 1, 1},
		{x, y + 128, 0, 0, 1}
	};

	d3d_Flat2DFX.Draw (D3DPT_TRIANGLEFAN, 2, verts, sizeof (vert_2d_t));
}


void Draw_Mapshot (char *name, int x, int y)
{
	if (!name)
	{
		// no name supplied so display the console image instead
		glpic_t *gl = (glpic_t *) conback->data;

		// note - sl/tl/sh/th for the conback are correct 0 and 1 values
		Draw_MapshotTexture (gl->tex, x, y);
		return;
	}

	if (stricmp (name, cached_name))
	{
		// save to cached name
		strncpy (cached_name, name, 255);

		// texture has changed, release the existing one
		SAFE_RELEASE (d3d_MapshotTexture);

		// attempt to load a new one
		// note - D3DX won't allow us to save TGAs (BASTARDS!), but we'll still attempt to load them
		if (!D3D_LoadExternalTexture (&d3d_MapshotTexture, cached_name, 0))
		{
			// ensure release
			SAFE_RELEASE (d3d_MapshotTexture);

			// if we didn't load it, call recursively to display the console
			Draw_Mapshot (NULL, x, y);

			// done
			return;
		}
	}

	// ensure valid
	if (!d3d_MapshotTexture)
	{
		// if we didn't load it, call recursively to display the console
		Draw_Mapshot (NULL, x, y);

		// return
		return;
	}

	// draw it
	Draw_MapshotTexture (d3d_MapshotTexture, x, y);
}


/*
=============
Draw_PicTranslate

Only used for the player color selection menu
=============
*/
void Draw_PicTranslate (int x, int y, qpic_t *pic, byte *translation)
{
	int				v, u, c;
	unsigned		*dest;
	byte			*src;
	int				p;

	c = pic->width * pic->height;

	// lock the texture rectangle for updating
	D3DLOCKED_RECT LockRect;

	((glpic_t *) (pic->data))->tex->LockRect (0, &LockRect, NULL, 0);

	dest = (unsigned *) LockRect.pBits;

	for (v = 0; v < 64; v++, dest += 64)
	{
		src = &menuplyr_pixels[((v * pic->height) >> 6) * pic->width];

		for (u = 0; u < 64; u++)
		{
			p = src[(u * pic->width) >> 6];

			if (p == 255)
				dest[u] = p;
			else
				dest[u] =  d_8to24table[translation[p]];
		}
	}

	// unlock
	((glpic_t *) (pic->data))->tex->UnlockRect (0);

	// draw it normally
	Draw_Pic (x, y, pic);
}


/*
================
Draw_ConsoleBackground

================
*/
void Draw_ConsoleBackground (int lines)
{
	int y = (vid.height * 3) >> 2;

	// ensure these are always valid
	conback->width = vid.width;
	conback->height = vid.height;

	if (lines <= y)
		Draw_AlphaPic (0, lines - vid.height, conback, (float) (1.2 * lines) / y);
	else Draw_Pic (0, lines - vid.height, conback);
}


/*
=============
Draw_TileClear

This repeats a 64*64 tile graphic to fill the screen around a sized down
refresh window.
=============
*/
void Draw_TileClear (int x, int y, int w, int h)
{
	glpic_t *gl = (glpic_t *) draw_backtile->data;

	d3d_Flat2DFX.SwitchToPass (3);
	d3d_Flat2DFX.SetTexture (gl->tex);

	vert_2d_t verts[] =
	{
		{x, y, 0, x / 64.0, y / 64.0},
		{x + w, y, 0, (x + w) / 64.0, y / 64.0},
		{x + w, y + h, 0, (x + w) / 64.0, (y + h) / 64.0},
		{x, y + h, 0, x / 64.0, (y + h) / 64.0},
	};

	d3d_Flat2DFX.Draw (D3DPT_TRIANGLEFAN, 2, verts, sizeof (vert_2d_t));
}


/*
=============
Draw_Fill

Fills a box of pixels with a single color
=============
*/
void Draw_Fill (int x, int y, int w, int h, int c, int alpha)
{
	byte *bgra = (byte *) &d_8to24table[c];

	float FillColour[4] =
	{
		D3D_TransformColourSpace (bgra[2] / 255.0f), 
		D3D_TransformColourSpace (bgra[1] / 255.0f), 
		D3D_TransformColourSpace (bgra[0] / 255.0f), 
		D3D_TransformColourSpace (alpha / 255.0f)
	};

	d3d_Flat2DFX.SwitchToPass (1);
	d3d_Flat2DFX.SetColor4f (FillColour);

	vert_2d_t verts[] =
	{
		{x, y, 0},
		{x + w, y, 0},
		{x + w, y + h, 0},
		{x, y + h, 0}
	};

	d3d_Flat2DFX.Draw (D3DPT_TRIANGLEFAN, 2, verts, sizeof (vert_2d_t));
	d3d_Flat2DFX.SwitchToPass (0);
}


//=============================================================================

/*
================
Draw_FadeScreen

================
*/
void Draw_FadeScreen (void)
{
	float FadeColour[4] = {0, 0, 0, r_sRGBgamma.integer ? 0.9f : 0.8f};

	d3d_Flat2DFX.SwitchToPass (1);
	d3d_Flat2DFX.SetColor4f (FadeColour);

	vert_2d_t verts[] =
	{
		{0, 0, 0},
		{vid.width, 0, 0},
		{vid.width, vid.height, 0},
		{0, vid.height, 0}
	};

	d3d_Flat2DFX.Draw (D3DPT_TRIANGLEFAN, 2, verts, sizeof (vert_2d_t));
	d3d_Flat2DFX.SwitchToPass (0);
}


//=============================================================================

/*
============
Draw_PolyBlend
============
*/
void Draw_PolyBlend (void)
{
	if (!gl_polyblend.value) return;
	if (!v_blend[3]) return;

	float BlendColor[4] =
	{
		D3D_TransformColourSpace (v_blend[0] / 255.0f),
		D3D_TransformColourSpace (v_blend[1] / 255.0f),
		D3D_TransformColourSpace (v_blend[2] / 255.0f),
		D3D_TransformColourSpace (v_blend[3] / 255.0f)
	};

	d3d_Flat2DFX.SwitchToPass (1);
	d3d_Flat2DFX.SetColor4f (BlendColor);

	vert_2d_t verts[] =
	{
		{0, 0, 0},
		{vid.width, 0, 0},
		{vid.width, vid.height, 0},
		{0, vid.height, 0}
	};

	d3d_Flat2DFX.Draw (D3DPT_TRIANGLEFAN, 2, verts, sizeof (vert_2d_t));
	d3d_Flat2DFX.SwitchToPass (0);
}


void D3D_Set2DShade (float shadecolor)
{
	if (shadecolor >= 0.99f)
	{
		// revert to pass 0
		d3d_Flat2DFX.SwitchToPass (0);
	}
	else
	{
		// transform
		shadecolor = D3D_TransformColourSpace (shadecolor);

		// set color
		float ShadeColor[4] = {shadecolor, shadecolor, shadecolor, shadecolor};

		// switch to pass 2
		d3d_Flat2DFX.SwitchToPass (2);
		d3d_Flat2DFX.SetColor4f (ShadeColor);
	}
}


// we'll store this out too in case we ever want to do anything with it
D3DVIEWPORT9 d3d_2DViewport;
void D3D_DrawUnderwaterWarp (void);

void D3D_Set2D (void)
{
	// switch to a fullscreen viewport for 2D drawing
	d3d_2DViewport.X = 0;
	d3d_2DViewport.Y = 0;
	d3d_2DViewport.Width = d3d_CurrentMode.Width;
	d3d_2DViewport.Height = d3d_CurrentMode.Height;
	d3d_2DViewport.MinZ = 0.0f;
	d3d_2DViewport.MaxZ = 1.0f;

	// because the underwater warp needs it's own beginscene/endscene pair, we need a beginscene here too...
	d3d_Device->BeginScene ();
	d3d_Device->SetViewport (&d3d_2DViewport);

	// world matrix
	d3d_WorldMatrixStack->LoadIdentity ();
	d3d_Device->SetTransform (D3DTS_WORLD, d3d_WorldMatrixStack->GetTop ());

	// disable depth testing and writing
	d3d_Device->SetRenderState (D3DRS_ZENABLE, D3DZB_FALSE);
	d3d_Device->SetRenderState (D3DRS_ZWRITEENABLE, FALSE);

	// no backface culling
	D3D_BackfaceCull (D3DCULL_NONE);

	// common for all 2D drawing, including the underwater warp
	d3d_Device->SetVertexDeclaration (d3d_V3ST2Declaration);

	// draw the underwater warp here - all the above state is common but it uses a different orthographic matrix
	D3D_DrawUnderwaterWarp ();

	// projection matrix
	D3DXMatrixIdentity (&d3d_OrthoMatrix);
	D3DXMatrixOrthoOffCenterRH (&d3d_OrthoMatrix, 0, vid.width, vid.height, 0, 0, 1);
	d3d_Device->SetTransform (D3DTS_PROJECTION, &d3d_OrthoMatrix);

	// enable alpha blending
	d3d_Device->SetRenderState (D3DRS_ALPHABLENDENABLE, TRUE);
	d3d_Device->SetRenderState (D3DRS_BLENDOP, D3DBLENDOP_ADD);
	d3d_Device->SetRenderState (D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	d3d_Device->SetRenderState (D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

	// note - texture clamping is done per element rather than globally as the Draw_TileClear requires wrapping
	// set up our shader
	d3d_Flat2DFX.BeginRender ();
	d3d_Flat2DFX.SetWPMatrix (&((*d3d_WorldMatrixStack->GetTop ()) * d3d_OrthoMatrix));

	// do the polyblend here for simplicity
	Draw_PolyBlend ();

	d3d_Flat2DFX.SwitchToPass (0);
}

