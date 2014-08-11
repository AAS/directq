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

#include "quakedef.h"
#include "d3d_quake.h"


cvar_t r_newparticles ("r_newparticles", "0", CVAR_ARCHIVE);

// allows us to combine different behaviour types
// (largely obsolete, replaced by individual particle parameters, retained for noclip type only)
#define PT_STATIC			0
#define PT_NOCLIP			(1 << 1)

// !!! if this is changed, it must be changed in d_ifacea.h too !!!
typedef struct particle_s
{
	// driver-usable fields
	vec3_t		org;
	float		color;

	// drivers never touch the following fields
	struct particle_s	*next;

	// velocity increases
	vec3_t		vel;
	vec3_t		dvel;
	float		grav;

	// color ramps
	int			*colorramp;
	float		ramp;
	float		ramptime;

	// remove if < cl.time
	float		die;

	// leaf the particle is in
	mleaf_t		*leaf;

	// behaviour flags
	int			flags;

	// render adjustable
	LPDIRECT3DTEXTURE9 tex;
	float scale;
	float alpha;
	float fade;
	float growth;
	float scalemod;
} particle_t;


// default particle texture
LPDIRECT3DTEXTURE9 particledottexture = NULL;

// QMB particle textures
LPDIRECT3DTEXTURE9 qmbparticleblood = NULL;
LPDIRECT3DTEXTURE9 qmbparticlebubble = NULL;
LPDIRECT3DTEXTURE9 qmbparticlelightning = NULL;
LPDIRECT3DTEXTURE9 qmbparticlelightningold = NULL;
LPDIRECT3DTEXTURE9 particlesmoketexture = NULL;
LPDIRECT3DTEXTURE9 qmbparticlespark = NULL;
LPDIRECT3DTEXTURE9 qmbparticletrail = NULL;

// initially allocated batch - demo1 requires this number of particles,
// demo2 requires 2048, demo3 requires 3072 (rounding up to the nearest 1024 in each case)
// funny, you'd think demo3 would be the particle monster...
#define PARTICLE_BATCH_SIZE      4096

// extra particles are allocated in batches of this size.  this
// is more than would be needed most of the time; we'd have to have
// 3 explosions going off exactly simultaneously to require an allocation
// beyond this amount.
#define PARTICLE_EXTRA_SIZE      2048

// particle colour ramps - the extra values of -1 at the end of each are to protect us if the engine locks for a few seconds
int		ramp1[] = {0x6f, 0x6d, 0x6b, 0x69, 0x67, 0x65, 0x63, 0x61, -1, -1, -1, -1, -1};
int		ramp2[] = {0x6f, 0x6e, 0x6d, 0x6c, 0x6b, 0x6a, 0x68, 0x66, -1, -1, -1, -1, -1};
int		ramp3[] = {0x6d, 0x6b, 6, 5, 4, 3, -1, -1, -1, -1, -1};

particle_t	*active_particles, *free_particles;
particle_t	r_defaultparticle;

int	r_numparticles;
int r_numallocations;

vec3_t			r_pright, r_pup, r_ppn;


typedef struct partverts_s
{
	float x, y, z;
	DWORD color;
	float s, t;
} partverts_t;


// sizes of the particle batch
#define R_PARTICLE_RENDER_BATCH_SIZE	300
#define R_PARTICLE_RENDER_VERT_COUNT	200

// draw in batches
partverts_t partverts[R_PARTICLE_RENDER_VERT_COUNT];
short partindexes[R_PARTICLE_RENDER_BATCH_SIZE];

void D3D_SetParticleIndexes (void)
{
	// simulate quads using triangles (d3d don't have quads)
	int indexes[] = {0, 1, 2, 2, 3, 0};

	for (int i = 0, j = 0; i < R_PARTICLE_RENDER_BATCH_SIZE; i++)
	{
		partindexes[i] = indexes[j];
		indexes[j] += 4;
		j++;

		// wrap
		if (j == 6) j = 0;
	}
}


/*
===============
R_InitParticles
===============
*/
void R_InitParticles (void)
{
	// sanity check (it's a developer error if this happens
	if (R_PARTICLE_RENDER_BATCH_SIZE % 6)
	{
		Sys_Error ("R_InitParticles: R_PARTICLE_RENDER_BATCH_SIZE is not a multiple of 6");
		return;
	}

	// sanity check the vertex count also; these should be equal otherwise the code is bad
	int vc = R_PARTICLE_RENDER_BATCH_SIZE / 6 * 4;

	if (vc != R_PARTICLE_RENDER_VERT_COUNT)
	{
		Sys_Error ("R_InitParticles: particle batch sizes do not compute");
		return;
	}

	// create indexes for quad drawing
	D3D_SetParticleIndexes ();

	// setup default particle state
	// we need to explicitly set stuff to 0 as memsetting to 0 doesn't
	// actually equate to a value of 0 with floating point
	r_defaultparticle.scale = 0.5;
	r_defaultparticle.alpha = 255;
	r_defaultparticle.fade = 0;
	r_defaultparticle.growth = 0;
	r_defaultparticle.scalemod = 0.004;

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

	free_particles = (particle_t *) Pool_Alloc (POOL_MAP, PARTICLE_BATCH_SIZE * sizeof (particle_t));
	active_particles = NULL;

	for (i = 1; i < PARTICLE_BATCH_SIZE; i++)
	{
		free_particles[i - 1].next = &free_particles[i];
		free_particles[i].next = NULL;
	}

	// track the number of particles
	r_numparticles = PARTICLE_BATCH_SIZE;

	// no allocations have been done yet
	r_numallocations = 0;

	// particledottexture didn't exist when r_defaultparticle was created
	r_defaultparticle.tex = particledottexture;
}


particle_t *R_NewParticle (void)
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
		p->next = active_particles;
		active_particles = p;

		// done
		return p;
	}

	// alloc some more free particles
	free_particles = (particle_t *) Pool_Alloc (POOL_MAP, PARTICLE_EXTRA_SIZE * sizeof (particle_t));

	// link them up
	for (i = 0; i < PARTICLE_EXTRA_SIZE; i++)
		free_particles[i].next = &free_particles[i + 1];

	// finish the link
	free_particles[PARTICLE_EXTRA_SIZE - 1].next = NULL;

	// track the number of particles we have
	r_numparticles += PARTICLE_EXTRA_SIZE;

	// call recursively to return the first new free particle
	return R_NewParticle ();
}


/*
===============
R_EntityParticles
===============
*/

#define NUMVERTEXNORMALS	162
extern	float	r_avertexnormals[NUMVERTEXNORMALS][3];
vec3_t	avelocities[NUMVERTEXNORMALS];
float	beamlength = 16;
vec3_t	avelocity = {23, 7, 3};
float	partstep = 0.01;
float	timescale = 0.01;

void R_EntityParticles (entity_t *ent)
{
	int			count;
	int			i;
	particle_t	*p;
	float		angle;
	float		sr, sp, sy, cr, cp, cy;
	vec3_t		forward;
	float		dist;
	
	dist = 64;
	count = 50;

	if (!avelocities[0][0])
		for (i=0 ; i<NUMVERTEXNORMALS*3 ; i++)
			avelocities[0][i] = (rand()&255) * 0.01;

	for (i=0 ; i<NUMVERTEXNORMALS ; i++)
	{
		angle = cl.time * avelocities[i][0];
		sy = sin(angle);
		cy = cos(angle);
		angle = cl.time * avelocities[i][1];
		sp = sin(angle);
		cp = cos(angle);
		angle = cl.time * avelocities[i][2];
		sr = sin(angle);
		cr = cos(angle);
	
		forward[0] = cp*cy;
		forward[1] = cp*sy;
		forward[2] = -sp;

		p = R_NewParticle ();

		p->die = cl.time + 0.01;
		p->color = 0x6f;
		p->colorramp = ramp1;
		p->ramptime = 10;
		p->grav = -1;

		p->dvel[0] = p->dvel[1] = p->dvel[2] = 4;

		p->org[0] = ent->origin[0] + r_avertexnormals[i][0]*dist + forward[0]*beamlength;			
		p->org[1] = ent->origin[1] + r_avertexnormals[i][1]*dist + forward[1]*beamlength;			
		p->org[2] = ent->origin[2] + r_avertexnormals[i][2]*dist + forward[2]*beamlength;			
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

	for ( ;; )
	{
		r = fscanf (f,"%f %f %f\n", &org[0], &org[1], &org[2]);
		if (r != 3)
			break;
		c++;

		p = R_NewParticle ();
		
		p->die = 99999;
		p->color = (-c)&15;
		p->flags |= PT_NOCLIP;
		VectorCopy (vec3_origin, p->vel);
		VectorCopy (org, p->org);
	}

	fclose (f);
	Con_Printf ("%i points read\n", c);
}


void R_EmitParticle (vec3_t org)
{
}


void R_EmitParticles (vec3_t mins, vec3_t maxs, int scale, int time)
{
	particle_t *p;

	for (int i = mins[0]; i < maxs[0]; i += scale)
	{
		for (int j = mins[1]; j < maxs[1]; j += scale)
		{
			for (int k = mins[2]; k < maxs[2]; k += scale)
			{
				p = R_NewParticle ();

				p->org[0] = i;
				p->org[1] = j;
				p->org[2] = k;

				p->die = cl.time + 0.01;
			}
		}
	}
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
	
	for (i = 0; i < 3; i++) org[i] = MSG_ReadCoord ();
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

	for (i = 0; i < count; i++)
	{
		p = R_NewParticle ();

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

	for (i=0; i<512; i++)
	{
		p = R_NewParticle ();

		p->die = cl.time + 0.3;
		p->color = colorStart + (colorMod % colorLength);
		colorMod++;

		p->grav = -1;
		p->dvel[0] = p->dvel[1] = p->dvel[2] = 4;

		for (j=0 ; j<3 ; j++)
		{
			p->org[j] = org[j] + ((rand()%32)-16);
			p->vel[j] = (rand()%512)-256;
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
	
	for (i=0 ; i<1024 ; i++)
	{
		p = R_NewParticle ();

		p->die = cl.time + 1 + (rand()&8)*0.05;
		p->grav = -1;

		if (i & 1)
		{
			p->dvel[0] = p->dvel[1] = p->dvel[2] = 4;
			p->color = 66 + rand()%6;

			for (j=0 ; j<3 ; j++)
			{
				p->org[j] = org[j] + ((rand()%32)-16);
				p->vel[j] = (rand()%512)-256;
			}
		}
		else
		{
			p->dvel[0] = p->dvel[1] = -4;
			p->color = 150 + rand()%6;

			for (j=0 ; j<3 ; j++)
			{
				p->org[j] = org[j] + ((rand()%32)-16);
				p->vel[j] = (rand()%512)-256;
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

	for (i = 0; i < count; i++)
	{
		p = R_NewParticle ();

		p->die = cl.time + 0.1 * (rand () % 5);
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
	for (i = 0; i < count; i++)
	{
		p = R_NewParticle ();

		p->die = cl.time + 0.1 * (rand () % 5);
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

	for (i = 0; i < count; i++)
	{
		particle_t *p = R_NewParticle ();

		p->die = cl.time + 0.1 * (rand () % 5);
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

	for (i = -16; i < 16; i++)
	{
		for (j = -16; j < 16; j++)
		{
			for (k = 0; k < 1; k++)
			{
				p = R_NewParticle ();
		
				p->die = cl.time + 2 + (rand () & 31) * 0.02;
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

	for (i = -16; i < 16; i += 4)
	{
		for (j = -16; j < 16; j += 4)
		{
			for (k = -24; k < 32; k += 4)
			{
				p = R_NewParticle ();
		
				p->die = cl.time + 0.2 + (rand()&7) * 0.02;
				p->color = 7 + (rand()&7);
				p->grav = -1;

				dir[0] = j*8;
				dir[1] = i*8;
				dir[2] = k*8;
	
				p->org[0] = org[0] + i + (rand()&3);
				p->org[1] = org[1] + j + (rand()&3);
				p->org[2] = org[2] + k + (rand()&3);
	
				VectorNormalize (dir);						
				vel = 50 + (rand()&63);
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

	while (len > 0)
	{
		len -= dec;

		p = R_NewParticle ();
		
		VectorCopy (vec3_origin, p->vel);
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

			/*
			if (r_newparticles.integer)
			{
				p->tex = particlesmoketexture;
				p->scale = 2;
				p->alpha = 192;
				p->growth = 6;
				p->fade = 192;
				len -= 16;
			}
			*/

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

			/*
			if (r_newparticles.integer)
			{
				p->tex = particleblood[0];
				p->scale = 3;
			}
			*/

			break;

		case 3:
		case 5:
			// tracer - wizard/hellknight
			p->die = cl.time + 0.5;

			if (type == 3)
				p->color = 52 + ((tracercount & 4) << 1);
			else p->color = 230 + ((tracercount & 4) << 1);

			tracercount++;

			VectorCopy (start, p->org);

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
			p->die = cl.time + 0.3;

			for (j = 0; j < 3; j++)
				p->org[j] = start[j] + ((rand () & 15) - 8);

			break;
		}

		VectorAdd (start, vec, start);
	}
}


/*
===============
D3D_DrawParticles
===============
*/
extern	cvar_t	sv_gravity;

float partst[6][2] = {{1, 0}, {1, 1}, {0, 1}, {0, 0}};
float partv[6][2] = {{1, 1}, {-1, 1}, {-1, -1}, {1, -1}};


void D3D_PrepareParticles (void)
{
	// no particles at all!
	if (!active_particles) return;

	// removes expired particles from the active particles list
	particle_t *p;
	particle_t *kill;

	// remove from the head of the list
	for (;;)
	{
		kill = active_particles;

		if (kill && kill->die < cl.time)
		{
			active_particles = kill->next;
			kill->next = free_particles;
			free_particles = kill;

			continue;
		}

		break;
	}

	for (p = active_particles; p; p = p->next)
	{
		// remove from a mid-point in the list
		for (;;)
		{
			kill = p->next;

			if (kill && kill->die < cl.time)
			{
				p->next = kill->next;
				kill->next = free_particles;
				free_particles = kill;

				continue;
			}

			break;
		}

		// particle is potentially drawable; see if it is visible
		p->leaf = Mod_PointInLeaf (p->org, cl.worldmodel);

		// particles coming from a pointfile command are not clipped
		if (!(p->flags & PT_NOCLIP))
		{
			// kill it for next frame
			if (p->leaf->contents == CONTENTS_SOLID || p->leaf->contents == CONTENTS_SKY) p->die = -1;
		}

		// set contents rendering flags
		if (p->leaf->contents == CONTENTS_WATER || p->leaf->contents == CONTENTS_SLIME || p->leaf->contents == CONTENTS_LAVA)
		{
			d3d_RenderDef.renderflags |= R_RENDERWATERPARTICLE;
			p->flags |= R_RENDERWATERPARTICLE;
		}
		else
		{
			d3d_RenderDef.renderflags |= R_RENDEREMPTYPARTICLE;
			p->flags |= R_RENDEREMPTYPARTICLE;
		}

		// do anything else interesting with the particle that we need to do here...
	}
}


void R_UpdateParticle (particle_t *p)
{
	int i;
	float grav = d3d_RenderDef.frametime * sv_gravity.value * 0.05;

	// fade alpha and increase size
	p->alpha -= (p->fade * d3d_RenderDef.frametime);
	p->scale += (p->growth * d3d_RenderDef.frametime);

	// kill if fully faded or too small
	if (p->alpha <= 0 || p->scale <= 0)
	{
		// no further adjustments needed
		p->die = -1;
		return;
	}

	if (p->colorramp)
	{
		// colour ramps
		p->ramp += p->ramptime * d3d_RenderDef.frametime;

		// adjust color for ramp
		if (p->colorramp[(int) p->ramp] < 0)
		{
			// no further adjustments needed
			p->die = -1;
			return;
		}
		else p->color = p->colorramp[(int) p->ramp];
	}

	// update origin
	p->org[0] += p->vel[0] * d3d_RenderDef.frametime;
	p->org[1] += p->vel[1] * d3d_RenderDef.frametime;
	p->org[2] += p->vel[2] * d3d_RenderDef.frametime;

	// adjust for gravity
	p->vel[2] += grav * p->grav;

	// adjust for velocity change
	p->vel[0] += p->dvel[0] * d3d_RenderDef.frametime;
	p->vel[1] += p->dvel[1] * d3d_RenderDef.frametime;
	p->vel[2] += p->dvel[2] * d3d_RenderDef.frametime;
}


int D3D_SubmitParticles (int v, int treshold)
{
	// nothing to submit
	if (!v) return 0;

	// not time to update yet
	if (v < treshold) return v;

	// draw current batch
	D3D_DrawPrimitive
	(
		D3DPT_TRIANGLELIST,
		0,
		v,
		v / 2,
		partindexes,
		D3DFMT_INDEX16,
		partverts,
		sizeof (partverts_t)
	);

	// reset counter to 0
	return 0;
}


void D3D_DrawParticles (int flag)
{
	// no particles to render
	if (!active_particles) return;
	if ((flag & R_RENDEREMPTYPARTICLE) && !(d3d_RenderDef.renderflags & R_RENDEREMPTYPARTICLE)) return;
	if ((flag & R_RENDERWATERPARTICLE) && !(d3d_RenderDef.renderflags & R_RENDERWATERPARTICLE)) return;

	// texture is a shader param so we need to update shader state if we switch texture
	// hence we also cache it and check for changes here
	LPDIRECT3DTEXTURE9 cachedtexture = NULL;
	int NumPasses;

	// state setup
	D3D_SetFVF (D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
	D3D_SetTextureAddressMode (D3DTADDRESS_CLAMP);

	D3D_SetTextureMipmap (0, d3d_3DFilterMag, d3d_3DFilterMin, D3DTEXF_NONE);

	// disable z buffer writing and enable blending
	D3D_EnableAlphaBlend (D3DBLENDOP_ADD, D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA);

	D3D_SetTextureColorMode (0, D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE);
	D3D_SetTextureAlphaMode (0, D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE);

	D3D_SetTextureColorMode (1, D3DTOP_DISABLE);
	D3D_SetTextureAlphaMode (1, D3DTOP_DISABLE);

	D3D_SetTextureColorMode (2, D3DTOP_DISABLE);
	D3D_SetTextureAlphaMode (2, D3DTOP_DISABLE);

	D3D_SetTexCoordIndexes (0);

	// for rendering
	int v = 0;
	vec3_t up, right;
	float scale;

	// walk the list starting at the first active particle
	for (particle_t *p = active_particles; p; p = p->next)
	{
		// if the particle isn't in a visible leaf this frame there's no point in drawing it
		// note however that we just skip drawing rather than killing it as it may be in a
		// visible leaf later on in it's lifetime.  the exception (dealt with above) is if it
		// hits a solid or sky leaf, at which point we stop drawing it.
		if (p->leaf->visframe != d3d_RenderDef.visframecount) continue;

		// final sanity check on colour to avoid array bounds errors
		if (p->color < 0 || p->color > 255) continue;

		if ((flag & R_RENDEREMPTYPARTICLE) && !(p->flags & R_RENDEREMPTYPARTICLE)) continue;
		if ((flag & R_RENDERWATERPARTICLE) && !(p->flags & R_RENDERWATERPARTICLE)) continue;

		// contents
		// check for a texture change
		// to do - sort by texture?
		// we need to keep a separate texture cache here as the particle renderer need to know when
		// to finish the current batch and begin a new one.
		if (p->tex != cachedtexture)
		{
			// submit everything in the current batch
			v = D3D_SubmitParticles (v, 0);

			// bind it and update the shader state
			D3D_SetTexture (0, p->tex);

			// re-cache
			cachedtexture = p->tex;
		}

		byte *color = (byte *) &d_8to24table[(int) p->color];

		DWORD pc = D3DCOLOR_ARGB
		(
			BYTE_CLAMP (p->alpha),
			color[2],
			color[1],
			color[0]
		);

		if (p->scalemod)
		{
			// hack a scale up to keep particles from disappearing
			// note - all of this *could* go into the vertex shader, but we'd have a pretty heavyweight vertex submission
			// and we likely wouldn't gain that much from it.
			scale = (p->org[0] - r_origin[0]) * vpn[0] + (p->org[1] - r_origin[1]) * vpn[1] + (p->org[2] - r_origin[2]) * vpn[2];

			if (scale < 20)
				scale = 1;
			else scale = 1 + scale * p->scalemod;
		}
		else scale = 1;

		// note - scale each particle individually
		VectorScale (vup, p->scale * scale * 1.125, up);
		VectorScale (vright, p->scale * scale * 1.125, right);

		// draw as two triangles so that we can submit multiple particles in a single batch
		// note: these are indexed triangles
		for (int pv = 0; pv < 4; pv++, v++)
		{
			partverts[v].x = p->org[0] + up[0] * partv[pv][0] + right[0] * partv[pv][1];
			partverts[v].y = p->org[1] + up[1] * partv[pv][0] + right[1] * partv[pv][1];
			partverts[v].z = p->org[2] + up[2] * partv[pv][0] + right[2] * partv[pv][1];
			partverts[v].color = pc;
			partverts[v].s = partst[pv][0];
			partverts[v].t = partst[pv][1];
		}

		// check batch submission
		v = D3D_SubmitParticles (v, R_PARTICLE_RENDER_VERT_COUNT);

		// update this particle
		R_UpdateParticle (p);
	}

	// submit everything left over
	D3D_SubmitParticles (v, 0);

	// revert state
	D3D_DisableAlphaBlend ();
}


