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
// in_win.c -- windows 95 mouse and joystick code
// 02/21/97 JCB Added extended DirectInput code to support external controllers.
// NOTE - the stock ID code is *NOT* DirectInput; it's Windows API.

#include "quakedef.h"
#include <dinput.h>
#include <XInput.h>
#include "winquake.h"

HINSTANCE hInstDInput = NULL;
typedef HRESULT (WINAPI *DIRECTINPUT8CREATEPROC) (HINSTANCE, DWORD, REFIID, LPVOID *, LPUNKNOWN);
DIRECTINPUT8CREATEPROC QDirectInput8Create = NULL;

// mouse format; this is microsoft's c_dfDIMouse2 format derived from a dump of the data to a text file
// and is used so that we don't need to statically link to dinput8.lib for 8 button support
DIOBJECTDATAFORMAT di_MouseFormatDesc[11] =
{
	{&GUID_XAxis, 0, 16776963, 0}, {&GUID_YAxis, 4, 16776963, 0}, {&GUID_ZAxis, 8, 2164260611, 0}, {NULL, 12, 16776972, 0}, {NULL, 13, 16776972, 0},
	{NULL, 14, 2164260620, 0}, {NULL, 15, 2164260620, 0}, {NULL, 16, 2164260620, 0}, {NULL, 17, 2164260620, 0}, {NULL, 18, 2164260620, 0}, {NULL, 19, 2164260620, 0}
};

DIDATAFORMAT di_MouseFormat = {sizeof (DIDATAFORMAT), sizeof (DIOBJECTDATAFORMAT), DIDF_RELAXIS, 20, 11, di_MouseFormatDesc};

void IN_ShutdownXInput (void);

typedef DWORD (WINAPI *XINPUTGETCAPABILITIESPROC) (DWORD, DWORD, XINPUT_CAPABILITIES *);
typedef DWORD (WINAPI *XINPUTGETSTATEPROC) (DWORD, XINPUT_STATE *);
typedef void (WINAPI *XINPUTENABLEPROC) (BOOL);

HINSTANCE hInstXInput = NULL;
XINPUTGETCAPABILITIESPROC QXInputGetCapabilities = NULL;
XINPUTGETSTATEPROC QXInputGetState = NULL;
XINPUTENABLEPROC QXInputEnable = NULL;

// xinput stuff
int xiActiveController = -1;
bool xiActive = false;
void IN_ControllerMove (usercmd_t *cmd);
int xi_olddpadstate = 0;
int xi_oldbuttonstate = 0;

static bool restore_spi;

// keep this consistent with dinput behaviour
static int originalmouseparms[3], newmouseparms[3] = {0, 0, 0};

// mouse variables
cvar_t m_filter ("m_filter", "0", CVAR_ARCHIVE);
cvar_t m_look ("freelook", "1", CVAR_ARCHIVE);
cvar_alias_t freelook ("m_look", &m_look);
cvar_t m_boost ("m_boost", "1", CVAR_ARCHIVE);
cvar_t m_directinput ("m_directinput", "1", CVAR_ARCHIVE);

// consistency with stock Win/GL Quake
cvar_t m_accellevel ("m_accellevel", "0", CVAR_ARCHIVE);
cvar_t m_accelthreshold1 ("m_accelthreshold1", "0", CVAR_ARCHIVE);
cvar_t m_accelthreshold2 ("m_accelthreshold2", "0", CVAR_ARCHIVE);

int			mouse_oldbuttonstate;
POINT		current_pos;
int			mouse_x, mouse_y, old_mouse_x, old_mouse_y, mx_accum, my_accum;

unsigned int uiWheelMessage;
bool	mouseactive;
bool		mouseinitialized;
static bool	mouseparmsvalid;
static bool	mouseshowtoggle = 1;
static bool	dinput_acquired;

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


static unsigned int		mstate_di = 0;

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
cvar_t	in_joystick ("joystick","0", CVAR_ARCHIVE);
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

bool	joy_avail, joy_advancedinit, joy_haspov;
DWORD		joy_oldbuttonstate, joy_oldpovstate;

int			joy_id;
DWORD		joy_flags;
DWORD		joy_numbuttons;

static LPDIRECTINPUT		di_Object;
static LPDIRECTINPUTDEVICE	di_Mouse;

static JOYINFOEX	ji;

// forward-referenced functions
void IN_StartupJoystick (void);
void IN_StartupXInput (void);
void Joy_AdvancedUpdate_f (void);
void IN_JoyMove (usercmd_t *cmd);


void CL_BoundViewPitch (void);


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
	if (mouseinitialized && mouseactive && !m_directinput.integer)
	{
		RECT cliprect;
		GetWindowRect (d3d_Window, &cliprect);
		ClipCursor (&cliprect);
	}
}


void IN_ShowMouse (BOOL mshow)
{
	static BOOL mouseshowtoggle = TRUE;

	if (mshow)
	{
		if (!mouseshowtoggle)
		{
			ShowCursor (TRUE);
			mouseshowtoggle = 1;
		}
	}
	else
	{
		if (mouseshowtoggle)
		{
			ShowCursor (FALSE);
			mouseshowtoggle = FALSE;
		}
	}
}


void IN_FlushDInput (void)
{
	if (di_Mouse)
	{
		// only necessary if it's acquired as otherwise it's not buffering
		if (dinput_acquired)
		{
			DWORD dwItems = INFINITE;
			di_Mouse->GetDeviceData (sizeof (DIDEVICEOBJECTDATA), NULL, &dwItems, 0);
		}

		// switch off input state
		mouse_oldbuttonstate = mstate_di;
		mstate_di = 0;

		// send key up events for all mouse buttons that were down
		IN_MouseEvent (mstate_di, 8, true);
	}
}


/*
===========
IN_ActivateMouse
===========
*/
void IN_ActivateMouse (void)
{
	if (xiActiveController >= 0)
	{
		// toggle xinput on
		xiActive = true;
		QXInputEnable (TRUE);
	}

	if (mouseinitialized)
	{
		// not so certain about this... i think the update each frame should cover it OK if dinput failed to come up
		if (m_directinput.integer)
		{
			if (di_Mouse)
			{
				if (!dinput_acquired)
				{
					di_Mouse->Acquire ();
					dinput_acquired = true;
					IN_FlushDInput ();
				}
			}
			else
			{
				return;
			}
		}
		else
		{
			if (mouseparmsvalid)
				restore_spi = SystemParametersInfo (SPI_SETMOUSE, 0, newmouseparms, 0);

			RECT cliprect;

			GetWindowRect (d3d_Window, &cliprect);
			SetCursorPos (cliprect.left + (cliprect.right - cliprect.left) / 2, cliprect.top + (cliprect.bottom - cliprect.top) / 2);
			SetCapture (d3d_Window);
			ClipCursor (&cliprect);
		}

		mouseactive = true;
	}
}


/*
===========
IN_DeactivateMouse
===========
*/
void IN_DeactivateMouse (void)
{
	if (xiActiveController >= 0)
	{
		// toggle xinput off
		xiActive = false;
		QXInputEnable (FALSE);
	}

	if (mouseinitialized)
	{
		if (m_directinput.integer)
		{
			if (di_Mouse)
			{
				if (dinput_acquired)
				{
					IN_FlushDInput ();
					di_Mouse->Unacquire ();
					dinput_acquired = false;
				}
			}
		}
		else
		{
			if (restore_spi)
				SystemParametersInfo (SPI_SETMOUSE, 0, originalmouseparms, 0);

			ClipCursor (NULL);
			ReleaseCapture ();
		}

		mouseactive = false;
	}
}


/*
===========
IN_InitDInput
===========
*/
void IN_InitDInputMouse (void)
{
	// clear to NULL
	SAFE_RELEASE (di_Mouse);

	if (COM_CheckParm ("-nodinput")) return;

	// obtain an interface to the system mouse device.
	hr = di_Object->CreateDevice (GUID_SysMouse, &di_Mouse, NULL);

	if (FAILED (hr))
	{
		SAFE_RELEASE (di_Mouse);
		Con_SafePrintf ("Couldn't open DI mouse device\n");
		return;
	}

	// set the data format to "mouse format".
	hr = di_Mouse->SetDataFormat (&di_MouseFormat);

	if (FAILED (hr))
	{
		SAFE_RELEASE (di_Mouse);
		Con_SafePrintf ("Couldn't set DI mouse format\n");
		return;
	}

	// we don't know how big a buffer we can create so keep trying until we get to 0
	for (int i = 1024; i; i >>= 1)
	{
		if (!i)
		{
			SAFE_RELEASE (di_Mouse);
			Con_SafePrintf ("Couldn't set DI mouse buffer\n");
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
	hr = di_Mouse->SetCooperativeLevel (d3d_Window, DISCL_EXCLUSIVE | DISCL_FOREGROUND);

	if (FAILED (hr))
	{
		SAFE_RELEASE (di_Mouse);
		Con_SafePrintf ("Couldn't set DI coop level\n");
	}

	// done
	Con_SafePrintf ("DirectInput mouse initialized\n");
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

	// do this whether or not we have directinput up as we will want to set from a Windows mouse control panel
	// setting to what Quake expects, and also give the opportunity for cvars to override
	mouseparmsvalid = SystemParametersInfo (SPI_GETMOUSE, 0, originalmouseparms, 0);

	if (mouseparmsvalid)
	{
		// set acceleration cvars
		Cvar_Set (&m_accelthreshold1, originalmouseparms[0]);
		Cvar_Set (&m_accelthreshold2, originalmouseparms[1]);
	}

	hInstDInput = LoadLibrary ("dinput8.dll");

	if (hInstDInput)
	{
		QDirectInput8Create = (DIRECTINPUT8CREATEPROC) GetProcAddress (hInstDInput, "DirectInput8Create");

		if (QDirectInput8Create)
		{
			// always create the direct input interface
			hr = QDirectInput8Create (GetModuleHandle (NULL), DIRECTINPUT_VERSION, IID_IDirectInput8, (LPVOID *) &di_Object, NULL);

			if (SUCCEEDED (hr) && di_Object)
			{
				// now create any and all directinput devices we use
				IN_InitDInputMouse ();
			}
		}
	}

	// if we didn't create any directinput devices we release the interface
	if (!di_Mouse || !di_Object)
	{
		SAFE_RELEASE (di_Mouse);
		SAFE_RELEASE (di_Object);
	}

	// now deactivate the mouse so that nothing is buffered while loading
	IN_DeactivateMouse ();
}


/*
===========
IN_Init
===========
*/
cmd_t Force_CenterView_f_Cmd ("force_centerview", Force_CenterView_f);
cmd_t Joy_AdvancedUpdate_f_Cmd ("joyadvancedupdate", Joy_AdvancedUpdate_f);

void IN_Init (void)
{
	uiWheelMessage = RegisterWindowMessage ("MSWHEEL_ROLLMSG");

	// IN_StartupMouse needs to be last so that it can also deactivate XInput
	IN_StartupXInput ();
	IN_StartupJoystick ();
	IN_StartupMouse ();

	// make the display all sweet 'n' pretty
	Con_Printf ("\n");
}


/*
===========
IN_Shutdown
===========
*/
void IN_Shutdown (void)
{
	IN_DeactivateMouse ();
	IN_ShowMouse (TRUE);

	SAFE_RELEASE (di_Mouse);
	SAFE_RELEASE (di_Object);
	UNLOAD_LIBRARY (hInstDInput);

	hInstDInput = NULL;
	QDirectInput8Create = NULL;

	IN_ShutdownXInput ();
}


/*
===========
IN_MouseEvent
===========
*/
void IN_MouseEvent (int mstate, int numbuttons, bool dinput)
{
	if (mouseactive && !m_directinput.integer || dinput)
	{
		// perform button actions
		for (int i = 0; i < numbuttons; i++)
		{
			if ((mstate & (1 << i)) && !(mouse_oldbuttonstate & (1 << i))) Key_Event (K_MOUSE1 + i, true);
			if (!(mstate & (1 << i)) && (mouse_oldbuttonstate & (1 << i))) Key_Event (K_MOUSE1 + i, false);
		}

		mouse_oldbuttonstate = mstate;
	}
}


/*
===========
IN_MouseMove
===========
*/
void IN_MouseMove (usercmd_t *cmd)
{
	int i;
	int mx = 0;
	int my = 0;
	extern bool keybind_grab;

	RECT cliprect;

	GetWindowRect (d3d_Window, &cliprect);

	if (!mouseactive && !keybind_grab) return;

	// ack, it's horrible but we don't want to change the code too much for this version.  we'll do it right in 1.8.4
	if (key_dest == key_game) IN_ShowMouse (FALSE);

	// ensure that we have a device and it's actually acquired!!!
	if (m_directinput.integer && di_Mouse && dinput_acquired)
	{
		if ((cls.demoplayback || cls.timedemo || key_dest != key_game) && !keybind_grab)
		{
			// in a demo, the menu or the console we just flush the buffers and reset state always
			IN_FlushDInput ();
			return;
		}

		// read one element at a time
		DIDEVICEOBJECTDATA di_MouseBuffer;
		int NumMouseEvents;

		for (NumMouseEvents = 0;; NumMouseEvents++)
		{
			DWORD dwElements = 1;

			hr = di_Mouse->GetDeviceData (sizeof (DIDEVICEOBJECTDATA), &di_MouseBuffer, &dwElements, 0);

			if ((hr == DIERR_INPUTLOST) || (hr == DIERR_NOTACQUIRED))
			{
				// if we lose the device or if it's not acquired, we just acquire it for the next frame
				// (the sdk cautions against reacquiring after DIERR_NOTACQUIRED but those circumstances don't apply here)
				dinput_acquired = true;
				di_Mouse->Acquire ();
				IN_FlushDInput ();

				// if we lost the device we try to process any events we did get
				// flushing above will cause us to lose button events but at least we'll get the movement
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
				for (i = 0; i < NUM_DI_MBUTTONS; i++)
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
		}

		// if we didn't read any events we don't bother updating the command or processing any keys
		if (!NumMouseEvents)
		{
			// this is needed to prevent freelook from recentering
			if ((in_mlook.state & 1) || m_look.value) V_StopPitchDrift ();

			// done
			return;
		}

		// adjust movement by boost factor
		// deferred to here so that calcs on DWORDs (above) won't send it all screwy
		mx *= m_boost.value;
		my *= m_boost.value;

		// do up to 8 buttons; most folks won't have this many (and Windows won't support this many without
		// 3rd party drivers) but it does no harm...
		IN_MouseEvent (mstate_di, 8, true);
	}
	else
	{
		// also flush if acquired
		if (di_Mouse && dinput_acquired)
		{
			IN_FlushDInput ();

			// unacquire so this doesn't happen every frame
			di_Mouse->Unacquire ();
			dinput_acquired = false;
		}

		GetCursorPos (&current_pos);

		// these were right - left and bottom - top causing all sort of havoc when di was disabled or failed to start!!!
		mx = current_pos.x - (cliprect.left + (cliprect.right - cliprect.left) / 2) + mx_accum;
		my = current_pos.y - (cliprect.top + (cliprect.bottom - cliprect.top) / 2) + my_accum;

		mx_accum = 0;
		my_accum = 0;
	}

	// don't update if not in-game (possible cheat potential)
	if (key_dest == key_game)
	{
		// add acceleration parameters - per mouse_event MSDN description
		if (m_accellevel.integer > 0)
		{
			int oldmx = mx;
			int oldmy = my;

			// threshold 1 doubles when movement is > it
			if (oldmx > m_accelthreshold1.integer) mx *= 2;
			if (oldmx < -m_accelthreshold1.integer) mx *= 2;
			if (oldmy > m_accelthreshold1.integer) my *= 2;
			if (oldmy < -m_accelthreshold1.integer) my *= 2;

			if (m_accellevel.integer > 1)
			{
				// threshold 2 doubles again when movement is > it
				if (oldmx > m_accelthreshold2.integer) mx *= 2;
				if (oldmx < -m_accelthreshold2.integer) mx *= 2;
				if (oldmy > m_accelthreshold2.integer) my *= 2;
				if (oldmy < -m_accelthreshold2.integer) my *= 2;
			}
		}

		if (m_filter.value)
		{
			mouse_x = (mx + old_mouse_x) * 0.5;
			mouse_y = (my + old_mouse_y) * 0.5;
		}
		else
		{
			mouse_x = mx;
			mouse_y = my;
		}

		old_mouse_x = mx;
		old_mouse_y = my;

		mouse_x *= sensitivity.value;
		mouse_y *= sensitivity.value;

		// add mouse X/Y movement to cmd
		if ((in_strafe.state & 1) || (lookstrafe.value && ((in_mlook.state & 1) || m_look.value)))
			cmd->sidemove += m_side.value * mouse_x;
		else cl.viewangles[YAW] -= m_yaw.value * mouse_x;

		// don't drift pitch back if freelooking
		if ((in_mlook.state & 1) || m_look.value) V_StopPitchDrift ();

		// allow cvar controlled free-looking as well as by using +mlook
		if (((in_mlook.state & 1) || m_look.value) && !(in_strafe.state & 1))
		{
			cl.viewangles[PITCH] += m_pitch.value * mouse_y;
			CL_BoundViewPitch ();
		}
		else
		{
			if ((in_strafe.state & 1) && noclip_anglehack)
				cmd->upmove -= m_forward.value * mouse_y;
			else cmd->forwardmove -= m_forward.value * mouse_y;
		}
	}

	// if the mouse has moved, force it to the center, so there's room to move
	if (mx || my) SetCursorPos (cliprect.left + (cliprect.right - cliprect.left) / 2, cliprect.top + (cliprect.bottom - cliprect.top) / 2);
}


void IN_ToggleDirectInput (void)
{
	// don't display if in game (for a switch on initial startup)
	if (key_dest != key_game)
		Con_Printf ("Switching DirectInput %s\n", m_directinput.integer ? "On" : "Off");

	// ensure that dinput came up OK
	if (m_directinput.integer && di_Mouse)
	{
		// deactivate software
		m_directinput.integer = 0;
		IN_DeactivateMouse ();

		// activate directinput
		m_directinput.integer = 1;
		IN_ActivateMouse ();
	}
	else
	{
		// deactivate directinput
		m_directinput.integer = 1;
		IN_DeactivateMouse ();

		// activate software
		m_directinput.integer = 0;
		IN_ActivateMouse ();
	}
}


/*
===========
IN_Move
===========
*/
void IN_Move (usercmd_t *cmd)
{
	static int old_m_directinput = -1; //m_directinput.value;

	// if for some wacky reason direct input failed to initialize we force it to off always
	if (!di_Mouse) m_directinput.integer = 0;

	// called every frame so seems as good a place as any...
	// ensure that it's really toggled...
	if ((m_directinput.integer && !old_m_directinput) || (!m_directinput.integer && old_m_directinput))
	{
		// toggle
		IN_ToggleDirectInput ();

		// keep consistent for toggle
		Cvar_Set (&m_directinput, !!m_directinput.integer);

		// store back
		old_m_directinput = m_directinput.integer;
	}

	if (ActiveApp && !Minimized)
	{
		IN_MouseMove (cmd);
		IN_ControllerMove (cmd);
		IN_JoyMove (cmd);
	}
}


/*
===========
IN_Accumulate
===========
*/
void IN_Accumulate (void)
{
	if (mouseactive)
	{
		if (!m_directinput.integer)
		{
			RECT cliprect;

			GetCursorPos (&current_pos);
			GetWindowRect (d3d_Window, &cliprect);

			mx_accum += current_pos.x - (cliprect.left + (cliprect.right - cliprect.left) / 2);
			my_accum += current_pos.y - (cliprect.top + (cliprect.bottom - cliprect.top) / 2);

			// force the mouse to the center, so there's room to move
			SetCursorPos (cliprect.left + (cliprect.right - cliprect.left) / 2, cliprect.top + (cliprect.bottom - cliprect.top) / 2);
		}
	}
}


/*
===================
IN_ClearStates
===================
*/
void IN_ClearStates (void)
{
	if (mouseactive)
	{
		mx_accum = 0;
		my_accum = 0;
		IN_FlushDInput ();
	}
}


/*
=============== 
IN_StartupJoystick 
=============== 
*/
void IN_StartupJoystick (void)
{ 
	int			numdevs;
	JOYCAPS		jc;
	MMRESULT	mmr;
 
 	// assume no joystick
	joy_avail = false; 

	// if xinput is active then we don't bother with the standard joystick API
	if (xiActiveController >= 0) return;

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

		// (just takes the first valid joystick it finds)
		if ((mmr = joyGetPosEx (joy_id, &ji)) == JOYERR_NOERROR) break;
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
	// (note - cvars are available now so we can handle this differently...)
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
	case JOY_AXIS_X:
		return &ji.dwXpos;
	case JOY_AXIS_Y:
		return &ji.dwYpos;
	case JOY_AXIS_Z:
		return &ji.dwZpos;
	case JOY_AXIS_R:
		return &ji.dwRpos;
	case JOY_AXIS_U:
		return &ji.dwUpos;
	case JOY_AXIS_V:
		return &ji.dwVpos;
	}

	// shut up compiler
	return 0;
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
		pdwRawValue[i] = RawValuePointer(i);
	}

	if (joy_advanced.value == 0.0)
	{
		// default joystick initialization
		// 2 axes only with joystick control
		dwAxisMap[JOY_AXIS_X] = AxisTurn;
		dwAxisMap[JOY_AXIS_Y] = AxisForward;
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
	int		i, key_index;
	DWORD	buttonstate, povstate;

	if (!joy_avail) return;

	// loop through the joystick buttons
	// key a joystick event or auxillary event for higher number buttons for each state change
	buttonstate = ji.dwButtons;

	for (i = 0; i < joy_numbuttons; i++)
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

		if(ji.dwPOV != JOY_POVCENTERED)
		{
			if (ji.dwPOV == JOY_POVFORWARD) povstate |= 0x01;
			if (ji.dwPOV == JOY_POVRIGHT) povstate |= 0x02;
			if (ji.dwPOV == JOY_POVBACKWARD) povstate |= 0x04;
			if (ji.dwPOV == JOY_POVLEFT) povstate |= 0x08;
		}

		// determine which bits have changed and key a POV event for each change
		for (i = 0; i < 4; i++)
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
		if (joy_wwhack1.value != 0.0) ji.dwUpos += 100;

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
void IN_JoyMove (usercmd_t *cmd)
{
	float	speed, aspeed;
	float	fAxisValue, fTemp;
	int		i;

	// complete initialization if first time in
	// this is needed as cvars are not available at initialization time
	if (joy_advancedinit != true)
	{
		Joy_AdvancedUpdate_f();
		joy_advancedinit = true;
	}

	// verify joystick is available and that the user wants to use it
	if (!joy_avail || !in_joystick.value) return; 
 
	// collect the joystick data, if possible
	if (IN_ReadJoystick () != true) return;

	if (in_speed.state & 1)
		speed = cl_movespeedkey.value;
	else speed = 1;

	aspeed = speed * host_frametime;

	// loop through the axes
	for (i = 0; i < JOY_MAX_AXES; i++)
	{
		// get the floating point zero-centered, potentially-inverted data for the current axis
		fAxisValue = (float) *pdwRawValue[i];

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

				if (fTemp > 14000.0) fTemp = 14000.0;

				// restore direction information
				fAxisValue = (fAxisValue > 0.0) ? fTemp : -fTemp;
			}
		}

		// convert range from -32768..32767 to -1..1 
		fAxisValue /= 32768.0;

		switch (dwAxisMap[i])
		{
		case AxisForward:
			if ((joy_advanced.value == 0.0) && (in_mlook.state & 1))
			{
				// user wants forward control to become look control
				if (fabs(fAxisValue) > joy_pitchthreshold.value)
				{		
					// if mouse invert is on, invert the joystick pitch value
					// only absolute control support here (joy_advanced is false)
					if (m_pitch.value < 0.0)
						cl.viewangles[PITCH] -= (fAxisValue * joy_pitchsensitivity.value) * aspeed * cl_pitchspeed.value;
					else cl.viewangles[PITCH] += (fAxisValue * joy_pitchsensitivity.value) * aspeed * cl_pitchspeed.value;

					V_StopPitchDrift();
				}
				else
				{
					// no pitch movement
					// disable pitch return-to-center unless requested by user
					// *** this code can be removed when the lookspring bug is fixed
					// *** the bug always has the lookspring feature on
					if (lookspring.value == 0.0) V_StopPitchDrift ();
				}
			}
			else
			{
				// user wants forward control to be forward control
				if (fabs(fAxisValue) > joy_forwardthreshold.value)
				{
					cmd->forwardmove += (fAxisValue * joy_forwardsensitivity.value) * speed * cl_forwardspeed.value;
				}
			}

			break;

		case AxisSide:
			if (fabs(fAxisValue) > joy_sidethreshold.value)
				cmd->sidemove += (fAxisValue * joy_sidesensitivity.value) * speed * cl_sidespeed.value;

			break;

		case AxisTurn:
			if ((in_strafe.state & 1) || (lookstrafe.value && (in_mlook.state & 1)))
			{
				// user wants turn control to become side control
				if (fabs(fAxisValue) > joy_sidethreshold.value)
				{
					cmd->sidemove -= (fAxisValue * joy_sidesensitivity.value) * speed * cl_sidespeed.value;
				}
			}
			else
			{
				// user wants turn control to be turn control
				if (fabs(fAxisValue) > joy_yawthreshold.value)
				{
					if(dwControlMap[i] == JOY_ABSOLUTE_AXIS)
					{
						cl.viewangles[YAW] += (fAxisValue * joy_yawsensitivity.value) * aspeed * cl_yawspeed.value;
					}
					else
					{
						cl.viewangles[YAW] += (fAxisValue * joy_yawsensitivity.value) * speed * 180.0;
					}
				}
			}
			break;

		case AxisLook:
			if (in_mlook.state & 1)
			{
				if (fabs(fAxisValue) > joy_pitchthreshold.value)
				{
					// pitch movement detected and pitch movement desired by user
					if(dwControlMap[i] == JOY_ABSOLUTE_AXIS)
					{
						cl.viewangles[PITCH] += (fAxisValue * joy_pitchsensitivity.value) * aspeed * cl_pitchspeed.value;
					}
					else
					{
						cl.viewangles[PITCH] += (fAxisValue * joy_pitchsensitivity.value) * speed * 180.0;
					}
					V_StopPitchDrift();
				}
				else
				{
					// no pitch movement
					// disable pitch return-to-center unless requested by user
					// *** this code can be removed when the lookspring bug is fixed
					// *** the bug always has the lookspring feature on
					if (lookspring.value == 0.0) V_StopPitchDrift();
				}
			}

			break;

		default:
			// do nothing
			break;
		}
	}

	// bounds check pitch
	CL_BoundViewPitch ();
}


void IN_ShutdownXInput (void)
{
	UNLOAD_LIBRARY (hInstXInput);

	hInstXInput = NULL;
	QXInputEnable = NULL;
	QXInputGetState = NULL;
	QXInputGetCapabilities = NULL;
}


// dummy proc to handle an absent XInputEnable
void WINAPI QXInputEnableNULL (BOOL blah) {}


void IN_StartupXInput (void)
{
	// explicitly null library inst and pointers in case this gets called more than once
	IN_ShutdownXInput ();

	// attempt to load the best xinput DLL we can find
	for (int i = 9; i >= 0; i--)
	{
		hInstXInput = LoadLibrary (va ("xinput1_%i.dll", i));

		if (hInstXInput) break;
	}

	if (!hInstXInput) goto no_xinput;

	QXInputEnable = (XINPUTENABLEPROC) GetProcAddress (hInstXInput, "XInputEnable");
	QXInputGetState = (XINPUTGETSTATEPROC) GetProcAddress (hInstXInput, "XInputGetState");
	QXInputGetCapabilities = (XINPUTGETCAPABILITIESPROC) GetProcAddress (hInstXInput, "XInputGetCapabilities");

	// this entrypoint is not present in all versions
	if (!QXInputEnable) QXInputEnable = QXInputEnableNULL;

	if (!QXInputGetCapabilities || !QXInputGetState)
		goto no_xinput;

	XINPUT_CAPABILITIES xiCaps;

	// reset to -1 each time as this can be called at runtime
	xiActiveController = -1;
	xiActive = false;
	QXInputEnable (FALSE);

	// only support up to 4 controllers (in a PC scenario usually just one will be attached)
	for (int c = 0; c < 4; c++)
	{
		memset (&xiCaps, 0, sizeof (XINPUT_CAPABILITIES));
		DWORD gc = QXInputGetCapabilities (c, XINPUT_FLAG_GAMEPAD, &xiCaps);

		if (gc == ERROR_SUCCESS)
		{
			// just use the first one
			Con_Printf ("Using XInput Device on Port %i\n", c);

			// store to global active controller
			xiActiveController = c;
			return;
		}
	}

no_xinput:;
	IN_ShutdownXInput ();
	Con_Printf ("No XInput Devices Found\n");
}


// set cvars to these values to decode the action for a given axis
// if these are changed the menu strings in menu_other.cpp should also be changed!!!
#define XI_AXIS_NONE		0
#define XI_AXIS_LOOK		1
#define XI_AXIS_MOVE		2
#define XI_AXIS_TURN		3
#define XI_AXIS_STRAFE		4
#define XI_AXIS_INVLOOK		5
#define XI_AXIS_INVMOVE		6
#define XI_AXIS_INVTURN		7
#define XI_AXIS_INVSTRAFE	8

cvar_t xi_axislx ("xi_axislx", "3", CVAR_ARCHIVE);
cvar_t xi_axisly ("xi_axisly", "2", CVAR_ARCHIVE);
cvar_t xi_axisrx ("xi_axisrx", "3", CVAR_ARCHIVE);
cvar_t xi_axisry ("xi_axisry", "1", CVAR_ARCHIVE);
cvar_t xi_axislt ("xi_axislt", "8", CVAR_ARCHIVE);
cvar_t xi_axisrt ("xi_axisrt", "4", CVAR_ARCHIVE);
cvar_t xi_dpadarrowmap ("xi_dpadarrowmap", "1", CVAR_ARCHIVE);
cvar_t xi_usecontroller ("xi_usecontroller", "1", CVAR_ARCHIVE);

void IN_ControllerAxisMove (usercmd_t *cmd, int axisval, int dz, int axismax, cvar_t *axisaction)
{
	if (!hInstXInput) return;
	if (!QXInputGetState) return;
	if (!QXInputGetCapabilities) return;

	// not using this axis
	if (axisaction->integer <= XI_AXIS_NONE) return;

	// unimplemented
	if (axisaction->integer > XI_AXIS_INVSTRAFE) return;

	// get the amount moved less the deadzone
	int realmove = abs (axisval) - dz;

	// move is within deadzone threshold
	if (realmove < dz) return;

	// 0 to 1 scale
	float fmove = (float) realmove / (axismax - dz);

	// square it to get better scale at small moves
	fmove *= fmove;

	// go back to negative
	if (axisval < 0) fmove *= -1;

	// check for inverse scale
	if (axisaction->integer > XI_AXIS_STRAFE) fmove *= -1;

	// decode the move
	switch (axisaction->integer)
	{
	case XI_AXIS_LOOK:
	case XI_AXIS_INVLOOK:
		// inverted by default (positive = look down)
		cl.viewangles[PITCH] += fmove * cl_pitchspeed.value / 20.0f;
		break;

	case XI_AXIS_MOVE:
	case XI_AXIS_INVMOVE:
		cmd->forwardmove += fmove * cl_forwardspeed.value;
		break;

	case XI_AXIS_TURN:
	case XI_AXIS_INVTURN:
		// slow this down because the default cl_yawspeed is too fast here
		// invert it so that positive move = right
		cl.viewangles[YAW] += fmove * cl_yawspeed.value / 20.0f * -1;
		break;

	case XI_AXIS_STRAFE:
	case XI_AXIS_INVSTRAFE:
		cmd->sidemove += fmove * cl_sidespeed.value;
		break;

	default:
		// unimplemented
		break;
	}
}


void IN_ControllerMove (usercmd_t *cmd)
{
	// no controller to use
	if (!xiActive) return;
	if (xiActiveController < 0) return;
	if (!xi_usecontroller.integer) return;

	if (!hInstXInput) return;
	if (!QXInputGetState) return;
	if (!QXInputGetCapabilities) return;

	XINPUT_STATE xiState;
	static DWORD xiLastPacket = 666;

	// get current state
	DWORD xiResult = QXInputGetState (xiActiveController, &xiState);

	if (xiResult != ERROR_SUCCESS)
	{
		// something went wrong - we'll handle that properly later...
		return;
	}

	// check the axes (always, even if state doesn't change)
	IN_ControllerAxisMove (cmd, xiState.Gamepad.sThumbLX, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, 32768, &xi_axislx);
	IN_ControllerAxisMove (cmd, xiState.Gamepad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, 32768, &xi_axisly);
	IN_ControllerAxisMove (cmd, xiState.Gamepad.sThumbRX, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE, 32768, &xi_axisrx);
	IN_ControllerAxisMove (cmd, xiState.Gamepad.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE, 32768, &xi_axisry);
	IN_ControllerAxisMove (cmd, xiState.Gamepad.bLeftTrigger, 0, 255, &xi_axislt);
	IN_ControllerAxisMove (cmd, xiState.Gamepad.bRightTrigger, 0, 255, &xi_axisrt);

	// fix up the command (bound/etc)
	CL_BoundViewPitch ();

	// check for a change of state
	if (xiLastPacket == xiState.dwPacketNumber) return;

	// store back last packet
	xiLastPacket = xiState.dwPacketNumber;

	int buttonstate = 0;
	int dpadstate = 0;

	if (xi_dpadarrowmap.integer)
	{
		// check dpad (same order as arrow keys)
		if (xiState.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP) dpadstate |= 1;
		if (xiState.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) dpadstate |= 2;
		if (xiState.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) dpadstate |= 4;
		if (xiState.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) dpadstate |= 8;
	}
	else
	{
		// check dpad (same order as joystick pov hats)
		if (xiState.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP) dpadstate |= 1;
		if (xiState.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) dpadstate |= 2;
		if (xiState.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) dpadstate |= 4;
		if (xiState.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) dpadstate |= 8;
	}

	// check for event changes
	for (int i = 0; i < 4; i++)
	{
		if (xi_dpadarrowmap.integer)
		{
			// map dpad to arrow keys
			if ((dpadstate & (1 << i)) && !(xi_olddpadstate & (1 << i))) Key_Event (K_UPARROW + i, true);
			if (!(dpadstate & (1 << i)) && (xi_olddpadstate & (1 << i))) Key_Event (K_UPARROW + i, false);
		}
		else
		{
			// map dpad to POV keys
			if ((dpadstate & (1 << i)) && !(xi_olddpadstate & (1 << i))) Key_Event (K_POV1 + i, true);
			if (!(dpadstate & (1 << i)) && (xi_olddpadstate & (1 << i))) Key_Event (K_POV1 + i, false);
		}
	}

	// store back
	xi_olddpadstate = dpadstate;

	// check other buttons - map these to K_JOY buttons
	if (xiState.Gamepad.wButtons & XINPUT_GAMEPAD_START) buttonstate |= 1;
	if (xiState.Gamepad.wButtons & XINPUT_GAMEPAD_BACK) buttonstate |= 2;
	if (xiState.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_THUMB) buttonstate |= 4;
	if (xiState.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB) buttonstate |= 8;
	if (xiState.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) buttonstate |= 16;
	if (xiState.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) buttonstate |= 32;
	if (xiState.Gamepad.wButtons & XINPUT_GAMEPAD_A) buttonstate |= 64;
	if (xiState.Gamepad.wButtons & XINPUT_GAMEPAD_B) buttonstate |= 128;
	if (xiState.Gamepad.wButtons & XINPUT_GAMEPAD_X) buttonstate |= 256;
	if (xiState.Gamepad.wButtons & XINPUT_GAMEPAD_Y) buttonstate |= 512;

	// check for event changes
	for (int i = 0; i < 10; i++)
	{
		if ((buttonstate & (1 << i)) && !(xi_oldbuttonstate & (1 << i))) Key_Event (K_JOY1 + i, true);
		if (!(buttonstate & (1 << i)) && (xi_oldbuttonstate & (1 << i))) Key_Event (K_JOY1 + i, false);
	}

	// store back
	xi_oldbuttonstate = buttonstate;
}

