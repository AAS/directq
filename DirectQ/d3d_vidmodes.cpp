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

#include "winquake.h"
#include "resource.h"
#include <commctrl.h>


/*
===================================================================================================================

		VIDEO DRIVER VERSION CONTROL

	When I make breaking changes to the video driver I change this cvar default and clients will pick up
	a known-good safe mode by default instead of whatever they may have specified.  This also happens the
	first time DirectQ is ever run.

	This prevents the client's value of the d3d_mode cvar from ever being invalid for the current mode list.

===================================================================================================================
*/

#define VIDDRIVER_VERSION "1.8.7-new-modes-list-v4"
cvar_t viddriver_version ("viddriver_version", "unknown", CVAR_ARCHIVE);

void D3DVid_UpdateDriver (void)
{
	// this must be called from Host_WriteConfiguration before writing out the cvars so that the next run will
	// have the correct driver version.
	Cvar_Set (&viddriver_version, VIDDRIVER_VERSION);
}


bool D3DVid_DriverChanged (void)
{
	if (strcmp (viddriver_version.string, VIDDRIVER_VERSION))
		return true;
	else return false;
}


/*
===================================================================================================================

		VIDEO MODE ENUMERATION AND SELECTION

	Handles the identification of video modes available and the selection of an appropriate mode

===================================================================================================================
*/

void D3DVid_ResizeFromCvar (cvar_t *var);

// let's cohabit more peacefully with fitz...
// fixme - do this right (using vid_width and vid_height and mode 0 for a windowed mode)
cvar_t vid_width ("d3d_width", 0.0f, CVAR_ARCHIVE, D3DVid_ResizeFromCvar);
cvar_t vid_height ("d3d_height", 0.0f, CVAR_ARCHIVE, D3DVid_ResizeFromCvar);

RECT WorkArea;

D3DDISPLAYMODE *d3d_DisplayModeList = NULL;
int d3d_NumDisplayModes = 0;

D3DDISPLAYMODE d3d_DesktopMode;
D3DDISPLAYMODE d3d_CurrentMode;
D3DDISPLAYMODE *d3d_DefaultWindowedMode;

void Menu_VideoDecodeVideoModes (D3DDISPLAYMODE *modelist, int nummodes);


int D3DVid_SortDisplayModes (D3DDISPLAYMODE *m1, D3DDISPLAYMODE *m2)
{
	if (m1->Width == m2->Width)
		return ((int) m1->Height - (int) m2->Height);
	else return ((int) m1->Width - (int) m2->Width);
}


void D3DVid_EnumerateVideoModes (void)
{
	// get the working area of the screen for window positioning
	SystemParametersInfo (SPI_GETWORKAREA, 0, &WorkArea, 0);

	// get the desktop mode for reference
	d3d_Object->GetAdapterDisplayMode (D3DADAPTER_DEFAULT, &d3d_DesktopMode);

	// get the count of modes for this format
	int ModeCount = d3d_Object->GetAdapterModeCount (D3DADAPTER_DEFAULT, D3DFMT_X8R8G8B8);

	// no modes available for this format
	if (!ModeCount)
	{
		Sys_Error ("D3DVid_EnumerateVideoModes: No 32 BPP display modes available");
		return;
	}

	// this allows up to 1 MB for display modes
	d3d_DisplayModeList = (D3DDISPLAYMODE *) scratchbuf;
	d3d_NumDisplayModes = 0;

	int d3d_MaxDisplayModes = SCRATCHBUF_SIZE / sizeof (D3DDISPLAYMODE);

	// set up mode 0 which is the windowed mode (width and height are unknown)
	d3d_DisplayModeList[0].Format = D3DFMT_UNKNOWN;
	d3d_DisplayModeList[0].Height = 0;
	d3d_DisplayModeList[0].RefreshRate = 0;
	d3d_DisplayModeList[0].Width = 0;
	d3d_NumDisplayModes = 1;

	// enumerate the rest of the modes
	for (int i = 0; i < ModeCount; i++)
	{
		// will we ever have more than 64k modes???
		if (d3d_NumDisplayModes >= d3d_MaxDisplayModes) break;

		// enumerate this mode
		d3d_Object->EnumAdapterModes (D3DADAPTER_DEFAULT, D3DFMT_X8R8G8B8, i, &d3d_DisplayModeList[d3d_NumDisplayModes]);

		// we're only interested in modes that match the desktop refresh rate
		if (d3d_DisplayModeList[d3d_NumDisplayModes].RefreshRate != d3d_DesktopMode.RefreshRate) continue;

		// don't allow modes < 640 x 480
		if (d3d_DisplayModeList[d3d_NumDisplayModes].Width < 640) continue;
		if (d3d_DisplayModeList[d3d_NumDisplayModes].Height < 480) continue;

		// if the mode width is < height we assume that we have a monitor capable of rotating it's desktop and therefore we skip the mode
		if (d3d_DisplayModeList[d3d_NumDisplayModes].Width < d3d_DisplayModeList[d3d_NumDisplayModes].Height) continue;

		// see if it is already present (it may be if there are multiple refresh rates for example)
		bool existingmode = false;

		// (note that we start at 1 as mode 0 is the windowed mode)
		for (int j = 1; j < d3d_NumDisplayModes; j++)
		{
			// only check width and height as we're only allowing 32bpp modes
			if (d3d_DisplayModeList[j].Width != d3d_DisplayModeList[d3d_NumDisplayModes].Width) continue;
			if (d3d_DisplayModeList[j].Height != d3d_DisplayModeList[d3d_NumDisplayModes].Height) continue;

			// this is a duplicate mode
			existingmode = true;
			break;
		}

		// if not found we go to a new mode
		if (!existingmode) d3d_NumDisplayModes++;
	}

	// now that we have the modes list we sort it and store it out
	qsort (d3d_DisplayModeList, d3d_NumDisplayModes, sizeof (D3DDISPLAYMODE), (sortfunc_t) D3DVid_SortDisplayModes);

	d3d_DisplayModeList = (D3DDISPLAYMODE *) MainZone->Alloc (d3d_NumDisplayModes * sizeof (D3DDISPLAYMODE));
	memcpy (d3d_DisplayModeList, scratchbuf, d3d_NumDisplayModes * sizeof (D3DDISPLAYMODE));

	// and finally we decode them for the video menu
	Menu_VideoDecodeVideoModes (d3d_DisplayModeList, d3d_NumDisplayModes);

	// now sync up the windowed mode with current cvar values
	d3d_DisplayModeList[0].Width = vid_width.integer;
	d3d_DisplayModeList[0].Height = vid_height.integer;
}



