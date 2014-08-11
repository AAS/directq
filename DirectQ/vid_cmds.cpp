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

#include <vector>

extern D3DPRESENT_PARAMETERS d3d_PresentParams;
extern std::vector<D3DDISPLAYMODE> d3d_DisplayModes;

void D3DVid_Restart (void);

extern cvar_t viddriver_version;

void D3DVid_InfoDump_f (void)
{
	if (d3d_Object && d3d_Device)
	{
		Con_Printf ("Getting driver info... ");
		FILE *f = fopen ("driver.bin", "wb");

		if (f)
		{
			// dump out info about the card and the currently running state so that we can analyze it later
			D3DADAPTER_IDENTIFIER9 adinfo;
			d3d_Object->GetAdapterIdentifier (D3DADAPTER_DEFAULT, 0, &adinfo);
			fwrite (&adinfo, sizeof (D3DADAPTER_IDENTIFIER9), 1, f);

			D3DCAPS9 dcaps;
			d3d_Device->GetDeviceCaps (&dcaps);
			fwrite (&dcaps, sizeof (D3DCAPS9), 1, f);

			fwrite (&d3d_PresentParams, sizeof (D3DPRESENT_PARAMETERS), 1, f);

			Con_Printf ("Done\n");
			fclose (f);
			return;
		}

		Con_Printf ("Failed to get driver info\n");
	}
}


void D3DVid_Restart_f (void)
{
	D3DVid_Restart ();

	// this should only ever be called from the command, preventing system-side restarts from stuffing to the console
	Con_Printf ("reset video mode\n");
}


void D3DVid_ConsoleInfo (void)
{
	// this one's for Spike
	Con_Printf ("\n");
	Con_Printf ("video driver    : version %i\n", viddriver_version.integer);
	Con_Printf ("vid_mode        : select a mode\n");
	Con_Printf ("vid_vsync       : vsync on/off\n");
	Con_Printf ("vid_fullscreen  : fullscreen on/off\n");

	if (d3d_GlobalCaps.MaxMultiSample > 0)
		Con_Printf ("vid_multisample : multisample level\n");
	else Con_Printf ("vid_multisample : unavailable\n");
}


void D3DVid_NumModes_f (void)
{
	Con_Printf ("%i video modes available\n", d3d_DisplayModes.size ());
	D3DVid_ConsoleInfo ();
}


void D3DVid_DescribeMode (D3DDISPLAYMODE *mode)
{
	Con_Printf ("%4i x %-4i  ", mode->Width, mode->Height);

	if (mode->RefreshRate)
		Con_Printf ("%i hz\n", mode->RefreshRate);
	else Con_Printf ("(windowed)\n");
}


void D3DVid_DescribeCurrentMode_f (void)
{
	D3DVid_DescribeMode (&d3d_CurrentMode);
	D3DVid_ConsoleInfo ();
}


void D3DVid_DescribeModes_f (void)
{
	for (int i = 0; i < d3d_DisplayModes.size (); i++)
	{
		Con_Printf ("%3i  ", i);
		D3DVid_DescribeMode (&d3d_DisplayModes[i]);
	}

	D3DVid_ConsoleInfo ();
}


void D3DVid_DescribeBackBuffer_f (void)
{
	LPDIRECT3DSURFACE9 pSurface = NULL;
	D3DSURFACE_DESC SurfaceDesc;
	RECT ClientRect;

	if (GetClientRect (d3d_Window, &ClientRect))
	{
		Con_Printf ("%i x %i client rect\n", (ClientRect.right - ClientRect.left), (ClientRect.bottom - ClientRect.top));
	}

	if (SUCCEEDED (d3d_Device->GetBackBuffer (0, 0, D3DBACKBUFFER_TYPE_MONO, &pSurface)))
	{
		if (SUCCEEDED (pSurface->GetDesc (&SurfaceDesc)))
			Con_Printf ("%i x %i backbuffer\n", SurfaceDesc.Width, SurfaceDesc.Height);

		SAFE_RELEASE (pSurface);
	}
}


cmd_t d3d_InfoDump_Cmd ("d3d_infodump", D3DVid_InfoDump_f);
cmd_t D3DVid_Restart_f_Cmd ("vid_restart", D3DVid_Restart_f);
cmd_t D3DVid_DescribeModes_f_Cmd ("vid_describemodes", D3DVid_DescribeModes_f);
cmd_t D3DVid_NumModes_f_Cmd ("vid_nummodes", D3DVid_NumModes_f);
cmd_t D3DVid_DescribeCurrentMode_f_Cmd ("vid_describecurrentmode", D3DVid_DescribeCurrentMode_f);
cmd_t D3DVid_DescribeBackBuffer_f_Cmd ("vid_describebackbuffer", D3DVid_DescribeBackBuffer_f);

