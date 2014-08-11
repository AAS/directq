
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
#include "d3d_vbo.h"
#include <vfw.h>

#pragma comment (lib, "vfw32.lib")

bool avi_incapture = false;

static LPDIRECT3DSURFACE9 avi_vidsurface = NULL;

static int avi_videoframe = 0;
static int avi_audioframe = 0;
static int avi_width = 0;
static int avi_height = 0;
static unsigned *avi_scaledbuf = NULL;

static PAVIFILE avifile = NULL;
static PAVISTREAM avi_basicvideo = NULL;
static PAVISTREAM avi_compressedvideo = NULL;
static PAVISTREAM avi_audiostream = NULL;


cvar_t		scr_capturewidth ("scr_capturewidth", 640);
cvar_t		scr_captureheight ("scr_captureheight", 480);
cvar_t		scr_capturefps ("scr_capturefps", 25);
cvar_t		scr_capturecompression ("scr_capturecompression", 0.0f);

void AVI_BeginCapture (char *capturedir, bool silent)
{
	char name[MAX_PATH];

	// create a directory to capture to and begin capturing
	sprintf (name, "%s/capture", com_gamedir);
	Sys_mkdir (name);
	sprintf (name, "%s/capture/%s.avi", com_gamedir, capturedir);

	// sometimes we don't want to spam the console
	if (!silent) Con_Printf ("Capturing to %s...\n", name);

	if (scr_capturewidth.integer > d3d_CurrentMode.Width) Cvar_Set (&scr_capturewidth, d3d_CurrentMode.Width);
	if (scr_captureheight.integer > d3d_CurrentMode.Height) Cvar_Set (&scr_captureheight, d3d_CurrentMode.Height);

	if (scr_capturewidth.integer < 1) Cvar_Set (&scr_capturewidth, 1);
	if (scr_captureheight.integer < 1) Cvar_Set (&scr_captureheight, 1);

	if (scr_capturefps.integer < 1) Cvar_Set (&scr_capturefps, 1);
	if (scr_capturefps.integer > 1000) Cvar_Set (&scr_capturefps, 1000);

	// to do - set up cvars and parameters/etc
	AVI_Begin (name, scr_capturewidth.integer, scr_captureheight.integer, scr_capturefps.integer, shm->speed, shm->channels, shm->samplebits);
}


void AVI_CaptureCommand_f (void)
{
	int argc = Cmd_Argc ();
	char *cmd = NULL;

	if (argc < 2)
		goto badargs;

	cmd = Cmd_Argv (1);

	if (!strcmp (cmd, "start"))
	{
		if (avi_incapture)
			Con_Printf ("Already capturing\n");
		else if (argc < 3)
			goto badargs;
		else AVI_BeginCapture (Cmd_Argv (2));
	}
	else if (!strcmp (cmd, "stop"))
	{
		if (avi_incapture)
		{
			avi_incapture = false;
			Con_Printf ("Capture stopped\n");
		}
		else Con_Printf ("Cannot stop - not currently recording\n");
	}
	else goto badargs;

	return;

badargs:;
	Con_Printf ("capture start <directory> : capture AV playback to the specified directory\n");
	Con_Printf ("capture stop : stop capturing\n");
}

void AVI_AutoCaptureCommand_f (void)
{
	if (avi_incapture)
	{
		AVI_End ();
		return;
	}

	char capturename[MAX_PATH];

	for (int i = 0; i < 9999; i++)
	{
		sprintf (capturename, "%s/capture/directq%04i.avi", com_gamedir, i);

		FILE *f = fopen (capturename, "rb");

		if (!f)
		{
			AVI_BeginCapture (va ("directq%04i", i));
			return;
		}

		fclose (f);
	}

	Con_Printf ("Failed to find a file to autocapture to\n");
}


cmd_t cmd_autocapture ("autocapture", AVI_AutoCaptureCommand_f);
cmd_t cmd_capture ("capture", AVI_CaptureCommand_f);


void AVI_Begin (char *aviname, int width, int height, int fps, int samples, int channels, int bits)
{
	if (avi_incapture) return;

	// default for uncompressed video
	DWORD vid_4cc = BI_RGB;

	if (scr_capturecompression.integer)
		vid_4cc = MAKEFOURCC ('M', 'P', 'G', '4');

	// delete down any previous instances of this file
	DeleteFile (aviname);

	AVIFileInit ();
	hr = AVIFileOpen (&avifile, aviname, OF_CREATE, NULL);

	if (FAILED (hr))
	{
		Con_Printf ("AVIStreamSetFormat failed\n");
		AVI_End ();
		return;
	}

	BITMAPINFOHEADER bi;
	AVISTREAMINFO si;

	memset (&bi, 0, sizeof (BITMAPINFOHEADER));
	bi.biSize = sizeof (BITMAPINFOHEADER);
	bi.biWidth = width;
	bi.biHeight = -height;	// by default the image data is inverted; setting height = -height will flip it to correct
	bi.biPlanes = 1;
	bi.biBitCount = 24;
	bi.biCompression = BI_RGB;
	bi.biSizeImage = width * height * 3;

	memset (&si, 0, sizeof (AVISTREAMINFO));
	si.fccType = streamtypeVIDEO;
	si.fccHandler = vid_4cc;
	si.dwScale = 1;
	si.dwRate = fps;
	si.dwSuggestedBufferSize = bi.biSizeImage;
	SetRect (&si.rcFrame, 0, 0, width, -height);

	hr = AVIFileCreateStream (avifile, &avi_basicvideo, &si);

	if (FAILED (hr))
	{
		Con_Printf ("AVIFileCreateStream failed\n");
		AVI_End ();
		return;
	}

	if (scr_capturecompression.integer)
	{
		AVICOMPRESSOPTIONS	opts;

		memset (&opts, 0, sizeof (AVICOMPRESSOPTIONS));
		opts.fccType = si.fccType;
		opts.fccHandler = vid_4cc;

		// Make the stream according to compression
		hr = AVIMakeCompressedStream (&avi_compressedvideo, avi_basicvideo, &opts, NULL);

		if (FAILED (hr))
		{
			Con_Printf ("AVIMakeCompressedStream failed\n");
			AVI_End ();
			return;
		}

		hr = AVIStreamSetFormat (avi_compressedvideo, 0, &bi, bi.biSize);
	}
	else hr = AVIStreamSetFormat (avi_basicvideo, 0, &bi, bi.biSize);

	if (FAILED (hr))
	{
		Con_Printf ("AVIStreamSetFormat failed\n");
		AVI_End ();
		return;
	}

	// init everything else we need
	avi_width = width;
	avi_height = height;
	avi_videoframe = 0;
	avi_audioframe = 0;

	if (!avi_vidsurface)
	{
		// create as D3DFMT_X8R8G8B8 so that we can handle 16bpp backbuffers too
		hr = d3d_Device->CreateOffscreenPlainSurface
		(
			d3d_CurrentMode.Width,
			d3d_CurrentMode.Height,
			D3DFMT_X8R8G8B8,
			D3DPOOL_SYSTEMMEM,
			&avi_vidsurface,
			NULL
		);

		if (FAILED (hr))
		{
			Con_Printf ("IDirect3DDevice9::CreateOffscreenPlainSurface failed\n");
			AVI_End ();
			return;
		}
	}

	// * 4 because we read as bgra
	if (!avi_scaledbuf) avi_scaledbuf = (unsigned *) MainZone->Alloc (width * height * 4);

	// we're capturing now (leave till last so any fails above won't trigger a capture)
	avi_incapture = true;
}


void AVI_BeginFrame (void)
{
	if (!avi_incapture)
	{
		// end a previous capture (this can be automatic by just setting avi_incapture = false elsewhere, or explicit by calling AVI_End elsewhere
		AVI_End ();
		return;
	}

	// anything else interesting we need to do at the start of a frame gets done here
}

void D3D_Resample32BitTexture (unsigned *in, int inwidth, int inheight, unsigned *out, int outwidth, int outheight);

void TM_MipMapWH (unsigned *data, int width, int height)
{
	int		i, j;
	byte	*out, *in;

	width <<= 2;
	height >>= 1;
	out = in = (byte *) data;

	for (i = 0; i < height; i++, in += width)
	{
		for (j = 0; j < width; j += 8, out += 4, in += 8)
		{
			out[0] = (in[0] + in[4] + in[width+0] + in[width+4]) >> 2;
			out[1] = (in[1] + in[5] + in[width+1] + in[width+5]) >> 2;
			out[2] = (in[2] + in[6] + in[width+2] + in[width+6]) >> 2;
			out[3] = (in[3] + in[7] + in[width+3] + in[width+7]) >> 2;
		}
	}
}


unsigned *TM_MipMapW (unsigned *data, int width, int height)
{
	int		i, size;
	byte	*out, *in;

	out = in = (byte *) data;
	size = (width * height) >> 1;

	for (i = 0; i < size; i++, out += 4, in += 8)
	{
		out[0] = (in[0] + in[4]) >> 1;
		out[1] = (in[1] + in[5]) >> 1;
		out[2] = (in[2] + in[6]) >> 1;
		out[3] = (in[3] + in[7]) >> 1;
	}

	return data;
}


unsigned *TM_MipMapH (unsigned *data, int width, int height)
{
	int		i, j;
	byte	*out, *in;

	out = in = (byte *) data;
	height >>= 1;
	width <<= 2;

	for (i = 0; i < height; i++, in += width)
	{
		for (j = 0; j < width; j += 4, out += 4, in += 4)
		{
			out[0] = (in[0] + in[width+0]) >> 1;
			out[1] = (in[1] + in[width+1]) >> 1;
			out[2] = (in[2] + in[width+2]) >> 1;
			out[3] = (in[3] + in[width+3]) >> 1;
		}
	}

	return data;
}


void AVI_Resample (unsigned *in)
{
	if (d3d_CurrentMode.Width == avi_width && d3d_CurrentMode.Height == avi_height)
	{
		// no need to resample so just copy out and return
		memcpy (avi_scaledbuf, in, avi_width * avi_height * 4);
		return;
	}

	int srcwidth = d3d_CurrentMode.Width;
	int srcheight = d3d_CurrentMode.Height;

	// mipmap down in-place if > 2* (faster than resampling and preserves quality better)
	// should this be > or >= ???
	while (srcwidth >= avi_width * 2 && srcheight >= avi_height * 2)
	{
		TM_MipMapWH (in, srcwidth, srcheight);
		srcwidth >>= 1;
		srcheight >>= 1;
	}

	while (srcwidth >= avi_width * 2)
	{
		TM_MipMapW (in, srcwidth, srcheight);
		srcwidth >>= 1;
	}

	while (srcheight >= avi_height * 2)
	{
		TM_MipMapH (in, srcwidth, srcheight);
		srcheight >>= 1;
	}

	if (srcwidth == avi_width && srcheight == avi_height)
	{
		// no need to resample so just copy out and return
		memcpy (avi_scaledbuf, in, avi_width * avi_height * 4);
		return;
	}

	// needed because resampling temp allocs on the hunk
	int hunkmark = MainHunk->GetLowMark ();

	D3D_Resample32BitTexture (in, srcwidth, srcheight, avi_scaledbuf, avi_width, avi_height);
	MainHunk->FreeToLowMark (hunkmark);
}


void AVI_CaptureVideo (void)
{
	// not capturing
	if (!avi_incapture) return;

	// grab the backbuffer surface for writing to an AVI file
	// the surface we'll use
	LPDIRECT3DSURFACE9 Surf = NULL;
	D3DLOCKED_RECT locked_rect;

	// get the backbuffer and transfer to system memory (we don't have a lockable backbuffer so we can't take it direct)
	// this is too slow for downsampling so we must do it directly ourselves
	d3d_Device->GetRenderTarget (0, &Surf);
	D3DXLoadSurfaceFromSurface (avi_vidsurface, NULL, NULL, Surf, NULL, NULL, D3DX_FILTER_NONE, 0);
	Surf->Release ();

	// now transfer from the system memory copy to our AVI (don't update dirty regions in the surf otherwise it will transfer back when we unlock)
	avi_vidsurface->LockRect (&locked_rect, NULL, D3DLOCK_NO_DIRTY_UPDATE);
	AVI_Resample ((unsigned *) locked_rect.pBits);
	avi_vidsurface->UnlockRect ();

	// take down to BGR in-place
	int size = avi_width * avi_height;
	unsigned char *src = (byte *) avi_scaledbuf;
	unsigned char *dst = (byte *) avi_scaledbuf;

	for (int i = 0; i < size; i++, src += 4, dst += 3)
	{
		dst[0] = src[0];
		dst[1] = src[1];
		dst[2] = src[2];
	}

	if (scr_capturecompression.integer)
		hr = AVIStreamWrite (avi_compressedvideo, avi_videoframe++, 1, avi_scaledbuf, size * 3, AVIIF_KEYFRAME, NULL, NULL);
	else hr = AVIStreamWrite (avi_basicvideo, avi_videoframe++, 1, avi_scaledbuf, size * 3, AVIIF_KEYFRAME, NULL, NULL);

	if (FAILED (hr))
	{
		Con_Printf ("AVIStreamWrite failed for video, resulting video may be corrupted\n");
		AVI_End ();
	}
}


void AVI_CaptureAudio (void *data, int len)
{
	if (!avi_incapture) return;

}


void AVI_EndFrame (void)
{
	if (!avi_incapture) return;

	// if (avi_videoframe > 200) AVI_End ();
}


void AVI_End (void)
{
	if (!avi_incapture) return;

	if (avi_scaledbuf)
	{
		MainZone->Free (avi_scaledbuf);
		avi_scaledbuf = NULL;
	}

	if (avi_vidsurface)
	{
		avi_vidsurface->Release ();
		avi_vidsurface = NULL;
	}

	if (avi_audiostream)
	{
		AVIStreamRelease (avi_audiostream);
		avi_audiostream = NULL;
	}

	if (avi_compressedvideo)
	{
		AVIStreamRelease (avi_compressedvideo);
		avi_compressedvideo = NULL;
	}

	if (avi_basicvideo)
	{
		AVIStreamRelease (avi_basicvideo);
		avi_basicvideo = NULL;
	}

	if (avifile)
	{
		AVIFileRelease (avifile);
		avifile = NULL;
	}

	AVIFileExit ();

	// we're no longer capturing
	avi_incapture = false;
}

