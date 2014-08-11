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
#include "d3d_hlsl.h"

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


// note - unused for now... the textures are loaded, but the system is still fully classic...
LPDIRECT3DTEXTURE9 particledottexture = NULL;
LPDIRECT3DTEXTURE9 particlesmoketexture = NULL;
LPDIRECT3DTEXTURE9 particletracertexture = NULL;
LPDIRECT3DTEXTURE9 particleblood[8] = {NULL};

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

particle_t	*particles;
int	r_numparticles;
int r_numallocations;

vec3_t			r_pright, r_pup, r_ppn;


typedef struct partverts_s
{
	float x, y, z;
	DWORD color;
	float s, t;
} partverts_t;

// size of the particle batch
#define R_PARTICLE_RENDER_BATCH_SIZE	300

// draw in batches
partverts_t partverts[R_PARTICLE_RENDER_BATCH_SIZE];

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

	// setup anything else we need here...
}


/*
===============
R_ClearParticles
===============
*/
void R_ClearParticles (void)
{
	int i;

	free_particles = (particle_t *) Heap_TagAlloc (TAG_PARTICLES, PARTICLE_BATCH_SIZE * sizeof (particle_t));
	active_particles = NULL;

	for (i = 0; i < PARTICLE_BATCH_SIZE; i++)
		free_particles[i].next = &free_particles[i + 1];

	free_particles[PARTICLE_BATCH_SIZE - 1].next = NULL;

	// track the number of particles
	r_numparticles = PARTICLE_BATCH_SIZE;

	// no allocations have been done yet
	r_numallocations = 0;
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
		p->next = active_particles;
		active_particles = p;

		// set default parameters
		// (these may be overwritten as required)
		p->tex = particledottexture;
		p->scale = 0.5;
		p->alpha = 255;
		p->fade = 0;
		p->growth = 0;
		p->scalemod = 0.004;

		// default flags
		p->flags = PT_STATIC;

		// default velocity change and gravity
		p->dvel[0] = p->dvel[1] = p->dvel[2] = 0;
		p->grav = 0;

		// colour ramps
		p->colorramp = NULL;
		p->ramp = 0;
		p->ramptime = 0;

		return p;
	}
	else
	{
		// testing
		// Con_Printf ("%i Particle Allocations!\n", ++r_numallocations);

		// alloc some more free particles
		free_particles = (particle_t *) Heap_TagAlloc (TAG_PARTICLES, PARTICLE_EXTRA_SIZE * sizeof (particle_t));

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
	FILE	*f;
	vec3_t	org;
	int		r;
	int		c;
	particle_t	*p;
	char	name[MAX_OSPATH];
	
	sprintf (name,"maps/%s.pts", sv.name);

	COM_FOpenFile (name, &f);
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
		return;
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

	for (i=-16 ; i<16 ; i+=4)
		for (j=-16 ; j<16 ; j+=4)
			for (k=-24 ; k<32 ; k+=4)
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
R_DrawParticles
===============
*/
extern	cvar_t	sv_gravity;

inline DWORD FloatToDW (float f)
{
	return *((DWORD *) &f);
}


float partst[6][2] = {{0, 1}, {1, 1}, {0, 0}, {1, 1}, {1, 0}, {0, 0}};
float partv[6][2] = {{-1, -1}, {-1, 1}, {1, -1}, {-1, 1}, {1, 1}, {1, -1}};


void R_KillParticles (void)
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

		// do anything else interesting with the particle that we need to do here...
	}
}


__inline int R_SubmitParticles (int v, int treshold)
{
	// nothing to submit
	if (!v) return 0;

	// not time to update yet
	if (v < treshold) return v;

	// draw current batch
	d3d_ParticleFX.Draw (D3DPT_TRIANGLELIST, v / 3, partverts, sizeof (partverts_t));

	// reset counter to 0
	return 0;
}


void R_RenderParticles (void)
{
	// no particles to render
	if (!active_particles) return;

	// texture is a shader param so we need to update shader state if we switch texture
	// hence we also cache it and check for changes here
	LPDIRECT3DTEXTURE9 cachedtexture = NULL;
	int NumPasses;

	// state setup
	d3d_Device->SetVertexDeclaration (d3d_ParticleVertexDeclaration);

	d3d_ParticleFX.BeginRender ();
	d3d_ParticleFX.SetWPMatrix (&(d3d_WorldMatrix * d3d_PerspectiveMatrix));
	d3d_ParticleFX.SwitchToPass (0);

	// disable z buffer writing and enable blending
	d3d_Device->SetRenderState (D3DRS_ZWRITEENABLE, FALSE);
	d3d_EnableAlphaBlend->Apply ();

	// don't cull these as they should never backface
	// might give a small speedup as the runtime doesn't have to check it...
	D3D_BackfaceCull (D3DCULL_NONE);

	// for rendering
	int v = 0;
	vec3_t up, right;
	float scale;

	// walk the list starting at the first active particle
	for (particle_t *p = active_particles; p; p = p->next)
	{
		if (p->leaf->visframe != r_visframecount) continue;

		// check for a texture change
		// to do - sort by texture?
		// we need to keep a separate texture cache here as the particle renderer need to know when
		// to finish the current batch and begin a new one.
		if (p->tex != cachedtexture)
		{
			// submit everything in the current batch
			v = R_SubmitParticles (v, 0);

			// bind it and update the shader state
			d3d_ParticleFX.SetTexture (p->tex);

			// re-cache
			cachedtexture = p->tex;
		}

		byte *color = (byte *) &d_8to24table[(int) p->color];
		DWORD pc = D3DCOLOR_ARGB (BYTE_CLAMP (p->alpha), color[2], color[1], color[0]);

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
		VectorScale (vup, p->scale * scale, up);
		VectorScale (vright, p->scale * scale, right);

		// draw as two triangles so that we can submit multiple particles in a single batch
		// (might not be optimal...)
		for (int pv = 0; pv < 6; pv++, v++)
		{
			partverts[v].x = p->org[0] + up[0] * partv[pv][0] + right[0] * partv[pv][1];
			partverts[v].y = p->org[1] + up[1] * partv[pv][0] + right[1] * partv[pv][1];
			partverts[v].z = p->org[2] + up[2] * partv[pv][0] + right[2] * partv[pv][1];
			partverts[v].color = pc;
			partverts[v].s = partst[pv][0];
			partverts[v].t = partst[pv][1];
		}

		// check batch submission
		v = R_SubmitParticles (v, R_PARTICLE_RENDER_BATCH_SIZE);
	}

	// submit everything left over
	R_SubmitParticles (v, 0);

	// revert state
	d3d_ParticleFX.EndRender ();
	D3D_BackfaceCull (D3DCULL_CCW);
	d3d_Device->SetRenderState (D3DRS_ZWRITEENABLE, TRUE);
	d3d_DisableAlphaBlend->Apply ();
}


void R_UpdateParticles (void)
{
	// no particles to update
	if (!active_particles) return;

	int i;
	float grav = r_frametime * sv_gravity.value * 0.05;

	// walk the list starting at the first active particle
	for (particle_t *p = active_particles; p; p = p->next)
	{
		// fade alpha and increase size
		p->alpha -= (p->fade * r_frametime);
		p->scale += (p->growth * r_frametime);

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
			p->ramp += p->ramptime * r_frametime;

			// adjust color for ramp
			if (p->colorramp[(int) p->ramp] < 0)
			{
				// no further adjustments needed
				p->die = -1;
				continue;
			}
			else p->color = p->colorramp[(int) p->ramp];
		}

		// update origin
		p->org[0] += p->vel[0] * r_frametime;
		p->org[1] += p->vel[1] * r_frametime;
		p->org[2] += p->vel[2] * r_frametime;

		// adjust for gravity
		p->vel[2] += grav * p->grav;

		// adjust for velocity change
		p->vel[0] += p->dvel[0] * r_frametime;
		p->vel[1] += p->dvel[1] * r_frametime;
		p->vel[2] += p->dvel[2] * r_frametime;
	}
}


void R_DrawParticles (void)
{
	// remove expired particles from the active particles list
	R_KillParticles ();

	// render all particles still in the active list
	R_RenderParticles ();

	// update particle states
	R_UpdateParticles ();
}

