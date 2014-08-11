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
// cl_tent.c -- client side temporary entities

#include "quakedef.h"
#include "d3d_model.h"

CSpaceBuffer *Pool_TempEntities = NULL;
CSpaceBuffer *Pool_Beams = NULL;

int			num_temp_entities;
entity_t	*cl_temp_entities;

beam_t		*cl_beams;
int			num_beams;

sfx_t			*cl_sfx_wizhit;
sfx_t			*cl_sfx_knighthit;
sfx_t			*cl_sfx_tink1;
sfx_t			*cl_sfx_ric1;
sfx_t			*cl_sfx_ric2;
sfx_t			*cl_sfx_ric3;
sfx_t			*cl_sfx_r_exp3;

cvar_t r_extradlight ("r_extradlight", "1", CVAR_ARCHIVE);

void D3D_AddVisEdict (entity_t *ent);

/*
=================
CL_ParseTEnt
=================
*/
model_t	*cl_bolt1_mod = NULL;
model_t	*cl_bolt2_mod = NULL;
model_t	*cl_bolt3_mod = NULL;
model_t *cl_beam_mod = NULL;

void CL_InitTEnts (void)
{
	beam_t	*b;
	int		i;

	// reset temp entitiy counts; the pools will be created on-demand
	num_temp_entities = 0;
	num_beams = 0;

	// we need to load these too as models are being cleared between maps
	cl_bolt1_mod = Mod_ForName ("progs/bolt.mdl", true);
	cl_bolt2_mod = Mod_ForName ("progs/bolt2.mdl", true);
	cl_bolt3_mod = Mod_ForName ("progs/bolt3.mdl", true);

	// don't crash as this isn't in ID1
	cl_beam_mod = Mod_ForName ("progs/beam.mdl", false);

	// sounds
	cl_sfx_wizhit = S_PrecacheSound ("wizard/hit.wav");
	cl_sfx_knighthit = S_PrecacheSound ("hknight/hit.wav");
	cl_sfx_tink1 = S_PrecacheSound ("weapons/tink1.wav");
	cl_sfx_ric1 = S_PrecacheSound ("weapons/ric1.wav");
	cl_sfx_ric2 = S_PrecacheSound ("weapons/ric2.wav");
	cl_sfx_ric3 = S_PrecacheSound ("weapons/ric3.wav");
	cl_sfx_r_exp3 = S_PrecacheSound ("weapons/r_exp3.wav");
}

/*
=================
CL_ParseBeam
=================
*/
void CL_ParseBeam (model_t *m, int ent, vec3_t start, vec3_t end)
{
	// if the model didn't load just ignore it
	if (!m) return;

	if (!Pool_Beams)
	{
		Pool_Beams = new CSpaceBuffer ("Beams", 1, POOL_MAP);
		cl_beams = (beam_t *) Pool_Beams->Alloc (1);
		Pool_Beams->Rewind ();
	}

	// reset the beams pool to the beginning if there are no active beams
	if (!num_beams) Pool_Beams->Rewind ();

	beam_t	*b;
	int		i;

	// override any beam with the same entity
	// fixme - reuse first???
	for (i = 0, b = cl_beams; i < num_beams; i++, b++)
	{
		if (b->entity == ent)
		{
			b->entity = ent;
			b->model = m;
			b->endtime = cl.time + 0.2;
			VectorCopy (start, b->start);
			VectorCopy (end, b->end);
			return;
		}
	}

	// find a free beam
	for (i = 0, b = cl_beams; i < num_beams; i++, b++)
	{
		if (!b->model || b->endtime < cl.time)
		{
			b->entity = ent;
			b->model = m;
			b->endtime = cl.time + 0.2;
			VectorCopy (start, b->start);
			VectorCopy (end, b->end);
			return;
		}
	}

	// create a new beam
	b = (beam_t *) Pool_Beams->Alloc (sizeof (beam_t));

	b->entity = ent;
	b->model = m;
	b->endtime = cl.time + 0.2;
	VectorCopy (start, b->start);
	VectorCopy (end, b->end);
	num_beams++;
}


/*
=================
CL_ParseTEnt
=================
*/
void R_WallHitParticles (vec3_t org, vec3_t dir, int color, int count);
void R_ParticleRain (vec3_t mins, vec3_t maxs, vec3_t dir, int count, int colorbase, int type);

void CL_ParseTEnt (void)
{
	int type;
	int count;
	vec3_t	pos;
	vec3_t	pos2;
	vec3_t	dir;

	dlight_t *dl;
	int rnd;
	int colorStart, colorLength;

	// lightning needs this
	int ent;
	vec3_t	start, end;

	type = MSG_ReadByte ();

	switch (type)
	{
	case TE_WIZSPIKE:			// spike hitting wall
		pos[0] = MSG_ReadCoord ();
		pos[1] = MSG_ReadCoord ();
		pos[2] = MSG_ReadCoord ();

		if (r_extradlight.value)
		{
			dl = CL_AllocDlight (0);
			VectorCopy (pos, dl->origin);
			dl->radius = 250;
			dl->die = cl.time + 0.5;
			dl->decay = 300;

			R_ColourDLight (dl, 125, 492, 146);
		}

		R_WallHitParticles (pos, vec3_origin, 20, 30);
		S_StartSound (-1, 0, cl_sfx_wizhit, pos, 1, 1);
		break;
		
	case TE_KNIGHTSPIKE:			// spike hitting wall
		pos[0] = MSG_ReadCoord ();
		pos[1] = MSG_ReadCoord ();
		pos[2] = MSG_ReadCoord ();

		if (r_extradlight.value)
		{
			dl = CL_AllocDlight (0);
			VectorCopy (pos, dl->origin);
			dl->radius = 250;
			dl->die = cl.time + 0.5;
			dl->decay = 300;

			R_ColourDLight (dl, 408, 242, 117);
		}

		R_WallHitParticles (pos, vec3_origin, 226, 20);
		S_StartSound (-1, 0, cl_sfx_knighthit, pos, 1, 1);
		break;
		
	case TE_SPIKE:			// spike hitting wall
		pos[0] = MSG_ReadCoord ();
		pos[1] = MSG_ReadCoord ();
		pos[2] = MSG_ReadCoord ();

		R_WallHitParticles (pos, vec3_origin, 0, 10);

		if ( rand() % 5 )
			S_StartSound (-1, 0, cl_sfx_tink1, pos, 1, 1);
		else
		{
			rnd = rand() & 3;
			if (rnd == 1)
				S_StartSound (-1, 0, cl_sfx_ric1, pos, 1, 1);
			else if (rnd == 2)
				S_StartSound (-1, 0, cl_sfx_ric2, pos, 1, 1);
			else S_StartSound (-1, 0, cl_sfx_ric3, pos, 1, 1);
		}
		break;

	case TE_SUPERSPIKE:			// super spike hitting wall
		pos[0] = MSG_ReadCoord ();
		pos[1] = MSG_ReadCoord ();
		pos[2] = MSG_ReadCoord ();
		R_WallHitParticles (pos, vec3_origin, 0, 20);

		if ( rand() % 5 )
			S_StartSound (-1, 0, cl_sfx_tink1, pos, 1, 1);
		else
		{
			rnd = rand() & 3;
			if (rnd == 1)
				S_StartSound (-1, 0, cl_sfx_ric1, pos, 1, 1);
			else if (rnd == 2)
				S_StartSound (-1, 0, cl_sfx_ric2, pos, 1, 1);
			else
				S_StartSound (-1, 0, cl_sfx_ric3, pos, 1, 1);
		}
		break;
		
	case TE_GUNSHOT:			// bullet hitting wall
		pos[0] = MSG_ReadCoord ();
		pos[1] = MSG_ReadCoord ();
		pos[2] = MSG_ReadCoord ();
		R_WallHitParticles (pos, vec3_origin, 0, 20);
		break;
		
	case TE_EXPLOSION:			// rocket explosion
		pos[0] = MSG_ReadCoord ();
		pos[1] = MSG_ReadCoord ();
		pos[2] = MSG_ReadCoord ();
		R_ParticleExplosion (pos);

		dl = CL_AllocDlight (0);
		VectorCopy (pos, dl->origin);
		dl->radius = 350;
		dl->die = cl.time + 0.5;
		dl->decay = 300;

		R_ColourDLight (dl, 408, 242, 117);

		S_StartSound (-1, 0, cl_sfx_r_exp3, pos, 1, 1);
		break;
		
	case TE_TAREXPLOSION:			// tarbaby explosion
		pos[0] = MSG_ReadCoord ();
		pos[1] = MSG_ReadCoord ();
		pos[2] = MSG_ReadCoord ();
		R_BlobExplosion (pos);

		if (r_extradlight.value)
		{
			dl = CL_AllocDlight (0);
			VectorCopy (pos, dl->origin);
			dl->radius = 350;
			dl->die = cl.time + 0.5;
			dl->decay = 300;

			R_ColourDLight (dl, 399, 141, 228);
		}

		S_StartSound (-1, 0, cl_sfx_r_exp3, pos, 1, 1);
		break;

	case TE_LIGHTNING1:				// lightning bolts
		ent = MSG_ReadShort ();
		
		start[0] = MSG_ReadCoord ();
		start[1] = MSG_ReadCoord ();
		start[2] = MSG_ReadCoord ();
		
		end[0] = MSG_ReadCoord ();
		end[1] = MSG_ReadCoord ();
		end[2] = MSG_ReadCoord ();

		if (r_extradlight.value)
		{
			dl = CL_AllocDlight (0);
			VectorCopy (start, dl->origin);
			dl->radius = 250;
			dl->die = cl.time + 0.1;
			dl->decay = 300;

			R_ColourDLight (dl, 2, 225, 541);

			dl = CL_AllocDlight (0);
			VectorCopy (end, dl->origin);
			dl->radius = 250;
			dl->die = cl.time + 0.1;
			dl->decay = 300;

			R_ColourDLight (dl, 2, 225, 541);
		}

		CL_ParseBeam (cl_bolt1_mod, ent, start, end);
		break;

	case TE_LIGHTNING2:				// lightning bolts
		ent = MSG_ReadShort ();
		
		start[0] = MSG_ReadCoord ();
		start[1] = MSG_ReadCoord ();
		start[2] = MSG_ReadCoord ();
		
		end[0] = MSG_ReadCoord ();
		end[1] = MSG_ReadCoord ();
		end[2] = MSG_ReadCoord ();

		if (r_extradlight.value)
		{
			dl = CL_AllocDlight (0);
			VectorCopy (start, dl->origin);
			dl->radius = 250;
			dl->die = cl.time + 0.1;
			dl->decay = 300;

			R_ColourDLight (dl, 2, 225, 541);

			dl = CL_AllocDlight (0);
			VectorCopy (end, dl->origin);
			dl->radius = 250;
			dl->die = cl.time + 0.1;
			dl->decay = 300;

			R_ColourDLight (dl, 2, 225, 541);
		}

		CL_ParseBeam (cl_bolt2_mod, ent, start, end);
		break;

	case TE_LIGHTNING3:				// lightning bolts
		ent = MSG_ReadShort ();
		
		start[0] = MSG_ReadCoord ();
		start[1] = MSG_ReadCoord ();
		start[2] = MSG_ReadCoord ();
		
		end[0] = MSG_ReadCoord ();
		end[1] = MSG_ReadCoord ();
		end[2] = MSG_ReadCoord ();

		if (r_extradlight.value)
		{
			dl = CL_AllocDlight (0);
			VectorCopy (start, dl->origin);
			dl->radius = 250;
			dl->die = cl.time + 0.1;
			dl->decay = 300;

			R_ColourDLight (dl, 2, 225, 541);

			dl = CL_AllocDlight (0);
			VectorCopy (end, dl->origin);
			dl->radius = 250;
			dl->die = cl.time + 0.1;
			dl->decay = 300;

			R_ColourDLight (dl, 2, 225, 541);
		}

		CL_ParseBeam (cl_bolt3_mod, ent, start, end);
		break;

// PGM 01/21/97 
	case TE_BEAM:				// grappling hook beam
		ent = MSG_ReadShort ();

		start[0] = MSG_ReadCoord ();
		start[1] = MSG_ReadCoord ();
		start[2] = MSG_ReadCoord ();
		
		end[0] = MSG_ReadCoord ();
		end[1] = MSG_ReadCoord ();
		end[2] = MSG_ReadCoord ();

		CL_ParseBeam (cl_beam_mod, ent, start, end);
		break;
// PGM 01/21/97

	case TE_LAVASPLASH:	
		pos[0] = MSG_ReadCoord ();
		pos[1] = MSG_ReadCoord ();
		pos[2] = MSG_ReadCoord ();
		R_LavaSplash (pos);
		break;
	
	case TE_TELEPORT:
		pos[0] = MSG_ReadCoord ();
		pos[1] = MSG_ReadCoord ();
		pos[2] = MSG_ReadCoord ();
		R_TeleportSplash (pos);
		break;
		
	case TE_EXPLOSION2:				// color mapped explosion
		pos[0] = MSG_ReadCoord ();
		pos[1] = MSG_ReadCoord ();
		pos[2] = MSG_ReadCoord ();
		colorStart = MSG_ReadByte ();
		colorLength = MSG_ReadByte ();
		R_ParticleExplosion2 (pos, colorStart, colorLength);
		dl = CL_AllocDlight (0);
		VectorCopy (pos, dl->origin);
		dl->radius = 350;
		dl->die = cl.time + 0.5;
		dl->decay = 300;
		S_StartSound (-1, 0, cl_sfx_r_exp3, pos, 1, 1);
		break;

	case TE_SMOKE:
		// falls through to explosion 3
		pos[0] = MSG_ReadCoord ();
		pos[1] = MSG_ReadCoord ();
		pos[2] = MSG_ReadCoord ();
		MSG_ReadByte();

	case TE_EXPLOSION3:
		pos[0] = MSG_ReadCoord ();
		pos[1] = MSG_ReadCoord ();
		pos[2] = MSG_ReadCoord ();
		dl = CL_AllocDlight (0);
		VectorCopy (pos, dl->origin);
		dl->radius = 350;
		dl->die = cl.time + 0.5;
		dl->decay = 300;
		dl->rgb[0] = MSG_ReadCoord() * 255;
		dl->rgb[1] = MSG_ReadCoord() * 255;
		dl->rgb[2] = MSG_ReadCoord() * 255;
		S_StartSound (-1, 0, cl_sfx_r_exp3, pos, 1, 1);
		break;

	case TE_LIGHTNING4:
		{
			// need to do it this way for correct parsing order
			char *modelname = MSG_ReadString ();

			ent = MSG_ReadShort ();
			
			start[0] = MSG_ReadCoord ();
			start[1] = MSG_ReadCoord ();
			start[2] = MSG_ReadCoord ();
			
			end[0] = MSG_ReadCoord ();
			end[1] = MSG_ReadCoord ();
			end[2] = MSG_ReadCoord ();

			CL_ParseBeam (Mod_ForName (modelname, true), ent, start, end);
		}

		break;

	case TE_NEW1:
		break;

	case TE_NEW2:
		break;

	case TE_PARTICLESNOW:	// general purpose particle effect
		pos[0] = MSG_ReadCoord ();
		pos[1] = MSG_ReadCoord ();
		pos[2] = MSG_ReadCoord ();

		pos2[0] = MSG_ReadCoord ();
		pos2[1] = MSG_ReadCoord ();
		pos2[2] = MSG_ReadCoord ();

		dir[0] = MSG_ReadCoord ();
		dir[1] = MSG_ReadCoord ();
		dir[2] = MSG_ReadCoord ();

		count = MSG_ReadShort (); // number of particles
		colorStart = MSG_ReadByte (); // color

		R_ParticleRain (pos, pos2, dir, count, colorStart, 1);
		break;

	case TE_PARTICLERAIN:	// general purpose particle effect
		pos[0] = MSG_ReadCoord ();
		pos[1] = MSG_ReadCoord ();
		pos[2] = MSG_ReadCoord ();

		pos2[0] = MSG_ReadCoord ();
		pos2[1] = MSG_ReadCoord ();
		pos2[2] = MSG_ReadCoord ();

		dir[0] = MSG_ReadCoord ();
		dir[1] = MSG_ReadCoord ();
		dir[2] = MSG_ReadCoord ();

		count = MSG_ReadShort (); // number of particles
		colorStart = MSG_ReadByte (); // color

		R_ParticleRain (pos, pos2, dir, count, colorStart, 0);
		break;

	default:
		// no need to crash the engine but we will crash the map, as it means we have
		// a malformed packet
		Con_DPrintf ("CL_ParseTEnt: bad type %i\n", type);

		// note - this might crash the server at some stage if more data is expected
		pos[0] = MSG_ReadCoord ();
		pos[1] = MSG_ReadCoord ();
		pos[2] = MSG_ReadCoord ();
		break;
	}
}


/*
=================
CL_NewTempEntity
=================
*/
entity_t *CL_NewTempEntity (void)
{
	entity_t	*ent;

	if (!Pool_TempEntities)
	{
		Pool_TempEntities = new CSpaceBuffer ("Temp Entities", 8, POOL_MAP);
		cl_temp_entities = (entity_t *) Pool_TempEntities->Alloc (1);
		Pool_TempEntities->Rewind ();
	}

	// begin a new sequence
	if (!num_temp_entities) Pool_TempEntities->Rewind ();

	// ensure space in the buffer
	Pool_TempEntities->Alloc (sizeof (entity_t));

	// alloc this temp entity
	ent = &cl_temp_entities[num_temp_entities];
	memset (ent, 0, sizeof (*ent));

	num_temp_entities++;
	ent->colormap = vid.colormap;
	return ent;
}


/*
=================
CL_UpdateTEnts
=================
*/
void CL_UpdateTEnts (void)
{
	int			i;
	beam_t		*b;
	vec3_t		dist, org;
	float		d;
	entity_t	*ent;
	float		yaw, pitch;
	float		forward;

	// hack - cl.time goes to 0 before some maps are fully flushed which can cause invalid
	// beam entities to be added to the list, so need to test for that (this can cause
	// crashes on maps where you give yourself the lightning gun and then issue a changelevel)
	if (cl.time < 0.1) return;

	num_temp_entities = 0;

	// update lightning
	for (i = 0, b = cl_beams; i < num_beams; i++, b++)
	{
		if (!b->model || b->endtime < cl.time)
			continue;

		// if coming from the player, update the start position
		if (b->entity == cl.viewentity)
			VectorCopy (cl_entities[cl.viewentity]->origin, b->start);

		// calculate pitch and yaw
		VectorSubtract (b->end, b->start, dist);

		if (dist[1] == 0 && dist[0] == 0)
		{
			yaw = 0;

			if (dist[2] > 0)
				pitch = 90;
			else pitch = 270;
		}
		else
		{
			yaw = (int) (atan2(dist[1], dist[0]) * 180 / M_PI);

			if (yaw < 0) yaw += 360;
	
			forward = sqrt (dist[0] * dist[0] + dist[1] * dist[1]);
			pitch = (int) (atan2 (dist[2], forward) * 180 / M_PI);

			if (pitch < 0) pitch += 360;
		}

		// add new entities for the lightning
		VectorCopy (b->start, org);
		d = VectorNormalize (dist);

		while (d > 0)
		{
			if (!(ent = CL_NewTempEntity ()))
				return;

			VectorCopy (org, ent->origin);
			ent->model = b->model;
			ent->alphaval = 255;
			ent->angles[0] = pitch;
			ent->angles[1] = yaw;
			ent->angles[2] = rand () % 360;

			// spotted by metlslime - inner loop used i as well!
			for (int j = 0; j < 3; j++)
			{
				org[j] += dist[j] * 30;
				ent->origin[j] = org[j];
			}

			// never occlude these wee buggers
			ent->effects |= EF_NEVEROCCLUDE;

			// add a visedict for it
			D3D_AddVisEdict (ent);

			d -= 30;
		}
	}

	// if (num_beams) Con_Printf ("%i beams\n", num_beams);
}


