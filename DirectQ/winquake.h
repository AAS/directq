/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// winquake.h: Win32-specific Quake header file

#ifndef __WINQUAKE_H
#define __WINQUAKE_H
#pragma warning( disable : 4229 )  // mgraph gets this

#include <windows.h>
#define WM_MOUSEWHEEL                   0x020A

extern HRESULT hr;

#include <ddraw.h>
#include <dsound.h>

extern	int			global_nCmdShow;

#ifndef SERVERONLY

//#define SNDBUFSIZE 65536

#endif

void S_ClearSounds (void);

typedef enum {MS_WINDOWED, MS_FULLSCREEN, MS_FULLDIB, MS_UNINIT} modestate_t;

extern modestate_t	modestate;

extern HWND			d3d_Window;
extern bool		ActiveApp, Minimized;

extern bool	WinNT;

void IN_ShowMouse (BOOL mshow);
void IN_DeactivateMouse (void);
void IN_ActivateMouse (void);
void IN_MouseEvent (int mstate, int numbuttons, bool dinput);

extern bool	winsock_lib_initialized;

extern int		window_center_x, window_center_y;
extern RECT		window_rect;

extern bool	mouseinitialized;

extern HANDLE	hinput, houtput;

void IN_UpdateClipCursor (void);

void VID_SetDefaultMode (void);

// these are needed in vidnt and sys_win
LRESULT CALLBACK MainWndProc (HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
#define D3D_WINDOW_CLASS_NAME "D3DQuake Window Class"

#endif