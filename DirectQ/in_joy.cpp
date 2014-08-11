
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


extern cvar_t freelook;
extern bool mouselooking;

void CL_BoundViewPitch (float *viewangles);
void CL_ClearCmd (usercmd_t *cmd);
void CL_RebalanceMove (usercmd_t *basecmd, usercmd_t *newcmd, double frametime);

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
	int			numdevs;
	JOYCAPS		jc;
	MMRESULT	mmr = JOYERR_NOERROR;

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
	joy_haspov = (jc.wCaps & JOYCAPS_HASPOV) ? true : false;

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

	CL_RebalanceMove (cmd, &basecmd, movetime);

	// bounds check pitch
	CL_BoundViewPitch (cl.viewangles);
}
