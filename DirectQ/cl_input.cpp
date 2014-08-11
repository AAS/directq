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
// cl.input.c  -- builds an intended movement command to send to the server

// Quake is a trademark of Id Software, Inc., (c) 1996 Id Software, Inc. All
// rights reserved.

#include "quakedef.h"

/*
===============================================================================

KEY BUTTONS

Continuous button event tracking is complicated by the fact that two different
input sources (say, mouse button 1 and the control key) can both press the
same button, but the button should only be released when both of the
pressing key have been released.

When a key event issues a button command (+forward, +attack, etc), it appends
its key number as a parameter to the command so it can be matched up with
the release.

state bit 0 is the current state of the key
state bit 1 is edge triggered on the up to down transition
state bit 2 is edge triggered on the down to up transition

===============================================================================
*/

cvar_t pq_moveup ("pq_moveup", 0.0f, CVAR_ARCHIVE);

kbutton_t	in_mlook, in_klook;
kbutton_t	in_left, in_right, in_forward, in_back;
kbutton_t	in_lookup, in_lookdown, in_moveleft, in_moveright;
kbutton_t	in_strafe, in_speed, in_use, in_jump, in_attack;
kbutton_t	in_up, in_down;

// no impulse
int	in_impulse = 0;
int in_lastimpulse[2] = {0, 0};


void KeyDown (kbutton_t *b)
{
	int		k;
	char	*c;

	c = Cmd_Argv (1);

	if (c[0])
		k = atoi (c);
	else
		k = -1;		// typed manually at the console for continuous down

	if (b == &in_jump && pq_moveup.value && cl.stats[STAT_HEALTH] > 0 && cl.inwater)
		b = &in_up;

	if (k == b->down[0] || k == b->down[1])
		return;		// repeating key

	if (!b->down[0])
		b->down[0] = k;
	else if (!b->down[1])
		b->down[1] = k;
	else
	{
		Con_Printf ("Three keys down for a button!\n");
		return;
	}

	if (b->state & 1)
		return;		// still down

	b->state |= 1 + 2;	// down + impulse down
}

void KeyUp (kbutton_t *b)
{
	int		k;
	char	*c;

	c = Cmd_Argv (1);

	if (c[0])
		k = atoi (c);
	else
	{
		// typed manually at the console, assume for unsticking, so clear all
		b->down[0] = b->down[1] = 0;
		b->state = 4;	// impulse up
		return;
	}

	// JPG 1.05 - check to see if we need to translate -jump to -moveup
	if (b == &in_jump && pq_moveup.value)
	{
		if (k == in_up.down[0] || k == in_up.down[1])
			b = &in_up;
		else
		{
			// in case a -moveup got lost somewhere
			in_up.down[0] = in_up.down[1] = 0;
			in_up.state = 4;
		}
	}

	if (b->down[0] == k)
		b->down[0] = 0;
	else if (b->down[1] == k)
		b->down[1] = 0;
	else
		return;		// key up without coresponding down (menu pass through)

	if (b->down[0] || b->down[1])
		return;		// some other key is still holding it down

	if (!(b->state & 1))
		return;		// still up (this should not happen)

	b->state &= ~1;		// now up
	b->state |= 4; 		// impulse up
}

void IN_KLookDown (void) {KeyDown (&in_klook);}
void IN_KLookUp (void) {KeyUp (&in_klook);}
void IN_MLookDown (void) {KeyDown (&in_mlook);}
void IN_MLookUp (void)
{
	KeyUp (&in_mlook);
	extern cvar_t freelook;

	if (!((in_mlook.state & 1) || freelook.integer) && lookspring.value)
		CL_StartPitchDrift();
}
void IN_UpDown (void) {KeyDown (&in_up);}
void IN_UpUp (void) {KeyUp (&in_up);}
void IN_DownDown (void) {KeyDown (&in_down);}
void IN_DownUp (void) {KeyUp (&in_down);}
void IN_LeftDown (void) {KeyDown (&in_left);}
void IN_LeftUp (void) {KeyUp (&in_left);}
void IN_RightDown (void) {KeyDown (&in_right);}
void IN_RightUp (void) {KeyUp (&in_right);}
void IN_ForwardDown (void) {KeyDown (&in_forward);}
void IN_ForwardUp (void) {KeyUp (&in_forward);}
void IN_BackDown (void) {KeyDown (&in_back);}
void IN_BackUp (void) {KeyUp (&in_back);}
void IN_LookupDown (void) {KeyDown (&in_lookup);}
void IN_LookupUp (void) {KeyUp (&in_lookup);}
void IN_LookdownDown (void) {KeyDown (&in_lookdown);}
void IN_LookdownUp (void) {KeyUp (&in_lookdown);}
void IN_MoveleftDown (void) {KeyDown (&in_moveleft);}
void IN_MoveleftUp (void) {KeyUp (&in_moveleft);}
void IN_MoverightDown (void) {KeyDown (&in_moveright);}
void IN_MoverightUp (void) {KeyUp (&in_moveright);}

void IN_SpeedDown (void) {KeyDown (&in_speed);}
void IN_SpeedUp (void) {KeyUp (&in_speed);}
void IN_StrafeDown (void) {KeyDown (&in_strafe);}
void IN_StrafeUp (void) {KeyUp (&in_strafe);}

void IN_AttackDown (void) {KeyDown (&in_attack);}
void IN_AttackUp (void) {KeyUp (&in_attack);}

void IN_UseDown (void) {KeyDown (&in_use);}
void IN_UseUp (void) {KeyUp (&in_use);}
void IN_JumpDown (void) {KeyDown (&in_jump);}
void IN_JumpUp (void) {KeyUp (&in_jump);}
void IN_Impulse (void) {in_impulse = atoi (Cmd_Argv (1));}

// i'm a bit leery of this as it doesn't take mods/etc into account...
int weaponstat[] = {STAT_SHELLS, STAT_SHELLS, STAT_NAILS, STAT_NAILS, STAT_ROCKETS, STAT_ROCKETS, STAT_CELLS};

bool IsWeaponImpulse (int impulse)
{
	if (impulse > 0 && impulse < 9 && (impulse == 1 || ((cl.items & (IT_SHOTGUN << (impulse - 2))) && cl.stats[weaponstat[impulse - 2]])))
		return true;

	return false;
}


void CL_BestWeapon_f (void)
{
	int argc = Cmd_Argc ();

	for (int i = 1; i < argc; i++)
	{
		int impulse = atoi (Cmd_Argv (i));

		if (IsWeaponImpulse (impulse))
		{
			in_impulse = impulse;
			break;
		}
	}
}


void CL_PlusQuickGrenade_f (void) {Cbuf_AddText ("-attack;wait;impulse 6;wait;+attack\n");}
cmd_t CL_PlusQuickGrenade_Cmd ("+quickgrenade", CL_PlusQuickGrenade_f);

void CL_MinusQuickGrenade_f (void) {Cbuf_AddText ("-attack;wait;bestweapon 7 8 5 3 4 2 1\n");}
cmd_t CL_MinusQuickGrenade_Cmd ("-quickgrenade", CL_MinusQuickGrenade_f);

void CL_PlusQuickRocket_f (void) {Cbuf_AddText ("-attack;wait;impulse 7;wait;+attack\n");}
cmd_t CL_PlusQuickRocket_Cmd ("+quickrocket", CL_PlusQuickRocket_f);

void CL_MinusQuickRocket_f (void) {Cbuf_AddText ("-attack;wait;bestweapon 8 5 3 4 2 7 1\n");}
cmd_t CL_MinusQuickRocket_Cmd ("-quickrocket", CL_MinusQuickRocket_f);

void CL_PlusQuickShaft_f (void) {Cbuf_AddText ("-attack;wait;impulse 8;wait;+attack\n");}
cmd_t CL_PlusQuickShaft_Cmd ("+quickShaft", CL_PlusQuickShaft_f);

void CL_MinusQuickShaft_f (void) {Cbuf_AddText ("-attack;wait;bestweapon 7 5 8 3 4 2 1\n");}
cmd_t CL_MinusQuickShaft_Cmd ("-quickShaft", CL_MinusQuickShaft_f);

void CL_PlusQuickShot_f (void) {Cbuf_AddText ("-attack;wait;impulse 2;wait;+attack\n");}
cmd_t CL_PlusQuickShot_Cmd ("+quickShot", CL_PlusQuickShot_f);

void CL_MinusQuickShot_f (void) {Cbuf_AddText ("-attack;wait;bestweapon 7 8 5 3 4 2 1\n");}
cmd_t CL_MinusQuickShot_Cmd ("-quickShot", CL_MinusQuickShot_f);

void CL_BestSafe_f (void) {Cbuf_AddText ("bestweapon 8 5 3 4 2 1\n");}
cmd_t CL_BestSafe_Cmd ("bestsafe", CL_BestSafe_f);

cmd_t CL_BestWeapon_Cmd ("bestweapon", CL_BestWeapon_f);

void CL_LastWeapon_f (void)
{
	// ensure that the last impulse was a weapon
	if (IsWeaponImpulse (in_lastimpulse[1]))
	{
		in_impulse = in_lastimpulse[1];
	}
}


cmd_t CL_LastWeapon_Cmd ("lastweapon", CL_LastWeapon_f);

/*
===============
CL_KeyState

Returns 0.25 if a key was pressed and released during the frame,
0.5 if it was pressed and held
0 if held then released, and
1.0 if held for the entire time
===============
*/
float CL_KeyState (kbutton_t *key)
{
	float		val;
	bool	impulsedown, impulseup, down;

	impulsedown = key->state & 2;
	impulseup = key->state & 4;
	down = key->state & 1;
	val = 0;

	if (impulsedown && !impulseup)
	{
		if (down)
			val = 0.5;	// pressed and held this frame
		else val = 0;	//	I_Error ();
	}

	if (impulseup && !impulsedown)
	{
		if (down)
			val = 0;	//	I_Error ();
		else val = 0;	// released this frame
	}

	if (!impulsedown && !impulseup)
	{
		if (down)
			val = 1.0;	// held the entire frame
		else val = 0;	// up the entire frame
	}

	if (impulsedown && impulseup)
	{
		if (down)
			val = 0.75;	// released and re-pressed this frame
		else val = 0.25;	// pressed and released this frame
	}

	key->state &= 1;		// clear impulses

	return val;
}




//==========================================================================

cvar_t	cl_upspeed ("cl_upspeed", "200");
cvar_t	cl_forwardspeed ("cl_forwardspeed", "200", CVAR_ARCHIVE);
cvar_t	cl_backspeed ("cl_backspeed", "200", CVAR_ARCHIVE);
cvar_t	cl_sidespeed ("cl_sidespeed", "350");
cvar_t	cl_movespeedkey ("cl_movespeedkey", "2.0");

cvar_t	cl_yawspeed ("cl_yawspeed", "140");
cvar_t	cl_pitchspeed ("cl_pitchspeed", "150");

cvar_t	cl_anglespeedkey ("cl_anglespeedkey", "1.5");

cvar_t cl_fullpitch ("cl_fullpitch", "0", CVAR_ARCHIVE);

// ProQuake compatibility
cvar_alias_t pq_fullpitch ("pq_fullpitch", &cl_fullpitch);

// fitzquake compatibility
cvar_t cl_maxpitch ("cl_maxpitch", 80.0f, CVAR_ARCHIVE);
cvar_t cl_minpitch ("cl_minpitch", -70.0f, CVAR_ARCHIVE);

void CL_BoundViewPitch (float *viewangles)
{
	if (cl_maxpitch.value > 90.0f) Cvar_Set (&cl_maxpitch, 90.0f);
	if (cl_minpitch.value < -90.0f) Cvar_Set (&cl_minpitch, -90.0f);

	if (cl_fullpitch.integer)
	{
		if (viewangles[PITCH] > 90) viewangles[PITCH] = 90;
		if (viewangles[PITCH] < -90) viewangles[PITCH] = -90;
	}
	else
	{
		if (viewangles[PITCH] > cl_maxpitch.value) viewangles[PITCH] = cl_maxpitch.value;
		if (viewangles[PITCH] < cl_minpitch.value) viewangles[PITCH] = cl_minpitch.value;
	}
}


/*
================
CL_AdjustAngles

Moves the local angle positions
================
*/
CDQEventTimer *cl_AnglesTimer = NULL;

void CL_AdjustAngles (void)
{
	float	speed;
	float	up, down;
	extern cvar_t freelook;

	// based on realtime so that the timing is always correct; this will be filtered anyway
	// (fixme : what if the client pauses?  there may be a discrepancy...)  OK, cl.time it is then!
	// fixme - this happens before cl.time is updated for the current frame...
	if (!cl_AnglesTimer)
		cl_AnglesTimer = new CDQEventTimer (realtime);

	float frametime = cl_AnglesTimer->GetElapsedTime (realtime);

	if (in_speed.state & 1)
		speed = frametime * cl_anglespeedkey.value;
	else speed = frametime;

	if (!(in_strafe.state & 1))
	{
		cl.viewangles[YAW] -= speed * cl_yawspeed.value * CL_KeyState (&in_right);
		cl.viewangles[YAW] += speed * cl_yawspeed.value * CL_KeyState (&in_left);
		cl.viewangles[YAW] = anglemod (cl.viewangles[YAW]);
	}

	if (in_klook.state & 1)
	{
		CL_StopPitchDrift ();
		cl.viewangles[PITCH] -= speed * cl_pitchspeed.value * CL_KeyState (&in_forward);
		cl.viewangles[PITCH] += speed * cl_pitchspeed.value * CL_KeyState (&in_back);
	}

	if (freelook.integer || (in_mlook.state & 1))
		CL_StopPitchDrift ();

	if (in_lookup.state & 1) up = CL_KeyState (&in_lookup); else up = 0;
	if (in_lookdown.state & 1) down = CL_KeyState (&in_lookdown); else down = 0;

	if (up || down)
	{
		cl.viewangles[PITCH] -= speed * cl_pitchspeed.value * up;
		cl.viewangles[PITCH] += speed * cl_pitchspeed.value * down;
		CL_StopPitchDrift ();
	}

	CL_BoundViewPitch (cl.viewangles);

	if (cl.viewangles[ROLL] > 50) cl.viewangles[ROLL] = 50;
	if (cl.viewangles[ROLL] < -50) cl.viewangles[ROLL] = -50;
}


/*
=======================================================================================================================================

	PITCH DRIFTING

	This properly should run on the client to keep timings completely consistent with input

=======================================================================================================================================
*/

cvar_t	v_centermove ("v_centermove", "0.15");
cvar_t	v_centerspeed ("v_centerspeed", "500");


void CL_StartPitchDrift (void)
{
	if (cl.laststop == cl.time)
	{
		// something else is keeping it from drifting
		return;
	}

	if (cl.nodrift || !cl.pitchvel)
	{
		cl.pitchvel = v_centerspeed.value;
		cl.nodrift = false;
		cl.driftmove = 0;
	}
}

void CL_StopPitchDrift (void)
{
	cl.laststop = cl.time;
	cl.nodrift = true;
	cl.pitchvel = 0;
}

/*
===============
CL_DriftPitch

Moves the client pitch angle towards cl.idealpitch sent by the server.

If the user is adjusting pitch manually, either with lookup/lookdown,
mlook and mouse, or klook and keyboard, pitch drifting is constantly stopped.

Drifting is enabled when the center view key is hit, mlook is released and
lookspring is non 0, or when
===============
*/
void CL_DriftPitch (float time)
{
	float delta, move;
	extern cvar_t freelook;

	if (noclip_anglehack || !cl.onground || cls.demoplayback)
	{
		cl.driftmove = 0;
		cl.pitchvel = 0;
		return;
	}

	if (freelook.value || (in_mlook.state & 1))
	{
		// it seems more reliable to have this check here rather than having multiple calls to CL_StopPitchDrift scattered all over the engine code
		// (especially if different subsystems can be running at different framerates nowadays)
		cl.laststop = cl.time;
		cl.nodrift = true;
		cl.pitchvel = 0;
		return;
	}

	// don't count small mouse motion
	if (cl.nodrift)
	{
		if (fabs (cl.cmd.forwardmove) < cl_forwardspeed.value)
			cl.driftmove = 0;
		else cl.driftmove += time;

		if (cl.driftmove > v_centermove.value)
			CL_StartPitchDrift ();

		return;
	}

	delta = cl.idealpitch - cl.viewangles[PITCH];

	if (!delta)
	{
		cl.pitchvel = 0;
		return;
	}

	move = time * cl.pitchvel;
	cl.pitchvel += time * v_centerspeed.value;

	//Con_Printf ("move: %f (%f)\n", move, time);

	if (delta > 0)
	{
		if (move > delta)
		{
			cl.pitchvel = 0;
			move = delta;
		}

		cl.viewangles[PITCH] += move;
	}
	else if (delta < 0)
	{
		if (move > -delta)
		{
			cl.pitchvel = 0;
			move = -delta;
		}

		cl.viewangles[PITCH] -= move;
	}
}


/*
================
CL_BaseMove

Send the intended movement message to the server
================
*/

void CL_BaseMove (usercmd_t *cmd)
{
	if (cls.signon != SIGNONS)
		return;

	CL_AdjustAngles ();

	// fixme - adjust these speeds for frametime?
	if (in_strafe.state & 1)
	{
		cmd->sidemove += cl_sidespeed.value * CL_KeyState (&in_right);
		cmd->sidemove -= cl_sidespeed.value * CL_KeyState (&in_left);
	}

	cmd->sidemove += cl_sidespeed.value * CL_KeyState (&in_moveright);
	cmd->sidemove -= cl_sidespeed.value * CL_KeyState (&in_moveleft);

	cmd->upmove += cl_upspeed.value * CL_KeyState (&in_up);
	cmd->upmove -= cl_upspeed.value * CL_KeyState (&in_down);

	if (!(in_klook.state & 1))
	{
		cmd->forwardmove += cl_forwardspeed.value * CL_KeyState (&in_forward);
		cmd->forwardmove -= cl_backspeed.value * CL_KeyState (&in_back);
	}

	// adjust for speed key
	if (in_speed.state & 1)
	{
		cmd->forwardmove *= cl_movespeedkey.value;
		cmd->sidemove *= cl_movespeedkey.value;
		cmd->upmove *= cl_movespeedkey.value;
	}
}


// JPG - support for synthetic lag
sizebuf_t lag_buff[32];
byte lag_data[32][128];
unsigned int lag_head, lag_tail;
float lag_sendtime[32];

cvar_t	pq_lag ("pq_lag", "0");

void CL_SendLagMove (void)
{
	if (cls.demoplayback || cls.state != ca_connected || cls.signon != SIGNONS) return;

	while ((lag_tail < lag_head) && (lag_sendtime[lag_tail & 31] <= realtime))
	{
		lag_tail++;

		if (++cl.movemessages <= 2)
		{
			lag_head = lag_tail = 0;  // JPG - hack: if cl.movemessages has been reset, we should reset these too
			continue;	// return -> continue
		}

		if (NET_SendUnreliableMessage (cls.netcon, &lag_buff[(lag_tail - 1) & 31]) == -1)
		{
			Con_Printf ("CL_SendLagMove: lost server connection\n");
			CL_Disconnect ();
		}

		// Con_Printf ("sent %i\n", lag_buff[(lag_tail - 1) & 31].cursize);
	}
}


/*
==============
CL_SendMove
==============
*/
void CL_SendMove (usercmd_t *cmd)
{
	int		bits;
	sizebuf_t	*buf;

	if (pq_lag.value < 0) Cvar_Set (&pq_lag, 0.0f);
	if (pq_lag.value > 400) Cvar_Set (&pq_lag, 400.0f);

	buf = &lag_buff[lag_head & 31];
	SZ_Init (buf, lag_data[lag_head & 31], sizeof (lag_data[lag_head & 31]));
	lag_sendtime[(lag_head++) & 31] = realtime + (pq_lag.value / 1000.0f);

	memcpy (&cl.cmd, cmd, sizeof (usercmd_t));

	// send the movement message
	MSG_WriteByte (buf, clc_move);
	MSG_WriteFloat (buf, cl.mtime[0]);	// so server can get ping times

	if (!cls.demoplayback && (cls.netcon->mod == MOD_PROQUAKE) && cl.Protocol == PROTOCOL_VERSION_NQ)
	{
		MSG_WriteProQuakeAngle (buf, cl.viewangles[0]);
		MSG_WriteProQuakeAngle (buf, cl.viewangles[1]);
		MSG_WriteProQuakeAngle (buf, cl.viewangles[2]);
	}
	else if (cl.Protocol == PROTOCOL_VERSION_FITZ || cl.Protocol == PROTOCOL_VERSION_RMQ)
	{
		MSG_WriteAngle16 (buf, cl.viewangles[0], cl.Protocol, cl.PrototcolFlags);
		MSG_WriteAngle16 (buf, cl.viewangles[1], cl.Protocol, cl.PrototcolFlags);
		MSG_WriteAngle16 (buf, cl.viewangles[2], cl.Protocol, cl.PrototcolFlags);
	}
	else
	{
		// don't rough-round angle[1] here (fixme - the param name is no longer descriptive)
		MSG_WriteAngle (buf, cl.viewangles[0], cl.Protocol, cl.PrototcolFlags, 0);
		MSG_WriteAngle (buf, cl.viewangles[1], cl.Protocol, cl.PrototcolFlags, 0);
		MSG_WriteAngle (buf, cl.viewangles[2], cl.Protocol, cl.PrototcolFlags, 2);
	}

	MSG_WriteShort (buf, cmd->forwardmove);
	MSG_WriteShort (buf, cmd->sidemove);
	MSG_WriteShort (buf, cmd->upmove);

	// send button bits
	bits = 0;

	if (in_attack.state & 3) bits |= 1;
	if (in_jump.state & 3) bits |= 2;

	in_attack.state &= ~2;
	in_jump.state &= ~2;

	MSG_WriteByte (buf, bits);
	MSG_WriteByte (buf, in_impulse);

	if (IsWeaponImpulse (in_impulse))
	{
		in_lastimpulse[1] = in_lastimpulse[0];
		in_lastimpulse[0] = in_impulse;
	}

	// clear the impulse (if any)
	in_impulse = 0;

	// deliver the message (unless we're playing a demo in which case there is no server to deliver to)
	if (cls.demoplayback) return;

	// and deliver it
	CL_SendLagMove ();
}


/*
============
CL_InitInput
============
*/
cmd_t IN_UpDown_Cmd ("+moveup", IN_UpDown);
cmd_t IN_UpUp_Cmd ("-moveup", IN_UpUp);
cmd_t IN_DownDown_Cmd ("+movedown", IN_DownDown);
cmd_t IN_DownUp_Cmd ("-movedown", IN_DownUp);
cmd_t IN_LeftDown_Cmd ("+left", IN_LeftDown);
cmd_t IN_LeftUp_Cmd ("-left", IN_LeftUp);
cmd_t IN_RightDown_Cmd ("+right", IN_RightDown);
cmd_t IN_RightUp_Cmd ("-right", IN_RightUp);
cmd_t IN_ForwardDown_Cmd ("+forward", IN_ForwardDown);
cmd_t IN_ForwardUp_Cmd ("-forward", IN_ForwardUp);
cmd_t IN_BackDown_Cmd ("+back", IN_BackDown);
cmd_t IN_BackUp_Cmd ("-back", IN_BackUp);
cmd_t IN_LookupDown_Cmd ("+lookup", IN_LookupDown);
cmd_t IN_LookupUp_Cmd ("-lookup", IN_LookupUp);
cmd_t IN_LookdownDown_Cmd ("+lookdown", IN_LookdownDown);
cmd_t IN_LookdownUp_Cmd ("-lookdown", IN_LookdownUp);
cmd_t IN_StrafeDown_Cmd ("+strafe", IN_StrafeDown);
cmd_t IN_StrafeUp_Cmd ("-strafe", IN_StrafeUp);
cmd_t IN_MoveleftDown_Cmd ("+moveleft", IN_MoveleftDown);
cmd_t IN_MoveleftUp_Cmd ("-moveleft", IN_MoveleftUp);
cmd_t IN_MoverightDown_Cmd ("+moveright", IN_MoverightDown);
cmd_t IN_MoverightUp_Cmd ("-moveright", IN_MoverightUp);
cmd_t IN_SpeedDown_Cmd ("+speed", IN_SpeedDown);
cmd_t IN_SpeedUp_Cmd ("-speed", IN_SpeedUp);
cmd_t IN_AttackDown_Cmd ("+attack", IN_AttackDown);
cmd_t IN_AttackUp_Cmd ("-attack", IN_AttackUp);
cmd_t IN_UseDown_Cmd ("+use", IN_UseDown);
cmd_t IN_UseUp_Cmd ("-use", IN_UseUp);
cmd_t IN_JumpDown_Cmd ("+jump", IN_JumpDown);
cmd_t IN_JumpUp_Cmd ("-jump", IN_JumpUp);
cmd_t IN_Impulse_Cmd ("impulse", IN_Impulse);
cmd_t IN_KLookDown_Cmd ("+klook", IN_KLookDown);
cmd_t IN_KLookUp_Cmd ("-klook", IN_KLookUp);
cmd_t IN_MLookDown_Cmd ("+mlook", IN_MLookDown);
cmd_t IN_MLookUp_Cmd ("-mlook", IN_MLookUp);

void CL_InitInput (void)
{
}

