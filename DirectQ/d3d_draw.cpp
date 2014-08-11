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
#include "d3d_Quads.h"

typedef struct drawrect_s
{
	float sl;
	float tl;
	float sh;
	float th;
} drawrect_t;

typedef struct
{
	LPDIRECT3DTEXTURE9 Texture;
	int flags;
	drawrect_t picrect;
	int texwidth;
	int texheight;
} glpic_t;


qpic_t *conback;

LPDIRECT3DTEXTURE9 char_texture = NULL;
cvar_t r_smoothcharacters ("r_smoothcharacters", "0", CVAR_ARCHIVE);

int d3d_DrawOfs_x = 0;
int d3d_DrawOfs_y = 0;

typedef struct d3d_drawstate_s
{
	int ShaderPass;
	D3DTEXTUREFILTERTYPE DrawFilter;
	DWORD ClampMode;
	DWORD color2D;
	LPDIRECT3DTEXTURE9 LastTexture;
} d3d_drawstate_t;

d3d_drawstate_t d3d_DrawState;

typedef struct d3d_drawsurf_s
{
	float x;
	float w;
	float y;
	float h;
	D3DCOLOR c;
	drawrect_t texrect;
} d3d_drawsurf_t;

d3d_drawsurf_t d3d_DrawSurfs[D3D_MAX_QUADS];
int d3d_NumDrawSurfs = 0;

__inline void D3DDraw_Vertex (quadvert_t *vert, float x, float y, D3DCOLOR c, float s, float t)
{
	VectorSet (vert->xyz, x + d3d_DrawOfs_x, y + d3d_DrawOfs_y, 0);

	vert->color = c;

	vert->st[0] = s;
	vert->st[1] = t;
}


void D3DDraw_DrawBatch (void)
{
	// - do an instancing thing like with particles here???
	// x/y/w/h - stbase/stadd - color
	if (d3d_NumDrawSurfs)
	{
		quadvert_t *verts = NULL;

		D3DQuads_Begin (d3d_NumDrawSurfs, &verts);

		for (int i = 0; i < d3d_NumDrawSurfs; i++, verts += 4)
		{
			d3d_drawsurf_t *ds = &d3d_DrawSurfs[i];

			D3DDraw_Vertex (&verts[0], ds->x, ds->y, ds->c, ds->texrect.sl, ds->texrect.tl);
			D3DDraw_Vertex (&verts[1], ds->x + ds->w, ds->y, ds->c, ds->texrect.sh, ds->texrect.tl);
			D3DDraw_Vertex (&verts[2], ds->x + ds->w, ds->y + ds->h, ds->c, ds->texrect.sh, ds->texrect.th);
			D3DDraw_Vertex (&verts[3], ds->x, ds->y + ds->h, ds->c, ds->texrect.sl, ds->texrect.th);
		}

		D3DQuads_End ();

		d3d_NumDrawSurfs = 0;
	}
}


void D3DDraw_SubmitQuad (float x, float y, float w, float h, D3DCOLOR c = 0xffffffff,
						 LPDIRECT3DTEXTURE9 Texture = NULL, drawrect_t *texrect = NULL, bool clampmode = false)
{
	bool switchpass = false;

	if (clampmode && d3d_DrawState.ClampMode != D3DTADDRESS_CLAMP)
	{
		D3DDraw_DrawBatch ();
		D3D_SetSamplerState (0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		D3D_SetSamplerState (0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
		d3d_DrawState.ClampMode = D3DTADDRESS_CLAMP;
	}
	else if (!clampmode && d3d_DrawState.ClampMode != D3DTADDRESS_WRAP)
	{
		D3DDraw_DrawBatch ();
		D3D_SetSamplerState (0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
		D3D_SetSamplerState (0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
		d3d_DrawState.ClampMode = D3DTADDRESS_WRAP;
	}

	// conditions for flushing/switching
	if (Texture != d3d_DrawState.LastTexture) switchpass = true;
	if (Texture && d3d_DrawState.ShaderPass != FX_PASS_DRAWTEXTURED) switchpass = true;
	if (!Texture && d3d_DrawState.ShaderPass != FX_PASS_DRAWCOLORED) switchpass = true;

	if (switchpass)
	{
		// switch texture
		D3DDraw_DrawBatch ();

		if (Texture)
		{
			D3DHLSL_SetPass (FX_PASS_DRAWTEXTURED);
			D3DHLSL_SetTexture (0, Texture);
			d3d_DrawState.ShaderPass = FX_PASS_DRAWTEXTURED;

			// set the correct filtering mode depending on the type of texture
			// the MP crew sometimes don't give a fuck for things looking right; it's all about advantage in MP for them
			if (Texture == ((glpic_t *) conback->data)->Texture)
				D3D_SetTextureMipmap (0, d3d_TexFilter);
			else if (Texture == char_texture && !r_smoothcharacters.integer)
				D3D_SetTextureMipmap (0, D3DTEXF_POINT);
			else D3D_SetTextureMipmap (0, d3d_DrawState.DrawFilter);
		}
		else
		{
			D3DHLSL_SetPass (FX_PASS_DRAWCOLORED);
			d3d_DrawState.ShaderPass = FX_PASS_DRAWCOLORED;
		}

		d3d_DrawState.LastTexture = Texture;
	}

	if (d3d_NumDrawSurfs >= D3D_MAX_QUADS) D3DDraw_DrawBatch ();

	d3d_drawsurf_t *ds = &d3d_DrawSurfs[d3d_NumDrawSurfs];
	d3d_NumDrawSurfs++;

	ds->x = x;
	ds->y = y;
	ds->w = w;
	ds->h = h;
	ds->c = c;

	if (texrect)
	{
		ds->texrect.sl = texrect->sl;
		ds->texrect.tl = texrect->tl;
		ds->texrect.sh = texrect->sh;
		ds->texrect.th = texrect->th;
	}
}


void D3DDraw_Begin2D (void);

drawrect_t charrects[256];

//  scrap allocation
//  Allocate all the little status bar obejcts into a single texture
//  to crutch up stupid hardware / drivers

// 512 x 256 is adequate for ID1; the extra space is for rogue/hipnotic extras
#define	MAX_SCRAPS		2
#define	SCRAP_WIDTH		512
#define	SCRAP_HEIGHT	512

// fixme - move to a struct, or lock the texture
int scrap_allocated[MAX_SCRAPS][SCRAP_WIDTH];
LPDIRECT3DTEXTURE9 scrap_textures[MAX_SCRAPS];


void Scrap_Init (void)
{
	for (int i = 0; i < MAX_SCRAPS; i++)
	{
		memset (scrap_allocated[i], 0, sizeof (scrap_allocated[i]));
	}
}


void Scrap_Destroy (void)
{
	for (int i = 0; i < MAX_SCRAPS; i++)
	{
		// same as above
		SAFE_RELEASE (scrap_textures[i]);
		memset (scrap_allocated[i], 0, sizeof (scrap_allocated[i]));
	}
}


/*
================
Scrap_AllocBlock

returns an index into scrap_texnums[] and the position inside it
================
*/
int Scrap_AllocBlock (int w, int h, int *x, int *y)
{
	int		i, j;
	int		best, best2;
	int		texnum;

	for (texnum = 0; texnum < MAX_SCRAPS; texnum++)
	{
		best = SCRAP_HEIGHT;

		for (i = 0; i < SCRAP_WIDTH - w; i++)
		{
			best2 = 0;

			for (j = 0; j < w; j++)
			{
				if (scrap_allocated[texnum][i + j] >= best) break;
				if (scrap_allocated[texnum][i + j] > best2) best2 = scrap_allocated[texnum][i + j];
			}

			if (j == w)
			{
				// this is a valid spot
				*x = i;
				*y = best = best2;
			}
		}

		if (best + h > SCRAP_HEIGHT)
			continue;

		for (i = 0; i < w; i++)
			scrap_allocated[texnum][*x + i] = best + h;

		return texnum;
	}

	return -1;
}


void SCR_RefdefCvarChange (cvar_t *blah);

cvar_t gl_conscale ("gl_conscale", "1", CVAR_ARCHIVE, SCR_RefdefCvarChange);
cvar_t scr_sbarscale ("scr_sbarscale", "1", CVAR_ARCHIVE, SCR_RefdefCvarChange);
cvar_t scr_menuscale ("scr_menuscale", "1", CVAR_ARCHIVE, SCR_RefdefCvarChange);
cvar_t scr_conscale ("scr_conscale", "1", CVAR_ARCHIVE, SCR_RefdefCvarChange);

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

LPDIRECT3DTEXTURE9 char_textures[MAX_CHAR_TEXTURES] = {NULL};	// if this array size is changed the release loop in D3DTexture_Release must be changed also!!!
LPDIRECT3DTEXTURE9 crosshairtexture = NULL;

typedef struct crosshair_s
{
	drawrect_t rect;
	bool replaced;
	LPDIRECT3DTEXTURE9 texture;
} crosshair_t;

int d3d_NumCrosshairs = 0;
crosshair_t *d3d_Crosshairs = NULL;


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


// temp bugfix - externals are just not loaded for these (we'll do it right next time)
qpic_t *Draw_LoadPicData (char *name, qpic_t *pic, bool allowscrap)
{
	if (pic->width < 1 || pic->height < 1)
	{
		// this occasionally gets hosed, dunno why yet...
		// seems preferable to crashing
		Con_Printf ("Draw_LoadPicData : pic->width < 1 || pic->height < 1 for %s\n(I fucked up - sorry)\n", name);
		pic = draw_failsafe;

#ifdef _DEBUG
		// prevent an exception in release builds
		assert (false);
#endif
	}

	glpic_t *gl = (glpic_t *) pic->data;
	LPDIRECT3DTEXTURE9 tex = NULL;	// setting gl->Texture at this stage will stomp data so store it separately

	// HACK HACK HACK --- we need to keep the bytes for the translatable player picture just for the menu configuration dialog
	// this should NEVER be an external texture because the translation would break
	if (!strcmp (name, "gfx/menuplyr.lmp"))
	{
		// this should only happen once
		menuplyr_pixels = (byte *) GameZone->Alloc (pic->width * pic->height);
		menuplyr_pixels_translated = (byte *) GameZone->Alloc (pic->width * pic->height);
		memcpy (menuplyr_pixels, pic->data, pic->width * pic->height);
		memcpy (menuplyr_pixels_translated, pic->data, pic->width * pic->height);
	}
	else
	{
		// try for an external texture (we can't put these into the scrap yet)
		char loadname[128];
		char *paths[] = {"textures/", "", NULL};

		strcpy (loadname, name);

		for (int i = strlen (loadname) - 1; i; i--)
		{
			if (loadname[i] == '.')
			{
				loadname[i] = 0;
				break;
			}
		}

		// try to get an external one (fixme - is this gonna leak?)
		if ((tex = D3DTexture_LoadExternal (loadname, paths, IMAGE_ALPHA)) != NULL)
		{
			// these textures don't go through the regular loader so they will leak on exit and game change
			// therefore we must register them separately.....
			D3DTexture_Register (tex, loadname);
			goto neverscrap;
		}
	}

	if (allowscrap)
	{
		int		x, y;
		int		texnum;
		D3DLOCKED_RECT lockrect;
		unsigned int *texels;
		RECT scraprect;

		// pad the allocation to prevent linear filtering from causing the edges of adjacent textures to bleed into each other
		int scrapw = pic->width + 4;
		int scraph = pic->height + 4;

		if (scrapw > SCRAP_WIDTH) goto noscrap;
		if (scraph > SCRAP_HEIGHT) goto noscrap;

		// find a padded block
		if ((texnum = Scrap_AllocBlock (scrapw, scraph, &x, &y)) == -1) goto noscrap;

		scraprect.left = x;
		scraprect.right = x + scrapw;
		scraprect.top = y;
		scraprect.bottom = y + scraph;

		// center in the padded region
		x += 2;
		y += 2;

		if (!scrap_textures[texnum])
		{
			hr = d3d_Device->CreateTexture
			(
				SCRAP_WIDTH,
				SCRAP_HEIGHT,
				1,
				0,
				D3DFMT_A8R8G8B8,
				D3DPOOL_MANAGED,
				&scrap_textures[texnum],
				NULL
			);

			if (FAILED (hr)) Sys_Error ("Draw_LoadPicData : Failed to allocate a texture");
		}

		scrap_textures[texnum]->LockRect (0, &lockrect, NULL, d3d_GlobalCaps.DynamicLock);
		texels = (unsigned int *) lockrect.pBits;

		// pad up/down/left/right so that the correct texels will be caught when filtering
		for (int i = 0, k = 0; i < pic->height; i++)
			for (int j = 0; j < pic->width; j++, k++)
				texels[((y - 1) + i) * SCRAP_WIDTH + x + j] = d3d_QuakePalette.standard32[pic->data[k]];

		for (int i = 0, k = 0; i < pic->height; i++)
			for (int j = 0; j < pic->width; j++, k++)
				texels[((y + 1) + i) * SCRAP_WIDTH + x + j] = d3d_QuakePalette.standard32[pic->data[k]];

		for (int i = 0, k = 0; i < pic->height; i++)
			for (int j = 0; j < pic->width; j++, k++)
				texels[(y + i) * SCRAP_WIDTH + (x - 1) + j] = d3d_QuakePalette.standard32[pic->data[k]];

		for (int i = 0, k = 0; i < pic->height; i++)
			for (int j = 0; j < pic->width; j++, k++)
				texels[(y + i) * SCRAP_WIDTH + (x + 1) + j] = d3d_QuakePalette.standard32[pic->data[k]];

		// do the final centered image
		for (int i = 0, k = 0; i < pic->height; i++)
			for (int j = 0; j < pic->width; j++, k++)
				texels[(y + i) * SCRAP_WIDTH + x + j] = d3d_QuakePalette.standard32[pic->data[k]];

		scrap_textures[texnum]->UnlockRect (0);
		scrap_textures[texnum]->AddDirtyRect (&scraprect);

		gl->Texture = scrap_textures[texnum];

		// scrap textures just go at their default sizes
		gl->picrect.sl = (float) x / (float) SCRAP_WIDTH;
		gl->picrect.sh = (float) (x + pic->width) / (float) SCRAP_WIDTH;
		gl->picrect.tl = (float) y / (float) SCRAP_HEIGHT;
		gl->picrect.th = (float) (y + pic->height) / (float) SCRAP_HEIGHT;

		gl->texwidth = SCRAP_WIDTH;
		gl->texheight = SCRAP_HEIGHT;

		// that's all we need
		return pic;
	}

noscrap:;
	// load with no scrap
	if (!strcmp (name, "gfx/conback.lmp"))
		tex = D3DTexture_Load (name, pic->width, pic->height, pic->data, 0);
	else tex = D3DTexture_Load (name, pic->width, pic->height, pic->data, IMAGE_ALPHA | IMAGE_PADDABLE);

neverscrap:;
	D3DSURFACE_DESC texdesc;

	gl->Texture = tex;
	tex->GetLevelDesc (0, &texdesc);

	// rescale everything correctly
	gl->picrect.sl = 0;
	gl->picrect.tl = 0;

	// different scale factors here
	if (d3d_GlobalCaps.supportNonPow2)
	{
		gl->picrect.sh = 1;
		gl->picrect.th = 1;
	}
	else
	{
		gl->picrect.sh = (float) pic->width / (float) texdesc.Width;
		gl->picrect.th = (float) pic->height / (float) texdesc.Height;
	}

	gl->texwidth = texdesc.Width;
	gl->texheight = texdesc.Height;

	return pic;
}


qpic_t *Draw_LoadPic (char *name, bool allowscrap)
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

	return Draw_LoadPicData (loadname, pic, usescrap && allowscrap);
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


void D3DTexture_MakeAlpha (LPDIRECT3DTEXTURE9 tex);

void Draw_LoadCrosshairs (void)
{
	// free anything that we may have had previously
	Draw_FreeCrosshairs ();

	// load them into the scratch buffer to begin with because we don't know how many we'll have
	d3d_Crosshairs = (crosshair_t *) scratchbuf;

	// full texcoords for each value
	int xhairs[] = {0, 32, 64, 96, 0, 32, 64, 96};
	int xhairt[] = {0, 0, 0, 0, 32, 32, 32, 32};

	// load default crosshairs
	for (int i = 0; i < 10; i++)
	{
		if (i < 2)
		{
			// + sign crosshairs
			d3d_Crosshairs[i].texture = char_textures[0];
			d3d_Crosshairs[i].rect.sl = charrects['+' + (128 * i)].sl;
			d3d_Crosshairs[i].rect.tl = charrects['+' + (128 * i)].tl;
			d3d_Crosshairs[i].rect.sh = charrects['+' + (128 * i)].sh;
			d3d_Crosshairs[i].rect.th = charrects['+' + (128 * i)].th;
		}
		else
		{
			// crosshair images
			d3d_Crosshairs[i].texture = crosshairtexture;
			d3d_Crosshairs[i].rect.sl = (float) xhairs[i - 2] / 128.0f;
			d3d_Crosshairs[i].rect.tl = (float) xhairt[i - 2] / 64.0f;
			d3d_Crosshairs[i].rect.sh = (float) (xhairs[i - 2] + 32) / 128.0f;
			d3d_Crosshairs[i].rect.th = (float) (xhairt[i - 2] + 32) / 64.0f;
		}

		// we need to track if the image has been replaced so that we know to add colour to crosshair 1 and 2 if so
		d3d_Crosshairs[i].replaced = false;
	}

	// nothing here to begin with
	d3d_NumCrosshairs = 0;

	// conform to qrack (fixme - what does dp do?)
	char *paths[] = {"crosshairs/", "gfx/", "textures/crosshairs/", "textures/gfx/", NULL};

	// now attempt to load replacements
	for (int i = 0;; i++)
	{
		// attempt to load one
		LPDIRECT3DTEXTURE9 newcrosshair = NULL;

		// standard loader; qrack crosshairs begin at 1 and so should we
		if ((newcrosshair = D3DTexture_LoadExternal (va ("crosshair%i", i + 1), paths, IMAGE_ALPHA)) == NULL)
			break;

		// fill it in
		d3d_Crosshairs[i].texture = newcrosshair;
		d3d_Crosshairs[i].rect.sl = 0;
		d3d_Crosshairs[i].rect.tl = 0;
		d3d_Crosshairs[i].rect.sh = 1;
		d3d_Crosshairs[i].rect.th = 1;
		d3d_Crosshairs[i].replaced = true;

		// mark a new crosshair
		d3d_NumCrosshairs++;
	}

	// always include the standard images
	if (d3d_NumCrosshairs < 10) d3d_NumCrosshairs = 10;

	// now set them up in memory for real - put them in the main zone so that we can free replacement textures properly
	d3d_Crosshairs = (crosshair_t *) MainZone->Alloc (d3d_NumCrosshairs * sizeof (crosshair_t));
	memcpy (d3d_Crosshairs, scratchbuf, d3d_NumCrosshairs * sizeof (crosshair_t));
}


void Draw_SpaceOutCharSet (byte *data, int w, int h)
{
	byte *buf = data;
	byte *newchars;
	int i, j, c;

	// SCR_WriteDataToTGA ("conchars.tga", data, w, h, 8, 24);

	// allocate memory for a new charset
	newchars = (byte *) scratchbuf;

	// clear to all alpha
	memset (newchars, 0, w * h * 4);

	// copy them in with better spacing
	for (i = 0, j = 0; i < w * h;)
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
			newchars[i *w + j - c] = newchars[i * w + j];
			newchars[i *w + j + c] = newchars[i * w + j];
		}
	}

	for (i = (c * 18); i < (c * 19); i++)
	{
		for (j = (c * 28); j < (c * 29); j++)
		{
			newchars[i *w + j - c] = newchars[i * w + j];
			newchars[i *w + j + c] = newchars[i * w + j];
		}
	}

	for (i = (c * 16); i < (c * 17); i++)
	{
		for (j = (c * 2); j < (c * 3); j++)
		{
			newchars[i *w + j - c] = newchars[i * w + j];
			newchars[i *w + j + c] = newchars[i * w + j];
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
	char_textures[0] = D3DTexture_Upload (newchars, 256, 256, IMAGE_ALPHA);
}


void Draw_SetCharrects (int h, int v, int addx, int addy)
{
	int yadd = v / 16;
	int xadd = h / 16;

	for (int y = 0, z = 0; y < v; y += yadd)
	{
		for (int x = 0; x < h; x += xadd, z++)
		{
			charrects[z].sl = (float) x / (float) h;
			charrects[z].sh = (float) (x + addx) / (float) h;
			charrects[z].tl = (float) y / (float) v;
			charrects[z].th = (float) (y + addy) / (float) v;
		}
	}
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
	// prevent double-calling; this was a crasher in ye olde dayes
	if (draw_init) return;

	draw_init = true;

	// default chaaracter rectangles for the standard charset
	Draw_SetCharrects (256, 256, 8, 8);

	// Draw_DumpLumps ();

	// initialize scrap textures
	Scrap_Init ();

	if (!failsafedata)
	{
		// this is a qpic_t that's used in the event of any qpic_t failing to load!!!
		// we alloc enough memory for the glpic_t that draw_failsafe->data is casted to.
		// this crazy-assed shit prevents an "ambiguous call to overloaded function" error.
		// add 1 cos of integer round down.  do it this way in case we ever change the glpic_t struct
		int failsafedatasize = D3DTexture_PowerOf2Size ((int) sqrt ((float) (sizeof (glpic_t))) + 1);

		// persist in memory
		failsafedata = (byte *) Zone_Alloc (sizeof (int) * 2 + (failsafedatasize * failsafedatasize));
		draw_failsafe = (qpic_t *) failsafedata;
		draw_failsafe->height = failsafedatasize;
		draw_failsafe->width = failsafedatasize;
	}

	draw_chars = (byte *) W_GetLumpName ("conchars");
	Draw_SpaceOutCharSet (draw_chars, 128, 128);

	conback = Draw_LoadPic ("gfx/conback.lmp", false);

	// get the other pics we need
	// draw_disc is also used on the sbar so we need to retain it
	draw_disc = Draw_LoadPic ("disc");
	draw_backtile = Draw_LoadPic ("backtile", false);

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

float font_scale_x = 1, font_scale_y = 1;

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

		char *paths[] =
		{
			"textures/charsets/",
			"textures/gfx/",
			"gfx/",
			NULL
		};

		// load them dynamically so that we don't waste vram by having up to 49 big textures in memory at once!
		// we guaranteed that char_textures[0] is always loaded so this will always terminate
		for (;;)
		{
			// no more textures
			if (gl_consolefont.integer == 0) break;

			// attempt to load it
			if ((char_textures[gl_consolefont.integer] = D3DTexture_LoadExternal (va ("charset-%i", gl_consolefont.integer), paths, IMAGE_ALPHA)) != NULL) break;

			// go to the next one
			gl_consolefont.integer--;
		}

		// set the correct font texture (this can be 0)
		char_texture = char_textures[gl_consolefont.integer];

		// store back
		Cvar_Set (&gl_consolefont, gl_consolefont.integer);
		oldfont = gl_consolefont.integer;

		// to do - set up character rectangles (and scales for external charsets!)
		if (gl_consolefont.integer > 0)
		{
			LPDIRECT3DSURFACE9 texsurf;
			D3DSURFACE_DESC texdesc;
			char_texture->GetSurfaceLevel (0, &texsurf);
			texsurf->GetDesc (&texdesc);
			Draw_SetCharrects (texdesc.Width, texdesc.Height, texdesc.Width / 16, texdesc.Height / 16);
			font_scale_x = 128.0f / (float) texdesc.Width;
			font_scale_y = 128.0f / (float) texdesc.Height;
			texsurf->Release ();
		}
		else
		{
			Draw_SetCharrects (256, 256, 8, 8);
			font_scale_x = font_scale_y = 1;
		}
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
	if (y >= vid.currsize->height) return 0;
	if (x <= -8) return 0;
	if (x >= vid.currsize->width) return 0;

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

	D3DDraw_SubmitQuad (x, y, 8, 8, d3d_DrawState.color2D, char_texture, &charrects[num], true);
}


void Draw_BackwardsCharacter (int x, int y, int num)
{
	if (!(num = Draw_PrepareCharacter (x, y, num))) return;

	D3DDraw_SubmitQuad (x + 8, y, -8, 8, d3d_DrawState.color2D, char_texture, &charrects[num], true);
}


// oh well; it wasn't really used anymore anyway...
void Draw_RotateCharacter (int x, int y, int num) {}
void Draw_VScrollBar (int x, int y, int height, int pos, int scale) {}


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
	Draw_Character (vid.currsize->width - 20, 20, num);
}


/*
=============
Draw_Pic
=============
*/
void Draw_SubPic (int x, int y, qpic_t *pic, int srcx, int srcy, int width, int height)
{
	glpic_t *gl = (glpic_t *) pic->data;

	drawrect_t rect =
	{
		gl->picrect.sl + (float) srcx / (float) gl->texwidth,
		gl->picrect.tl + (float) srcy / (float) gl->texheight,
		gl->picrect.sl + (float) (srcx + width) / (float) gl->texwidth,
		gl->picrect.tl + (float) (srcy + height) / (float) gl->texheight
	};

	D3DDraw_SubmitQuad (x, y, width, height, d3d_DrawState.color2D, gl->Texture, &rect, true);
}


void Draw_Pic (int x, int y, qpic_t *pic, float alpha, bool clamp)
{
	glpic_t *gl = (glpic_t *) pic->data;
	DWORD alphacolor;

	if (alpha < 1)
		alphacolor = D3DCOLOR_ARGB (BYTE_CLAMPF (alpha), 255, 255, 255);
	else alphacolor = d3d_DrawState.color2D;

	D3DDraw_SubmitQuad (x, y, pic->width, pic->height, alphacolor, gl->Texture, &gl->picrect, clamp);
}


void Draw_Pic (int x, int y, int w, int h, LPDIRECT3DTEXTURE9 texpic)
{
	drawrect_t rect = {0, 0, 1, 1};

	D3DDraw_SubmitQuad (x, y, w, h, d3d_DrawState.color2D, texpic, &rect);
}


void Draw_Crosshair (int x, int y)
{
	// deferred loading because the crosshair texture is not yet up in Draw_Init
	if (!d3d_Crosshairs)
	{
		D3DDraw_DrawBatch ();
		Draw_LoadCrosshairs ();
	}

	// we don't know about these cvars
	extern cvar_t crosshair;
	extern cvar_t scr_crosshaircolor;
	extern cvar_t scr_crosshairscale;

	// no crosshair
	if (!crosshair.integer) return;

	// get scale
	float crossscale = (32.0f * scr_crosshairscale.value);

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

	// classic crosshair
	if (currcrosshair < 2 && !d3d_Crosshairs[currcrosshair].replaced)
	{
		Draw_Character (x - 4, y - 4, '+' + 128 * currcrosshair);
		return;
	}

	// don't draw it if too small
	if (crossscale < 2) return;

	// center it properly
	x -= (crossscale / 2);
	y -= (crossscale / 2);

	D3DDraw_SubmitQuad (x, y, crossscale, crossscale, xhaircolor, d3d_Crosshairs[currcrosshair].texture, &d3d_Crosshairs[currcrosshair].rect, true);
}


void Draw_TextBox (int x, int y, int width, int height)
{
	// corners
	Draw_Pic (x, y, box_tl, 1, true);
	Draw_Pic (x + width + 8, y, box_tr, 1, true);
	Draw_Pic (x, y + height + 8, box_bl, 1, true);
	Draw_Pic (x + width + 8, y + height + 8, box_br, 1, true);

	// left and right sides
	for (int i = 8; i < height; i += 8) Draw_Pic (x, y + i, box_ml, 1, true);
	Draw_Pic (x, y + height, box_ml, 1, true);
	for (int i = 8; i < height; i += 8) Draw_Pic (x + width + 8, y + i, box_mr, 1, true);
	Draw_Pic (x + width + 8, y + height, box_mr, 1, true);

	// top and bottom sides
	for (int i = 16; i < width; i += 8) Draw_Pic (x + i - 8, y, box_tm, 1, true);
	Draw_Pic (x + width - 8, y, box_tm, 1, true);
	for (int i = 16; i < width; i += 8) Draw_Pic (x + i - 8, y + height + 8, box_bm, 1, true);
	Draw_Pic (x + width - 8, y + height + 8, box_bm, 1, true);

	glpic_t *gl = (glpic_t *) box_mm2->data;

	drawrect_t texrect =
	{
		0, 0,
		(float) width / (float) box_mm2->width,
		(float) height / (float) box_mm2->height
	};

	// fill it
	D3DDraw_SubmitQuad (x + 8, y + 8, width, height, 0xffffffff, gl->Texture, &texrect);
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
	Draw_Pic (x, y, 128, 128, mstex);
}


void Draw_Mapshot (char *name, int x, int y)
{
	// flush drawing because we're updating textures here
	D3DDraw_DrawBatch ();

	if (!name)
	{
		// no name supplied so display the console image instead
		glpic_t *gl = (glpic_t *) conback->data;

		// the conback is unpadded so use regular texcoords
		Draw_TextBox (x - 8, y - 8, 128, 128);
		Draw_Pic (x, y, 128, 128, gl->Texture);
		return;
	}

	if (_stricmp (name, cached_name))
	{
		// save to cached name
		Q_strncpy (cached_name, name, 255);

		// texture has changed, release the existing one
		SAFE_RELEASE (d3d_MapshotTexture);

		char *paths[] = {"", NULL};

		for (int j = strlen (cached_name) - 1; j; j--)
		{
			if (cached_name[j] == '.')
			{
				cached_name[j] = 0;
				break;
			}
		}

		// attempt to load it
		if ((d3d_MapshotTexture = D3DTexture_LoadExternal (cached_name, paths, 0)) == NULL)
		{
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
	// flush drawing because we're updating textures here
	D3DDraw_DrawBatch ();

	// force an update on first entry
	static int old_shirt = -1;
	static int old_pants = -1;

	if (shirt == old_shirt && pants == old_pants)
	{
		// prevent updating if it hasn't changed
		Draw_Pic (x, y, pic, 1, true);
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
	SAFE_RELEASE (gl->Texture);

	gl->Texture = D3DTexture_Upload (menuplyr_pixels_translated,
		pic->width, pic->height, IMAGE_ALPHA);

	// and draw it normally
	Draw_Pic (x, y, pic, 1, true);
}


/*
================
Draw_ConsoleBackground

================
*/
void Draw_ConsoleBackground (int percent)
{
	float alpha = (float) percent / 75.0f;
	glpic_t *gl = (glpic_t *) conback->data;
	D3DCOLOR consolecolor = D3DCOLOR_ARGB (BYTE_CLAMPF (alpha), 255, 255, 255);

	// the conback image is unpadded so use regular texcoords
	drawrect_t rect = {0, 1.0f - ((float) percent / 100.0f), 1, 1};

	D3DDraw_SubmitQuad (0, 0, vid.currsize->width, (float) vid.currsize->height * ((float) percent / 100.0f), consolecolor, gl->Texture, &rect, true);
}


/*
=============
Draw_TileClear

This repeats a 64*64 tile graphic to fill the screen around a sized down
refresh window.  Only drawn when an update is needed and the full screen is
covered to make certain that we get everything.
=============
*/
void Draw_TileClear (float x, float y, float w, float h)
{
	glpic_t *gl = (glpic_t *) draw_backtile->data;
	drawrect_t rect = {x / 64.0f, y / 64.0f, (x + w) / 64.0f, (y + h) / 64.0f};

	D3DDraw_SubmitQuad (x, y, w, h, 0xffffffff, gl->Texture, &rect, false);
}


void Draw_TileClear (void)
{
	extern cvar_t cl_sbar;
	extern cvar_t scr_centersbar;

	if (cls.state != ca_connected) return;
	if (cl_sbar.integer) return;

	// do it proper for left-aligned HUDs
	if (scr_centersbar.integer)
	{
		int width = (vid.currsize->width - 320) >> 1;
		int offset = width + 320;

		Draw_TileClear (0, vid.currsize->height - sb_lines, width, sb_lines);
		Draw_TileClear (offset, vid.currsize->height - sb_lines, width, sb_lines);
	}
	else Draw_TileClear (320, vid.currsize->height - sb_lines, vid.currsize->width - 320, sb_lines);
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

	D3DDraw_SubmitQuad (x, y, w, h, fillcolor);
}


void Draw_Fill (int x, int y, int w, int h, float r, float g, float b, float alpha)
{
	DWORD fillcolor = D3DCOLOR_ARGB
	(
		BYTE_CLAMPF (alpha),
		BYTE_CLAMPF (r),
		BYTE_CLAMPF (g),
		BYTE_CLAMPF (b)
	);

	D3DDraw_SubmitQuad (x, y, w, h, fillcolor);
}


//=============================================================================

/*
================
Draw_FadeScreen

================
*/
void Draw_FadeScreen (int alpha)
{
	D3DDraw_SubmitQuad (0, 0, vid.currsize->width, vid.currsize->height, D3DCOLOR_ARGB (BYTE_CLAMP (alpha), 0, 0, 0));
}


//=============================================================================

void D3D_Set2DShade (float shadecolor)
{
	if (shadecolor >= 0.99f)
	{
		// solid
		d3d_DrawState.color2D = 0xffffffff;
	}
	else
	{
		// 0 to 255 scale
		shadecolor *= 255.0f;

		// fade out
		d3d_DrawState.color2D = D3DCOLOR_ARGB
		(
			BYTE_CLAMP (shadecolor),
			BYTE_CLAMP (shadecolor),
			BYTE_CLAMP (shadecolor),
			BYTE_CLAMP (shadecolor)
		);
	}
}


void D3DDraw_Begin2D (void)
{
	// disable depth testing and writing
	D3DState_SetZBuffer (D3DZB_FALSE, FALSE);

	// no backface culling
	D3D_BackfaceCull (D3DCULL_NONE);

	// alwats switch to a full-sized viewport even if we're drawing nothing so that
	// IDirect3DDevice9::Clear can do a fast clear in the next frame
	D3D_SetViewport (0, 0, d3d_CurrentMode.Width, d3d_CurrentMode.Height, 0, 1);

	// end our RTT scene here first
	D3DRTT_EndScene ();

	// backtile needs wrap mode explicitly
	D3D_SetTextureAddress (0, D3DTADDRESS_WRAP);

	// enable alpha blending (always)
	D3DState_SetAlphaBlend (TRUE);

	// set up shader for drawing; always force a pass switch first time
	d3d_DrawState.ShaderPass = FX_PASS_NOTBEGUN;
	d3d_DrawState.color2D = 0xffffffff;
	d3d_DrawState.DrawFilter = d3d_TexFilter;
	d3d_DrawState.ClampMode = D3DTADDRESS_FORCE_DWORD;
	d3d_DrawState.LastTexture = NULL;

	// clear size to force a recache as the world matrix always needs to be updated for the first 2D draw
	vid.currsize = NULL;
}


void D3DDraw_End2D (void)
{
	D3DDraw_DrawBatch ();
	d3d_DrawState.color2D = 0xffffffff;
}


void D3DDraw_SetSize (sizedef_t *size)
{
	if (vid.currsize && vid.currsize->width == size->width && vid.currsize->height == size->height)
		return;

	// flush any pending drawing because this is a state change
	D3DDraw_DrawBatch ();

	// reset the size to that which we're going to use
	D3DHLSL_SetFloatArray ("drawmult", D3DXVECTOR4 (2.0f / size->width, -(2.0f / size->height), 0, 1.0f), 4);
	D3DHLSL_SetFloatArray ("drawadd", D3DXVECTOR4 (-1, 1, 0, 0), 4);
	D3DHLSL_SetFloatArray ("halfpixoffset", D3DXVECTOR4 (-0.5f, -0.5f, 0, 0), 4);

	// and store it out
	vid.currsize = size;
}


void D3DDraw_SetOfs (int x, int y)
{
	// flush any pending drawing because this is a state change
	D3DDraw_DrawBatch ();

	d3d_DrawOfs_x = x;
	d3d_DrawOfs_y = y;
}

