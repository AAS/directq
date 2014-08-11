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
extern LPDIRECT3DTEXTURE9 char_textures[];
extern LPDIRECT3DTEXTURE9 solidskytexture;
extern LPDIRECT3DTEXTURE9 alphaskytexture;
LPDIRECT3DTEXTURE9 yahtexture;

// palette hackery
palettedef_t d3d_QuakePalette;


void D3D_MakeQuakePalettes (byte *palette)
{
	int dark = 21024;
	int darkindex = 0;

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

		PALETTEENTRY *keepcolor = &d3d_QuakePalette.noluma[i];
		PALETTEENTRY *discardcolor = &d3d_QuakePalette.luma[i];

		if (vid.fullbright[i])
		{
			keepcolor = &d3d_QuakePalette.luma[i];
			discardcolor = &d3d_QuakePalette.noluma[i];
		}

		keepcolor->peRed = rgb[0];
		keepcolor->peGreen = rgb[1];
		keepcolor->peBlue = rgb[2];
		keepcolor->peFlags = alpha;

		discardcolor->peRed = discardcolor->peGreen = discardcolor->peBlue = 0;
		discardcolor->peFlags = alpha;

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


D3DFORMAT D3D_GetTextureFormat (int flags)
{
	if (flags & IMAGE_ALPHA)
	{
		if ((flags & IMAGE_LUMA) || (flags & IMAGE_NOCOMPRESS))
			return D3DFMT_A8R8G8B8;
		else if ((flags & IMAGE_MIPMAP) && (d3d_GlobalCaps.supportDXT5))
			return D3DFMT_DXT5;
		else if ((flags & IMAGE_MIPMAP) && (d3d_GlobalCaps.supportDXT3))
			return D3DFMT_DXT3;
		else return D3DFMT_A8R8G8B8;
	}

	if ((flags & IMAGE_LUMA) || (flags & IMAGE_NOCOMPRESS))
		return d3d_GlobalCaps.supportXRGB ? D3DFMT_X8R8G8B8 : D3DFMT_A8R8G8B8;
	else if ((flags & IMAGE_MIPMAP) && (d3d_GlobalCaps.supportDXT1))
		return D3DFMT_DXT1;
	else return d3d_GlobalCaps.supportXRGB ? D3DFMT_X8R8G8B8 : D3DFMT_A8R8G8B8;
}

bool d3d_ImagePadded = false;

void D3D_UploadTexture (LPDIRECT3DTEXTURE9 *texture, void *data, int width, int height, int flags)
{
	if ((flags & IMAGE_SCRAP) && !(flags & IMAGE_32BIT))// && width < 64 && height < 64)
	{
		// load little ones into the scrap
		texture[0] = NULL;
		return;
	}

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

	// don't compress if not a multiple of 4 (marcher gets this)
	if (scaled.width & 3) flags |= IMAGE_NOCOMPRESS;
	if (scaled.height & 3) flags |= IMAGE_NOCOMPRESS;

	// create the texture at the scaled size
	hr = d3d_Device->CreateTexture
	(
		scaled.width,
		scaled.height,
		(flags & IMAGE_MIPMAP) ? 0 : 1,
		0,
		D3D_GetTextureFormat (flags),
		(flags & IMAGE_SYSMEM) ? D3DPOOL_SYSTEMMEM : D3DPOOL_MANAGED,
		texture,
		NULL
	);

	if (FAILED (hr))
	{
		UINT blah = d3d_Device->GetAvailableTextureMem ();
		Sys_Error ("D3D_UploadTexture: d3d_Device->CreateTexture failed with %i vram", (blah / 1024) / 1024);
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

	// default to the standard palette
	PALETTEENTRY *activepal = d3d_QuakePalette.standard;

	// switch the palette
//	if (flags & IMAGE_HALFLIFE) activepal = (PALETTEENTRY *) palette;

	// sky has flags & IMAGE_32BIT so it doesn't get paletteized
	// (not always - solid sky goes up as 8 bits.  it doesn't have IMAGE_BSP however so all is good
	if ((flags & IMAGE_BSP) || (flags & IMAGE_ALIAS))
	{
		if (flags & IMAGE_LIQUID)
			activepal = d3d_QuakePalette.standard;
		else if (flags & IMAGE_NOLUMA)
			activepal = d3d_QuakePalette.standard;
		else if (flags & IMAGE_LUMA)
			activepal = d3d_QuakePalette.luma;
		else activepal = d3d_QuakePalette.noluma;
	}
	else activepal = d3d_QuakePalette.standard;

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

		// compare the hash and reuse if it matches
		if (COM_CheckHash (texhash, d3dtex->texture->hash))
		{
			// check for luma match as the incoming luma will get the same hash as it's base
			if ((flags & (IMAGE_LUMA | IMAGE_NOLUMA)) == (d3dtex->texture->flags & (IMAGE_LUMA | IMAGE_NOLUMA)))
			{
				// set last usage to 0
				d3dtex->texture->LastUsage = 0;

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

		// don't apply compression to native Quake textures
		tex->flags |= IMAGE_NOCOMPRESS;

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
void R_UnloadSkybox (void);
void Scrap_Destroy (void);

// fixme - this needs to fully go through the correct shutdown paths so that aux data is also cleared
void D3D_ReleaseTextures (void)
{
	Scrap_Destroy ();

	D3D_ShutdownHLSL ();

	R_UnloadSkybox ();

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
		Q_MemCpy (tex->texture->hash, no_match_hash, 16);
	}

	// release player textures
	for (int i = 0; i < 256; i++)
		SAFE_RELEASE (d3d_PlayerSkins[i].d3d_Texture);

	// skyboxes
	for (int i = 0; i < 6; i++)
		SAFE_RELEASE (skyboxtextures[i]);

	SAFE_RELEASE (skyboxcubemap);

	// standard lightmaps
	SAFE_DELETE (d3d_Lightmaps);

	D3D_Kill3DSceneTexture ();

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
	SAFE_RELEASE (r_notexture);
	SAFE_RELEASE (yahtexture);
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
int d3d_ExternalTextureTable[257] = {-1};

// hmmm - can be used for both bsearch and qsort
// clever boy, bill!
int D3D_ExternalTextureCompareFunc (const void *a, const void *b)
{
	d3d_externaltexture_t *t1 = *(d3d_externaltexture_t **) a;
	d3d_externaltexture_t *t2 = *(d3d_externaltexture_t **) b;

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
	Q_MemCpy (d3d_ExternalTextures, scratchbuf, d3d_NumExternalTextures * sizeof (d3d_externaltexture_t *));

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
	qsort
	(
		d3d_ExternalTextures,
		d3d_NumExternalTextures,
		sizeof (d3d_externaltexture_t *),
		D3D_ExternalTextureCompareFunc
	);

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

