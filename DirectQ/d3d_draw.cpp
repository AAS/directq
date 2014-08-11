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

// draw.c -- this is the only file outside the refresh that touches the
// vid buffer

#include "quakedef.h"
#include "d3d_model.h"
#include "d3d_quake.h"
#include "d3d_vbo.h"


//  scrap allocation
//  Allocate all the little status bar obejcts into a single texture
//  to crutch up stupid hardware / drivers

// 512 x 256 is adequate for ID1; the extra space is for rogue/hipnotic extras
#define	MAX_SCRAPS		1
#define	SCRAP_WIDTH		512
#define	SCRAP_HEIGHT	512

// fixme - move to a struct, or lock the texture
int			scrap_allocated[SCRAP_WIDTH];
byte		scrap_texels[SCRAP_WIDTH * SCRAP_HEIGHT];
bool		scrap_dirty = false;
image_t		scrap_textures;


void Scrap_Init (void)
{
	SAFE_RELEASE (scrap_textures.d3d_Texture);

	// clear texels to correct alpha colour
	Q_MemSet (scrap_texels, 255, sizeof (scrap_texels));
	Q_MemSet (scrap_allocated, 0, sizeof (scrap_allocated));

	scrap_dirty = false;
}


void Scrap_Destroy (void)
{
	// same as above
	SAFE_RELEASE (scrap_textures.d3d_Texture);

	// clear texels to correct alpha colour
	Q_MemSet (scrap_texels, 255, sizeof (scrap_texels));
	Q_MemSet (scrap_allocated, 0, sizeof (scrap_allocated));

	scrap_dirty = false;
}


/*
================
Scrap_AllocBlock

returns an index into scrap_texnums[] and the position inside it
================
*/
bool Scrap_AllocBlock (int w, int h, int *x, int *y)
{
	int		i, j;
	int		best, best2;
	int		texnum;

	best = SCRAP_HEIGHT;

	for (i = 0; i < SCRAP_WIDTH - w; i++)
	{
		best2 = 0;

		for (j = 0; j < w; j++)
		{
			if (scrap_allocated[i + j] >= best) break;
			if (scrap_allocated[i + j] > best2) best2 = scrap_allocated[i + j];
		}

		if (j == w)
		{
			// this is a valid spot
			*x = i;
			*y = best = best2;
		}
	}

	if (best + h > SCRAP_HEIGHT) return false;

	// done
	for (i = 0; i < w; i++)
		scrap_allocated[*x + i] = best + h;

	return true;
}

/*
================
Scrap_Upload
================
*/
void Scrap_Upload (void)
{
	if (scrap_dirty)
	{
		SAFE_RELEASE (scrap_textures.d3d_Texture);
		D3D_UploadTexture (&scrap_textures.d3d_Texture, scrap_texels, SCRAP_WIDTH, SCRAP_HEIGHT, IMAGE_ALPHA | IMAGE_NOCOMPRESS);

		// SCR_WriteDataToTGA ("scrap.tga", scrap_texels, SCRAP_WIDTH, SCRAP_HEIGHT, 8, 32);
		scrap_dirty = false;
	}
}


cvar_t gl_conscale ("gl_conscale", "1", CVAR_ARCHIVE);

char *gfxlmps[] =
{
	"bigbox.lmp", "box_bl.lmp", "box_bm.lmp", "box_br.lmp", "box_ml.lmp", "box_mm.lmp", "box_mm2.lmp", "box_mr.lmp", "box_tl.lmp", "box_tm.lmp", "box_tr.lmp",
	"colormap.lmp", "complete.lmp", "conback.lmp", "dim_drct.lmp", "dim_ipx.lmp", "dim_modm.lmp", "dim_mult.lmp", "dim_tcp.lmp", "finale.lmp", "help0.lmp",
	"help1.lmp", "help2.lmp", "help3.lmp", "help4.lmp", "help5.lmp", "inter.lmp", "loading.lmp", "mainmenu.lmp", "menudot1.lmp", "menudot2.lmp", "menudot3.lmp",
	"menudot4.lmp", "menudot5.lmp", "menudot6.lmp", "menuplyr.lmp", "mp_menu.lmp", "netmen1.lmp", "netmen2.lmp", "netmen3.lmp", "netmen4.lmp", "netmen5.lmp",
	"palette.lmp", "pause.lmp", "p_load.lmp", "p_multi.lmp", "p_option.lmp", "p_save.lmp", "qplaque.lmp", "ranking.lmp", "sell.lmp", "sp_menu.lmp", "ttl_cstm.lmp",
	"ttl_main.lmp", "ttl_sgl.lmp", "vidmodes.lmp", NULL
};


void Draw_DumpLumps (void)
{
	for (int i = 0;; i++)
	{
		if (!gfxlmps[i]) break;

		// load the pic from disk
		qpic_t *dat = (qpic_t *) COM_LoadFile (va ("gfx/%s", gfxlmps[i]));

		if (dat->width > 1024) continue;
		if (dat->height > 1024) continue;

		SCR_WriteDataToTGA (va ("%s.tga", gfxlmps[i]), dat->data, dat->width, dat->height, 8, 24);
		Zone_Free (dat);
	}
}


cvar_t		gl_nobind ("gl_nobind", "0");

byte		*draw_chars;				// 8*8 graphic characters
qpic_t		*draw_disc;
qpic_t		*draw_backtile;

int			translate_texture;

LPDIRECT3DTEXTURE9 char_texture = NULL;
LPDIRECT3DTEXTURE9 char_textures[MAX_CHAR_TEXTURES] = {NULL};	// if this array size is changed the release loop in D3D_ReleaseTextures must be changed also!!!
LPDIRECT3DTEXTURE9 crosshairtexture = NULL;

typedef struct crosshair_s
{
	float l;
	float t;
	float r;
	float b;
	bool replaced;
	LPDIRECT3DTEXTURE9 texture;
} crosshair_t;

int d3d_NumCrosshairs = 0;
crosshair_t *d3d_Crosshairs = NULL;

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
	float scaled_width;
	float scaled_height;
} glpic_t;


qpic_t *conback;

int		texels;

// 2d drawing
DWORD d3d_2DTextureColor = 0xffffffff;

cvar_t r_smoothcharacters ("r_smoothcharacters", "0", CVAR_ARCHIVE);

//=============================================================================
/* Support Routines */

byte		*menuplyr_pixels = NULL;
byte		*menuplyr_pixels_translated = NULL;

int		pic_texels;
int		pic_count;

// failsafe pic for when a load from the wad fails
// (prevent crashes here during mod dev cycles)
byte *failsafedata = NULL;
qpic_t *draw_failsafe = NULL;


qpic_t *Draw_LoadPicData (char *name, qpic_t *pic, bool allowscrap)
{
	if (pic->width < 1 || pic->height < 1)
	{
		// this occasionally gets hosed, dunno why yet...
		// seems preferable to crashing
		Con_Printf ("Draw_LoadPicData: pic->width < 1 || pic->height < 1 for %s\n(I fucked up - sorry)\n", name);
		pic = draw_failsafe;

#ifdef _DEBUG
		// prevent an exception in release builds
		assert (false);
#endif
	}

	// HACK HACK HACK --- we need to keep the bytes for
	// the translatable player picture just for the menu
	// configuration dialog
	if (!strcmp (name, "gfx/menuplyr.lmp"))
	{
		// this should only happen once
		menuplyr_pixels = (byte *) GameZone->Alloc (pic->width * pic->height);
		menuplyr_pixels_translated = (byte *) GameZone->Alloc (pic->width * pic->height);
		Q_MemCpy (menuplyr_pixels, pic->data, pic->width * pic->height);
		Q_MemCpy (menuplyr_pixels_translated, pic->data, pic->width * pic->height);
	}

	glpic_t *gl = (glpic_t *) pic->data;
	image_t *tex = NULL;

	// we can't use gl->tex as the return as it would overwrite pic->data would would corrupt the texels
	// backtile doesn't go in the scrap because it wraps
	if (!strcmp (name, "gfx/backtile.blah") || !allowscrap)
		tex = D3D_LoadTexture (name, pic->width, pic->height, pic->data, IMAGE_ALPHA | IMAGE_PADDABLE);
	else tex = D3D_LoadTexture (name, pic->width, pic->height, pic->data, IMAGE_ALPHA | IMAGE_PADDABLE | IMAGE_SCRAP);

	if (!tex)
	{
		int		x, y;

		// pad the allocation to prevent linear filtering from causing the edges of adjacent textures to bleed into each other
		int scrapw = pic->width + 4;
		int scraph = pic->height + 4;

		if (scrapw > SCRAP_WIDTH) goto noscrap;
		if (scraph > SCRAP_HEIGHT) goto noscrap;

		// find a padded block
		if (!Scrap_AllocBlock (scrapw, scraph, &x, &y)) goto noscrap;

		// center in the padded region
		x += 2;
		y += 2;

		scrap_dirty = true;

		// pad up/down/left/right so that the correct texels will be caught when filtering
		for (int i = 0, k = 0; i < pic->height; i++)
			for (int j = 0; j < pic->width; j++, k++)
				scrap_texels[((y - 1) + i) * SCRAP_WIDTH + x + j] = pic->data[k];

		for (int i = 0, k = 0; i < pic->height; i++)
			for (int j = 0; j < pic->width; j++, k++)
				scrap_texels[((y + 1) + i) * SCRAP_WIDTH + x + j] = pic->data[k];

		for (int i = 0, k = 0; i < pic->height; i++)
			for (int j = 0; j < pic->width; j++, k++)
				scrap_texels[(y + i) * SCRAP_WIDTH + (x - 1) + j] = pic->data[k];

		for (int i = 0, k = 0; i < pic->height; i++)
			for (int j = 0; j < pic->width; j++, k++)
				scrap_texels[(y + i) * SCRAP_WIDTH + (x + 1) + j] = pic->data[k];

		// do the final centered image
		for (int i = 0, k = 0; i < pic->height; i++)
			for (int j = 0; j < pic->width; j++, k++)
				scrap_texels[(y + i) * SCRAP_WIDTH + x + j] = pic->data[k];

		gl->tex = &scrap_textures;

		gl->scaled_width = pic->width;
		gl->scaled_height = pic->height;

		gl->sl = x / (float) SCRAP_WIDTH;
		gl->sh = (x + gl->scaled_width) / (float) SCRAP_WIDTH;
		gl->tl = y / (float) SCRAP_HEIGHT;
		gl->th = (y + gl->scaled_height) / (float) SCRAP_HEIGHT;
	}
	else
	{
noscrap_reload:;
		gl->tex = tex;
		gl->sl = 0;
		gl->tl = 0;
		gl->sh = 1;
		gl->th = 1;

		if (gl->tex->flags & IMAGE_PADDED)
		{
			gl->scaled_width = D3D_PowerOf2Size (pic->width);
			gl->scaled_height = D3D_PowerOf2Size (pic->height);
		}
		else
		{
			gl->scaled_width = pic->width;
			gl->scaled_height = pic->height;
		}
	}

	return pic;

noscrap:;
	// reload with no scrap
	tex = D3D_LoadTexture (name, pic->width, pic->height, pic->data, IMAGE_ALPHA | IMAGE_PADDABLE);
	goto noscrap_reload;
}


qpic_t *Draw_LoadPic (char *name)
{
	qpic_t	*pic = NULL;
	char loadname[256] = {0};
	bool usescrap = false;

	// this should never happen
	if (!name) return NULL;

	// look for it in the gfx.wad
	pic = (qpic_t *) W_GetLumpName (name);

	// did we get it?
	if (pic)
	{
		// yes, construct a fake loadname (it needs an extension too for the external texture loader) and
		// allow it to use the scrap texture
		sprintf (loadname, "gfx/%s.blah", name);
		usescrap = true;
	}
	else
	{
		// no, look for it in the direct path given (never uses the scrap)
		pic = (qpic_t *) COM_LoadFile (name, GameZone);
		strcpy (loadname, name);
		usescrap = false;
	}

	if (!pic)
	{
		// if both failed we use the failsafe and never use the scrap
		pic = draw_failsafe;
		usescrap = false;
	}

	return Draw_LoadPicData (loadname, pic, usescrap);
}


/*
===============
Draw_Init
===============
*/
void Draw_FreeCrosshairs (void)
{
	if (!d3d_Crosshairs) return;

	for (int i = 0; i < d3d_NumCrosshairs; i++)
	{
		// only textures which were replaced can be released here
		// the others are released separately
		if (d3d_Crosshairs[i].replaced && d3d_Crosshairs[i].texture)
		{
			SAFE_RELEASE (d3d_Crosshairs[i].texture);

			// prevent multiple release attempts
			d3d_Crosshairs[i].texture = NULL;
			d3d_Crosshairs[i].replaced = false;
		}
	}

	// this will force a load the first time we draw
	d3d_NumCrosshairs = 0;
	d3d_Crosshairs = NULL;
}


void Draw_LoadCrosshairs (void)
{
	// free anything that we may have had previously
	Draw_FreeCrosshairs ();

	// load them into the scratch buffer to begin with because we don't know how many we'll have
	d3d_Crosshairs = (crosshair_t *) scratchbuf;

	// full texcoords for each value
	float xhairs[] = {0, 0.25, 0.5, 0.75, 0, 0.25, 0.5, 0.75};
	float xhairt[] = {0, 0, 0, 0, 0.5, 0.5, 0.5, 0.5};

	// load default crosshairs
	for (int i = 0; i < 10; i++)
	{
		if (i < 2)
		{
			// + sign crosshairs
			d3d_Crosshairs[i].texture = char_textures[0];
			d3d_Crosshairs[i].l = quadcoords['+' + (128 * i)].s;
			d3d_Crosshairs[i].t = quadcoords['+' + (128 * i)].t;
			d3d_Crosshairs[i].r = quadcoords['+' + (128 * i)].smax;
			d3d_Crosshairs[i].b = quadcoords['+' + (128 * i)].tmax;
		}
		else
		{
			// crosshair images
			d3d_Crosshairs[i].texture = crosshairtexture;
			d3d_Crosshairs[i].l = xhairs[i - 2];
			d3d_Crosshairs[i].t = xhairt[i - 2];
			d3d_Crosshairs[i].r = xhairs[i - 2] + 0.25f;
			d3d_Crosshairs[i].b = xhairt[i - 2] + 0.5f;
		}

		// we need to track if the image has been replaced so that we know to add colour to crosshair 1 and 2 if so
		d3d_Crosshairs[i].replaced = false;
	}

	// nothing here to begin with
	d3d_NumCrosshairs = 0;

	// now attempt to load replacements
	for (int i = 0; ; i++)
	{
		// attempt to load one
		LPDIRECT3DTEXTURE9 newcrosshair;

		// standard loader; qrack crosshairs begin at 1 and so should we
		if (!D3D_LoadExternalTexture (&newcrosshair, va ("crosshair%i", i + 1), 0))
			break;

		// fill it in
		d3d_Crosshairs[i].texture = newcrosshair;
		d3d_Crosshairs[i].l = 0;
		d3d_Crosshairs[i].t = 0;
		d3d_Crosshairs[i].r = 1;
		d3d_Crosshairs[i].b = 1;
		d3d_Crosshairs[i].replaced = true;

		// mark a new crosshair
		d3d_NumCrosshairs++;
	}

	// always include the standard images
	if (d3d_NumCrosshairs < 10) d3d_NumCrosshairs = 10;

	// now set them up in memory for real - put them in the main zone so that we can free replacement textures properly
	d3d_Crosshairs = (crosshair_t *) MainZone->Alloc (d3d_NumCrosshairs * sizeof (crosshair_t));
	Q_MemCpy (d3d_Crosshairs, scratchbuf, d3d_NumCrosshairs * sizeof (crosshair_t));
}


void Draw_SpaceOutCharSet (byte *data, int w, int h)
{
	byte *buf = data;
	byte *newchars;
	int i, j, c;

	// allocate memory for a new charset
	newchars = (byte *) scratchbuf;

	// clear to all alpha
	Q_MemSet (newchars, 0, w * h * 4);

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
	for (i = 0; i < (w * h); i++)
		if (newchars[i] == 0)
			newchars[i] = 255;

	// now turn them into textures
	D3D_UploadTexture (&char_textures[0], newchars, 256, 256, IMAGE_ALPHA | IMAGE_NOCOMPRESS);
}


bool draw_init = false;

qpic_t *box_ml;
qpic_t *box_mr;
qpic_t *box_tm;
qpic_t *box_bm;
qpic_t *box_mm2;
qpic_t *box_tl;
qpic_t *box_tr;
qpic_t *box_bl;
qpic_t *box_br;
qpic_t *gfx_pause_lmp;
qpic_t *gfx_loading_lmp;

void Menu_InitPics (void);

void Draw_Init (void)
{
	if (draw_init) return;

	draw_init = true;

	// Draw_DumpLumps ();

	qpic_t	*cb;
	byte	*dest;
	int		x, y;
	char	ver[40];
	glpic_t	*gl;
	int		start;
	byte	*ncdata;

	// initialize scrap textures
	Scrap_Init ();

	if (!failsafedata)
	{
		// this is a qpic_t that's used in the event of any qpic_t failing to load!!!
		// we alloc enough memory for the glpic_t that draw_failsafe->data is casted to.
		// this crazy-assed shit prevents an "ambiguous call to overloaded function" error.
		// add 1 cos of integer round down.  do it this way in case we ever change the glpic_t struct
		int failsafedatasize = D3D_PowerOf2Size ((int) sqrt ((float) (sizeof (glpic_t))) + 1);

		// persist in memory
		failsafedata = (byte *) Zone_Alloc (sizeof (int) * 2 + (failsafedatasize * failsafedatasize));
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
		frow = row * 0.0625f;
		fcol = col * 0.0625f;

		// this can change depending on the charsewt we're using (see below)
		size = 0.0625f;

		quadcoords[i].s = fcol;
		quadcoords[i].t = frow;
		quadcoords[i].smax = fcol + size;
		quadcoords[i].tmax = frow + size;
	}

	draw_chars = (byte *) W_GetLumpName ("conchars");
	Draw_SpaceOutCharSet (draw_chars, 128, 128);

	conback = Draw_LoadPic ("gfx/conback.lmp");

	// get the other pics we need
	// draw_disc is also used on the sbar so we need to retain it
	draw_disc = Draw_LoadPic ("disc");
	draw_backtile = Draw_LoadPic ("backtile");

	box_ml = Draw_LoadPic ("gfx/box_ml.lmp");
	box_mr = Draw_LoadPic ("gfx/box_mr.lmp");
	box_tm = Draw_LoadPic ("gfx/box_tm.lmp");
	box_bm = Draw_LoadPic ("gfx/box_bm.lmp");
	box_mm2 = Draw_LoadPic ("gfx/box_mm2.lmp");
	box_tl = Draw_LoadPic ("gfx/box_tl.lmp");
	box_tr = Draw_LoadPic ("gfx/box_tr.lmp");
	box_bl = Draw_LoadPic ("gfx/box_bl.lmp");
	box_br = Draw_LoadPic ("gfx/box_br.lmp");

	gfx_pause_lmp = Draw_LoadPic ("gfx/pause.lmp");
	gfx_loading_lmp = Draw_LoadPic ("gfx/loading.lmp");

	Menu_InitPics ();
}

cvar_t gl_consolefont ("gl_consolefont", "0", CVAR_ARCHIVE);


__inline int Draw_PrepareCharacter (int x, int y, int num)
{
	static int oldfont = -1;

	// get the correct charset to use
	if (gl_consolefont.integer != oldfont || !char_texture)
	{
		// release all except 0 which is the default texture
		for (int i = 1; i < MAX_CHAR_TEXTURES; i++)
			SAFE_RELEASE (char_textures[i]);

		// bound it
		while (gl_consolefont.integer >= MAX_CHAR_TEXTURES) gl_consolefont.integer -= MAX_CHAR_TEXTURES;
		while (gl_consolefont.integer < 0) gl_consolefont.integer += MAX_CHAR_TEXTURES;

		// load them dynamically so that we don't waste vram by having up to 49 big textures in memory at once!
		// we guaranteed that char_textures[0] is always loaded so this will always terminate
		for (;;)
		{
			// no more textures
			if (gl_consolefont.integer == 0) break;

			// attempt to load it
			bool loaded = D3D_LoadExternalTexture (&char_textures[gl_consolefont.integer], va ("charset-%i", gl_consolefont.integer), IMAGE_ALPHA);

			if (char_textures[gl_consolefont.integer] && loaded) break;

			// ensure
			SAFE_RELEASE (char_textures[gl_consolefont.integer]);

			// go to the next one
			gl_consolefont.integer--;
		}

		// set the correct font texture (this can be 0)
		char_texture = char_textures[gl_consolefont.integer];

		// store back
		Cvar_Set (&gl_consolefont, gl_consolefont.integer);
		oldfont = gl_consolefont.integer;
	}
	else if (!char_textures[gl_consolefont.integer])
	{
		char_texture = char_textures[0];
		Cvar_Set (&gl_consolefont, 0.0f);
	}

	num &= 255;

	// don't draw spaces
	if (num == 32) return 0;

	// check for offscreen
	if (y <= -8) return 0;
	if (y >= vid.height) return 0;
	if (x <= -8) return 0;
	if (x >= vid.width) return 0;

	// set correct spacing
	if (char_texture == char_textures[0])
	{
		quadcoords[num].smax = quadcoords[num].s + 0.03125f;
		quadcoords[num].tmax = quadcoords[num].t + 0.03125f;
	}
	else
	{
		quadcoords[num].smax = quadcoords[num].s + 0.0625f;
		quadcoords[num].tmax = quadcoords[num].t + 0.0625f;
	}

	// ok to draw
	return num;
}


typedef struct d3ddraw_state_s
{
	LPDIRECT3DVERTEXDECLARATION9 decl;
	DWORD addrmode;
	LPDIRECT3DTEXTURE9 stage0tex;
} d3ddraw_state_t;

d3ddraw_state_t d3ddraw_state =
{
	NULL,
	0,
	NULL
};


void D3DDraw_InitState (void)
{
	// force the state to toggle first time
	d3ddraw_state.decl = NULL;
	d3ddraw_state.addrmode = 0;
	d3ddraw_state.stage0tex = NULL;
}


void D3DDraw_StateCallback (void *data)
{
	// callback from renderer
	d3ddraw_state_t *ds = (d3ddraw_state_t *) data;

	D3D_SetVertexDeclaration (ds->decl);

	if (!ds->stage0tex)
	{
		if (d3d_GlobalCaps.usingPixelShaders)
		{
			if (d3d_FXPass == FX_PASS_NOTBEGUN)
				D3D_BeginShaderPass (FX_PASS_DRAWCOLORED);
			else if (d3d_FXPass != FX_PASS_DRAWCOLORED)
			{
				D3D_EndShaderPass ();
				D3D_BeginShaderPass (FX_PASS_DRAWCOLORED);
			}
		}
		else
		{
			// disable texturing
			D3D_SetTextureColorMode (0, D3DTOP_DISABLE);
		}
	}
	else
	{
		D3D_SetTextureAddressMode (ds->addrmode);

		if (d3d_GlobalCaps.usingPixelShaders)
		{
			if (d3d_FXPass == FX_PASS_NOTBEGUN)
				;	// do nothing (yet)
			else if (d3d_FXPass == FX_PASS_DRAWTEXTURED)
				;	// do nothing (yet)
			else D3D_EndShaderPass ();
		}
		else
		{
			D3D_SetTexture (0, ds->stage0tex);

			// need to reset explicitly here as it may have been disabled; it will be filtered
			// by D3D_SetTextureColorMode and also by the d3d runtime if it doesn't need to be set.
			D3D_SetTextureColorMode (0, D3DTOP_MODULATE);
		}

		// switch the mip mode on a texture change
		if (ds->stage0tex == ((glpic_t *) conback->data)->tex->d3d_Texture)
			D3D_SetTextureMipmap (0, d3d_TexFilter, D3DTEXF_NONE);
		else if (gl_conscale.value < 1)
		{
			if (ds->stage0tex == char_textures[0] && !r_smoothcharacters.integer)
				D3D_SetTextureMipmap (0, D3DTEXF_POINT, D3DTEXF_NONE);
			else D3D_SetTextureMipmap (0, d3d_TexFilter, D3DTEXF_NONE);
		}
		else D3D_SetTextureMipmap (0, D3DTEXF_POINT, D3DTEXF_NONE);

		if (d3d_GlobalCaps.usingPixelShaders)
		{
			d3d_MasterFX->SetTexture ("tmu0Texture", ds->stage0tex);

			if (d3d_FXPass != FX_PASS_DRAWTEXTURED)
				D3D_BeginShaderPass (FX_PASS_DRAWTEXTURED);
			else d3d_FXCommitPending = true;
		}
	}
}


void D3DDraw_ToggleState (LPDIRECT3DVERTEXDECLARATION9 decl, DWORD addrmode = 0, LPDIRECT3DTEXTURE9 stage0tex = NULL)
{
	// no state change
	if (decl == d3ddraw_state.decl && 
		addrmode == d3ddraw_state.addrmode && 
		stage0tex == d3ddraw_state.stage0tex) return;

	// set a new state
	d3ddraw_state.decl = decl;
	d3ddraw_state.addrmode = addrmode;
	d3ddraw_state.stage0tex = stage0tex;

	VBO_AddCallback (D3DDraw_StateCallback, &d3ddraw_state, sizeof (d3ddraw_state_t));
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

	D3DDraw_ToggleState (d3d_VDXyzDiffuseTex1, D3DTADDRESS_CLAMP, char_texture);

	quaddef_textured_t q =
	{
		x, 8,
		y, 8,
		d3d_2DTextureColor,
		quadcoords[num].s, quadcoords[num].smax,
		quadcoords[num].t, quadcoords[num].tmax
	};

	VBO_Add2DQuad (&q);
}


void Draw_InvertCharacter (int x, int y, int num)
{
	if (!(num = Draw_PrepareCharacter (x, y, num))) return;

	D3DDraw_ToggleState (d3d_VDXyzDiffuseTex1, D3DTADDRESS_CLAMP, char_texture);

	quaddef_textured_t q =
	{
		x, 8,
		y, 8,
		d3d_2DTextureColor,
		quadcoords[num].s, quadcoords[num].smax,
		quadcoords[num].tmax, quadcoords[num].t
	};

	VBO_Add2DQuad (&q);
}


void Draw_BackwardsCharacter (int x, int y, int num)
{
	if (!(num = Draw_PrepareCharacter (x, y, num))) return;

	D3DDraw_ToggleState (d3d_VDXyzDiffuseTex1, D3DTADDRESS_CLAMP, char_texture);

	quaddef_textured_t q =
	{
		x, 8,
		y, 8,
		d3d_2DTextureColor,
		quadcoords[num].smax, quadcoords[num].s,
		quadcoords[num].t, quadcoords[num].tmax
	};

	VBO_Add2DQuad (&q);
}


void Draw_RotateCharacter (int x, int y, int num)
{
	if (!(num = Draw_PrepareCharacter (x, y, num))) return;

	D3DDraw_ToggleState (d3d_VDXyzDiffuseTex1, D3DTADDRESS_CLAMP, char_texture);

	quaddef_textured_t q =
	{
		x, 8,
		y, 8,
		d3d_2DTextureColor,
		quadcoords[num].s, quadcoords[num].smax,
		quadcoords[num].t, quadcoords[num].tmax
	};

	VBO_Add2DQuad (&q, true);
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
	while (str[0])
	{
		Draw_Character (x, y, str[0] + ofs);
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
float Draw_SubScale (float l, float h, float xy, float wh)
{
	return 0;
}


void Draw_SubPic (int x, int y, qpic_t *pic, int srcx, int srcy, int width, int height)
{
	glpic_t *gl;
	float newsl, newtl, newsh, newth;

	gl = (glpic_t *) pic->data;

	// figure width and height of the source image
	float picw = gl->scaled_width / (gl->sh - gl->sl);
	float pich = gl->scaled_height / (gl->th - gl->tl);

	newsl = ((float) srcx / picw) + gl->sl;
	newsh = ((float) width / picw) + newsl;

	newtl = ((float) srcy / pich) + gl->tl;
	newth = ((float) height / pich) + newtl;

	D3DDraw_ToggleState (d3d_VDXyzDiffuseTex1, D3DTADDRESS_CLAMP, gl->tex->d3d_Texture);

	quaddef_textured_t q =
	{
		x, width,
		y, height,
		d3d_2DTextureColor,
		newsl, newsh,
		newtl, newth
	};

	VBO_Add2DQuad (&q);
}


void Draw_Pic (int x, int y, qpic_t *pic, float alpha)
{
	glpic_t *gl = (glpic_t *) pic->data;

	DWORD alphacolor = D3DCOLOR_ARGB (BYTE_CLAMP (alpha * 255), 255, 255, 255);

	D3DDraw_ToggleState (d3d_VDXyzDiffuseTex1, D3DTADDRESS_CLAMP, gl->tex->d3d_Texture);

	quaddef_textured_t q =
	{
		x, gl->scaled_width,
		y, gl->scaled_height,
		alphacolor,
		gl->sl, gl->sh,
		gl->tl, gl->th
	};

	VBO_Add2DQuad (&q);
}


void Draw_Pic (int x, int y, qpic_t *pic)
{
	glpic_t *gl = (glpic_t *) pic->data;

	D3DDraw_ToggleState (d3d_VDXyzDiffuseTex1, D3DTADDRESS_CLAMP, gl->tex->d3d_Texture);

	quaddef_textured_t q =
	{
		x, gl->scaled_width,
		y, gl->scaled_height,
		d3d_2DTextureColor,
		gl->sl, gl->sh,
		gl->tl, gl->th
	};

	VBO_Add2DQuad (&q);
}


void Draw_Pic (int x, int y, int w, int h, LPDIRECT3DTEXTURE9 texpic)
{
	D3DDraw_ToggleState (d3d_VDXyzDiffuseTex1, D3DTADDRESS_CLAMP, texpic);

	quaddef_textured_t q =
	{
		x, w,
		y, h,
		d3d_2DTextureColor,
		0, 1,
		0, 1
	};

	VBO_Add2DQuad (&q);
}


void Draw_Crosshair (int x, int y)
{
	// deferred loading because the crosshair texture is not yet up in Draw_Init
	if (!d3d_Crosshairs) Draw_LoadCrosshairs ();

	// we don't know about these cvars
	extern cvar_t crosshair;
	extern cvar_t scr_crosshaircolor;
	extern cvar_t scr_crosshairscale;

	// no crosshair
	if (!crosshair.integer) return;

	// get scale
	int crossscale = (int) (32.0f * scr_crosshairscale.value);

	// - 1 because crosshair 0 is no crosshair
	int currcrosshair = crosshair.integer - 1;

	// wrap it
	if (currcrosshair >= d3d_NumCrosshairs) currcrosshair -= d3d_NumCrosshairs;

	// bound colour
	if (scr_crosshaircolor.integer < 0) Cvar_Set (&scr_crosshaircolor, (float) 0.0f);
	if (scr_crosshaircolor.integer > 13) Cvar_Set (&scr_crosshaircolor, 13.0f);

	// handle backwards ranges
	int cindex = scr_crosshaircolor.integer * 16 + (scr_crosshaircolor.integer < 8 ? 15 : 0);

	// bound
	if (cindex < 0) cindex = 0;
	if (cindex > 255) cindex = 255;

	// colour for custom pics
	D3DCOLOR xhaircolor = D3DCOLOR_XRGB
	(
		d3d_QuakePalette.standard[cindex].peRed,
		d3d_QuakePalette.standard[cindex].peGreen,
		d3d_QuakePalette.standard[cindex].peBlue
	);

	// classic crosshairs are just drawn with the default colour and fixed scale
	if (currcrosshair < 2 && !d3d_Crosshairs[currcrosshair].replaced)
	{
		xhaircolor = 0xffffffff;
		crossscale = 8;
	}

	// don't draw it if too small
	if (crossscale < 2) return;

	// center it properly
	x -= (crossscale / 2);
	y -= (crossscale / 2);


	D3DDraw_ToggleState (d3d_VDXyzDiffuseTex1, D3DTADDRESS_CLAMP, d3d_Crosshairs[currcrosshair].texture);

	quaddef_textured_t q =
	{
		x, crossscale,
		y, crossscale,
		xhaircolor,
		d3d_Crosshairs[currcrosshair].l,
		d3d_Crosshairs[currcrosshair].r,
		d3d_Crosshairs[currcrosshair].t,
		d3d_Crosshairs[currcrosshair].b
	};

	VBO_Add2DQuad (&q);
}


void Draw_HalfPic (int x, int y, qpic_t *pic)
{
	glpic_t *gl = (glpic_t *) pic->data;

	D3DDraw_ToggleState (d3d_VDXyzDiffuseTex1, D3DTADDRESS_CLAMP, gl->tex->d3d_Texture);

	quaddef_textured_t q =
	{
		x, gl->scaled_width / 2,
		y, gl->scaled_height / 2,
		d3d_2DTextureColor,
		gl->sl, gl->sh,
		gl->tl, gl->th
	};

	VBO_Add2DQuad (&q);
}


void Draw_TextBox (int x, int y, int width, int height)
{
	// corners
	Draw_Pic (x, y, box_tl);
	Draw_Pic (x + width + 8, y, box_tr);
	Draw_Pic (x, y + height + 8, box_bl);
	Draw_Pic (x + width + 8, y + height + 8, box_br);

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

	D3DDraw_ToggleState (d3d_VDXyzDiffuseTex1, D3DTADDRESS_CLAMP, mstex);

	quaddef_textured_t q =
	{
		x, 128,
		y, 128,
		0xffffffff,
		0, 1,
		0, 1
	};

	VBO_Add2DQuad (&q);
}


void Draw_Mapshot (char *name, int x, int y)
{
	if (!name)
	{
		// no name supplied so display the console image instead
		glpic_t *gl = (glpic_t *) conback->data;

		// ugh, can't send it through Draw_MapshotTexture because of padding.
		// at this kind of scale integer division is hopefully no big deal
		Draw_TextBox (x - 8, y - 8, 128, 128);
		Draw_Pic (x, y, (128 * gl->scaled_width) / gl->tex->width, (128 * gl->scaled_height) / gl->tex->height, gl->tex->d3d_Texture);
		return;
	}

	if (stricmp (name, cached_name))
	{
		// save to cached name
		Q_strncpy (cached_name, name, 255);

		// texture has changed, release the existing one
		SAFE_RELEASE (d3d_MapshotTexture);

		for (int i = COM_MAXGAMES - 1;; i--)
		{
			// not registered
			if (!com_games[i]) continue;

			// attempt to load it
			if (D3D_LoadExternalTexture (&d3d_MapshotTexture, va ("%s\\%s", com_games[i], cached_name), IMAGE_KEEPPATH))
			{
				break;
			}

			if (!i)
			{
				// ensure release
				SAFE_RELEASE (d3d_MapshotTexture);

				// if we didn't load it, call recursively to display the console
				Draw_Mapshot (NULL, x, y);

				// done
				return;
			}
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
	// force an update on first entry
	static int old_shirt = -1;
	static int old_pants = -1;

	if (shirt == old_shirt && pants == old_pants)
	{
		// prevent updating if it hasn't changed
		Draw_Pic (x, y, pic);
		return;
	}

	// recache the change
	old_shirt = shirt;
	old_pants = pants;

	// update for translation
	byte *src = menuplyr_pixels;
	byte *dst = menuplyr_pixels_translated;

	// copy out the new pixels
	for (int v = 0; v < pic->height; v++, dst += pic->width, src += pic->width)
		for (int u = 0; u < pic->width; u++)
			dst[u] = translation[src[u]];

	// replace the texture fully
	glpic_t *gl = (glpic_t *) pic->data;
	SAFE_RELEASE (gl->tex->d3d_Texture);
	D3D_UploadTexture (&gl->tex->d3d_Texture, menuplyr_pixels_translated, pic->width, pic->height, (gl->tex->flags & ~IMAGE_32BIT) | IMAGE_NOCOMPRESS);

	// and draw it normally
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

	// because the conback needs scaling/etc we need to handle it specially
	glpic_t *gl = (glpic_t *) conback->data;
	float alpha;

	if (lines < y)
		alpha = (float) lines / y;
	else alpha = 1;

	DWORD alphacolor = D3DCOLOR_ARGB (BYTE_CLAMP (alpha * 255), 255, 255, 255);

	D3DDraw_ToggleState (d3d_VDXyzDiffuseTex1, D3DTADDRESS_CLAMP, gl->tex->d3d_Texture);

	quaddef_textured_t q =
	{
		0, vid.width,
		(float) lines - vid.height, vid.height,
		alphacolor,
		0, (float) conback->width / (float) gl->scaled_width,
		0, (float) conback->height / (float) gl->scaled_height
	};

	VBO_Add2DQuad (&q);
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

	D3DDraw_ToggleState (d3d_VDXyzDiffuseTex1, D3DTADDRESS_WRAP, gl->tex->d3d_Texture);

	quaddef_textured_t q =
	{
		x, w,
		y, h,
		0xffffffff,
		x / 64.0, (x + w) / 64.0,
		y / 64.0, (y + h) / 64.0,
	};

	VBO_Add2DQuad (&q);
}


/*
=============
Draw_Fill

Fills a box of pixels with a single color
=============
*/
void Draw_Fill (int x, int y, int w, int h, int c, int alpha)
{
	// this should never happen
	if (c > 255) c = 255;
	if (c < 0) c = 0;

	DWORD fillcolor = D3DCOLOR_ARGB
	(
		BYTE_CLAMP (alpha),
		d3d_QuakePalette.standard[c].peRed,
		d3d_QuakePalette.standard[c].peGreen,
		d3d_QuakePalette.standard[c].peBlue
	);

	D3DDraw_ToggleState (d3d_VDXyzDiffuse);

	quaddef_coloured_t q =
	{
		x, w,
		y, h,
		fillcolor
	};

	VBO_Add2DQuad (&q);
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

	D3DDraw_ToggleState (d3d_VDXyzDiffuse);

	quaddef_coloured_t q =
	{
		x, w,
		y, h,
		fillcolor
	};

	VBO_Add2DQuad (&q);
}


//=============================================================================

/*
================
Draw_FadeScreen

================
*/
void Draw_FadeScreen (int alpha)
{
	D3DDraw_ToggleState (d3d_VDXyzDiffuse);

	quaddef_coloured_t q =
	{
		0, vid.width,
		0, vid.height,
		D3DCOLOR_ARGB (BYTE_CLAMP (alpha), 0, 0, 0)
	};

	VBO_Add2DQuad (&q);

	Sbar_Changed ();
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

	D3DDraw_ToggleState (d3d_VDXyzDiffuse);

	// don't go down into the status bar area
	quaddef_coloured_t q =
	{
		0, vid.width,
		0, vid.height - sb_lines,
		blendcolor
	};

	VBO_Add2DQuad (&q);

	// disable polyblend in case the map changes while a blend is active
	v_blend[3] = 0;
}


void D3D_Set2DShade (float shadecolor)
{
	if (shadecolor >= 0.99f)
	{
		// solid
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
	}
}


void D3DDraw_PrepareCallback (void *blah)
{
	Scrap_Upload ();

	if (!d3d_SceneBegun)
	{
		// beginscene if necessary
		d3d_Device->BeginScene ();
		d3d_SceneBegun = true;
	}

	D3D_SetViewport (0, 0, d3d_CurrentMode.Width, d3d_CurrentMode.Height, 0, 1);

	// disable depth testing and writing
	D3D_SetRenderState (D3DRS_ZENABLE, D3DZB_FALSE);
	D3D_SetRenderState (D3DRS_ZWRITEENABLE, FALSE);

	// no backface culling
	D3D_BackfaceCull (D3DCULL_NONE);

	if (d3d_GlobalCaps.usingPixelShaders)
	{
		extern bool d3d_HLSLBegun;

		// this may not have been called by r_main
		if (!d3d_HLSLBegun)
		{
			UINT numpasses;

			d3d_MasterFX->SetTechnique ("MasterRefresh");
			d3d_MasterFX->Begin (&numpasses, D3DXFX_DONOTSAVESTATE);
			d3d_MasterFX->SetFloat ("Overbright", r_overbright.integer ? 2.0f : 1.0f);
			d3d_MasterFX->SetFloatArray ("r_origin", r_origin, 3);
			d3d_FXPass = FX_PASS_NOTBEGUN;
			d3d_FXCommitPending = false;

			d3d_HLSLBegun = true;
		}

		if (d3d_FXPass != FX_PASS_NOTBEGUN)
			D3D_EndShaderPass ();

		D3DXMATRIX m;
		D3D_LoadIdentity (&m);
		QD3DXMatrixOrthoOffCenterRH (&m, 0, vid.width, vid.height, 0, 0, 1);

		d3d_MasterFX->SetMatrix ("WorldMatrix", &m);
	}
	else
	{
		D3D_SetTexCoordIndexes (0, 0, 0);

		// revert to standard ortho projections here (simplicity/reuse of vertex decls/shift some load to gpu/etc)
		D3DXMATRIX m;
		D3D_LoadIdentity (&m);
		d3d_Device->SetTransform (D3DTS_VIEW, &m);
		d3d_Device->SetTransform (D3DTS_WORLD, &m);

		QD3DXMatrixOrthoOffCenterRH (&m, 0, vid.width, vid.height, 0, 0, 1);
		d3d_Device->SetTransform (D3DTS_PROJECTION, &m);

		// modulate alpha always here
		D3D_SetTextureColorMode (0, D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE);
		D3D_SetTextureAlphaMode (0, D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE);

		D3D_SetTextureColorMode (1, D3DTOP_DISABLE);
		D3D_SetTextureAlphaMode (1, D3DTOP_DISABLE);

		D3D_SetTextureColorMode (2, D3DTOP_DISABLE);
		D3D_SetTextureAlphaMode (2, D3DTOP_DISABLE);
	}

	// enable alpha blending
	D3D_EnableAlphaBlend (D3DBLENDOP_ADD, D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA);
}


void D3D_Set2D (void)
{
	// note - the beginscene check is in the callback
	D3DDraw_InitState ();
	VBO_AddCallback (D3DDraw_PrepareCallback);

	// do the polyblends here for simplicity
	// these won't happen in rtt mode
	Draw_PolyBlend ();
}

