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
#include <assert.h>

// used for generating md5 hashes
#include <wincrypt.h>


LPDIRECT3DTEXTURE9 d3d_CurrentTexture[8] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
d3d_texture_t *d3d_Textures = NULL;

void D3D_ReleaseLightmaps (void);
void D3D_KillUnderwaterTexture (void);

// other textures we use
extern LPDIRECT3DTEXTURE9 playertextures[];
extern LPDIRECT3DTEXTURE9 char_texture;
extern LPDIRECT3DTEXTURE9 solidskytexture;
extern LPDIRECT3DTEXTURE9 alphaskytexture;

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
	// this used to register a cvar; it does nothing now.  we'll keep it in case we ever want to put something in here.
}


void D3D_HashTexture (image_t *image)
{
	// generate an MD5 hash of an image's data
	HCRYPTPROV hCryptProv;
	HCRYPTHASH hHash;

	// acquire the cryptographic context (can we cache this?)
	if (CryptAcquireContext (&hCryptProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT | CRYPT_MACHINE_KEYSET))
	{
		// create a hashing algorithm (can we cache this?)
		if (CryptCreateHash (hCryptProv, CALG_MD5, 0, 0, &hHash))
		{
			int datalen = image->height * image->width;

			if (image->flags & IMAGE_32BIT) datalen *= 4;

			// hash the data
			if (CryptHashData (hHash, image->data, datalen, 0))
			{
				DWORD dwHashLen = 16;

				// retrieve the hash
				if (CryptGetHashParam (hHash, HP_HASHVAL, image->hash, &dwHashLen, 0)) 
				{
					// hashed OK
				}
				else
				{
					// oh crap
 					Sys_Error ("D3D_HashTexture: CryptGetHashParam failed");
				}
			}
			else
			{
				// oh crap
				Sys_Error ("D3D_HashTexture: CryptHashData failed");
			}

			CryptDestroyHash (hHash); 
		}
		else
		{
			// oh crap
			Sys_Error ("D3D_HashTexture: CryptCreateHash failed");
		}

	    CryptReleaseContext (hCryptProv, 0);
	}
	else
	{
		// oh crap
		Sys_Error ("D3D_HashTexture: CryptAcquireContext failed");
	}
}


void D3D_ResampleTexture (image_t *src, image_t *dst)
{
	int w, h;
	byte *nwpx, *nepx, *swpx, *sepx, *dest;
	unsigned xfrac, yfrac, x, y, modx, mody, imodx, imody, injump, outjump;
	unsigned *outdata = (unsigned *) dst->data;

	xfrac = ((src->width - 1) << 16) / (dst->width - 1);
	yfrac = ((src->height - 1) << 16) / (dst->height - 1);

	y = outjump = 0;

	for (h = 0; h < dst->height; h++)
	{
		mody = (y >> 8) & 0xFF;
		imody = 256 - mody;
		injump = (y >> 16) * src->width;
		x = 0;

		for (w = 0; w < dst->width; w++)
		{
			modx = (x >> 8) & 0xFF;
			imodx = 256 - modx;

			// set up the data points
			if (src->flags & IMAGE_32BIT)
			{
				// just resample
				nwpx = src->data + (((x >> 16) + injump) << 2);
				nepx = nwpx + 4;
				swpx = nwpx + (src->width << 2);
				sepx = swpx + 4;
			}
			else if (src->palette)
			{
				// get base offset
				byte *base = src->data + (x >> 16) + injump;

				// in-place upsampling and resampling
				nwpx = (byte *) &src->palette[*base];
				nepx = (byte *) &src->palette[*(base + 1)];
				swpx = (byte *) &src->palette[*(base + src->width)];
				sepx = (byte *) &src->palette[*(base + src->width + 1)];
			}
			else
			{
				// it's my fault if this ever happens
				Sys_Error ("D3D_ResampleTexture: !(src->flags & IMAGE_32BIT) without src->palette");
				return;
			}

			// get a pointer to the outdata destination
			dest = (byte *) (outdata + outjump + w);

			dest[0] = (nwpx[0] * imodx * imody + nepx[0] * modx * imody + swpx[0] * imodx * mody + sepx[0] * modx * mody) >> 16;
			dest[1] = (nwpx[1] * imodx * imody + nepx[1] * modx * imody + swpx[1] * imodx * mody + sepx[1] * modx * mody) >> 16;
			dest[2] = (nwpx[2] * imodx * imody + nepx[2] * modx * imody + swpx[2] * imodx * mody + sepx[2] * modx * mody) >> 16;
			dest[3] = (nwpx[3] * imodx * imody + nepx[3] * modx * imody + swpx[3] * imodx * mody + sepx[3] * modx * mody) >> 16;

			x += xfrac;
		}

		outjump += dst->width;
		y += yfrac;
	}
}


HRESULT D3D_CreateExternalTexture (LPDIRECT3DTEXTURE9 *tex, int len, byte *data, int flags)
{
	// wrap this monster so that we can more easily modify it if required
	HRESULT hr = D3DXCreateTextureFromFileInMemoryEx
	(
		d3d_Device,
		data,
		len,
		D3DX_DEFAULT,
		D3DX_DEFAULT,
		(flags & IMAGE_MIPMAP) ? D3DX_DEFAULT : 1,
		0,
		D3DFMT_A8R8G8B8,
		D3DPOOL_MANAGED,
		D3DX_FILTER_TRIANGLE | D3DX_FILTER_DITHER,
		D3DX_FILTER_BOX,
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

bool D3D_LoadPCX (FILE *f, LPDIRECT3DTEXTURE9 *tex, int flags, int len)
{
	pcx_t pcx;
	byte *pcx_rgb;
	byte palette[768];

	// seek to end of file (or end of file in PAK)
	// can't use SEEK_END because the file might be in a PAK
	fseek (f, len, SEEK_CUR);
	fseek (f, -768, SEEK_CUR);

	// now we can read the palette
	fread (palette, 1, 768, f);

	// rewind to start pos (essential for PAK support)
	fseek (f, -len, SEEK_CUR);

	// and now we can read the header in
	fread (&pcx, sizeof (pcx_t), 1, f);

	// the caller must handle fclose here
	if (pcx.manufacturer != 0x0a || pcx.version != 5 || pcx.encoding != 1 || pcx.bits_per_pixel != 8) return false;

	// alloc memory to hold it (extra space for fake TGA header - see below)
	pcx_rgb = (byte *) Heap_QMalloc ((pcx.xmax + 1) * (pcx.ymax + 1) * 4 + 18);

	// because we want to support PAK files here we can't seek from the end.
	// we could make a mess of offsets/etc from the file length, but instead we'll just keep it simple
	for (int y = 0; y <= pcx.ymax; y++)
	{
		byte *pix = &pcx_rgb[18] + 4 * y * (pcx.xmax + 1);

		for (int x = 0; x <= pcx.xmax;)
		{
			int dataByte = fgetc (f);
			int runLength;

			if ((dataByte & 0xC0) == 0xC0)
			{
				runLength = dataByte & 0x3F;
				dataByte = fgetc (f);
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

	// attempt to load it (this will generate miplevels for us too)
	HRESULT hr = D3D_CreateExternalTexture (tex, (pcx.xmax + 1) * (pcx.ymax + 1) * 4 + 18, pcx_rgb, flags);

	// ensure that the buffer is released before checking for success, as otherwise we'll leak it
	Heap_QFree (pcx_rgb);

	if (FAILED (hr)) return false;

	fclose (f);
	return true;
}

bool D3D_LoadExternalTexture (LPDIRECT3DTEXTURE9 *tex, char *filename, int flags)
{
	char workingname[256];

	// copy the name out so that we can safely modify it
	strncpy (workingname, filename, 255);

	// ensure that we have some kind of extension on it - anything will do
	COM_DefaultExtension (workingname, ".blah");

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

		FILE *f;
		int filelen = COM_FOpenFile (workingname, &f);

		if (!f) continue;

		// make this a bit more robust that i == 0
		if (!strcmp (TextureExtensions[i], "link"))
		{
			// handle link format
			for (int c = strlen (workingname) - 1; c; c--)
			{
				if (workingname[c] == '/' || workingname[c] == '\\')
				{
					fscanf (f, "%s", &workingname[c + 1]);
					break;
				}
			}

			// done with the file
			fclose (f);

			// skip the rest; we check link first so that it will fall through to the other standard types
			continue;
		}
		else if (!strcmp (TextureExtensions[i], "pcx"))
		{
			// D3DX can't handle PCX formats so we have our own loader
			if (D3D_LoadPCX (f, tex, flags, filelen)) return true;

			// we may want to add other formats after PCX...
			fclose (f);
			continue;
		}

		// allocate a buffer to hold it
		byte *filebuf = (byte *) Heap_QMalloc (filelen);

		// read it all in
		fread (filebuf, filelen, 1, f);

		// done with the file
		fclose (f);

		// attempt to load it (this will generate miplevels for us too)
		HRESULT hr = D3D_CreateExternalTexture (tex, filelen, filebuf, flags);

		// ensure that the buffer is released before checking for success, as otherwise we'll leak it
		Heap_QFree (filebuf);

		// failed to load
		if (FAILED (hr)) continue;

		// load succeeded
		return true;
	}

	// didn't find it
	return false;
}


void D3D_LoadResourceTexture (LPDIRECT3DTEXTURE9 *tex, int ResourceID, int flags)
{
	HRESULT hr = D3DXCreateTextureFromResourceEx
	(
		d3d_Device,
		NULL,
		MAKEINTRESOURCE (ResourceID),
		D3DX_DEFAULT,
		D3DX_DEFAULT,
		(flags & IMAGE_MIPMAP) ? D3DX_DEFAULT : 1,
		0,
		D3DFMT_UNKNOWN,
		D3DPOOL_MANAGED,
		D3DX_FILTER_TRIANGLE | D3DX_FILTER_DITHER,
		D3DX_FILTER_BOX,
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

	if (image->flags & IMAGE_LIQUID)
	{
		// ensure liquid textures are square
		if (scaled.width > scaled.height) scaled.height = scaled.width;
		if (scaled.height > scaled.width) scaled.width = scaled.height;
	}

	// create the texture at the scaled size
	HRESULT hr = d3d_Device->CreateTexture
	(
		scaled.width,
		scaled.height,
		(image->flags & IMAGE_MIPMAP) ? 0 : 1,
		0,
		(image->flags & IMAGE_ALPHA) ? D3DFMT_A8R8G8B8 : D3DFMT_X8R8G8B8,
		D3DPOOL_MANAGED,
		tex,
		NULL
	);

	if (image->flags & IMAGE_MIPMAP)
	{
		int xxxx = 32;
	}

	if (FAILED (hr))
	{
		Sys_Error ("D3D_LoadTexture: d3d_Device->CreateTexture failed");
		return;
	}

	// the rectangle to lock
	D3DLOCKED_RECT LockRect;

	// lock the texture rectangle
	(*tex)->LockRect (0, &LockRect, NULL, 0);

	// fill it in - how we do it depends on the scaling
	if (scaled.width == image->width && scaled.height == image->height)
	{
		// no scaling
		for (int i = 0; i < (scaled.width * scaled.height); i++)
		{
			unsigned int p;

			// retrieve the correct texel - this will either be direct or a palette lookup
			if (image->flags & IMAGE_32BIT)
				p = ((unsigned *) image->data)[i];
			else if (image->palette)
				p = image->palette[image->data[i]];
			else Sys_Error ("D3D_LoadTexture: !(flags & IMAGE_32BIT) without palette set");

			// store it back
			((unsigned *) LockRect.pBits)[i] = p;
		}
	}
	else
	{
		// save out lockbits in scaled data pointer
		scaled.data = (byte *) LockRect.pBits;

		// resample data into the texture
		D3D_ResampleTexture
		(
			image,
			&scaled
		);
	}

	// unlock it
	(*tex)->UnlockRect (0);

	// good old box filter - triangle is way too slow, linear doesn't work well for mipmapping
	if (image->flags & IMAGE_MIPMAP) D3DXFilterTexture (*tex, NULL, 0, D3DX_FILTER_BOX);

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


void D3D_MakeLumaTexture (LPDIRECT3DTEXTURE9 tex)
{
	D3DSURFACE_DESC Level0Desc;
	D3DLOCKED_RECT Level0Rect;

	tex->GetLevelDesc (0, &Level0Desc);
	tex->LockRect (0, &Level0Rect, NULL, 0);

	for (int i = 0; i < Level0Desc.Width * Level0Desc.Height; i++)
	{
		byte *rgba = (byte *) (&((unsigned int *) Level0Rect.pBits)[i]);

		// external texture packs come with lumas intended to be used in an additive blend over (texture * lightmap).
		// restoring the correct (lightmap + luma) * texture means that we have to increase the intensity of their
		// components.  we also do this for native textures as it makes no difference to them.
		rgba[0] = BYTE_CLAMP ((((int) rgba[0] + (int) rgba[1] + (int) rgba[2]) * 2));
		rgba[1] = rgba[2] = rgba[3] = rgba[0];
	}

	tex->UnlockRect (0);

	// re-filter the mipmaps
	D3DXFilterTexture (tex, NULL, 0, D3DX_FILTER_BOX);
}


void D3D_TranslateAlphaTexture (int r, int g, int b, LPDIRECT3DTEXTURE9 tex)
{
	D3DSURFACE_DESC Level0Desc;
	D3DLOCKED_RECT Level0Rect;

	tex->GetLevelDesc (0, &Level0Desc);
	tex->LockRect (0, &Level0Rect, NULL, 0);

	extern cvar_t r_sRGBgamma;

	for (int i = 0; i < Level0Desc.Width * Level0Desc.Height; i++)
	{
		byte *rgba = (byte *) (&((unsigned int *) Level0Rect.pBits)[i]);

		// direct3d doesn't specify sRGB transforms for the alpha channel, but it turns out we need it anyway
		// we can't write directly into rgba[3] as we might want to restore it if switching between colour spaces,
		// so instead we hack it in a "good enough" fashion....
		// strict correctness says we should divide by 255, but that makes it too dark, so instead we divide by 128
		rgba[0] = BYTE_CLAMP (D3D_TransformColourSpaceByte (BYTE_CLAMP (b)) * D3D_TransformColourSpaceByte (rgba[3]) / 128);
		rgba[1] = BYTE_CLAMP (D3D_TransformColourSpaceByte (BYTE_CLAMP (g)) * D3D_TransformColourSpaceByte (rgba[3]) / 128);
		rgba[2] = BYTE_CLAMP (D3D_TransformColourSpaceByte (BYTE_CLAMP (r)) * D3D_TransformColourSpaceByte (rgba[3]) / 128);
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
	if ((image->flags & IMAGE_LUMA) && !(image->flags & IMAGE_32BIT))
	{
		// lumas only come from native data
		bool hasluma = false;

		for (int i = 0; i < image->width * image->height; i++)
		{
			if (image->data[i] > 223)
				hasluma = true;
			else image->data[i] = 0;
		}

		if (!hasluma) return NULL;

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
		// data but different usage, so here we check for it and accomodate it
		if (image->flags != t->TexImage.flags) continue;

		// compare the hash and reuse if it matches
		if (!memcmp (image->hash, t->TexImage.hash, 16))
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
		// create a new one
		tex = (d3d_texture_t *) Heap_QMalloc (sizeof (d3d_texture_t));

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

	// detect water textures
	if (image->identifier[0] == '*')
	{
		image->flags |= IMAGE_LIQUID;
	}

	// check for textures that have the same name but different data, and amend the name by the QRP standard
	if (!memcmp (image->hash, plat_top1_cable, 16))
		strcat (image->identifier, "_cable");
	else if (!memcmp (image->hash, plat_top1_bolt, 16))
		strcat (image->identifier, "_bolt");
	else if (!memcmp (image->hash, metal5_2_arc, 16))
		strcat (image->identifier, "_arc");
	else if (!memcmp (image->hash, metal5_2_x, 16))
		strcat (image->identifier, "_x");
	else if (!memcmp (image->hash, metal5_4_arc, 16))
		strcat (image->identifier, "_arc");
	else if (!memcmp (image->hash, metal5_4_double, 16))
		strcat (image->identifier, "_double");
	else if (!memcmp (image->hash, metal5_8_back, 16))
		strcat (image->identifier, "_back");
	else if (!memcmp (image->hash, metal5_8_rune, 16))
		strcat (image->identifier, "_rune");
	else if (!memcmp (image->hash, sky_blue_solid, 16))
		strcpy (image->identifier, "sky4_solid");
	else if (!memcmp (image->hash, sky_blue_alpha, 16))
		strcpy (image->identifier, "sky4_alpha");
	else if (!memcmp (image->hash, sky_purp_solid, 16))
		strcpy (image->identifier, "sky1_solid");
	else if (!memcmp (image->hash, sky_purp_alpha, 16))
		strcpy (image->identifier, "sky1_alpha");

	// hack the colour for certain models
	// fix white line at base of shotgun shells box
	if (!memcmp (image->hash, ShotgunShells, 16))
		memcpy (image->data, image->data + 32 * 31, 32);

	COM_CheckContentDirectory (&gl_texturedir, false);

	if (!D3D_LoadExternalTexture (&tex->d3d_Texture, va ("%s/%s", gl_texturedir.string, image->identifier), image->flags))
	{
		// upload through direct 3d
		D3D_LoadTexture (&tex->d3d_Texture, image);
	}

	// make the luma representation of the texture
	if (image->flags & IMAGE_LUMA) D3D_MakeLumaTexture (tex->d3d_Texture);

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


LPDIRECT3DTEXTURE9 D3D_LoadTexture (miptex_t *mt, int flags)
{
	image_t image;

	image.data = (byte *) (mt + 1);
	image.flags = flags;
	image.height = mt->height;
	image.width = mt->width;
	image.palette = d_8to24table;

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

void D3D_ReleaseTextures (void)
{
	// other textures we need to release that we don't define globally
	extern LPDIRECT3DTEXTURE9 d3d_MapshotTexture;
	extern LPDIRECT3DTEXTURE9 R_PaletteTexture;
	extern LPDIRECT3DTEXTURE9 r_blacktexture;
	extern LPDIRECT3DTEXTURE9 skyboxtextures[];

	// release cached textures
	for (d3d_texture_t *tex = d3d_Textures; tex; tex = tex->next)
	{
		SAFE_RELEASE (tex->d3d_Texture);

		// set the hash value to ensure that it will never match
		// (needed because game changing goes through this path too)
		memcpy (tex->TexImage.hash, no_match_hash, 16);
	}

	// release player textures
	for (int i = 0; i < 16; i++)
		SAFE_RELEASE (playertextures[i]);

	// release lightmaps too
	D3D_ReleaseLightmaps ();
	D3D_KillUnderwaterTexture ();

	// resource textures
	R_ReleaseResourceTextures ();

	// other textures
	SAFE_RELEASE (char_texture);
	SAFE_RELEASE (solidskytexture);
	SAFE_RELEASE (alphaskytexture);
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


void D3D_FlushTextures (void)
{
	int numflush = 0;

	for (d3d_texture_t *tex = d3d_Textures; tex; tex = tex->next)
	{
		// all textures just loaded in the cache will have lastusage set to 0
		// incremenent lastusage for types we want to flush.
		// alias models are no longer cached between maps.
		if (tex->TexImage.flags & IMAGE_BSP) tex->LastUsage++;
		if (tex->TexImage.flags & IMAGE_ALIAS) tex->LastUsage++;
		if (tex->TexImage.flags & IMAGE_SPRITE) tex->LastUsage++;

		// always preserve these types irrespective
		if (tex->TexImage.flags & IMAGE_PRESERVE) tex->LastUsage = 0;

		if (tex->LastUsage > 3 && tex->d3d_Texture)
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
	}

	Con_DPrintf ("Flushed %i textures\n", numflush);
}


