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

void D3D_UploadTexture (LPDIRECT3DTEXTURE9 *texture, void *data, int width, int height, int flags);

#pragma pack (push, 1)
typedef struct q2wal_s
{
	char		name[32];
	unsigned	width, height;
	unsigned	offsets[MIPLEVELS];		// four mip maps stored
	char		animname[32];			// next frame in animation chain
	int			flags;
	int			contents;
	int			value;
} q2wal_t;
#pragma pack (pop)


unsigned int q2palette_hc[256] =
{
	// hard-coded version of the q2 palette for .wal loading; this will break if a .wal uses anything other than the standard q2 palette
	-16777216, -15790321, -14737633, -13684945, -12632257, -11842741, -10790053, -9737365, -8684677, -7631989, -6579301, -5526613, -4473925, -3421237, -2368549, 
	-1315861, -10269917, -10796257, -11321569, -11584741, -12110053, -12636393, -12899561, -13424877, -13688045, -13951213, -14214385, -14477553, -15002869, 
	-15266037, -15528185, -15791353, -10526865, -10790041, -10792097, -11055269, -11318445, -11581621, -12107965, -12633285, -12896457, -13422801, -13685973, 
	-14211289, -14474461, -15000805, -15263977, -15527149, -7375021, -8690877, -9217221, -10006737, -3172533, -5801157, -7641297, -9481433, -1335513, -3437789, 
	-5277921, -7118053, -8958185, -10798321, -12638453, -14477561, -5817557, -6344925, -6870245, -7657709, -8446193, -9234677, -10021113, -11070720, -11858176, 
	-12382464, -12906752, -13432064, -13956352, -14480640, -15005952, -15530240, -8691893, -9218237, -9743553, -10006725, -10533065, -11058381, -11321553, 
	-11847893, -12373209, -12636381, -13162725, -13688041, -14214381, -14739697, -15266037, -15791353, -9487593, -10537193, -11325673, -12375273, -13163757, 
	-14214385, -15002869, -15791353, -5022897, -4228241, -3433581, -2638921, -3418145, -4995117, -6309949, -7886921, -9201753, -10778725, -12093557, -13670529, 
	-15248529, -15512729, -15776933, -16040109, -16304309, -16306369, -16308429, -16769237, -16771297, -16773357, -16775413, -16777216, -7645353, -8171697, 
	-8698041, -9223357, -9749701, -10276045, -10801361, -11064533, -11853021, -12640481, -13427941, -13954285, -14741745, -15529205, -16054521, -16777216, 
	-6840453, -7366797, -7894165, -8420509, -8946849, -9211049, -9737393, -10263737, -10790077, -11579589, -12369101, -13158613, -13684957, -14474469, -15263981, 
	-15790325, -6337729, -7126217, -7652561, -8440025, -8966365, -9753829, -10280169, -11067629, -11592945, -12380405, -13167861, -13955321, -14742777, -15268096, 
	-16056320, -16777216, -8946737, -9473085, -9999433, -10263641, -10789989, -11315313, -11841665, -12105869, -12632217, -13158569, -13684917, -14211265, -14475473, 
	-15001821, -15528169, -16054521, -6575237, -7364753, -7891101, -8680617, -9206965, -9996477, -10522821, -11049165, -11838681, -12628197, -13155565, -13681909, 
	-14471417, -14998784, -15526144, -16052480, -16711936, -14424305, -12594405, -11289817, -10508497, -10514637, -10519757, -1, -45, -89, -129, -173, -217, -5345, 
	-10473, -16625, -21753, -27904, -1081600, -1873152, -2926848, -3717376, -4769024, -5559552, -6611200, -7399680, -8450304, -9238784, -10551296, -12124160, 
	-13697024, -15007744, -1114112, -13158401, -65536, -16776961, -13948125, -15000809, -15527153, -1337473, -3968173, -6334669, -8700133, -1322041, -3691621, 
	-5797001, -7902377, 0
};

byte *D3D_LoadWAL (byte *f, int *loadsize)
{
	q2wal_t *walheader = (q2wal_t *) f;
	byte *mip0data = (byte *) walheader + walheader->offsets[0];
	byte *image_rgba = (byte *) MainZone->Alloc (walheader->width * walheader->height * 4 + 18);

	if (!image_rgba)
	{
		MainZone->Free (f);
		return NULL;
	}

	unsigned int *pbuf = (unsigned int *) (image_rgba + 18);

	for (int i = 0; i < walheader->width * walheader->height; i++)
		pbuf[i] = q2palette_hc[mip0data[i]];

	// now fill in our fake tga header
	memset (image_rgba, 0, 18);
	image_rgba[2] = 2;
	image_rgba[12] = walheader->width & 255;
	image_rgba[13] = walheader->width >> 8;
	image_rgba[14] = walheader->height & 255;
	image_rgba[15] = walheader->height >> 8;
	image_rgba[16] = 32;
	image_rgba[17] = 0x20;

	// need to modify the len also
	loadsize[0] = walheader->width * walheader->height * 4 + 18;

	MainZone->Free (f);
	return image_rgba;
}


byte *D3D_LoadLMP (byte *f, int *loadsize)
{
	sizedef_t *sd = (sizedef_t *) f;
	byte *mip0data = (byte *) (sd + 1);

	byte *image_rgba = (byte *) MainZone->Alloc (sd->width * sd->height * 4 + 18);

	if (!image_rgba)
	{
		MainZone->Free (f);
		return NULL;
	}

	unsigned int *pbuf = (unsigned int *) (image_rgba + 18);

	for (int i = 0; i < sd->width * sd->height; i++)
		pbuf[i] = (unsigned int) d3d_QuakePalette.standard32[mip0data[i]];

	// now fill in our fake tga header
	memset (image_rgba, 0, 18);
	image_rgba[2] = 2;
	image_rgba[12] = sd->width & 255;
	image_rgba[13] = sd->width >> 8;
	image_rgba[14] = sd->height & 255;
	image_rgba[15] = sd->height >> 8;
	image_rgba[16] = 32;
	image_rgba[17] = 0;	// fixme - Kurok skyboxes are pre-inverted, other Kurok LMPs are not...

	// need to modify the len also
	loadsize[0] = sd->width * sd->height * 4 + 18;

	MainZone->Free (f);
	return image_rgba;
}


HRESULT D3D_CreateExternalTexture (LPDIRECT3DTEXTURE9 *tex, int len, byte *data, int flags)
{
	SAFE_RELEASE (tex[0]);

	// wrap this monster so that we can more easily modify it if required
	hr = D3DXCreateTextureFromFileInMemoryEx
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
		tex
	);

	if (SUCCEEDED (hr))
	{
		// load into system ram instead, lock, then load through our normal loader
		// we load faster this way as we get to use our fast mipmap generator rather than the slow D3D one
		LPDIRECT3DTEXTURE9 tex2 = NULL;
		D3DLOCKED_RECT lockrect;
		D3DSURFACE_DESC surfdesc;

		tex[0]->GetLevelDesc (0, &surfdesc);
		tex[0]->LockRect (0, &lockrect, NULL, D3DLOCK_NO_DIRTY_UPDATE);
		D3D_UploadTexture (&tex2, lockrect.pBits, surfdesc.Width, surfdesc.Height, flags | IMAGE_32BIT);
		tex[0]->UnlockRect (0);
		tex[0]->Release ();
		tex[0] = tex2;

		// now bring it into video RAM
		if (SUCCEEDED (hr))
			tex[0]->PreLoad ();
	}

	return hr;
}


// types we're going to support - NOTE - link MUST be first in this list!
char *TextureExtensions[] = {"link", "dds", "tga", "bmp", "png", "jpg", "pcx", "wal", NULL};

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

	memcpy (&pcx, fin, sizeof (pcx));
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
	memset (image_rgba, 0, 18);
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

	memset (&targa_header, 0, sizeof (TargaHeader));

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
							pixbuf = image_rgba + realrow * columns * 4 + 18;
						}
					}
				}
				else
				{
					// non run-length packet
					for (j = 0; j < packetSize; j++)
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
	memset (image_rgba, 0, 18);
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
			FILE_READ_DATA,
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

	// ditto for .wal
	if (!strcmp (texext, ".wal"))
		if (!(filebuf = D3D_LoadWAL (filebuf, &filelen)))
			return NULL;

	// ditto for .lmp
	if (!strcmp (texext, ".lmp") && (flags & IMAGE_SKYBOX))
		if (!(filebuf = D3D_LoadLMP (filebuf, &filelen)))
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

		Zone_Free (filebuf);
		return false;
	}

	// load succeeded
	Zone_Free (filebuf);
	return true;
}


void D3D_LoadResourceTexture (char *name, LPDIRECT3DTEXTURE9 *tex, int ResourceID, int flags)
{
	hr = D3DXCreateTextureFromResourceExA
	(
		d3d_Device,
		NULL,
		MAKEINTRESOURCE (ResourceID),
		D3DX_DEFAULT,
		D3DX_DEFAULT,
		(flags & IMAGE_MIPMAP) ? D3DX_DEFAULT : 1,
		0,
		D3DFMT_A8R8G8B8,
		D3DPOOL_MANAGED,
		D3DX_FILTER_LINEAR,
		D3DX_FILTER_BOX,
		0,
		NULL,
		NULL,
		tex
	);

	if (FAILED (hr))
	{
		// try open the resource and load it as a file if the above fails
		byte *resdata = NULL;
		int reslen = Sys_LoadResourceData (ResourceID, (void **) &resdata);

		hr = D3DXCreateTextureFromFileInMemoryEx
		(
			d3d_Device,
			resdata,
			reslen,
			D3DX_DEFAULT,
			D3DX_DEFAULT,
			(flags & IMAGE_MIPMAP) ? D3DX_DEFAULT : 1,
			0,
			D3DFMT_A8R8G8B8,
			D3DPOOL_MANAGED,
			D3DX_FILTER_LINEAR,
			D3DX_FILTER_BOX,
			0,
			NULL,
			NULL,
			tex
		);

		if (FAILED (hr))
		{
			// a resource texture failing is a program crash bug
			Sys_Error ("D3D_LoadResourceTexture: Failed to create %s texture", name);
			return;
		}
	}

	tex[0]->PreLoad ();
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
			dst[(w * height) + dest_col] = texels[h * width + w];
		}
	}

	memcpy (texels, dst, width * height * sizeof (unsigned int));
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




