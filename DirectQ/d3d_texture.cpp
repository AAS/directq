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
#include "d3d_model.h"
#include "d3d_quake.h"
#include "resource.h"


// leave this at 1 cos texture caching will resolve loading time issues
cvar_t gl_compressedtextures ("gl_texture_compression", 1, CVAR_ARCHIVE);
cvar_t gl_maxtextureretention ("gl_maxtextureretention", 3, CVAR_ARCHIVE);
cvar_t r_lumatextures ("r_lumaintensity", "1", CVAR_ARCHIVE);
cvar_alias_t gl_fullbrights ("gl_fullbrights", &r_lumatextures);
extern cvar_t r_overbright;

LPDIRECT3DTEXTURE9 d3d_CurrentTexture[8] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};

typedef struct d3d_texture_s
{
	image_t *texture;
	struct d3d_texture_s *next;
} d3d_texture_t;

d3d_texture_t *d3d_TextureList = NULL;
int d3d_NumTextures = 0;

void D3D_Kill3DSceneTexture (void);

// other textures we use
extern LPDIRECT3DTEXTURE9 char_texture;
extern LPDIRECT3DTEXTURE9 solidskytexture;
extern LPDIRECT3DTEXTURE9 alphaskytexture;
extern LPDIRECT3DTEXTURE9 simpleskytexture;
LPDIRECT3DTEXTURE9 yahtexture;

// for palette hackery
PALETTEENTRY *texturepal = NULL;
extern PALETTEENTRY lumapal[];

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
	if (!(flags & IMAGE_MIPMAP)) flags |= IMAGE_NOCOMPRESS;

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

bool d3d_ImagePadded = false;

void D3D_UploadTexture (LPDIRECT3DTEXTURE9 *texture, void *data, int width, int height, int flags)
{
	// scaled image
	image_t scaled;
	byte *padbytes = NULL;

	// explicit NULL
	texture[0] = NULL;

	// check scaling here first
	// removed np2 support because it can scale textures weirdly on some (all?) drivers
	scaled.width = D3D_PowerOf2Size (width);
	scaled.height = D3D_PowerOf2Size (height);

	// clamp to max texture size
	if (scaled.width > d3d_DeviceCaps.MaxTextureWidth) scaled.width = d3d_DeviceCaps.MaxTextureWidth;
	if (scaled.height > d3d_DeviceCaps.MaxTextureHeight) scaled.height = d3d_DeviceCaps.MaxTextureHeight;

	// d3d specifies that compressed formats should be a multiple of 4
	// would you believe marcher has a 1x1 texture in it?
	if ((scaled.width & 3) || (scaled.height & 3)) flags |= IMAGE_NOCOMPRESS;

	// create the texture at the scaled size
	hr = d3d_Device->CreateTexture
	(
		scaled.width,
		scaled.height,
		(flags & IMAGE_MIPMAP) ? 0 : 1,
		0,
		D3D_GetTextureFormatForFlags (flags),
		D3DPOOL_MANAGED,
		texture,
		NULL
	);

	if (FAILED (hr))
	{
		Sys_Error ("D3D_UploadTexture: d3d_Device->CreateTexture failed");
		return;
	}

	LPDIRECT3DSURFACE9 texsurf = NULL;
	texture[0]->GetSurfaceLevel (0, &texsurf);

	RECT SrcRect;
	SrcRect.top = 0;
	SrcRect.left = 0;
	SrcRect.bottom = height;
	SrcRect.right = width;

	DWORD FilterFlags = 0;
	d3d_ImagePadded = false;

	if (scaled.width == width && scaled.height == height)
	{
		// no need
		FilterFlags |= D3DX_FILTER_NONE;
	}
	else if ((flags & IMAGE_PADDABLE) && !(flags & IMAGE_32BIT))
	{
		// create a buffer for padding
		padbytes = (byte *) Zone_Alloc (scaled.width * scaled.height);
		byte *dst = padbytes;
		byte *src = (byte *) data;

		for (int y = 0; y < height; y++, dst += scaled.width, src += width)
			for (int x = 0; x < width; x++) dst[x] = src[x];

		d3d_ImagePadded = true;
	}
	else if ((flags & IMAGE_PADDABLE) && (flags & IMAGE_32BIT))
	{
		// create a buffer for padding
		padbytes = (byte *) Zone_Alloc (scaled.width * scaled.height * 4);
		unsigned int *dst = (unsigned int *) padbytes;
		unsigned int *src = (unsigned int *) data;

		for (int y = 0; y < height; y++, dst += scaled.width, src += width)
			for (int x = 0; x < width; x++) dst[x] = src[x];

		d3d_ImagePadded = true;
	}
	else
	{
		FilterFlags |= D3DX_FILTER_LINEAR; //D3DX_FILTER_TRIANGLE | D3DX_FILTER_DITHER;

		// it's generally assumed that mipmapped textures should also wrap
		if (flags & IMAGE_MIPMAP) FilterFlags |= (D3DX_FILTER_MIRROR_U | D3DX_FILTER_MIRROR_V);
	}

	if (d3d_ImagePadded)
	{
		// no filter needed now
		FilterFlags |= D3DX_FILTER_NONE;

		// the source data has changed so change these too
		width = scaled.width;
		height = scaled.height;
	}

	if (!texturepal)
	{
		texturepal = (PALETTEENTRY *) Zone_Alloc (256 * sizeof (PALETTEENTRY));

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
//	if (flags & IMAGE_HALFLIFE) activepal = (PALETTEENTRY *) palette;
	if (flags & IMAGE_LUMA) activepal = lumapal;

	hr = QD3DXLoadSurfaceFromMemory
	(
		texsurf,
		NULL,
		NULL,
		padbytes ? padbytes : data,
		(flags & IMAGE_32BIT) ? D3DFMT_A8R8G8B8 : D3DFMT_P8,
		(flags & IMAGE_32BIT) ? width * 4 : width,
		(flags & IMAGE_32BIT) ? NULL : activepal,
		&SrcRect,
		FilterFlags,
		0
	);

	if (FAILED (hr))
	{
		Sys_Error ("D3D_UploadTexture: QD3DXLoadSurfaceFromMemory failed");
		return;
	}

	texsurf->Release ();

	// if we padded the image we need to hand back the memory used now
	if (padbytes) Zone_Free (padbytes);

	// good old box filter - triangle is way too slow, linear doesn't work well for mipmapping
	if (flags & IMAGE_MIPMAP) QD3DXFilterTexture (texture[0], NULL, 0, D3DX_FILTER_BOX | D3DX_FILTER_SRGB);

	// tell Direct 3D that we're going to be needing to use this managed resource shortly
	texture[0]->PreLoad ();
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
	QD3DXFilterTexture (tex, NULL, 0, D3DX_FILTER_BOX | D3DX_FILTER_SRGB);
	tex->AddDirtyRect (NULL);

	return isrealluma;
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

image_t *D3D_LoadTexture (char *identifier, int width, int height, byte *data, int flags)
{
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

		// it's not beyond the bounds of possibility that we might have 2 textures with the same
		// data but different usage, so here we check for it and accomodate it (this will happen with lumas
		// where the hash will match, so it remains a valid check)
		if (flags != d3dtex->texture->flags) continue;

		// compare the hash and reuse if it matches
		if (COM_CheckHash (texhash, d3dtex->texture->hash))
		{
			// set last usage to 0
			d3dtex->texture->LastUsage = 0;

			// return it
			return d3dtex->texture;
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

	Q_MemCpy (tex->hash, texhash, 16);
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
		Q_MemCpy (tex->data, tex->data + 32 * 31, 32);

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
			Q_MemCpy (tex->hash, no_match_hash, 16);
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
	}

	// make the luma representation of the texture
	if (flags & IMAGE_LUMA)
	{
		// if it's not really a luma (should never happen...) clear it and return NULL
		if (!D3D_MakeLumaTexture (tex->d3d_Texture))
		{
			tex->LastUsage = 666;
			SAFE_RELEASE (tex->d3d_Texture);
			Q_MemCpy (tex->hash, no_match_hash, 16);
			return NULL;
		}
	}

	// notify that we padded the image
	if (d3d_ImagePadded) tex->flags |= IMAGE_PADDED;

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
void D3D_ShutdownHLSL (void);

void D3D_ReleaseTextures (void)
{
	D3D_ShutdownHLSL ();

	// other textures we need to release that we don't define globally
	extern LPDIRECT3DTEXTURE9 d3d_MapshotTexture;
	extern LPDIRECT3DTEXTURE9 R_PaletteTexture;
	extern LPDIRECT3DTEXTURE9 r_blacktexture;
	extern LPDIRECT3DTEXTURE9 skyboxtextures[];
	extern image_t d3d_PlayerSkins[];

	// release cached textures
	for (d3d_texture_t *tex = d3d_TextureList; tex; tex = tex->next)
	{
		SAFE_RELEASE (tex->texture->d3d_Texture);

		// set the hash value to ensure that it will never match
		// (needed because game changing goes through this path too)
		Q_MemCpy (tex->texture->hash, no_match_hash, 16);
	}

	// release player textures
	for (int i = 0; i < 256; i++)
		SAFE_RELEASE (d3d_PlayerSkins[i].d3d_Texture);

	// skyboxes
	for (int i = 0; i < 6; i++)
		SAFE_RELEASE (skyboxtextures[i]);

	// VBOs
	// VBOs aren't textures but - hey! - what's mutual incompatibility when we're such fantastic friends?
	D3D_VBOReleaseBuffers ();

	// standard lightmaps
	SAFE_DELETE (d3d_Lightmaps);

	D3D_Kill3DSceneTexture ();

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
			Q_MemCpy (tex->texture->hash, no_match_hash, 16);

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
	LPDIRECT3DTEXTURE9 tex;

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
	QD3DXFilterTexture (tex, NULL, 0, D3DX_FILTER_BOX | D3DX_FILTER_SRGB);
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

		for (d3d_texture_t *tex = d3d_TextureList; tex; tex = tex->next)
		{
			if (!tex->texture->d3d_Texture) continue;
			if (!(tex->texture->flags & IMAGE_LUMA)) continue;

			D3D_RescaleIndividualLuma (tex->texture->d3d_Texture);
		}
	}
}


