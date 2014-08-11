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


typedef struct pcx_s
{
	char	manufacturer;
	char	version;
	char	encoding;
	char	bits_per_pixel;
	unsigned short	xmin, ymin, xmax, ymax;
	unsigned short	hres, vres;
	unsigned char	palette[48];
	char	reserved;
	char	color_planes;
	unsigned short	bytes_per_line;
	unsigned short	palette_type;
	char	filler[58];
} pcx_t;


byte *D3DImage_LoadPCX (byte *f, int *width, int *height)
{
	pcx_t	pcx;
	byte	*palette, *a, *b, *image_rgba, *fin, *pbuf, *enddata;
	int		x, y, x2, dataByte;

	if (com_filesize < sizeof (pcx) + 768) return NULL;

	fin = f;

	memcpy (&pcx, fin, sizeof (pcx));
	fin += sizeof (pcx);

	if (pcx.manufacturer != 0x0a || pcx.version != 5 || pcx.encoding != 1 || pcx.bits_per_pixel != 8) return NULL;

	*width = pcx.xmax + 1;
	*height = pcx.ymax + 1;

	palette = f + com_filesize - 768;

	if ((image_rgba = (byte *) MainHunk->Alloc ((*width) * (*height) * 4)) == NULL) return NULL;

	pbuf = image_rgba + (*width) * (*height) * 3;
	enddata = palette;

	for (y = 0; y < (*height) && fin < enddata; y++)
	{
		a = pbuf + y * (*width);

		for (x = 0; x < (*width) && fin < enddata;)
		{
			dataByte = *fin++;

			if (dataByte >= 0xC0)
			{
				if (fin >= enddata) break;

				x2 = x + (dataByte & 0x3F);
				dataByte = *fin++;

				if (x2 > (*width)) x2 = (*width);
				while (x < x2) a[x++] = dataByte;
			}
			else a[x++] = dataByte;
		}

		while (x < (*width))
			a[x++] = 0;
	}

	a = image_rgba;
	b = pbuf;

	for (x = 0; x < (*width) * (*height); x++)
	{
		y = *b++ * 3;

		// swap to bgra
		*a++ = palette[y + 2];
		*a++ = palette[y + 1];
		*a++ = palette[y];
		*a++ = 255;
	}

	return image_rgba;
}


typedef struct tgaheader_s
{
	unsigned char 	id_length, colormap_type, image_type;
	unsigned short	colormap_index, colormap_length;
	unsigned char	colormap_size;
	unsigned short	x_origin, y_origin, width, height;
	unsigned char	pixel_size, attributes;
} tgaheader_t;


byte *D3DImage_LoadTGA (byte *f, int *width, int *height)
{
	if (com_filesize < 18 + 3) return NULL;

	int columns, rows, row, column;
	byte *pixbuf, *image_rgba, *fin, *enddata;
	tgaheader_t header;

	memset (&header, 0, sizeof (tgaheader_t));

	header.id_length = f[0];
	header.colormap_type = f[1];
	header.image_type = f[2];

	header.colormap_index = f[3] + f[4] * 256;
	header.colormap_length = f[5] + f[6] * 256;
	header.colormap_size = f[7];
	header.x_origin = f[8] + f[9] * 256;
	header.y_origin = f[10] + f[11] * 256;
	header.width = f[12] + f[13] * 256;
	header.height = f[14] + f[15] * 256;

	header.pixel_size = f[16];
	header.attributes = f[17];

	if (header.image_type != 2 && header.image_type != 10) return NULL;
	if (header.colormap_type != 0	|| (header.pixel_size != 32 && header.pixel_size != 24)) return NULL;

	enddata = f + com_filesize;

	columns = header.width;
	rows = header.height;

	if ((image_rgba = (byte *) MainHunk->Alloc (columns * rows * 4)) == NULL)
	{
		Con_Printf ("LoadTGA: not enough memory for %i by %i image\n", columns, rows);
		return NULL;
	}

	fin = f + 18;

	if (header.id_length != 0)
		fin += header.id_length;  // skip TARGA image comment

	bool upside_down = !(header.attributes & 0x20); // johnfitz -- fix for upside-down targas
	int realrow;

	if (header.image_type == 2)
	{
		// Uncompressed, RGB images
		for (row = rows - 1; row >= 0; row--)
		{
			realrow = upside_down ? row : rows - 1 - row;
			pixbuf = image_rgba + realrow * columns * 4;

			for (column = 0; column < columns; column++)
			{
				switch (header.pixel_size)
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
	else if (header.image_type == 10)
	{
		// Runlength encoded RGB images
		unsigned char red = 0, green = 0, blue = 0, alphabyte = 0, packetHeader, packetSize, j;

		for (row = rows - 1; row >= 0; row--)
		{
			realrow = upside_down ? row : rows - 1 - row;
			pixbuf = image_rgba + realrow * columns * 4;

			for (column = 0; column < columns;)
			{
				if (fin >= enddata)
					goto outofdata;

				packetHeader = *fin++;
				packetSize = 1 + (packetHeader & 0x7f);

				if (packetHeader & 0x80)
				{
					// run-length packet
					switch (header.pixel_size)
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

					for (j = 0; j < packetSize; j++)
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
							pixbuf = image_rgba + realrow * columns * 4;
						}
					}
				}
				else
				{
					// non run-length packet
					for (j = 0; j < packetSize; j++)
					{
						switch (header.pixel_size)
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
							pixbuf = image_rgba + realrow * columns * 4;
						}
					}
				}
			}

breakOut:;
		}
	}

outofdata:;
	*width = columns;
	*height = rows;

	return image_rgba;
}



LPDIRECT3DTEXTURE9 D3DImage_LoadResourceTexture (char *name, int ResourceID, int flags)
{
	LPDIRECT3DTEXTURE9 tex = NULL;

	hr = D3DXCreateTextureFromResourceEx
	(
		d3d_Device, NULL,
		MAKEINTRESOURCE (ResourceID),
		D3DX_DEFAULT, D3DX_DEFAULT,
		(flags & IMAGE_MIPMAP) ? D3DX_DEFAULT : 1, 0,
		D3DFMT_A8R8G8B8, D3DPOOL_MANAGED,
		D3DX_FILTER_LINEAR, D3DX_FILTER_BOX,
		0, NULL, NULL, &tex
	);

	if (FAILED (hr) || !tex)
	{
		// try open the resource and load it as a file if the above fails
		byte *resdata = NULL;
		int reslen = Sys_LoadResourceData (ResourceID, (void **) &resdata);

		hr = D3DXCreateTextureFromFileInMemoryEx
		(
			d3d_Device, resdata, reslen,
			D3DX_DEFAULT, D3DX_DEFAULT,
			(flags & IMAGE_MIPMAP) ? D3DX_DEFAULT : 1, 0,
			D3DFMT_A8R8G8B8, D3DPOOL_MANAGED,
			D3DX_FILTER_LINEAR, D3DX_FILTER_BOX,
			0, NULL, NULL, &tex
		);

		if (FAILED (hr) || !tex)
		{
			// a resource texture failing is a program crash bug
			Sys_Error ("D3DImage_LoadResourceTexture: Failed to create %s texture", name);
			return NULL;
		}
	}

	tex->PreLoad ();

	return tex;
}


void D3DImage_FlipTexels (unsigned int *texels, int width, int height)
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


void D3DImage_MirrorTexels (unsigned int *texels, int width, int height)
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


void D3DImage_RotateTexels (unsigned int *texels, int width, int height)
{
	// fixme - rotate in place if possible
	int hunkmark = MainHunk->GetLowMark ();
	unsigned int *dst = (unsigned int *) MainHunk->Alloc (width * height * sizeof (unsigned int));

	if (dst)
	{
		for (int h = 0, dest_col = height - 1; h < height; ++h, --dest_col)
		{
			for (int w = 0; w < width; w++)
			{
				dst[(w * height) + dest_col] = texels[h * width + w];
			}
		}

		memcpy (texels, dst, width * height * sizeof (unsigned int));
	}

	MainHunk->FreeToLowMark (hunkmark);
}



void D3DImage_RotateTexelsInPlace (unsigned int *texels, int size)
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


void D3DImage_AlignCubeMapFaceTexels (LPDIRECT3DSURFACE9 surf, D3DCUBEMAP_FACES face)
{
	D3DSURFACE_DESC surfdesc;
	D3DLOCKED_RECT lockrect;

	if (SUCCEEDED (surf->GetDesc (&surfdesc)))
	{
		if (surfdesc.Pool == D3DPOOL_DEFAULT) return;

		if (SUCCEEDED (surf->LockRect (&lockrect, NULL, d3d_GlobalCaps.DefaultLock)))
		{
			if (surfdesc.Width == surfdesc.Height)
			{
				if (face == D3DCUBEMAP_FACE_POSITIVE_Y)
					D3DImage_FlipTexels ((unsigned int *) lockrect.pBits, surfdesc.Width, surfdesc.Height);
				else if (face == D3DCUBEMAP_FACE_NEGATIVE_Y)
					D3DImage_MirrorTexels ((unsigned int *) lockrect.pBits, surfdesc.Width, surfdesc.Height);
				else if (face == D3DCUBEMAP_FACE_POSITIVE_Z)
					D3DImage_RotateTexelsInPlace ((unsigned int *) lockrect.pBits, surfdesc.Width);
				else if (face == D3DCUBEMAP_FACE_POSITIVE_X)
					D3DImage_RotateTexelsInPlace ((unsigned int *) lockrect.pBits, surfdesc.Width);
				else if (face == D3DCUBEMAP_FACE_NEGATIVE_X)
				{
					D3DImage_RotateTexelsInPlace ((unsigned int *) lockrect.pBits, surfdesc.Width);
					D3DImage_MirrorTexels ((unsigned int *) lockrect.pBits, surfdesc.Width, surfdesc.Height);
					D3DImage_FlipTexels ((unsigned int *) lockrect.pBits, surfdesc.Width, surfdesc.Height);
				}
				else D3DImage_RotateTexelsInPlace ((unsigned int *) lockrect.pBits, surfdesc.Width);
			}

			surf->UnlockRect ();
		}
	}
}




