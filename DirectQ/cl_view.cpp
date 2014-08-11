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
// view.c -- player eye positioning
// renamed to cl_view to keep it grouped with the rest of the client code

#include "quakedef.h"


/*

The view is allowed to move slightly from it's true position for bobbing,
but if it exceeds 8 pixels linear distance (spherical, not box), the list of
entities sent from the server may not include everything in the pvs, especially
when crossing a water boudnary.

*/

float v_time = 0;
float v_frametime = 0;

cvar_t	scr_ofsx ("scr_ofsx", "0");
cvar_t	scr_ofsy ("scr_ofsy", "0");
cvar_t	scr_ofsz ("scr_ofsz", "0");

cvar_t	cl_rollspeed ("cl_rollspeed", "200");
cvar_t	cl_rollangle ("cl_rollangle", "2.0");

cvar_t	cl_bob ("cl_bob", "0.02");
cvar_t	cl_bobcycle ("cl_bobcycle", "0.6");
cvar_t	cl_bobup ("cl_bobup", "0.5");

cvar_t	v_kicktime ("v_kicktime", "0.5", CVAR_ARCHIVE);
cvar_t	v_kickroll ("v_kickroll", "0.6", CVAR_ARCHIVE);
cvar_t	v_kickpitch ("v_kickpitch", "0.6", CVAR_ARCHIVE);
cvar_t  v_gunkick ("v_gunkick", "1", CVAR_ARCHIVE);

cvar_t	v_iyaw_cycle ("v_iyaw_cycle", "2");
cvar_t	v_iroll_cycle ("v_iroll_cycle", "0.5");
cvar_t	v_ipitch_cycle ("v_ipitch_cycle", "1");
cvar_t	v_iyaw_level ("v_iyaw_level", "0.3");
cvar_t	v_iroll_level ("v_iroll_level", "0.1");
cvar_t	v_ipitch_level ("v_ipitch_level", "0.3");

cvar_t	v_idlescale ("v_idlescale", "0");

cvar_t	crosshair ("crosshair", "3", CVAR_ARCHIVE);
cvar_t	cl_crossx ("cl_crossx", "0", CVAR_ARCHIVE);
cvar_t	cl_crossy ("cl_crossy", "0", CVAR_ARCHIVE);
cvar_t	scr_crosshairscale ("scr_crosshairscale", 1, CVAR_ARCHIVE);
cvar_t	scr_crosshaircolor ("scr_crosshaircolor", "0", CVAR_ARCHIVE);
cvar_alias_t	crosshaircolor ("crosshaircolor", &scr_crosshaircolor);			
cvar_alias_t	crosshairsize ("crosshairsize", &scr_crosshairscale);

cvar_t	gl_cshiftpercent ("gl_cshiftpercent", "100");

cvar_t	v_gunangle ("v_gunangle", 2, CVAR_ARCHIVE);
cvar_alias_t r_gunangle ("r_gunangle", &v_gunangle);

float	v_dmg_time, v_dmg_roll, v_dmg_pitch;

extern	int			in_forward, in_forward2, in_back;


/*
===============
V_CalcRoll

Used by view and sv_user
no time dependencies
===============
*/
vec3_t	forward, right, up;

float V_CalcRoll (vec3_t angles, vec3_t velocity)
{
	float	sign;
	float	side;
	float	value;

	AngleVectors (angles, forward, right, up);
	side = DotProduct (velocity, right);
	sign = side < 0 ? -1 : 1;
	side = fabs (side);

	value = cl_rollangle.value;

	if (side < cl_rollspeed.value)
		side = side * value / cl_rollspeed.value;
	else side = value;

	return side * sign;
}


/*
===============
V_CalcBob

===============
*/
float V_CalcBob (float time)
{
	// prevent division by 0 weirdness
	if (!cl_bobcycle.value) return 0;
	if (!cl_bobup.value) return 0;

	// bound bob up
	if (cl_bobup.value >= 0.99f) Cvar_Set (&cl_bobup, 0.99f);

	float	bob;
	float	cycle;

	cycle = time - (int) (time / cl_bobcycle.value) * cl_bobcycle.value;
	cycle /= cl_bobcycle.value;

	if (cycle < cl_bobup.value)
		cycle = D3DX_PI * cycle / cl_bobup.value;
	else cycle = D3DX_PI + D3DX_PI * (cycle - cl_bobup.value) / (1.0 - cl_bobup.value);

	// bob is proportional to velocity in the xy plane
	// (don't count Z, or jumping messes it up)
	bob = sqrt (cl.velocity[0] * cl.velocity[0] + cl.velocity[1] * cl.velocity[1]) * cl_bob.value;
	bob = bob * 0.3 + bob * 0.7 * sin (cycle);

	if (bob > 4)
		bob = 4;
	else if (bob < -7)
		bob = -7;

	return bob;
}


//=============================================================================


cvar_t	v_centermove ("v_centermove", "0.15");
cvar_t	v_centerspeed ("v_centerspeed", "500");


void V_StartPitchDrift (void)
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

void V_StopPitchDrift (void)
{
	cl.laststop = cl.time;
	cl.nodrift = true;
	cl.pitchvel = 0;
}

/*
===============
V_DriftPitch

Moves the client pitch angle towards cl.idealpitch sent by the server.

If the user is adjusting pitch manually, either with lookup/lookdown,
mlook and mouse, or klook and keyboard, pitch drifting is constantly stopped.

Drifting is enabled when the center view key is hit, mlook is released and
lookspring is non 0, or when
===============
*/
void V_DriftPitch (void)
{
	float		delta, move;

	if (noclip_anglehack || !cl.onground || cls.demoplayback)
	{
		cl.driftmove = 0;
		cl.pitchvel = 0;
		return;
	}

	// don't count small mouse motion
	if (cl.nodrift)
	{
		if (fabs (cl.cmd.forwardmove) < cl_forwardspeed.value)
			cl.driftmove = 0;
		else
			cl.driftmove += v_frametime;

		if (cl.driftmove > v_centermove.value)
		{
			V_StartPitchDrift ();
		}

		return;
	}

	delta = cl.idealpitch - cl.viewangles[PITCH];

	if (!delta)
	{
		cl.pitchvel = 0;
		return;
	}

	move = v_frametime * cl.pitchvel;
	cl.pitchvel += v_frametime * v_centerspeed.value;

	//Con_Printf ("move: %f (%f)\n", move, v_frametime);

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
==============================================================================

						PALETTE FLASHES

==============================================================================
*/


// default cshifts are too dark in GL so lighten them a little
cshift_t	cshift_empty = {{130, 80, 50}, 0};
cshift_t	cshift_water = {{130, 80, 50}, 64};
cshift_t	cshift_slime = {{0, 25, 5}, 96};
cshift_t	cshift_lava = {{255, 80, 0}, 128};

int			v_blend[4];

cvar_t cl_damagered ("cl_damagered", "3", CVAR_ARCHIVE);

/*
===============
V_ParseDamage
===============
*/
void V_ParseDamage (float time)
{
	int		armor, blood;
	vec3_t	from;
	int		i;
	vec3_t	forward, right, up;
	entity_t	*ent;
	float	side;
	float	count;

	armor = MSG_ReadByte ();
	blood = MSG_ReadByte ();

	for (i = 0; i < 3; i++)
		from[i] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);

	count = blood * 0.5 + armor * 0.5;

	if (count < 10) count = 10;

	cl.faceanimtime = time + 0.2f;
	cl.cshifts[CSHIFT_DAMAGE].percent += cl_damagered.value * count;

	if (cl.cshifts[CSHIFT_DAMAGE].percent < 0) cl.cshifts[CSHIFT_DAMAGE].percent = 0;
	if (cl.cshifts[CSHIFT_DAMAGE].percent > 150) cl.cshifts[CSHIFT_DAMAGE].percent = 150;

	if (armor > blood)
	{
		cl.cshifts[CSHIFT_DAMAGE].destcolor[0] = 200;
		cl.cshifts[CSHIFT_DAMAGE].destcolor[1] = 100;
		cl.cshifts[CSHIFT_DAMAGE].destcolor[2] = 100;
	}
	else if (armor)
	{
		cl.cshifts[CSHIFT_DAMAGE].destcolor[0] = 220;
		cl.cshifts[CSHIFT_DAMAGE].destcolor[1] = 50;
		cl.cshifts[CSHIFT_DAMAGE].destcolor[2] = 50;
	}
	else
	{
		cl.cshifts[CSHIFT_DAMAGE].destcolor[0] = 255;
		cl.cshifts[CSHIFT_DAMAGE].destcolor[1] = 0;
		cl.cshifts[CSHIFT_DAMAGE].destcolor[2] = 0;
	}

	// calculate view angle kicks
	ent = cl_entities[cl.viewentity];

	VectorSubtract (from, ent->origin, from);
	VectorNormalize (from);

	AngleVectors (ent->angles, forward, right, up);

	side = DotProduct (from, right);
	v_dmg_roll = count * side * v_kickroll.value;

	side = DotProduct (from, forward);
	v_dmg_pitch = count * side * v_kickpitch.value;

	v_dmg_time = v_kicktime.value;
}


/*
==================
V_cshift_f
==================
*/
void V_cshift_f (void)
{
	// fixme - should this write to it's own cshift???
	cshift_empty.destcolor[0] = atoi (Cmd_Argv (1));
	cshift_empty.destcolor[1] = atoi (Cmd_Argv (2));
	cshift_empty.destcolor[2] = atoi (Cmd_Argv (3));
	cshift_empty.percent = atoi (Cmd_Argv (4));
}


/*
==================
V_BonusFlash_f

When you run over an item, the server sends this command
==================
*/
void V_BonusFlash_f (void)
{
	cl.cshifts[CSHIFT_BONUS].destcolor[0] = 215;
	cl.cshifts[CSHIFT_BONUS].destcolor[1] = 186;
	cl.cshifts[CSHIFT_BONUS].destcolor[2] = 69;
	cl.cshifts[CSHIFT_BONUS].percent = 50;
}

/*
=============
V_SetContentsColor

Underwater, lava, etc each has a color shift
=============
*/
void V_SetContentsColor (int contents)
{
	switch (contents)
	{
	case CONTENTS_EMPTY:
	case CONTENTS_SOLID:
		cl.cshifts[CSHIFT_CONTENTS] = cshift_empty;
		break;
	case CONTENTS_LAVA:
		cl.cshifts[CSHIFT_CONTENTS] = cshift_lava;
		break;
	case CONTENTS_SLIME:
		cl.cshifts[CSHIFT_CONTENTS] = cshift_slime;
		break;
	default:
		cl.cshifts[CSHIFT_CONTENTS] = cshift_water;
	}
}

/*
=============
V_CalcPowerupCshift
=============
*/
void V_CalcPowerupCshift (void)
{
	if (cl.items & IT_QUAD)
	{
		cl.cshifts[CSHIFT_POWERUP].destcolor[0] = 0;
		cl.cshifts[CSHIFT_POWERUP].destcolor[1] = 0;
		cl.cshifts[CSHIFT_POWERUP].destcolor[2] = 255;
		cl.cshifts[CSHIFT_POWERUP].percent = 30;
	}
	else if (cl.items & IT_SUIT)
	{
		cl.cshifts[CSHIFT_POWERUP].destcolor[0] = 0;
		cl.cshifts[CSHIFT_POWERUP].destcolor[1] = 255;
		cl.cshifts[CSHIFT_POWERUP].destcolor[2] = 0;
		cl.cshifts[CSHIFT_POWERUP].percent = 20;
	}
	else if (cl.items & IT_INVISIBILITY)
	{
		cl.cshifts[CSHIFT_POWERUP].destcolor[0] = 100;
		cl.cshifts[CSHIFT_POWERUP].destcolor[1] = 100;
		cl.cshifts[CSHIFT_POWERUP].destcolor[2] = 100;
		cl.cshifts[CSHIFT_POWERUP].percent = 100;
	}
	else if (cl.items & IT_INVULNERABILITY)
	{
		cl.cshifts[CSHIFT_POWERUP].destcolor[0] = 255;
		cl.cshifts[CSHIFT_POWERUP].destcolor[1] = 255;
		cl.cshifts[CSHIFT_POWERUP].destcolor[2] = 0;
		cl.cshifts[CSHIFT_POWERUP].percent = 30;
	}
	else
		cl.cshifts[CSHIFT_POWERUP].percent = 0;
}

/*
=============
V_CalcBlend
=============
*/
void V_CalcBlend (void)
{
	float	r, g, b, a, a2;
	int		j;

	r = 0;
	g = 0;
	b = 0;
	a = 0;

	for (j = 0; j < NUM_CSHIFTS; j++)
	{
		// no shift
		if (!cl.cshifts[j].percent) continue;

		// calc alpha amount
		a2 = (float) cl.cshifts[j].percent / 255.0;

		// evaluate blends
		a = a + a2 * (1 - a);
		a2 = a2 / a;

		// blend it in
		r = r * (1 - a2) + cl.cshifts[j].destcolor[0] * a2;
		g = g * (1 - a2) + cl.cshifts[j].destcolor[1] * a2;
		b = b * (1 - a2) + cl.cshifts[j].destcolor[2] * a2;
	}

	// set final amounts
	v_blend[0] = r;
	v_blend[1] = g;
	v_blend[2] = b;
	v_blend[3] = a * 255.0f;

	// clamp blend 0-255
	if (v_blend[0] > 255) v_blend[0] = 255; else if (v_blend[0] < 0) v_blend[0] = 0;
	if (v_blend[1] > 255) v_blend[1] = 255; else if (v_blend[1] < 0) v_blend[1] = 0;
	if (v_blend[2] > 255) v_blend[2] = 255; else if (v_blend[2] < 0) v_blend[2] = 0;
	if (v_blend[3] > 255) v_blend[3] = 255; else if (v_blend[3] < 0) v_blend[3] = 0;
}


/*
=============
V_UpdatePalette

a lot of this can go away with a non-sw renderer
=============
*/
void V_UpdatePalette (float frametime)
{
	int		i, j;
	bool	newp;
	byte	*basepal, *newpal;
	byte	pal[768];
	float	r, g, b, a;
	int		ir, ig, ib;
	bool force;

	v_frametime = frametime;
	V_CalcPowerupCshift ();

	newp = false;

	for (i = 0; i < NUM_CSHIFTS; i++)
	{
		if (cl.cshifts[i].percent != cl.prev_cshifts[i].percent)
		{
			newp = true;
			cl.prev_cshifts[i].percent = cl.cshifts[i].percent;
		}

		for (j = 0; j < 3; j++)
		{
			if (cl.cshifts[i].destcolor[j] != cl.prev_cshifts[i].destcolor[j])
			{
				newp = true;
				cl.prev_cshifts[i].destcolor[j] = cl.cshifts[i].destcolor[j];
			}
		}
	}

	// drop the damage value
	cl.cshifts[CSHIFT_DAMAGE].percent -= v_frametime * 150;

	if (cl.cshifts[CSHIFT_DAMAGE].percent <= 0)
		cl.cshifts[CSHIFT_DAMAGE].percent = 0;

	// drop the bonus value
	cl.cshifts[CSHIFT_BONUS].percent -= v_frametime * 100;

	if (cl.cshifts[CSHIFT_BONUS].percent <= 0)
		cl.cshifts[CSHIFT_BONUS].percent = 0;

	if (!newp) return;

	V_CalcBlend ();
}


/*
==============================================================================

						VIEW RENDERING

==============================================================================
*/

float angledelta (float a)
{
	a = anglemod (a);

	if (a > 180)
		a -= 360;

	return a;
}

/*
==================
CalcGunAngle
==================
*/
void CalcGunAngle (void)
{
	float	yaw, pitch, move;
	static float oldyaw = 0;
	static float oldpitch = 0;

	yaw = r_refdef.viewangles[YAW];
	pitch = -r_refdef.viewangles[PITCH];

	yaw = angledelta (yaw - r_refdef.viewangles[YAW]) * 0.4;

	if (yaw > 10)
		yaw = 10;

	if (yaw < -10)
		yaw = -10;

	pitch = angledelta (-pitch - r_refdef.viewangles[PITCH]) * 0.4;

	if (pitch > 10)
		pitch = 10;

	if (pitch < -10)
		pitch = -10;

	move = v_frametime * 20;

	if (yaw > oldyaw)
	{
		if (oldyaw + move < yaw)
			yaw = oldyaw + move;
	}
	else
	{
		if (oldyaw - move > yaw)
			yaw = oldyaw - move;
	}

	if (pitch > oldpitch)
	{
		if (oldpitch + move < pitch)
			pitch = oldpitch + move;
	}
	else
	{
		if (oldpitch - move > pitch)
			pitch = oldpitch - move;
	}

	oldyaw = yaw;
	oldpitch = pitch;

	cl.viewent.angles[YAW] = r_refdef.viewangles[YAW] + yaw;
	cl.viewent.angles[PITCH] = - (r_refdef.viewangles[PITCH] + pitch);

	cl.viewent.angles[ROLL] -= v_idlescale.value * sin (v_time * v_iroll_cycle.value) * v_iroll_level.value;
	cl.viewent.angles[PITCH] -= v_idlescale.value * sin (v_time * v_ipitch_cycle.value) * v_ipitch_level.value;
	cl.viewent.angles[YAW] -= v_idlescale.value * sin (v_time * v_iyaw_cycle.value) * v_iyaw_level.value;
}

/*
==============
V_BoundOffsets
==============
*/
void V_BoundOffsets (void)
{
	entity_t	*ent;

	ent = cl_entities[cl.viewentity];

	// absolutely bound refresh reletive to entity clipping hull
	// so the view can never be inside a solid wall
	if (r_refdef.vieworg[0] < ent->origin[0] - 14)
		r_refdef.vieworg[0] = ent->origin[0] - 14;
	else if (r_refdef.vieworg[0] > ent->origin[0] + 14)
		r_refdef.vieworg[0] = ent->origin[0] + 14;

	if (r_refdef.vieworg[1] < ent->origin[1] - 14)
		r_refdef.vieworg[1] = ent->origin[1] - 14;
	else if (r_refdef.vieworg[1] > ent->origin[1] + 14)
		r_refdef.vieworg[1] = ent->origin[1] + 14;

	if (r_refdef.vieworg[2] < ent->origin[2] - 22)
		r_refdef.vieworg[2] = ent->origin[2] - 22;
	else if (r_refdef.vieworg[2] > ent->origin[2] + 30)
		r_refdef.vieworg[2] = ent->origin[2] + 30;
}

/*
==============
V_AddIdle

Idle swaying
==============
*/
void V_AddIdle (void)
{
	r_refdef.viewangles[ROLL] += v_idlescale.value * sin (v_time * v_iroll_cycle.value) * v_iroll_level.value;
	r_refdef.viewangles[PITCH] += v_idlescale.value * sin (v_time * v_ipitch_cycle.value) * v_ipitch_level.value;
	r_refdef.viewangles[YAW] += v_idlescale.value * sin (v_time * v_iyaw_cycle.value) * v_iyaw_level.value;
}


/*
==============
V_CalcViewRoll

Roll is induced by movement and damage
==============
*/
void V_CalcViewRoll (void)
{
	float		side;

	side = V_CalcRoll (cl_entities[cl.viewentity]->angles, cl.velocity);
	r_refdef.viewangles[ROLL] += side;

	if (v_dmg_time > 0)
	{
		r_refdef.viewangles[ROLL] += v_dmg_time / v_kicktime.value * v_dmg_roll;
		r_refdef.viewangles[PITCH] += v_dmg_time / v_kicktime.value * v_dmg_pitch;
		v_dmg_time -= v_frametime;
	}

	if (cl.stats[STAT_HEALTH] <= 0)
	{
		r_refdef.viewangles[ROLL] = 80;	// dead view angle
		return;
	}

}


/*
==================
V_CalcIntermissionRefdef

==================
*/
void V_CalcIntermissionRefdef (void)
{
	entity_t	*ent, *view;
	float		old;

	// ent is the player model (visible when out of body)
	ent = cl_entities[cl.viewentity];

	// view is the weapon model (only visible from inside body)
	view = &cl.viewent;

	VectorCopy (ent->origin, r_refdef.vieworg);
	VectorCopy (ent->angles, r_refdef.viewangles);
	view->model = NULL;

	// allways idle in intermission
	old = v_idlescale.value;
	v_idlescale.value = 1;
	V_AddIdle ();
	v_idlescale.value = old;
}


/*
==================
V_CalcRefdef

==================
*/
void V_CalcRefdef (float time)
{
	entity_t	*ent, *view;
	int			i;
	vec3_t		forward, right, up;
	vec3_t		angles;
	float		bob;
	static float oldz = 0;

	V_DriftPitch ();

	// ent is the player model (visible when out of body)
	ent = cl_entities[cl.viewentity];

	// view is the weapon model (only visible from inside body)
	view = &cl.viewent;

	// transform the view offset by the model's matrix to get the offset from
	// model origin for the view.  the model should face the view dir.
	ent->angles[YAW] = cl.viewangles[YAW];
	ent->angles[PITCH] = -cl.viewangles[PITCH];

	bob = V_CalcBob (time);

	// refresh position
	VectorCopy (ent->origin, r_refdef.vieworg);
	r_refdef.vieworg[2] += cl.viewheight + bob;

	// never let it sit exactly on a node line, because a water plane can
	// dissapear when viewed with the eye exactly on it.
	// the server protocol only specifies to 1/16 pixel, so add 1/32 in each axis
	r_refdef.vieworg[0] += 1.0 / 32;
	r_refdef.vieworg[1] += 1.0 / 32;
	r_refdef.vieworg[2] += 1.0 / 32;

	VectorCopy (cl.viewangles, r_refdef.viewangles);
	V_CalcViewRoll ();
	V_AddIdle ();

	// offsets - because entity pitches are actually backward
	angles[PITCH] = -ent->angles[PITCH];
	angles[YAW] = ent->angles[YAW];
	angles[ROLL] = ent->angles[ROLL];

	AngleVectors (angles, forward, right, up);

	for (i = 0; i < 3; i++)
		r_refdef.vieworg[i] += scr_ofsx.value * forward[i] + scr_ofsy.value * right[i] + scr_ofsz.value * up[i];

	V_BoundOffsets ();

	// set up gun position
	VectorCopy (cl.viewangles, view->angles);

	CalcGunAngle ();

	VectorCopy (ent->origin, view->origin);
	view->origin[2] += cl.viewheight;

	for (i = 0; i < 3; i++) view->origin[i] += forward[i] * bob * 0.4;

	view->origin[2] += bob;

	// note - default equates to glquakes "viewsize 100" position.
	// fudging was only needed in software...
	// set to 0 to replicate darkplaces/fitzquake style
	view->origin[2] += v_gunangle.value * 0.75f;

	view->model = cl.model_precache[cl.stats[STAT_WEAPON]];
	view->frame = cl.stats[STAT_WEAPONFRAME];

	// set up the refresh position
	vec3_t kickangle;

	VectorScale (cl.punchangle, v_gunkick.value, kickangle);

	if (v_gunkick.value) VectorAdd (r_refdef.viewangles, kickangle, r_refdef.viewangles);

	static float oldsteptime = 0;
	extern cvar_t freelook;

	// smooth out stair step ups
	if (cl.onground && ent->origin[2] - oldz > 0)
	{
		float steptime = time - oldsteptime;

		if (steptime < 0) steptime = 0;

		oldz += steptime * 80;

		if (oldz > ent->origin[2]) oldz = ent->origin[2];
		if (ent->origin[2] - oldz > 12) oldz = ent->origin[2] - 12;

		r_refdef.vieworg[2] += oldz - ent->origin[2];
		view->origin[2] += oldz - ent->origin[2];
	}
	else oldz = ent->origin[2];

	oldsteptime = time;

	if (chase_active.value) Chase_Update ();
}


/*
==================
V_RenderView

The player's clipping box goes from (-16 -16 -24) to (16 16 32) from
the entity origin, so any view position inside that will be valid
==================
*/
extern vrect_t	scr_vrect;

void V_RenderView (float time, float frametime)
{
	if (con_forcedup) return;
	if (cls.state != ca_connected) return;
	if (!cl.model_precache) return;

	v_time = time;
	v_frametime = frametime;

	// don't allow cheats in multiplayer
	if (cl.maxclients > 1)
	{
		Cvar_Set ("scr_ofsx", "0");
		Cvar_Set ("scr_ofsy", "0");
		Cvar_Set ("scr_ofsz", "0");
	}

	if (cl.intermission)
	{
		// intermission / finale rendering
		V_CalcIntermissionRefdef ();
	}
	else
	{
		if (!cl.paused)
			V_CalcRefdef (time);
	}
}


//============================================================================

/*
=============
V_Init
=============
*/
cmd_t V_cshift_f_Cmd ("v_cshift", V_cshift_f);
cmd_t V_BonusFlash_f_Cmd ("bf", V_BonusFlash_f);
cmd_t V_StartPitchDrift_Cmd ("centerview", V_StartPitchDrift);

void V_Init (void)
{
}


