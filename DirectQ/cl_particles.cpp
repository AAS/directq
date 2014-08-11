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
#include "cl_fx.h"

particle_t	*free_particles;
particle_emitter_t *active_particle_emitters, *free_particle_emitters;
particle_effect_t *active_particle_effects, *free_particle_effects;

// particle colour ramps - the extra values of -1 at the end of each are to protect us if the engine locks for a few seconds
int		ramp1[] = {0x6f, 0x6d, 0x6b, 0x69, 0x67, 0x65, 0x63, 0x61, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
int		ramp2[] = {0x6f, 0x6e, 0x6d, 0x6c, 0x6b, 0x6a, 0x68, 0x66, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
int		ramp3[] = {0x6d, 0x6b, 6, 5, 4, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};

int	r_numparticles;
int r_numallocations;

particle_t	r_defaultparticle;

// to do - how is this calculated anyway?  looks like normals for a sphere to me...
float r_avertexnormals[NUMVERTEXNORMALS][3] =
{
	{-0.525731f, 0.000000f, 0.850651f}, {-0.442863f, 0.238856f, 0.864188f}, {-0.295242f, 0.000000f, 0.955423f}, {-0.309017f, 0.500000f, 0.809017f},
	{-0.162460f, 0.262866f, 0.951056f}, {0.000000f, 0.000000f, 1.000000f}, {0.000000f, 0.850651f, 0.525731f}, {-0.147621f, 0.716567f, 0.681718f},
	{0.147621f, 0.716567f, 0.681718f}, {0.000000f, 0.525731f, 0.850651f}, {0.309017f, 0.500000f, 0.809017f}, {0.525731f, 0.000000f, 0.850651f},
	{0.295242f, 0.000000f, 0.955423f}, {0.442863f, 0.238856f, 0.864188f}, {0.162460f, 0.262866f, 0.951056f}, {-0.681718f, 0.147621f, 0.716567f},
	{-0.809017f, 0.309017f, 0.500000f}, {-0.587785f, 0.425325f, 0.688191f}, {-0.850651f, 0.525731f, 0.000000f}, {-0.864188f, 0.442863f, 0.238856f},
	{-0.716567f, 0.681718f, 0.147621f}, {-0.688191f, 0.587785f, 0.425325f}, {-0.500000f, 0.809017f, 0.309017f}, {-0.238856f, 0.864188f, 0.442863f},
	{-0.425325f, 0.688191f, 0.587785f}, {-0.716567f, 0.681718f, -0.147621f}, {-0.500000f, 0.809017f, -0.309017f}, {-0.525731f, 0.850651f, 0.000000f},
	{0.000000f, 0.850651f, -0.525731f}, {-0.238856f, 0.864188f, -0.442863f}, {0.000000f, 0.955423f, -0.295242f}, {-0.262866f, 0.951056f, -0.162460f},
	{0.000000f, 1.000000f, 0.000000f}, {0.000000f, 0.955423f, 0.295242f}, {-0.262866f, 0.951056f, 0.162460f}, {0.238856f, 0.864188f, 0.442863f},
	{0.262866f, 0.951056f, 0.162460f}, {0.500000f, 0.809017f, 0.309017f}, {0.238856f, 0.864188f, -0.442863f}, {0.262866f, 0.951056f, -0.162460f},
	{0.500000f, 0.809017f, -0.309017f}, {0.850651f, 0.525731f, 0.000000f}, {0.716567f, 0.681718f, 0.147621f}, {0.716567f, 0.681718f, -0.147621f},
	{0.525731f, 0.850651f, 0.000000f}, {0.425325f, 0.688191f, 0.587785f}, {0.864188f, 0.442863f, 0.238856f}, {0.688191f, 0.587785f, 0.425325f},
	{0.809017f, 0.309017f, 0.500000f}, {0.681718f, 0.147621f, 0.716567f}, {0.587785f, 0.425325f, 0.688191f}, {0.955423f, 0.295242f, 0.000000f},
	{1.000000f, 0.000000f, 0.000000f}, {0.951056f, 0.162460f, 0.262866f}, {0.850651f, -0.525731f, 0.000000f}, {0.955423f, -0.295242f, 0.000000f},
	{0.864188f, -0.442863f, 0.238856f}, {0.951056f, -0.162460f, 0.262866f}, {0.809017f, -0.309017f, 0.500000f}, {0.681718f, -0.147621f, 0.716567f},
	{0.850651f, 0.000000f, 0.525731f}, {0.864188f, 0.442863f, -0.238856f}, {0.809017f, 0.309017f, -0.500000f}, {0.951056f, 0.162460f, -0.262866f},
	{0.525731f, 0.000000f, -0.850651f}, {0.681718f, 0.147621f, -0.716567f}, {0.681718f, -0.147621f, -0.716567f}, {0.850651f, 0.000000f, -0.525731f},
	{0.809017f, -0.309017f, -0.500000f}, {0.864188f, -0.442863f, -0.238856f}, {0.951056f, -0.162460f, -0.262866f}, {0.147621f, 0.716567f, -0.681718f},
	{0.309017f, 0.500000f, -0.809017f}, {0.425325f, 0.688191f, -0.587785f}, {0.442863f, 0.238856f, -0.864188f}, {0.587785f, 0.425325f, -0.688191f},
	{0.688191f, 0.587785f, -0.425325f}, {-0.147621f, 0.716567f, -0.681718f}, {-0.309017f, 0.500000f, -0.809017f}, {0.000000f, 0.525731f, -0.850651f},
	{-0.525731f, 0.000000f, -0.850651f}, {-0.442863f, 0.238856f, -0.864188f}, {-0.295242f, 0.000000f, -0.955423f}, {-0.162460f, 0.262866f, -0.951056f},
	{0.000000f, 0.000000f, -1.000000f}, {0.295242f, 0.000000f, -0.955423f}, {0.162460f, 0.262866f, -0.951056f}, {-0.442863f, -0.238856f, -0.864188f},
	{-0.309017f, -0.500000f, -0.809017f}, {-0.162460f, -0.262866f, -0.951056f}, {0.000000f, -0.850651f, -0.525731f}, {-0.147621f, -0.716567f, -0.681718f},
	{0.147621f, -0.716567f, -0.681718f}, {0.000000f, -0.525731f, -0.850651f}, {0.309017f, -0.500000f, -0.809017f}, {0.442863f, -0.238856f, -0.864188f},
	{0.162460f, -0.262866f, -0.951056f}, {0.238856f, -0.864188f, -0.442863f}, {0.500000f, -0.809017f, -0.309017f}, {0.425325f, -0.688191f, -0.587785f},
	{0.716567f, -0.681718f, -0.147621f}, {0.688191f, -0.587785f, -0.425325f}, {0.587785f, -0.425325f, -0.688191f}, {0.000000f, -0.955423f, -0.295242f},
	{0.000000f, -1.000000f, 0.000000f}, {0.262866f, -0.951056f, -0.162460f}, {0.000000f, -0.850651f, 0.525731f}, {0.000000f, -0.955423f, 0.295242f},
	{0.238856f, -0.864188f, 0.442863f}, {0.262866f, -0.951056f, 0.162460f}, {0.500000f, -0.809017f, 0.309017f}, {0.716567f, -0.681718f, 0.147621f},
	{0.525731f, -0.850651f, 0.000000f}, {-0.238856f, -0.864188f, -0.442863f}, {-0.500000f, -0.809017f, -0.309017f}, {-0.262866f, -0.951056f, -0.162460f},
	{-0.850651f, -0.525731f, 0.000000f}, {-0.716567f, -0.681718f, -0.147621f}, {-0.716567f, -0.681718f, 0.147621f}, {-0.525731f, -0.850651f, 0.000000f},
	{-0.500000f, -0.809017f, 0.309017f}, {-0.238856f, -0.864188f, 0.442863f}, {-0.262866f, -0.951056f, 0.162460f}, {-0.864188f, -0.442863f, 0.238856f},
	{-0.809017f, -0.309017f, 0.500000f}, {-0.688191f, -0.587785f, 0.425325f}, {-0.681718f, -0.147621f, 0.716567f}, {-0.442863f, -0.238856f, 0.864188f},
	{-0.587785f, -0.425325f, 0.688191f}, {-0.309017f, -0.500000f, 0.809017f}, {-0.147621f, -0.716567f, 0.681718f}, {-0.425325f, -0.688191f, 0.587785f},
	{-0.162460f, -0.262866f, 0.951056f}, {0.442863f, -0.238856f, 0.864188f}, {0.162460f, -0.262866f, 0.951056f}, {0.309017f, -0.500000f, 0.809017f},
	{0.147621f, -0.716567f, 0.681718f}, {0.000000f, -0.525731f, 0.850651f}, {0.425325f, -0.688191f, 0.587785f}, {0.587785f, -0.425325f, 0.688191f},
	{0.688191f, -0.587785f, 0.425325f}, {-0.955423f, 0.295242f, 0.000000f}, {-0.951056f, 0.162460f, 0.262866f}, {-1.000000f, 0.000000f, 0.000000f},
	{-0.850651f, 0.000000f, 0.525731f}, {-0.955423f, -0.295242f, 0.000000f}, {-0.951056f, -0.162460f, 0.262866f}, {-0.864188f, 0.442863f, -0.238856f},
	{-0.951056f, 0.162460f, -0.262866f}, {-0.809017f, 0.309017f, -0.500000f}, {-0.864188f, -0.442863f, -0.238856f}, {-0.951056f, -0.162460f, -0.262866f},
	{-0.809017f, -0.309017f, -0.500000f}, {-0.681718f, 0.147621f, -0.716567f}, {-0.681718f, -0.147621f, -0.716567f}, {-0.850651f, 0.000000f, -0.525731f},
	{-0.688191f, 0.587785f, -0.425325f}, {-0.587785f, 0.425325f, -0.688191f}, {-0.425325f, 0.688191f, -0.587785f}, {-0.425325f, -0.688191f, -0.587785f},
	{-0.587785f, -0.425325f, -0.688191f}, {-0.688191f, -0.587785f, -0.425325f}
};

extern cvar_t sv_gravity;
extern cvar_t r_particlestyle;

float avelocities[NUMVERTEXNORMALS][2];


/*
===============
R_InitParticles
===============
*/
void R_InitParticles (void)
{
	for (int i = 0; i < NUMVERTEXNORMALS; i++)
	{
		avelocities[i][0] = (float) (rand () & 255) * 0.01;
		avelocities[i][1] = (float) (rand () & 255) * 0.01;
	}

	// setup default particle state
	r_defaultparticle.scale = 2.666f;
	r_defaultparticle.alpha = 1.0f;

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

	// particle emitter chains
	free_particle_emitters = (particle_emitter_t *) ClientZone->Alloc (PARTICLE_EMITTER_BATCH_SIZE * sizeof (particle_emitter_t));
	active_particle_emitters = NULL;

	for (i = 1; i < PARTICLE_EMITTER_BATCH_SIZE; i++)
	{
		free_particle_emitters[i - 1].next = &free_particle_emitters[i];
		free_particle_emitters[i].next = NULL;
	}

	// particle effect chains
	free_particle_effects = (particle_effect_t *) ClientZone->Alloc (PARTICLE_EFFECT_BATCH_SIZE * sizeof (particle_effect_t));
	active_particle_effects = NULL;

	for (i = 1; i < PARTICLE_EFFECT_BATCH_SIZE; i++)
	{
		free_particle_effects[i - 1].next = &free_particle_effects[i];
		free_particle_effects[i].next = NULL;
	}

	// track the number of particles
	r_numparticles = 0;

	// no allocations have been done yet
	r_numallocations = 0;
}


particle_emitter_t *R_NewParticleEmitter (vec3_t spawnorg)
{
	particle_emitter_t *pe;
	int i;

	if (free_particle_emitters)
	{
		// just take from the free list
		pe = free_particle_emitters;
		free_particle_emitters = pe->next;

		// no particles yet
		pe->particles = NULL;
		pe->numparticles = 0;

		// copy across origin
		VectorCopy2 (pe->spawnorg, spawnorg);
		pe->spawntime = cl.time;

		// link it in
		pe->next = active_particle_emitters;
		active_particle_emitters = pe;

		// done
		return pe;
	}

	// alloc some more free particles
	free_particle_emitters = (particle_emitter_t *) ClientZone->Alloc (PARTICLE_EMITTER_EXTRA_SIZE * sizeof (particle_emitter_t));

	// link them up
	for (i = 0; i < PARTICLE_EMITTER_EXTRA_SIZE; i++)
		free_particle_emitters[i].next = &free_particle_emitters[i + 1];

	// finish the link
	free_particle_emitters[PARTICLE_EMITTER_EXTRA_SIZE - 1].next = NULL;

	// call recursively to return the first new free particle type
	return R_NewParticleEmitter (spawnorg);
}


particle_effect_t *R_NewParticleEffect (void)
{
	particle_effect_t *pe;
	int i;

	if (free_particle_effects)
	{
		// just take from the free list
		pe = free_particle_effects;
		free_particle_effects = pe->next;

		// link it in
		pe->next = active_particle_effects;
		active_particle_effects = pe;

		// done
		return pe;
	}

	// alloc some more free particles
	free_particle_effects = (particle_effect_t *) ClientZone->Alloc (PARTICLE_EFFECT_EXTRA_SIZE * sizeof (particle_effect_t));

	// link them up
	for (i = 0; i < PARTICLE_EFFECT_EXTRA_SIZE; i++)
		free_particle_effects[i].next = &free_particle_effects[i + 1];

	// finish the link
	free_particle_effects[PARTICLE_EFFECT_EXTRA_SIZE - 1].next = NULL;

	// call recursively to return the first new free particle type
	return R_NewParticleEffect ();
}


particle_t *R_NewParticle (particle_emitter_t *pe)
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
		p->next = pe->particles;
		pe->particles = p;
		pe->numparticles++;

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
	return R_NewParticle (pe);
}


/*
===============
R_EntityParticles
===============
*/
void D3DAlpha_AddToList (entity_t *ent, int effects);
void D3DAlpha_AddToList (particle_effect_t *particle);

void R_EntityParticles (entity_t *ent)
{
	// these are entirely drawn and animated on the GPU now
	D3DAlpha_AddToList (ent, EF_BRIGHTFIELD);
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

	particle_emitter_t *pe = R_NewParticleEmitter (vec3_origin);

	for (;;)
	{
		r = fscanf (f, "%f %f %f\n", &org[0], &org[1], &org[2]);

		if (r != 3)
			break;

		c++;

		if (!(p = R_NewParticle (pe)))
		{
			// woah!  this was a return!  NOT clever!
			Con_Printf ("Pointfile overflow - ");
			break;
		}

		p->die = cl.time + 999999;
		p->color = (float) ((-c) & 15);
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
void R_ParticleExplosion (vec3_t org)
{
	// reserving this for D3D11 as the lack of constant buffers, not being able to use geometry shaders
	// or vertex textures, and use of the effects framework makes it a huge mess under 9
	// eventually all, or most, particles will move to effects, and most of this file will die
	if (0)
	{
		particle_effect_t *pe = R_NewParticleEffect ();

		pe->origin[0] = org[0];
		pe->origin[1] = org[1];
		pe->origin[2] = org[2];

		pe->starttime = cl.time;
		pe->die = cl.time + 5;
		pe->type = peff_explosion;

		return;
	}

	int			i, j;
	particle_t	*p;

	particle_emitter_t *pe = R_NewParticleEmitter (org);

	for (i = 0; i < 1024; i++)
	{
		if (!(p = R_NewParticle (pe))) return;

		p->die = cl.time + 5;
		p->color = ramp1[0];
		p->ramp = rand () & 3;
		p->grav = -1;

		if (i & 1)
		{
			p->colorramp = ramp1;
			p->ramptime = 10;
			p->dvel[0] = p->dvel[1] = p->dvel[2] = 4;
		}
		else
		{
			p->colorramp = ramp2;
			p->ramptime = 15;
			p->dvel[0] = p->dvel[1] = p->dvel[2] = -1;
		}

		for (j = 0; j < 3; j++)
		{
			p->org[j] = org[j] + ((rand () % 32) - 16);
			p->vel[j] = (rand () % 512) - 256;
		}
	}
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

	particle_emitter_t *pe = R_NewParticleEmitter (org);

	for (i = 0; i < 512; i++)
	{
		if (!(p = R_NewParticle (pe))) return;

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

	particle_emitter_t *pe = R_NewParticleEmitter (org);

	for (i = 0; i < 1024; i++)
	{
		if (!(p = R_NewParticle (pe))) return;

		p->die = cl.time + (1 + (rand () & 8) * 0.05);
		p->grav = -1;

		if (i & 1)
		{
			p->dvel[0] = p->dvel[1] = p->dvel[2] = 4;
			p->color = 66 + rand () % 6;
		}
		else
		{
			p->dvel[0] = p->dvel[1] = -4;
			p->color = 150 + rand () % 6;
		}

		for (j = 0; j < 3; j++)
		{
			p->org[j] = org[j] + ((rand () % 32) - 16);
			p->vel[j] = (rand () % 512) - 256;
		}
	}
}


void R_BloodParticles (vec3_t org, vec3_t dir, int color, int count)
{
	int			i, j;
	particle_t	*p;

	particle_emitter_t *pe = R_NewParticleEmitter (org);

	for (i = 0; i < count; i++)
	{
		if (!(p = R_NewParticle (pe))) return;

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
	particle_emitter_t *pe = R_NewParticleEmitter (org);

	for (i = 0; i < count; i++)
	{
		if (!(p = R_NewParticle (pe))) return;

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

	particle_emitter_t *pe = R_NewParticleEmitter (org);

	for (i = 0; i < count; i++)
	{
		if (!(p = R_NewParticle (pe))) break;

		p->grav = -1;
		p->die = cl.time + (0.1 * (rand () % 5));
		p->color = (color & ~7) + (rand () & 7);

		for (int j = 0; j < 3; j++)
		{
			p->org[j] = org[j] + ((rand () & 15) - 8);
			p->vel[j] = dir[j] * 15;
		}
	}
}


void R_SpawnRainParticle (particle_emitter_t *pe, vec3_t mins, vec3_t maxs, int color, vec3_t vel, float time, float z, int type)
{
	// type 1 is snow, 0 is rain
	particle_t *p = R_NewParticle (pe);

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
	particle_emitter_t *pe = R_NewParticleEmitter (mins);

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

	// type 1 is snow, 0 is rain
	switch (type)
	{
	case 0:
		while (count--)
		{
			vel[0] = dir[0] + Q_Random (-16, 16);
			vel[1] = dir[1] + Q_Random (-16, 16);
			vel[2] = dir[2] + Q_Random (-32, 32);

			R_SpawnRainParticle (pe, mins, maxs, colorbase + (rand () & 3), vel, t, z, type);
		}

		break;

	case 1:
		while (count--)
		{
			vel[0] = dir[0] + Q_Random (-16, 16);
			vel[1] = dir[1] + Q_Random (-16, 16);
			vel[2] = dir[2] + Q_Random (-32, 32);

			R_SpawnRainParticle (pe, mins, maxs, colorbase + (rand () & 3), vel, t, z, type);
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

	particle_emitter_t *pe = R_NewParticleEmitter (org);

	for (i = -16; i < 16; i++)
	{
		for (j = -16; j < 16; j++)
		{
			for (k = 0; k < 1; k++)
			{
				if (!(p = R_NewParticle (pe))) return;

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

	particle_emitter_t *pe = R_NewParticleEmitter (org);

	for (i = -16; i < 16; i += 4)
	{
		for (j = -16; j < 16; j += 4)
		{
			for (k = -24; k < 32; k += 4)
			{
				if (!(p = R_NewParticle (pe))) return;

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


int R_TrailLength (vec3_t start, vec3_t end, int dec)
{
	vec3_t vec;

	VectorSubtract (end, start, vec);

	float len = VectorNormalize (vec);
	int nump = (len / dec) + 0.5f;

	return nump < 1 ? 1 : nump;
}


void R_RocketTrail (vec3_t start, vec3_t end, int trailtype)
{
	int			j;
	particle_t	*p;
	int			dec;
	static int	tracercount;

	if (trailtype < 128)
		dec = 3;
	else
	{
		dec = 1;
		trailtype -= 128;
	}

	if (trailtype == RT_ZOMGIB) dec += 3;

	float porg[3];
	float plerp;
	int nump = R_TrailLength (start, end, dec);

	if (nump < 1) return;

	// a particle trail adds too few new particles to the emiiter, even over a 1/36 second interval
	// (maxes out at approx. 10) so we can't make this a single draw call....
	particle_emitter_t *pe = R_NewParticleEmitter (start);

	for (int i = 0; i < nump; i++)
	{
		if (!(p = R_NewParticle (pe))) return;

		// at 36 fps particles have a tendency to clump in discrete packets so interpolate them along the trail length instead
		if (nump == 1)
		{
			porg[0] = start[0];
			porg[1] = start[1];
			porg[2] = start[2];
		}
		else if (nump == 2)
		{
			porg[0] = i ? end[0] : start[0];
			porg[1] = i ? end[1] : start[1];
			porg[2] = i ? end[2] : start[2];
		}
		else
		{
			plerp = (float) i / (float) (nump - 1);

			porg[0] = start[0] + plerp * (end[0] - start[0]);
			porg[1] = start[1] + plerp * (end[1] - start[1]);
			porg[2] = start[2] + plerp * (end[2] - start[2]);
		}

		VectorCopy2 (p->vel, vec3_origin);
		p->die = cl.time + 2;

		switch (trailtype)
		{
		case RT_ROCKET:
		case RT_GRENADE:
			// rocket/grenade trail
			p->ramp = (rand () & 3);
			p->ramptime = 5;
			p->grav = 1;
			p->colorramp = ramp3;

			// grenade trail decays faster
			if (trailtype == RT_GRENADE) p->ramp += 2;

			// leave color to here as it depends on the possibly adjusted ramp
			p->color = ramp3[(int) p->ramp];

			for (j = 0; j < 3; j++)
				p->org[j] = porg[j] + ((rand () % 6) - 3);

			break;

		case RT_GIB:
		case RT_ZOMGIB:
			// blood/slight blood
			p->grav = -1;
			p->color = 67 + (rand () & 3);

			for (j = 0; j < 3; j++)
				p->org[j] = porg[j] + ((rand () % 6) - 3);

			break;

		case RT_WIZARD:
		case RT_KNIGHT:
			// tracer - wizard/hellknight
			p->die = cl.time + 0.5f;

			tracercount++;

			VectorCopy2 (p->org, porg);

			// split trail left/right
			/*
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
			*/

			p->color = ((trailtype == RT_WIZARD) ? 52 : 230) + ((tracercount & 4) << 1);

			break;

		case RT_VORE:
			p->color = 9 * 16 + 8 + (rand () & 3);
			p->die = cl.time + 0.3f;

			for (j = 0; j < 3; j++)
				p->org[j] = porg[j] + ((rand () & 15) - 8);

			break;
		}
	}
}


void CL_WipeParticles (void)
{
	// these need to be wiped immediately on going to a new server
	active_particle_emitters = NULL;
	free_particle_emitters = NULL;

	active_particle_effects = NULL;
	free_particle_effects = NULL;

	free_particles = NULL;
}


void R_SetupParticleEmitter (particle_emitter_t *pe)
{
	// no particles at all!
	if (!pe->particles) return;

	// removes expired particles from the active particles list
	particle_t *p;
	particle_t *kill;

	// this is the count of particles that will be drawn this frame
	pe->numactiveparticles = 0;

	// begin a new bounding box for this type
	Mod_ClearBoundingBox (pe->mins, pe->maxs);

	// remove from the head of the list
	for (;;)
	{
		kill = pe->particles;

		// note - client time is correct here
		if (kill && kill->die < cl.time)
		{
			pe->particles = kill->next;
			kill->next = free_particles;
			free_particles = kill;
			r_numparticles--;
			pe->numparticles--;

			continue;
		}

		break;
	}

	for (p = pe->particles; p; p = p->next)
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
				pe->numparticles--;

				continue;
			}

			break;
		}

		// sanity check on colour to avoid array bounds errors
		if (p->color < 0) p->color = 0;
		if (p->color > 255) p->color = 255;

		// re-eval bbox
		Mod_AccumulateBox (pe->mins, pe->maxs, p->org);

		// count the active particles for this type
		pe->numactiveparticles++;
	}
}


void D3D_AddParticesToAlphaList (void)
{
	if (active_particle_effects)
	{
		// removes expired particles from the active particles list
		particle_effect_t *pe;
		particle_effect_t *kill;

		// remove from the head of the list
		for (;;)
		{
			kill = active_particle_effects;

			if (kill && kill->die < cl.time)
			{
				// return to the free list
				active_particle_effects = kill->next;
				kill->next = free_particle_effects;
				free_particle_effects = kill;

				continue;
			}

			break;
		}

		for (pe = active_particle_effects; pe; pe = pe->next)
		{
			// remove from a mid-point in the list
			for (;;)
			{
				kill = pe->next;

				if (kill && kill->die < cl.time)
				{
					pe->next = kill->next;
					kill->next = free_particle_effects;
					free_particle_effects = kill;

					continue;
				}

				break;
			}

			// add to the draw list (only if there's something to draw)
			D3DAlpha_AddToList (pe);
		}
	}

	if (active_particle_emitters)
	{
		// removes expired particles from the active particles list
		particle_emitter_t *pe;
		particle_emitter_t *kill;

		// remove from the head of the list
		for (;;)
		{
			kill = active_particle_emitters;

			if (kill && !kill->particles)
			{
				// return to the free list
				active_particle_emitters = kill->next;
				kill->next = free_particle_emitters;
				kill->numparticles = 0;
				free_particle_emitters = kill;

				continue;
			}

			break;
		}

		for (pe = active_particle_emitters; pe; pe = pe->next)
		{
			// remove from a mid-point in the list
			for (;;)
			{
				kill = pe->next;

				if (kill && !kill->particles)
				{
					pe->next = kill->next;
					kill->next = free_particle_emitters;
					kill->numparticles = 0;
					free_particle_emitters = kill;

					continue;
				}

				break;
			}

			// prepare this type for rendering
			R_SetupParticleEmitter (pe);

			if (pe->numactiveparticles && !R_CullBox (pe->mins, pe->maxs, frustum))
			{
				// add to the draw list (only if there's something to draw)
				D3DAlpha_AddToList (pe);
			}
		}
	}
}


