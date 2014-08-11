
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
#include <XInput.h>

#pragma comment (lib, "xinput.lib")

extern cvar_t freelook;
extern bool mouselooking;

void CL_BoundViewPitch (float *viewangles);


// xinput stuff
int xiActiveController = -1;
bool xiActive = false;
int xi_olddpadstate = 0;
int xi_oldbuttonstate = 0;

void IN_StartupXInput (void)
{
	XINPUT_CAPABILITIES xiCaps;

	// reset to -1 each time as this can be called at runtime
	xiActiveController = -1;

	if (xiActive)
	{
		XInputEnable (FALSE);
		xiActive = false;
	}

	// only support up to 4 controllers (in a PC scenario usually just one will be attached)
	for (int c = 0; c < 4; c++)
	{
		memset (&xiCaps, 0, sizeof (XINPUT_CAPABILITIES));
		DWORD gc = XInputGetCapabilities (c, XINPUT_FLAG_GAMEPAD, &xiCaps);

		if (gc == ERROR_SUCCESS)
		{
			// just use the first one
			Con_Printf ("Using XInput Device on Port %i\n", c);

			// store to global active controller
			xiActiveController = c;
			break;
		}
	}

	Con_Printf ("No XInput Devices Found\n");
	XInputEnable (FALSE);
	xiActive = false;
}


void IN_ToggleXInput (cvar_t *var)
{
	if (var->value && !xiActive)
	{
		IN_StartupXInput ();

		if (xiActiveController != -1)
		{
			XInputEnable (TRUE);
			xiActive = true;
		}

		xi_olddpadstate = xi_oldbuttonstate = 0;
	}
	else if (!var->value && xiActive)
	{
		XInputEnable (FALSE);
		xiActive = false;
		xi_olddpadstate = xi_oldbuttonstate = 0;
	}
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
cvar_t xi_usecontroller ("xi_usecontroller", "1", CVAR_ARCHIVE, IN_ToggleXInput);

void IN_ControllerAxisMove (usercmd_t *cmd, int axisval, int dz, int axismax, cvar_t *axisaction)
{
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
		cl.viewangles[0] += fmove * cl_pitchspeed.value / 20.0f;
		break;

	case XI_AXIS_MOVE:
	case XI_AXIS_INVMOVE:
		cmd->forwardmove += fmove * cl_forwardspeed.value;
		break;

	case XI_AXIS_TURN:
	case XI_AXIS_INVTURN:
		// slow this down because the default cl_yawspeed is too fast here
		// invert it so that positive move = right
		cl.viewangles[1] += fmove * cl_yawspeed.value / 20.0f * -1;
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

	XINPUT_STATE xiState;
	static DWORD xiLastPacket = 666;

	// get current state
	DWORD xiResult = XInputGetState (xiActiveController, &xiState);

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
	if (cl.viewangles[0] > 80.0) cl.viewangles[0] = 80.0;
	if (cl.viewangles[0] < -70.0) cl.viewangles[0] = -70.0;

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


