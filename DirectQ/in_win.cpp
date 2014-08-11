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

#include "quakedef.h"
#include "winquake.h"
#include <dinput.h>

#pragma comment (lib, "dinput8.lib")

// mouse variables
cvar_t m_filter ("m_filter", "0", CVAR_ARCHIVE);
cvar_t freelook ("freelook", "1", CVAR_ARCHIVE);
cvar_t m_directinput ("m_directinput", "1", CVAR_ARCHIVE);

bool mouselooking;

static int originalmouseparms[3] = {0, 0, 0};
static int newmouseparms[3] = {0, 0, 0};

bool mouseinitialized = false;
bool in_mouseacquired = false;
bool in_directinput = false;
extern bool keybind_grab;

RECT in_rect;
int in_center_x;
int in_center_y;

LPDIRECTINPUT8 di_Object = NULL;
LPDIRECTINPUTDEVICE8 di_Mouse = NULL;

#define DINPUT_BUFFERSIZE 64
DIDEVICEOBJECTDATA di_Data[64] = {0, 0, 0, 0, NULL};

int in_buttonstate = 0;
int in_oldbuttonstate = 0;

void CenterCursor (void)
{
	// and center it (using screen-relative coords)
	SetCursorPos (in_center_x, in_center_y);
}


/*
===========
IN_MouseEvent
===========
*/
void IN_MouseEvent (int mstate, int numbuttons)
{
	int	i;

	if (in_mouseacquired)
	{
		// perform button actions
		for (i = 0; i < numbuttons; i++)
		{
			if ((mstate & (1 << i)) && !(in_oldbuttonstate & (1 << i))) Key_Event (K_MOUSE1 + i, true);
			if (!(mstate & (1 << i)) && (in_oldbuttonstate & (1 << i))) Key_Event (K_MOUSE1 + i, false);
		}

		in_oldbuttonstate = mstate;
	}
}


void IN_ReadWinMessage (UINT msg, WPARAM wParam)
{
	if (!in_directinput)
	{
		if (msg == WM_MOUSEWHEEL)
		{
			if ((short) HIWORD (wParam) > 0)
			{
				Key_Event (K_MWHEELUP, true);
				Key_Event (K_MWHEELUP, false);
			}
			else
			{
				Key_Event (K_MWHEELDOWN, true);
				Key_Event (K_MWHEELDOWN, false);
			}
		}
		else
		{
			if (wParam & MK_LBUTTON) in_buttonstate |= 1;
			if (wParam & MK_RBUTTON) in_buttonstate |= 2;
			if (wParam & MK_MBUTTON) in_buttonstate |= 4;

			// 3 buttons and we always send even if temp is 0 so that we'll get key up events on them too
			IN_MouseEvent (in_buttonstate, 3);
			in_buttonstate = 0;
		}
	}
}


DWORD di_Buttons[] =
{
	DIMOFS_BUTTON0, DIMOFS_BUTTON1,
	DIMOFS_BUTTON2, DIMOFS_BUTTON3,
	DIMOFS_BUTTON4, DIMOFS_BUTTON5,
	DIMOFS_BUTTON6, DIMOFS_BUTTON7
};

#define DICHECKBUTTON(b) \
	case (FIELD_OFFSET(DIMOUSESTATE, rgbButtons) + (b)): \
		if (di_Data[i].dwData & 0x80) \
			in_buttonstate |= (1 << (b)); \
		else in_buttonstate &= ~(1 << (b)); \
		break;

void IN_ReadDirectInput (int *mx, int *my)
{
	if (!in_directinput) return;

	// attempt to acquire the mouse in case something elsewhere caused it to be lost
	di_Mouse->Acquire ();

	for (int read = 0;; read++)
	{
		// read the full buffer
		DWORD dwElements = 64;
		hr = di_Mouse->GetDeviceData (sizeof (DIDEVICEOBJECTDATA), di_Data, &dwElements, 0);

		switch (hr)
		{
		case DI_BUFFEROVERFLOW:
			Con_Printf ("dinput buffer overflow\n");
		case DI_OK:
			// success conditions
			break;

		case DIERR_INPUTLOST:
			// this is a recoverable error
			Con_Printf ("dinput device lost\n");
			di_Mouse->Acquire ();
			return;

		case E_PENDING:
		case DIERR_NOTACQUIRED:
		case DIERR_INVALIDPARAM:
		case DIERR_NOTINITIALIZED:
		case DIERR_NOTBUFFERED:
		default:
			// these are unrecoverable errors
			Con_Printf ("unrecoverable dinput condition\n");
			return;
		}

		// if no elements were read we've got to the end of the buffer
		if (dwElements < 1) break;

		for (int i = 0; i < dwElements; i++)
		{
			// let's see what happened
			switch (di_Data[i].dwOfs)
			{
			case DIMOFS_X:
				mx[0] += di_Data[i].dwData;
				break;

			case DIMOFS_Y:
				my[0] += di_Data[i].dwData;
				break;

			case DIMOFS_Z:
				if ((int) di_Data[i].dwOfs < 0)
				{
					// mwheeldown
					Key_Event (K_MWHEELDOWN, true);
					Key_Event (K_MWHEELDOWN, false);
				}
				else if ((int) di_Data[i].dwOfs > 0)
				{
					// mwheelup
					Key_Event (K_MWHEELUP, true);
					Key_Event (K_MWHEELUP, false);
				}

				break;

			DICHECKBUTTON (0);
			DICHECKBUTTON (1);
			DICHECKBUTTON (2);
			DICHECKBUTTON (3);
			DICHECKBUTTON (4);
			DICHECKBUTTON (5);
			DICHECKBUTTON (6);
			DICHECKBUTTON (7);

			default:
				break;
			}
		}
	}
}


/*
===========
IN_MouseMove
===========
*/
void IN_MouseMove (usercmd_t *cmd)
{
	if (ActiveApp && !Minimized)
	{
		// if the mouse isn't active or there was no movement accumulated we don't run this
		if (!in_mouseacquired) return;

		extern cvar_t cl_fullpitch;
		POINT cursorpos;
		int mx = 0, my = 0;

		if (in_directinput)
		{
			IN_ReadDirectInput (&mx, &my);

			// hack - these are on a different scale to the screen scale
			// in the absence of an api to do the conversion we just roughly double them
			// works for me!
			mx *= (sensitivity.value * 2.5f);
			my *= (sensitivity.value * 2.5f);

			// run mouse events here
			// directinput supports up to 8 buttons, raw input only up to 5
			// but the extra bits won't be set anyway...
			// we need to always run them even if there are no events this frame as there may be old events to clear
			IN_MouseEvent (in_buttonstate, 8);
		}
		else
		{
			GetCursorPos (&cursorpos);

			mx = (cursorpos.x - in_center_x) * sensitivity.value;
			my = (cursorpos.y - in_center_y) * sensitivity.value;
		}

		if (!mx && !my) return;

		mouselooking = freelook.integer || (in_mlook.state & 1);

		if ((in_strafe.state & 1) || (lookstrafe.value && mouselooking))
			cmd->sidemove += m_side.value * mx;
		else cl.viewangles[YAW] -= m_yaw.value * mx;

		if (mouselooking) V_StopPitchDrift ();

		if (mouselooking && !(in_strafe.state & 1))
		{
			cl.viewangles[PITCH] += m_pitch.value * my;

			if (cl_fullpitch.integer)
			{
				if (cl.viewangles[PITCH] > 90) cl.viewangles[PITCH] = 90;
				if (cl.viewangles[PITCH] < -90) cl.viewangles[PITCH] = -90;
			}
			else
			{
				if (cl.viewangles[PITCH] > 80) cl.viewangles[PITCH] = 80;
				if (cl.viewangles[PITCH] < -70) cl.viewangles[PITCH] = -70;
			}
		}
		else
		{
			if ((in_strafe.state & 1) && noclip_anglehack)
				cmd->upmove -= m_forward.value * my;
			else cmd->forwardmove -= m_forward.value * my;
		}

		// force the cursor back to the center so that there's room for it to move again
		// this also blocks WM_MOUSEMOVE messages so that the movement back doesn't accumulate
		// to the overall movement.
		CenterCursor ();
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
		memcpy (&in_rect, &windowinfo.rcClient, sizeof (RECT));

		in_center_x = in_rect.left + ((in_rect.right - in_rect.left) >> 1);
		in_center_y = in_rect.top + ((in_rect.bottom - in_rect.top) >> 1);
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
	SystemParametersInfo (SPI_GETMOUSE, 0, originalmouseparms, 0);

	// to do - also cvar-ize these!
	if (COM_CheckParm ("-noforcemspd"))
		newmouseparms[2] = originalmouseparms[2];

	if (COM_CheckParm ("-noforcemaccel"))
	{
		newmouseparms[0] = originalmouseparms[0];
		newmouseparms[1] = originalmouseparms[1];
	}

	if (COM_CheckParm ("-noforcemparms"))
	{
		newmouseparms[0] = originalmouseparms[0];
		newmouseparms[1] = originalmouseparms[1];
		newmouseparms[2] = originalmouseparms[2];
	}
}


/*
===========
IN_Init
===========
*/
cmd_t fcv_cmd ("force_centerview", Force_CenterView_f);

void IN_Init (void)
{
	// this was for windows 95 and NT 3.5.1 - see "about mouse input" in your MSDN
	// uiWheelMessage = RegisterWindowMessage ("MSWHEEL_ROLLMSG");
	IN_StartupMouse ();
}


/*
===================
IN_ClearStates
===================
*/
void IN_ClearStates (void)
{
	// send an up event for any keys that are still down
	for (int i = 0; i < 8; i++)
		if (in_oldbuttonstate & (1 << i)) Key_Event (K_MOUSE1 + i, false);

	in_buttonstate = 0;
	in_oldbuttonstate = 0;
}


void IN_FlushInput (void)
{
	IN_ClearStates ();

	if (in_directinput)
	{
		// attempt to acquire the mouse in case something elsewhere caused it to be lost
		di_Mouse->Acquire ();

		for (int read = 0;; read++)
		{
			// read the full buffer
			DWORD dwElements = 64;
			hr = di_Mouse->GetDeviceData (sizeof (DIDEVICEOBJECTDATA), di_Data, &dwElements, 0);

			switch (hr)
			{
			case DI_BUFFEROVERFLOW:
			case DI_OK:
				// success conditions
				break;

			case DIERR_INPUTLOST:
			case E_PENDING:
			case DIERR_NOTACQUIRED:
			case DIERR_INVALIDPARAM:
			case DIERR_NOTINITIALIZED:
			case DIERR_NOTBUFFERED:
			default:
				// failure conditions
				return;
			}

			// if no elements were read we've got to the end of the buffer
			if (dwElements < 1) break;
		}
	}
}


void IN_UnregisterDirectInput (void)
{
	// this also needs to be called whenever directinput fails so it's been split out
	if (di_Mouse)
	{
		di_Mouse->Unacquire ();
		di_Mouse->Release ();
		di_Mouse = NULL;
	}

	if (di_Object)
	{
		di_Object->Release ();
		di_Object = NULL;
	}
}


void IN_RegisterInput (void)
{
	if (m_directinput.integer > 1)
	{
		in_directinput = false;
		DIPROPDWORD	dipdw = {{sizeof (DIPROPDWORD), sizeof (DIPROPHEADER), 0, DIPH_DEVICE}, DINPUT_BUFFERSIZE};

		if (FAILED (DirectInput8Create (GetModuleHandle (NULL), DIRECTINPUT_VERSION, IID_IDirectInput8, (LPVOID *) &di_Object, NULL)))
			return;

		if (FAILED (di_Object->CreateDevice (GUID_SysMouse, &di_Mouse, NULL)))
		{
			IN_UnregisterDirectInput ();
			return;
		}

		if (FAILED (di_Mouse->SetDataFormat (&c_dfDIMouse2)))
		{
			IN_UnregisterDirectInput ();
			return;
		}

		if (FAILED (di_Mouse->SetCooperativeLevel (d3d_Window, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND)))
		{
			IN_UnregisterDirectInput ();
			return;
		}

		if (FAILED (di_Mouse->SetProperty (DIPROP_BUFFERSIZE, &dipdw.diph)))
		{
			IN_UnregisterDirectInput ();
			return;
		}

		in_directinput = true;
	}
}


void IN_UnregisterInput (void)
{
	if (in_directinput)
	{
		IN_UnregisterDirectInput ();
		in_directinput = false;
	}
}


void IN_UnacquireMouse (void)
{
	if (in_mouseacquired)
	{
		IN_UnregisterInput ();
		CenterCursor ();
		ShowCursor (TRUE);
		ClipCursor (NULL);
		ReleaseCapture ();
		SystemParametersInfo (SPI_SETMOUSE, 0, originalmouseparms, SPIF_SENDCHANGE);

		IN_ClearStates ();
		in_mouseacquired = false;
	}
}


void IN_AcquireMouse (void)
{
	if (!in_mouseacquired)
	{
		CenterCursor ();
		ShowCursor (FALSE);
		SystemParametersInfo (SPI_SETMOUSE, 0, newmouseparms, SPIF_SENDCHANGE);
		SetCapture (d3d_Window);
		IN_RegisterInput ();

		IN_ClearStates ();
		in_mouseacquired = true;

		IN_UpdateClipCursor ();
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
	static int in_oldmouse = -1;

	// no mouse to set the state for
	if (!mouseinitialized) return;

	if (m_directinput.integer != in_oldmouse)
	{
		// input type has changed
		IN_UnacquireMouse ();
		in_oldmouse = m_directinput.integer;
	}

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
}


int VK_to_QuakeKey[256] =
{
//	0		1		2		3		4		5		6		7		8		9		a		b		c		d		e		f
	200,	201,	0,		202,	203,	204,	0,		0,		0,		0,		0,		0,		0,		0,		0,		0,
	0,		0,		0,		0,		0,		0,		0,		0,		0,		0,		0,		27,		0,		0,		'0',	'1',
	'2',	'3',	'4',	'5',	'6',	'7',	'8',	'9'
};


bool gasks_keydown[256] = {false};

void IN_ReadKeyboardEvents (void)
{
	for (int i = 0; i < 8; i++)
	{
		unsigned short ks = (unsigned short) GetAsyncKeyState (i);

		if (ks & 0x8000)
		{
			if (!gasks_keydown[i])
			{
				Con_Printf ("%i\n", i);
				Key_Event (VK_to_QuakeKey[i], true);
				gasks_keydown[i] = true;
			}
		}
		else if (gasks_keydown[i])
		{
			Key_Event (VK_to_QuakeKey[i], false);
			gasks_keydown[i] = false;
		}
	}
}


