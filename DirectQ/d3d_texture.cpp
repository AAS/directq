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


#include "quakedef.h"
#include "d3d_quake.h"
#include "resource.h"


// leave this at 1 cos texture caching will resolve loading time issues
cvar_t gl_compressedtextures ("gl_texture_compression", 1, CVAR_ARCHIVE);
cvar_t gl_maxtextureretention ("gl_maxtextureretention", 3, CVAR_ARCHIVE);
cvar_t r_lumatextures ("r_lumaintensity", "1", CVAR_ARCHIVE);
extern cvar_t r_overbright;

LPDIRECT3DTEXTURE9 d3d_CurrentTexture[8] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
d3d_texture_t *d3d_FreeTextures = NULL;
int d3d_NumFreeTextures = 0;
#define D3D_MAX_FREE_TEXTURES 128
d3d_texture_t *d3d_Textures = NULL;

void D3D_KillUnderwaterTexture (void);

// other textures we use
extern LPDIRECT3DTEXTURE9 char_texture;
extern LPDIRECT3DTEXTURE9 solidskytexture;
extern LPDIRECT3DTEXTURE9 alphaskytexture;
extern LPDIRECT3DTEXTURE9 simpleskytexture;
LPDIRECT3DTEXTURE9 yahtexture;

// for palette hackery
PALETTEENTRY *texturepal = NULL;
extern PALETTEENTRY lumapal[];

// external textures directory - note - this should be registered before any textures are loaded, otherwise we will attempt to load
// them from /textures
cvar_t gl_texturedir ("gl_texturedir", "textures", CVAR_ARCHIVE);

int D3D_PowerOf2Size (int size)
{
	int pow2;

	// 2003 will let us declare pow2 in the loop and then return it but later versions are standards compliant
	for (pow2 = 1; pow2 < size; pow2 *= 2);

	return pow2;
}


void D3D_InitTextures (void)
{
	// create a block of free textures
	d3d_FreeTextures = (d3d_texture_t *) Pool_Alloc (POOL_PERMANENT, D3D_MAX_FREE_TEXTURES * sizeof (d3d_texture_t));
	d3d_NumFreeTextures = D3D_MAX_FREE_TEXTURES;
}


void D3D_HashTexture (image_t *image)
{
	int datalen = image->height * image->width;

	if (image->flags & IMAGE_32BIT) datalen *= 4;

	// call into here instead
	COM_HashData (image->hash, image->data, datalen);
}


D3DFORMAT D3D_GetTextureFormatForFlags (int flags)
{
	// cut down on storage for luma textures.  we could just use L8 but we need the extra 8
	// bits for storing the original value in so that we can refresh it properly if the overbright mode changes.
	if (flags & IMAGE_LUMA)
	{
		if (d3d_GlobalCaps.supportA8L8)
			return D3DFMT_A8L8;
		else return D3DFMT_A8R8G8B8;
	}

	D3DFORMAT TextureFormat = d3d_GlobalCaps.supportXRGB ? D3DFMT_X8R8G8B8 : D3DFMT_A8R8G8B8;

	// disable compression
	if (!gl_compressedtextures.value) flags |= IMAGE_NOCOMPRESS;

	if (flags & IMAGE_ALPHA)
	{
		if (flags & IMAGE_NOCOMPRESS)
			return D3DFMT_A8R8G8B8;

		if (flags & IMAGE_32BIT)
		{
			// 32 bit textures should never use dxt1
			if (d3d_GlobalCaps.supportDXT3)
				TextureFormat = D3DFMT_DXT3;
			else if (d3d_GlobalCaps.supportDXT5)
				TextureFormat = D3DFMT_DXT5;
			else TextureFormat = D3DFMT_A8R8G8B8;
		}
		else
		{
			// 8 bit textures can use dxt1 as they only need one bit of alpha
			if (d3d_GlobalCaps.supportDXT1)
				TextureFormat = D3DFMT_DXT1;
			else if (d3d_GlobalCaps.supportDXT3)
				TextureFormat = D3DFMT_DXT3;
			else if (d3d_GlobalCaps.supportDXT5)
				TextureFormat = D3DFMT_DXT5;
			else TextureFormat = D3DFMT_A8R8G8B8;
		}
	}
	else
	{
		if (flags & IMAGE_NOCOMPRESS)
			return d3d_GlobalCaps.supportXRGB ? D3DFMT_X8R8G8B8 : D3DFMT_A8R8G8B8;

		// prefer dxt1 but allow other compression formats if available
		if (d3d_GlobalCaps.supportDXT1)
			TextureFormat = D3DFMT_DXT1;
		else if (d3d_GlobalCaps.supportDXT3)
			TextureFormat = D3DFMT_DXT3;
		else if (d3d_GlobalCaps.supportDXT5)
			TextureFormat = D3DFMT_DXT5;
		else if (d3d_GlobalCaps.supportXRGB)
			TextureFormat = D3DFMT_X8R8G8B8;
		else TextureFormat = D3DFMT_A8R8G8B8;
	}

	return TextureFormat;
}


HRESULT D3D_CreateExternalTexture (LPDIRECT3DTEXTURE9 *tex, int len, byte *data, int flags)
{
	// wrap this monster so that we can more easily modify it if required
	hr = D3DXCreateTextureFromFileInMemoryEx
	(
		d3d_Device,
		data,
		len,
		D3DX_DEFAULT,
		D3DX_DEFAULT,
		(flags & IMAGE_MIPMAP) ? D3DX_DEFAULT : 1,
		0,
		D3D_GetTextureFormatForFlags (flags | IMAGE_ALPHA | IMAGE_32BIT),
		D3DPOOL_MANAGED,
		D3DX_FILTER_LINEAR,
		D3DX_FILTER_BOX | D3DX_FILTER_SRGB,
		0,
		NULL,
		NULL,
		tex
	);

	return hr;
}


// types we're going to support - NOTE - link MUST be first in this list!
char *TextureExtensions[] = {"link", "dds", "tga", "bmp", "png", "jpg", "pcx", NULL};

typedef struct pcx_s
{
	char	manufacturer;
	char	version;
	char	encoding;
	char	bits_per_pixel;
	unsigned short	xmin, ymin, xmax, ymax;
	unsigned short	hres,vres;
	unsigned char	palette[48];
	char	reserved;
	char	color_planes;
	unsigned short	bytes_per_line;
	unsigned short	palette_type;
	char	filler[58];
} pcx_t;

bool D3D_LoadPCX (HANDLE fh, LPDIRECT3DTEXTURE9 *tex, int flags, int len)
{
	pcx_t pcx;
	byte *pcx_rgb;
	byte palette[768];

	// seek to end of file (or end of file in PAK)
	// can't use SEEK_END because the file might be in a PAK
	SetFilePointer (fh, len, NULL, FILE_CURRENT);
	SetFilePointer (fh, -768, NULL, FILE_CURRENT);

	// now we can read the palette
	COM_FReadFile (fh, palette, 768);

	// rewind to start pos (essential for PAK support)
	SetFilePointer (fh, -len, NULL, FILE_CURRENT);

	// and now we can read the header in
	COM_FReadFile (fh, &pcx, sizeof (pcx_t));

	// the caller must handle fclose here
	if (pcx.manufacturer != 0x0a || pcx.version != 5 || pcx.encoding != 1 || pcx.bits_per_pixel != 8) return false;

	// alloc memory to hold it (extra space for fake TGA header - see below)
	pcx_rgb = (byte *) Pool_Alloc (POOL_TEMP, (pcx.xmax + 1) * (pcx.ymax + 1) * 4 + 18);

	// because we want to support PAK files here we can't seek from the end.
	// we could make a mess of offsets/etc from the file length, but instead we'll just keep it simple
	for (int y = 0; y <= pcx.ymax; y++)
	{
		byte *pix = &pcx_rgb[18] + 4 * y * (pcx.xmax + 1);

		for (int x = 0; x <= pcx.xmax;)
		{
			int dataByte = COM_FReadChar (fh);
			int runLength;

			if ((dataByte & 0xC0) == 0xC0)
			{
				runLength = dataByte & 0x3F;
				dataByte = COM_FReadChar (fh);
			}
			else runLength = 1;

			while (runLength-- > 0)
			{
				// swap to bgr
				pix[2] = palette[dataByte * 3];
				pix[1] = palette[dataByte * 3 + 1];
				pix[0] = palette[dataByte * 3 + 2];
				pix[3] = 255;

				// advance
				pix += 4;
				x++;
			}
		}
	}

	// here we cheat a little and put a fake TGA header in front of the PCX data so
	// that we can call D3DXCreateTextureFromFileInMemoryEx on it
	memset (pcx_rgb, 0, 18);

	// compose the header
	pcx_rgb[2] = 2;
	pcx_rgb[12] = (pcx.xmax + 1) & 255;
	pcx_rgb[13] = (pcx.xmax + 1) >> 8;
	pcx_rgb[14] = (pcx.ymax + 1) & 255;
	pcx_rgb[15] = (pcx.ymax + 1) >> 8;
	pcx_rgb[16] = 32;
	pcx_rgb[17] = 0x20;

	if (((pcx.xmax + 1) % 4) || ((pcx.ymax + 1) % 4)) flags |= IMAGE_NOCOMPRESS;

	// attempt to load it (this will generate miplevels for us too)
	hr = D3D_CreateExternalTexture (tex, (pcx.xmax + 1) * (pcx.ymax + 1) * 4 + 18, pcx_rgb, flags);

	// close before checking
	COM_FCloseFile (&fh);

	if (FAILED (hr)) return false;

	return true;
}


bool D3D_LoadExternalTexture (LPDIRECT3DTEXTURE9 *tex, char *filename, int flags)
{
	// allow disabling of check for external replacement (speed)
	if (flags & IMAGE_NOEXTERN) return false;

	// add 3 for handling links.
	char workingname[259];

	if (flags & IMAGE_ALIAS)
	{
		// DP non-standard bullshit
		_snprintf (workingname, 255, "%s.blah", filename);
	}
	else
	{
		// copy the name out so that we can safely modify it
		strncpy (workingname, filename, 255);

		// ensure that we have some kind of extension on it - anything will do
		COM_DefaultExtension (workingname, ".blah");
	}

	// change * to #
	for (int c = 0; ; c++)
	{
		if (workingname[c] == 0) break;
		if (workingname[c] == '*') workingname[c] = '#';
	}

	for (int i = 0; ; i++)
	{
		// ran out of extensions
		if (!TextureExtensions[i]) break;

		// set the correct extension
		for (int c = strlen (workingname) - 1; c; c--)
		{
			if (workingname[c] == '.')
			{
				strcpy (&workingname[c + 1], TextureExtensions[i]);
				break;
			}
		}

		HANDLE fh = INVALID_HANDLE_VALUE;
		int filelen = COM_FOpenFile (workingname, &fh);

		if (fh == INVALID_HANDLE_VALUE) continue;

		// make this a bit more robust that i == 0
		if (!strcmp (TextureExtensions[i], "link"))
		{
			// handle link format
			for (int c = strlen (workingname) - 1; c; c--)
			{
				if (workingname[c] == '/' || workingname[c] == '\\')
				{
					for (int cc = 0; ; cc++)
					{
						char fc = COM_FReadChar (fh);

						if (fc == '\n' || fc < 1 || fc == '\r') break;

						workingname[c + cc + 1] = fc;
						workingname[c + cc + 2] = 0;
					}

					break;
				}
			}

			// done with the file
			COM_FCloseFile (&fh);

			// the system needs an extension so let's just give it a dummy one
			// (the above process causes the extension to be lost
			COM_DefaultExtension (workingname, ".blah");

			// skip the rest; we check link first so that it will fall through to the other standard types
			continue;
		}
		else if (!strcmp (TextureExtensions[i], "pcx"))
		{
			// D3DX can't handle PCX formats so we have our own loader
			if (D3D_LoadPCX (fh, tex, flags, filelen)) return true;

			// we may want to add other formats after PCX...
			COM_FCloseFile (&fh);
			continue;
		}

		// allocate a buffer to hold it
		byte *filebuf = (byte *) Pool_Alloc (POOL_TEMP, filelen);

		// read it all in
		COM_FReadFile (fh, filebuf, filelen);

		// done with the file
		COM_FCloseFile (&fh);

		// attempt to load it (this will generate miplevels for us too)
		hr = D3D_CreateExternalTexture (tex, filelen, filebuf, flags);

		if (FAILED (hr))
		{
			// the reason for failure may be because the compressed format was unsupported for the texture dimensions,
			// so try it again without compression before giving up
			hr = D3D_CreateExternalTexture (tex, filelen, filebuf, (flags | IMAGE_NOCOMPRESS));

			if (FAILED (hr)) continue;
		}

		// load succeeded
		return true;
	}

	// didn't find it
	return false;
}


void D3D_LoadResourceTexture (LPDIRECT3DTEXTURE9 *tex, int ResourceID, int flags)
{
	hr = D3DXCreateTextureFromResourceEx
	(
		d3d_Device,
		NULL,
		MAKEINTRESOURCE (ResourceID),
		D3DX_DEFAULT,
		D3DX_DEFAULT,
		(flags & IMAGE_MIPMAP) ? D3DX_DEFAULT : 1,
		0,
		D3D_GetTextureFormatForFlags (flags | IMAGE_ALPHA | IMAGE_32BIT | IMAGE_NOCOMPRESS),
		D3DPOOL_MANAGED,
		D3DX_FILTER_LINEAR,
		D3DX_FILTER_BOX | D3DX_FILTER_SRGB,
		0,
		NULL,
		NULL,
		tex
	);

	if (FAILED (hr))
	{
		// a resource texture failing is a program crash bug
		Sys_Error ("D3D_LoadResourceTexture: Failed to create texture");
		return;
	}

	// not much more we need to do here...
}


void D3D_LoadTexture (LPDIRECT3DTEXTURE9 *tex, image_t *image)
{
	// scaled image
	image_t scaled;

	// check scaling here first
	scaled.width = D3D_PowerOf2Size (image->width);
	scaled.height = D3D_PowerOf2Size (image->height);

	// clamp to max texture size
	if (scaled.width > d3d_DeviceCaps.MaxTextureWidth) scaled.width = d3d_DeviceCaps.MaxTextureWidth;
	if (scaled.height > d3d_DeviceCaps.MaxTextureHeight) scaled.height = d3d_DeviceCaps.MaxTextureHeight;

	// d3d specifies that compressed formats should be a multiple of 4
	// would you believe marcher has a 1x1 texture in it?
	// because we're doing powers of 2, every power of 2 above 4 is going to be a multiple of 4 also
	if ((scaled.width < 4) || (scaled.height < 4)) image->flags |= IMAGE_NOCOMPRESS;

	// sky can have large flat spaces of the same colour, meaning it doesn't compress so good
	if (!strnicmp (image->identifier, "sky", 3)) image->flags |= IMAGE_NOCOMPRESS;

	// create the texture at the scaled size
	hr = d3d_Device->CreateTexture
	(
		scaled.width,
		scaled.height,
		(image->flags & IMAGE_MIPMAP) ? 0 : 1,
		0,
		D3D_GetTextureFormatForFlags (image->flags),
		D3DPOOL_MANAGED,
		tex,
		NULL
	);

	if (FAILED (hr))
	{
		Sys_Error ("D3D_LoadTexture: d3d_Device->CreateTexture failed");
		return;
	}

	LPDIRECT3DSURFACE9 texsurf = NULL;
	(*tex)->GetSurfaceLevel (0, &texsurf);

	RECT SrcRect;
	SrcRect.top = 0;
	SrcRect.left = 0;
	SrcRect.bottom = image->height;
	SrcRect.right = image->width;

	DWORD FilterFlags = 0;

	if (scaled.width == image->width && scaled.height == image->height)
	{
		// no need
		FilterFlags |= D3DX_FILTER_NONE;
	}
	else
	{
		// higher quality filters blur and fade excessively
		FilterFlags |= D3DX_FILTER_LINEAR;

		// it's generally assumed that mipmapped textures should also wrap
		if (image->flags & IMAGE_MIPMAP) FilterFlags |= (D3DX_FILTER_MIRROR_U | D3DX_FILTER_MIRROR_V);
	}

	if (!texturepal)
	{
		texturepal = (PALETTEENTRY *) Pool_Alloc (POOL_PERMANENT, 256 * sizeof (PALETTEENTRY));

		for (int i = 0; i < 256; i++)
		{
			// per doc, peFlags is used for alpha
			// invert again so that the layout will be correct
			texturepal[i].peRed = ((byte *) &d_8to24table[i])[2];
			texturepal[i].peGreen = ((byte *) &d_8to24table[i])[1];
			texturepal[i].peBlue = ((byte *) &d_8to24table[i])[0];
			texturepal[i].peFlags = ((byte *) &d_8to24table[i])[3];
		}
	}

	// default to the standard palette
	PALETTEENTRY *activepal = texturepal;

	// switch the palette
	if (image->flags & IMAGE_HALFLIFE) activepal = (PALETTEENTRY *) image->palette;
	if (image->flags & IMAGE_LUMA) activepal = lumapal;

	hr = D3DXLoadSurfaceFromMemory
	(
		texsurf,
		NULL,
		NULL,
		image->data,
		(image->flags & IMAGE_32BIT) ? D3DFMT_A8R8G8B8 : D3DFMT_P8,
		(image->flags & IMAGE_32BIT) ? image->width * 4 : image->width,
		(image->flags & IMAGE_32BIT) ? NULL : activepal,
		&SrcRect,
		FilterFlags,
		0
	);

	if (FAILED (hr))
	{
		Sys_Error ("D3D_LoadTexture: D3DXLoadSurfaceFromMemory failed");
		return;
	}

	texsurf->Release ();

	// good old box filter - triangle is way too slow, linear doesn't work well for mipmapping
	if (image->flags & IMAGE_MIPMAP) D3DXFilterTexture (*tex, NULL, 0, D3DX_FILTER_BOX | D3DX_FILTER_SRGB);

	// tell Direct 3D that we're going to be needing to use this managed resource shortly
	(*tex)->PreLoad ();
}


void D3D_LoadTexture (LPDIRECT3DTEXTURE9 *tex, int width, int height, byte *data, unsigned int *palette, bool mipmap, bool alpha)
{
	// create an image struct for it
	image_t image;

	image.data = data;
	image.flags = 0;
	image.height = height;
	image.width = width;
	image.palette = palette;

	if (mipmap) image.flags |= IMAGE_MIPMAP;
	if (alpha) image.flags |= IMAGE_ALPHA;
	if (!palette) image.flags |= IMAGE_32BIT;

	// upload direct without going through the texture cache, as we already have a texture object for this
	D3D_LoadTexture (tex, &image);
}


byte D3D_ScaleLumaTexel (int lumain)
{
	return BYTE_CLAMP (((int) ((float) lumain * r_lumatextures.value)) >> r_overbright.integer);
}


bool D3D_MakeLumaTexture (LPDIRECT3DTEXTURE9 tex)
{
	// bound 0 to 2 - (float) 0 is required for overload
	if (r_overbright.integer < 0) Cvar_Set (&r_overbright, (float) 0);
	if (r_overbright.integer > 2) Cvar_Set (&r_overbright, 2);

	D3DSURFACE_DESC Level0Desc;
	D3DLOCKED_RECT Level0Rect;

	// because we're using A8L8 for lumas we can now lock the rect directly instead of having to go through an intermediate surface
	tex->GetLevelDesc (0, &Level0Desc);
	tex->LockRect (0, &Level0Rect, NULL, D3DLOCK_NO_DIRTY_UPDATE);

	bool isrealluma = false;

	if (d3d_GlobalCaps.supportA8L8)
	{
		for (int i = 0; i < Level0Desc.Width * Level0Desc.Height; i++)
		{
			// A8L8 maps to unsigned short
			byte *a8l8 = (byte *) (&((unsigned short *) Level0Rect.pBits)[i]);

			// store the original value in the alpha channel
			a8l8[1] = a8l8[0];

			// check for if it's really a luma
			if (a8l8[1]) isrealluma = true;

			// scale for overbrights
			a8l8[0] = D3D_ScaleLumaTexel (a8l8[1]);
		}
	}
	else
	{
		for (int i = 0; i < Level0Desc.Width * Level0Desc.Height; i++)
		{
			// A8R8G8B8 maps to unsigned int
			byte *argb = (byte *) (&((unsigned int *) Level0Rect.pBits)[i]);

			// store the original value in the alpha channel
			argb[3] = argb[0];

			// check for if it's really a luma
			if (argb[3]) isrealluma = true;

			// scale for overbrights
			argb[0] = argb[1] = argb[2] = D3D_ScaleLumaTexel (argb[3]);
		}
	}

	// unlock and rebuild the mipmap chain
	tex->UnlockRect (0);
	D3DXFilterTexture (tex, NULL, 0, D3DX_FILTER_BOX | D3DX_FILTER_SRGB);
	tex->AddDirtyRect (NULL);

	return isrealluma;
}


void D3D_TranslateAlphaTexture (int r, int g, int b, LPDIRECT3DTEXTURE9 tex)
{
	D3DSURFACE_DESC Level0Desc;
	D3DLOCKED_RECT Level0Rect;

	tex->GetLevelDesc (0, &Level0Desc);
	tex->LockRect (0, &Level0Rect, NULL, 0);

	for (int i = 0; i < Level0Desc.Width * Level0Desc.Height; i++)
	{
		byte *rgba = (byte *) (&((unsigned int *) Level0Rect.pBits)[i]);

		rgba[0] = BYTE_CLAMP (BYTE_CLAMP (b) * rgba[3] / 255);
		rgba[1] = BYTE_CLAMP (BYTE_CLAMP (g) * rgba[3] / 255);
		rgba[2] = BYTE_CLAMP (BYTE_CLAMP (r) * rgba[3] / 255);
	}

	tex->UnlockRect (0);
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

LPDIRECT3DTEXTURE9 D3D_LoadTexture (image_t *image)
{
	// detect water textures
	if (image->identifier[0] == '*')
	{
		// don't load lumas for liquid textures
		if (image->flags & IMAGE_LUMA) return NULL;

		// flag it early so that it gets stored in the struct
		image->flags |= IMAGE_LIQUID;
	}

	bool hasluma = false;

	// check native texture for a luma
	if ((image->flags & IMAGE_LUMA) && !(image->flags & IMAGE_32BIT))
	{
		for (int i = 0; i < image->width * image->height; i++)
		{
			if (vid.fullbright[image->data[i]])
			{
				hasluma = true;
				break;
			}
		}
	}

	if (image->flags & IMAGE_LUMA)
	{
		// change the identifier so that we can load an external texture properly
		strcat (image->identifier, "_luma");
	}

	// take a hash of the image data
	D3D_HashTexture (image);

	// stores a free texture slot
	d3d_texture_t *freetex = NULL;

	// look for a match
	for (d3d_texture_t *t = d3d_Textures; t; t = t->next)
	{
		// look for a free slot
		if (!t->d3d_Texture)
		{
			// got one
			freetex = t;
			continue;
		}

		// it's not beyond the bounds of possibility that we might have 2 textures with the same
		// data but different usage, so here we check for it and accomodate it (this will happen with lumas
		// where the hash will match, so it remains a valid check)
		if (image->flags != t->TexImage.flags) continue;

		// compare the hash and reuse if it matches
		if (COM_CheckHash (image->hash, t->TexImage.hash))
		{
			// set last usage to 0
			t->LastUsage = 0;

			// return it
			return t->d3d_Texture;
		}
	}

	// the actual texture we're going to load
	d3d_texture_t *tex = NULL;

	if (!freetex)
	{
		// check for free textures
		if (d3d_NumFreeTextures <= 0) D3D_InitTextures ();

		// take a texture off the texture list
		tex = &d3d_FreeTextures[--d3d_NumFreeTextures];

		// link it in
		tex->next = d3d_Textures;
		d3d_Textures = tex;
	}
	else
	{
		// reuse the free slot
		tex = freetex;
	}

	// fill in the struct
	tex->LastUsage = 0;
	tex->d3d_Texture = NULL;

	// copy the image
	memcpy (&tex->TexImage, image, sizeof (image_t));

	// check for textures that have the same name but different data, and amend the name by the QRP standard
	if (COM_CheckHash (image->hash, plat_top1_cable))
		strcat (image->identifier, "_cable");
	else if (COM_CheckHash (image->hash, plat_top1_bolt))
		strcat (image->identifier, "_bolt");
	else if (COM_CheckHash (image->hash, metal5_2_arc))
		strcat (image->identifier, "_arc");
	else if (COM_CheckHash (image->hash, metal5_2_x))
		strcat (image->identifier, "_x");
	else if (COM_CheckHash (image->hash, metal5_4_arc))
		strcat (image->identifier, "_arc");
	else if (COM_CheckHash (image->hash, metal5_4_double))
		strcat (image->identifier, "_double");
	else if (COM_CheckHash (image->hash, metal5_8_back))
		strcat (image->identifier, "_back");
	else if (COM_CheckHash (image->hash, metal5_8_rune))
		strcat (image->identifier, "_rune");
	else if (COM_CheckHash (image->hash, sky_blue_solid))
		strcpy (image->identifier, "sky4_solid");
	else if (COM_CheckHash (image->hash, sky_blue_alpha))
		strcpy (image->identifier, "sky4_alpha");
	else if (COM_CheckHash (image->hash, sky_purp_solid))
		strcpy (image->identifier, "sky1_solid");
	else if (COM_CheckHash (image->hash, sky_purp_alpha))
		strcpy (image->identifier, "sky1_alpha");

	// hack the colour for certain models
	// fix white line at base of shotgun shells box
	if (COM_CheckHash (image->hash, ShotgunShells))
		memcpy (image->data, image->data + 32 * 31, 32);

	COM_CheckContentDirectory (&gl_texturedir, false);

	// try to load an external texture
	bool externalloaded = D3D_LoadExternalTexture (&tex->d3d_Texture, va ("%s/%s", gl_texturedir.string, image->identifier), image->flags);

	// DP alias skin texture dir non-standard bullshit support
	if ((image->flags & IMAGE_ALIAS) && !externalloaded)
		externalloaded = D3D_LoadExternalTexture (&tex->d3d_Texture, image->identifier, image->flags);

	if (!externalloaded)
	{
		// test for a native luma texture
		if ((image->flags & IMAGE_LUMA) && !hasluma)
		{
			// if we got neither a native nor an external luma we must cancel it and return NULL
			tex->LastUsage = 666;
			SAFE_RELEASE (tex->d3d_Texture);
			memcpy (tex->TexImage.hash, no_match_hash, 16);
			return NULL;
		}

		// upload through direct 3d
		D3D_LoadTexture (&tex->d3d_Texture, image);
	}

	// make the luma representation of the texture
	if (image->flags & IMAGE_LUMA)
	{
		// if it's not really a luma (should never happen...) clear it and return NULL
		if (!D3D_MakeLumaTexture (tex->d3d_Texture))
		{
			tex->LastUsage = 666;
			SAFE_RELEASE (tex->d3d_Texture);
			memcpy (tex->TexImage.hash, no_match_hash, 16);
			return NULL;
		}

		// SCR_WriteTextureToTGA (va ("%s.tga", tex->TexImage.identifier), tex->d3d_Texture);
		//Con_Printf ("Uploaded luma for %s\n", tex->TexImage.identifier);
	}

	// return the texture we got
	return tex->d3d_Texture;
}


LPDIRECT3DTEXTURE9 D3D_LoadTexture (char *identifier, int width, int height, byte *data, bool mipmap, bool alpha)
{
	image_t image;

	image.data = data;
	image.flags = 0;
	image.height = height;
	image.width = width;
	image.palette = d_8to24table;

	strcpy (image.identifier, identifier);

	if (mipmap) image.flags |= IMAGE_MIPMAP;
	if (alpha) image.flags |= IMAGE_ALPHA;

	return D3D_LoadTexture (&image);
}


LPDIRECT3DTEXTURE9 D3D_LoadTexture (char *identifier, int width, int height, byte *data, int flags)
{
	image_t image;

	image.data = data;
	image.flags = flags;
	image.height = height;
	image.width = width;
	image.palette = d_8to24table;

	strcpy (image.identifier, identifier);

	// detect water textures
	if (identifier[0] == '*') image.flags |= IMAGE_LIQUID;

	return D3D_LoadTexture (&image);
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


LPDIRECT3DTEXTURE9 D3D_LoadTexture (miptex_t *mt, int flags)
{
	image_t image;

	image.data = ((byte *) mt) + mt->offsets[0];
	image.flags = flags;
	image.height = mt->height;
	image.width = mt->width;

	// set the correct palette
	if (flags & IMAGE_HALFLIFE)
		image.palette = D3D_MakeTexturePalette (mt);
	else image.palette = d_8to24table;

	// this needs to come after the palette as the texture name may be modified
	strcpy (image.identifier, mt->name);

	return D3D_LoadTexture (&image);
}


LPDIRECT3DTEXTURE9 D3D_LoadTexture (int width, int height, byte *data, int flags)
{
	image_t image;

	image.data = data;
	image.flags = flags;
	image.height = height;
	image.width = width;
	image.palette = NULL;
	image.identifier[0] = 0;

	return D3D_LoadTexture (&image);
}


void R_ReleaseResourceTextures (void);
void D3D_ShutdownHLSL (void);

void D3D_ReleaseTextures (void)
{
	// other textures we need to release that we don't define globally
	extern LPDIRECT3DTEXTURE9 d3d_MapshotTexture;
	extern LPDIRECT3DTEXTURE9 R_PaletteTexture;
	extern LPDIRECT3DTEXTURE9 r_blacktexture;
	extern LPDIRECT3DTEXTURE9 skyboxtextures[];
	extern LPDIRECT3DTEXTURE9 d3d_PlayerSkins[];

	// release cached textures
	for (d3d_texture_t *tex = d3d_Textures; tex; tex = tex->next)
	{
		SAFE_RELEASE (tex->d3d_Texture);

		// set the hash value to ensure that it will never match
		// (needed because game changing goes through this path too)
		memcpy (tex->TexImage.hash, no_match_hash, 16);
	}

	// release player textures
	for (int i = 0; i < 256; i++)
		SAFE_RELEASE (d3d_PlayerSkins[i]);

	// release lightmaps too
	SAFE_DELETE (d3d_Lightmaps);

	D3D_KillUnderwaterTexture ();
	D3D_ShutdownHLSL ();

	// resource textures
	R_ReleaseResourceTextures ();

	// other textures
	SAFE_RELEASE (char_texture);
	SAFE_RELEASE (solidskytexture);
	SAFE_RELEASE (alphaskytexture);
	SAFE_RELEASE (simpleskytexture);
	SAFE_RELEASE (d3d_MapshotTexture);
	SAFE_RELEASE (R_PaletteTexture);
	SAFE_RELEASE (r_blacktexture);
	SAFE_RELEASE (r_notexture);
	SAFE_RELEASE (skyboxtextures[0]);
	SAFE_RELEASE (skyboxtextures[1]);
	SAFE_RELEASE (skyboxtextures[2]);
	SAFE_RELEASE (skyboxtextures[3]);
	SAFE_RELEASE (skyboxtextures[4]);
	SAFE_RELEASE (skyboxtextures[5]);
}


void D3D_PreloadTextures (void)
{
	for (d3d_texture_t *tex = d3d_Textures; tex; tex = tex->next)
	{
		// pull it back to vram
		if (tex->d3d_Texture)
		{
			// keep the texture hot
			tex->d3d_Texture->SetPriority (512);
			tex->d3d_Texture->PreLoad ();
		}
	}
}


void D3D_FlushTextures (void)
{
	int numflush = 0;

	// sanity check
	if (gl_maxtextureretention.value < 2) Cvar_Set (&gl_maxtextureretention, 2);

	for (d3d_texture_t *tex = d3d_Textures; tex; tex = tex->next)
	{
		// all textures just loaded in the cache will have lastusage set to 0
		// incremenent lastusage for types we want to flush.
		// alias and sprite models are cached between maps so don't ever free them
		if (tex->TexImage.flags & IMAGE_BSP) tex->LastUsage++;

		// always preserve these types irrespective
		if (tex->TexImage.flags & IMAGE_PRESERVE) tex->LastUsage = 0;

		if (tex->LastUsage < 2 && tex->d3d_Texture)
		{
			// assign a higher priority to the texture
			tex->d3d_Texture->SetPriority (256);
		}
		else if (tex->d3d_Texture)
		{
			// assign a lower priority so that recently loaded textures can be preferred to be kept in vram
			tex->d3d_Texture->SetPriority (0);
		}

		if (tex->LastUsage > gl_maxtextureretention.value && tex->d3d_Texture)
		{
			// if the texture hasn't been used in 4 maps, we flush it
			// this means that texture flushes will start happening about exm4
			// (we might cvar-ize this as an option for players to influence the flushing
			// policy, *VERY* handy if they have lots of *HUGE* textures.
			SAFE_RELEASE (tex->d3d_Texture);

			// set the hash value to ensure that it will never match
			memcpy (tex->TexImage.hash, no_match_hash, 16);

			// increment number flushed
			numflush++;
		}
		else
		{
			// pull it back to vram
			if (tex->d3d_Texture)
			{
				// keep the texture hot
				tex->d3d_Texture->SetPriority (512);
				tex->d3d_Texture->PreLoad ();
			}
		}
	}

	Con_DPrintf ("Flushed %i textures\n", numflush);
	Con_DPrintf ("Available texture memory: %0.3f MB\n", ((float) d3d_Device->GetAvailableTextureMem () / 1024.0f) / 1024.0f);
}


void D3D_EvictTextures (void)
{
	// just because host.cpp doesn't know what a LPDIRECT3DDEVICE9 is...
	d3d_Device->EvictManagedResources ();
}


bool D3D_CheckTextureFormat (D3DFORMAT textureformat, BOOL mandatory)
{
	// test texture
	LPDIRECT3DTEXTURE9 tex;

	// check for compressed texture formats
	// rather than using CheckDeviceFormat we actually try to create one and see what happens
	hr = d3d_Device->CreateTexture
	(
		128,
		128,
		1,
		0,
		textureformat,
		D3DPOOL_DEFAULT,
		&tex,
		NULL
	);

	if (SUCCEEDED (hr))
	{
		// done with the texture
		tex->Release ();

		if (!mandatory)
			Con_Printf ("Allowing %s Texture Format\n", D3DTypeToString (textureformat));

		return true;
	}

	if (mandatory)
		Sys_Error ("Texture format %s is not supported", D3DTypeToString (textureformat));

	return false;
}


void D3D_RescaleIndividualLuma (LPDIRECT3DTEXTURE9 tex)
{
	// bound 0 to 2 - (float) 0 is required for overload
	if (r_overbright.integer < 0) Cvar_Set (&r_overbright, (float) 0);
	if (r_overbright.integer > 2) Cvar_Set (&r_overbright, 2);

	D3DSURFACE_DESC Level0Desc;
	D3DLOCKED_RECT Level0Rect;

	// because we're using A8L8 for lumas we can now lock the rect directly instead of having to go through an intermediate surface
	tex->GetLevelDesc (0, &Level0Desc);
	tex->LockRect (0, &Level0Rect, NULL, D3DLOCK_NO_DIRTY_UPDATE);

	if (d3d_GlobalCaps.supportA8L8)
	{
		for (int i = 0; i < Level0Desc.Width * Level0Desc.Height; i++)
		{
			// A8L8 maps to unsigned short
			byte *a8l8 = (byte *) (&((unsigned short *) Level0Rect.pBits)[i]);

			// scale for overbrights
			a8l8[0] = D3D_ScaleLumaTexel (a8l8[1]);
		}
	}
	else
	{
		for (int i = 0; i < Level0Desc.Width * Level0Desc.Height; i++)
		{
			// A8R8G8B8 maps to unsigned short
			byte *argb = (byte *) (&((unsigned int *) Level0Rect.pBits)[i]);

			// scale for overbrights
			argb[0] = argb[1] = argb[2] = D3D_ScaleLumaTexel (argb[3]);
		}
	}

	// unlock and rebuild the mipmap chain
	tex->UnlockRect (0);
	D3DXFilterTexture (tex, NULL, 0, D3DX_FILTER_BOX | D3DX_FILTER_SRGB);
	tex->AddDirtyRect (NULL);
}


void D3D_RescaleLumas (void)
{
	static int r_oldoverbright = r_overbright.integer;
	static int r_oldlumatextures = (int) (r_lumatextures.value * 10);

	// restrict cvar values to supported modes
	if (!(d3d_DeviceCaps.TextureOpCaps & D3DTEXOPCAPS_MODULATE4X) && r_overbright.integer > 1) Cvar_Set (&r_overbright, 1);
	if (!(d3d_DeviceCaps.TextureOpCaps & D3DTEXOPCAPS_MODULATE2X) && r_overbright.integer > 0) Cvar_Set (&r_overbright, (float) 0);

	// forbid use of the 3 TMU path if we only have 2 TMUs
	if (d3d_DeviceCaps.MaxTextureBlendStages < 3) Cvar_Set (&r_lumatextures, (float) 0);

	// bound 0 to 2 - (float) 0 is required for overload
	if (r_overbright.integer < 0) Cvar_Set (&r_overbright, (float) 0);
	if (r_overbright.integer > 2) Cvar_Set (&r_overbright, 2);

	if (r_oldoverbright != r_overbright.integer || r_oldlumatextures != (int) (r_lumatextures.value * 10))
	{
		r_oldoverbright = r_overbright.integer;
		r_oldlumatextures = (int) (r_lumatextures.value * 10);

		for (d3d_texture_t *t = d3d_Textures; t; t = t->next)
		{
			if (!t->d3d_Texture) continue;
			if (!(t->TexImage.flags & IMAGE_LUMA)) continue;

			D3D_RescaleIndividualLuma (t->d3d_Texture);
		}
	}
}

