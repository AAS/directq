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

// mouse variables
cvar_t freelook ("freelook", "1", CVAR_ARCHIVE);

bool mouselooking;

bool mouseinitialized = false;
bool in_mouseacquired = false;
extern bool keybind_grab;

void IN_StartupJoystick (void);
void Joy_AdvancedUpdate_f (void);
void CL_BoundViewPitch (float *viewangles);

cmd_t joyadvancedupdate ("joyadvancedupdate", Joy_AdvancedUpdate_f);


static int originalmouseparms[3];
static int newmouseparms[3] = {0, 0, 0};


/*
========================================================================================================================

						KEYBOARD and MOUSE

========================================================================================================================
*/


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


void IN_StartupKeyboard (void)
{
	RAWINPUTDEVICE ri_Keyboard;

	ri_Keyboard.usUsagePage = 1;
	ri_Keyboard.usUsage = 6;
	ri_Keyboard.dwFlags = RIDEV_NOLEGACY;
	ri_Keyboard.hwndTarget = NULL;

	if (!RegisterRawInputDevices (&ri_Keyboard, 1, sizeof (RAWINPUTDEVICE)))
	{
		Sys_Error ("IN_StartupKeyboard : Failed to register for raw input");
	}
}


void IN_ShutdownKeyboard (void)
{
	RAWINPUTDEVICE ri_Keyboard;

	ri_Keyboard.usUsagePage = 1;
	ri_Keyboard.usUsage = 6;
	ri_Keyboard.dwFlags = RIDEV_REMOVE;
	ri_Keyboard.hwndTarget = NULL;

	// allow this to fail silently as it happens during shutdown
	RegisterRawInputDevices (&ri_Keyboard, 1, sizeof (RAWINPUTDEVICE));
}


void IN_ReadKeyboard (RAWKEYBOARD *ri_Keyboard)
{
	// clear extra bits
	ri_Keyboard->Flags &= RI_KEY_BREAK;

	int key = IN_MapKey (ri_Keyboard->MakeCode > 127 ? 0 : ri_Keyboard->MakeCode);

	if (!ri_Keyboard->Flags)
		Key_Event (key, true);
	else if (ri_Keyboard->Flags & RI_KEY_BREAK)
		Key_Event (key, false);
}

// decode raw input structs to stuff we can actually use in Quake
typedef struct mousepos_s
{
	float x;
	float y;
} mousepos_t;

typedef struct ri_mousebutton_s
{
	int downflag;
	int upflag;
	int quakekey;
	bool down;
} ri_mousebutton_t;

typedef struct in_mousestate_s
{
	mousepos_t currpos;
	ri_mousebutton_t buttons[5];
} in_mousestate_t;

in_mousestate_t in_mousestate =
{
	{0, 0},
	{
		{RI_MOUSE_BUTTON_1_DOWN, RI_MOUSE_BUTTON_1_UP, K_MOUSE1, false},
		{RI_MOUSE_BUTTON_2_DOWN, RI_MOUSE_BUTTON_2_UP, K_MOUSE2, false},
		{RI_MOUSE_BUTTON_3_DOWN, RI_MOUSE_BUTTON_3_UP, K_MOUSE3, false},
		{RI_MOUSE_BUTTON_4_DOWN, RI_MOUSE_BUTTON_4_UP, K_MOUSE4, false},
		{RI_MOUSE_BUTTON_5_DOWN, RI_MOUSE_BUTTON_5_UP, K_MOUSE5, false}
	}
};


void CL_ClearCmd (usercmd_t *cmd);
void CL_RebalanceMove (usercmd_t *basecmd, usercmd_t *newcmd, double frametime);

void IN_MouseMove (usercmd_t *cmd, double movetime)
{
	if (ActiveApp && !Minimized)
	{
		// if the mouse isn't active or there was no movement accumulated we don't run this
		if (!in_mouseacquired) return;

		// always eval movement even if it's 0 so that we can set the last factors correctly
		float mx = in_mousestate.currpos.x * sensitivity.value * 2.0f;
		float my = in_mousestate.currpos.y * sensitivity.value * 2.0f;

		extern cvar_t scr_fov;
		usercmd_t basecmd;

		CL_ClearCmd (&basecmd);

		if (scr_fov.value <= 75 && kurok)
		{
			float fovscale = ((100 - scr_fov.value) / 25.0f) + 1;

			mx /= fovscale;
			my /= fovscale;
		}

		if (in_mousestate.currpos.x || in_mousestate.currpos.y)
		{
			mouselooking = freelook.integer || (in_mlook.state & 1);

			if ((in_strafe.state & 1) || (lookstrafe.value && mouselooking))
				basecmd.sidemove += m_side.value * mx;
			else cl.viewangles[YAW] -= m_yaw.value * mx;

			if (mouselooking && !(in_strafe.state & 1))
			{
				cl.viewangles[PITCH] += m_pitch.value * my;
				CL_BoundViewPitch (cl.viewangles);
			}
			else
			{
				if ((in_strafe.state & 1) && noclip_anglehack)
					basecmd.upmove -= m_forward.value * my;
				else basecmd.forwardmove -= m_forward.value * my;
			}
		}

		// rebalance the move to 72 FPS
		CL_RebalanceMove (cmd, &basecmd, movetime);

		// reset the accumulated positions
		in_mousestate.currpos.x = 0;
		in_mousestate.currpos.y = 0;
	}
}


void IN_ClearMouseState (void)
{
	for (int i = 0; i < 5; i++)
	{
		if (in_mousestate.buttons[i].down)
		{
			Key_Event (in_mousestate.buttons[i].quakekey, false);
			in_mousestate.buttons[i].down = false;
		}
	}

	in_mousestate.currpos.x = 0;
	in_mousestate.currpos.y = 0;
}


void IN_ReadMouse (RAWMOUSE *ri_Mouse)
{
	// read and accumulate the movement
	in_mousestate.currpos.x += ri_Mouse->lLastX;
	in_mousestate.currpos.y += ri_Mouse->lLastY;

	// 5 mouse buttons and we check them all
	for (int i = 0; i < 5; i++)
	{
		if ((ri_Mouse->usButtonFlags & in_mousestate.buttons[i].downflag) && !in_mousestate.buttons[i].down)
		{
			Key_Event (in_mousestate.buttons[i].quakekey, true);
			in_mousestate.buttons[i].down = true;
		}

		if ((ri_Mouse->usButtonFlags & in_mousestate.buttons[i].upflag) && in_mousestate.buttons[i].down)
		{
			Key_Event (in_mousestate.buttons[i].quakekey, false);
			in_mousestate.buttons[i].down = false;
		}
	}

	// track the wheel
	if (ri_Mouse->usButtonFlags & RI_MOUSE_WHEEL)
	{
		// this needs to cast to a short so that we can catch the proper delta
		if ((short) ri_Mouse->usButtonData > 0)
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
}


void IN_ReadRawInput (HRAWINPUT ri_Handle)
{
	UINT bufsize = 0;
	RAWINPUT ri_Buffer;

	// the second call will fail if we don't make the first
	GetRawInputData (ri_Handle, RID_INPUT, NULL, &bufsize, sizeof (RAWINPUTHEADER));
	GetRawInputData (ri_Handle, RID_INPUT, &ri_Buffer, &bufsize, sizeof (RAWINPUTHEADER));

	// and read the appropriate device type
	if (ri_Buffer.header.dwType == RIM_TYPEMOUSE)
		IN_ReadMouse (&ri_Buffer.data.mouse);
	else if (ri_Buffer.header.dwType == RIM_TYPEKEYBOARD)
		IN_ReadKeyboard (&ri_Buffer.data.keyboard);
	else Con_DPrintf ("Unknown raw input device\n");
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

		// clip to a 2x2 region center-screen to prevent the mouse from going outside the client region if we miss a window message in-game
		RECT cliprect;

		cliprect.left = windowinfo.rcClient.left + ((windowinfo.rcClient.right - windowinfo.rcClient.left) >> 1) - 1;
		cliprect.top = windowinfo.rcClient.top + ((windowinfo.rcClient.bottom - windowinfo.rcClient.top) >> 1) - 1;
		cliprect.right = cliprect.left + 2;
		cliprect.bottom = cliprect.top + 2;

		ClipCursor (&cliprect); //windowinfo.rcClient);
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
	// this was for windows 95 and NT 3.5.1 - see "about mouse input" in your MSDN
	// uiWheelMessage = RegisterWindowMessage ("MSWHEEL_ROLLMSG");
	IN_StartupMouse ();
	IN_StartupKeyboard ();
	IN_StartupJoystick ();
}


void IN_RegisterRawInputMouse (void)
{
	RAWINPUTDEVICE ri_Mouse;

	ri_Mouse.usUsagePage = 1;
	ri_Mouse.usUsage = 2;
	ri_Mouse.dwFlags = RIDEV_NOLEGACY;
	ri_Mouse.hwndTarget = NULL;

	if (!RegisterRawInputDevices (&ri_Mouse, 1, sizeof (RAWINPUTDEVICE)))
	{
		Sys_Error ("IN_RegisterRawInputMouse : Failed to register for raw input");
	}
}


void IN_UnregisterRawInputMouse (void)
{
	RAWINPUTDEVICE ri_Mouse;

	ri_Mouse.usUsagePage = 1;
	ri_Mouse.usUsage = 2;
	ri_Mouse.dwFlags = RIDEV_REMOVE;
	ri_Mouse.hwndTarget = NULL;

	if (!RegisterRawInputDevices (&ri_Mouse, 1, sizeof (RAWINPUTDEVICE)))
	{
		Sys_Error ("IN_UnregisterRawInputMouse : Failed to unregister for raw input");
	}
}


void IN_UnacquireMouse (void)
{
	if (in_mouseacquired)
	{
		ShowCursor (TRUE);
		ClipCursor (NULL);
		IN_UnregisterRawInputMouse ();

		IN_ClearMouseState ();
		in_mouseacquired = false;

		SystemParametersInfo (SPI_SETMOUSE, 0, originalmouseparms, 0);

		// sending WM_MOUSEMOVE doesn't work for correcting the cursor from busy to standard arrow, so
		// instead we hide it again, pump messages for a while, then show it again and pump some more messages
		ShowCursor (FALSE);
		Sys_SendKeyEvents ();
		ShowCursor (TRUE);
		Sys_SendKeyEvents ();
	}
}


void IN_AcquireMouse (void)
{
	if (!in_mouseacquired)
	{
		// done before bringing on raw input as raw input might disable our ability to do it
		if (SystemParametersInfo (SPI_GETMOUSE, 0, originalmouseparms, 0))
		{
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

		SystemParametersInfo (SPI_SETMOUSE, 0, newmouseparms, 0);

		ShowCursor (FALSE);
		IN_RegisterRawInputMouse ();

		IN_ClearMouseState ();
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
	IN_ShutdownKeyboard ();
}


/*
========================================================================================================================

						JOYSTICK

========================================================================================================================
*/


// joystick defines and variables
// where should defines be moved?
#define JOY_ABSOLUTE_AXIS	0x00000000		// control like a joystick
#define JOY_RELATIVE_AXIS	0x00000010		// control like a mouse, spinner, trackball
#define	JOY_MAX_AXES		6				// X, Y, Z, R, U, V
#define JOY_AXIS_X			0
#define JOY_AXIS_Y			1
#define JOY_AXIS_Z			2
#define JOY_AXIS_R			3
#define JOY_AXIS_U			4
#define JOY_AXIS_V			5


enum _ControlList
{
	AxisNada = 0, AxisForward, AxisLook, AxisSide, AxisTurn
};

DWORD dwAxisFlags[JOY_MAX_AXES] =
{
	JOY_RETURNX, JOY_RETURNY, JOY_RETURNZ, JOY_RETURNR, JOY_RETURNU, JOY_RETURNV
};

DWORD	dwAxisMap[JOY_MAX_AXES];
DWORD	dwControlMap[JOY_MAX_AXES];
PDWORD	pdwRawValue[JOY_MAX_AXES];


// none of these cvars are saved over a session
// this means that advanced controller configuration needs to be executed
// each time.  this avoids any problems with getting back to a default usage
// or when changing from one controller to another.  this way at least something
// works.
cvar_t	in_joystick ("joystick", "0", CVAR_ARCHIVE);
cvar_t	joy_name ("joyname", "joystick");
cvar_t	joy_advanced ("joyadvanced", "0");
cvar_t	joy_advaxisx ("joyadvaxisx", "0");
cvar_t	joy_advaxisy ("joyadvaxisy", "0");
cvar_t	joy_advaxisz ("joyadvaxisz", "0");
cvar_t	joy_advaxisr ("joyadvaxisr", "0");
cvar_t	joy_advaxisu ("joyadvaxisu", "0");
cvar_t	joy_advaxisv ("joyadvaxisv", "0");
cvar_t	joy_forwardthreshold ("joyforwardthreshold", "0.15");
cvar_t	joy_sidethreshold ("joysidethreshold", "0.15");
cvar_t	joy_pitchthreshold ("joypitchthreshold", "0.15");
cvar_t	joy_yawthreshold ("joyyawthreshold", "0.15");
cvar_t	joy_forwardsensitivity ("joyforwardsensitivity", "-1.0");
cvar_t	joy_sidesensitivity ("joysidesensitivity", "-1.0");
cvar_t	joy_pitchsensitivity ("joypitchsensitivity", "1.0");
cvar_t	joy_yawsensitivity ("joyyawsensitivity", "-1.0");
cvar_t	joy_wwhack1 ("joywwhack1", "0.0");
cvar_t	joy_wwhack2 ("joywwhack2", "0.0");

bool		joy_avail, joy_advancedinit, joy_haspov;
DWORD		joy_oldbuttonstate, joy_oldpovstate;

int			joy_id;
DWORD		joy_flags;
DWORD		joy_numbuttons;

static JOYINFOEX	ji;


/*
===============
IN_StartupJoystick
===============
*/
void IN_StartupJoystick (void)
{
	int			i, numdevs;
	JOYCAPS		jc;
	MMRESULT	mmr;

	// assume no joystick
	joy_avail = false;

	// abort startup if user requests no joystick
	if (COM_CheckParm ("-nojoy")) return;

	// verify joystick driver is present
	if ((numdevs = joyGetNumDevs ()) == 0)
	{
		Con_Printf ("\njoystick not found -- driver not present\n\n");
		return;
	}

	// cycle through the joystick ids for the first valid one
	for (joy_id = 0; joy_id < numdevs; joy_id++)
	{
		memset (&ji, 0, sizeof (ji));

		ji.dwSize = sizeof (ji);
		ji.dwFlags = JOY_RETURNCENTERED;

		if ((mmr = joyGetPosEx (joy_id, &ji)) == JOYERR_NOERROR)
			break;
	}

	// abort startup if we didn't find a valid joystick
	if (mmr != JOYERR_NOERROR)
	{
		Con_Printf ("\njoystick not found -- no valid joysticks (%x)\n\n", mmr);
		return;
	}

	// get the capabilities of the selected joystick
	// abort startup if command fails
	memset (&jc, 0, sizeof (jc));

	if ((mmr = joyGetDevCaps (joy_id, &jc, sizeof (jc))) != JOYERR_NOERROR)
	{
		Con_Printf ("\njoystick not found -- invalid joystick capabilities (%x)\n\n", mmr);
		return;
	}

	// save the joystick's number of buttons and POV status
	joy_numbuttons = jc.wNumButtons;
	joy_haspov = jc.wCaps & JOYCAPS_HASPOV;

	// old button and POV states default to no buttons pressed
	joy_oldbuttonstate = joy_oldpovstate = 0;

	// mark the joystick as available and advanced initialization not completed
	// this is needed as cvars are not available during initialization
	joy_avail = true;
	joy_advancedinit = false;
	Con_Printf ("\njoystick detected\n\n");
}


/*
===========
RawValuePointer
===========
*/
PDWORD RawValuePointer (int axis)
{
	switch (axis)
	{
	case JOY_AXIS_X: return &ji.dwXpos;
	case JOY_AXIS_Y: return &ji.dwYpos;
	case JOY_AXIS_Z: return &ji.dwZpos;
	case JOY_AXIS_R: return &ji.dwRpos;
	case JOY_AXIS_U: return &ji.dwUpos;
	case JOY_AXIS_V: return &ji.dwVpos;
	default: return 0;
	}
}


/*
===========
Joy_AdvancedUpdate_f
===========
*/
void Joy_AdvancedUpdate_f (void)
{
	// called once by IN_ReadJoystick and by user whenever an update is needed
	// cvars are now available
	int	i;
	DWORD dwTemp;

	// initialize all the maps
	for (i = 0; i < JOY_MAX_AXES; i++)
	{
		dwAxisMap[i] = AxisNada;
		dwControlMap[i] = JOY_ABSOLUTE_AXIS;
		pdwRawValue[i] = RawValuePointer (i);
	}

	if (joy_advanced.value == 0.0)
	{
		// default joystick initialization
		// 2 axes only with joystick control
		dwAxisMap[JOY_AXIS_X] = AxisTurn;
		// dwControlMap[JOY_AXIS_X] = JOY_ABSOLUTE_AXIS;

		dwAxisMap[JOY_AXIS_Y] = AxisForward;
		// dwControlMap[JOY_AXIS_Y] = JOY_ABSOLUTE_AXIS;
	}
	else
	{
		if (strcmp (joy_name.string, "joystick") != 0)
		{
			// notify user of advanced controller
			Con_Printf ("\n%s configured\n\n", joy_name.string);
		}

		// advanced initialization here
		// data supplied by user via joy_axisn cvars
		dwTemp = (DWORD) joy_advaxisx.value;
		dwAxisMap[JOY_AXIS_X] = dwTemp & 0x0000000f;
		dwControlMap[JOY_AXIS_X] = dwTemp & JOY_RELATIVE_AXIS;

		dwTemp = (DWORD) joy_advaxisy.value;
		dwAxisMap[JOY_AXIS_Y] = dwTemp & 0x0000000f;
		dwControlMap[JOY_AXIS_Y] = dwTemp & JOY_RELATIVE_AXIS;

		dwTemp = (DWORD) joy_advaxisz.value;
		dwAxisMap[JOY_AXIS_Z] = dwTemp & 0x0000000f;
		dwControlMap[JOY_AXIS_Z] = dwTemp & JOY_RELATIVE_AXIS;

		dwTemp = (DWORD) joy_advaxisr.value;
		dwAxisMap[JOY_AXIS_R] = dwTemp & 0x0000000f;
		dwControlMap[JOY_AXIS_R] = dwTemp & JOY_RELATIVE_AXIS;

		dwTemp = (DWORD) joy_advaxisu.value;
		dwAxisMap[JOY_AXIS_U] = dwTemp & 0x0000000f;
		dwControlMap[JOY_AXIS_U] = dwTemp & JOY_RELATIVE_AXIS;

		dwTemp = (DWORD) joy_advaxisv.value;
		dwAxisMap[JOY_AXIS_V] = dwTemp & 0x0000000f;
		dwControlMap[JOY_AXIS_V] = dwTemp & JOY_RELATIVE_AXIS;
	}

	// compute the axes to collect from DirectInput
	joy_flags = JOY_RETURNCENTERED | JOY_RETURNBUTTONS | JOY_RETURNPOV;

	for (i = 0; i < JOY_MAX_AXES; i++)
	{
		if (dwAxisMap[i] != AxisNada)
		{
			joy_flags |= dwAxisFlags[i];
		}
	}
}


/*
===========
IN_Commands
===========
*/
void IN_Commands (void)
{
	DWORD buttonstate, povstate;

	if (!joy_avail)
		return;

	// loop through the joystick buttons
	// key a joystick event or auxillary event for higher number buttons for each state change
	buttonstate = ji.dwButtons;

	for (int i = 0; i < joy_numbuttons; i++)
	{
		if ((buttonstate & (1 << i)) && !(joy_oldbuttonstate & (1 << i))) Key_Event (K_JOY1 + i, true);
		if (!(buttonstate & (1 << i)) && (joy_oldbuttonstate & (1 << i))) Key_Event (K_JOY1 + i, false);
	}

	joy_oldbuttonstate = buttonstate;

	if (joy_haspov)
	{
		// convert POV information into 4 bits of state information
		// this avoids any potential problems related to moving from one
		// direction to another without going through the center position
		povstate = 0;

		if (ji.dwPOV != JOY_POVCENTERED)
		{
			if (ji.dwPOV == JOY_POVFORWARD) povstate |= 0x01;
			if (ji.dwPOV == JOY_POVRIGHT) povstate |= 0x02;
			if (ji.dwPOV == JOY_POVBACKWARD) povstate |= 0x04;
			if (ji.dwPOV == JOY_POVLEFT) povstate |= 0x08;
		}

		// determine which bits have changed and key an auxillary event for each change
		for (int i = 0; i < 4; i++)
		{
			if ((povstate & (1 << i)) && !(joy_oldpovstate & (1 << i))) Key_Event (K_POV1 + i, true);
			if (!(povstate & (1 << i)) && (joy_oldpovstate & (1 << i))) Key_Event (K_POV1 + i, false);
		}

		joy_oldpovstate = povstate;
	}
}


/*
===============
IN_ReadJoystick
===============
*/
bool IN_ReadJoystick (void)
{
	memset (&ji, 0, sizeof (ji));

	ji.dwSize = sizeof (ji);
	ji.dwFlags = joy_flags;

	if (joyGetPosEx (joy_id, &ji) == JOYERR_NOERROR)
	{
		// this is a hack -- there is a bug in the Logitech WingMan Warrior DirectInput Driver
		// rather than having 32768 be the zero point, they have the zero point at 32668
		// go figure -- anyway, now we get the full resolution out of the device
		if (joy_wwhack1.value != 0.0)
			ji.dwUpos += 100;

		return true;
	}
	else
	{
		// read error occurred
		// turning off the joystick seems too harsh for 1 read error,\
		// but what should be done?
		// Con_Printf ("IN_ReadJoystick: no response\n");
		// joy_avail = false;
		return false;
	}
}


/*
===========
IN_JoyMove
===========
*/
void IN_JoyMove (usercmd_t *cmd, double movetime)
{
	float	speed, aspeed;
	float	fAxisValue, fTemp;
	int		i;

	// complete initialization if first time in
	// this is needed as cvars are not available at initialization time
	if (joy_advancedinit != true)
	{
		Joy_AdvancedUpdate_f ();
		joy_advancedinit = true;
	}

	// verify joystick is available and that the user wants to use it
	if (!joy_avail || !in_joystick.value)
		return;

	// collect the joystick data, if possible
	if (IN_ReadJoystick () != true)
		return;

	if (in_speed.state & 1)
		speed = cl_movespeedkey.value;
	else speed = 1;

	aspeed = speed * movetime;
	mouselooking = freelook.integer || (in_mlook.state & 1);

	usercmd_t basecmd;
	CL_ClearCmd (&basecmd);

	// loop through the axes
	for (i = 0; i < JOY_MAX_AXES; i++)
	{
		// get the floating point zero-centered, potentially-inverted data for the current axis
		fAxisValue = (float) * pdwRawValue[i];

		// move centerpoint to zero
		fAxisValue -= 32768.0;

		if (joy_wwhack2.value != 0.0)
		{
			if (dwAxisMap[i] == AxisTurn)
			{
				// this is a special formula for the Logitech WingMan Warrior
				// y=ax^b; where a = 300 and b = 1.3
				// also x values are in increments of 800 (so this is factored out)
				// then bounds check result to level out excessively high spin rates
				fTemp = 300.0 * pow (abs (fAxisValue) / 800.0, 1.3);

				if (fTemp > 14000.0)
					fTemp = 14000.0;

				// restore direction information
				fAxisValue = (fAxisValue > 0.0) ? fTemp : -fTemp;
			}
		}

		// convert range from -32768..32767 to -1..1
		fAxisValue /= 32768.0;

		switch (dwAxisMap[i])
		{
		case AxisForward:
			if ((joy_advanced.value == 0.0) && mouselooking)
			{
				// user wants forward control to become look control
				if (fabs (fAxisValue) > joy_pitchthreshold.value)
				{
					// if mouse invert is on, invert the joystick pitch value
					// only absolute control support here (joy_advanced is false)
					if (m_pitch.value < 0.0)
						cl.viewangles[PITCH] -= (fAxisValue * joy_pitchsensitivity.value) * aspeed * cl_pitchspeed.value;
					else cl.viewangles[PITCH] += (fAxisValue * joy_pitchsensitivity.value) * aspeed * cl_pitchspeed.value;
				}
			}
			else
			{
				// user wants forward control to be forward control
				if (fabs (fAxisValue) > joy_forwardthreshold.value)
					basecmd.forwardmove += (fAxisValue * joy_forwardsensitivity.value) * speed * cl_forwardspeed.value;
			}

			break;

		case AxisSide:
			if (fabs (fAxisValue) > joy_sidethreshold.value)
				basecmd.sidemove += (fAxisValue * joy_sidesensitivity.value) * speed * cl_sidespeed.value;

			break;

		case AxisTurn:
			if ((in_strafe.state & 1) || (lookstrafe.value && mouselooking))
			{
				// user wants turn control to become side control
				if (fabs (fAxisValue) > joy_sidethreshold.value)
					basecmd.sidemove -= (fAxisValue * joy_sidesensitivity.value) * speed * cl_sidespeed.value;
			}
			else
			{
				// user wants turn control to be turn control
				if (fabs (fAxisValue) > joy_yawthreshold.value)
				{
					if (dwControlMap[i] == JOY_ABSOLUTE_AXIS)
						cl.viewangles[YAW] += (fAxisValue * joy_yawsensitivity.value) * aspeed * cl_yawspeed.value;
					else cl.viewangles[YAW] += (fAxisValue * joy_yawsensitivity.value) * speed * 180.0;
				}
			}

			break;

		case AxisLook:
			if (mouselooking)
			{
				if (fabs (fAxisValue) > joy_pitchthreshold.value)
				{
					// pitch movement detected and pitch movement desired by user
					if (dwControlMap[i] == JOY_ABSOLUTE_AXIS)
						cl.viewangles[PITCH] += (fAxisValue * joy_pitchsensitivity.value) * aspeed * cl_pitchspeed.value;
					else cl.viewangles[PITCH] += (fAxisValue * joy_pitchsensitivity.value) * speed * 180.0;
				}
			}

			break;

		default:
			break;
		}
	}

	// rebalance the move to 72 FPS
	CL_RebalanceMove (cmd, &basecmd, movetime);

	// bounds check pitch
	CL_BoundViewPitch (cl.viewangles);
}
