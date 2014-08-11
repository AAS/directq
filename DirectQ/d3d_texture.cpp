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

cvar_t gl_maxtextureretention ("gl_maxtextureretention", 3, CVAR_ARCHIVE);

LPDIRECT3DTEXTURE9 d3d_CurrentTexture[8] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
bool texturelist_dirty = false;
char **d3d_TextureNames = NULL;

typedef struct d3d_texture_s
{
	image_t *texture;
	struct d3d_texture_s *next;
} d3d_texture_t;

d3d_texture_t *d3d_TextureList = NULL;
int d3d_NumTextures = 0;

// other textures we use
extern LPDIRECT3DTEXTURE9 char_texture;
extern LPDIRECT3DTEXTURE9 char_textures[];
extern LPDIRECT3DTEXTURE9 solidskytexture;
extern LPDIRECT3DTEXTURE9 alphaskytexture;

// palette hackery
palettedef_t d3d_QuakePalette;


void D3D_MakeQuakePalettes (byte *palette)
{
	int dark = 21024;
	int darkindex = 0;
	PALETTEENTRY *p1, *p2;

	for (int i = 0; i < 256; i++)
	{
		// on disk palette has 3 components
		byte *rgb = palette;
		palette += 3;

		if (i < 255)
		{
			int darkness = ((int) rgb[0] + (int) rgb[1] + (int) rgb[2]) / 3;

			if (darkness < dark)
			{
				dark = darkness;
				darkindex = i;
			}
		}

		// set correct alpha colour
		byte alpha = (i < 255) ? 255 : 0;

		// per doc, peFlags is used for alpha
		// invert again so that the layout will be correct
		d3d_QuakePalette.standard[i].peRed = rgb[0];
		d3d_QuakePalette.standard[i].peGreen = rgb[1];
		d3d_QuakePalette.standard[i].peBlue = rgb[2];
		d3d_QuakePalette.standard[i].peFlags = alpha;

		// pre-bake the fullbright stuff into the palettes so that we can avoid some expensive shader ops
		if (vid.fullbright[i])
		{
			p1 = &d3d_QuakePalette.luma[i];
			p2 = &d3d_QuakePalette.noluma[i];
		}
		else
		{
			p1 = &d3d_QuakePalette.noluma[i];
			p2 = &d3d_QuakePalette.luma[i];
		}

		p1->peRed = rgb[0];
		p1->peGreen = rgb[1];
		p1->peBlue = rgb[2];
		p1->peFlags = alpha;

		p2->peRed = 0;
		p2->peGreen = 0;
		p2->peBlue = 0;
		p2->peFlags = alpha;

		d3d_QuakePalette.standard32[i] = D3DCOLOR_XRGB (rgb[0], rgb[1], rgb[2]);
	}

	// correct alpha colour
	d3d_QuakePalette.standard32[255] = 0;

	// set index of darkest colour
	d3d_QuakePalette.darkindex = darkindex;
}


int D3D_PowerOf2Size (int size)
{
	int pow2;

	// 2003 will let us declare pow2 in the loop and then return it but later versions are standards compliant
	for (pow2 = 1; pow2 < size; pow2 *= 2);

	return pow2;
}


void D3D_HashTexture (byte *hash, int width, int height, void *data, int flags)
{
	int datalen = height * width;

	if (flags & IMAGE_32BIT) datalen *= 4;

	// call into here instead
	COM_HashData (hash, data, datalen);
}


bool d3d_ImagePadded = false;

void D3D_Transfer8BitTexture (byte *src, unsigned *dst, int size, unsigned *palette)
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


void D3D_Transfer32BitTexture (unsigned *src, unsigned *dst, int size)
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


void D3D_Resample8BitTexture (byte *in, int inwidth, int inheight, unsigned *out, int outwidth, int outheight, unsigned *palette)
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


void D3D_Resample32BitTexture (unsigned *in, int inwidth, int inheight, unsigned *out, int outwidth, int outheight)
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


unsigned *D3D_GetTexturePalette (PALETTEENTRY *d_8to24table, int flags)
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


void D3D_Pad8BitTexture (byte *data, int width, int height, unsigned *padded, int scaled_width, int scaled_height, unsigned *palette)
{
	for (int y = 0; y < height; y++)
	{
		D3D_Transfer8BitTexture (data, padded, width, palette);

		data += width;
		padded += scaled_width;
	}
}


void D3D_Pad32BitTexture (unsigned *data, int width, int height, unsigned *padded, int scaled_width, int scaled_height)
{
	for (int y = 0; y < height; y++)
	{
		D3D_Transfer32BitTexture (data, padded, width);

		data += width;
		padded += scaled_width;
	}
}


void TM_AlphaEdgeFix (byte *data, int width, int height)
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


void D3D_LoadTextureData (LPDIRECT3DTEXTURE9 *texture, void *data, int width, int height, int scaled_width, int scaled_height, int flags)
{
	// default to the standard palette
	PALETTEENTRY *activepal = d3d_QuakePalette.standard;

	// sky has flags & IMAGE_32BIT so it doesn't get paletteized
	// (not always - solid sky goes up as 8 bits.  it doesn't have IMAGE_BSP however so all is good
	// nehahra assumes that fullbrights are not available in the engine
	if (((flags & IMAGE_BSP) || (flags & IMAGE_ALIAS)) && !nehahra)
	{
		if (flags & IMAGE_LIQUID)
			activepal = d3d_QuakePalette.standard;
		else if (flags & IMAGE_LUMA)
			activepal = d3d_QuakePalette.luma;
		else activepal = d3d_QuakePalette.noluma;
	}
	else activepal = d3d_QuakePalette.standard;

	// now get the final palette in a format we can transfer to the raw data
	unsigned *palette = D3D_GetTexturePalette (activepal, flags);

	D3DLOCKED_RECT lockrect;
	texture[0]->LockRect (0, &lockrect, NULL, D3DLOCK_NO_DIRTY_UPDATE);
	unsigned *trans = (unsigned *) lockrect.pBits;

	// note - we don't check for np2 support here because we also need to account for clamping to max size
	if (scaled_width == width && scaled_height == height)
	{
		if (flags & IMAGE_32BIT)
			D3D_Transfer32BitTexture ((unsigned *) data, trans, width * height);
		else D3D_Transfer8BitTexture ((byte *) data, trans, width * height, palette);
	}
	else
	{
		int hunkmark = MainHunk->GetLowMark ();

		if (flags & IMAGE_PADDABLE)
		{
			// clear texture to alpha
			memset (trans, 0, scaled_width * scaled_height * 4);

			if (flags & IMAGE_32BIT)
				D3D_Pad32BitTexture ((unsigned *) data, width, height, trans, scaled_width, scaled_height);
			else D3D_Pad8BitTexture ((byte *) data, width, height, trans, scaled_width, scaled_height, palette);

			d3d_ImagePadded = true;
		}
		else
		{
			if (flags & IMAGE_32BIT)
				D3D_Resample32BitTexture ((unsigned *) data, width, height, trans, scaled_width, scaled_height);
			else D3D_Resample8BitTexture ((byte *) data, width, height, trans, scaled_width, scaled_height, palette);
		}

		MainHunk->FreeToLowMark (hunkmark);
	}

	// fix alpha edges on textures that need them
	if ((flags & IMAGE_ALPHA) || (flags & IMAGE_FENCE))
		TM_AlphaEdgeFix ((byte *) lockrect.pBits, scaled_width, scaled_height);

	// to do - mipmap with gamma correction here
	// (is there a D3DX option to do it?)

	texture[0]->UnlockRect (0);
	texture[0]->AddDirtyRect (NULL);

	// automatic mipmapping sucks because it doesn't support a box filter
	if (flags & IMAGE_MIPMAP)
		D3DXFilterTexture (texture[0], NULL, 0, D3DX_FILTER_BOX);

	texture[0]->PreLoad ();
}


void D3D_UploadTexture (LPDIRECT3DTEXTURE9 *texture, void *data, int width, int height, int flags)
{
	if ((flags & IMAGE_SCRAP) && !(flags & IMAGE_32BIT)) // && width < 64 && height < 64)
	{
		// load little ones into the scrap
		SAFE_RELEASE (texture[0]);
		return;
	}

	// explicit release as we're completely respecifying the texture
	SAFE_RELEASE (texture[0]);

	// the scaled sizes are initially equal to the original sizes (for np2 support)
	int scaled_width = width;
	int scaled_height = height;

	// check scaling here first
	if (!d3d_GlobalCaps.supportNonPow2)
	{
		scaled_width = D3D_PowerOf2Size (width);
		scaled_height = D3D_PowerOf2Size (height);
	}

	// clamp to max texture size (remove pad flag if it needs to clamp)
	if (scaled_width > d3d_DeviceCaps.MaxTextureWidth) {scaled_width = d3d_DeviceCaps.MaxTextureWidth; flags &= ~IMAGE_PADDABLE;}
	if (scaled_height > d3d_DeviceCaps.MaxTextureHeight) {scaled_height = d3d_DeviceCaps.MaxTextureHeight; flags &= ~IMAGE_PADDABLE;}

	// create the texture at the scaled size
	hr = d3d_Device->CreateTexture
	(
		scaled_width,
		scaled_height,
		(flags & IMAGE_MIPMAP) ? 0 : 1,
		0,
		((flags & IMAGE_ALPHA) || (flags & IMAGE_FENCE)) ? D3DFMT_A8R8G8B8 : D3DFMT_X8R8G8B8,
		(flags & IMAGE_SYSMEM) ? D3DPOOL_SYSTEMMEM : D3DPOOL_MANAGED,
		texture,
		NULL
	);

	switch (hr)
	{
	case D3DERR_INVALIDCALL: Sys_Error ("D3D_UploadTexture: d3d_Device->CreateTexture failed with D3DERR_INVALIDCALL");
	case D3DERR_OUTOFVIDEOMEMORY: Sys_Error ("D3D_UploadTexture: d3d_Device->CreateTexture failed with D3DERR_OUTOFVIDEOMEMORY");
	case E_OUTOFMEMORY: Sys_Error ("D3D_UploadTexture: d3d_Device->CreateTexture failed with E_OUTOFMEMORY");
	case D3D_OK: break;
	default: Sys_Error ("D3D_UploadTexture: d3d_Device->CreateTexture failed (unknown error)");
	}

	D3D_LoadTextureData (texture, data, width, height, scaled_width, scaled_height, flags);
}


// checksums of textures that need hacking...
byte ShotgunShells[] = {202, 6, 69, 163, 17, 112, 190, 234, 102, 56, 225, 242, 212, 175, 27, 187};
byte plat_top1_cable[] = {209, 191, 162, 164, 213, 63, 224, 73, 227, 251, 229, 137, 43, 60, 25, 138};
byte plat_top1_bolt[] = {52, 114, 210, 88, 38, 70, 116, 171, 89, 227, 115, 137, 102, 79, 193, 35};
byte metal5_2_arc[] = {35, 233, 88, 189, 135, 188, 152, 69, 221, 125, 104, 132, 51, 91, 22, 15};
byte metal5_2_x[] = {207, 93, 199, 54, 82, 58, 152, 177, 67, 18, 185, 231, 214, 4, 164, 99};
byte metal5_4_arc[] = {27, 95, 227, 196, 123, 235, 244, 145, 211, 222, 14, 190, 37, 255, 215, 107};
byte metal5_4_double[] = {47, 119, 108, 18, 244, 34, 166, 42, 207, 217, 179, 201, 114, 166, 199, 35};
byte metal5_8_back[] = {199, 119, 111, 184, 133, 111, 68, 52, 169, 1, 239, 142, 2, 233, 192, 15};
byte metal5_8_rune[] = {60, 183, 222, 11, 163, 158, 222, 195, 124, 161, 201, 158, 242, 30, 134, 28};
byte sky_blue_solid[] = {220, 67, 132, 212, 3, 131, 54, 160, 135, 4, 5, 86, 79, 146, 123, 89};
byte sky_blue_alpha[] = {163, 123, 35, 117, 154, 146, 68, 92, 141, 70, 253, 212, 187, 18, 112, 149};
byte sky_purp_solid[] = {196, 173, 196, 177, 19, 221, 134, 159, 208, 159, 158, 4, 108, 57, 10, 108};
byte sky_purp_alpha[] = {143, 106, 19, 206, 242, 171, 137, 86, 161, 74, 156, 217, 85, 10, 120, 149};

// this was generated from a word doc on my own PC
// while MD5 collisions are possible, they are sufficiently unlikely in the context of
// a poxy game engine.  this ain't industrial espionage, folks!
byte no_match_hash[] = {0x40, 0xB4, 0x54, 0x7D, 0x9D, 0xDA, 0x9D, 0x0B, 0xCF, 0x42, 0x70, 0xEE, 0xF1, 0x88, 0xBE, 0x99};

// nehahra sends some textures with 0 width and height (eeewww!  more hacks!)
byte nulldata[] = {255, 255, 255, 255};

image_t *D3D_LoadTexture (char *identifier, int width, int height, byte *data, int flags)
{
	// nehahra sends some textures with 0 width and height (eeewww!  more hacks!)
	if (!width || !height)
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
			if (vid.fullbright[data[i]])
			{
				hasluma = true;
				break;
			}
		}
	}

	// take a hash of the image data
	byte texhash[16];
	D3D_HashTexture (texhash, width, height, data, flags);

	// stores a free texture slot
	image_t *freetex = NULL;

	// look for a match
	for (d3d_texture_t *d3dtex = d3d_TextureList; d3dtex; d3dtex = d3dtex->next)
	{
		// look for a free slot
		if (!d3dtex->texture->d3d_Texture)
		{
			// got one
			freetex = d3dtex->texture;
			continue;
		}

		// fixes a bug in red slammer where a frame 0 in an animated texture generated the same checksum as a standard lava texture,
		// causing animation cycles to get messed up.  ideally the texture system would be immune to this but for now it's not...
		if (strcmp (identifier, d3dtex->texture->identifier)) continue;

		// compare the hash and reuse if it matches
		if (COM_CheckHash (texhash, d3dtex->texture->hash))
		{
			// check for luma match as the incoming luma will get the same hash as it's base
			if ((flags & IMAGE_LUMA) == (d3dtex->texture->flags & IMAGE_LUMA))
			{
				// set last usage to 0
				d3dtex->texture->LastUsage = 0;
				d3dtex->texture->RegistrationSequence = d3d_RenderDef.RegistrationSequence;

				// Con_Printf ("reused %s%s\n", identifier, (flags & IMAGE_LUMA) ? "_luma" : "");

				// return it
				return d3dtex->texture;
			}
		}
	}

	// the actual texture we're going to load
	image_t *tex = NULL;

	// we either allocate it fresh or we reuse a free slot
	if (!freetex)
	{
		d3d_texture_t *newtex = (d3d_texture_t *) Zone_Alloc (sizeof (d3d_texture_t));

		newtex->next = d3d_TextureList;
		d3d_TextureList = newtex;

		tex = (image_t *) Zone_Alloc (sizeof (image_t));
		newtex->texture = tex;
	}
	else tex = freetex;

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

	// change the identifier so that we can load an external texture properly
	if (flags & IMAGE_LUMA) strcat (tex->identifier, "_luma");

	// check for textures that have the same name but different data, and amend the name by the QRP standard
	if (COM_CheckHash (texhash, plat_top1_cable))
		strcat (tex->identifier, "_cable");
	else if (COM_CheckHash (texhash, plat_top1_bolt))
		strcat (tex->identifier, "_bolt");
	else if (COM_CheckHash (texhash, metal5_2_arc))
		strcat (tex->identifier, "_arc");
	else if (COM_CheckHash (texhash, metal5_2_x))
		strcat (tex->identifier, "_x");
	else if (COM_CheckHash (texhash, metal5_4_arc))
		strcat (tex->identifier, "_arc");
	else if (COM_CheckHash (texhash, metal5_4_double))
		strcat (tex->identifier, "_double");
	else if (COM_CheckHash (texhash, metal5_8_back))
		strcat (tex->identifier, "_back");
	else if (COM_CheckHash (texhash, metal5_8_rune))
		strcat (tex->identifier, "_rune");
	else if (COM_CheckHash (texhash, sky_blue_solid))
		strcpy (tex->identifier, "sky4_solid");
	else if (COM_CheckHash (texhash, sky_blue_alpha))
		strcpy (tex->identifier, "sky4_alpha");
	else if (COM_CheckHash (texhash, sky_purp_solid))
		strcpy (tex->identifier, "sky1_solid");
	else if (COM_CheckHash (texhash, sky_purp_alpha))
		strcpy (tex->identifier, "sky1_alpha");

	// hack the colour for certain models
	// fix white line at base of shotgun shells box
	if (COM_CheckHash (texhash, ShotgunShells))
		memcpy (tex->data, tex->data + 32 * 31, 32);

	// try to load an external texture
	bool externalloaded = D3D_LoadExternalTexture (&tex->d3d_Texture, tex->identifier, flags);

	if (!externalloaded)
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
		D3D_UploadTexture
		(
			&tex->d3d_Texture,
			tex->data,
			tex->width,
			tex->height,
			tex->flags
		);

		if (!tex->d3d_Texture && (flags & IMAGE_SCRAP))
		{
			return NULL;
		}
	}

	// Con_Printf ("created %s%s\n", identifier, (flags & IMAGE_LUMA) ? "_luma" : "");

	// notify that we padded the image
	if (d3d_ImagePadded) tex->flags |= IMAGE_PADDED;

	// notify that we got an external texture
	if (externalloaded) tex->flags |= IMAGE_EXTERNAL;

	texturelist_dirty = true;

	tex->RegistrationSequence = d3d_RenderDef.RegistrationSequence;

	// return the texture we got
	return tex;
}


unsigned int *D3D_MakeTexturePalette (miptex_t *mt)
{
	static unsigned int hlPal[256];

	// the palette follows the data for the last miplevel
	byte *pal = ((byte *) mt) + mt->offsets[0] + ((mt->width * mt->height * 85) >> 6) + 2;

	// now build a palette from the image data
	// don't gamma-adjust this data
	for (int i = 0; i < 256; i++)
	{
		((byte *) &hlPal[i])[0] = pal[0];
		((byte *) &hlPal[i])[1] = pal[1];
		((byte *) &hlPal[i])[2] = pal[2];
		((byte *) &hlPal[i])[3] = 255;
		pal += 3;
	}

	// check special texture types
	if (mt->name[0] == '{') hlPal[255] = 0;

	return hlPal;
}


void R_ReleaseResourceTextures (void);
void D3DHLSL_Shutdown (void);
void D3DSky_UnloadSkybox (void);
void Scrap_Destroy (void);
void D3DLight_ReleaseLightmaps (void);

// fixme - this needs to fully go through the correct shutdown paths so that aux data is also cleared
void D3D_ReleaseTextures (void)
{
	// some of these now go through the device onloss handlers
	D3DLight_ReleaseLightmaps ();
	Scrap_Destroy ();
	D3DHLSL_Shutdown ();
	D3DSky_UnloadSkybox ();

	// other textures we need to release that we don't define globally
	extern LPDIRECT3DTEXTURE9 d3d_MapshotTexture;
	extern LPDIRECT3DTEXTURE9 skyboxtextures[];
	extern LPDIRECT3DCUBETEXTURE9 skyboxcubemap;
	extern image_t d3d_PlayerSkins[];

	// release cached textures
	for (d3d_texture_t *tex = d3d_TextureList; tex; tex = tex->next)
	{
		SAFE_RELEASE (tex->texture->d3d_Texture);

		// set the hash value to ensure that it will never match
		// (needed because game changing goes through this path too)
		memcpy (tex->texture->hash, no_match_hash, 16);
	}

	// release player textures
	for (int i = 0; i < 256; i++)
		SAFE_RELEASE (d3d_PlayerSkins[i].d3d_Texture);

	// skyboxes
	for (int i = 0; i < 6; i++)
		SAFE_RELEASE (skyboxtextures[i]);

	SAFE_RELEASE (skyboxcubemap);

	// resource textures
	R_ReleaseResourceTextures ();

	char_texture = NULL;

	// other textures
	for (int i = 0; i < MAX_CHAR_TEXTURES; i++)
		SAFE_RELEASE (char_textures[i]);

	// ensure that we don't attempt to use this one
	char_texture = NULL;

	SAFE_RELEASE (solidskytexture);
	SAFE_RELEASE (alphaskytexture);
	SAFE_RELEASE (d3d_MapshotTexture);
}


void D3D_FlushTextures (void)
{
	int numflush = 0;

	// sanity check
	if (gl_maxtextureretention.value < 2) Cvar_Set (&gl_maxtextureretention, 2);

	for (d3d_texture_t *tex = d3d_TextureList; tex; tex = tex->next)
	{
		// all textures just loaded in the cache will have lastusage set to 0
		// incremenent lastusage for types we want to flush.
		// alias and sprite models are cached between maps so don't ever free them
		if (tex->texture->flags & IMAGE_BSP) tex->texture->LastUsage++;

		// always preserve these types irrespective
		if (tex->texture->flags & IMAGE_PRESERVE) tex->texture->LastUsage = 0;

		if (tex->texture->LastUsage < 2 && tex->texture->d3d_Texture)
		{
			// assign a higher priority to the texture
			tex->texture->d3d_Texture->SetPriority (256);
		}
		else if (tex->texture->d3d_Texture)
		{
			// assign a lower priority so that recently loaded textures can be preferred to be kept in vram
			tex->texture->d3d_Texture->SetPriority (0);
		}

		if (tex->texture->LastUsage > gl_maxtextureretention.value && tex->texture->d3d_Texture)
		{
			// if the texture hasn't been used in 4 maps, we flush it
			// this means that texture flushes will start happening about exm4
			// (we might cvar-ize this as an option for players to influence the flushing
			// policy, *VERY* handy if they have lots of *HUGE* textures.
			SAFE_RELEASE (tex->texture->d3d_Texture);

			// set the hash value to ensure that it will never match
			memcpy (tex->texture->hash, no_match_hash, 16);

			// increment number flushed
			numflush++;
		}
		else
		{
			// pull it back to vram
			if (tex->texture->d3d_Texture)
			{
				// keep the texture hot
				tex->texture->d3d_Texture->SetPriority (512);
				tex->texture->d3d_Texture->PreLoad ();
			}
		}
	}

	Con_DPrintf ("Flushed %i textures\n", numflush);
	Con_DPrintf ("Available texture memory: %0.3f MB\n", ((float) d3d_Device->GetAvailableTextureMem () / 1024.0f) / 1024.0f);
}


bool D3D_CheckTextureFormat (D3DFORMAT textureformat, BOOL mandatory)
{
	// test texture
	LPDIRECT3DTEXTURE9 tex = NULL;

	// check for compressed texture formats
	// rather than using CheckDeviceFormat we actually try to create one and see what happens
	hr = d3d_Device->CreateTexture
		 (
			 64,
			 64,
			 1,
			 0,
			 textureformat,
			 D3DPOOL_MANAGED,
			 &tex,
			 NULL
		 );

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


typedef struct d3d_externaltexture_s
{
	char basename[256];
	char texpath[256];
} d3d_externaltexture_t;


d3d_externaltexture_t **d3d_ExternalTextures = NULL;
int d3d_MaxExternalTextures = 0;
int d3d_NumExternalTextures = 0;

// gotcha!
int d3d_ExternalTextureTable[257] = { -1};

// hmmm - can be used for both bsearch and qsort
// clever boy, bill!
int D3D_ExternalTextureCompareFunc (const void *a, const void *b)
{
	d3d_externaltexture_t *t1 = * (d3d_externaltexture_t **) a;
	d3d_externaltexture_t *t2 = * (d3d_externaltexture_t **) b;

	return stricmp (t1->basename, t2->basename);
}


char *D3D_FindExternalTexture (char *basename)
{
	// find the first texture
	int texnum = d3d_ExternalTextureTable[basename[0]];

	// no textures
	if (texnum == -1) return NULL;

	for (int i = texnum; i < d3d_NumExternalTextures; i++)
	{
		// retrieve texture def
		d3d_externaltexture_t *et = d3d_ExternalTextures[i];

		// first char changes
		if (et->basename[0] != basename[0]) break;

		// if it came from a screenshot we ignore it
		if (strstr (et->texpath, "/screenshot/")) continue;

		// if basenames match return the path at which it can be found
		if (!stricmp (et->basename, basename)) return et->texpath;
	}

	// not found
	return NULL;
}


void D3D_RegisterExternalTexture (char *texname)
{
	char *texext = NULL;
	bool goodext = false;

	// find the extension
	for (int i = strlen (texname); i; i--)
	{
		if (texname[i] == '/') break;
		if (texname[i] == '\\') break;

		if (texname[i] == '.')
		{
			texext = &texname[i + 1];
			break;
		}
	}

	// didn't find an extension
	if (!texext) return;

	// check for supported types
	if (!stricmp (texext, "link")) goodext = true;
	if (!stricmp (texext, "dds")) goodext = true;
	if (!stricmp (texext, "tga")) goodext = true;
	if (!stricmp (texext, "bmp")) goodext = true;
	if (!stricmp (texext, "png")) goodext = true;
	if (!stricmp (texext, "jpg")) goodext = true;
	if (!stricmp (texext, "jpeg")) goodext = true;
	if (!stricmp (texext, "pcx")) goodext = true;
	if (!stricmp (texext, "wal")) goodext = true;

	// not a supported type
	if (!goodext) return;

	if (d3d_NumExternalTextures == d3d_MaxExternalTextures) return;

	char *typefilter = NULL;
	bool passedext = false;

	for (int i = strlen (texname); i; i--)
	{
		if (texname[i] == '/') break;
		if (texname[i] == '\\') break;
		if (texname[i] == '.' && passedext) break;
		if (texname[i] == '.' && !passedext) passedext = true;

		if (texname[i] == '_' && passedext)
		{
			typefilter = &texname[i + 1];
			break;
		}
	}

	// filter out types unsupported by DirectQ so that the likes of Rygel's pack
	// won't overflow the max textures allowed (will need to get the full list of types from DP)
	// although with space for 65536 textures that should never happen...
	if (typefilter)
	{
		if (!strnicmp (typefilter, "gloss.", 6)) return;
		if (!strnicmp (typefilter, "norm.", 5)) return;
		if (!strnicmp (typefilter, "normal.", 7)) return;
		if (!strnicmp (typefilter, "bump.", 5)) return;
	}

	// register a new external texture
	d3d_externaltexture_t *et = (d3d_externaltexture_t *) GameZone->Alloc (sizeof (d3d_externaltexture_t));
	d3d_ExternalTextures[d3d_NumExternalTextures] = et;
	d3d_NumExternalTextures++;

	// fill in the path (also copy to basename in case the next stage doesn't get it)
	Q_strncpy (et->texpath, texname, 255);
	Q_strncpy (et->basename, texname, 255);
	strlwr (et->texpath);

	if (strstr (et->texpath, "\\crosshairs\\"))
	{
		d3d_NumExternalTextures = d3d_NumExternalTextures;
	}

	// check for special handling of some types
	char *checkstuff = strstr (et->texpath, "\\save\\");

	if (!checkstuff) checkstuff = strstr (et->texpath, "\\maps\\");
	if (!checkstuff) checkstuff = strstr (et->texpath, "\\screenshot\\");

	// ignoring textures in maps, save and screenshot
	if (!checkstuff)
	{
		// base name is the path without directories or extension; first remove directories.
		// we leave extension alone for now so that we can sort on basename properly
		for (int i = strlen (et->texpath); i; i--)
		{
			if (et->texpath[i] == '/' || et->texpath[i] == '\\')
			{
				Q_strncpy (et->basename, &et->texpath[i + 1], 255);
				break;
			}
		}
	}
	else
	{
		for (checkstuff = checkstuff - 1;; checkstuff--)
		{
			if (checkstuff[0] == ':') break;

			if (checkstuff[0] == '/' || checkstuff[0] == '\\')
			{
				Q_strncpy (et->basename, &checkstuff[1], 255);
				break;
			}
		}
	}

	// switch basename to lower case
	strlwr (et->basename);

	// make path seperators consistent
	for (int i = 0;; i++)
	{
		if (!et->basename[i]) break;
		if (et->basename[i] == '/') et->basename[i] = '\\';
	}

	// switch extension to a dummy to establish the preference sort order; this is for
	// cases where a texture may be present more than once in different formats.
	// we'll remove it after we've sorted
	texext = NULL;

	// find the extension
	for (int i = strlen (et->basename); i; i--)
	{
		if (et->basename[i] == '/') break;
		if (et->basename[i] == '\\') break;

		if (et->basename[i] == '.')
		{
			texext = &et->basename[i + 1];
			break;
		}
	}

	// didn't find an extension (should never happen at this stage)
	if (!texext) return;

	// check for supported types and replace the extension to get the sort order
	if (!stricmp (texext, "link")) {strcpy (texext, "111"); return;}
	if (!stricmp (texext, "dds")) {strcpy (texext, "222"); return;}
	if (!stricmp (texext, "tga")) {strcpy (texext, "333"); return;}
	if (!stricmp (texext, "bmp")) {strcpy (texext, "444"); return;}
	if (!stricmp (texext, "png")) {strcpy (texext, "555"); return;}
	if (!stricmp (texext, "jpg")) {strcpy (texext, "666"); return;}
	if (!stricmp (texext, "jpeg")) {strcpy (texext, "777"); return;}
	if (!stricmp (texext, "pcx")) {strcpy (texext, "888"); return;}
	if (!stricmp (texext, "wal")) {strcpy (texext, "999"); return;}
}


void D3D_ExternalTextureDirectoryRecurse (char *dirname)
{
	WIN32_FIND_DATA FindFileData;
	HANDLE hFind = INVALID_HANDLE_VALUE;
	char find_filter[MAX_PATH];

	_snprintf (find_filter, 260, "%s/*.*", dirname);

	for (int i = 0;; i++)
	{
		if (find_filter[i] == 0) break;
		if (find_filter[i] == '/') find_filter[i] = '\\';
	}

	hFind = FindFirstFile (find_filter, &FindFileData);

	if (hFind == INVALID_HANDLE_VALUE)
	{
		// found no files
		FindClose (hFind);
		return;
	}

	do
	{
		// not interested
		if (FindFileData.dwFileAttributes & FILE_ATTRIBUTE_COMPRESSED) continue;
		if (FindFileData.dwFileAttributes & FILE_ATTRIBUTE_ENCRYPTED) continue;
		if (FindFileData.dwFileAttributes & FILE_ATTRIBUTE_OFFLINE) continue;
		if (FindFileData.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) continue;

		// never these
		if (!strcmp (FindFileData.cFileName, ".")) continue;
		if (!strcmp (FindFileData.cFileName, "..")) continue;

		// make the new directory or texture name
		char newname[256];

		_snprintf (newname, 255, "%s\\%s", dirname, FindFileData.cFileName);

		if (FindFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			// prefix dirs with _ to skip them
			if (FindFileData.cFileName[0] != '_')
				D3D_ExternalTextureDirectoryRecurse (newname);

			continue;
		}

		// register the texture
		D3D_RegisterExternalTexture (newname);
	} while (FindNextFile (hFind, &FindFileData));

	// done
	FindClose (hFind);
}


void D3D_EnumExternalTextures (void)
{
	// explicitly none to start with
	d3d_ExternalTextures = (d3d_externaltexture_t **) scratchbuf;
	d3d_MaxExternalTextures = SCRATCHBUF_SIZE / sizeof (d3d_externaltexture_t *);
	d3d_NumExternalTextures = 0;

	// we need 256 of these because textures can - in theory - begin with any allowable byte value
	// add the extra 1 to allow the list to be NULL terminated
	// as for unicode - let's not even go there.
	for (int i = 0; i < 257; i++) d3d_ExternalTextureTable[i] = -1;

	for (searchpath_t *search = com_searchpaths; search; search = search->next)
	{
		if (search->pack)
		{
			pack_t *pak = search->pack;

			for (int i = 0; i < pak->numfiles; i++)
			{
				D3D_RegisterExternalTexture (pak->files[i].name);
			}
		}
		else if (search->pk3)
		{
			pk3_t *pak = search->pk3;

			for (int i = 0; i < pak->numfiles; i++)
			{
				D3D_RegisterExternalTexture (pak->files[i].name);
			}
		}
		else D3D_ExternalTextureDirectoryRecurse (search->filename);
	}

	// no external textures were found
	if (!d3d_NumExternalTextures)
	{
		d3d_ExternalTextures = NULL;
		return;
	}

	// alloc them for real
	d3d_ExternalTextures = (d3d_externaltexture_t **) GameZone->Alloc (d3d_NumExternalTextures * sizeof (d3d_externaltexture_t *));
	memcpy (d3d_ExternalTextures, scratchbuf, d3d_NumExternalTextures * sizeof (d3d_externaltexture_t *));

	for (int i = 0; i < d3d_NumExternalTextures; i++)
	{
		d3d_externaltexture_t *et = d3d_ExternalTextures[i];

		for (int j = 0;; j++)
		{
			if (!et->texpath[j]) break;
			if (et->texpath[j] == '\\') et->texpath[j] = '/';
		}

		// restore drive signifier
		if (et->texpath[1] == ':' && et->texpath[2] == '/') et->texpath[2] = '\\';

		strlwr (et->texpath);
	}

	// sort the list
	qsort (d3d_ExternalTextures, d3d_NumExternalTextures, sizeof (d3d_externaltexture_t *), D3D_ExternalTextureCompareFunc);

	// set up byte pointers and remove dummy sort order extensions
	for (int i = 0; i < d3d_NumExternalTextures; i++)
	{
		d3d_externaltexture_t *et = d3d_ExternalTextures[i];

		// swap non-printing chars
		if (et->basename[0] == '#') et->basename[0] = '*';

		// set up the table pointer
		if (d3d_ExternalTextureTable[et->basename[0]] == -1)
			d3d_ExternalTextureTable[et->basename[0]] = i;

		// remove the extension
		for (int e = strlen (et->basename); e; e--)
		{
			if (et->basename[e] == '.')
			{
				et->basename[e] = 0;
				break;
			}
		}

		// Con_SafePrintf ("registered %s\n", et->basename);
	}
}


void D3D_GenerateTextureList (void)
{
	if (texturelist_dirty)
	{
		if (d3d_TextureNames)
		{
			for (int i = 0;; i++)
			{
				if (!d3d_TextureNames[i]) break;
				MainZone->Free (d3d_TextureNames[i]);
			}

			MainZone->Free (d3d_TextureNames);
			d3d_TextureNames = NULL;
		}

		int numtextures = 0;

		for (d3d_texture_t *tex = d3d_TextureList; tex; tex = tex->next)
		{
			if (!tex->texture->d3d_Texture) continue;
			numtextures++;
		}

		// +1 for NULL termination
		d3d_TextureNames = (char **) MainZone->Alloc ((numtextures + 1) * sizeof (char *));
		numtextures = 0;

		for (d3d_texture_t *tex = d3d_TextureList; tex; tex = tex->next)
		{
			if (!tex->texture->d3d_Texture) continue;

			d3d_TextureNames[numtextures] = (char *) MainZone->Alloc (strlen (tex->texture->identifier) + 1);
			strcpy (d3d_TextureNames[numtextures], tex->texture->identifier);
			d3d_TextureNames[numtextures + 1] = NULL;

			numtextures++;
		}

		texturelist_dirty = false;
	}
}


bool D3D_CopyTextureToClipboard (LPDIRECT3DTEXTURE9 tex)
{
	D3DLOCKED_RECT rect;
	D3DSURFACE_DESC desc;
	LPDIRECT3DSURFACE9 surf = NULL;
	LPDIRECT3DSURFACE9 dest = NULL;
	bool success = false;

	// copy to a new surface in case it's in a different format
	if (FAILED (tex->GetSurfaceLevel (0, &surf))) goto failed_1;
	if (FAILED (surf->GetDesc (&desc))) goto failed_1;
	if (FAILED (d3d_Device->CreateOffscreenPlainSurface (desc.Width, desc.Height, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &dest, NULL))) goto failed_1;
	if (FAILED (D3DXLoadSurfaceFromSurface (dest, NULL, NULL, surf, NULL, NULL, D3DX_FILTER_NONE, 0))) goto failed_1;
	if (FAILED (dest->LockRect (&rect, NULL, 0))) goto failed_1;

	if (OpenClipboard (NULL))
	{
		HANDLE hData = NULL;
		HBITMAP hBitmap = CreateBitmap (desc.Width, desc.Height, 1, 32, rect.pBits);

		if (hBitmap)
		{
			hData = SetClipboardData (CF_BITMAP, hBitmap);
			DeleteObject (hBitmap);
		}

		if (!CloseClipboard ()) goto failed_2;
		if (!hData) goto failed_2;

		// phew!  made it!
		success = true;
	}

	// clean up
failed_2:;
	dest->UnlockRect ();
failed_1:;
	if (dest) dest->Release ();
	if (surf) surf->Release ();
	return success;
}


void D3D_CopyTexture_f (void)
{
	if (Cmd_Argc () != 2)
	{
		Con_Printf ("copytexture <texturename> : copy the specified texture to the clipboard\n");
		Con_Printf ("   if the texture has miplevels, only the first level will be copied\n");
		return;
	}

	char *texturename = Cmd_Argv (1);

	for (d3d_texture_t *tex = d3d_TextureList; tex; tex = tex->next)
	{
		if (!tex->texture->d3d_Texture) continue;

		if (!stricmp (texturename, tex->texture->identifier))
		{
			if (D3D_CopyTextureToClipboard (tex->texture->d3d_Texture))
				Con_Printf ("copytexture : copied %s OK\n", texturename);
			else Con_Printf ("copytexture : failed to copy %s\n", texturename);

			return;
		}
	}

	Con_Printf ("copytexture : Could not find %s\n", texturename);
}


void D3D_TextureList_f (void)
{
	int numtextures = 0;

	for (d3d_texture_t *tex = d3d_TextureList; tex; tex = tex->next)
	{
		if (!tex->texture->d3d_Texture) continue;

		Con_Printf ("%s\n", tex->texture->identifier);
		numtextures++;
	}

	if (numtextures) Con_Printf ("\nListed %i textures\n", numtextures);
}


cmd_t d3d_TextureList_cmd ("texturelist", D3D_TextureList_f);
cmd_t d3d_CopyTexture_cmd ("copytexture", D3D_CopyTexture_f);


void D3DSurf_RegisterTextureChain (image_t *image);
void D3DSurf_FinishTextureChains (void);
extern int d3d_NumTextureChains;

void D3DTexture_RegisterChains (void)
{
	d3d_NumTextureChains = 0;

	for (d3d_texture_t *tex = d3d_TextureList; tex; tex = tex->next)
	{
		if (!tex) continue;
		if (!tex->texture) continue;
		if (!tex->texture->d3d_Texture) continue;
		if (tex->texture->flags & IMAGE_LUMA) continue;
		if (!(tex->texture->flags & IMAGE_BSP)) continue;
		if (tex->texture->RegistrationSequence != d3d_RenderDef.RegistrationSequence) continue;

		D3DSurf_RegisterTextureChain (tex->texture);
	}

	D3DSurf_FinishTextureChains ();
}


