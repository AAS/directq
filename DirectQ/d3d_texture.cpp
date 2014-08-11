

#include "quakedef.h"
#include "d3d_quake.h"

// used for generating md5 hashes
#include <wincrypt.h>

LPDIRECT3DTEXTURE9 d3d_CurrentTexture[8] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
d3d_texture_t *d3d_Textures = NULL;

void D3D_ReleaseLightmaps (void);

// other textures we use
extern LPDIRECT3DTEXTURE9 playertextures[];
extern LPDIRECT3DTEXTURE9 char_texture;
extern LPDIRECT3DTEXTURE9 solidskytexture;
extern LPDIRECT3DTEXTURE9 alphaskytexture;


void D3D_BindTexture (int stage, LPDIRECT3DTEXTURE9 tex)
{
	// no change
	if (tex == d3d_CurrentTexture[stage]) return;

	// set the texture
	d3d_Device->SetTexture (stage, tex);

	// store back to current
	d3d_CurrentTexture[stage] = tex;
}


void D3D_BindTexture (LPDIRECT3DTEXTURE9 tex)
{
	D3D_BindTexture (D3D_TEXTURE0, tex);
}


void D3D_HashTexture (image_t *image)
{
	// generate an MD5 hash of an image's data
	HCRYPTPROV hCryptProv;
	HCRYPTHASH hHash;

	// acquire the cryptographic context
	if (CryptAcquireContext (&hCryptProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT | CRYPT_MACHINE_KEYSET))
	{
		// create a hashing algorithm
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
			}

			CryptDestroyHash (hHash); 
		}

	    CryptReleaseContext (hCryptProv, 0);
	}
}


void D3D_ResampleTexture (image_t *src, image_t *dst)
{
	// take an unsigned pointer to the dest data that we'll actually fill
	unsigned int *dstdata = (unsigned int *) dst->data;

	// easier access to src data for 32 bit resampling
	unsigned int *srcdata = (unsigned int *) src->data;

	// nearest neighbour for now
	for (int y = 0, dstpos = 0; y < dst->height; y++)
	{
		int srcbase = (y * src->height / dst->height) * src->width;

		for (int x = 0; x < dst->width; x++, dstpos++)
		{
			int srcpos = srcbase + (x * src->width / dst->width);

			if (src->flags & IMAGE_32BIT)
				dstdata[dstpos] = srcdata[srcpos];
			else if (src->palette)
				dstdata[dstpos] = src->palette[src->data[srcpos]];
			else Sys_Error ("D3D_ResampleTexture: !(flags & IMAGE_32BIT) without palette set");
		}
	}
}


void D3D_LoadTexture (LPDIRECT3DTEXTURE9 *tex, image_t *image)
{
	// dedicated servers go through here so don't crash them
	if (isDedicated) return;

	D3DLOCKED_RECT LockRect;

	image_t scaled;

	// check scaling here first
	for (scaled.width = 1; scaled.width < image->width; scaled.width *= 2);
	for (scaled.height = 1; scaled.height < image->height; scaled.height *= 2);

	// clamp to max texture size
	if (scaled.width > d3d_DeviceCaps.MaxTextureWidth) scaled.width = d3d_DeviceCaps.MaxTextureWidth;
	if (scaled.height > d3d_DeviceCaps.MaxTextureHeight) scaled.height = d3d_DeviceCaps.MaxTextureHeight;

	// create the texture at the scaled size
	HRESULT hr = d3d_Device->CreateTexture
	(
		scaled.width,
		scaled.height,
		(image->flags & IMAGE_MIPMAP) ? 0 : 1,
		(image->flags & IMAGE_MIPMAP) ? D3DUSAGE_AUTOGENMIPMAP : 0,
		(image->flags & IMAGE_ALPHA) ? D3DFMT_A8R8G8B8 : D3DFMT_X8R8G8B8,
		D3DPOOL_MANAGED,
		tex,
		NULL
	);

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
		D3D_ResampleTexture (image, &scaled);
	}

	// unlock it
	(*tex)->UnlockRect (0);

	if (image->flags & IMAGE_MIPMAP)
	{
		// generate the mipmap sublevels
		(*tex)->SetAutoGenFilterType (D3DTEXF_LINEAR);
		(*tex)->GenerateMipSubLevels ();
	}

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


LPDIRECT3DTEXTURE9 D3D_LoadTexture (image_t *image)
{
	// take a hash of the image data
	D3D_HashTexture (image);

	// look for a match
	for (d3d_texture_t *t = d3d_Textures; t; t = t->next)
	{
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

	// create a new one
	d3d_texture_t *tex = (d3d_texture_t *) malloc (sizeof (d3d_texture_t));

	// link it in
	tex->next = d3d_Textures;
	d3d_Textures = tex;

	// fill in the struct
	tex->LastUsage = 0;
	tex->d3d_Texture = NULL;

	// copy the image
	memcpy (&tex->TexImage, image, sizeof (image_t));

	// upload through direct 3d
	D3D_LoadTexture (&tex->d3d_Texture, image);

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


LPDIRECT3DTEXTURE9 D3D_LoadTexture (int width, int height, byte *data, int flags)
{
	image_t image;

	image.data = data;
	image.flags = flags;
	image.height = height;
	image.width = width;
	image.palette = NULL;

	return D3D_LoadTexture (&image);
}


void D3D_ReleaseTextures (bool fullrelease = D3D_RELEASE_UNUSED)
{
	for (d3d_texture_t *tex = d3d_Textures; tex; tex = tex->next)
	{
		SAFE_RELEASE (tex->d3d_Texture);
	}

	// release player textures
	for (int i = 0; i < 16; i++)
		SAFE_RELEASE (playertextures[i]);

	// release lightmaps too
	D3D_ReleaseLightmaps ();

	// other textures
	SAFE_RELEASE (char_texture);
	SAFE_RELEASE (solidskytexture);
	SAFE_RELEASE (alphaskytexture);
}


