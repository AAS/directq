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

// evolving code; eventually all of the windows-side interaction in the video code is going to go here, and vidnt will just be for d3d stuff

#include "quakedef.h"
#include "d3d_model.h"
#include "d3d_quake.h"

#include "winquake.h"
#include "resource.h"
#include <commctrl.h>

void VIDWin32_SetActiveGamma (cvar_t *var);

cvar_t		v_gamma ("gamma", "1", CVAR_ARCHIVE, VIDWin32_SetActiveGamma);
cvar_t		r_gamma ("r_gamma", "1", CVAR_ARCHIVE, VIDWin32_SetActiveGamma);
cvar_t		g_gamma ("g_gamma", "1", CVAR_ARCHIVE, VIDWin32_SetActiveGamma);
cvar_t		b_gamma ("b_gamma", "1", CVAR_ARCHIVE, VIDWin32_SetActiveGamma);

cvar_t		vid_contrast ("contrast", "1", CVAR_ARCHIVE, VIDWin32_SetActiveGamma);
cvar_t		r_contrast ("r_contrast", "1", CVAR_ARCHIVE, VIDWin32_SetActiveGamma);
cvar_t		g_contrast ("g_contrast", "1", CVAR_ARCHIVE, VIDWin32_SetActiveGamma);
cvar_t		b_contrast ("b_contrast", "1", CVAR_ARCHIVE, VIDWin32_SetActiveGamma);

extern cvar_t vid_windowborders;

void VIDWin32_CenterWindow (D3DDISPLAYMODE *mode)
{
	// get the size of the desktop working area and the window
	RECT workarea, windowrect;

	SystemParametersInfo (SPI_GETWORKAREA, 0, &workarea, 0);
	GetWindowRect (d3d_Window, &windowrect);

	// center it properly in the working area (don't assume that top and left are 0!!!)
	SetWindowPos
	(
		d3d_Window,
		NULL,
		workarea.left + ((workarea.right - workarea.left) - (windowrect.right - windowrect.left)) / 2,
		workarea.top + ((workarea.bottom - workarea.top) - (windowrect.bottom - windowrect.top)) / 2,
		0,
		0,
		SWP_NOSIZE | SWP_NOZORDER | SWP_SHOWWINDOW | SWP_DRAWFRAME
	);
}

void VIDWin32_SetWindowFrame (D3DDISPLAYMODE *mode)
{
	RECT WindowRect = {0, 0, mode->Width, mode->Height};
	DWORD WindowStyle = mode->RefreshRate ? WS_POPUP : (vid_windowborders.value ? WS_OVERLAPPED | WS_BORDER | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX : WS_POPUP);
	DWORD ExWindowStyle = WS_EX_TOPMOST;

	// reset the styles
	SetWindowLong (d3d_Window, GWL_EXSTYLE, ExWindowStyle);
	SetWindowLong (d3d_Window, GWL_STYLE, WindowStyle);

	// and make the new rect
	AdjustWindowRectEx (&WindowRect, WindowStyle, FALSE, ExWindowStyle);

	// and update to the new size and frame state
	SetWindowPos (d3d_Window,
		NULL,
		0,
		0,
		(WindowRect.right - WindowRect.left),
		(WindowRect.bottom - WindowRect.top),
		SWP_NOMOVE | SWP_NOZORDER | SWP_FRAMECHANGED);

	// and move it to center screen
	VIDWin32_CenterWindow (mode);
}


typedef struct vid_gammaramp_s
{
	WORD r[256];
	WORD g[256];
	WORD b[256];
} vid_gammaramp_t;

vid_gammaramp_t d3d_DefaultGamma;
vid_gammaramp_t d3d_CurrentGamma;

void VID_SetGammaGeneric (vid_gammaramp_t *gr)
{
	HDC hdc = GetDC (NULL);
	SetDeviceGammaRamp (hdc, gr);
	ReleaseDC (NULL, hdc);
}


void VID_SetOSGamma (void) {VID_SetGammaGeneric (&d3d_DefaultGamma);}
void VID_SetAppGamma (void) {VID_SetGammaGeneric (&d3d_CurrentGamma);}


void VID_GetCurrentGamma (void)
{
	// retrieve and store the gamma ramp for the desktop
	HDC hdc = GetDC (NULL);
	GetDeviceGammaRamp (hdc, &d3d_DefaultGamma);
	ReleaseDC (NULL, hdc);
}


void VID_DefaultMonitorGamma_f (void)
{
	// restore ramps to linear in case something fucks up
	for (int i = 0; i < 256; i++)
	{
		// this is correct in terms of the default linear GDI gamma
		d3d_DefaultGamma.r[i] = d3d_CurrentGamma.r[i] = i << 8;
		d3d_DefaultGamma.g[i] = d3d_CurrentGamma.g[i] = i << 8;
		d3d_DefaultGamma.b[i] = d3d_CurrentGamma.b[i] = i << 8;
	}

	VID_SetOSGamma ();
}


cmd_t VID_DefaultMonitorGamma_Cmd ("vid_defaultmonitorgamma", VID_DefaultMonitorGamma_f);


int D3DVid_AdjustGamma (float gammaval, int baseval)
{
	baseval >>= 8;

	// the same gamma calc as GLQuake had some "gamma creep" where DirectQ would gradually get brighter
	// the more it was run; this hopefully fixes it once and for all
	float f = pow ((float) baseval / 255.0f, (float) gammaval);
	float inf = f * 255 + 0.5;

	// return what we got
	return (BYTE_CLAMP ((int) inf)) << 8;
}


int D3DVid_AdjustContrast (float contrastval, int baseval)
{
	int i = ((float) (baseval - 32767) * contrastval) + 32767;

	if (i < 0)
		return 0;
	else if (i > 65535)
		return 65535;
	else return i;
}


void VIDWin32_SetActiveGamma (cvar_t *var)
{
	// create a valid baseline for everything to work from
	for (int i = 0; i < 256; i++)
	{
		d3d_CurrentGamma.r[i] = d3d_DefaultGamma.r[i];
		d3d_CurrentGamma.g[i] = d3d_DefaultGamma.g[i];
		d3d_CurrentGamma.b[i] = d3d_DefaultGamma.b[i];
	}

	// apply v_gamma to all components
	for (int i = 0; i < 256; i++)
	{
		d3d_CurrentGamma.r[i] = D3DVid_AdjustGamma (v_gamma.value, d3d_CurrentGamma.r[i]);
		d3d_CurrentGamma.g[i] = D3DVid_AdjustGamma (v_gamma.value, d3d_CurrentGamma.g[i]);
		d3d_CurrentGamma.b[i] = D3DVid_AdjustGamma (v_gamma.value, d3d_CurrentGamma.b[i]);
	}

	// now apply r/g/b to the derived values
	for (int i = 0; i < 256; i++)
	{
		d3d_CurrentGamma.r[i] = D3DVid_AdjustGamma (r_gamma.value, d3d_CurrentGamma.r[i]);
		d3d_CurrentGamma.g[i] = D3DVid_AdjustGamma (g_gamma.value, d3d_CurrentGamma.g[i]);
		d3d_CurrentGamma.b[i] = D3DVid_AdjustGamma (b_gamma.value, d3d_CurrentGamma.b[i]);
	}

	// apply global contrast
	for (int i = 0; i < 256; i++)
	{
		d3d_CurrentGamma.r[i] = D3DVid_AdjustContrast (vid_contrast.value, d3d_CurrentGamma.r[i]);
		d3d_CurrentGamma.g[i] = D3DVid_AdjustContrast (vid_contrast.value, d3d_CurrentGamma.g[i]);
		d3d_CurrentGamma.b[i] = D3DVid_AdjustContrast (vid_contrast.value, d3d_CurrentGamma.b[i]);
	}

	// and again with the r/g/b
	for (int i = 0; i < 256; i++)
	{
		d3d_CurrentGamma.r[i] = D3DVid_AdjustContrast (r_contrast.value, d3d_CurrentGamma.r[i]);
		d3d_CurrentGamma.g[i] = D3DVid_AdjustContrast (g_contrast.value, d3d_CurrentGamma.g[i]);
		d3d_CurrentGamma.b[i] = D3DVid_AdjustContrast (b_contrast.value, d3d_CurrentGamma.b[i]);
	}

	VID_SetAppGamma ();
}


