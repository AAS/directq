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
// r_misc.c

#include "quakedef.h"
#include "d3d_model.h"
#include "d3d_quake.h"
#include "resource.h"

D3DFORMAT D3D_GetTextureFormat (int flags);

byte *gammaramp = NULL;

HRESULT D3D_CreateExternalTexture (LPDIRECT3DTEXTURE9 *tex, int len, byte *data, int flags)
{
#ifndef GLQUAKE_GAMMA_SCALE
	// wrap this monster so that we can more easily modify it if required
	hr = QD3DXCreateTextureFromFileInMemoryEx
	(
		d3d_Device,
		data,
		len,
		D3DX_DEFAULT,
		D3DX_DEFAULT,
		(flags & IMAGE_MIPMAP) ? D3DX_DEFAULT : 1,
		0,
		D3D_GetTextureFormat (flags | IMAGE_ALPHA | IMAGE_32BIT),
		D3DPOOL_MANAGED,
		D3DX_FILTER_LINEAR,
		D3DX_FILTER_BOX | D3DX_FILTER_SRGB,
		0,
		NULL,
		NULL,
		tex
	);

	return hr;
#else
	if (!gammaramp)
	{
		gammaramp = (byte *) MainZone->Alloc (256);

		// same ramp as gl_vidnt
		for (int i = 0; i < 256; i++)
		{
			// this calculation is IMPORTANT for retaining the full colour range of a LOT of
			// Quake 1 textures which gets LOST otherwise.
			float f = pow ((float) (((float) i + 1) / 256.0), 0.7f);

			// note: + 0.5f is IMPORTANT here for retaining a LOT of the visual look of Quake
			int inf = (f * 255.0f + 0.5f);

			// store back
			gammaramp[i] = BYTE_CLAMP (inf);
		}
	}

	LPDIRECT3DTEXTURE9 load = NULL;
	LPDIRECT3DSURFACE9 surf = NULL;
	D3DSURFACE_DESC desc;
	D3DLOCKED_RECT lock;

	// we can't create a surface from a file so we send it through hoops...
	// first we create it in system memory; this will also handle scaling/etc
	hr = QD3DXCreateTextureFromFileInMemoryEx
	(
		d3d_Device,
		data,
		len,
		D3DX_DEFAULT,
		D3DX_DEFAULT,
		1,
		0,
		D3DFMT_A8R8G8B8,
		D3DPOOL_SYSTEMMEM,
		D3DX_FILTER_LINEAR,
		D3DX_FILTER_BOX,
		0,
		NULL,
		NULL,
		&load
	);

	if (FAILED (hr)) goto loadfail;

	// get the surface and it's description so that we can modify it correctly
	hr = load->GetSurfaceLevel (0, &surf);
	if (FAILED (hr)) goto loadfail;

	hr = load->GetLevelDesc (0, &desc);
	if (FAILED (hr)) goto loadfail;
	if (desc.Format != D3DFMT_A8R8G8B8) goto loadfail;

	hr = surf->LockRect (&lock, NULL, 0);
	if (FAILED (hr)) goto loadfail;

	byte *texels = (byte *) lock.pBits;

	// apply gamma scaling
	for (int x = 0; x < desc.Width; x++)
	{
		for (int y = 0; y < desc.Height; y++, texels += 4)
		{
			// the format is A8R8G8B8 but the layout is rgba.  or bgra.  who knows?  who cares?
			// a is at [3] and that's all that matters here...
			texels[0] = gammaramp[texels[0]];
			texels[1] = gammaramp[texels[1]];
			texels[2] = gammaramp[texels[2]];
		}
	}

	// ideally we would prefer to flip/mirror/etc cubemap textures here, but the routines need them to be powers
	// of 2 and square, which they are not guaranteed to be (never trust data we don't have control over - those wacky modders!!!)
	// load it as a texture
	D3D_UploadTexture (tex, lock.pBits, desc.Width, desc.Height, flags | IMAGE_32BIT | IMAGE_ALPHA | IMAGE_EXTERNAL);

	// done
	surf->UnlockRect ();

	SAFE_RELEASE (surf);
	SAFE_RELEASE (load);
	return D3D_OK;

loadfail:;
	// failed to load
	SAFE_RELEASE (surf);
	SAFE_RELEASE (load);
	tex[0] = NULL;
	return hr;
#endif
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

// d3d can't load PCX files so we expand a PCX into a fake TGA and let it fall through to the TGA loader
// we could alternatively mod our standard texture loader to take a palette... (would load faster too)
byte *D3D_LoadPCX (byte *f, int *loadsize)
{
	pcx_t	pcx;
	byte	*palette, *a, *b, *image_rgba, *fin, *pbuf, *enddata;
	int		x, y, x2, dataByte;
	int image_width;
	int image_height;

	if (loadsize[0] < sizeof (pcx) + 768)
	{
		MainZone->Free (f);
		return NULL;
	}

	fin = f;

	Q_MemCpy (&pcx, fin, sizeof (pcx));
	fin += sizeof (pcx);

	if (pcx.manufacturer != 0x0a || pcx.version != 5 || pcx.encoding != 1 || pcx.bits_per_pixel != 8 || pcx.xmax > 320 || pcx.ymax > 256)
	{
		MainZone->Free (f);
		return NULL;
	}

	image_width = pcx.xmax + 1;
	image_height = pcx.ymax + 1;

	palette = f + loadsize[0] - 768;

	image_rgba = (byte *) MainZone->Alloc (image_width * image_height * 4 + 18);

	if (!image_rgba)
	{
		MainZone->Free (f);
		return NULL;
	}

	pbuf = image_rgba + 18 + image_width * image_height * 3;
	enddata = palette;

	for (y = 0; y < image_height && fin < enddata; y++)
	{
		a = pbuf + y * image_width;

		for (x = 0; x < image_width && fin < enddata;)
		{
			dataByte = *fin++;

			if (dataByte >= 0xC0)
			{
				if (fin >= enddata) break;

				x2 = x + (dataByte & 0x3F);
				dataByte = *fin++;

				if (x2 > image_width) x2 = image_width;
				while (x < x2) a[x++] = dataByte;
			}
			else a[x++] = dataByte;
		}

		while (x < image_width)
			a[x++] = 0;
	}

	a = image_rgba + 18;
	b = pbuf;

	for (x = 0; x < image_width * image_height; x++)
	{
		y = *b++ * 3;

		// swap to bgra
		*a++ = palette[y + 2];
		*a++ = palette[y + 1];
		*a++ = palette[y];
		*a++ = 255;
	}

	// now fill in our fake tga header
	Q_MemSet (image_rgba, 0, 18);
	image_rgba[2] = 2;
	image_rgba[12] = image_width & 255;
	image_rgba[13] = image_width >> 8;
	image_rgba[14] = image_height & 255;
	image_rgba[15] = image_height >> 8;
	image_rgba[16] = 32;
	image_rgba[17] = 0x20;

	// need to modify the len also
	loadsize[0] = image_width * image_height * 4 + 18;

	MainZone->Free (f);
	return image_rgba;
}


typedef struct _TargaHeader
{
	unsigned char 	id_length, colormap_type, image_type;
	unsigned short	colormap_index, colormap_length;
	unsigned char	colormap_size;
	unsigned short	x_origin, y_origin, width, height;
	unsigned char	pixel_size, attributes;
} TargaHeader;

byte *D3D_LoadTGA (byte *f, int *loadsize)
{
	int columns, rows, row, column;
	byte *pixbuf, *image_rgba, *fin, *enddata;
	int image_width;
	int image_height;
	TargaHeader targa_header;

	if (loadsize[0] < 18 + 3)
	{
		MainZone->Free (f);
		return NULL;
	}

	Q_MemSet (&targa_header, 0, sizeof (TargaHeader));

	targa_header.id_length = f[0];
	targa_header.colormap_type = f[1];
	targa_header.image_type = f[2];

	targa_header.colormap_index = f[3] + f[4] * 256;
	targa_header.colormap_length = f[5] + f[6] * 256;
	targa_header.colormap_size = f[7];
	targa_header.x_origin = f[8] + f[9] * 256;
	targa_header.y_origin = f[10] + f[11] * 256;
	targa_header.width = f[12] + f[13] * 256;
	targa_header.height = f[14] + f[15] * 256;

	targa_header.pixel_size = f[16];
	targa_header.attributes = f[17];

	if (targa_header.image_type != 2 && targa_header.image_type != 10)
	{
		MainZone->Free (f);
		return NULL;
	}

	if (targa_header.colormap_type != 0	|| (targa_header.pixel_size != 32 && targa_header.pixel_size != 24))
	{
		MainZone->Free (f);
		return NULL;
	}

	enddata = f + loadsize[0];

	columns = targa_header.width;
	rows = targa_header.height;

	image_rgba = (byte *) MainZone->Alloc (columns * rows * 4 + 18);

	if (!image_rgba)
	{
		Con_Printf ("LoadTGA: not enough memory for %i by %i image\n", columns, rows);
		MainZone->Free (f);
		return NULL;
	}

	fin = f + 18;

	if (targa_header.id_length != 0)
		fin += targa_header.id_length;  // skip TARGA image comment

	bool upside_down = !(targa_header.attributes & 0x20); //johnfitz -- fix for upside-down targas
	int realrow;

	if (targa_header.image_type == 2)
	{
		// Uncompressed, RGB images
		for (row = rows - 1; row >= 0; row--)
		{
			realrow = upside_down ? row : rows - 1 - row;
			pixbuf = image_rgba + realrow * columns * 4 + 18;

			for (column = 0; column < columns; column++)
			{
				switch (targa_header.pixel_size)
				{
				case 24:
					if (fin + 3 > enddata)
						break;
					*pixbuf++ = fin[0];
					*pixbuf++ = fin[1];
					*pixbuf++ = fin[2];
					*pixbuf++ = 255;
					fin += 3;
					break;
				case 32:
					if (fin + 4 > enddata)
						break;
					*pixbuf++ = fin[0];
					*pixbuf++ = fin[1];
					*pixbuf++ = fin[2];
					*pixbuf++ = fin[3];
					fin += 4;
					break;
				}
			}
		}
	}
	else if (targa_header.image_type == 10)
	{
		// Runlength encoded RGB images
		unsigned char red = 0, green = 0, blue = 0, alphabyte = 0, packetHeader, packetSize, j;

		for (row = rows - 1; row >= 0; row--)
		{
			realrow = upside_down ? row : rows - 1 - row;
			pixbuf = image_rgba + realrow * columns * 4 + 18;

			for (column = 0; column < columns;)
			{
				if (fin >= enddata)
					goto outofdata;

				packetHeader = *fin++;
				packetSize = 1 + (packetHeader & 0x7f);

				if (packetHeader & 0x80)
				{
					// run-length packet
					switch (targa_header.pixel_size)
					{
					case 24:
						if (fin + 3 > enddata)
							goto outofdata;
						red = *fin++;
						green = *fin++;
						blue = *fin++;
						alphabyte = 255;
						break;
					case 32:
						if (fin + 4 > enddata)
							goto outofdata;
						red = *fin++;
						green = *fin++;
						blue = *fin++;
						alphabyte = *fin++;
						break;
					}

					for(j = 0; j < packetSize; j++)
					{
						*pixbuf++ = red;
						*pixbuf++ = green;
						*pixbuf++ = blue;
						*pixbuf++ = alphabyte;
						column++;

						if (column == columns)
						{
							// run spans across rows
							column = 0;
							if (row > 0)
								row--;
							else goto breakOut;

							realrow = upside_down ? row : rows - 1 - row;
							pixbuf = image_rgba + realrow * columns * 4 + 18;
						}
					}
				}
				else
				{
					// non run-length packet
					for(j = 0; j < packetSize; j++)
					{
						switch (targa_header.pixel_size)
						{
						case 24:
							if (fin + 3 > enddata)
								goto outofdata;
							*pixbuf++ = fin[0];
							*pixbuf++ = fin[1];
							*pixbuf++ = fin[2];
							*pixbuf++ = 255;
							fin += 3;
							break;
						case 32:
							if (fin + 4 > enddata)
								goto outofdata;
							*pixbuf++ = fin[0];
							*pixbuf++ = fin[1];
							*pixbuf++ = fin[2];
							*pixbuf++ = fin[3];
							fin += 4;
							break;
						}

						column++;

						if (column == columns)
						{
							// pixel packet run spans across rows
							column = 0;

							if (row > 0)
								row--;
							else goto breakOut;

							realrow = upside_down ? row : rows - 1 - row;
							pixbuf = image_rgba + realrow * columns * 4 + 18;
						}
					}
				}
			}
breakOut:;
		}
	}
outofdata:;

	image_width = columns;
	image_height = rows;

	// now fill in our fake tga header
	Q_MemSet (image_rgba, 0, 18);
	image_rgba[2] = 2;
	image_rgba[12] = image_width & 255;
	image_rgba[13] = image_width >> 8;
	image_rgba[14] = image_height & 255;
	image_rgba[15] = image_height >> 8;
	image_rgba[16] = 32;
	image_rgba[17] = 0x20;

	// need to modify the len also
	loadsize[0] = image_width * image_height * 4 + 18;

	MainZone->Free (f);
	return image_rgba;
}


char *D3D_FindExternalTexture (char *basename);

bool D3D_LoadExternalTexture (LPDIRECT3DTEXTURE9 *tex, char *filename, int flags)
{
	// allow disabling of check for external replacement (speed)
	if (flags & IMAGE_NOEXTERN) return false;

	char texname[256];

	// initial copy
	Q_strncpy (texname, filename, 255);

	if (!(flags & IMAGE_KEEPPATH))
	{
		// remove path
		for (int i = strlen (filename); i; i--)
		{
			if (filename[i] == '/' || filename[i] == '\\')
			{
				Q_strncpy (texname, &filename[i + 1], 255);
				break;
			}
		}
	}
	else
	{
		// sigh - more path seperator consistency here
		for (int i = 0;; i++)
		{
			if (!texname[i]) break;
			if (texname[i] == '/') texname[i] = '\\';
		}
	}

	int extpos = 0;

	// remove extension (unless it's an alias model which is in the format "%s_%i", loadmodel->name, i
	// in which case we just store it's position
	for (int i = strlen (texname); i; i--)
	{
		if (texname[i] == '/' || texname[i] == '\\') break;

		if (texname[i] == '.')
		{
			// store ext position
			extpos = i;

			// alias and sprite textures don't null term at the extension
			if (flags & IMAGE_ALIAS) break;
			if (flags & IMAGE_SPRITE) break;

			// other types do
			texname[i] = 0;
			break;
		}
	}

	// convert to lowercase
	strlwr (texname);

	// prevent infinite looping
	bool abort_retry = false;

	// this is used as a goto target for alias and sprite models
retry_nonstd:;

	// locate the texture
	char *extpath = D3D_FindExternalTexture (texname);

	if (!extpath)
	{
		if ((flags & IMAGE_ALIAS) || (flags & IMAGE_SPRITE))
		{
			// already retried
			if (abort_retry) return false;

			// mark that we're retrying
			abort_retry = true;

			// find the extension part
			char *mdlstring = NULL;

			if (flags & IMAGE_ALIAS) mdlstring = strstr (texname, ".mdl_");
			if (flags & IMAGE_SPRITE) mdlstring = strstr (texname, ".spr_");

			// didn't find it
			if (!mdlstring) return false;

			// jump past .ext to the _
			mdlstring += 4;

			// remove the ".ext" part
			for (int i = 0;; i++)
			{
				// replace
				texname[i + extpos] = mdlstring[i];

				// done after so that texname will null term properly
				if (!mdlstring[i]) break;
			}

			// try it again
			goto retry_nonstd;
		}

		return false;
	}

	// this is used as a goto target for .link files
ext_tex_load:;

	HANDLE fh = INVALID_HANDLE_VALUE;
	int filelen = 0;

	// only if it's a full path
	if (extpath[1] == ':' && extpath[2] == '\\')
	{
		// attempt to open it direct
		fh = CreateFile
		(
			extpath,
			GENERIC_READ,
			FILE_SHARE_READ,
			NULL,
			OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_NO_RECALL | FILE_FLAG_SEQUENTIAL_SCAN,
			NULL
		);

		filelen = GetFileSize (fh, NULL);
	}
	else
	{
		// attempt it through the filesystem
		filelen = COM_FOpenFile (extpath, &fh);
	}

	// didn't get it
	if (fh == INVALID_HANDLE_VALUE) return false;

	// retrieve the extension we're using
	char *texext = NULL;

	for (int i = strlen (extpath); i; i--)
	{
		if (extpath[i] == '.')
		{
			texext = &extpath[i];
			break;
		}
	}

	if (!stricmp (texext, ".link"))
	{
#if 0
		// copy out the name
		Q_strncpy (texname, extpath, 255);

		// now build the new name from the same path as the link file
		for (int c = strlen (texname) - 1; c; c--)
		{
			if (texname[c] == '/' || texname[c] == '\\')
			{
				for (int cc = 0;; cc++)
				{
					char fc = COM_FReadChar (fh);

					if (fc == '\n' || fc < 1 || fc == '\r') break;

					texname[c + cc + 1] = fc;
					texname[c + cc + 2] = 0;
				}

				break;
			}
		}

		// done with the file
		COM_FCloseFile (&fh);

		// set the new path
		extpath = texname;

		// now go round again to load it for real
		goto ext_tex_load;
#else
		for (int cc = 0;; cc++)
		{
			char fc = COM_FReadChar (fh);

			if (fc == '\n' || fc < 1 || fc == '\r') break;

			// ensure NULL termination
			texname[cc] = fc;
			texname[cc + 1] = 0;
		}

		// handle special names
		if (texname[0] == '#') texname[0] = '*';

		// done with the file
		COM_FCloseFile (&fh);

		return D3D_LoadExternalTexture (tex, texname, flags);
#endif
	}

	// all other supported formats
	// allocate a buffer to hold it
	byte *filebuf = (byte *) Zone_Alloc (filelen);

	// read it all in
	COM_FReadFile (fh, filebuf, filelen);

	// done with the file
	COM_FCloseFile (&fh);

	// D3DX can't load a PCX so we need our own loader
	if (!strcmp (texext, ".pcx"))
		if (!(filebuf = D3D_LoadPCX (filebuf, &filelen)))
			return NULL;

	// attempt to load it (this will generate miplevels for us too)
	hr = D3D_CreateExternalTexture (tex, filelen, filebuf, flags);

	if (FAILED (hr))
	{
		// in THEORY d3dx CAN load a TGA, but in practice it sometimes fails.
		// this is more likely than a compressed format being unsupported so try that first.
		// if it's a TGA and the real reason is that it's oddly sized we let it fall through to the no-compression case
		if (!strcmp (texext, ".tga"))
		{
			if (filebuf = D3D_LoadTGA (filebuf, &filelen))
			{
				// try it with the new TGA
				hr = D3D_CreateExternalTexture (tex, filelen, filebuf, flags);

				if (SUCCEEDED (hr))
				{
					// ha!  gotcha you dirty little piece of shit!
					Zone_Free (filebuf);
					return true;
				}
			}
		}

		// the reason for failure may be because the compressed format was unsupported for the texture dimensions,
		// so try it again without compression before giving up
		hr = D3D_CreateExternalTexture (tex, filelen, filebuf, flags);

		if (FAILED (hr))
		{
			Zone_Free (filebuf);
			return false;
		}
	}

	// load succeeded
	Zone_Free (filebuf);
	return true;
}


void D3D_LoadResourceTexture (LPDIRECT3DTEXTURE9 *tex, int ResourceID, int flags)
{
	hr = QD3DXCreateTextureFromResourceExA
	(
		d3d_Device,
		NULL,
		MAKEINTRESOURCE (ResourceID),
		D3DX_DEFAULT,
		D3DX_DEFAULT,
		(flags & IMAGE_MIPMAP) ? D3DX_DEFAULT : 1,
		0,
		D3D_GetTextureFormat (flags | IMAGE_ALPHA | IMAGE_32BIT),
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


void D3D_FlipTexels (unsigned int *texels, int width, int height)
{
	for (int x = 0; x < width; x++)
	{
		for (int y = 0; y < (height / 2); y++)
		{
			int pos1 = y * width + x;
			int pos2 = (height - 1 - y) * width + x;

			unsigned int temp = texels[pos1];
			texels[pos1] = texels[pos2];
			texels[pos2] = temp;
		}
	}
}


void D3D_MirrorTexels (unsigned int *texels, int width, int height)
{
	for (int x = 0; x < (width / 2); x++)
	{
		for (int y = 0; y < height; y++)
		{
			int pos1 = y * width + x;
			int pos2 = y * width + (width - 1 - x);

			unsigned int temp = texels[pos1];
			texels[pos1] = texels[pos2];
			texels[pos2] = temp;
		}
	}
}


void D3D_RotateTexels (unsigned int *texels, int width, int height)
{
	// fixme - rotate in place if possible
	unsigned int *dst = (unsigned int *) MainZone->Alloc (width * height * sizeof (unsigned int));

	for (int h = 0, dest_col = height - 1; h < height; ++h, --dest_col)
	{
		for (int w = 0; w < width; w++)
		{
			dst[( w * height) + dest_col] = texels[h * width + w];
		}
	}

	Q_MemCpy (texels, dst, width * height * sizeof (unsigned int));
	MainZone->Free (dst);
}



void D3D_RotateTexelsInPlace (unsigned int *texels, int size)
{
	for (int i = 0; i < size; i++)
	{
		for (int j = i; j < size; j++)
		{
			int pos1 = i * size + j;
			int pos2 = j * size + i;

			unsigned int temp = texels[pos1];

			texels[pos1] = texels[pos2];
			texels[pos2] = temp;
		}
	}
}


void D3D_AlignCubeMapFaceTexels (LPDIRECT3DSURFACE9 surf, D3DCUBEMAP_FACES face)
{
	D3DSURFACE_DESC surfdesc;
	D3DLOCKED_RECT lockrect;

	surf->GetDesc (&surfdesc);
	surf->LockRect (&lockrect, NULL, 0);

	if (surfdesc.Width == surfdesc.Height)
	{
		if (face == D3DCUBEMAP_FACE_POSITIVE_Y)
			D3D_FlipTexels ((unsigned int *) lockrect.pBits, surfdesc.Width, surfdesc.Height);
		else if (face == D3DCUBEMAP_FACE_NEGATIVE_Y)
			D3D_MirrorTexels ((unsigned int *) lockrect.pBits, surfdesc.Width, surfdesc.Height);
		else if (face == D3DCUBEMAP_FACE_POSITIVE_Z)
			D3D_RotateTexelsInPlace ((unsigned int *) lockrect.pBits, surfdesc.Width);
		else if (face == D3DCUBEMAP_FACE_POSITIVE_X)
			D3D_RotateTexelsInPlace ((unsigned int *) lockrect.pBits, surfdesc.Width);
		else if (face == D3DCUBEMAP_FACE_NEGATIVE_X)
		{
			D3D_RotateTexelsInPlace ((unsigned int *) lockrect.pBits, surfdesc.Width);
			D3D_MirrorTexels ((unsigned int *) lockrect.pBits, surfdesc.Width, surfdesc.Height);
			D3D_FlipTexels ((unsigned int *) lockrect.pBits, surfdesc.Width, surfdesc.Height);
		}
		else D3D_RotateTexelsInPlace ((unsigned int *) lockrect.pBits, surfdesc.Width);
	}

	surf->UnlockRect ();
}




