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

#define	D3DQUAKE_VERSION		0.01

cvar_t		gl_nobind = {"gl_nobind", "0"};
cvar_t		gl_max_size = {"gl_max_size", "1024"};
cvar_t		gl_picmip = {"gl_picmip", "0"};
cvar_t		gl_conscale = {"gl_conscale", "0", true};

byte		*draw_chars;				// 8*8 graphic characters
qpic_t		*draw_disc;
qpic_t		*draw_backtile;

int			translate_texture;
LPDIRECT3DTEXTURE9 char_texture;

typedef struct
{
	LPDIRECT3DTEXTURE9 tex;
	float	sl, tl, sh, th;
} glpic_t;


qpic_t *conback;

int		texels;


typedef struct vert_2dtextured_s
{
	float xyz[3];
	float st[2];
} vert_2dtextured_t;

typedef struct vert_2dtexturedcoloured_s
{
	float xyz[3];
	DWORD color;
	float st[2];
} vert_2dtexturedcoloured_t;

typedef struct vert_2dcoloured_s
{
	float xyz[3];
	DWORD color;
} vert_2dcoloured_t;


//=============================================================================
/* Support Routines */

typedef struct cachepic_s
{
	char		name[MAX_QPATH];
	qpic_t		pic;
	byte		padding[32];	// for appended glpic
} cachepic_t;

#define	MAX_CACHED_PICS		128
cachepic_t	menu_cachepics[MAX_CACHED_PICS];
int			menu_numcachepics;

byte		menuplyr_pixels[4096];

int		pic_texels;
int		pic_count;

qpic_t *Draw_PicFromWad (char *name)
{
	qpic_t	*p;
	glpic_t	*gl;

	p = (qpic_t *) W_GetLumpName (name);
	gl = (glpic_t *)p->data;

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
qpic_t	*Draw_CachePic (char *path)
{
	cachepic_t	*pic;
	int			i;
	qpic_t		*dat;
	glpic_t		*gl;

	for (pic=menu_cachepics, i=0 ; i<menu_numcachepics ; pic++, i++)
		if (!strcmp (path, pic->name))
			return &pic->pic;

	if (menu_numcachepics == MAX_CACHED_PICS)
		Sys_Error ("menu_numcachepics == MAX_CACHED_PICS");
	menu_numcachepics++;
	strcpy (pic->name, path);

//
// load the pic from disk
//
	dat = (qpic_t *)COM_LoadTempFile (path);	
	if (!dat)
		Sys_Error ("Draw_CachePic: failed to load %s", path);
	SwapPic (dat);

	// HACK HACK HACK --- we need to keep the bytes for
	// the translatable player picture just for the menu
	// configuration dialog
	if (!strcmp (path, "gfx/menuplyr.lmp"))
		memcpy (menuplyr_pixels, dat->data, dat->width*dat->height);

	pic->pic.width = dat->width;
	pic->pic.height = dat->height;

	gl = (glpic_t *)pic->pic.data;

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
	newchars = (byte *) malloc (w * h * 4);

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
//	free (buf);
	free (newchars);
}


/*
===============
Draw_Init
===============
*/
void Draw_Init (void)
{
	qpic_t	*cb;
	byte	*dest;
	int		x, y;
	char	ver[40];
	glpic_t	*gl;
	int		start;
	byte	*ncdata;

	Cvar_RegisterVariable (&gl_nobind);
	Cvar_RegisterVariable (&gl_max_size);
	Cvar_RegisterVariable (&gl_picmip);
	Cvar_RegisterVariable (&gl_conscale);

	// load the console background and the charset
	// by hand, because we need to write the version
	// string into the background before turning
	// it into a texture
	draw_chars = (byte *) W_GetLumpName ("conchars");
	Draw_SpaceOutCharSet (draw_chars, 128, 128);

	// load the console background
	conback = Draw_CachePic ("gfx/conback.lmp");

	// set it's correct dimensions
	conback->width = vid.width;
	conback->height = vid.height;

	// get the other pics we need
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
	int				row, col;
	float			frow, fcol, size;

	num &= 255;
	
	if (num == 32)
		return;		// space

	if (y <= -8)
		return;			// totally off screen

	row = num>>4;
	col = num&15;

	// base texcoords relative to grid
	frow = row * 0.0625;
	fcol = col * 0.0625;

	// half size because we space out the chars to prevent bilerp overlap
	size = 0.03125;

	D3D_SetFVFStateManaged (D3DFVF_XYZ | D3DFVF_TEX1);
	D3D_BindTexture (char_texture);

	vert_2dtextured_t verts[] =
	{
		{x, y, 0, fcol, frow},
		{x + 8, y, 0, fcol + size, frow},
		{x + 8, y + 8, 0, fcol + size, frow + size},
		{x, y + 8, 0, fcol, frow + size}
	};

	d3d_Device->DrawPrimitiveUP (D3DPT_TRIANGLEFAN, 2, verts, sizeof (vert_2dtextured_t));
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

	d3d_DisableAlphaTest->Apply ();
	d3d_EnableAlphaBlend->Apply ();

	alpha *= 255;

	DWORD AlphaColour = D3DCOLOR_ARGB (alpha > 255 ? 255 : (int) alpha, 255, 255, 255);

	D3D_SetFVFStateManaged (D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
	D3D_BindTexture (gl->tex);

	vert_2dtexturedcoloured_t verts[] =
	{
		{x, y, 0, AlphaColour, gl->sl, gl->tl},
		{x + pic->width, y, 0, AlphaColour, gl->sh, gl->tl},
		{x + pic->width, y + pic->height, 0, AlphaColour, gl->sh, gl->th},
		{x, y + pic->height, 0, AlphaColour, gl->sl, gl->th}
	};

	d3d_Device->DrawPrimitiveUP (D3DPT_TRIANGLEFAN, 2, verts, sizeof (vert_2dtexturedcoloured_t));

	d3d_DisableAlphaBlend->Apply ();
	d3d_EnableAlphaTest->Apply ();
}


/*
=============
Draw_Pic
=============
*/
void Draw_Pic (int x, int y, qpic_t *pic)
{
	glpic_t *gl = (glpic_t *) pic->data;

	D3D_SetFVFStateManaged (D3DFVF_XYZ | D3DFVF_TEX1);
	D3D_BindTexture (gl->tex);

	vert_2dtextured_t verts[] =
	{
		{x, y, 0, gl->sl, gl->tl},
		{x + pic->width, y, 0, gl->sh, gl->tl},
		{x + pic->width, y + pic->height, 0, gl->sh, gl->th},
		{x, y + pic->height, 0, gl->sl, gl->th}
	};

	d3d_Device->DrawPrimitiveUP (D3DPT_TRIANGLEFAN, 2, verts, sizeof (vert_2dtextured_t));
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

	if (lines > y)
		Draw_Pic (0, lines - vid.height, conback);
	else
		Draw_AlphaPic (0, lines - vid.height, conback, (float)(1.2 * lines)/y);
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

	D3D_SetFVFStateManaged (D3DFVF_XYZ | D3DFVF_TEX1);
	D3D_BindTexture (gl->tex);

	vert_2dtextured_t verts[] =
	{
		{x, y, 0, x / 64.0, y / 64.0},
		{x + w, y, 0, (x + w) / 64.0, y / 64.0},
		{x + w, y + h, 0, (x + w) / 64.0, (y + h) / 64.0},
		{x, y + h, 0, x / 64.0, (y + h) / 64.0},
	};

	d3d_Device->DrawPrimitiveUP (D3DPT_TRIANGLEFAN, 2, verts, sizeof (vert_2dtextured_t));
}


/*
=============
Draw_Fill

Fills a box of pixels with a single color
=============
*/
void Draw_Fill (int x, int y, int w, int h, int c)
{
	// disable texturing
	d3d_Device->SetTextureStageState (0, D3DTSS_COLOROP, D3DTOP_DISABLE);

	D3D_SetFVFStateManaged (D3DFVF_XYZ | D3DFVF_DIFFUSE);

	byte *bgra = (byte *) &d_8to24table[c];

	DWORD FillColour = D3DCOLOR_XRGB (bgra[2], bgra[1], bgra[0]);

	vert_2dcoloured_t verts[] =
	{
		{x, y, 0, FillColour},
		{x + w, y, 0, FillColour},
		{x + w, y + h, 0, FillColour},
		{x, y + h, 0, FillColour}
	};

	d3d_Device->DrawPrimitiveUP (D3DPT_TRIANGLEFAN, 2, verts, sizeof (vert_2dcoloured_t));

	// revert state
	d3d_Device->SetTextureStageState (0, D3DTSS_COLOROP, D3DTOP_MODULATE);
}

//=============================================================================

/*
================
Draw_FadeScreen

================
*/
void Draw_FadeScreen (void)
{
	// disable texturing
	d3d_Device->SetTextureStageState (0, D3DTSS_COLOROP, D3DTOP_DISABLE);

	d3d_DisableAlphaTest->Apply ();
	d3d_EnableAlphaBlend->Apply ();

	D3D_SetFVFStateManaged (D3DFVF_XYZ | D3DFVF_DIFFUSE);
	DWORD FadeColour = D3DCOLOR_ARGB (204, 0, 0, 0);

	vert_2dcoloured_t verts[] =
	{
		{0, 0, 0, FadeColour},
		{vid.width, 0, 0, FadeColour},
		{vid.width, vid.height, 0, FadeColour},
		{0, vid.height, 0, FadeColour}
	};

	d3d_Device->DrawPrimitiveUP (D3DPT_TRIANGLEFAN, 2, verts, sizeof (vert_2dcoloured_t));

	// revert state
	d3d_Device->SetTextureStageState (0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	d3d_DisableAlphaBlend->Apply ();
	d3d_EnableAlphaTest->Apply ();
}


//=============================================================================

bool DrawLoadDisc = false;

/*
================
Draw_BeginDisc

Draws the little blue disc in the corner of the screen.
Call before beginning any disc IO.
================
*/
void Draw_BeginDisc (void)
{
	if (!draw_disc) return;

	DrawLoadDisc = true;
}


/*
================
Draw_EndDisc

Erases the disc icon.
Call after completing any disc IO
================
*/
void Draw_EndDisc (void)
{
	DrawLoadDisc = false;
}


/*
============
Draw_PolyBlend
============
*/
void Draw_PolyBlend (void)
{
	if (!gl_polyblend.value) return;
	if (!v_blend[3]) return;

	// disable texturing
	d3d_Device->SetTextureStageState (0, D3DTSS_COLOROP, D3DTOP_DISABLE);

	d3d_DisableAlphaTest->Apply ();
	d3d_EnableAlphaBlend->Apply ();

	D3D_SetFVFStateManaged (D3DFVF_XYZ | D3DFVF_DIFFUSE);
	DWORD BlendColour = D3DCOLOR_ARGB (v_blend[3], v_blend[0], v_blend[1], v_blend[2]);

	vert_2dcoloured_t verts[] =
	{
		{0, 0, 0, BlendColour},
		{vid.width, 0, 0, BlendColour},
		{vid.width, vid.height, 0, BlendColour},
		{0, vid.height, 0, BlendColour}
	};

	d3d_Device->DrawPrimitiveUP (D3DPT_TRIANGLEFAN, 2, verts, sizeof (vert_2dcoloured_t));

	// revert state
	d3d_Device->SetTextureStageState (0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	d3d_DisableAlphaBlend->Apply ();
	d3d_EnableAlphaTest->Apply ();
}


void D3D_Set2D (void)
{
	// switch to the fullscreen viewport for 2D drawing
	d3d_2DViewport->Apply ();

	// world matrix
	d3d_WorldMatrixStack->LoadIdentity ();
	d3d_Device->SetTransform (D3DTS_WORLD, d3d_WorldMatrixStack->GetTop ());

	// projection matrix
	D3DXMatrixIdentity (&d3d_ProjectionMatrix);
	D3DXMatrixOrthoOffCenterRH (&d3d_ProjectionMatrix, 0, vid.width, vid.height, 0, -100, 100);
	d3d_Device->SetTransform (D3DTS_PROJECTION, &d3d_ProjectionMatrix);

	// disable depth testing and writing
	d3d_Device->SetRenderState (D3DRS_ZENABLE, D3DZB_FALSE);
	d3d_Device->SetRenderState (D3DRS_ZWRITEENABLE, FALSE);

	// no backface culling
	d3d_Device->SetRenderState (D3DRS_CULLMODE, D3DCULL_NONE);

	// do the polyblend here for simplicity
	Draw_PolyBlend ();

	// enable alpha testing
	d3d_EnableAlphaTest->Apply ();

	// default texturing
	d3d_Device->SetTextureStageState (0, D3DTSS_COLOROP, D3DTOP_MODULATE);
}

