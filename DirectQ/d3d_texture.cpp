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


#include "quakedef.h"
#include "d3d_model.h"
#include "d3d_quake.h"
#include "resource.h"
#include <vector>

typedef struct image_s
{
	char identifier[64];
	int width;
	int height;
	byte *data;
	byte hash[16];
	int flags;
	int LastUsage;
	LPDIRECT3DTEXTURE9 d3d_Texture;
} image_t;


typedef struct d3d_filtermode_s
{
	char *name;
	D3DTEXTUREFILTERTYPE texfilter;
	D3DTEXTUREFILTERTYPE mipfilter;
} d3d_filtermode_t;

d3d_filtermode_t d3d_filtermodes[] =
{
	{"GL_NEAREST", D3DTEXF_POINT, D3DTEXF_NONE},
	{"GL_LINEAR", D3DTEXF_LINEAR, D3DTEXF_NONE},
	{"GL_NEAREST_MIPMAP_NEAREST", D3DTEXF_POINT, D3DTEXF_POINT},
	{"GL_LINEAR_MIPMAP_NEAREST", D3DTEXF_LINEAR, D3DTEXF_POINT},
	{"GL_NEAREST_MIPMAP_LINEAR", D3DTEXF_POINT, D3DTEXF_LINEAR},
	{"GL_LINEAR_MIPMAP_LINEAR", D3DTEXF_LINEAR, D3DTEXF_LINEAR}
};

D3DTEXTUREFILTERTYPE d3d_TexFilter = D3DTEXF_LINEAR;
D3DTEXTUREFILTERTYPE d3d_MipFilter = D3DTEXF_LINEAR;

void D3DVid_TextureMode_f (void)
{
	if (Cmd_Argc () == 1)
	{
		D3DTEXTUREFILTERTYPE texfilter = d3d_TexFilter;
		D3DTEXTUREFILTERTYPE mipfilter = d3d_MipFilter;

		Con_Printf ("Available Filters:\n");

		for (int i = 0; i < 6; i++)
			Con_Printf ("%i: %s\n", i, d3d_filtermodes[i].name);

		for (int i = 0; i < 6; i++)
		{
			if (texfilter == d3d_filtermodes[i].texfilter && mipfilter == d3d_filtermodes[i].mipfilter)
			{
				Con_Printf ("\nCurrent filter: %s\n", d3d_filtermodes[i].name);
				return;
			}
		}

		Con_Printf ("current filter is unknown???\n");
		Con_Printf ("Texture filter: %s\n", D3DTypeToString (d3d_TexFilter));
		Con_Printf ("Mipmap filter:  %s\n", D3DTypeToString (d3d_MipFilter));
		return;
	}

	char *desiredmode = Cmd_Argv (1);
	int modenum = desiredmode[0] - '0';

	for (int i = 0; i < 6; i++)
	{
		if (!_stricmp (d3d_filtermodes[i].name, desiredmode) || i == modenum)
		{
			// reset filter
			d3d_TexFilter = d3d_filtermodes[i].texfilter;
			d3d_MipFilter = d3d_filtermodes[i].mipfilter;

			Con_Printf ("Texture filter: %s\n", D3DTypeToString (d3d_TexFilter));
			Con_Printf ("Mipmap filter:  %s\n", D3DTypeToString (d3d_MipFilter));
			return;
		}
	}

	Con_Printf ("bad filter name\n");
}


void D3DVid_SaveTextureMode (FILE *f)
{
	for (int i = 0; i < 6; i++)
	{
		if (d3d_TexFilter == d3d_filtermodes[i].texfilter && d3d_MipFilter == d3d_filtermodes[i].mipfilter)
		{
			fprintf (f, "gl_texturemode %s\n", d3d_filtermodes[i].name);
			return;
		}
	}
}


void D3DVid_TexMem_f (void)
{
	Con_Printf ("Available Texture Memory: %i MB\n", (d3d_Device->GetAvailableTextureMem ()) / (1024 * 1024));
}


cmd_t D3DVid_TexMem_Cmd ("gl_videoram", D3DVid_TexMem_f);
cmd_t D3DVid_TextureMode_Cmd ("gl_texturemode", D3DVid_TextureMode_f);

void D3DVid_ValidateTextureSizes (void)
{
	LPDIRECT3DTEXTURE9 tex = NULL;

	for (int s = d3d_DeviceCaps.MaxTextureWidth;; s >>= 1)
	{
		if (s < 256)
		{
			Sys_Error ("Could not create a 256x256 texture");
			return;
		}

		hr = d3d_Device->CreateTexture (s, 256, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, NULL);

		if (FAILED (hr))
		{
			tex = NULL;
			continue;
		}

		d3d_DeviceCaps.MaxTextureWidth = s;
		SAFE_RELEASE (tex);
		break;
	}

	for (int s = d3d_DeviceCaps.MaxTextureHeight;; s >>= 1)
	{
		if (s < 256)
		{
			Sys_Error ("Could not create a 256x256 texture");
			return;
		}

		hr = d3d_Device->CreateTexture (256, s, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, NULL);

		if (FAILED (hr))
		{
			tex = NULL;
			continue;
		}

		d3d_DeviceCaps.MaxTextureHeight = s;
		SAFE_RELEASE (tex);
		break;
	}
}


extern LPDIRECT3DTEXTURE9 d3d_PaletteRowTextures[];

cvar_t gl_maxtextureretention ("gl_maxtextureretention", 3, CVAR_ARCHIVE);

std::vector<image_t *> d3d_TextureList;


void D3DTexture_GenerateMipLevels (unsigned *data, int width, int height, int flags, LPDIRECT3DTEXTURE9 tex)
{
	DWORD mips = tex->GetLevelCount ();
	D3DLOCKED_RECT lockrect;

	// copy in miplevel 0
	if (SUCCEEDED (tex->LockRect (0, &lockrect, NULL, d3d_GlobalCaps.DynamicLock)))
	{
		// copy this in properly using pitch
		unsigned *dst = (unsigned *) lockrect.pBits;

		for (int h = 0; h < height; h++)
		{
			for (int w = 0; w < width; w++)
				dst[w] = data[w];

			data += width;
			dst += (lockrect.Pitch >> 2);
		}

		tex->UnlockRect (0);

		// filter the rest (needed for NP2 support)
		if (mips > 1) D3DXFilterTexture (tex, NULL, 0, D3DX_FILTER_BOX);
	}
}


// other textures we use
extern LPDIRECT3DTEXTURE9 char_texture;
extern LPDIRECT3DTEXTURE9 char_textures[];

// palette hackery
palettedef_t d3d_QuakePalette;


void D3DTexture_MakeQuakePalettes (byte *palette)
{
	int dark = 21024;
	int darkindex = 0;

	for (int i = 0; i < 256; i++, palette += 3)
	{
		// on disk palette has 3 components
		byte *rgb = palette;

		if (i < 255)
		{
			int darkness = ((int) rgb[0] + (int) rgb[1] + (int) rgb[2]) / 3;

			if (darkness < dark)
			{
				dark = darkness;
				darkindex = i;
			}
		}
		else rgb[0] = rgb[1] = rgb[2] = 0;

		// set correct alpha colour
		byte alpha = (i < 255) ? 255 : 0;

		// per doc, peFlags is used for alpha
		// invert again so that the layout will be correct
		d3d_QuakePalette.standard[i].peRed = rgb[0];
		d3d_QuakePalette.standard[i].peGreen = rgb[1];
		d3d_QuakePalette.standard[i].peBlue = rgb[2];
		d3d_QuakePalette.standard[i].peFlags = alpha;

		if (i > 223)
		{
			d3d_QuakePalette.luma[i].peRed = rgb[0];
			d3d_QuakePalette.luma[i].peGreen = rgb[1];
			d3d_QuakePalette.luma[i].peBlue = rgb[2];
			d3d_QuakePalette.luma[i].peFlags = alpha;
		}
		else
		{
			d3d_QuakePalette.luma[i].peRed = 0;
			d3d_QuakePalette.luma[i].peGreen = 0;
			d3d_QuakePalette.luma[i].peBlue = 0;
			d3d_QuakePalette.luma[i].peFlags = alpha;
		}

		d3d_QuakePalette.standard32[i] = D3DCOLOR_ARGB (alpha, rgb[0], rgb[1], rgb[2]);

		d3d_QuakePalette.colorfloat[i][0] = (float) rgb[0] / 255.0f;
		d3d_QuakePalette.colorfloat[i][1] = (float) rgb[1] / 255.0f;
		d3d_QuakePalette.colorfloat[i][2] = (float) rgb[2] / 255.0f;
		d3d_QuakePalette.colorfloat[i][3] = (float) alpha / 255.0f;
	}

	// set index of darkest colour
	d3d_QuakePalette.darkindex = darkindex;
}


int D3DTexture_PowerOf2Size (int size)
{
	size--;
	size |= size >> 1;
	size |= size >> 2;
	size |= size >> 4;
	size |= size >> 8;
	size |= size >> 16;
	size++;

	return size;
}


bool d3d_ImagePadded = false;

void D3DTexture_Transfer8Bit (byte *src, unsigned *dst, int size, unsigned *palette)
{
	int n = (size + 7) >> 3;
	size %= 8;

	switch (size)
	{
	case 0: do {*dst++ = palette[*src++];
	case 7: *dst++ = palette[*src++];
	case 6: *dst++ = palette[*src++];
	case 5: *dst++ = palette[*src++];
	case 4: *dst++ = palette[*src++];
	case 3: *dst++ = palette[*src++];
	case 2: *dst++ = palette[*src++];
	case 1: *dst++ = palette[*src++];
	} while (--n > 0);
	}
}


void D3DTexture_Transfer32Bit (unsigned *src, unsigned *dst, int size)
{
	int n = (size + 7) >> 3;
	size %= 8;

	switch (size)
	{
	case 0: do {*dst++ = *src++;
	case 7: *dst++ = *src++;
	case 6: *dst++ = *src++;
	case 5: *dst++ = *src++;
	case 4: *dst++ = *src++;
	case 3: *dst++ = *src++;
	case 2: *dst++ = *src++;
	case 1: *dst++ = *src++;
	} while (--n > 0);
	}
}


void D3DTexture_Resample8Bit (byte *in, int inwidth, int inheight, unsigned *out, int outwidth, int outheight, unsigned *palette)
{
	int		i, j;
	byte	*inrow, *inrow2;
	unsigned	frac, fracstep;
	unsigned	*p1, *p2;
	byte		*pix1, *pix2, *pix3, *pix4;

	p1 = (unsigned *) MainHunk->Alloc (outwidth * 4);
	p2 = (unsigned *) MainHunk->Alloc (outwidth * 4);

	fracstep = inwidth * 0x10000 / outwidth;
	frac = fracstep >> 2;

	for (i = 0; i < outwidth; i++)
	{
		p1[i] = (frac >> 16);
		frac += fracstep;
	}

	frac = 3 * (fracstep >> 2);

	for (i = 0; i < outwidth; i++)
	{
		p2[i] = (frac >> 16);
		frac += fracstep;
	}

	for (i = 0; i < outheight; i++, out += outwidth)
	{
		inrow = in + inwidth * (int) ((i + 0.25) * inheight / outheight);
		inrow2 = in + inwidth * (int) ((i + 0.75) * inheight / outheight);
		frac = fracstep >> 1;

		for (j = 0; j < outwidth; j++)
		{
			pix1 = (byte *) &palette[inrow[p1[j]]];
			pix2 = (byte *) &palette[inrow[p2[j]]];
			pix3 = (byte *) &palette[inrow2[p1[j]]];
			pix4 = (byte *) &palette[inrow2[p2[j]]];

			((byte *) (out + j))[0] = (pix1[0] + pix2[0] + pix3[0] + pix4[0]) >> 2;
			((byte *) (out + j))[1] = (pix1[1] + pix2[1] + pix3[1] + pix4[1]) >> 2;
			((byte *) (out + j))[2] = (pix1[2] + pix2[2] + pix3[2] + pix4[2]) >> 2;
			((byte *) (out + j))[3] = (pix1[3] + pix2[3] + pix3[3] + pix4[3]) >> 2;
		}
	}
}


void D3DTexture_Resample32Bit (unsigned *in, int inwidth, int inheight, unsigned *out, int outwidth, int outheight)
{
	int		i, j;
	unsigned	*inrow, *inrow2;
	unsigned	frac, fracstep;
	unsigned	*p1, *p2;
	byte		*pix1, *pix2, *pix3, *pix4;

	p1 = (unsigned *) MainHunk->Alloc (outwidth * 4);
	p2 = (unsigned *) MainHunk->Alloc (outwidth * 4);

	fracstep = inwidth * 0x10000 / outwidth;
	frac = fracstep >> 2;

	for (i = 0; i < outwidth; i++)
	{
		p1[i] = 4 * (frac >> 16);
		frac += fracstep;
	}

	frac = 3 * (fracstep >> 2);

	for (i = 0; i < outwidth; i++)
	{
		p2[i] = 4 * (frac >> 16);
		frac += fracstep;
	}

	for (i = 0; i < outheight; i++, out += outwidth)
	{
		inrow = in + inwidth * (int) ((i + 0.25) * inheight / outheight);
		inrow2 = in + inwidth * (int) ((i + 0.75) * inheight / outheight);
		frac = fracstep >> 1;

		for (j = 0; j < outwidth; j++)
		{
			pix1 = (byte *) inrow + p1[j];
			pix2 = (byte *) inrow + p2[j];
			pix3 = (byte *) inrow2 + p1[j];
			pix4 = (byte *) inrow2 + p2[j];

			((byte *) (out + j))[0] = (pix1[0] + pix2[0] + pix3[0] + pix4[0]) >> 2;
			((byte *) (out + j))[1] = (pix1[1] + pix2[1] + pix3[1] + pix4[1]) >> 2;
			((byte *) (out + j))[2] = (pix1[2] + pix2[2] + pix3[2] + pix4[2]) >> 2;
			((byte *) (out + j))[3] = (pix1[3] + pix2[3] + pix3[3] + pix4[3]) >> 2;
		}
	}
}


unsigned *D3DTexture_GetPalette (PALETTEENTRY *d_8to24table, int flags)
{
	static unsigned texturepal[256];
	static int lastflags = -1;

	// if the type hasn't changed just return the last palette we got
	if (flags == lastflags) return texturepal;

	// 32 bit images don't need palettes
	if (flags & IMAGE_32BIT) return texturepal;

	for (int i = 0; i < 256; i++)
	{
		byte *dst = (byte *) &texturepal[i];

		// bgra
		dst[2] = d_8to24table[i].peRed;
		dst[1] = d_8to24table[i].peGreen;
		dst[0] = d_8to24table[i].peBlue;
		dst[3] = d_8to24table[i].peFlags;
	}

	lastflags = flags;

	return texturepal;
}


void D3DTexture_Pad8Bit (byte *data, int width, int height, unsigned *padded, int scaled_width, int scaled_height, unsigned *palette)
{
	for (int y = 0; y < height; y++)
	{
		D3DTexture_Transfer8Bit (data, padded, width, palette);

		data += width;
		padded += scaled_width;
	}
}


void D3DTexture_Pad32Bit (unsigned *data, int width, int height, unsigned *padded, int scaled_width, int scaled_height)
{
	for (int y = 0; y < height; y++)
	{
		D3DTexture_Transfer32Bit (data, padded, width);

		data += width;
		padded += scaled_width;
	}
}


void D3DTexture_AlphaEdgeFix (byte *data, int width, int height)
{
	int i, j, n = 0, b, c[3] = {0, 0, 0}, lastrow, thisrow, nextrow, lastpix, thispix, nextpix;
	byte *dest = data;

	for (i = 0; i < height; i++)
	{
		lastrow = width * 4 * ((i == 0) ? height - 1 : i - 1);
		thisrow = width * 4 * i;
		nextrow = width * 4 * ((i == height - 1) ? 0 : i + 1);

		for (j = 0; j < width; j++, dest += 4)
		{
			if (dest[3]) // not transparent
				continue;

			lastpix = 4 * ((j == 0) ? width - 1 : j - 1);
			thispix = 4 * j;
			nextpix = 4 * ((j == width - 1) ? 0 : j + 1);

			b = lastrow + lastpix; if (data[b + 3]) {c[0] += data[b]; c[1] += data[b + 1]; c[2] += data[b + 2]; n++;}
			b = thisrow + lastpix; if (data[b + 3]) {c[0] += data[b]; c[1] += data[b + 1]; c[2] += data[b + 2]; n++;}
			b = nextrow + lastpix; if (data[b + 3]) {c[0] += data[b]; c[1] += data[b + 1]; c[2] += data[b + 2]; n++;}
			b = lastrow + thispix; if (data[b + 3]) {c[0] += data[b]; c[1] += data[b + 1]; c[2] += data[b + 2]; n++;}
			b = nextrow + thispix; if (data[b + 3]) {c[0] += data[b]; c[1] += data[b + 1]; c[2] += data[b + 2]; n++;}
			b = lastrow + nextpix; if (data[b + 3]) {c[0] += data[b]; c[1] += data[b + 1]; c[2] += data[b + 2]; n++;}
			b = thisrow + nextpix; if (data[b + 3]) {c[0] += data[b]; c[1] += data[b + 1]; c[2] += data[b + 2]; n++;}
			b = nextrow + nextpix; if (data[b + 3]) {c[0] += data[b]; c[1] += data[b + 1]; c[2] += data[b + 2]; n++;}

			// average all non-transparent neighbors
			if (n)
			{
				dest[0] = (byte) (c[0] / n);
				dest[1] = (byte) (c[1] / n);
				dest[2] = (byte) (c[2] / n);

				n = c[0] = c[1] = c[2] = 0;
			}
		}
	}
}


void D3DTexture_LoadData (LPDIRECT3DTEXTURE9 texture, void *data, int width, int height, int scaled_width, int scaled_height, int flags)
{
	// for hunk usage
	int hunkmark = MainHunk->GetLowMark ();

	// default to the standard palette
	PALETTEENTRY *activepal = d3d_QuakePalette.standard;

	// sky has flags & IMAGE_32BIT so it doesn't get paletteized
	// (not always - solid sky goes up as 8 bits.  it doesn't have IMAGE_BSP however so all is good
	// nehahra assumes that fullbrights are not available in the engine
	if (nehahra)
		activepal = d3d_QuakePalette.standard;
	else if ((flags & IMAGE_BSP) || (flags & IMAGE_ALIAS))
	{
		if (flags & IMAGE_LIQUID)
			activepal = d3d_QuakePalette.standard;
		else if (flags & IMAGE_LUMA)
			activepal = d3d_QuakePalette.luma;
		else activepal = d3d_QuakePalette.standard;
	}
	else activepal = d3d_QuakePalette.standard;

	// now get the final palette in a format we can transfer to the raw data
	unsigned *palette = D3DTexture_GetPalette (activepal, flags);
	unsigned *trans = NULL;

	// note - we don't check for np2 support here because we also need to account for clamping to max size
	if (scaled_width == width && scaled_height == height)
	{
		// 32 bit shouldn't alloc trans but should just copy the pointer
		if (flags & IMAGE_32BIT)
			trans = (unsigned *) data;
		else
		{
			trans = (unsigned *) MainHunk->Alloc (scaled_width * scaled_height * 4);
			D3DTexture_Transfer8Bit ((byte *) data, trans, width * height, palette);
		}
	}
	else
	{
		trans = (unsigned *) MainHunk->Alloc (scaled_width * scaled_height * 4);

		if (flags & IMAGE_PADDABLE)
		{
			// clear texture to alpha
			memset (trans, 0, scaled_width * scaled_height * 4);

			if (flags & IMAGE_32BIT)
				D3DTexture_Pad32Bit ((unsigned *) data, width, height, trans, scaled_width, scaled_height);
			else D3DTexture_Pad8Bit ((byte *) data, width, height, trans, scaled_width, scaled_height, palette);

			d3d_ImagePadded = true;
		}
		else
		{
			if (flags & IMAGE_32BIT)
				D3DTexture_Resample32Bit ((unsigned *) data, width, height, trans, scaled_width, scaled_height);
			else D3DTexture_Resample8Bit ((byte *) data, width, height, trans, scaled_width, scaled_height, palette);
		}
	}

	// it's a bug if this is still NULL
	assert (trans);

	// fix alpha edges on textures that need them
	if ((flags & IMAGE_ALPHA) || (flags & IMAGE_FENCE))
		D3DTexture_AlphaEdgeFix ((byte *) trans, scaled_width, scaled_height);

	D3DTexture_GenerateMipLevels (trans, scaled_width, scaled_height, flags, texture);

	texture->PreLoad ();
	MainHunk->FreeToLowMark (hunkmark);
}


LPDIRECT3DTEXTURE9 D3DTexture_Upload (void *data, int width, int height, int flags)
{
	LPDIRECT3DTEXTURE9 tex = NULL;

	// the scaled sizes are initially equal to the original sizes (for np2 support)
	int scaled_width = width;
	int scaled_height = height;

	// check scaling here first
	if (!d3d_GlobalCaps.supportNonPow2)
	{
		scaled_width = D3DTexture_PowerOf2Size (width);
		scaled_height = D3DTexture_PowerOf2Size (height);
	}
	else
	{
		scaled_width = (width + 3) & ~3;
		scaled_height = (height + 3) & ~3;
	}

	// clamp to max texture size (remove pad flag if it needs to clamp)
	if (scaled_width > d3d_DeviceCaps.MaxTextureWidth) {scaled_width = d3d_DeviceCaps.MaxTextureWidth; flags &= ~IMAGE_PADDABLE;}
	if (scaled_height > d3d_DeviceCaps.MaxTextureHeight) {scaled_height = d3d_DeviceCaps.MaxTextureHeight; flags &= ~IMAGE_PADDABLE;}

	int miplevels = (flags & IMAGE_MIPMAP) ? 0 : 1;

	// create the texture at the scaled size
	hr = d3d_Device->CreateTexture
	(
		scaled_width,
		scaled_height,
		miplevels,
		0,
		((flags & IMAGE_ALPHA) || (flags & IMAGE_FENCE)) ? D3DFMT_A8R8G8B8 : D3DFMT_X8R8G8B8,
		(flags & IMAGE_SYSMEM) ? D3DPOOL_SYSTEMMEM : D3DPOOL_MANAGED,
		&tex,
		NULL
	);

	switch (hr)
	{
	case D3DERR_INVALIDCALL: Sys_Error ("D3DTexture_Upload: d3d_Device->CreateTexture failed with D3DERR_INVALIDCALL");
	case D3DERR_OUTOFVIDEOMEMORY: Sys_Error ("D3DTexture_Upload: d3d_Device->CreateTexture failed with D3DERR_OUTOFVIDEOMEMORY");
	case E_OUTOFMEMORY: Sys_Error ("D3DTexture_Upload: d3d_Device->CreateTexture failed with E_OUTOFMEMORY");
	case D3D_OK: break;
	default: Sys_Error ("D3DTexture_Upload: d3d_Device->CreateTexture failed (unknown error)");
	}

	D3DTexture_LoadData (tex, data, width, height, scaled_width, scaled_height, flags);

	return tex;
}

//							  0       1      2      3      4      5      6
char *textureextensions[] = {"link", "dds", "tga", "bmp", "png", "jpg", "pcx", NULL};
char *defaultpaths[] = {"textures/", "", NULL};

byte *D3DImage_LoadTGA (byte *f, int *width, int *height);
byte *D3DImage_LoadPCX (byte *f, int *width, int *height);

static LPDIRECT3DTEXTURE9 D3DTexture_CreateExternal (byte *data, int type, int flags)
{
	LPDIRECT3DTEXTURE9 tex = NULL;
	int width, height;

	if (!data) return NULL;

	switch (type)
	{
	case 0:	// link
		// a .link file should never explicitly go through this codepath
		return NULL;

	case 2:	// tga
		// d3dx can load tga but it fails on rle which some mods use (to reduce a 22.5mb download to 22.25mb or something...)
		if ((data = D3DImage_LoadTGA (data, &width, &height)) != NULL)
			tex = D3DTexture_Upload (data, width, height, flags | IMAGE_32BIT);

		break;

	case 6:	// pcx
		if ((data = D3DImage_LoadPCX (data, &width, &height)) != NULL)
			tex = D3DTexture_Upload (data, width, height, flags | IMAGE_32BIT);

		break;

	default:
		// these types can go through D3DX Loaders
		D3DXCreateTextureFromFileInMemoryEx (d3d_Device,
			data, com_filesize,
			D3DX_DEFAULT, D3DX_DEFAULT,
			(flags & IMAGE_MIPMAP) ? D3DX_DEFAULT : 1,
			0, D3DFMT_FROM_FILE, D3DPOOL_MANAGED,
			D3DX_FILTER_LINEAR, D3DX_FILTER_BOX,
			0, NULL, NULL, &tex);

		break;
	}

	return tex;
}


LPDIRECT3DTEXTURE9 D3DTexture_LoadExternal (char *filename, char **paths, int flags)
{
	// sanity check
	if (!filename) return NULL;
	if (!paths) return NULL;
	if (!paths[0]) return NULL;

	char namebuf[256];
	int hunkmark = MainHunk->GetLowMark ();
	byte *data = NULL;
	LPDIRECT3DTEXTURE9 tex = NULL;

	for (int i = 0; ; i++)
	{
		// no more extensions
		if (!paths[i]) break;

		// tested and confirmed to be an invalid path (doesn't exist in the filesystem)
		if (!paths[i][0]) continue;

		for (int j = 0; ; j++)
		{
			if (!textureextensions[j]) break;

			if (flags & IMAGE_LUMA)
				sprintf (namebuf, "%s%s_luma.%s", paths[i], filename, textureextensions[j]);
			else sprintf (namebuf, "%s%s.%s", paths[i], filename, textureextensions[j]);

			// identify liquid
			for (int t = 0; ; t++)
			{
				if (!namebuf[t]) break;
				if (namebuf[t] == '*') namebuf[t] = '#';
			}

			if ((data = COM_LoadFile (namebuf, MainHunk)) != NULL)
			{
				if (j == 0)
				{
					// got a link file so use it instead
					char *linkname = (char *) data;
					int type = 1;

					// weenix loonies
					_strlwr (linkname);

					// COM_LoadFile will 0-termnate a string automatically for us
					if (strstr (linkname, ".tga")) type = 2;
					if (strstr (linkname, ".pcx")) type = 6;

					// .link assumes the same path
					sprintf (namebuf, "%s%s", paths[i], linkname);

					if ((data = COM_LoadFile (namebuf, MainHunk)) != NULL)
					{
						Con_DPrintf ("got a file : %s\n", namebuf);
						tex = D3DTexture_CreateExternal (data, type, flags);
						goto done;
					}
				}
				else
				{
					Con_DPrintf ("got a file : %s\n", namebuf);
					tex = D3DTexture_CreateExternal (data, j, flags);
					goto done;
				}
			}
		}
	}

done:;
	MainHunk->FreeToLowMark (hunkmark);

	// may be NULL if it didn't load
	return tex;
}


// checksums of textures that need hacking...
typedef struct hashhacker_s
{
	byte hash[16];
	char *idcat;
	char *idcpy;
} hashhacker_t;

// hardcoded data about textures that got screwed up in id1
hashhacker_t d3d_HashHacks[] =
{
	{{209, 191, 162, 164, 213, 63, 224, 73, 227, 251, 229, 137, 43, 60, 25, 138}, "_cable", NULL},
	{{52, 114, 210, 88, 38, 70, 116, 171, 89, 227, 115, 137, 102, 79, 193, 35}, "_bolt", NULL},
	{{35, 233, 88, 189, 135, 188, 152, 69, 221, 125, 104, 132, 51, 91, 22, 15}, "_arc", NULL},
	{{207, 93, 199, 54, 82, 58, 152, 177, 67, 18, 185, 231, 214, 4, 164, 99}, "_x", NULL},
	{{27, 95, 227, 196, 123, 235, 244, 145, 211, 222, 14, 190, 37, 255, 215, 107}, "_arc", NULL},
	{{47, 119, 108, 18, 244, 34, 166, 42, 207, 217, 179, 201, 114, 166, 199, 35}, "_double", NULL},
	{{199, 119, 111, 184, 133, 111, 68, 52, 169, 1, 239, 142, 2, 233, 192, 15}, "_back", NULL},
	{{60, 183, 222, 11, 163, 158, 222, 195, 124, 161, 201, 158, 242, 30, 134, 28}, "_rune", NULL},
	{{220, 67, 132, 212, 3, 131, 54, 160, 135, 4, 5, 86, 79, 146, 123, 89}, NULL, "sky4_solid"},
	{{163, 123, 35, 117, 154, 146, 68, 92, 141, 70, 253, 212, 187, 18, 112, 149}, NULL, "sky4_alpha"},
	{{196, 173, 196, 177, 19, 221, 134, 159, 208, 159, 158, 4, 108, 57, 10, 108}, NULL, "sky1_solid"},
	{{143, 106, 19, 206, 242, 171, 137, 86, 161, 74, 156, 217, 85, 10, 120, 149}, NULL, "sky1_alpha"},
};

byte ShotgunShells[] = {202, 6, 69, 163, 17, 112, 190, 234, 102, 56, 225, 242, 212, 175, 27, 187};

// this was generated from a word doc on my own PC
// while MD5 collisions are possible, they are sufficiently unlikely in the context of
// a poxy game engine.  this ain't industrial espionage, folks!
byte no_match_hash[] = {0x40, 0xB4, 0x54, 0x7D, 0x9D, 0xDA, 0x9D, 0x0B, 0xCF, 0x42, 0x70, 0xEE, 0xF1, 0x88, 0xBE, 0x99};

// nehahra sends some textures with 0 width and height (eeewww!  more hacks!)
byte nulldata[] = {255, 255, 255, 255};

LPDIRECT3DTEXTURE9 D3DTexture_Load (char *identifier, int width, int height, byte *data, int flags, char **paths)
{
	// supply a path to load it from if none was given
	if (!paths) paths = defaultpaths;

	// nehahra sends some textures with 0 width and height (eeewww!  more hacks!)
	if (!width || !height || !data)
	{
		width = 2;
		height = 2;
		data = nulldata;
	}

	// nehahra assumes that fullbrights are not available in the engine
	if ((flags & IMAGE_LUMA) && nehahra) return NULL;

	// detect water textures
	if (identifier[0] == '*')
	{
		// don't load lumas for liquid textures
		if (flags & IMAGE_LUMA) return NULL;

		// flag it early so that it gets stored in the struct
		flags |= IMAGE_LIQUID;
	}

	bool hasluma = false;

	// check native texture for a luma
	if ((flags & IMAGE_LUMA) && !(flags & IMAGE_32BIT))
	{
		for (int i = 0; i < width * height; i++)
		{
			if (data[i] > 223)
			{
				hasluma = true;
				break;
			}
		}
	}

	// take a hash of the image data
	byte texhash[16];
	int slot = -1;

	COM_HashData (texhash, data, width * height * ((flags & IMAGE_32BIT) ? 4 : 1));

	// look for a match
	for (int i = 0; i < d3d_TextureList.size (); i++)
	{
		// look for a free slot
		if (!d3d_TextureList[i]->d3d_Texture)
		{
			// got one
			slot = i;
			continue;
		}

		// fixes a bug in red slammer where a frame 0 in an animated texture generated the same checksum as a standard lava texture,
		// causing animation cycles to get messed up.  ideally the texture system would be immune to this but for now it's not...
		if (strcmp (identifier, d3d_TextureList[i]->identifier)) continue;

		// compare the hash and reuse if it matches
		if (COM_CheckHash (texhash, d3d_TextureList[i]->hash))
		{
			// check for luma match as the incoming luma will get the same hash as it's base
			// we can't compare flags directly as incoming flags may be changed
			if ((flags & IMAGE_LUMA) == (d3d_TextureList[i]->flags & IMAGE_LUMA))
			{
				// set last usage to 0
				d3d_TextureList[i]->LastUsage = 0;

				Con_DPrintf ("reused %s%s\n", identifier, (flags & IMAGE_LUMA) ? "_luma" : "");

				// return it
				return d3d_TextureList[i]->d3d_Texture;
			}
		}
	}

	// the actual texture we're going to load
	image_t *tex = NULL;

	// we either allocate it fresh or we reuse a free slot
	if (slot >= 0)
		tex = d3d_TextureList[slot];
	else
	{
		tex = (image_t *) Zone_Alloc (sizeof (image_t));
		d3d_TextureList.push_back (tex);
		tex = d3d_TextureList[d3d_TextureList.size () - 1];
	}

	// fill in the struct
	tex->LastUsage = 0;

	// set up the image
	tex->d3d_Texture = NULL;
	tex->data = data;
	tex->flags = flags;
	tex->height = height;
	tex->width = width;

	memcpy (tex->hash, texhash, 16);
	strcpy (tex->identifier, identifier);

	// hack the colour for certain models
	// fix white line at base of shotgun shells box
	if (COM_CheckHash (texhash, ShotgunShells)) memcpy (tex->data, tex->data + 32 * 31, 32);

	// try to load an external texture using the base identifier
	if ((tex->d3d_Texture = D3DTexture_LoadExternal (tex->identifier, paths, flags)) != NULL)
	{
		// notify that we got an external texture
		tex->flags |= IMAGE_EXTERNAL;
	}
	else
	{
		// it might yet have the QRP convention so try that
		char qrpident[256] = {0};

		// try the QRP names here
		for (int i = 0; i < STRUCT_ARRAY_LENGTH (d3d_HashHacks); i++)
		{
			if (COM_CheckHash (texhash, d3d_HashHacks[i].hash))
			{
				// don't mess with the original tex->identifier as that's used for cache checks
				strcpy (qrpident, tex->identifier);

				if (d3d_HashHacks[i].idcat) strcat (qrpident, d3d_HashHacks[i].idcat);
				if (d3d_HashHacks[i].idcpy) strcpy (qrpident, d3d_HashHacks[i].idcpy);

				break;
			}
		}

		// and now try load it (but only if we got a match for it)
		if (qrpident[0] && (tex->d3d_Texture = D3DTexture_LoadExternal (qrpident, paths, flags)) != NULL)
		{
			// notify that we got an external texture
			tex->flags |= IMAGE_EXTERNAL;
		}
		else
		{
			// test for a native luma texture
			if ((flags & IMAGE_LUMA) && !hasluma)
			{
				// if we got neither a native nor an external luma we must cancel it and return NULL
				tex->LastUsage = 666;
				SAFE_RELEASE (tex->d3d_Texture);
				memcpy (tex->hash, no_match_hash, 16);
				return NULL;
			}

			// upload through direct 3d
			tex->d3d_Texture = D3DTexture_Upload (tex->data, tex->width, tex->height, tex->flags);
		}
	}

	// Con_Printf ("created %s%s\n", identifier, (flags & IMAGE_LUMA) ? "_luma" : "");

	// notify that we padded the image
	if (d3d_ImagePadded) tex->flags |= IMAGE_PADDED;

	// return the texture we got
	return tex->d3d_Texture;
}


void D3DTexture_Register (LPDIRECT3DTEXTURE9 tex, char *loadname)
{
	image_t *img = (image_t *) Zone_Alloc (sizeof (image_t));
	D3DSURFACE_DESC desc;

	d3d_TextureList.push_back (img);
	img = d3d_TextureList[d3d_TextureList.size () - 1];

	tex->GetLevelDesc (0, &desc);

	// ensure that it never gets a hash collision and that it's never flushed
	Q_strncpy (img->identifier, loadname, 63);
	memcpy (img->hash, no_match_hash, 16);
	img->d3d_Texture = tex;
	img->flags = IMAGE_PRESERVE;
	img->height = desc.Height;
	img->width = desc.Width;
}


void R_ReleaseResourceTextures (void);
void D3DSky_UnloadSkybox (void);
void Scrap_Destroy (void);
void D3DLight_ReleaseLightmaps (void);
void D3DSky_ReleaseTextures (void);

// fixme - this needs to fully go through the correct shutdown paths so that aux data is also cleared
void D3DTexture_Release (void)
{
	// some of these now go through the device onloss handlers
	D3DLight_ReleaseLightmaps ();
	Scrap_Destroy ();
	D3DSky_UnloadSkybox ();

	// other textures we need to release that we don't define globally
	extern LPDIRECT3DTEXTURE9 d3d_MapshotTexture;

	// release cached textures
	for (int i = 0; i < d3d_TextureList.size (); i++)
	{
		SAFE_RELEASE (d3d_TextureList[i]->d3d_Texture);
		memcpy (d3d_TextureList[i]->hash, no_match_hash, 16);
	}

	D3DSky_ReleaseTextures ();

	for (int i = 0; i < 16; i++)
		SAFE_RELEASE (d3d_PaletteRowTextures[i]);

	// resource textures
	R_ReleaseResourceTextures ();

	char_texture = NULL;

	// other textures
	for (int i = 0; i < MAX_CHAR_TEXTURES; i++)
		SAFE_RELEASE (char_textures[i]);

	// ensure that we don't attempt to use this one
	char_texture = NULL;

	SAFE_RELEASE (d3d_MapshotTexture);
}


void D3DTexture_Flush (void)
{
	int numflush = 0;

	// sanity check
	if (gl_maxtextureretention.value < 2) Cvar_Set (&gl_maxtextureretention, 2);

	for (int i = 0; i < d3d_TextureList.size (); i++)
	{
		// all textures just loaded in the cache will have lastusage set to 0
		// incremenent lastusage for types we want to flush.
		// alias and sprite models are cached between maps so don't ever free them
		if (d3d_TextureList[i]->flags & IMAGE_BSP) d3d_TextureList[i]->LastUsage++;

		// always preserve these types irrespective
		if (d3d_TextureList[i]->flags & IMAGE_PRESERVE) d3d_TextureList[i]->LastUsage = 0;

		if (d3d_TextureList[i]->LastUsage < 2 && d3d_TextureList[i]->d3d_Texture)
		{
			// assign a higher priority to the texture
			d3d_TextureList[i]->d3d_Texture->SetPriority (256);
		}
		else if (d3d_TextureList[i]->d3d_Texture)
		{
			// assign a lower priority so that recently loaded textures can be preferred to be kept in vram
			d3d_TextureList[i]->d3d_Texture->SetPriority (0);
		}

		if (d3d_TextureList[i]->LastUsage > gl_maxtextureretention.value && d3d_TextureList[i]->d3d_Texture)
		{
			// if the texture hasn't been used in 4 maps, we flush it
			// this means that texture flushes will start happening about exm4
			// (we might cvar-ize this as an option for players to influence the flushing
			// policy, *VERY* handy if they have lots of *HUGE* textures.
			SAFE_RELEASE (d3d_TextureList[i]->d3d_Texture);

			// set the hash value to ensure that it will never match
			memcpy (d3d_TextureList[i]->hash, no_match_hash, 16);

			// increment number flushed
			numflush++;
		}
		else
		{
			// pull it back to vram
			if (d3d_TextureList[i]->d3d_Texture)
			{
				// keep the texture hot
				d3d_TextureList[i]->d3d_Texture->SetPriority (512);
				d3d_TextureList[i]->d3d_Texture->PreLoad ();
			}
		}
	}

	Con_DPrintf ("Flushed %i textures\n", numflush);
	Con_DPrintf ("Available texture memory: %0.3f MB\n", ((float) d3d_Device->GetAvailableTextureMem () / 1024.0f) / 1024.0f);
}


bool D3DTexture_CheckFormat (D3DFORMAT textureformat, BOOL mandatory)
{
	// test texture
	LPDIRECT3DTEXTURE9 tex = NULL;

	// check for compressed texture formats
	// rather than using CheckDeviceFormat we actually try to create one and see what happens
	// (using the default pool so that we ensure it's in hardware)
	hr = d3d_Device->CreateTexture (256, 256, 1, 0, textureformat, D3DPOOL_DEFAULT, &tex, NULL);

	if (SUCCEEDED (hr))
	{
		// done with the texture
		tex->Release ();
		tex = NULL;

		if (!mandatory)
			Con_Printf ("Allowing %s Texture Format\n", D3DTypeToString (textureformat));

		return true;
	}

	if (mandatory)
		Sys_Error ("Texture format %s is not supported", D3DTypeToString (textureformat));

	return false;
}


/*
======================================================================================================================================================

		EXTERNAL TEXTURE LOADING/ETC/BLAH/YADDA YADDA YADDA

	These need to be in common as they need access to the search paths

======================================================================================================================================================
*/


char *D3D_FindExternalTexture (char *basename)
{
	// not found
	return NULL;
}


void D3DTexture_MakeAlpha (LPDIRECT3DTEXTURE9 tex)
{
	D3DSURFACE_DESC surfdesc;
	D3DLOCKED_RECT lockrect;

	hr = tex->GetLevelDesc (0, &surfdesc);

	if (FAILED (hr)) return;

	hr = tex->LockRect (0, &lockrect, NULL, d3d_GlobalCaps.DefaultLock);

	if (FAILED (hr)) return;

	byte *data = (byte *) lockrect.pBits;

	for (int i = 0; i < surfdesc.Width * surfdesc.Height; i++, data += 4)
	{
		byte best = 0;

		if (data[0] > best) best = data[0];
		if (data[1] > best) best = data[1];
		if (data[2] > best) best = data[2];

		data[3] = best;
		data[0] = 255;
		data[1] = 255;
		data[2] = 255;
	}

	tex->UnlockRect (0);
}

