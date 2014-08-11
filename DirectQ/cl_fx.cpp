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

#include "quakedef.h"
#include "d3d_model.h"
#include "cl_fx.h"

// spawn extra dynamic lights
cvar_t r_extradlight ("r_extradlight", "1", CVAR_ARCHIVE);

// the rate at which dynamic client-side effects are updated
cvar_t cl_effectrate ("cl_effectrate", 36.0f, CVAR_ARCHIVE);

// flickering (muzzleflash, brightlight, dimlight) effect rates
cvar_t cl_flickerrate ("cl_flickerrate", 10, CVAR_ARCHIVE);

// prevent wild flickering when running fast; this also keeps random effects synced with time which means they won't flicker when paused
#define FLICKERTABLE_SIZE	4096

int flickertable[FLICKERTABLE_SIZE];


void CL_InitFX (void)
{
	for (int i = 0; i < FLICKERTABLE_SIZE; i++)
		flickertable[i] = rand ();
}


dlight_t *CL_FindDlight (int key)
{
	dlight_t *dl = NULL;
	float lowdie = 999999999;
	int lownum = 0;

	// first look for an exact key match
	if (key)
	{
		dl = cl_dlights;

		for (int i = 0; i < MAX_DLIGHTS; i++, dl++)
		{
			if (dl->key == key)
			{
				memset (dl, 0, sizeof (*dl));
				dl->key = key;
				return dl;
			}
		}
	}

	// then look for anything else
	dl = cl_dlights;

	for (int i = 0; i < MAX_DLIGHTS; i++, dl++)
	{
		if (dl->die < lowdie)
		{
			// mark the one that will die soonest
			lowdie = dl->die;
			lownum = i;
		}

		if (dl->die < cl.time)
		{
			// first one that's died can go
			memset (dl, 0, sizeof (*dl));
			dl->key = key;
			return dl;
		}
	}

	// replace the one that's going to die soonest
	dl = &cl_dlights[lownum];
	memset (dl, 0, sizeof (*dl));
	dl->key = key;
	return dl;
}


void CL_ColourDlight (dlight_t *dl, unsigned short r, unsigned short g, unsigned short b)
{
	dl->rgb[0] = r;
	dl->rgb[1] = g;
	dl->rgb[2] = b;
}


dlight_t *CL_AllocDlight (int key)
{
	// find a dlight to use
	dlight_t *dl = CL_FindDlight (key);

	// set default colour for any we don't colour ourselves
	CL_ColourDlight (dl, DL_COLOR_WHITE);

	// initial dirty state is dirty
	dl->dirty = true;
	dl->dirtytime = cl.time;

	// other defaults
	dl->die = cl.time + 0.1f;
	dl->flags = 0;

	// done
	return dl;
}


void CL_DecayLights (void)
{
	dlight_t *dl = cl_dlights;
	double dl_dirtytime = (cl_effectrate.value > 0) ? (1.0 / cl_effectrate.value) : 0;

	for (int i = 0; i < MAX_DLIGHTS; i++, dl++)
	{
		// ensure that one frame flags get an update
		if (dl->flags & DLF_KILL) dl->die = cl.time + 1;
		if (dl->die < cl.time || (dl->radius <= 0)) continue;

		if (cl.time >= (dl->dirtytime + dl_dirtytime))
		{
			dl->radius -= (cl.time - dl->dirtytime) * dl->decay;
			dl->dirty = true;
			dl->dirtytime = cl.time;

			// DLF_KILL is handled here to ensure that the light lasts for at least one frame
			if (dl->radius < 0 || (dl->flags & DLF_KILL))
			{
				dl->flags &= ~DLF_KILL;
				dl->radius = 0;
				dl->die = -1;
			}

			// testing
			// Con_Printf ("light %i is dirty\n", i);
		}
		else
		{
			// testing
			// Con_Printf ("clean %i\n", i);
			dl->dirty = false;
		}
	}
}


void CL_MuzzleFlash (entity_t *ent, int entnum)
{
	avectors_t av;
	dlight_t *dl;

	dl = CL_AllocDlight (entnum);
	dl->flags |= DLF_NOCORONA;

	if (entnum == cl.viewentity)
	{
		// switch the flash colour for the current weapon
		if (cl.stats[STAT_ACTIVEWEAPON] == IT_SUPER_LIGHTNING)
			CL_ColourDlight (dl, DL_COLOR_BLUE);
		else if (cl.stats[STAT_ACTIVEWEAPON] == IT_LIGHTNING)
			CL_ColourDlight (dl, DL_COLOR_BLUE);
		else CL_ColourDlight (dl, DL_COLOR_ORANGE);
	}
	else
	{
		// some entities have different attacks resulting in a different flash colour
		if ((ent->model->flags & EF_WIZARDFLASH) || (ent->model->flags & EF_GREENFLASH))
			CL_ColourDlight (dl, DL_COLOR_GREEN);
		else if ((ent->model->flags & EF_SHALRATHFLASH) || (ent->model->flags & EF_PURPLEFLASH))
			CL_ColourDlight (dl, DL_COLOR_PURPLE);
		else if ((ent->model->flags & EF_SHAMBLERFLASH) || (ent->model->flags & EF_BLUEFLASH))
			CL_ColourDlight (dl, DL_COLOR_BLUE);
		else if (ent->model->flags & EF_ORANGEFLASH)
			CL_ColourDlight (dl, DL_COLOR_ORANGE);
		else if (ent->model->flags & EF_REDFLASH)
			CL_ColourDlight (dl, DL_COLOR_RED);
		else if (ent->model->flags & EF_YELLOWFLASH)
			CL_ColourDlight (dl, DL_COLOR_YELLOW);
		else CL_ColourDlight (dl, DL_COLOR_ORANGE);
	}

	VectorCopy2 (dl->origin, ent->origin);
	dl->origin[2] += 16;
	AngleVectors (ent->angles, &av);

	dl->origin[0] = av.forward[0] * 18 + dl->origin[0];
	dl->origin[1] = av.forward[1] * 18 + dl->origin[1];
	dl->origin[2] = av.forward[2] * 18 + dl->origin[2];
	dl->radius = 200 + (flickertable[(entnum + (int) (cl.time * cl_flickerrate.value)) & (FLICKERTABLE_SIZE - 1)] & 31);

	// the server clears muzzleflashes after each frame, but as the client is now running faster, it won't get the message for several
	// frames - potentially over 10.  therefore we should also clear the flash on the client too.  this also fixes demos ;)
	ent->effects &= ~EF_MUZZLEFLASH;
}


void CL_BrightLight (entity_t *ent, int entnum)
{
	// uncoloured
	dlight_t *dl = CL_AllocDlight (entnum);

	VectorCopy2 (dl->origin, ent->origin);
	dl->origin[2] += 16;
	dl->radius = 400 + (flickertable[(entnum + (int) (cl.time * cl_flickerrate.value)) & (FLICKERTABLE_SIZE - 1)] & 31);
	dl->flags = DLF_NOCORONA | DLF_KILL;
}


void CL_DimLight (entity_t *ent, int entnum)
{
	dlight_t *dl = CL_AllocDlight (entnum);

	VectorCopy2 (dl->origin, ent->origin);
	dl->radius = 200 + (flickertable[(entnum + (int) (cl.time * cl_flickerrate.value)) & (FLICKERTABLE_SIZE - 1)] & 31);
	dl->flags = DLF_KILL;

	// powerup dynamic lights
	if (entnum == cl.viewentity)
	{
		// if it's a powerup coming from the viewent then we remove the corona
		dl->flags |= DLF_NOCORONA;

		// and set the appropriate colour depending on the current powerup(s)
		if ((cl.items & IT_QUAD) && (cl.items & IT_INVULNERABILITY))
			CL_ColourDlight (dl, DL_COLOR_PURPLE);
		else if (cl.items & IT_QUAD)
			CL_ColourDlight (dl, DL_COLOR_BLUE);
		else if (cl.items & IT_INVULNERABILITY)
			CL_ColourDlight (dl, DL_COLOR_RED);
		else CL_ColourDlight (dl, DL_COLOR_WHITE);
	}
}


void CL_WizardTrail (entity_t *ent, int entnum)
{
	if (r_extradlight.value)
	{
		dlight_t *dl = CL_AllocDlight (entnum);

		VectorCopy2 (dl->origin, ent->origin);
		dl->radius = 200;
		dl->flags |= DLF_KILL;

		CL_ColourDlight (dl, DL_COLOR_GREEN);
	}

	R_RocketTrail (ent->oldorg, ent->origin, RT_WIZARD);
}


void CL_KnightTrail (entity_t *ent, int entnum)
{
	if (r_extradlight.value)
	{
		dlight_t *dl = CL_AllocDlight (entnum);

		VectorCopy2 (dl->origin, ent->origin);
		dl->radius = 200;
		dl->flags |= DLF_KILL;

		CL_ColourDlight (dl, DL_COLOR_ORANGE);
	}

	R_RocketTrail (ent->oldorg, ent->origin, RT_KNIGHT);
}


void CL_RocketTrail (entity_t *ent, int entnum)
{
	dlight_t *dl = CL_AllocDlight (entnum);

	VectorCopy2 (dl->origin, ent->origin);
	dl->radius = 200;
	dl->flags |= DLF_KILL;

	CL_ColourDlight (dl, DL_COLOR_ORANGE);

	R_RocketTrail (ent->oldorg, ent->origin, RT_ROCKET);
}


void CL_VoreTrail (entity_t *ent, int entnum)
{
	if (r_extradlight.value)
	{
		dlight_t *dl = CL_AllocDlight (entnum);

		VectorCopy2 (dl->origin, ent->origin);
		dl->radius = 200;
		dl->flags |= DLF_KILL;

		CL_ColourDlight (dl, DL_COLOR_PURPLE);
	}

	R_RocketTrail (ent->oldorg, ent->origin, RT_VORE);
}


// old cl_tent stuff; appropriate here

#include <vector>

typedef struct beam_s
{
	int		entity;
	struct model_s	*model;
	float	endtime;
	vec3_t	start, end;
	int		type;
} beam_t;

std::vector<beam_t *> cl_beams;
std::vector<entity_t *> cl_tempentities;
int cl_numtempentities;

sfx_t			*cl_sfx_wizhit;
sfx_t			*cl_sfx_knighthit;
sfx_t			*cl_sfx_tink1;
sfx_t			*cl_sfx_ric1;
sfx_t			*cl_sfx_ric2;
sfx_t			*cl_sfx_ric3;
sfx_t			*cl_sfx_r_exp3;

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
	cl_beams.clear ();
	cl_tempentities.clear ();
	cl_numtempentities = 0;
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

	beam_t *b = NULL;

	// override any beam with the same entity
	for (int i = 0; i < cl_beams.size (); i++)
	{
		b = cl_beams[i];

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
	for (int i = 0; i < cl_beams.size (); i++)
	{
		b = cl_beams[i];

		if (!b->model || b->endtime < cl.time)
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

	// alloc a new beam
	b = (beam_t *) ClientZone->Alloc (sizeof (beam_t));
	cl_beams.push_back (b);
	b = cl_beams[cl_beams.size () - 1];

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

			CL_ColourDlight (dl, DL_COLOR_GREEN);
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

			CL_ColourDlight (dl, DL_COLOR_ORANGE);
		}

		R_WallHitParticles (pos, vec3_origin, 226, 20);
		S_StartSound (-1, 0, cl_sfx_knighthit, pos, 1, 1);
		break;

	case TE_SPIKE:			// spike hitting wall
		pos[0] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		pos[1] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		pos[2] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);

		R_WallHitParticles (pos, vec3_origin, 0, 10);

		if (rand () % 5)
			S_StartSound (-1, 0, cl_sfx_tink1, pos, 1, 1);
		else
		{
			rnd = rand () & 3;

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

		if (rand () % 5)
			S_StartSound (-1, 0, cl_sfx_tink1, pos, 1, 1);
		else
		{
			rnd = rand () & 3;

			if (rnd == 1)
				S_StartSound (-1, 0, cl_sfx_ric1, pos, 1, 1);
			else if (rnd == 2)
				S_StartSound (-1, 0, cl_sfx_ric2, pos, 1, 1);
			else S_StartSound (-1, 0, cl_sfx_ric3, pos, 1, 1);
		}

		break;

	case TE_GUNSHOT:			// bullet hitting wall
		pos[0] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		pos[1] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		pos[2] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);

		R_WallHitParticles (pos, vec3_origin, 0, 20);
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
		CL_ColourDlight (dl, DL_COLOR_ORANGE);

		S_StartSound (-1, 0, cl_sfx_r_exp3, pos, 1, 1);
		break;

	case TE_TAREXPLOSION:			// tarbaby explosion
		pos[0] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		pos[1] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		pos[2] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);

		R_BlobExplosion (pos);

		if (r_extradlight.value)
		{
			dl = CL_AllocDlight (0);
			VectorCopy (pos, dl->origin);
			dl->radius = 350;
			dl->die = cl.time + 0.5f;
			dl->decay = 300;

			CL_ColourDlight (dl, DL_COLOR_PURPLE);
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
	entity_t *ent = NULL;

	if (cl_numtempentities == cl_tempentities.size ())
	{
		ent = (entity_t *) ClientZone->Alloc (sizeof (entity_t));
		cl_tempentities.push_back (ent);
	}

	ent = cl_tempentities[cl_numtempentities];
	memset (ent, 0, sizeof (entity_t));
	cl_numtempentities++;

	return ent;
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

	// reset tempents for this frame
	cl_numtempentities = 0;

	if (!cl_beams.size ()) return;

	// update lightning
	for (int beamnum = 0; beamnum < cl_beams.size (); beamnum++)
	{
		beam_t *b = cl_beams[beamnum];

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
			ent = CL_NewTempEntity ();

			ent->model = b->model;
			ent->alphaval = 0;
			ent->angles[0] = pitch;
			ent->angles[1] = yaw;
			ent->angles[2] = anglestable[(int) ((cl.time * cl_effectrate.value) + d) & 1023];

			// i is no longer on the outer loop
			for (int i = 0; i < 3; i++)
			{
				org[i] += dist[i] * 30;
				ent->origin[i] = org[i];
			}

			if (r_extradlight.value && ((++dlstep) > 8) && cl.time >= cl.nexteffecttime)
			{
				switch (b->type)
				{
				case TE_LIGHTNING1:
				case TE_LIGHTNING2:
				case TE_LIGHTNING3:
				case TE_LIGHTNING4:
					dl = CL_AllocDlight (0);

					VectorCopy (org, dl->origin);
					dl->radius = 250 + (flickertable[(int) (d + (cl.time * cl_flickerrate.value)) & (FLICKERTABLE_SIZE - 1)] & 31);
					dl->flags |= (DLF_NOCORONA | DLF_KILL);

					CL_ColourDlight (dl, DL_COLOR_BLUE);
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


