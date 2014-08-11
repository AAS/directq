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
// in_win.c -- windows 95 mouse and joystick code

#define DIRECTINPUT_VERSION 0x0800

#include "quakedef.h"
#include "winquake.h"
#include <dinput.h>

#pragma comment (lib, "dinput8.lib")
#pragma comment (lib, "dxguid.lib")


// mouse variables
cvar_t freelook ("freelook", "1", CVAR_ARCHIVE);

bool mouselooking;

bool mouseinitialized = false;
bool in_mouseacquired = false;
extern bool keybind_grab;

void IN_StartupJoystick (void);
void Joy_AdvancedUpdate_f (void);
void CL_BoundViewPitch (float *viewangles);
void ClearAllStates (void);

cmd_t joyadvancedupdate ("joyadvancedupdate", Joy_AdvancedUpdate_f);

int mstate_di = 0;
int mouse_oldbuttonstate = 0;

int in_mx = 0;
int in_my = 0;

int window_center_x;
int window_center_y;

/*
========================================================================================================================

						KEYBOARD and MOUSE

========================================================================================================================
*/

LPDIRECTINPUT8 di_Object = NULL;
LPDIRECTINPUTDEVICE8 di_Mouse = NULL;

/*
===================================================================

KEY MAPPING

moved from gl_vidnt.c
shiftscantokey was unused

===================================================================
*/
byte scantokey[128] =
{
	// scancode to quake key table
	// 0     1     2     3     4     5     6     7     8     9     a     b     c     d     e     f
	0x00, 0x1b, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x30, 0x2d, 0x3d, 0x7f, 0x09,		// 0x0
	0x71, 0x77, 0x65, 0x72, 0x74, 0x79, 0x75, 0x69, 0x6f, 0x70, 0x5b, 0x5d, 0x0d, 0x85, 0x61, 0x73,		// 0x1
	0x64, 0x66, 0x67, 0x68, 0x6a, 0x6b, 0x6c, 0x3b, 0x27, 0x60, 0x86, 0x5c, 0x7a, 0x78, 0x63, 0x76,		// 0x2
	0x62, 0x6e, 0x6d, 0x2c, 0x2e, 0x2f, 0x86, 0x2a, 0x84, 0x20, 0x99, 0x87, 0x88, 0x89, 0x8a, 0x8b,		// 0x3
	0x8c, 0x8d, 0x8e, 0x8f, 0x90, 0xff, 0x00, 0x97, 0x80, 0x96, 0x2d, 0x82, 0x35, 0x83, 0x2b, 0x98,		// 0x4
	0x81, 0x95, 0x93, 0x94, 0x00, 0x00, 0x00, 0x91, 0x92, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,		// 0x5
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,		// 0x6
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00		// 0x7
};



int IN_MapKey (int key)
{
	if (key > 127)
		return 0;

	if (scantokey[key] == 0)
		Con_DPrintf ("key 0x%02x has no translation\n", key);

	return scantokey[key];
}


void IN_MouseMove (usercmd_t *cmd, double movetime)
{
	if (ActiveApp && !Minimized && in_mouseacquired)
	{
		extern cvar_t scr_fov;

		// this is just a rough scaling factor to bring directinput movement back up to the expected range
		float mx = (float) in_mx * sensitivity.value * 1.5f;
		float my = (float) in_my * sensitivity.value * 1.5f;

		in_mx = in_my = 0;

		if (scr_fov.value <= 75 && kurok)
		{
			float fovscale = ((100 - scr_fov.value) / 25.0f) + 1;

			mx /= fovscale;
			my /= fovscale;
		}

		if (mx || my)
		{
			mouselooking = freelook.integer || (in_mlook.state & 1);

			if ((in_strafe.state & 1) || (lookstrafe.value && mouselooking))
				cmd->sidemove += m_side.value * mx;
			else cl.viewangles[YAW] -= m_yaw.value * mx;

			if (mouselooking && !(in_strafe.state & 1))
			{
				cl.viewangles[PITCH] += m_pitch.value * my;
				CL_BoundViewPitch (cl.viewangles);
			}
			else
			{
				if ((in_strafe.state & 1) && noclip_anglehack)
					cmd->upmove -= m_forward.value * my;
				else cmd->forwardmove -= m_forward.value * my;
			}

			SetCursorPos (window_center_x, window_center_y);
		}
	}
}


void IN_ClearMouseState (void)
{
	in_mx = in_my = 0;
	mouse_oldbuttonstate = 0;
	mstate_di = 0;
}


#define NUM_DI_MBUTTONS		8

static DWORD di_MouseButtons[NUM_DI_MBUTTONS] =
{
	DIMOFS_BUTTON0,
	DIMOFS_BUTTON1,
	DIMOFS_BUTTON2,
	DIMOFS_BUTTON3,
	DIMOFS_BUTTON4,
	DIMOFS_BUTTON5,
	DIMOFS_BUTTON6,
	DIMOFS_BUTTON7
};

void IN_MouseEvent (int mstate)
{
	if (in_mouseacquired)
	{
		// we support 5 mouse buttons and we check them all
		for (int i = 0; i < NUM_DI_MBUTTONS; i++)
		{
			if ((mstate & (1 << i)) && !(mouse_oldbuttonstate & (1 << i))) Key_Event (K_MOUSE1 + i, true);
			if (!(mstate & (1 << i)) && (mouse_oldbuttonstate & (1 << i))) Key_Event (K_MOUSE1 + i, false);
		}

		mouse_oldbuttonstate = mstate;
	}
}


void IN_ReadDirectInputMessages (void)
{
	if (in_mouseacquired)
	{
		int mx = 0;
		int my = 0;
		extern double last_inputtime;

		for (int NumMouseEvents = 0; ; NumMouseEvents++)
		{
			DIDEVICEOBJECTDATA di_MouseBuffer;
			DWORD dwElements = 1;

			hr = di_Mouse->GetDeviceData (sizeof (DIDEVICEOBJECTDATA), &di_MouseBuffer, &dwElements, 0);

			if ((hr == DIERR_INPUTLOST) || (hr == DIERR_NOTACQUIRED))
			{
				IN_ClearMouseState ();
				di_Mouse->Acquire ();
				break;
			}

			// if we otherwise failed to read it or if we ran out of elements we just do nothing
			if (FAILED (hr) || dwElements == 0) break;

			// look at what we got and see what happened
			switch (di_MouseBuffer.dwOfs)
			{
			case DIMOFS_X:
				mx += di_MouseBuffer.dwData;
				break;

			case DIMOFS_Y:
				my += di_MouseBuffer.dwData;
				break;

			case DIMOFS_Z:
				// interpret mousewheel
				// note - this is a dword so we need to cast to int for < 0 comparison to work
				if ((int) di_MouseBuffer.dwData < 0)
				{
					// mwheeldown
					Key_Event (K_MWHEELDOWN, true);
					Key_Event (K_MWHEELDOWN, false);
				}
				else if ((int) di_MouseBuffer.dwData > 0)
				{
					// mwheelup
					Key_Event (K_MWHEELUP, true);
					Key_Event (K_MWHEELUP, false);
				}

				break;

			default:
				for (int i = 0; i < NUM_DI_MBUTTONS; i++)
				{
					if (di_MouseBuffer.dwOfs == di_MouseButtons[i])
					{
						if (di_MouseBuffer.dwData & 0x80)
							mstate_di |= (1 << i);
						else mstate_di &= ~(1 << i);

						// one is all it can be
						break;
					}
				}
				break;
			}

			// record the time of the input event
			last_inputtime = realtime;
		}

		// fire any events that happened
		IN_MouseEvent (mstate_di);

		// now store out the final movement for processing by the cmd
		in_mx += mx;
		in_my += my;
	}
}


bool IN_ReadInputMessages (HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	extern double last_inputtime;
	double saved = last_inputtime;
	int key = (int) lParam;

	key = (key >> 16) & 255;
	last_inputtime = realtime;

	switch (Msg)
	{
	case WM_MOUSEWHEEL:
		// in the console or the menu we capture the mousewheel and use it for scrolling
		if (key_dest == key_console)
		{
			if ((short) HIWORD (wParam) > 0)
			{
				Key_Event (K_PGUP, true);
				Key_Event (K_PGUP, false);
			}
			else
			{
				Key_Event (K_PGDN, true);
				Key_Event (K_PGDN, false);
			}

			return true;
		}
		else if (key_dest == key_menu)
		{
			if ((short) HIWORD (wParam) > 0)
			{
				Key_Event (K_UPARROW, true);
				Key_Event (K_UPARROW, false);
			}
			else
			{
				Key_Event (K_DOWNARROW, true);
				Key_Event (K_DOWNARROW, false);
			}

			return true;
		}
		else
		{
			last_inputtime = saved;
			return false;
		}

	case WM_LBUTTONDOWN:
	case WM_LBUTTONUP:
	case WM_RBUTTONDOWN:
	case WM_RBUTTONUP:
	case WM_MBUTTONDOWN:
	case WM_MBUTTONUP:
	case WM_MOUSEMOVE:
		// we just discard these messages
		return true;

	case WM_KEYDOWN:
	case WM_SYSKEYDOWN:
		Key_Event (IN_MapKey (key), true);
		return true;

	case WM_KEYUP:
	case WM_SYSKEYUP:
		Key_Event (IN_MapKey (key), false);
		return true;

	default:
		last_inputtime = saved;
		return false;
	}
}


/*
===========
Force_CenterView_f
===========
*/
void Force_CenterView_f (void)
{
	cl.viewangles[PITCH] = 0;
}


/*
===========
IN_UpdateClipCursor
===========
*/
void IN_UpdateClipCursor (void)
{
	if (mouseinitialized && in_mouseacquired)
	{
		// get the client coords of the window so that we don't go outside of them
		// this seems to be the only way to get the client coords relative to the screen;
		// oh well, at least there is a way and it's not too onerous
		WINDOWINFO windowinfo;

		windowinfo.cbSize = sizeof (WINDOWINFO);
		GetWindowInfo (d3d_Window, &windowinfo);
		ClipCursor (&windowinfo.rcClient);

		window_center_x = windowinfo.rcClient.left + ((windowinfo.rcClient.right - windowinfo.rcClient.left) >> 1);
		window_center_y = windowinfo.rcClient.top + ((windowinfo.rcClient.bottom - windowinfo.rcClient.top) >> 1);
	}
}


/*
===========
IN_StartupMouse
===========
*/
void IN_StartupMouse (void)
{
	if (COM_CheckParm ("-nomouse")) return;

	mouseinitialized = true;
}


/*
===========
IN_Init
===========
*/
cmd_t fcv_cmd ("force_centerview", Force_CenterView_f);

void IN_Init (void)
{
	hr = DirectInput8Create (GetModuleHandle (NULL), DIRECTINPUT_VERSION, IID_IDirectInput8, (void **) &di_Object, NULL);

	if (FAILED (hr) || !di_Object)
		Con_SafePrintf ("Error : IN_Init : DirectInput8Create failed\n");
	else IN_StartupMouse ();

	IN_UpdateClipCursor ();
	IN_StartupJoystick ();
}


void IN_UnacquireMouse (void)
{
	if (in_mouseacquired)
	{
		ClearAllStates ();

		if (di_Mouse)
		{
			// flush the directinput buffers and discard all events so that no events occur when we come back
			DWORD dwItems = INFINITE;
			di_Mouse->GetDeviceData (sizeof (DIDEVICEOBJECTDATA), NULL, &dwItems, 0);
			di_Mouse->Unacquire ();
			di_Mouse->Release ();
			di_Mouse = NULL;
		}

		ShowCursor (TRUE);
		ClipCursor (NULL);

		IN_ClearMouseState ();
		in_mouseacquired = false;
	}
}


void IN_AcquireMouse (void)
{
	if (!in_mouseacquired)
	{
		ClearAllStates ();

		SAFE_RELEASE (di_Mouse);

		if (di_Object)
		{
			if (FAILED (di_Object->CreateDevice (GUID_SysMouse, &di_Mouse, NULL)))
			{
				Con_SafePrintf ("Error : IN_AcquireMouse : IDirectInput8::CreateDevice failed for GUID_SysMouse\n");
				IN_UnacquireMouse ();
				return;
			}

			if (FAILED (di_Mouse->SetDataFormat (&c_dfDIMouse2)))
			{
				Con_SafePrintf ("Error : IN_AcquireMouse : IDirectInputDevice8::SetDataFormat failed for c_dfDIMouse2\n");
				IN_UnacquireMouse ();
				return;
			}

			for (int i = 1024; i; i >>= 1)
			{
				if (!i)
				{
					Con_SafePrintf ("Error : IN_AcquireMouse : IDirectInputDevice8::SetProperty failed\n");
					IN_UnacquireMouse ();
					return;
				}

				// set up the mouse buffer with 256 elements (should be more than enough)
				DIPROPDWORD dipdw;
				dipdw.diph.dwSize = sizeof (DIPROPDWORD);
				dipdw.diph.dwHeaderSize = sizeof (DIPROPHEADER);
				dipdw.diph.dwObj = 0;
				dipdw.diph.dwHow = DIPH_DEVICE;
				dipdw.dwData = i;
				hr = di_Mouse->SetProperty (DIPROP_BUFFERSIZE, &dipdw.diph);

				if (SUCCEEDED (hr)) break;
			}

			// set the cooperative level.
			if (FAILED (di_Mouse->SetCooperativeLevel (d3d_Window, DISCL_EXCLUSIVE | DISCL_FOREGROUND)))
			{
				Con_SafePrintf ("Error : IN_AcquireMouse : IDirectInputDevice8::SetCooperativeLevel failed\n");
				IN_UnacquireMouse ();
				return;
			}

			di_Mouse->Acquire ();
			in_mouseacquired = true;

			ShowCursor (FALSE);
			IN_ClearMouseState ();
			IN_UpdateClipCursor ();
		}
	}
}


/*
===================
IN_SetMouseState

sets the correct mouse state for the current view; if the required state is already set it just does nothing;
called once per frame before beginning to render
===================
*/
void IN_SetMouseState (bool fullscreen)
{
	// no mouse to set the state for
	if (!mouseinitialized) return;

	if (keybind_grab)
	{
		// non-negotiable, always capture and hide the mouse
		IN_AcquireMouse ();
	}
	else if (!ActiveApp || Minimized)
	{
		// give the mouse back to the user
		IN_UnacquireMouse ();
	}
	else if (key_dest == key_game || fullscreen || cl.maxclients > 1)
	{
		// non-negotiable, always capture and hide the mouse
		IN_AcquireMouse ();
	}
	else
	{
		// here we release the mouse back to the OS
		IN_UnacquireMouse ();
	}
}


/*
===========
IN_Shutdown
===========
*/
void IN_Shutdown (void)
{
	IN_UnacquireMouse ();

	SAFE_RELEASE (di_Object);
}

