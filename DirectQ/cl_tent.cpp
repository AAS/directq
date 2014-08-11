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
// cl_tent.c -- client side temporary entities

#include "quakedef.h"
#include "d3d_model.h"


typedef struct tempent_s
{
	entity_t *ent;
	struct tempent_s *next;
} tempent_t;

tempent_t	*cl_temp_entities = NULL;
tempent_t	*cl_free_tempents = NULL;

typedef struct beam_s
{
	int		entity;
	struct model_s	*model;
	float	endtime;
	vec3_t	start, end;
	int		type;
	struct beam_s *next;
} beam_t;

#define EXTRA_TEMPENTS 64

beam_t		*cl_beams;
beam_t		*cl_free_beams;

sfx_t			*cl_sfx_wizhit;
sfx_t			*cl_sfx_knighthit;
sfx_t			*cl_sfx_tink1;
sfx_t			*cl_sfx_ric1;
sfx_t			*cl_sfx_ric2;
sfx_t			*cl_sfx_ric3;
sfx_t			*cl_sfx_r_exp3;

cvar_t r_extradlight ("r_extradlight", "1", CVAR_ARCHIVE);

void D3D_AddVisEdict (entity_t *ent);

float anglestable[1024];

/*
=================
CL_InitTEnts
=================
*/
model_t	*cl_bolt1_mod = NULL;
model_t	*cl_bolt2_mod = NULL;
model_t	*cl_bolt3_mod = NULL;
model_t *cl_beam_mod = NULL;

void CL_InitTEnts (void)
{
	// set up the angles table
	for (int i = 0; i < 1024; i++)
		anglestable[i] = rand () % 360;

	// we need to load these too as models are being cleared between maps
	cl_bolt1_mod = Mod_ForName ("progs/bolt.mdl", true);
	cl_bolt2_mod = Mod_ForName ("progs/bolt2.mdl", true);
	cl_bolt3_mod = Mod_ForName ("progs/bolt3.mdl", true);

	// don't crash as this isn't in ID1
	cl_beam_mod = Mod_ForName ("progs/beam.mdl", false);

	// these models never get occlusion queries
	if (cl_bolt1_mod) cl_bolt1_mod->flags |= EF_NOOCCLUDE;
	if (cl_bolt2_mod) cl_bolt2_mod->flags |= EF_NOOCCLUDE;
	if (cl_bolt3_mod) cl_bolt3_mod->flags |= EF_NOOCCLUDE;
	if (cl_beam_mod) cl_beam_mod->flags |= EF_NOOCCLUDE;

	// sounds
	cl_sfx_wizhit = S_PrecacheSound ("wizard/hit.wav");
	cl_sfx_knighthit = S_PrecacheSound ("hknight/hit.wav");
	cl_sfx_tink1 = S_PrecacheSound ("weapons/tink1.wav");
	cl_sfx_ric1 = S_PrecacheSound ("weapons/ric1.wav");
	cl_sfx_ric2 = S_PrecacheSound ("weapons/ric2.wav");
	cl_sfx_ric3 = S_PrecacheSound ("weapons/ric3.wav");
	cl_sfx_r_exp3 = S_PrecacheSound ("weapons/r_exp3.wav");

	// ensure on each map load
	cl_beams = NULL;
	cl_free_beams = NULL;
	cl_temp_entities = NULL;
	cl_free_tempents = NULL;
}


/*
=================
CL_ParseBeam
=================
*/
void CL_ParseBeam (model_t *m, int ent, int type, vec3_t start, vec3_t end)
{
	// if the model didn't load just ignore it
	if (!m) return;

	beam_t *b;

	// override any beam with the same entity
	for (b = cl_beams; b; b = b->next)
	{
		if (b->entity == ent)
		{
			b->entity = ent;
			b->model = m;
			b->type = type;
			b->endtime = cl.time + 0.2f;
			VectorCopy (start, b->start);
			VectorCopy (end, b->end);
			return;
		}
	}

	// find a free beam
	if (!cl_free_beams)
	{
		cl_free_beams = (beam_t *) MainHunk->Alloc (EXTRA_TEMPENTS * sizeof (beam_t));

		for (int i = 1; i < EXTRA_TEMPENTS; i++)
		{
			cl_free_beams[i - 1].next = &cl_free_beams[i];
			cl_free_beams[i].next = NULL;
		}
	}

	// take the first free beam
	b = cl_free_beams;
	cl_free_beams = b->next;

	// link it in
	b->next = cl_beams;
	cl_beams = b;

	// set it's properties
	b->entity = ent;
	b->model = m;
	b->type = type;
	b->endtime = cl.time + 0.2f;
	VectorCopy (start, b->start);
	VectorCopy (end, b->end);
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
		pos[0] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		pos[1] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		pos[2] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);

		if (r_extradlight.value)
		{
			dl = CL_AllocDlight (0);
			VectorCopy (pos, dl->origin);
			dl->radius = 250;
			dl->die = cl.time + 0.5f;
			dl->decay = 300;

			R_ColourDLight (dl, 308, 351, 109);
		}

		R_WallHitParticles (pos, vec3_origin, 20, 30);
		S_StartSound (-1, 0, cl_sfx_wizhit, pos, 1, 1);
		break;

	case TE_KNIGHTSPIKE:			// spike hitting wall
		pos[0] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		pos[1] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		pos[2] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);

		if (r_extradlight.value)
		{
			dl = CL_AllocDlight (0);
			VectorCopy (pos, dl->origin);
			dl->radius = 250;
			dl->die = cl.time + 0.5f;
			dl->decay = 300;

			R_ColourDLight (dl, 408, 242, 117);
		}

		R_WallHitParticles (pos, vec3_origin, 226, 20);
		S_StartSound (-1, 0, cl_sfx_knighthit, pos, 1, 1);
		break;

	case TE_SPIKE:			// spike hitting wall
		pos[0] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		pos[1] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		pos[2] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);

		R_WallHitParticles (pos, vec3_origin, 0, 10);

		if (rand() % 5)
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
		pos[0] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		pos[1] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		pos[2] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		R_WallHitParticles (pos, vec3_origin, 0, 20);

		if (rand() % 5)
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
		pos[0] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		pos[1] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		pos[2] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		if (!kurok) R_WallHitParticles (pos, vec3_origin, 0, 20);
		break;

	case TE_EXPLOSION:			// rocket explosion
		pos[0] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		pos[1] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		pos[2] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		R_ParticleExplosion (pos);

		dl = CL_AllocDlight (0);
		VectorCopy (pos, dl->origin);
		dl->radius = 350;
		dl->die = cl.time + 0.5f;
		dl->decay = 300;

		if (kurok)
		{
			float tempcolor[3];

        	tempcolor[0] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
			tempcolor[1] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
			tempcolor[2] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);

			// directq scales dlights slightly different
			R_ColourDLight (dl, tempcolor[0] * 384, tempcolor[1] * 384, tempcolor[2] * 384);
		}
		else R_ColourDLight (dl, 408, 242, 117);

		S_StartSound (-1, 0, cl_sfx_r_exp3, pos, 1, 1);
		break;

	case TE_TAREXPLOSION:			// tarbaby explosion
		pos[0] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		pos[1] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		pos[2] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		R_BlobExplosion (pos);

		if (kurok)
		{
			float tempcolor[3];

			dl = CL_AllocDlight (0);
			VectorCopy (pos, dl->origin);
			dl->radius = 150;
			dl->die = cl.time + 0.75;
			dl->decay = 200;

			tempcolor[0] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
			tempcolor[1] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
			tempcolor[2] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);

			// directq scales dlights slightly different
			R_ColourDLight (dl, tempcolor[0] * 384, tempcolor[1] * 384, tempcolor[2] * 384);
		}
		else if (r_extradlight.value)
		{
			dl = CL_AllocDlight (0);
			VectorCopy (pos, dl->origin);
			dl->radius = 350;
			dl->die = cl.time + 0.5f;
			dl->decay = 300;

			R_ColourDLight (dl, 399, 141, 228);
		}

		S_StartSound (-1, 0, cl_sfx_r_exp3, pos, 1, 1);
		break;

	case TE_LIGHTNING1:				// lightning bolts
		ent = MSG_ReadShort ();

		start[0] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		start[1] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		start[2] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);

		end[0] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		end[1] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		end[2] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);

		CL_ParseBeam (cl_bolt1_mod, ent, TE_LIGHTNING1, start, end);
		break;

	case TE_LIGHTNING2:				// lightning bolts
		ent = MSG_ReadShort ();

		start[0] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		start[1] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		start[2] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);

		end[0] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		end[1] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		end[2] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);

		CL_ParseBeam (cl_bolt2_mod, ent, TE_LIGHTNING2, start, end);
		break;

	case TE_LIGHTNING3:				// lightning bolts
		ent = MSG_ReadShort ();

		start[0] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		start[1] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		start[2] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);

		end[0] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		end[1] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		end[2] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);

		CL_ParseBeam (cl_bolt3_mod, ent, TE_LIGHTNING3, start, end);
		break;

		// PGM 01/21/97
	case TE_BEAM:				// grappling hook beam
		ent = MSG_ReadShort ();

		start[0] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		start[1] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		start[2] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);

		end[0] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		end[1] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		end[2] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);

		CL_ParseBeam (cl_beam_mod, ent, TE_BEAM, start, end);
		break;
		// PGM 01/21/97

	case TE_LAVASPLASH:
		pos[0] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		pos[1] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		pos[2] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		R_LavaSplash (pos);
		break;

	case TE_TELEPORT:
		pos[0] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		pos[1] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		pos[2] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		R_TeleportSplash (pos);
		break;

	case TE_EXPLOSION2:				// color mapped explosion
		pos[0] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		pos[1] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		pos[2] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		colorStart = MSG_ReadByte ();
		colorLength = MSG_ReadByte ();
		R_ParticleExplosion2 (pos, colorStart, colorLength);
		dl = CL_AllocDlight (0);
		VectorCopy (pos, dl->origin);
		dl->radius = 350;
		dl->die = cl.time + 0.5f;
		dl->decay = 300;
		S_StartSound (-1, 0, cl_sfx_r_exp3, pos, 1, 1);
		break;

	case TE_SMOKE:
		// falls through to explosion 3
		pos[0] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		pos[1] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		pos[2] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		MSG_ReadByte();

	case TE_EXPLOSION3:
		pos[0] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		pos[1] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		pos[2] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		dl = CL_AllocDlight (0);
		VectorCopy (pos, dl->origin);
		dl->radius = 350;
		dl->die = cl.time + 0.5f;
		dl->decay = 300;
		dl->rgb[0] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags) * 255;
		dl->rgb[1] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags) * 255;
		dl->rgb[2] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags) * 255;
		S_StartSound (-1, 0, cl_sfx_r_exp3, pos, 1, 1);
		break;

	case TE_LIGHTNING4:
	{
		// need to do it this way for correct parsing order
		char *modelname = MSG_ReadString ();

		ent = MSG_ReadShort ();

		start[0] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		start[1] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		start[2] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);

		end[0] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		end[1] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		end[2] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);

		CL_ParseBeam (Mod_ForName (modelname, true), ent, TE_LIGHTNING4, start, end);
	}

	break;

	case TE_NEW1:
		break;

	case TE_NEW2:
		break;

	case TE_PARTICLESNOW:	// general purpose particle effect
		pos[0] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		pos[1] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		pos[2] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);

		pos2[0] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		pos2[1] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		pos2[2] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);

		dir[0] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		dir[1] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		dir[2] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);

		count = MSG_ReadShort (); // number of particles
		colorStart = MSG_ReadByte (); // color

		R_ParticleRain (pos, pos2, dir, count, colorStart, 1);
		break;

	case TE_PARTICLERAIN:	// general purpose particle effect
		pos[0] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		pos[1] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		pos[2] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);

		pos2[0] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		pos2[1] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		pos2[2] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);

		dir[0] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		dir[1] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		dir[2] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);

		count = MSG_ReadShort (); // number of particles
		colorStart = MSG_ReadByte (); // color

		R_ParticleRain (pos, pos2, dir, count, colorStart, 0);
		break;

	default:
		// no need to crash the engine but we will crash the map, as it means we have
		// a malformed packet
		Con_DPrintf ("CL_ParseTEnt: bad type %i\n", type);

		// note - this might crash the server at some stage if more data is expected
		pos[0] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		pos[1] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		pos[2] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
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
	tempent_t *tempent;

	if (!cl_free_tempents)
	{
		// alloc a new batch of free temp entities if required
		cl_free_tempents = (tempent_t *) MainHunk->Alloc (sizeof (tempent_t) * EXTRA_TEMPENTS);

		for (int i = 2; i < EXTRA_TEMPENTS - 1; i++)
			cl_free_tempents[i - 2].next = &cl_free_tempents[i - 1];

		cl_free_tempents[EXTRA_TEMPENTS - 1].next = NULL;
	}

	// unlink from free
	tempent = cl_free_tempents;
	cl_free_tempents = tempent->next;

	// link to active
	tempent->next = cl_temp_entities;
	cl_temp_entities = tempent;

	if (!tempent->ent)
	{
		// alloc a new entity_t (tempents never get occlusion queries)
		tempent->ent = CL_AllocEntity ();
		tempent->ent->allocnum = 0;
	}
	else
	{
		// clear the entity (tempents never get occlusion queries)
		memset (tempent->ent, 0, sizeof (entity_t));
	}

	// done
	return tempent->ent;
}


/*
=================
CL_UpdateTEnts
=================
*/
void CL_UpdateTEnts (void)
{
	vec3_t	    dist, org;
	entity_t    *ent;
	float	    yaw, pitch;
	float	    forward;

	// hack - cl.time goes to 0 before some maps are fully flushed which can cause invalid
	// beam entities to be added to the list, so need to test for that (this can cause
	// crashes on maps where you give yourself the lightning gun and then issue a changelevel)
	// hmmm - i think this one was more like i being used in both the inner and outer loops.
	if (cl.time < 0.1f) return;

	// no beams while a server is paused (if running locally)
	if (sv.active && sv.paused) return;

	// move all tempents back to the free list
	for (;;)
	{
		tempent_t *kill = cl_temp_entities;

		if (kill)
		{
			cl_temp_entities = kill->next;

			kill->next = cl_free_tempents;
			cl_free_tempents = kill;
			continue;
		}

		// no temp entities to begin with
		cl_temp_entities = NULL;
		break;
	}

	for (;;)
	{
		beam_t *kill = cl_beams;

		if (kill && (!kill->model || kill->endtime < cl.time))
		{
			cl_beams = kill->next;
			kill->next = cl_free_beams;
			cl_free_beams = kill;
			continue;
		}

		break;
	}

	if (!cl_beams) return;

	// update lightning
	for (beam_t *b = cl_beams; b; b = b->next)
	{
		for (;;)
		{
			beam_t *kill = b->next;

			if (kill && (!kill->model || kill->endtime < cl.time))
			{
				b->next = kill->next;
				kill->next = cl_free_beams;
				cl_free_beams = kill;
				continue;
			}

			break;
		}

		// is this needed any more???
		if (!b->model || b->endtime < cl.time) continue;

		// if coming from the player, update the start position
		if (b->entity == cl.viewentity)
			VectorCopy (cl_entities[cl.viewentity]->origin, b->start);

		// calculate pitch and yaw
		VectorSubtract (b->end, b->start, dist);

		if (dist[1] == 0 && dist[0] == 0)
		{
			// linear so pythagoras can have his coffee break
			yaw = 0;

			if (dist[2] > 0)
				pitch = 90;
			else pitch = 270;
		}
		else
		{
			yaw = (int) (atan2 (dist[1], dist[0]) * 180 / D3DX_PI);

			if (yaw < 0) yaw += 360;

			forward = sqrt (dist[0] * dist[0] + dist[1] * dist[1]);
			pitch = (int) (atan2 (dist[2], forward) * 180 / D3DX_PI);

			if (pitch < 0) pitch += 360;
		}

		// add new entities for the lightning
		VectorCopy (b->start, org);
		float d = VectorNormalize (dist);
		int dlstep = 0;
		dlight_t *dl;

		while (d > 0)
		{
			if (!(ent = CL_NewTempEntity ()))
				break;

			ent->model = b->model;
			ent->alphaval = 255;
			ent->angles[0] = pitch;
			ent->angles[1] = yaw;
			ent->angles[2] = anglestable[(int) ((cl.time * 15) + d) & 1023];

			// i is no longer on the outer loop
			for (int i = 0; i < 3; i++)
			{
				org[i] += dist[i] * 30;
				ent->origin[i] = org[i];
			}

			if (r_extradlight.value && ((++dlstep) > 8))
			{
				switch (b->type)
				{
				case TE_LIGHTNING1:
				case TE_LIGHTNING2:
				case TE_LIGHTNING3:
				case TE_LIGHTNING4:
					dl = CL_AllocDlight (0);

					VectorCopy (org, dl->origin);
					dl->radius = 250;
					dl->die = cl.time + 0.1f;
					dl->decay = 300;
					dl->flags |= (DLF_NOCORONA | DLF_KILL);

					R_ColourDLight (dl, 64, 256, 512);
					break;

				default: break;
				}

				dlstep = 0;
			}

			// add a visedict for it
			D3D_AddVisEdict (ent);

			d -= 30;
		}
	}
}


