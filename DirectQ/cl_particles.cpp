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

// contains all particle code which is actually spawned from the client, i.e. is not really a part of the renderer

#include "quakedef.h"
#include "d3d_model.h"
#include "d3d_quake.h"
#include "resource.h"
#include "particles.h"

particle_t	*free_particles;
particle_type_t *active_particle_types, *free_particle_types;

// particle colour ramps - the extra values of -1 at the end of each are to protect us if the engine locks for a few seconds
int		ramp1[] = {0x6f, 0x6d, 0x6b, 0x69, 0x67, 0x65, 0x63, 0x61, -1, -1, -1, -1, -1};
int		ramp2[] = {0x6f, 0x6e, 0x6d, 0x6c, 0x6b, 0x6a, 0x68, 0x66, -1, -1, -1, -1, -1};
int		ramp3[] = {0x6d, 0x6b, 6, 5, 4, 3, -1, -1, -1, -1, -1};

int	r_numparticles;
int r_numallocations;

particle_t	r_defaultparticle;

// these were never used in the alias render
#define NUMVERTEXNORMALS	162

float r_avertexnormals[NUMVERTEXNORMALS][3] =
{
#include "anorms.h"
};

extern cvar_t sv_gravity;

/*
===============
R_InitParticles
===============
*/
void R_InitParticles (void)
{
	// setup default particle state
	// we need to explicitly set stuff to 0 as memsetting to 0 doesn't
	// actually equate to a value of 0 with floating point
	r_defaultparticle.scale = 2.666f;
	r_defaultparticle.alpha = 1.0f;
	r_defaultparticle.fade = 0;
	r_defaultparticle.growth = 0;

	// default flags
	r_defaultparticle.flags = PT_STATIC;

	// default velocity change and gravity
	r_defaultparticle.dvel[0] = r_defaultparticle.dvel[1] = r_defaultparticle.dvel[2] = 0;
	r_defaultparticle.grav = 0;

	// colour and ramps
	r_defaultparticle.colorramp = NULL;
	r_defaultparticle.color = 0;
	r_defaultparticle.ramp = 0;
	r_defaultparticle.ramptime = 0;
}


/*
===============
R_ClearParticles
===============
*/
void R_ClearParticles (void)
{
	int i;

	// particles
	free_particles = (particle_t *) ClientZone->Alloc (PARTICLE_BATCH_SIZE * sizeof (particle_t));

	for (i = 1; i < PARTICLE_BATCH_SIZE; i++)
	{
		free_particles[i - 1].next = &free_particles[i];
		free_particles[i].next = NULL;
	}

	// particle type chains
	free_particle_types = (particle_type_t *) ClientZone->Alloc (PARTICLE_TYPE_BATCH_SIZE * sizeof (particle_type_t));
	active_particle_types = NULL;

	for (i = 1; i < PARTICLE_TYPE_BATCH_SIZE; i++)
	{
		free_particle_types[i - 1].next = &free_particle_types[i];
		free_particle_types[i].next = NULL;
	}

	// track the number of particles
	r_numparticles = 0;

	// no allocations have been done yet
	r_numallocations = 0;
}


particle_type_t *R_NewParticleType (vec3_t spawnorg)
{
	particle_type_t *pt;
	int i;

	if (free_particle_types)
	{
		// just take from the free list
		pt = free_particle_types;
		free_particle_types = pt->next;

		// no particles yet
		pt->particles = NULL;
		pt->numparticles = 0;

		// copy across origin
		VectorCopy2 (pt->spawnorg, spawnorg);

		// link it in
		pt->next = active_particle_types;
		active_particle_types = pt;

		// done
		return pt;
	}

	// alloc some more free particles
	free_particle_types = (particle_type_t *) ClientZone->Alloc (PARTICLE_TYPE_EXTRA_SIZE * sizeof (particle_type_t));

	// link them up
	for (i = 0; i < PARTICLE_TYPE_EXTRA_SIZE; i++)
		free_particle_types[i].next = &free_particle_types[i + 1];

	// finish the link
	free_particle_types[PARTICLE_TYPE_EXTRA_SIZE - 1].next = NULL;

	// call recursively to return the first new free particle type
	return R_NewParticleType (spawnorg);
}


particle_t *R_NewParticle (particle_type_t *pt)
{
	particle_t *p;
	int i;

	if (free_particles)
	{
		// just take from the free list
		p = free_particles;
		free_particles = p->next;

		// set default drawing parms (may be overwritten as desired)
		memcpy (p, &r_defaultparticle, sizeof (particle_t));

		// link it in
		p->next = pt->particles;
		pt->particles = p;
		pt->numparticles++;

		// track the number of particles we have
		r_numparticles++;

		// done
		return p;
	}

	// alloc some more free particles
	free_particles = (particle_t *) ClientZone->Alloc (PARTICLE_EXTRA_SIZE * sizeof (particle_t));

	// link them up
	for (i = 0; i < PARTICLE_EXTRA_SIZE; i++)
		free_particles[i].next = &free_particles[i + 1];

	// finish the link
	free_particles[PARTICLE_EXTRA_SIZE - 1].next = NULL;

	// call recursively to return the first new free particle
	return R_NewParticle (pt);
}


/*
===============
R_EntityParticles
===============
*/

vec3_t	avelocities[NUMVERTEXNORMALS];
float	beamlength = 16;
vec3_t	avelocity = {23, 7, 3};
float	partstep = 0.01;
float	timescale = 0.01;

void R_EntityParticles (entity_t *ent)
{
	if (key_dest != key_game) return;

	particle_t	*p;
	float		angle;
	float		sp, sy, cp, cy;
	vec3_t		forward;
	float		dist;

	dist = 64;

	if (!avelocities[0][0])
		for (int i = 0; i < NUMVERTEXNORMALS * 3; i++)
			avelocities[0][i] = (rand () & 255) * 0.01;

	particle_type_t *pt = R_NewParticleType (ent->origin);

	for (int i = 0; i < NUMVERTEXNORMALS; i++)
	{
		angle = cl.time * avelocities[i][0];
		sy = sin (angle);
		cy = cos (angle);
		angle = cl.time * avelocities[i][1];
		sp = sin (angle);
		cp = cos (angle);

		forward[0] = cp * cy;
		forward[1] = cp * sy;
		forward[2] = -sp;

		if (!(p = R_NewParticle (pt))) return;

		// fixme - these particles should be automatically killed after 1 frame
		p->die = cl.time + 0.01f;
		p->color = 0x6f;
		p->colorramp = ramp1;
		p->ramptime = 10;
		p->grav = -1;

		p->dvel[0] = p->dvel[1] = p->dvel[2] = 4;

		p->org[0] = ent->origin[0] + r_avertexnormals[i][0] * dist + forward[0] * beamlength;
		p->org[1] = ent->origin[1] + r_avertexnormals[i][1] * dist + forward[1] * beamlength;
		p->org[2] = ent->origin[2] + r_avertexnormals[i][2] * dist + forward[2] * beamlength;
	}
}


void R_ReadPointFile_f (void)
{
	// fixme - draw as a line list...
	FILE	*f;
	vec3_t	org;
	int		r;
	int		c;
	particle_t	*p;
	char	name[MAX_PATH];

	_snprintf (name, 128, "%s/maps/%s.pts", com_gamedir, sv.name);

	// we don't expect that these will ever be in PAKs
	f = fopen (name, "rb");

	if (!f)
	{
		Con_Printf ("couldn't open %s\n", name);
		return;
	}

	Con_Printf ("Reading %s...\n", name);
	c = 0;

	particle_type_t *pt = R_NewParticleType (vec3_origin);

	for (;;)
	{
		r = fscanf (f, "%f %f %f\n", &org[0], &org[1], &org[2]);

		if (r != 3)
			break;

		c++;

		if (!(p = R_NewParticle (pt)))
		{
			// woah!  this was a return!  NOT clever!
			Con_Printf ("Pointfile overflow - ");
			break;
		}

		p->die = cl.time + 999999;
		p->color = (-c) & 15;
		p->flags |= PT_NOCLIP;
		p->color = 251;	// let's make pointfiles more visible
		p->scale = 5;
		VectorCopy2 (p->vel, vec3_origin);
		VectorCopy2 (p->org, org);
	}

	fclose (f);
	Con_Printf ("%i points read\n", c);
}


/*
===============
R_ParseParticleEffect

Parse an effect out of the server message
===============
*/
void R_ParseParticleEffect (void)
{
	vec3_t		org, dir;
	int			i, count, color;

	for (i = 0; i < 3; i++) org[i] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
	for (i = 0; i < 3; i++) dir[i] = MSG_ReadChar () * (1.0 / 16);

	count = MSG_ReadByte ();
	color = MSG_ReadByte ();

	R_RunParticleEffect (org, dir, color, count);
}


/*
===============
R_ParticleExplosion

===============
*/
void R_ParticleExplosion (vec3_t org, int count)
{
	int			i, j;
	particle_t	*p;

	particle_type_t *pt = R_NewParticleType (org);

	for (i = 0; i < count; i++)
	{
		if (!(p = R_NewParticle (pt))) return;

		p->die = cl.time + 5;
		p->color = ramp1[0];
		p->ramp = rand () & 3;
		p->grav = -1;

		if (i & 1)
		{
			p->colorramp = ramp1;
			p->ramptime = 10;
			p->dvel[0] = p->dvel[1] = p->dvel[2] = 4;

			for (j = 0; j < 3; j++)
			{
				p->org[j] = org[j] + ((rand () % 32) - 16);
				p->vel[j] = (rand () % 512) - 256;
			}
		}
		else
		{
			p->colorramp = ramp2;
			p->ramptime = 15;

			p->dvel[0] = p->dvel[1] = p->dvel[2] = -1;

			for (j = 0; j < 3; j++)
			{
				p->org[j] = org[j] + ((rand () % 32) - 16);
				p->vel[j] = (rand () % 512) - 256;
			}
		}
	}
}


void R_ExplosionSmoke (vec3_t org)
{
}


void R_ParticleExplosion (vec3_t org)
{
	// explosion core
	R_ParticleExplosion (org, 1024);

	// add some smoke (enhanced only)
	R_ExplosionSmoke (org);
}


/*
===============
R_ParticleExplosion2

===============
*/
void R_ParticleExplosion2 (vec3_t org, int colorStart, int colorLength)
{
	int			i, j;
	particle_t	*p;
	int			colorMod = 0;

	particle_type_t *pt = R_NewParticleType (org);

	for (i = 0; i < 512; i++)
	{
		if (!(p = R_NewParticle (pt))) return;

		p->die = cl.time + 0.3f;
		p->color = colorStart + (colorMod % colorLength);
		colorMod++;

		p->grav = -1;
		p->dvel[0] = p->dvel[1] = p->dvel[2] = 4;

		for (j = 0; j < 3; j++)
		{
			p->org[j] = org[j] + ((rand () % 32) - 16);
			p->vel[j] = (rand () % 512) - 256;
		}
	}

	// add some smoke (enhanced only)
	R_ExplosionSmoke (org);
}


/*
===============
R_BlobExplosion

===============
*/
void R_BlobExplosion (vec3_t org)
{
	int			i, j;
	particle_t	*p;

	particle_type_t *pt = R_NewParticleType (org);

	for (i = 0; i < 1024; i++)
	{
		if (!(p = R_NewParticle (pt))) return;

		p->die = cl.time + (1 + (rand () & 8) * 0.05);
		p->grav = -1;

		if (i & 1)
		{
			p->dvel[0] = p->dvel[1] = p->dvel[2] = 4;
			p->color = 66 + rand () % 6;

			for (j = 0; j < 3; j++)
			{
				p->org[j] = org[j] + ((rand () % 32) - 16);
				p->vel[j] = (rand () % 512) - 256;
			}
		}
		else
		{
			p->dvel[0] = p->dvel[1] = -4;
			p->color = 150 + rand () % 6;

			for (j = 0; j < 3; j++)
			{
				p->org[j] = org[j] + ((rand () % 32) - 16);
				p->vel[j] = (rand () % 512) - 256;
			}
		}
	}

	// add some smoke (enhanced only)
	R_ExplosionSmoke (org);
}


void R_BloodParticles (vec3_t org, vec3_t dir, int color, int count)
{
	int			i, j;
	particle_t	*p;

	particle_type_t *pt = R_NewParticleType (org);

	for (i = 0; i < count; i++)
	{
		if (!(p = R_NewParticle (pt))) return;

		p->die = cl.time + (0.1 * (rand () % 5));
		p->color = (color & ~7) + (rand () & 7);
		p->grav = -1;

		for (j = 0; j < 3; j++)
		{
			p->org[j] = org[j] + ((rand () & 15) - 8);
			p->vel[j] = dir[j] * 15;
		}
	}
}


/*
===============
R_RunParticleEffect

===============
*/
void R_RunParticleEffect (vec3_t org, vec3_t dir, int color, int count)
{
	int			i, j;
	particle_t	*p;

	// hack the effect type out of the parameters
	if (hipnotic && count <= 4)
	{
		// particle field
		//return;
	}
	else if (count == 255)
	{
		// always an explosion
		R_ParticleExplosion (org);
		return;
	}
	else if (color == 73)
	{
		// blood splashes
		R_BloodParticles (org, dir, color, count);
		return;
	}
	else if (color == 225)
	{
		// blood splashes
		R_BloodParticles (org, dir, color, count);
		return;
	}

	// standard/undefined effect
	particle_type_t *pt = R_NewParticleType (org);

	for (i = 0; i < count; i++)
	{
		if (!(p = R_NewParticle (pt))) return;

		p->die = cl.time + (0.1 * (rand () % 5));
		p->color = (color & ~7) + (rand () & 7);
		p->grav = -1;

		for (j = 0; j < 3; j++)
		{
			p->org[j] = org[j] + ((rand () & 15) - 8);
			p->vel[j] = dir[j] * 15;
		}
	}
}


void R_WallHitParticles (vec3_t org, vec3_t dir, int color, int count)
{
	particle_t *p;
	int i;

	particle_type_t *pt = R_NewParticleType (org);

	for (i = 0; i < count; i++)
	{
		if (!(p = R_NewParticle (pt))) return;

		p->die = cl.time + (0.1 * (rand () % 5));
		p->color = (color & ~7) + (rand () & 7);
		p->grav = -1;

		for (int j = 0; j < 3; j++)
		{
			p->org[j] = org[j] + ((rand () & 15) - 8);
			p->vel[j] = dir[j] * 15;
		}
	}
}


void R_SpawnSmoke (vec3_t org, float jitter, float time)
{
}


void R_SpawnRainParticle (particle_type_t *pt, vec3_t mins, vec3_t maxs, int color, vec3_t vel, float time, float z)
{
	particle_t *p = R_NewParticle (pt);

	if (!p) return;

	p->org[0] = Q_Random (mins[0], maxs[0]);
	p->org[1] = Q_Random (mins[1], maxs[1]);
	p->org[2] = z;

	p->vel[0] = vel[0];
	p->vel[1] = vel[1];
	p->vel[2] = vel[2];

	p->color = color;
	p->grav = -1;

	p->die = cl.time + time;
}


void R_ParticleRain (vec3_t mins, vec3_t maxs, vec3_t dir, int count, int colorbase, int type)
{
	particle_type_t *pt = R_NewParticleType (mins);

	// increase the number of particles
	count *= 2;

	vec3_t		vel;
	float		t, z;

	if (maxs[0] <= mins[0]) {t = mins[0]; mins[0] = maxs[0]; maxs[0] = t;}
	if (maxs[1] <= mins[1]) {t = mins[1]; mins[1] = maxs[1]; maxs[1] = t;}
	if (maxs[2] <= mins[2]) {t = mins[2]; mins[2] = maxs[2]; maxs[2] = t;}

	if (dir[2] < 0) // falling
	{
		t = (maxs[2] - mins[2]) / -dir[2];
		z = maxs[2];
	}
	else // rising??
	{
		t = (maxs[2] - mins[2]) / dir[2];
		z = mins[2];
	}

	if (t < 0 || t > 2) // sanity check
		t = 2;

	switch (type)
	{
	case 0:
		while (count--)
		{
			vel[0] = dir[0] + Q_Random (-16, 16);
			vel[1] = dir[1] + Q_Random (-16, 16);
			vel[2] = dir[2] + Q_Random (-32, 32);

			R_SpawnRainParticle (pt, mins, maxs, colorbase + (rand () & 3), vel, t, z);
		}

		break;

	case 1:
		while (count--)
		{
			vel[0] = dir[0] + Q_Random (-16, 16);
			vel[1] = dir[1] + Q_Random (-16, 16);
			vel[2] = dir[2] + Q_Random (-32, 32);

			R_SpawnRainParticle (pt, mins, maxs, colorbase + (rand () & 3), vel, t, z);
		}

		break;

	default:
		Con_DPrintf ("R_ParticleRain: unknown type %i (0 = rain, 1 = snow)\n", type);
	}
}


/*
===============
R_LavaSplash

===============
*/
void R_LavaSplash (vec3_t org)
{
	int			i, j, k;
	particle_t	*p;
	float		vel;
	vec3_t		dir;

	particle_type_t *pt = R_NewParticleType (org);

	for (i = -16; i < 16; i++)
	{
		for (j = -16; j < 16; j++)
		{
			for (k = 0; k < 1; k++)
			{
				if (!(p = R_NewParticle (pt))) return;

				p->die = cl.time + (2 + (rand () & 31) * 0.02);
				p->color = 224 + (rand () & 7);
				p->grav = -1;

				dir[0] = j * 8 + (rand () & 7);
				dir[1] = i * 8 + (rand () & 7);
				dir[2] = 256;

				p->org[0] = org[0] + dir[0];
				p->org[1] = org[1] + dir[1];
				p->org[2] = org[2] + (rand () & 63);

				VectorNormalize (dir);
				vel = 50 + (rand () & 63);
				VectorScale (dir, vel, p->vel);
			}
		}
	}
}


/*
===============
R_TeleportSplash

===============
*/
void R_TeleportSplash (vec3_t org)
{
	int			i, j, k;
	particle_t	*p;
	float		vel;
	vec3_t		dir;

	particle_type_t *pt = R_NewParticleType (org);

	for (i = -16; i < 16; i += 4)
	{
		for (j = -16; j < 16; j += 4)
		{
			for (k = -24; k < 32; k += 4)
			{
				if (!(p = R_NewParticle (pt))) return;

				p->die = cl.time + (0.2 + (rand () & 7) * 0.02);
				p->color = 7 + (rand () & 7);
				p->grav = -1;

				dir[0] = j * 8;
				dir[1] = i * 8;
				dir[2] = k * 8;

				p->org[0] = org[0] + i + (rand () & 3);
				p->org[1] = org[1] + j + (rand () & 3);
				p->org[2] = org[2] + k + (rand () & 3);

				VectorNormalize (dir);
				vel = 50 + (rand () & 63);
				VectorScale (dir, vel, p->vel);
			}
		}
	}
}


void R_RocketTrail (vec3_t start, vec3_t end, int type)
{
	vec3_t		vec;
	float		len;
	int			j;
	particle_t	*p;
	int			dec;
	static int	tracercount;

	VectorSubtract (end, start, vec);
	len = VectorNormalize (vec);

	if (type < 128)
		dec = 3;
	else
	{
		dec = 1;
		type -= 128;
	}

	particle_type_t *pt = R_NewParticleType (start);

	while (len > 0)
	{
		len -= dec;

		if (!(p = R_NewParticle (pt))) return;

		VectorCopy2 (p->vel, vec3_origin);
		p->die = cl.time + 2;

		switch (type)
		{
		case 0:
		case 1:
			// rocket/grenade trail
			p->ramp = (rand () & 3);
			p->ramptime = 5;
			p->grav = 1;
			p->colorramp = ramp3;

			// grenade trail decays faster
			if (type == 1) p->ramp += 2;

			// leave color to here as it depends on the possibly adjusted ramp
			p->color = ramp3[(int) p->ramp];

			for (j = 0; j < 3; j++)
				p->org[j] = start[j] + ((rand () % 6) - 3);

			break;

		case 2:
		case 4:
			// blood/slight blood
			p->grav = -1;
			p->color = 67 + (rand () & 3);

			for (j = 0; j < 3; j++)
				p->org[j] = start[j] + ((rand () % 6) - 3);

			// slight blood
			if (type == 4) len -= 3;

			break;

		case 3:
		case 5:
			// tracer - wizard/hellknight
			p->die = cl.time + 0.5f;

			if (type == 3)
				p->color = 52 + ((tracercount & 4) << 1);
			else p->color = 230 + ((tracercount & 4) << 1);

			tracercount++;

			VectorCopy2 (p->org, start);

			// split trail left/right
			if (tracercount & 1)
			{
				p->vel[0] = 30 * vec[1];
				p->vel[1] = 30 * -vec[0];
			}
			else
			{
				p->vel[0] = 30 * -vec[1];
				p->vel[1] = 30 * vec[0];
			}

			break;

		case 6:	// voor trail
			p->color = 9 * 16 + 8 + (rand () & 3);
			p->die = cl.time + 0.3f;

			for (j = 0; j < 3; j++)
				p->org[j] = start[j] + ((rand () & 15) - 8);

			break;
		}

		VectorAdd (start, vec, start);
	}
}


void CL_WipeParticles (void)
{
	// these need to be wiped immediately on going to a new server
	active_particle_types = NULL;
	free_particle_types = NULL;
	free_particles = NULL;
}


void R_SetupParticleType (particle_type_t *pt)
{
	// no particles at all!
	if (!pt->particles) return;

	// removes expired particles from the active particles list
	particle_t *p;
	particle_t *kill;

	// this is the count of particles that will be drawn this frame
	pt->numactiveparticles = 0;

	// begin a new bounding box for this type
	pt->mins[0] = pt->mins[1] = pt->mins[2] = 9999999;
	pt->maxs[0] = pt->maxs[1] = pt->maxs[2] = -9999999;

	// remove from the head of the list
	for (;;)
	{
		kill = pt->particles;

		// note - client time is correct here
		if (kill && kill->die < cl.time)
		{
			pt->particles = kill->next;
			kill->next = free_particles;
			free_particles = kill;
			r_numparticles--;
			pt->numparticles--;

			continue;
		}

		break;
	}

	// particle updating has been moved back to here to preserve cache-friendliness
	extern cvar_t sv_gravity;
	float grav = cl.frametime * sv_gravity.value * 0.025f;
	float gravchange;
	float velchange[3];
	float gravtime = cl.frametime * 0.5f;

	for (p = pt->particles; p; p = p->next)
	{
		// remove from a mid-point in the list
		for (;;)
		{
			kill = p->next;

			// note - client time is correct here
			if (kill && kill->die < cl.time)
			{
				p->next = kill->next;
				kill->next = free_particles;
				free_particles = kill;
				r_numparticles--;
				pt->numparticles--;

				continue;
			}

			break;
		}

		// sanity check on colour to avoid array bounds errors
		if (p->color < 0) p->color = 0;
		if (p->color > 255) p->color = 255;

		// re-eval bbox
		for (int i = 0; i < 3; i++)
		{
			if (p->org[i] < pt->mins[i]) pt->mins[i] = p->org[i];
			if (p->org[i] > pt->maxs[i]) pt->maxs[i] = p->org[i];
		}

		// particle updating has been moved back to here to preserve cache-friendliness
		// also to ensure updates happen even if we've been bbculled
		// fade alpha and increase size
		p->alpha -= (p->fade * cl.frametime);
		p->scale += (p->growth * cl.frametime);

		// kill if fully faded or too small
		if (p->alpha <= 0 || p->scale <= 0)
		{
			// no further adjustments needed
			p->die = -1;
			continue;
		}

		if (p->colorramp)
		{
			// colour ramps
			p->ramp += p->ramptime * cl.frametime;

			// adjust color for ramp
			if (p->colorramp[(int) p->ramp] < 0)
			{
				// no further adjustments needed
				p->die = -1;
				continue;
			}
			else p->color = p->colorramp[(int) p->ramp];
		}

		// framerate independent delta factors
		gravchange = grav * p->grav;
		velchange[0] = p->dvel[0] * gravtime;
		velchange[1] = p->dvel[1] * gravtime;
		velchange[2] = p->dvel[2] * gravtime;

		// adjust for gravity (framerate independent)
		p->vel[2] += gravchange;

		// adjust for velocity change (framerate-independent)
		p->vel[0] += velchange[0];
		p->vel[1] += velchange[1];
		p->vel[2] += velchange[2];

		// update origin (framerate-independent)
		p->org[0] += p->vel[0] * cl.frametime;
		p->org[1] += p->vel[1] * cl.frametime;
		p->org[2] += p->vel[2] * cl.frametime;

		// adjust for gravity (framerate independent)
		p->vel[2] += gravchange;

		// adjust for velocity change (framerate-independent)
		p->vel[0] += velchange[0];
		p->vel[1] += velchange[1];
		p->vel[2] += velchange[2];

		// count the active particles for this type
		pt->numactiveparticles++;
	}
}


void D3D_AddParticesToAlphaList (void)
{
	// nothing to draw
	if (!active_particle_types) return;

	// removes expired particles from the active particles list
	particle_type_t *pt;
	particle_type_t *kill;

	// remove from the head of the list
	for (;;)
	{
		kill = active_particle_types;

		if (kill && !kill->particles)
		{
			// return to the free list
			active_particle_types = kill->next;
			kill->next = free_particle_types;
			kill->numparticles = 0;
			free_particle_types = kill;

			continue;
		}

		break;
	}

	// no types currently active
	if (!active_particle_types) return;

	for (pt = active_particle_types; pt; pt = pt->next)
	{
		// remove from a mid-point in the list
		for (;;)
		{
			kill = pt->next;

			if (kill && !kill->particles)
			{
				pt->next = kill->next;
				kill->next = free_particle_types;
				kill->numparticles = 0;
				free_particle_types = kill;

				continue;
			}

			break;
		}

		// prepare this type for rendering
		R_SetupParticleType (pt);

		if (pt->numactiveparticles && !R_CullBox (pt->mins, pt->maxs, frustum))
		{
			// add to the draw list (only if there's something to draw)
			D3DAlpha_AddToList (pt);
		}
	}
}


