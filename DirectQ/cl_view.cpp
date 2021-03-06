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
cvar_alias_t crosshaircolor ("crosshaircolor", &scr_crosshaircolor);			
cvar_alias_t crosshairsize ("crosshairsize", &scr_crosshairscale);

// compatibility with a typo in fitz
cvar_alias_t scr_crosshaircale ("scr_crosshaircale", &scr_crosshairscale);

cvar_t	gl_cshiftpercent ("gl_cshiftpercent", "100");

cvar_t	v_gunangle ("v_gunangle", 2, CVAR_ARCHIVE);
cvar_alias_t r_gunangle ("r_gunangle", &v_gunangle);

float	v_dmg_time, v_dmg_roll, v_dmg_pitch;

extern	int			in_forward, in_forward2, in_back;


cvar_t	v_centermove ("v_centermove", 0.15f);
cvar_t	v_centerspeed ("v_centerspeed", 250.0f);


void V_StartPitchDrift (void)
{
	if (cl.laststop >= cl.time)
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
	// set above client time as CL_LerpPoint can change time before V_StartPitchDrift is called
	cl.laststop = cl.time + 1;
	cl.nodrift = true;
	cl.pitchvel = 0;
}


void V_DriftPitch (void)
{
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
		else cl.driftmove += (cl.time - cl.oldtime);	// fixme

		if (cl.driftmove > v_centermove.value)
			V_StartPitchDrift ();

		return;
	}

	float delta = cl.idealpitch - cl.viewangles[0];

	if (!delta)
	{
		cl.pitchvel = 0;
		return;
	}

	float move = (cl.time - cl.oldtime) * cl.pitchvel;
	cl.pitchvel += (cl.time - cl.oldtime) * v_centerspeed.value;

	if (delta > 0)
	{
		if (move > delta)
		{
			cl.pitchvel = 0;
			move = delta;
		}

		cl.viewangles[0] += move;
	}
	else if (delta < 0)
	{
		if (move > -delta)
		{
			cl.pitchvel = 0;
			move = -delta;
		}

		cl.viewangles[0] -= move;
	}
}

cmd_t v_centerview_cmd ("centerview", V_StartPitchDrift);

/*
===============
V_CalcRoll

Used by view and sv_user
no time dependencies
===============
*/
float V_CalcRoll (vec3_t angles, vec3_t velocity)
{
	float	sign;
	float	side;
	float	value;
	avectors_t avview;

	AngleVectors (angles, &avview);
	side = DotProduct (velocity, avview.right);
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
float V_CalcBob (void)
{
	// prevent division by 0 weirdness
	if (!cl_bob.value) return 0;
	if (!cl_bobup.value) return 0;
	if (!cl_bobcycle.value) return 0;

	// this prevents the bob cycle from constantly repeating if you bring the console down while mid-bob
	if (key_dest != key_game) return 0;

	// bound bob up
	if (cl_bobup.value >= 0.99f) Cvar_Set (&cl_bobup, 0.99f);

	// WARNING - don't try anything sexy with time in here or you'll
	// screw things up and make the engine appear to run jerky
	float cycle = cl.time - (int) (cl.time / cl_bobcycle.value) * cl_bobcycle.value;
	cycle /= cl_bobcycle.value;

	if (cycle < cl_bobup.value)
		cycle = D3DX_PI * cycle / cl_bobup.value;
	else cycle = D3DX_PI + D3DX_PI * (cycle - cl_bobup.value) / (1.0 - cl_bobup.value);

	// bob is proportional to velocity in the xy plane
	// (don't count Z, or jumping messes it up)
	float bob = sqrt (cl.velocity[0] * cl.velocity[0] + cl.velocity[1] * cl.velocity[1]) * cl_bob.value;
	bob = bob * 0.3 + bob * 0.7 * sin (cycle);

	if (bob > 4)
		bob = 4;
	else if (bob < -7)
		bob = -7;

	return bob;
}


//=============================================================================


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
void V_ParseDamage (void)
{
	int		armor, blood;
	vec3_t	from;
	int		i;
	avectors_t av;
	entity_t	*ent;
	float	side;
	float	count;

	armor = MSG_ReadByte ();
	blood = MSG_ReadByte ();

	for (i = 0; i < 3; i++)
		from[i] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);

	count = blood * 0.5 + armor * 0.5;

	if (count < 10) count = 10;

	cl.faceanimtime = cl.time + 0.2f;
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

	AngleVectors (ent->angles, &av);

	side = DotProduct (from, av.right);
	v_dmg_roll = count * side * v_kickroll.value;

	side = DotProduct (from, av.forward);
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
	cl.cshifts[CSHIFT_VCSHIFT].destcolor[0] = atoi (Cmd_Argv (1));
	cl.cshifts[CSHIFT_VCSHIFT].destcolor[1] = atoi (Cmd_Argv (2));
	cl.cshifts[CSHIFT_VCSHIFT].destcolor[2] = atoi (Cmd_Argv (3));
	cl.cshifts[CSHIFT_VCSHIFT].percent = atoi (Cmd_Argv (4));

	// setting a contents shift is evil as it will fuck with fog and warping
	/*
	cshift_empty.destcolor[0] = atoi (Cmd_Argv (1));
	cshift_empty.destcolor[1] = atoi (Cmd_Argv (2));
	cshift_empty.destcolor[2] = atoi (Cmd_Argv (3));
	cshift_empty.percent = atoi (Cmd_Argv (4));
	*/
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


void V_CalcIndividualBlend (float *rgba, float r, float g, float b, float a)
{
	// no blend
	if (a < 1) return;

	// calc alpha amount
	float a2 = a / 255.0;

	// evaluate blends
	rgba[3] = rgba[3] + a2 * (1 - rgba[3]);
	a2 = a2 / rgba[3];

	// blend it in
	rgba[0] = rgba[0] * (1 - a2) + r * a2;
	rgba[1] = rgba[1] * (1 - a2) + g * a2;
	rgba[2] = rgba[2] * (1 - a2) + b * a2;
}


/*
=============
V_CalcPowerupCshift
=============
*/
cvar_t v_quadcshift ("v_quadcshift", 1.0f);
cvar_t v_suitcshift ("v_suitcshift", 1.0f);
cvar_t v_ringcshift ("v_ringcshift", 1.0f);
cvar_t v_pentcshift ("v_pentcshift", 1.0f);

void V_CalcPowerupCshift (void)
{
	// default is no shift for no powerups
	float rgba[4] = {0, 0, 0, 0};

	// now let's see what we've got
	if ((cl.items & IT_QUAD) && v_quadcshift.value >= 0) V_CalcIndividualBlend (rgba, 0, 0, 255, (int) (30.0f * v_quadcshift.value));
	if ((cl.items & IT_SUIT) && v_suitcshift.value >= 0) V_CalcIndividualBlend (rgba, 0, 255, 0, (int) (20.0f * v_suitcshift.value));
	if ((cl.items & IT_INVISIBILITY) && v_ringcshift.value >= 0) V_CalcIndividualBlend (rgba, 100, 100, 100, (int) (100.0f * v_ringcshift.value));
	if ((cl.items & IT_INVULNERABILITY) && v_pentcshift.value >= 0) V_CalcIndividualBlend (rgba, 255, 255, 0, (int) (30.0f * v_pentcshift.value));

	// clamp blend 0-255 and store out
	cl.cshifts[CSHIFT_POWERUP].destcolor[0] = BYTE_CLAMP (rgba[0]);
	cl.cshifts[CSHIFT_POWERUP].destcolor[1] = BYTE_CLAMP (rgba[1]);
	cl.cshifts[CSHIFT_POWERUP].destcolor[2] = BYTE_CLAMP (rgba[2]);
	cl.cshifts[CSHIFT_POWERUP].percent = BYTE_CLAMPF (rgba[3]);
}


/*
=============
V_CalcBlend
=============
*/
cvar_t v_contentblend ("v_contentblend", 1.0f);
cvar_t v_damagecshift ("v_damagecshift", 1.0f);
cvar_t v_bonusflash ("v_bonusflash", 1.0f);
cvar_t v_powerupcshift ("v_powerupcshift", 1.0f);

cvar_t v_emptycshift ("v_emptycshift", 1.0f);
cvar_t v_solidcshift ("v_solidcshift", 1.0f);
cvar_t v_lavacshift ("v_lavacshift", 1.0f);
cvar_t v_watercshift ("v_watercshift", 1.0f);
cvar_t v_slimecshift ("v_slimecshift", 1.0f);

void V_AdjustContentCShift (int contents)
{
	if (contents == CONTENTS_SOLID && v_solidcshift.value >= 0)
		cl.cshifts[CSHIFT_CONTENTS].percent *= v_solidcshift.value;
	else if (contents == CONTENTS_EMPTY && v_emptycshift.value >= 0)
		cl.cshifts[CSHIFT_CONTENTS].percent *= v_emptycshift.value;
	else if (contents == CONTENTS_LAVA && v_lavacshift.value >= 0)
		cl.cshifts[CSHIFT_CONTENTS].percent *= v_lavacshift.value;
	else if (contents == CONTENTS_WATER && v_watercshift.value >= 0)
		cl.cshifts[CSHIFT_CONTENTS].percent *= v_watercshift.value;
	else if (contents == CONTENTS_SLIME && v_slimecshift.value >= 0)
		cl.cshifts[CSHIFT_CONTENTS].percent *= v_slimecshift.value;
}


void V_CalcBlend (void)
{
	float rgba[4] = {0, 0, 0, 0};

	for (int j = 0; j < NUM_CSHIFTS; j++)
	{
		if (j == CSHIFT_CONTENTS && v_contentblend.value >= 0) cl.cshifts[j].percent *= v_contentblend.value;
		if (j == CSHIFT_DAMAGE && v_damagecshift.value >= 0) cl.cshifts[j].percent *= v_damagecshift.value;
		if (j == CSHIFT_BONUS && v_bonusflash.value >= 0) cl.cshifts[j].percent *= v_bonusflash.value;
		if (j == CSHIFT_POWERUP && v_powerupcshift.value >= 0) cl.cshifts[j].percent *= v_powerupcshift.value;

		// no shift
		if (cl.cshifts[j].percent < 1) continue;

		V_CalcIndividualBlend (rgba,
			cl.cshifts[j].destcolor[0],
			cl.cshifts[j].destcolor[1],
			cl.cshifts[j].destcolor[2],
			cl.cshifts[j].percent);
	}

	// set final amounts
	v_blend[0] = BYTE_CLAMP (rgba[0]);
	v_blend[1] = BYTE_CLAMP (rgba[1]);
	v_blend[2] = BYTE_CLAMP (rgba[2]);
	v_blend[3] = BYTE_CLAMPF (rgba[3]);
}


/*
=============
V_UpdatePalette

a lot of this can go away with a non-sw renderer
=============
*/
void V_UpdateCShifts (void)
{
	if (cl.intermission)
	{
		// certain cshifts are removed on intermission
		// (we keep contents as the intermission camera may be underwater)
		cl.cshifts[CSHIFT_DAMAGE].percent = 0;
		cl.cshifts[CSHIFT_BONUS].percent = 0;
		cl.cshifts[CSHIFT_POWERUP].percent = 0;
		cl.cshifts[CSHIFT_VCSHIFT].percent = 0;
	}
	else
	{
		// drop the damage value
		cl.cshifts[CSHIFT_DAMAGE].percent -= cl.frametime * 150;

		if (cl.cshifts[CSHIFT_DAMAGE].percent <= 0)
			cl.cshifts[CSHIFT_DAMAGE].percent = 0;

		// drop the bonus value
		cl.cshifts[CSHIFT_BONUS].percent -= cl.frametime * 100;

		if (cl.cshifts[CSHIFT_BONUS].percent <= 0)
			cl.cshifts[CSHIFT_BONUS].percent = 0;

		// add powerups
		V_CalcPowerupCshift ();
	}

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

	yaw = r_refdef.viewangles[1];
	pitch = -r_refdef.viewangles[0];

	yaw = angledelta (yaw - r_refdef.viewangles[1]) * 0.4;

	if (yaw > 10) yaw = 10;
	if (yaw < -10) yaw = -10;

	pitch = angledelta (-pitch - r_refdef.viewangles[0]) * 0.4;

	if (pitch > 10) pitch = 10;
	if (pitch < -10) pitch = -10;

	move = cl.frametime * 20;

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

	cl.viewent.angles[1] = r_refdef.viewangles[1] + yaw;
	cl.viewent.angles[0] = -(r_refdef.viewangles[0] + pitch);

	// WARNING - don't try anything sexy with time in here or you'll
	// screw things up and make the engine appear to run jerky
	cl.viewent.angles[2] -= v_idlescale.value * sin (cl.time * v_iroll_cycle.value) * v_iroll_level.value;
	cl.viewent.angles[0] -= v_idlescale.value * sin (cl.time * v_ipitch_cycle.value) * v_ipitch_level.value;
	cl.viewent.angles[1] -= v_idlescale.value * sin (cl.time * v_iyaw_cycle.value) * v_iyaw_level.value;
}

/*
==============
V_BoundOffsets
==============
*/
void V_BoundOffsets (void)
{
	entity_t *ent;
	float mins[3] = {-14, -14, -22};
	float maxs[3] = {14, 14, 30};

	ent = cl_entities[cl.viewentity];

	// absolutely bound refresh reletive to entity clipping hull
	// so the view can never be inside a solid wall
	for (int i = 0; i < 3; i++)
	{
		if (r_refdef.vieworigin[i] < ent->origin[i] + mins[i])
			r_refdef.vieworigin[i] = ent->origin[i] + mins[i];
		else if (r_refdef.vieworigin[i] > ent->origin[i] + maxs[i])
			r_refdef.vieworigin[i] = ent->origin[i] + maxs[i];
	}
}


/*
==============
V_AddIdle

Idle swaying
==============
*/
void V_AddIdle (void)
{
	// WARNING - don't try anything sexy with time in here or you'll
	// screw things up and make the engine appear to run jerky
	r_refdef.viewangles[2] += v_idlescale.value * sin (cl.time * v_iroll_cycle.value) * v_iroll_level.value;
	r_refdef.viewangles[0] += v_idlescale.value * sin (cl.time * v_ipitch_cycle.value) * v_ipitch_level.value;
	r_refdef.viewangles[1] += v_idlescale.value * sin (cl.time * v_iyaw_cycle.value) * v_iyaw_level.value;
}


/*
==============
V_CalcViewRoll

Roll is induced by movement and damage
==============
*/
void V_CalcViewRoll (void)
{
	float side;

	side = V_CalcRoll (cl_entities[cl.viewentity]->angles, cl.velocity);
	r_refdef.viewangles[2] += side;

	if (v_dmg_time > 0)
	{
		r_refdef.viewangles[2] += v_dmg_time / v_kicktime.value * v_dmg_roll;
		r_refdef.viewangles[0] += v_dmg_time / v_kicktime.value * v_dmg_pitch;
		v_dmg_time -= cl.frametime;
	}

	if (cl.stats[STAT_HEALTH] <= 0)
	{
		r_refdef.viewangles[2] = 80;
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

	VectorCopy (ent->origin, r_refdef.vieworigin);
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
cvar_t cl_stepsmooth ("cl_stepsmooth", "1", CVAR_ARCHIVE);
cvar_t cl_stepsmooth_mult ("cl_stepsmooth_mult", "80", CVAR_ARCHIVE);
cvar_t cl_stepsmooth_delta ("cl_stepsmooth_delta", "12", CVAR_ARCHIVE);

void V_CalcRefdef (void)
{
	entity_t	*ent, *view;
	int			i;
	avectors_t	av;
	vec3_t		angles;
	float		bob = 0;
	static float oldz = 0;

	V_DriftPitch ();

	// ent is the player model (visible when out of body)
	ent = cl_entities[cl.viewentity];

	// view is the weapon model (only visible from inside body)
	view = &cl.viewent;

	// transform the view offset by the model's matrix to get the offset from
	// model origin for the view.  the model should face the view dir.
	ent->angles[1] = cl.viewangles[1];
	ent->angles[0] = -cl.viewangles[0];

	bob = V_CalcBob ();

	// refresh position
	VectorCopy (ent->origin, r_refdef.vieworigin);
	r_refdef.vieworigin[2] += cl.viewheight + bob;

	// never let it sit exactly on a node line, because a water plane can
	// dissapear when viewed with the eye exactly on it.
	// the server protocol only specifies to 1/16 pixel, so add 1/32 in each axis
	r_refdef.vieworigin[0] += 1.0 / 32;
	r_refdef.vieworigin[1] += 1.0 / 32;
	r_refdef.vieworigin[2] += 1.0 / 32;

	VectorCopy (cl.viewangles, r_refdef.viewangles);
	V_CalcViewRoll ();
	V_AddIdle ();

	// offsets - because entity pitches are actually backward
	angles[0] = -ent->angles[0];
	angles[1] = ent->angles[1];
	angles[2] = ent->angles[2];

	AngleVectors (angles, &av);

	for (i = 0; i < 3; i++)
		r_refdef.vieworigin[i] += scr_ofsx.value * av.forward[i] + scr_ofsy.value * av.right[i] + scr_ofsz.value * av.up[i];

	V_BoundOffsets ();

	// set up gun position
	VectorCopy (cl.viewangles, view->angles);

	CalcGunAngle ();

	VectorCopy (ent->origin, view->origin);
	view->origin[2] += cl.viewheight;

	for (i = 0; i < 3; i++) view->origin[i] += av.forward[i] * bob * 0.4;

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

	// smooth out stair step ups
	if (cl.onground && ent->origin[2] - oldz > 0 && cl_stepsmooth.value)
	{
		// WARNING - don't try anything sexy with time in here or you'll
		// screw things up and make the engine appear to run jerky
		float steptime = cl.time - cl.oldtime;

		if (steptime < 0) steptime = 0;

		// ???? what does this magic number signify anyway ????
		// looks like it should really be 72...?
		oldz += steptime * cl_stepsmooth_mult.value;

		if (oldz > ent->origin[2]) oldz = ent->origin[2];
		if (ent->origin[2] - oldz > cl_stepsmooth_delta.value) oldz = ent->origin[2] - cl_stepsmooth_delta.value;

		r_refdef.vieworigin[2] += oldz - ent->origin[2];
		view->origin[2] += oldz - ent->origin[2];
		// Con_Printf ("smmothing stair step-ups\n");
	}
	else oldz = ent->origin[2];

	if (chase_active.value) Chase_Update ();
}


/*
==================
V_RenderView

The player's clipping box goes from (-16 -16 -24) to (16 16 32) from
the entity origin, so any view position inside that will be valid
==================
*/
void V_RenderView (void)
{
	if (!cls.maprunning) return;

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
			V_CalcRefdef ();
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

void V_Init (void)
{
}


