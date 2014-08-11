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
#include "d3d_model.h"
#include "d3d_quake.h"

PALETTEENTRY lumapal[256];

extern unsigned int lumatable[];

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
typedef struct quadcoord_s
{
	float s;
	float t;
	float smax;
	float tmax;
} quadcoord_t;

quadcoord_t quadcoords[256];

typedef struct
{
	struct image_s *tex;
	float	sl, tl, sh, th;
} glpic_t;


qpic_t *conback;

int		texels;


// 2d drawing
DWORD d3d_2DTextureColor = 0xffffffff;

float vert2dscale_x = 1;
float vert2dscale_y = 1;

typedef struct flatpolyvert_s
{
	// switched to rhw to avoid matrix transforms and state updates
	float x, y, z;
	float rhw;
	DWORD c;
	float s;
	float t;
} flatpolyvert_t;

flatpolyvert_t verts2d[4];

// needed because some drivers don't obey the DPUP stride value
typedef struct coloredpolyvert_s
{
	// switched to rhw to avoid matrix transforms and state updates
	float x, y, z;
	float rhw;
	DWORD c;
} coloredpolyvert_t;

coloredpolyvert_t verts2dcolored[4];

__inline void D3D_EmitFlatPolyVert (int vertnum, float x, float y, D3DCOLOR c, float s, float t)
{
	verts2d[vertnum].x = x * vert2dscale_x;
	verts2d[vertnum].y = y * vert2dscale_y;
	verts2d[vertnum].z = 0;
	verts2d[vertnum].rhw = 1;
	verts2d[vertnum].c = c;
	verts2d[vertnum].s = s;
	verts2d[vertnum].t = t;
}


__inline void D3D_EmitColoredPolyVert (int vertnum, float x, float y, D3DCOLOR c)
{
	verts2dcolored[vertnum].x = x * vert2dscale_x;
	verts2dcolored[vertnum].y = y * vert2dscale_y;
	verts2dcolored[vertnum].z = 0;
	verts2dcolored[vertnum].rhw = 1;
	verts2dcolored[vertnum].c = c;
}


void D3D_DrawFlatPoly (float x, float y, float w, float h, D3DCOLOR c, float s, float t, float maxs, float maxt)
{
	D3D_EmitFlatPolyVert (0, x, y, c, s, t);
	D3D_EmitFlatPolyVert (1, x + w, y, c, maxs, t);
	D3D_EmitFlatPolyVert (2, x + w, y + h, c, maxs, maxt);
	D3D_EmitFlatPolyVert (3, x, y + h, c, s, maxt);

	d3d_Device->DrawPrimitiveUP (D3DPT_TRIANGLEFAN, 2, verts2d, sizeof (flatpolyvert_t));
}


void D3D_DrawFlatPoly (float x, float y, float w, float h, float s, float t, float maxs, float maxt)
{
	D3D_EmitFlatPolyVert (0, x, y, d3d_2DTextureColor, s, t);
	D3D_EmitFlatPolyVert (1, x + w, y, d3d_2DTextureColor, maxs, t);
	D3D_EmitFlatPolyVert (2, x + w, y + h, d3d_2DTextureColor, maxs, maxt);
	D3D_EmitFlatPolyVert (3, x, y + h, d3d_2DTextureColor, s, maxt);

	d3d_Device->DrawPrimitiveUP (D3DPT_TRIANGLEFAN, 2, verts2d, sizeof (flatpolyvert_t));
}


void D3D_DrawColoredPoly (float x, float y, float w, float h, D3DCOLOR c)
{
	// untextured
	D3D_SetTextureColorMode (0, D3DTOP_DISABLE);
	D3D_SetFVF (D3DFVF_XYZRHW | D3DFVF_DIFFUSE);

	D3D_EmitColoredPolyVert (0, x, y, c);
	D3D_EmitColoredPolyVert (1, x + w, y, c);
	D3D_EmitColoredPolyVert (2, x + w, y + h, c);
	D3D_EmitColoredPolyVert (3, x, y + h, c);

	d3d_Device->DrawPrimitiveUP (D3DPT_TRIANGLEFAN, 2, verts2dcolored, sizeof (coloredpolyvert_t));

	// back to texturing
	D3D_SetTextureColorMode (0, D3DTOP_SELECTARG1);
	D3D_SetFVF (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
}


void D3D_PrepareFlatDraw (DWORD texfvf, DWORD addrmode, LPDIRECT3DTEXTURE9 stage0tex)
{
	D3D_SetFVF (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | texfvf);
	D3D_SetTextureAddressMode (addrmode);
	D3D_SetTexture (0, stage0tex);
}


//=============================================================================
/* Support Routines */

typedef struct cachepic_s
{
	char		name[MAX_QPATH];
	qpic_t		pic;
	byte		padding[32];	// for appended glpic

	struct cachepic_s *next;
} cachepic_t;

cachepic_t *menu_cachepics = NULL;

byte		*menuplyr_pixels = NULL;

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

	gl->tex = D3D_LoadTexture (name, p->width, p->height, p->data, 0);
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

	if (!path) return NULL;

	// these can't go into normal cached memory because of on-demand cache clearing
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
		// add a new pic to the game pool
		pic = (cachepic_t *) Pool_Game->Alloc (sizeof (cachepic_t));
		pic->next = menu_cachepics;
		menu_cachepics = pic;
	}

	strncpy (pic->name, path, 63);

	// load the pic from disk
	dat = (qpic_t *) COM_LoadTempFile (path);

	if (!dat)
	{
		// don't swap draw_failsafe!!!
		Con_Printf ("Draw_CachePic: failed to load %s", path);
		dat = draw_failsafe;
	}
	else
	{
		// only swap if it loaded
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
	{
		// this should only happen once
		menuplyr_pixels = (byte *) Pool_Game->Alloc (dat->width * dat->height);
		memcpy (menuplyr_pixels, dat->data, dat->width * dat->height);
	}

	pic->pic.width = dat->width;
	pic->pic.height = dat->height;

	gl = (glpic_t *) pic->pic.data;

	gl->tex = D3D_LoadTexture (path, dat->width, dat->height, dat->data, 0);
	gl->sl = 0;
	gl->sh = 1;
	gl->tl = 0;
	gl->th = 1;

	// don't pollute the temp pool with small fry
	Pool_Temp->Free ();

	return &pic->pic;
}


void Draw_SpaceOutCharSet (byte *data, int w, int h)
{
	byte *buf = data;
	byte *newchars;
	int i, j, c;

	// allocate memory for a new charset
	newchars = (byte *) Pool_Temp->Alloc (w * h * 4);

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
	D3D_UploadTexture (&char_texture, newchars, 256, 256, 0);
}


/*
===============
Draw_Init
===============
*/
extern cvar_t gl_texturedir;
int D3D_PowerOf2Size (int size);
int COM_BuildContentList (char ***FileList, char *basedir, char *filetype);

void Draw_Init (void)
{
	qpic_t	*cb;
	byte	*dest;
	int		x, y;
	char	ver[40];
	glpic_t	*gl;
	int		start;
	byte	*ncdata;

	// make palette for lumas
	for (int i = 0; i < 256; i++)
	{
		byte *bgra = (byte *) &lumatable[i];

		lumapal[i].peRed = bgra[2];
		lumapal[i].peGreen = bgra[1];
		lumapal[i].peBlue = bgra[0];
		lumapal[i].peFlags = bgra[3];
	}

	// free cache pics
	menu_cachepics = NULL;

	if (!failsafedata)
	{
		// this is a qpic_t that's used in the event of any qpic_t failing to load!!!
		// we alloc enough memory for the glpic_t that draw_failsafe->data is casted to.
		// this crazy-assed shit prevents an "ambiguous call to overloaded function" error.
		// add 1 cos of integer round down.  do it this way in case we ever change the glpic_t struct
		int failsafedatasize = D3D_PowerOf2Size ((int) sqrt ((float) (sizeof (glpic_t))) + 1);

		// persist in memory
		failsafedata = (byte *) Pool_Permanent->Alloc (sizeof (int) * 2 + (failsafedatasize * failsafedatasize));
		draw_failsafe = (qpic_t *) failsafedata;
		draw_failsafe->height = failsafedatasize;
		draw_failsafe->width = failsafedatasize;
	}

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
	Draw_SpaceOutCharSet (draw_chars, 128, 128);

	conback = Draw_CachePic ("gfx/conback.lmp");

	// get the other pics we need
	// draw_disc is also used on the sbar so we need to retain it
	draw_disc = Draw_PicFromWad ("disc");
	draw_backtile = Draw_PicFromWad ("backtile");
}


__inline int Draw_PrepareCharacter (int x, int y, int num)
{
	num &= 255;

	// don't draw spaces
	if (num == 32) return 0;

	// check for offscreen
	if (y <= -8) return 0;
	if (y >= vid.height) return 0;
	if (x <= -8) return 0;
	if (x >= vid.width) return 0;

	// ok to draw
	return num;
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
	if (!(num = Draw_PrepareCharacter (x, y, num))) return;

	D3D_PrepareFlatDraw (D3DFVF_TEX1, D3DTADDRESS_CLAMP, char_texture);
	D3D_DrawFlatPoly (x, y, 8, 8, quadcoords[num].s, quadcoords[num].t, quadcoords[num].smax, quadcoords[num].tmax);
}


void Draw_InvertCharacter (int x, int y, int num)
{
	if (!(num = Draw_PrepareCharacter (x, y, num))) return;

	D3D_PrepareFlatDraw (D3DFVF_TEX1, D3DTADDRESS_CLAMP, char_texture);
	D3D_DrawFlatPoly (x, y, 8, 8, quadcoords[num].s, quadcoords[num].tmax, quadcoords[num].smax, quadcoords[num].t);
}


void Draw_BackwardsCharacter (int x, int y, int num)
{
	if (!(num = Draw_PrepareCharacter (x, y, num))) return;

	D3D_PrepareFlatDraw (D3DFVF_TEX1, D3DTADDRESS_CLAMP, char_texture);
	D3D_DrawFlatPoly (x, y, 8, 8, quadcoords[num].smax, quadcoords[num].t, quadcoords[num].s, quadcoords[num].tmax);
}


void Draw_RotateCharacter (int x, int y, int num)
{
	if (!(num = Draw_PrepareCharacter (x, y, num))) return;

	D3D_PrepareFlatDraw (D3DFVF_TEX1, D3DTADDRESS_CLAMP, char_texture);

	// rotation needs to go direct as y and y + h are the same t texcoord
	D3D_EmitFlatPolyVert (0, x, y, d3d_2DTextureColor, quadcoords[num].s, quadcoords[num].tmax);
	D3D_EmitFlatPolyVert (1, x + 8, y, d3d_2DTextureColor, quadcoords[num].s, quadcoords[num].t);
	D3D_EmitFlatPolyVert (2, x + 8, y + 8, d3d_2DTextureColor, quadcoords[num].smax, quadcoords[num].t);
	D3D_EmitFlatPolyVert (3, x, y + 8, d3d_2DTextureColor, quadcoords[num].smax, quadcoords[num].tmax);

	d3d_Device->DrawPrimitiveUP (D3DPT_TRIANGLEFAN, 2, verts2d, sizeof (flatpolyvert_t));
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
void Draw_String (int x, int y, char *str, int ofs)
{
	while (*str)
	{
		Draw_Character (x, y, (*str) + ofs);
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
	Draw_Character (vid.width - 20, 20, num);
}


/*
=============
Draw_Pic
=============
*/
void Draw_Pic (int x, int y, qpic_t *pic, float alpha)
{
	glpic_t *gl = (glpic_t *) pic->data;

	DWORD alphacolor = D3DCOLOR_ARGB (BYTE_CLAMP (alpha * 255), 255, 255, 255);

	D3D_PrepareFlatDraw (D3DFVF_TEX1, D3DTADDRESS_CLAMP, gl->tex->d3d_Texture);
	D3D_DrawFlatPoly (x, y, pic->width, pic->height, alphacolor, gl->sl, gl->tl, gl->sh, gl->th);
}


void Draw_Pic (int x, int y, qpic_t *pic)
{
	glpic_t *gl = (glpic_t *) pic->data;

	D3D_PrepareFlatDraw (D3DFVF_TEX1, D3DTADDRESS_CLAMP, gl->tex->d3d_Texture);
	D3D_DrawFlatPoly (x, y, pic->width, pic->height, gl->sl, gl->tl, gl->sh, gl->th);
}


void Draw_Pic (int x, int y, int w, int h, LPDIRECT3DTEXTURE9 texpic)
{
	D3D_PrepareFlatDraw (D3DFVF_TEX1, D3DTADDRESS_CLAMP, texpic);
	D3D_DrawFlatPoly (x, y, w, h, 0, 0, 1, 1);
}


void Draw_Crosshair (int x, int y, int size)
{
	// we don't know about these cvars
	extern cvar_t crosshair;
	extern cvar_t scr_crosshaircolor;

	// no custom crosshair
	if (crosshair.integer < 3) return;

	// bound colour
	if (scr_crosshaircolor.integer < 0) Cvar_Set (&scr_crosshaircolor, (float) 0.0f);
	if (scr_crosshaircolor.integer > 13) Cvar_Set (&scr_crosshaircolor, 13.0f);

	// handle backwards ranges
	int cindex = scr_crosshaircolor.integer * 16 + (scr_crosshaircolor.integer < 8 ? 15 : 0);

	// bound
	if (cindex < 0) cindex = 0;
	if (cindex > 255) cindex = 255;

	D3DCOLOR xhaircolor = D3DCOLOR_XRGB
	(
		((byte *) &d_8to24table[cindex])[2],
		((byte *) &d_8to24table[cindex])[1],
		((byte *) &d_8to24table[cindex])[0]
	);

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

	// bind it (modulate by color)
	D3D_PrepareFlatDraw (D3DFVF_TEX1, D3DTADDRESS_CLAMP, crosshairtexture);
	D3D_SetTextureColorMode (0, D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE);
	D3D_DrawFlatPoly (x, y, size, size, xhaircolor, s, t, s + 0.25, t + 0.25);
	D3D_SetTextureColorMode (0, D3DTOP_SELECTARG1, D3DTA_TEXTURE, D3DTA_DIFFUSE);
}


void Draw_HalfPic (int x, int y, qpic_t *pic)
{
	glpic_t *gl = (glpic_t *) pic->data;

	D3D_PrepareFlatDraw (D3DFVF_TEX1, D3DTADDRESS_CLAMP, gl->tex->d3d_Texture);
	D3D_DrawFlatPoly (x, y, pic->width / 2, pic->height / 2, gl->sl, gl->tl, gl->sh, gl->th);
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
	D3D_PrepareFlatDraw (D3DFVF_TEX1, D3DTADDRESS_CLAMP, mstex);
	D3D_DrawFlatPoly (x, y, 128, 128, 0, 0, 1, 1);
}


void Draw_Mapshot (char *name, int x, int y)
{
	if (!name)
	{
		// no name supplied so display the console image instead
		glpic_t *gl = (glpic_t *) conback->data;

		// note - sl/tl/sh/th for the conback are correct 0 and 1 values
		Draw_MapshotTexture (gl->tex->d3d_Texture, x, y);
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
void Draw_PicTranslate (int x, int y, qpic_t *pic, byte *translation, int shirt, int pants)
{
	int			v, u, c;
	unsigned int			*dest;
	byte			*src;
	int				p;
	static int old_shirt = -1;
	static int old_pants = -1;

	if (shirt == old_shirt && pants == old_pants)
	{
		// prevent updating if it hasn't changed
		Draw_Pic (x, y, pic);
		return;
	}

	old_shirt = shirt;
	old_pants = pants;
	c = pic->width * pic->height;

	// lock the texture rectangle for updating
	LPDIRECT3DTEXTURE9 tex = ((glpic_t *) (pic->data))->tex->d3d_Texture;
	D3DSURFACE_DESC Level0Desc;
	D3DLOCKED_RECT Level0Rect;

	LPDIRECT3DSURFACE9 LumaSurf;

	tex->GetLevelDesc (0, &Level0Desc);
	tex->GetSurfaceLevel (0, &LumaSurf);

	// copy it out to an ARGB surface
	LPDIRECT3DSURFACE9 CopySurf;

	d3d_Device->CreateOffscreenPlainSurface (Level0Desc.Width, Level0Desc.Height, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &CopySurf, NULL);
	D3DXLoadSurfaceFromSurface (CopySurf, NULL, NULL, LumaSurf, NULL, NULL, D3DX_FILTER_NONE, 0);
	CopySurf->LockRect (&Level0Rect, NULL, 0);

	dest = (unsigned *) Level0Rect.pBits;

	for (v = 0; v < 64; v++, dest += 64)
	{
		src = &menuplyr_pixels[((v * pic->height) >> 6) * pic->width];

		for (u = 0; u < 64; u++)
		{
			p = src[(u * pic->width) >> 6];

			// bug in the previous just set colour to red rather than the correct alpha
			dest[u] =  d_8to24table[translation[p]];
		}
	}

	CopySurf->UnlockRect ();
	D3DXLoadSurfaceFromSurface (LumaSurf, NULL, NULL, CopySurf, NULL, NULL, D3DX_FILTER_NONE, 0);

	CopySurf->Release ();
	LumaSurf->Release ();

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
		Draw_Pic (0, lines - vid.height, conback, (float) lines / y);
	else Draw_Pic (0, lines - vid.height, conback);

	Draw_String (vid.width - 84, (lines - vid.height) + vid.height - 30, "DirectQ", 128);
	Draw_String (vid.width - 76, (lines - vid.height) + vid.height - 22, va ("%s", DIRECTQ_VERSION), 128);
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

	D3D_PrepareFlatDraw (D3DFVF_TEX1, D3DTADDRESS_WRAP, gl->tex->d3d_Texture);
	D3D_DrawFlatPoly (x, y, w, h, x / 64.0, y / 64.0, (x + w) / 64.0, (y + h) / 64.0);
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

	DWORD fillcolor = D3DCOLOR_ARGB
	(
		BYTE_CLAMP (alpha),
		bgra[2],
		bgra[1],
		bgra[0]
	);

	D3D_DrawColoredPoly (x, y, w, h, fillcolor);
}


void Draw_Fill (int x, int y, int w, int h, float r, float g, float b, float alpha)
{
	DWORD fillcolor = D3DCOLOR_ARGB
	(
		BYTE_CLAMP (alpha * 255),
		BYTE_CLAMP (r * 255),
		BYTE_CLAMP (g * 255),
		BYTE_CLAMP (b * 255)
	);

	D3D_DrawColoredPoly (x, y, w, h, fillcolor);
}


//=============================================================================

/*
================
Draw_FadeScreen

================
*/
void Draw_FadeScreen (int alpha)
{
	D3D_DrawColoredPoly (0, 0, vid.width, vid.height, D3DCOLOR_ARGB (BYTE_CLAMP (alpha), 0, 0, 0));
}


//=============================================================================

/*
============
Draw_PolyBlend
============
*/
void Draw_PolyBlend (void)
{
	if (d3d_RenderDef.automap) return;
	if (!gl_polyblend.value) return;
	if (!v_blend[3]) return;

	DWORD blendcolor = D3DCOLOR_ARGB
	(
		BYTE_CLAMP (v_blend[3]),
		BYTE_CLAMP (v_blend[0]),
		BYTE_CLAMP (v_blend[1]),
		BYTE_CLAMP (v_blend[2])
	);

	// don't go down into the status bar area
	D3D_DrawColoredPoly (0, 0, vid.width, vid.height - sb_lines, blendcolor);

	// disable polyblend in case the map changes while a blend is active
	v_blend[3] = 0;
}


/*
========================================================================================

	BBB   L     OO    OO   DDD      AA   N  N  DDD     DDD   EEEE   AA  TTTTT  H  H
	B  B  L    O  O  O  O  D  D    A  A  NN N  D  D    D  D  E     A  A   T    H  H
	BBB   L    O  O  O  O  D  D    A  A  N NN  D  D    D  D  EEE   A  A   T    HHHH
	B  B  L    O  O  O  O  D  D    AAAA  N  N  D  D    D  D  E     AAAA   T    H  H
	BBB   LLLL  OO    OO   DDD     A  A  N  N  DDD     DDD   EEEE  A  A   T    H  H

========================================================================================
*/

void Draw_DeathBlend (void)
{
	// don't draw it if in the automap or not connected
	if (d3d_RenderDef.automap) return;
	if (cls.state != ca_connected) return;

	// console is full screen
	if (scr_con_current == vid.height) return;

	// no death in intermission
	if (cl.intermission) return;

	// don't draw it in multiplayer as some people might like to take a good
	// look around after they die, and god knows there's all sorts of folks in the world
	if (cl.maxclients > 1) return;

	static float deathred = 0;
	static float deathalpha = 0;
	static bool wasalive = true;

	// see if we're still alive
	if (cl.stats[STAT_HEALTH] > 0)
	{
		// we're still alive
		wasalive = true;
		return;
	}

	// ok, we're dead - see did we just die or have we been dead for a while?
	if (wasalive)
	{
		// init the color and blend
		deathalpha = 0;
		deathred = 0.666;

		// signal that we've been dead before
		wasalive = false;
	}

	// draw the death blend fade out
	DWORD deathcolor = D3DCOLOR_ARGB
	(
		BYTE_CLAMP (deathalpha * 255.0f),
		BYTE_CLAMP (deathred * 255.0f),
		BYTE_CLAMP (0),
		BYTE_CLAMP (0)
	);

	D3D_DrawColoredPoly (0, 0, vid.width, vid.height, deathcolor);

	// update blend
	deathalpha += d3d_RenderDef.frametime / 5.0f;
	deathred -= d3d_RenderDef.frametime / 8.0f;
}


// ========================================================================================


void D3D_Set2DShade (float shadecolor)
{
	if (shadecolor >= 0.99f)
	{
		// solid
		D3D_SetTextureColorMode (0, D3DTOP_SELECTARG1, D3DTA_TEXTURE, D3DTA_DIFFUSE);
		d3d_2DTextureColor = 0xffffffff;
	}
	else
	{
		// 0 to 255 scale
		shadecolor *= 255.0f;

		// fade out
		d3d_2DTextureColor = D3DCOLOR_ARGB
		(
			BYTE_CLAMP (shadecolor),
			BYTE_CLAMP (shadecolor),
			BYTE_CLAMP (shadecolor),
			BYTE_CLAMP (shadecolor)
		);

		D3D_SetTextureColorMode (0, D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE);
	}
}


// we'll store this out too in case we ever want to do anything with it
void D3D_DrawUnderwaterWarp (void);
void SCR_SetupToDrawHUD (void);

void D3D_Set2D (void)
{
	D3DVIEWPORT9 d3d_2DViewport;

	// switch to a fullscreen viewport for 2D drawing
	d3d_2DViewport.X = 0;
	d3d_2DViewport.Y = 0;
	d3d_2DViewport.Width = d3d_CurrentMode.Width;
	d3d_2DViewport.Height = d3d_CurrentMode.Height;
	d3d_2DViewport.MinZ = 0.0f;
	d3d_2DViewport.MaxZ = 1.0f;

	// this may be the same as the 3d viewport in which case we wouldn't need to change it
	// (but there may not always be 3d stuff rendered...) (do we even need a viewport with xyzrhw?)
	d3d_Device->SetViewport (&d3d_2DViewport);

	// disable depth testing and writing
	D3D_SetRenderState (D3DRS_ZENABLE, D3DZB_FALSE);
	D3D_SetRenderState (D3DRS_ZWRITEENABLE, FALSE);

	// no backface culling
	D3D_BackfaceCull (D3DCULL_NONE);

	// evaluate scaling factor for 2d rendering (uses D3DFVF_RHW instead of transforms)
	vert2dscale_x = (float) d3d_CurrentMode.Width / (float) vid.width;
	vert2dscale_y = (float) d3d_CurrentMode.Height / (float) vid.height;

	// state
	D3D_SetTextureMipmap (0, d3d_3DFilterMag, d3d_3DFilterMin, D3DTEXF_NONE);
	D3D_SetTexCoordIndexes (0, 0, 0);

	// draw the underwater warp here - all the above state is common but it uses a different orthographic matrix
	if (!d3d_RenderDef.automap) D3D_DrawUnderwaterWarp ();

	// modulate alpha always here
	D3D_SetTextureColorMode (0, D3DTOP_SELECTARG1, D3DTA_TEXTURE, D3DTA_DIFFUSE);
	D3D_SetTextureAlphaMode (0, D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE);

	D3D_SetTextureColorMode (1, D3DTOP_DISABLE);
	D3D_SetTextureAlphaMode (1, D3DTOP_DISABLE);

	D3D_SetTextureColorMode (2, D3DTOP_DISABLE);
	D3D_SetTextureAlphaMode (2, D3DTOP_DISABLE);

	// do this before enabling alpha blending so that we're not alpha-blending a large part of the screen
	// with a texture that has an alpha of 1!!!
	SCR_SetupToDrawHUD ();

	// enable alpha blending
	D3D_EnableAlphaBlend (D3DBLENDOP_ADD, D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA);

	// do the polyblends here for simplicity
	Draw_PolyBlend ();
	Draw_DeathBlend ();
}

