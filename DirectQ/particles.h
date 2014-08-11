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

	// behaviour flags
	int			flags;

	// don't draw if != framecount
	int			visframe;

	// render adjustable
	LPDIRECT3DTEXTURE9 tex;
	float scale;
	float alpha;
	float fade;
	float growth;
	float scalemod;
} particle_t;


// this is needed outside of r_part now...
typedef struct particle_type_s
{
	struct particle_s *particles;
	int numparticles;
	int numactiveparticles;
	vec3_t spawnorg;
	struct particle_type_s *next;
} particle_type_t;


// allows us to combine different behaviour types
// (largely obsolete, replaced by individual particle parameters, retained for noclip type only)
#define PT_STATIC			0
#define PT_NOCLIP			(1 << 1)

// initially allocated batch - demo1 requires this number of particles,
// demo2 requires 2048, demo3 requires 3072 (rounding up to the nearest 1024 in each case)
// funny, you'd think demo3 would be the particle monster...
#define PARTICLE_BATCH_SIZE      4096

// extra particles are allocated in batches of this size.  this
// is more than would be needed most of the time; we'd have to have
// 3 explosions going off exactly simultaneously to require an allocation
// beyond this amount.
#define PARTICLE_EXTRA_SIZE      2048

// we don't expect these to ever be exceeded but we allow it if required
#define PARTICLE_TYPE_BATCH_SIZE	64
#define PARTICLE_TYPE_EXTRA_SIZE	32

extern particle_t	*free_particles;
extern particle_type_t *active_particle_types;
extern particle_type_t *free_particle_types;

extern int r_numparticles;
