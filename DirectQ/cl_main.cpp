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
// cl_main.c  -- client main loop

#include "quakedef.h"

// we need to declare some mouse variables here, because the menu system
// references them even when on a unix system.

// these two are not intended to be set directly
cvar_t	cl_name ("_cl_name", "player", CVAR_ARCHIVE);
cvar_t	cl_color ("_cl_color", "0", CVAR_ARCHIVE);

cvar_t	cl_shownet ("cl_shownet","0");	// can be 0, 1, or 2
cvar_t	cl_nolerp ("cl_nolerp","0");

cvar_t	lookspring ("lookspring","0", CVAR_ARCHIVE);
cvar_t	lookstrafe ("lookstrafe","0", CVAR_ARCHIVE);
cvar_t	sensitivity ("sensitivity","3", CVAR_ARCHIVE);

cvar_t	m_pitch ("m_pitch","0.022", CVAR_ARCHIVE);
cvar_t	m_yaw ("m_yaw","0.022", CVAR_ARCHIVE);
cvar_t	m_forward ("m_forward","0", CVAR_ARCHIVE);
cvar_t	m_side ("m_side","0.8", CVAR_ARCHIVE);

cvar_t	r_rapidfire ("r_rapidfire", "0", CVAR_ARCHIVE);

extern cvar_t r_extradlight;

client_static_t	cls;
client_state_t	cl;

// FIXME: put these on hunk?
// consider it done ;)
#define BASE_ENTITIES	512

entity_t		**cl_entities = NULL;
lightstyle_t	*cl_lightstyle;
dlight_t		*cl_dlights = NULL;

// visedicts now belong to the render
void D3D_AddVisEdict (entity_t *ent, bool noculledict);
void D3D_BeginVisedicts (void);


/*
=====================
CL_ClearState

=====================
*/
void CL_ClearState (void)
{
	int i;

	// this really only happens with demos
	if (!sv.active) Host_ClearMemory ();

	// wipe the entire cl structure
	memset (&cl, 0, sizeof(cl));

	SZ_Clear (&cls.message);

	// clear down anything that was allocated one-time-only at startup
	memset (cl_dlights, 0, MAX_DLIGHTS * sizeof (dlight_t));
	memset (cl_temp_entities, 0, MAX_TEMP_ENTITIES * sizeof (entity_t));
	memset (cl_beams, 0, MAX_BEAMS * sizeof (beam_t));
	memset (cl_lightstyle, 0, MAX_LIGHTSTYLES * sizeof (lightstyle_t));

	// allocate space for the first 512 entities - also clears the array
	// the remainder are left at NULL and allocated on-demand if they are ever needed
	entity_t *cl_dynamic_entities = (entity_t *) Pool_Alloc (POOL_MAP, sizeof (entity_t) * BASE_ENTITIES);

	// now fill them in
	// this array was allocated in CL_Init as it's a one-time-only need.
	for (i = 0; i < MAX_EDICTS; i++)
	{
		if (i < BASE_ENTITIES)
		{
			cl_entities[i] = &cl_dynamic_entities[i];
			cl_entities[i]->entnum = i;
		}
		else cl_entities[i] = NULL;
	}
}


/*
=====================
CL_Disconnect

Sends a disconnect message to the server
This is also called on Host_Error, so it shouldn't cause any errors
=====================
*/
void SHOWLMP_clear (void);

void CL_Disconnect (void)
{
	// stop sounds (especially looping!)
	S_StopAllSounds (true);

	SHOWLMP_clear ();

	// bring the console down and fade the colors back to normal
	//	SCR_BringDownConsole ();

	cl.worldbrush = NULL;
	cl.worldmodel = NULL;

	// if running a local server, shut it down
	if (cls.demoplayback)
		CL_StopPlayback ();
	else if (cls.state == ca_connected)
	{
		if (cls.demorecording)
			CL_Stop_f ();

		Con_DPrintf ("Sending clc_disconnect\n");
		SZ_Clear (&cls.message);
		MSG_WriteByte (&cls.message, clc_disconnect);
		NET_SendUnreliableMessage (cls.netcon, &cls.message);
		SZ_Clear (&cls.message);
		NET_Close (cls.netcon);

		cls.state = ca_disconnected;
		if (sv.active)
			Host_ShutdownServer(false);
	}

	cls.demoplayback = cls.timedemo = false;
	cls.signon = 0;
}

void CL_Disconnect_f (void)
{
	CL_Disconnect ();
	if (sv.active)
		Host_ShutdownServer (false);
}


/*
=====================
CL_EstablishConnection

Host should be either "local" or a net address to be passed on
=====================
*/
void CL_EstablishConnection (char *host)
{
	if (cls.demoplayback) return;

	CL_Disconnect ();

	cls.netcon = NET_Connect (host);
	if (!cls.netcon)
		Host_Error ("CL_Connect: connect failed\n");
	Con_DPrintf ("CL_EstablishConnection: connected to %s\n", host);
	
	cls.demonum = -1;			// not in the demo loop now
	cls.state = ca_connected;
	cls.signon = 0;				// need all the signon messages before playing
}

/*
=====================
CL_SignonReply

An svc_signonnum has been received, perform a client side setup
=====================
*/
void CL_SignonReply (void)
{
	static char 	str[8192];

	Con_DPrintf ("CL_SignonReply: %i\n", cls.signon);

	switch (cls.signon)
	{
	case 1:
		MSG_WriteByte (&cls.message, clc_stringcmd);
		MSG_WriteString (&cls.message, "prespawn");
		break;
		
	case 2:		
		MSG_WriteByte (&cls.message, clc_stringcmd);
		MSG_WriteString (&cls.message, va("name \"%s\"\n", cl_name.string));
	
		MSG_WriteByte (&cls.message, clc_stringcmd);
		MSG_WriteString (&cls.message, va("color %i %i\n", ((int)cl_color.value)>>4, ((int)cl_color.value)&15));
	
		MSG_WriteByte (&cls.message, clc_stringcmd);
		_snprintf (str, 8192, "spawn %s", cls.spawnparms);
		MSG_WriteString (&cls.message, str);
		break;
		
	case 3:	
		MSG_WriteByte (&cls.message, clc_stringcmd);
		MSG_WriteString (&cls.message, "begin");
		break;
		
	case 4:
		SCR_EndLoadingPlaque ();		// allow normal screen updates
		break;
	}
}

/*
=====================
CL_NextDemo

Called to play the next demo in the demo loop
=====================
*/
void CL_NextDemo (void)
{
	char	str[1024];

	if (cls.demonum == -1)
		return;		// don't play demos

	// this causes lockups when switching between demos in warpspasm.
	// let's just not do it. ;)
	SCR_BeginLoadingPlaque ();

	if (!cls.demos[cls.demonum][0] || cls.demonum == MAX_DEMOS)
	{
		cls.demonum = 0;
		if (!cls.demos[cls.demonum][0])
		{
			Con_Printf ("No demos listed with startdemos\n");
			cls.demonum = -1;
			return;
		}
	}

	_snprintf (str,1024, "playdemo %s\n", cls.demos[cls.demonum]);
	Cbuf_InsertText (str);
	cls.demonum++;
}


/*
==============
CL_PrintEntities_f
==============
*/
void CL_PrintEntities_f (void)
{
	int i;

	for (i = 0; i < cl.num_entities; i++)
	{
		entity_t *ent = cl_entities[i];

		Con_Printf ("%3i:", i);

		if (!ent->model)
		{
			Con_Printf ("EMPTY\n");
			continue;
		}

		Con_Printf
		(
			"%s:%3i  (%5.1f,%5.1f,%5.1f) [%5.1f %5.1f %5.1f]\n",
			ent->model->name,
			ent->frame,
			ent->origin[0],
			ent->origin[1],
			ent->origin[2],
			ent->angles[0],
			ent->angles[1],
			ent->angles[2]
		);
	}
}


/*
===============
SetPal

Debugging tool, just flashes the screen
===============
*/
void SetPal (int i)
{
#if 0
	static int old;
	byte	pal[768];
	int		c;
	
	if (i == old)
		return;
	old = i;

	if (i==0)
		VID_SetPalette (host_basepal);
	else if (i==1)
	{
		for (c=0 ; c<768 ; c+=3)
		{
			pal[c] = 0;
			pal[c+1] = 255;
			pal[c+2] = 0;
		}
		VID_SetPalette (pal);
	}
	else
	{
		for (c=0 ; c<768 ; c+=3)
		{
			pal[c] = 0;
			pal[c+1] = 0;
			pal[c+2] = 255;
		}
		VID_SetPalette (pal);
	}
#endif
}


/*
===============
CL_AllocDlight

===============
*/
dlight_t *CL_FindDlight (int key)
{
	int		i;
	dlight_t	*dl;

	// first look for an exact key match
	if (key)
	{
		dl = cl_dlights;
		for (i=0 ; i<MAX_DLIGHTS ; i++, dl++)
		{
			if (dl->key == key)
			{
				memset (dl, 0, sizeof(*dl));
				dl->key = key;
				return dl;
			}
		}
	}

	// then look for anything else
	dl = cl_dlights;

	for (i=0 ; i<MAX_DLIGHTS ; i++, dl++)
	{
		if (dl->die < cl.time)
		{
			memset (dl, 0, sizeof(*dl));
			dl->key = key;
			return dl;
		}
	}

	dl = &cl_dlights[0];
	memset (dl, 0, sizeof(*dl));
	dl->key = key;
	return dl;
}


dlight_t *CL_AllocDlight (int key)
{
	// find a dlight to use
	dlight_t *dl = CL_FindDlight (key);

	// set default colour for any we don't colour ourselves
	// NOTE - all dlight colour ops should go through R_ColourDLight (in d3d_rlight.cpp) rather than
	// accessing the fields directly, so that those who don't want coloured light can switch it off.
	dl->rgb[0] = dl->rgb[1] = dl->rgb[2] = 255;

	// done
	return dl;
}


/*
===============
CL_DecayLights

===============
*/
void CL_DecayLights (void)
{
	int			i;
	dlight_t	*dl;
	float		time;
	static float old_decaytime = 0;

	time = cl.time - old_decaytime;

	dl = cl_dlights;

	for (i = 0; i < MAX_DLIGHTS; i++, dl++)
	{
		if (dl->die < cl.time || !dl->radius)
			continue;

		dl->radius -= time * dl->decay;

		if (dl->radius < 0)
		{
			dl->radius = 0;
			dl->die = -1;
		}
	}

	old_decaytime = cl.time;
}


/*
===============
CL_LerpPoint

Determines the fraction between the last two messages that the objects
should be put at.
===============
*/
float	CL_LerpPoint (void)
{
	float	f, frac;

	f = cl.mtime[0] - cl.mtime[1];
	
	if (!f || cl_nolerp.value || cls.timedemo || sv.active)
	{
		cl.time = cl.mtime[0];
		return 1;
	}
		
	if (f > 0.1)
	{	// dropped packet, or start of demo
		cl.mtime[1] = cl.mtime[0] - 0.1;
		f = 0.1;
	}
	frac = (cl.time - cl.mtime[1]) / f;
//Con_Printf ("frac: %f\n",frac);
	if (frac < 0)
	{
		if (frac < -0.01)
		{
SetPal(1);
			cl.time = cl.mtime[1];
//				Con_Printf ("low frac\n");
		}
		frac = 0;
	}
	else if (frac > 1)
	{
		if (frac > 1.01)
		{
SetPal(2);
			cl.time = cl.mtime[0];
//				Con_Printf ("high frac\n");
		}
		frac = 1;
	}
	else
		SetPal(0);
		
	return frac;
}


/*
===============
CL_RelinkEntities
===============
*/
extern cvar_t r_lerporient;

void CL_RelinkEntities (void)
{
	// reset visedicts count and structs
	D3D_BeginVisedicts ();

	entity_t	*ent;
	int			i, j;
	float		frac, f, d;
	vec3_t		delta;
	float		bobjrotate;
	vec3_t		oldorg;
	dlight_t	*dl;
	float timepassed;
	float blend;

	// determine partial update time	
	frac = CL_LerpPoint ();

	// interpolate player info
	for (i = 0; i < 3; i++)
		cl.velocity[i] = cl.mvelocity[1][i] + frac * (cl.mvelocity[0][i] - cl.mvelocity[1][i]);

	if (cls.demoplayback)
	{
		// interpolate the angles	
		for (j = 0; j < 3; j++)
		{
			d = cl.mviewangles[0][j] - cl.mviewangles[1][j];

			if (d > 180) 
				d -= 360;
			else if (d < -180)
				d += 360;

			cl.viewangles[j] = cl.mviewangles[1][j] + frac * d;
		}
	}

	bobjrotate = anglemod (100 * cl.time);

	// start on the entity after the world
	for (i = 1; i < cl.num_entities; i++)
	{
		ent = cl_entities[i];

		// doesn't have a model
		if (!ent->model)
		{
			// remove this entity from the render
			if (ent->forcelink) ent->staticremoved = true;

			continue;
		}

		// if the object wasn't included in the last packet, remove it
		if (ent->msgtime != cl.mtime[0])
		{
			ent->model = NULL;

			// clear it's interpolation data
			ent->frame_start_time     = 0;
			ent->translate_start_time = 0;
			ent->rotate_start_time    = 0;

			// and the poses
			ent->pose1 = ent->pose2 = 0;

			continue;
		}

		VectorCopy (ent->origin, oldorg);

		if (ent->forcelink)
		{
			// the entity was not updated in the last message
			// so move to the final spot
			VectorCopy (ent->msg_origins[0], ent->origin);
			VectorCopy (ent->msg_angles[0], ent->angles);
		}
		else
		{
			// if the delta is large, assume a teleport and don't lerp
			f = frac;

			for (j = 0; j < 3; j++)
			{
				delta[j] = ent->msg_origins[0][j] - ent->msg_origins[1][j];

				if (delta[j] > 100 || delta[j] < -100)
				{
					// assume a teleportation, not a motion
					f = 1;
				}

				if (f >= 1)
				{
					// clear interpolation data
					ent->translate_start_time = 0;
					ent->rotate_start_time    = 0;
				}
			}

			// interpolate the origin and angles
			for (j = 0; j < 3; j++)
			{
				ent->origin[j] = ent->msg_origins[1][j] + f * delta[j];

				d = ent->msg_angles[0][j] - ent->msg_angles[1][j];

				if (d > 180)
					d -= 360;
				else if (d < -180)
					d += 360;

				ent->angles[j] = ent->msg_angles[1][j] + f * d;
			}
		}

		// blend the positions
		// (moved from R_RotateForEntity to here so that the blended position is available everywhere on the client)
		// (fixes a lot of interpolation related bugs and makes other hacky workarounds unnecessary :))
		// positional interpolation
		timepassed = cl.time - ent->translate_start_time;

		if (ent->translate_start_time == 0 || timepassed > 1)
		{
			ent->translate_start_time = cl.time;

			VectorCopy (ent->origin, ent->origin1);
			VectorCopy (ent->origin, ent->origin2);
		}

		if (!VectorCompare (ent->origin, ent->origin2))
		{
			ent->translate_start_time = cl.time;

			VectorCopy (ent->origin2, ent->origin1);
			VectorCopy (ent->origin,  ent->origin2);

			blend = 0;
		}
		else
		{
			blend = timepassed / 0.1;

			if (cl.paused || blend > 1) blend = 1;
		}

		VectorSubtract (ent->origin2, ent->origin1, delta);

		// switch off interpolation
		bool nolerp = false;

		if (ent->model->type == mod_alias)
			nolerp = ent->model->ah->nolerp;

		if (r_lerporient.value && !nolerp)
		{
			// set interpolated origin
			ent->origin[0] = ent->origin1[0] + (blend * delta[0]);
			ent->origin[1] = ent->origin1[1] + (blend * delta[1]);
			ent->origin[2] = ent->origin1[2] + (blend * delta[2]);

			// orientation interpolation (Euler angles, yuck!)
			timepassed = cl.time - ent->rotate_start_time; 

			if (ent->rotate_start_time == 0 || timepassed > 1)
			{
				ent->rotate_start_time = cl.time;

				VectorCopy (ent->angles, ent->angles1);
				VectorCopy (ent->angles, ent->angles2);
			}

			if (!VectorCompare (ent->angles, ent->angles2))
			{
				ent->rotate_start_time = cl.time;

				VectorCopy (ent->angles2, ent->angles1);
				VectorCopy (ent->angles,  ent->angles2);

				blend = 0;
			}
			else
			{
				blend = timepassed / 0.1;

				if (cl.paused || blend > 1) blend = 1;
			}

			VectorSubtract (ent->angles2, ent->angles1, delta);

			// always interpolate along the shortest path
			if (delta[0] > 180) delta[0] -= 360; else if (delta[0] < -180) delta[0] += 360;
			if (delta[1] > 180) delta[1] -= 360; else if (delta[1] < -180) delta[1] += 360;
			if (delta[2] > 180) delta[2] -= 360; else if (delta[2] < -180) delta[2] += 360;

			// set interpolated angles
			ent->angles[0] = ent->angles1[0] + (blend * delta[0]);
			ent->angles[1] = ent->angles1[1] + (blend * delta[1]);
			ent->angles[2] = ent->angles1[2] + (blend * delta[2]);
		}

		// rotate binary objects locally
		if (ent->model->flags & EF_ROTATE)
		{
			// bugfix - a rotating backpack spawned from a dead player gets the same angles as the player
			// if it was spawned when the player is not upright (e.g. killed by a rocket or similar) and
			// it inherits the players entity_t struct
			ent->angles[0] = 0;
			ent->angles[1] = bobjrotate;
			ent->angles[2] = 0;
		}

		if (ent->effects & EF_BRIGHTFIELD)
			R_EntityParticles (ent);

		if (ent->effects & EF_MUZZLEFLASH)
		{
			vec3_t		fv, rv, uv;

			dl = CL_AllocDlight (i);
			dl->die = cl.time + 0.1;

			// some entities have different attacks resulting in a different flash colour
			if (!strncmp (&ent->model->name[6], "wizard", 6))
				R_ColourDLight (dl, 125, 492, 146);
			else if (!strncmp (&ent->model->name[6], "shalrath", 8))
				R_ColourDLight (dl, 399, 141, 228);
			else if (!strncmp (&ent->model->name[6], "shambler", 8))
				R_ColourDLight (dl, 2, 225, 541);
			else R_ColourDLight (dl, 408, 242, 117);

			if (i == cl.viewentity)
			{
				if (cl.stats[STAT_ACTIVEWEAPON] == IT_NAILGUN)
				{
					// rapid fire
					dl->die = cl.time + 0.001;
				}
				else if (cl.stats[STAT_ACTIVEWEAPON] == IT_SUPER_LIGHTNING)
				{
					// rapid fire
					dl->die = cl.time + 0.001;

					// switch the dlight colour
					R_ColourDLight (dl, 2, 225, 541);
				}
				else if (cl.stats[STAT_ACTIVEWEAPON] == IT_LIGHTNING)
				{
					// rapid fire
					dl->die = cl.time + 0.001;

					// switch the dlight colour
					R_ColourDLight (dl, 2, 225, 541);
				}
				else if (cl.stats[STAT_ACTIVEWEAPON] == IT_SUPER_NAILGUN)
				{
					// rapid fire
					dl->die = cl.time + 0.001;
				}

				// switch off rapid fire
				if (!r_rapidfire.value) dl->die = cl.time + 0.1;
			}

			VectorCopy (ent->origin,  dl->origin);
			dl->origin[2] += 16;
			AngleVectors (ent->angles, fv, rv, uv);

			VectorMA (dl->origin, 18, fv, dl->origin);
			dl->radius = 200 + (rand()&31);
			dl->minlight = 32;
		}
		if (ent->effects & EF_BRIGHTLIGHT)
		{
			// uncoloured
			dl = CL_AllocDlight (i);
			VectorCopy (ent->origin,  dl->origin);
			dl->origin[2] += 16;
			dl->radius = 400 + (rand()&31);
			dl->die = cl.time + 0.001;
		}
		if (ent->effects & EF_DIMLIGHT)
		{			
			dl = CL_AllocDlight (i);
			VectorCopy (ent->origin,  dl->origin);
			dl->radius = 200 + (rand()&31);
			dl->die = cl.time + 0.001;

			// powerup dynamic lights
			if (i == cl.viewentity)
			{
				int AverageColour;
				int rgb[3];

				rgb[0] = 64;
				rgb[1] = 64;
				rgb[2] = 64;

				if (cl.items & IT_QUAD) rgb[2] = 255;
				if (cl.items & IT_INVULNERABILITY) rgb[0] = 255;

				// re-balance the colours
				AverageColour = (rgb[0] + rgb[1] + rgb[2]) / 3;

				rgb[0] = rgb[0] * 255 / AverageColour;
				rgb[1] = rgb[1] * 255 / AverageColour;
				rgb[2] = rgb[2] * 255 / AverageColour;

				R_ColourDLight (dl, rgb[0], rgb[1], rgb[2]);
			}
		}

		if (ent->model->flags & EF_GIB)
			R_RocketTrail (oldorg, ent->origin, 2);
		else if (ent->model->flags & EF_ZOMGIB)
			R_RocketTrail (oldorg, ent->origin, 4);
		else if (ent->model->flags & EF_TRACER)
		{
			// wizard trail
			R_RocketTrail (oldorg, ent->origin, 3);

			if (r_extradlight.value)
			{
				dl = CL_AllocDlight (i);
				VectorCopy (ent->origin, dl->origin);
				dl->radius = 200;
				dl->die = cl.time + 0.01;

				R_ColourDLight (dl, 125, 492, 146);
			}
		}
		else if (ent->model->flags & EF_TRACER2)
		{
			// knight trail
			R_RocketTrail (oldorg, ent->origin, 5);

			if (r_extradlight.value)
			{
				dl = CL_AllocDlight (i);
				VectorCopy (ent->origin, dl->origin);
				dl->radius = 200;
				dl->die = cl.time + 0.01;

				R_ColourDLight (dl, 408, 242, 117);
			}
		}
		else if (ent->model->flags & EF_ROCKET)
		{
			R_RocketTrail (oldorg, ent->origin, 0);
			dl = CL_AllocDlight (i);
			VectorCopy (ent->origin, dl->origin);
			dl->radius = 200;
			dl->die = cl.time + 0.01;

			R_ColourDLight (dl, 408, 242, 117);
		}
		else if (ent->model->flags & EF_GRENADE)
			R_RocketTrail (oldorg, ent->origin, 1);
		else if (ent->model->flags & EF_TRACER3)
		{
			// vore trail
			R_RocketTrail (oldorg, ent->origin, 6);

			if (r_extradlight.value)
			{
				dl = CL_AllocDlight (i);
				VectorCopy (ent->origin, dl->origin);
				dl->radius = 200;
				dl->die = cl.time + 0.01;

				R_ColourDLight (dl, 399, 141, 228);
			}
		}

		ent->forcelink = false;

		// chasecam test
		if (i == cl.viewentity && !chase_active.value) continue;

		D3D_AddVisEdict (ent, false);
	}
}


/*
===============
CL_ReadFromServer

Read all incoming data from the server
===============
*/
int CL_ReadFromServer (void)
{
	int		ret;

	cl.oldtime = cl.time;
	cl.time += host_frametime;

	do
	{
		ret = CL_GetMessage ();
		if (ret == -1)
			Host_Error ("CL_ReadFromServer: lost server connection");
		if (!ret)
			break;
		
		cl.last_received_message = realtime;
		CL_ParseServerMessage ();
	} while (ret && cls.state == ca_connected);

	if (cl_shownet.value)
		Con_Printf ("\n");

	CL_RelinkEntities ();

	CL_UpdateTEnts ();

	// bring the links up to date
	return 0;
}

/*
=================
CL_SendCmd
=================
*/
void CL_SendCmd (void)
{
	usercmd_t		cmd;

	if (cls.state != ca_connected)
		return;

	if (cls.signon == SIGNONS)
	{
		// get basic movement from keyboard
		CL_BaseMove (&cmd);

		// allow mice or other external controllers to add to the move
		IN_Move (&cmd);

		// send the unreliable message
		CL_SendMove (&cmd);
	}

	if (cls.demoplayback)
	{
		SZ_Clear (&cls.message);
		return;
	}

	// send the reliable message
	if (!cls.message.cursize)
		return;		// no message at all

	if (!NET_CanSendMessage (cls.netcon))
	{
		Con_DPrintf ("CL_WriteToServer: can't send\n");
		return;
	}

	if (NET_SendMessage (cls.netcon, &cls.message) == -1)
		Host_Error ("CL_WriteToServer: lost server connection");

	SZ_Clear (&cls.message);
}

/*
=================
CL_Init
=================
*/
cmd_t CL_PrintEntities_f_Cmd ("entities", CL_PrintEntities_f);
cmd_t CL_Disconnect_f_Cmd ("disconnect", CL_Disconnect_f);
cmd_t CL_Record_f_Cmd ("record", CL_Record_f);
cmd_t CL_Stop_f_Cmd ("stop", CL_Stop_f);
cmd_t CL_PlayDemo_f_Cmd ("playdemo", CL_PlayDemo_f);
cmd_t CL_TimeDemo_f_Cmd ("timedemo", CL_TimeDemo_f);


void CL_Init (void)
{	
	SZ_Alloc (&cls.message, 1024);

	// allocated here because (1) it's just a poxy array of pointers, so no big deal, and (2) it's
	// referenced during a changelevel (?only when there's no intermission?) so it needs to be in
	// the persistent heap, and (3) the full thing is allocated each map anyway, so why not shift
	// the overhead (small as it is) to one time only.
	cl_entities = (entity_t **) Pool_Alloc (POOL_PERMANENT, MAX_EDICTS * sizeof (entity_t *));

	// these were static arrays but we put them into memory pools so that we can track usage more accurately
	cl_dlights = (dlight_t *) Pool_Alloc (POOL_PERMANENT, MAX_DLIGHTS * sizeof (dlight_t));
	cl_temp_entities = (entity_t *) Pool_Alloc (POOL_PERMANENT, MAX_TEMP_ENTITIES * sizeof (entity_t));
	cl_beams = (beam_t *) Pool_Alloc (POOL_PERMANENT, MAX_BEAMS * sizeof (beam_t));
	cl_lightstyle = (lightstyle_t *) Pool_Alloc (POOL_PERMANENT, MAX_LIGHTSTYLES * sizeof (lightstyle_t));

	CL_InitInput ();
}


