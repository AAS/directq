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


void D3DVid_CenterWindow (HWND hWnd)
{
	// get the size of the desktop working area and the window
	RECT workarea, windowrect;

	SystemParametersInfo (SPI_GETWORKAREA, 0, &workarea, 0);
	GetWindowRect (hWnd, &windowrect);

	// center it properly in the working area
	SetWindowPos
	(
		hWnd,
		HWND_TOP,
		((workarea.right - workarea.left) - (windowrect.right - windowrect.left)) / 2,
		((workarea.bottom - workarea.top) - (windowrect.bottom - windowrect.top)) / 2,
		0,
		0,
		SWP_NOSIZE | SWP_NOZORDER | SWP_SHOWWINDOW | SWP_DRAWFRAME
	);
}


void D3DVid_SendToForeground (HWND hWnd)
{
	SetWindowPos
	(
		hWnd,
		HWND_TOP,
		0,
		0,
		0,
		0,
		SWP_DRAWFRAME | SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOCOPYBITS
	);

	SetForegroundWindow (hWnd);
}


void D3DVid_SetWindowStyles (HWND hWnd, RECT *adjrect, D3DDISPLAYMODE *mode)
{
	DWORD WindowStyle;
	DWORD ExWindowStyle;

	if (mode->RefreshRate == 0)
	{
		// windowed mode
		WindowStyle = WS_TILEDWINDOW;//WS_OVERLAPPED | WS_BORDER | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
		ExWindowStyle = WS_EX_TOPMOST;
	}
	else
	{
		// fullscreen mode
		WindowStyle = WS_POPUP;
		ExWindowStyle = WS_EX_TOPMOST;
	}

	// reset the styles
	SetWindowLong (hWnd, GWL_EXSTYLE, ExWindowStyle);
	SetWindowLong (hWnd, GWL_STYLE, WindowStyle);

	if (adjrect)
	{
		// and calc the adjusted rect for the new style
		adjrect->left = 0;
		adjrect->top = 0;
		adjrect->right = mode->Width;
		adjrect->bottom = mode->Height;

		// evaluate the rect size for the style
		AdjustWindowRectEx (adjrect, WindowStyle, FALSE, ExWindowStyle);
	}
}


