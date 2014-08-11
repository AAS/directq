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
#include "d3d_quake.h"
#include "resource.h"
#include "particles.h"


void D3DMain_CreateBuffers (void);
extern LPDIRECT3DINDEXBUFFER9 d3d_MainIBO;


cvar_t r_particlesize ("r_particlesize", "1", CVAR_ARCHIVE);
cvar_t r_drawparticles ("r_drawparticles", "1", CVAR_ARCHIVE);
cvar_t r_particlestyle ("r_particlestyle", "0", CVAR_ARCHIVE);

#pragma pack (pop, 1)

typedef struct partvertinstanced0_s
{
	float xyz[3];
	float st[2];
} partvertinstanced0_t;


typedef struct partvertinstanced1_s
{
	float position[3];
	float scale;
	DWORD color;
} partvertinstanced1_t;


typedef struct partvertstream0_s
{
	float xyz[3];
	DWORD color;
} partvertstream0_t;


typedef struct partvertstream1_s
{
	float st[2];
} partvertstream1_t;


typedef struct spritevert_s
{
	float xyz[3];
	DWORD color;
	float st[2];
} spritevert_t;


#pragma pack (push)

typedef struct d3d_partstate_s
{
	int FirstVertex;
	int NumVertexes;
	int FirstIndex;
	int NumIndexes;
	int TotalVertexes;
	int TotalIndexes;
	int LockOffset;

	union
	{
		partvertinstanced1_t *VertsInstanced;
		partvertstream0_t *VertsNonInstanced;
	};
} d3d_partstate_t;

d3d_partstate_t d3d_PartState;
d3d_partstate_t d3d_SpriteState;


// each drawn quad has 4 vertexes and 6 indexes
#define MAX_PART_QUADS 4096		// if this is changed then MAX_MAIN_INDEXES in d3d_main.cpp may need to be changed too
#define MAX_PART_VERTEXES 60000		// if this is changed then MAX_MAIN_INDEXES in d3d_main.cpp may need to be changed too
#define MAX_SPRITE_VERTEXES 1000		// if this is changed then MAX_MAIN_INDEXES in d3d_main.cpp may need to be changed too

LPDIRECT3DVERTEXBUFFER9 d3d_PartVBOStream0 = NULL;
LPDIRECT3DVERTEXBUFFER9 d3d_PartVBOStream1 = NULL;
LPDIRECT3DVERTEXBUFFER9 d3d_SpriteVBO = NULL;
LPDIRECT3DVERTEXDECLARATION9 d3d_PartDecl = NULL;
LPDIRECT3DVERTEXDECLARATION9 d3d_SpriteDecl = NULL;

__inline void D3DPart_SetPosition (float *dst, float x = 0, float y = 0, float z = 0)
{
	dst[0] = x;
	dst[1] = y;
	dst[2] = z;
}


__inline void D3DPart_SetTexCoord (float *dst, float s = 0, float t = 0)
{
	dst[0] = s;
	dst[1] = t;
}


void D3DPart_CreateBuffers (void)
{
	// for testing...
	// d3d_GlobalCaps.supportInstancing = false;

	if (d3d_GlobalCaps.supportInstancing)
	{
		// this is our shared data (the model)
		if (!d3d_PartVBOStream0)
		{
			// we only need base vertexes for one instance with instancing
			D3DMain_CreateVertexBuffer (4 * sizeof (partvertinstanced0_t), D3DUSAGE_WRITEONLY, &d3d_PartVBOStream0);
			partvertinstanced0_t *verts = NULL;

			hr = d3d_PartVBOStream0->Lock (0, 0, (void **) &verts, 0);
			if (FAILED (hr)) Sys_Error ("D3DPart_CreateBuffers: failed to lock vertex buffer");

			// index these as strips to help out certain hardware
			D3DPart_SetPosition (verts[0].xyz, -1, -1);
			D3DPart_SetTexCoord (verts[0].st, 0, 0);

			D3DPart_SetPosition (verts[1].xyz, -1, 1);
			D3DPart_SetTexCoord (verts[1].st, 1, 0);

			D3DPart_SetPosition (verts[2].xyz, 1, -1);
			D3DPart_SetTexCoord (verts[2].st, 0, 1);

			D3DPart_SetPosition (verts[3].xyz, 1, 1);
			D3DPart_SetTexCoord (verts[3].st, 1, 1);

			hr = d3d_PartVBOStream0->Unlock ();
			if (FAILED (hr)) Sys_Error ("D3DPart_CreateBuffers: failed to unlock vertex buffer");
		}

		// this is our per-instance data (position, size and colour)
		if (!d3d_PartVBOStream1)
		{
			// each full quad is represented by a single vertex in this VB and it contains the properties that are unique to that quad
			D3DMain_CreateVertexBuffer (MAX_PART_QUADS * sizeof (partvertinstanced1_t), D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, &d3d_PartVBOStream1);
			D3D_PrelockVertexBuffer (d3d_PartVBOStream1);
			d3d_PartState.TotalVertexes = 0;
			d3d_PartState.TotalIndexes = 0;
			d3d_PartState.LockOffset = 0;
		}

		if (!d3d_PartDecl)
		{
			D3DVERTEXELEMENT9 d3d_partlayout[] =
			{
				// this isn't really a blendweight, it's a scaling factor so that we can do billboarding in the vertex shader
				{0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
				{0, 12, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
				{1, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1},
				{1, 12, D3DDECLTYPE_FLOAT1, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDWEIGHT, 0},
				{1, 16, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0},
				D3DDECL_END ()
			};

			hr = d3d_Device->CreateVertexDeclaration (d3d_partlayout, &d3d_PartDecl);
			if (FAILED (hr)) Sys_Error ("D3DPart_CreateBuffers: d3d_Device->CreateVertexDeclaration failed");
		}
	}
	else
	{
		if (!d3d_PartVBOStream0)
		{
			D3DMain_CreateVertexBuffer (MAX_PART_VERTEXES * sizeof (partvertstream0_t), D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, &d3d_PartVBOStream0);
			D3D_PrelockVertexBuffer (d3d_PartVBOStream0);
			d3d_PartState.TotalVertexes = 0;
			d3d_PartState.TotalIndexes = 0;
			d3d_PartState.LockOffset = 0;
		}

		if (!d3d_PartVBOStream1)
		{
			D3DMain_CreateVertexBuffer (MAX_PART_VERTEXES * sizeof (partvertstream1_t), D3DUSAGE_WRITEONLY, &d3d_PartVBOStream1);
			partvertstream1_t *st = NULL;

			hr = d3d_PartVBOStream1->Lock (0, 0, (void **) &st, 0);
			if (FAILED (hr)) Sys_Error ("D3DPart_CreateBuffers: failed to lock index buffer");

			// and fill it in
			for (int i = 0; i < MAX_PART_VERTEXES; i += 4, st += 4)
			{
				// index these as strips to help out certain hardware
				D3DPart_SetTexCoord (st[0].st, 0, 0);
				D3DPart_SetTexCoord (st[1].st, 1, 0);
				D3DPart_SetTexCoord (st[2].st, 0, 1);
				D3DPart_SetTexCoord (st[3].st, 1, 1);
			}

			hr = d3d_PartVBOStream1->Unlock ();
			if (FAILED (hr)) Sys_Error ("D3DPart_CreateBuffers: failed to unlock index buffer");
		}

		if (!d3d_PartDecl)
		{
			D3DVERTEXELEMENT9 d3d_partlayout[] =
			{
				{0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
				{0, 12, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0},
				{1, 0, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
				D3DDECL_END ()
			};

			hr = d3d_Device->CreateVertexDeclaration (d3d_partlayout, &d3d_PartDecl);
			if (FAILED (hr)) Sys_Error ("D3DPart_CreateBuffers: d3d_Device->CreateVertexDeclaration failed");
		}
	}

	D3DMain_CreateBuffers ();

	if (!d3d_SpriteVBO)
	{
		D3DMain_CreateVertexBuffer (MAX_SPRITE_VERTEXES * sizeof (spritevert_t), D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, &d3d_SpriteVBO);
		D3D_PrelockVertexBuffer (d3d_SpriteVBO);
		d3d_SpriteState.TotalVertexes = 0;
		d3d_SpriteState.TotalIndexes = 0;
		d3d_SpriteState.LockOffset = 0;
	}

	if (!d3d_SpriteDecl)
	{
		D3DVERTEXELEMENT9 d3d_spritelayout[] =
		{
			{0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
			{0, 12, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0},
			{0, 16, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
			D3DDECL_END ()
		};

		hr = d3d_Device->CreateVertexDeclaration (d3d_spritelayout, &d3d_SpriteDecl);
		if (FAILED (hr)) Sys_Error ("D3DPart_CreateBuffers: d3d_Device->CreateVertexDeclaration failed");
	}
}


void D3DPart_ReleaseBuffers (void)
{
	SAFE_RELEASE (d3d_PartVBOStream0);
	SAFE_RELEASE (d3d_PartVBOStream1);
	SAFE_RELEASE (d3d_SpriteVBO);
	SAFE_RELEASE (d3d_PartDecl);
	SAFE_RELEASE (d3d_SpriteDecl);
}


CD3DDeviceLossHandler d3d_PartBuffersHandler (D3DPart_ReleaseBuffers, D3DPart_CreateBuffers);


typedef void (*D3DPARTSUBMITPARTICLEPROC) (particle_type_t *, particle_t *, float, float *, float *, DWORD);
D3DPARTSUBMITPARTICLEPROC D3DPart_SubmitParticle = NULL;

void D3DPart_SubmitParticleInstanced (particle_type_t *pt, particle_t *p, float scale, float *up, float *right, DWORD color);
void D3DPart_SubmitParticleNonInstanced (particle_type_t *pt, particle_t *p, float scale, float *up, float *right, DWORD color);

float d3d_PartScale;

void D3DPart_SetBuffers (void)
{
	// create the buffers if needed
	D3DPart_CreateBuffers ();

	// initial states
	d3d_PartState.NumVertexes = 0;
	d3d_PartState.NumIndexes = 0;
	d3d_PartState.VertsInstanced = NULL;

	if (d3d_GlobalCaps.supportInstancing)
	{
		D3DPart_SubmitParticle = D3DPart_SubmitParticleInstanced;

		// streams 0 and 1 are set later as they depend on the number of particles to draw
		D3D_SetVertexDeclaration (d3d_PartDecl);
		D3D_SetStreamSource (2, NULL, 0, 0);
		D3D_SetIndices (d3d_MainIBO);

		// keep sizes consistent
		d3d_PartScale = 0.3f * r_particlesize.value;
	}
	else
	{
		D3DPart_SubmitParticle = D3DPart_SubmitParticleNonInstanced;

		// and set up for drawing
		D3D_SetVertexDeclaration (d3d_PartDecl);
		D3D_SetStreamSource (0, d3d_PartVBOStream0, 0, sizeof (partvertstream0_t));
		D3D_SetStreamSource (1, d3d_PartVBOStream1, 0, sizeof (partvertstream1_t));
		D3D_SetStreamSource (2, NULL, 0, 0);
		D3D_SetIndices (d3d_MainIBO);

		// keep sizes consistent
		d3d_PartScale = 0.6f * r_particlesize.value;
	}
}


void D3DPart_Flush (void)
{
	// don't check numindexes here as it will be 0 with instancing
	if (d3d_PartState.NumVertexes)
	{
		if (d3d_GlobalCaps.supportInstancing)
		{
			// instancing needs to orphan the VB at each call and maintains the lock while vertexes are coming in to it
			// this is somewhat cheaper than a separate lock/unlock for each particle
			if (d3d_PartState.VertsInstanced)
			{
				hr = d3d_PartVBOStream1->Unlock ();
				if (FAILED (hr)) Sys_Error ("D3DPart_Flush: failed to unlock a vertex buffer");
				d3d_RenderDef.numlock++;
				d3d_PartState.VertsInstanced = NULL;
			}

			// stream zero is the model and it's frequency is the number of particles to draw
			// stream one is the per-instance data
			D3D_SetStreamSource (0, d3d_PartVBOStream0, 0, sizeof (partvertinstanced0_t), D3DSTREAMSOURCE_INDEXEDDATA | d3d_PartState.NumVertexes);
			D3D_SetStreamSource (1, d3d_PartVBOStream1, 0, sizeof (partvertinstanced1_t), D3DSTREAMSOURCE_INSTANCEDATA | 1);

			// ... and we draw them ... (different params for instancing, we pretend we only have one particle)
			D3D_DrawIndexedPrimitive (0, 4, 0, 2);
		}
		else
		{
			// an unlock should always be needed
			if (d3d_PartState.VertsNonInstanced)
			{
				hr = d3d_PartVBOStream0->Unlock ();
				if (FAILED (hr)) Sys_Error ("D3DPart_Flush: failed to unlock a vertex buffer");
				d3d_RenderDef.numlock++;
				d3d_PartState.VertsNonInstanced = NULL;
			}

			D3D_DrawIndexedPrimitive (d3d_PartState.FirstVertex,
				d3d_PartState.NumVertexes,
				d3d_PartState.FirstIndex,
				d3d_PartState.NumIndexes / 3);
		}
	}

	d3d_PartState.FirstVertex = d3d_PartState.TotalVertexes;
	d3d_PartState.FirstIndex = d3d_PartState.TotalIndexes;
	d3d_PartState.NumVertexes = 0;
	d3d_PartState.NumIndexes = 0;
}


// made this a global because otherwise multiple particle types don't get batched at all!!!
LPDIRECT3DTEXTURE9 cachedparticletexture = NULL;

void D3DPart_SubmitParticleInstanced (particle_type_t *pt, particle_t *p, float scale, float *up, float *right, DWORD color)
{
	// instancing needs to orphan the VB at each call and maintains the lock while vertexes are coming in to it
	// this is somewhat cheaper than a separate lock/unlock for each particle
	if (d3d_PartState.NumVertexes >= MAX_PART_QUADS || !d3d_PartState.VertsInstanced)
	{
		D3DPart_Flush ();

		hr = d3d_PartVBOStream1->Lock (0, 0, (void **) &d3d_PartState.VertsInstanced, D3DLOCK_DISCARD);
		if (FAILED (hr)) Sys_Error ("D3DPart_SubmitParticle: failed to lock vertex buffer");

		d3d_PartState.NumVertexes = 0;
	}

	// position gets the baseline particle origin
	VectorCopy2 (d3d_PartState.VertsInstanced->position, p->org);

	// copy out the rest (keep size consistent)
	d3d_PartState.VertsInstanced->scale = scale * p->scale * d3d_PartScale;
	d3d_PartState.VertsInstanced->color = color;

	d3d_PartState.NumVertexes++;
	d3d_PartState.VertsInstanced++;
}


void D3DPart_SubmitParticleNonInstanced (particle_type_t *pt, particle_t *p, float scale, float *up, float *right, DWORD color)
{
	/*
	if (d3d_PartState.TotalVertexes + 4 >= MAX_PART_VERTEXES)
	{
		D3DPart_Flush ();

		hr = d3d_PartVBOStream0->Lock (0, 0, (void **) &d3d_PartState.VertsNonInstanced, D3DLOCK_DISCARD);
		if (FAILED (hr)) Sys_Error ("D3DPart_SubmitParticle: failed to lock vertex buffer");

		d3d_PartState.FirstVertex = d3d_PartState.TotalVertexes = d3d_PartState.LockOffset = 0;
		d3d_PartState.FirstIndex = d3d_PartState.TotalIndexes = 0;
	}
	else
	{
		hr = d3d_PartVBOStream0->Lock (d3d_PartState.LockOffset, 4 * sizeof (partvertstream0_t), (void **) &d3d_PartState.VertsNonInstanced, D3DLOCK_NOOVERWRITE);
		if (FAILED (hr)) Sys_Error ("D3DPart_SubmitParticle: failed to lock vertex buffer");
	}
	*/

	if (d3d_PartState.TotalVertexes + 4 >= MAX_PART_VERTEXES)
	{
		D3DPart_Flush ();

		hr = d3d_PartVBOStream0->Lock (0, 0, (void **) &d3d_PartState.VertsNonInstanced, D3DLOCK_DISCARD);
		if (FAILED (hr)) Sys_Error ("D3DPart_SubmitParticle: failed to lock vertex buffer");

		d3d_PartState.FirstVertex = d3d_PartState.TotalVertexes = d3d_PartState.LockOffset = 0;
		d3d_PartState.FirstIndex = d3d_PartState.TotalIndexes = 0;
	}
	else if (!d3d_PartState.VertsNonInstanced)
	{
		if (d3d_PartState.TotalVertexes + 4 * pt->numactiveparticles >= MAX_PART_VERTEXES)
		{
			D3DPart_Flush ();

			hr = d3d_PartVBOStream0->Lock (0, 0, (void **) &d3d_PartState.VertsNonInstanced, D3DLOCK_DISCARD);

			d3d_PartState.FirstVertex = d3d_PartState.TotalVertexes = d3d_PartState.LockOffset = 0;
			d3d_PartState.FirstIndex = d3d_PartState.TotalIndexes = 0;
		}
		else
		{
			hr = d3d_PartVBOStream0->Lock
			(
				d3d_PartState.LockOffset,
				pt->numactiveparticles * 4 * sizeof (partvertstream0_t),
				(void **) &d3d_PartState.VertsNonInstanced,
				D3DLOCK_NOOVERWRITE
			);
		}

		if (FAILED (hr)) Sys_Error ("D3DPart_SubmitParticle: failed to lock vertex buffer");
	}

	// fixme - rescale all particle scales for this
	scale *= d3d_PartScale;

	// take a copy out so that we can avoid having to read back from the buffer
	// index these as strips to help out certain hardware
	vec3_t origin2;
	VectorMultiplyAdd (p->org, scale, up, origin2);

	VectorCopy (p->org, d3d_PartState.VertsNonInstanced[0].xyz);
	d3d_PartState.VertsNonInstanced[0].color = color;

	VectorCopy (origin2, d3d_PartState.VertsNonInstanced[1].xyz);
	d3d_PartState.VertsNonInstanced[1].color = color;

	VectorMultiplyAdd (p->org, scale, right, d3d_PartState.VertsNonInstanced[2].xyz);
	d3d_PartState.VertsNonInstanced[2].color = color;

	VectorMultiplyAdd (origin2, scale, right, d3d_PartState.VertsNonInstanced[3].xyz);
	d3d_PartState.VertsNonInstanced[3].color = color;

	/*
	hr = d3d_PartVBOStream0->Unlock ();
	if (FAILED (hr)) Sys_Error ("D3DPart_SubmitParticle: failed to unlock vertex buffer");
	d3d_PartState.VertsNonInstanced = NULL;
	d3d_RenderDef.numlock++;
	*/

	d3d_PartState.VertsNonInstanced += 4;

	d3d_PartState.LockOffset += 4 * sizeof (partvertstream0_t);
	d3d_PartState.NumVertexes += 4;
	d3d_PartState.TotalVertexes += 4;

	d3d_PartState.NumIndexes += 6;
	d3d_PartState.TotalIndexes += 6;
}


/*
=================
R_CullSphere

Returns true if the sphere is outside the frustum
grabbed from AlienArena 2011 source (repaying the compliment!!!)
=================
*/
bool R_CullSphere (const float *center, const float radius, const int clipflags)
{
	int		i;
	mplane_t *p;
	extern mplane_t frustum[];

	for (i = 0, p = frustum; i < 4; i++, p++)
	{
		if (!(clipflags & (1 << i)))
			continue;

		if (DotProduct (center, p->normal) - p->dist <= -radius)
			return true;
	}

	return false;
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

		// runs faster
		if (R_CullSphere (p->org, 64, 15)) continue;

		// the particle is visible this frame
		p->visframe = d3d_RenderDef.framecount;
		pt->numactiveparticles++;
		/*
		it's faster to just render the bastards
		int pointcontents = Mod_PointInLeaf (p->org, cl.worldmodel)->contents;

		if (pointcontents == CONTENTS_SKY) p->die = -1;
		if (pointcontents == CONTENTS_SOLID) p->die = -1;
		*/
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

		if (pt->numactiveparticles)
		{
			// add to the draw list (only if there's something to draw)
			D3DAlpha_AddToList (pt);
		}
	}
}


void D3DPart_BeginParticles (void)
{
	D3D_SetTextureAddress (0, D3DTADDRESS_CLAMP);

	// switch to point sampling for particle mips as they don't need full trilinear
	D3D_SetTextureMipmap (0, d3d_TexFilter, d3d_MipFilter == D3DTEXF_NONE ? D3DTEXF_NONE : D3DTEXF_POINT);

	D3DPart_SetBuffers ();
	cachedparticletexture = NULL;
}


void D3DPart_EndParticles (void)
{
	D3DPart_Flush ();
}


void R_AddParticleTypeToRender (particle_type_t *pt)
{
	if (!pt->particles) return;
	if (!pt->numactiveparticles) return;
	if (!r_drawparticles.value) return;
	if (r_particlesize.value < 0.001f) return;

	// for rendering
	vec3_t up, right;
	float scale = 1;	// ensure that it has a value

	if (d3d_GlobalCaps.supportInstancing)
	{
		D3DHLSL_SetFloatArray ("upvec", r_viewvectors.up, 3);
		D3DHLSL_SetFloatArray ("rightvec", r_viewvectors.right, 3);
	}

	// walk the list starting at the first active particle
	for (particle_t *p = pt->particles; p; p = p->next)
	{
		// particle was culled
		if (p->visframe != d3d_RenderDef.framecount) continue;

		DWORD pc = D3DCOLOR_ARGB
		(
			BYTE_CLAMP (p->alpha),
			d3d_QuakePalette.standard[(int) p->color].peRed,
			d3d_QuakePalette.standard[(int) p->color].peGreen,
			d3d_QuakePalette.standard[(int) p->color].peBlue
		);

		if (p->scalemod)
		{
			// hack a scale up to keep particles from disappearing
			// note - all of this *could* go into a vertex shader, but we'd have a pretty heavyweight vertex submission
			// and we likely wouldn't gain that much from it.
			scale = (p->org[0] - r_viewvectors.origin[0]) * r_viewvectors.forward[0] +
					(p->org[1] - r_viewvectors.origin[1]) * r_viewvectors.forward[1] +
					(p->org[2] - r_viewvectors.origin[2]) * r_viewvectors.forward[2];

			if (scale < 20)
				scale = 1;
			else scale = 1 + scale * p->scalemod;
		}
		else scale = 1;

		if (!d3d_GlobalCaps.supportInstancing)
		{
			// note - scale each particle individually
			VectorScale (r_viewvectors.up, p->scale, up);
			VectorScale (r_viewvectors.right, p->scale, right);
		}

		if (p->tex != cachedparticletexture)
		{
			D3DPart_Flush ();

			D3DHLSL_SetTexture (0, p->tex);
			D3DHLSL_SetAlpha (1.0f);
			D3DHLSL_SetPass (d3d_GlobalCaps.supportInstancing ? FX_PASS_PARTICLES_INSTANCED : FX_PASS_PARTICLES);

			cachedparticletexture = p->tex;
		}

		D3DPart_SubmitParticle (pt, p, scale, up, right, pc);
	}
}



/*
=============================================================

  SPRITE MODELS

=============================================================
*/

void D3DSprite_SetBuffers (void)
{
	// create the buffers if needed
	D3DPart_CreateBuffers ();

	// initial states
	d3d_SpriteState.NumVertexes = 0;
	d3d_SpriteState.NumIndexes = 0;

	// and set up for drawing
	D3D_SetVertexDeclaration (d3d_SpriteDecl);
	D3D_SetStreamSource (0, d3d_SpriteVBO, 0, sizeof (spritevert_t));
	D3D_SetStreamSource (1, NULL, 0, 0);
	D3D_SetStreamSource (2, NULL, 0, 0);
	D3D_SetIndices (d3d_MainIBO);
}


void D3DSprite_Flush (void)
{
	// don't check numindexes here as it will be 0 with instancing
	if (d3d_SpriteState.NumVertexes)
	{
		D3D_DrawIndexedPrimitive (d3d_SpriteState.FirstVertex,
			d3d_SpriteState.NumVertexes,
			d3d_SpriteState.FirstIndex,
			d3d_SpriteState.NumIndexes / 3);
	}

	d3d_SpriteState.FirstVertex = d3d_SpriteState.TotalVertexes;
	d3d_SpriteState.FirstIndex = d3d_SpriteState.TotalIndexes;
	d3d_SpriteState.NumVertexes = 0;
	d3d_SpriteState.NumIndexes = 0;
}


void D3DSprite_SubmitSprite (mspriteframe_t *frame, float *origin, float *up, float *right, DWORD color)
{
	float point[3];
	spritevert_t *verts = NULL;

	if (d3d_SpriteState.TotalVertexes + 4 >= MAX_SPRITE_VERTEXES)
	{
		D3DSprite_Flush ();

		hr = d3d_SpriteVBO->Lock (0, 0, (void **) &verts, D3DLOCK_DISCARD);
		if (FAILED (hr)) Sys_Error ("D3DSprite_SubmitSprite: failed to lock vertex buffer");

		d3d_SpriteState.FirstVertex = d3d_SpriteState.TotalVertexes = d3d_SpriteState.LockOffset = 0;
		d3d_SpriteState.FirstIndex = d3d_SpriteState.TotalIndexes = 0;
	}
	else
	{
		hr = d3d_SpriteVBO->Lock (d3d_SpriteState.LockOffset, 4 * sizeof (spritevert_t), (void **) &verts, D3DLOCK_NOOVERWRITE);
		if (FAILED (hr)) Sys_Error ("D3DSprite_SubmitSprite: failed to lock vertex buffer");
	}

	// index these as strips to help out certain hardware
	VectorMultiplyAdd (origin, frame->up, up, point);
	VectorMultiplyAdd (point, frame->left, right, point);
	VectorCopy (point, verts[0].xyz);

	verts[0].color = color;
	D3DPart_SetTexCoord (verts[0].st, 0, 0);

	VectorMultiplyAdd (origin, frame->up, up, point);
	VectorMultiplyAdd (point, frame->right, right, point);
	VectorCopy (point, verts[1].xyz);

	verts[1].color = color;
	D3DPart_SetTexCoord (verts[1].st, frame->s, 0);

	VectorMultiplyAdd (origin, frame->down, up, point);
	VectorMultiplyAdd (point, frame->left, right, point);
	VectorCopy (point, verts[2].xyz);

	verts[2].color = color;
	D3DPart_SetTexCoord (verts[2].st, 0, frame->t);

	VectorMultiplyAdd (origin, frame->down, up, point);
	VectorMultiplyAdd (point, frame->right, right, point);
	VectorCopy (point, verts[3].xyz);

	verts[3].color = color;
	D3DPart_SetTexCoord (verts[3].st, frame->s, frame->t);

	hr = d3d_SpriteVBO->Unlock ();
	if (FAILED (hr)) Sys_Error ("D3DSprite_SubmitSprite: failed to unlock vertex buffer");
	d3d_RenderDef.numlock++;

	d3d_SpriteState.LockOffset += 4 * sizeof (spritevert_t);
	d3d_SpriteState.NumVertexes += 4;
	d3d_SpriteState.TotalVertexes += 4;

	d3d_SpriteState.NumIndexes += 6;
	d3d_SpriteState.TotalIndexes += 6;
}


/*
================
R_GetSpriteFrame
================
*/
mspriteframe_t *R_GetSpriteFrame (entity_t *ent)
{
	msprite_t		*psprite;
	mspritegroup_t	*pspritegroup;
	mspriteframe_t	*pspriteframe;
	int				i, numframes, frame;
	float			*pintervals, fullinterval, targettime, time;

	psprite = ent->model->spritehdr;
	frame = ent->frame;

	if ((frame >= psprite->numframes) || (frame < 0))
	{
		Con_Printf ("R_DrawSprite: no such frame %d\n", frame);
		frame = 0;
	}

	if (psprite->frames[frame].type == SPR_SINGLE)
		pspriteframe = psprite->frames[frame].frameptr;
	else
	{
		pspritegroup = (mspritegroup_t *) psprite->frames[frame].frameptr;
		pintervals = pspritegroup->intervals;
		numframes = pspritegroup->numframes;
		fullinterval = pintervals[numframes - 1];

		time = cl.time + ent->syncbase;

		// when loading in Mod_LoadSpriteGroup, we guaranteed all interval values
		// are positive, so we don't have to worry about division by 0
		targettime = time - ((int) (time / fullinterval)) * fullinterval;

		for (i = 0; i < (numframes - 1); i++)
			if (pintervals[i] > targettime)
				break;

		pspriteframe = pspritegroup->frames[i];
	}

	return pspriteframe;
}


/*
=================
D3D_SetupSpriteModel

=================
*/
LPDIRECT3DTEXTURE9 cachedspritetexture = NULL;

void D3DSprite_Begin (void)
{
	cachedspritetexture = NULL;
	D3DSprite_SetBuffers ();
}


void D3DSprite_End (void)
{
	// draw anything left over
	D3DSprite_Flush ();
}


void D3DSprite_SetState (image_t *img)
{
	D3D_SetTextureAddress (0, D3DTADDRESS_CLAMP);

	D3D_SetTextureMipmap (1, d3d_TexFilter, d3d_MipFilter);
	D3DHLSL_SetTexture (0, img->d3d_Texture);
	D3DHLSL_SetAlpha (1.0f);
	D3DHLSL_SetPass (FX_PASS_PARTICLES);
}


void D3D_SetupSpriteModel (entity_t *ent)
{
	vec3_t	point;
	mspriteframe_t	*frame;
	float		*up, *right;
	avectors_t	av;
	msprite_t	*psprite;
	vec3_t		fixed_origin;
	vec3_t		temp;
	vec3_t		tvec;
	float		angle, sr, cr;

	// don't even bother culling, because it's just a single
	// polygon without a surface cache
	frame = R_GetSpriteFrame (ent);
	psprite = ent->model->spritehdr;

	VectorCopy (ent->origin, fixed_origin);

	switch (psprite->type)
	{
	case SPR_ORIENTED:
		// bullet marks on walls
		AngleVectors (ent->angles, &av);

		VectorScale (av.forward, -2, temp);
		VectorAdd (temp, fixed_origin, fixed_origin);

		up = av.up;
		right = av.right;
		break;

	case SPR_VP_PARALLEL_UPRIGHT:
		av.up[0] = 0;
		av.up[1] = 0;
		av.up[2] = 1;

		up = av.up;
		right = r_viewvectors.right;
		break;

	case SPR_FACING_UPRIGHT:
		VectorSubtract (ent->origin, r_viewvectors.origin, av.forward);
		av.forward[2] = 0;
		VectorNormalize (av.forward);

		av.right[0] = av.forward[1];
		av.right[1] = -av.forward[0];
		av.right[2] = 0;

		av.up[0] = 0;
		av.up[1] = 0;
		av.up[2] = 1;

		up = av.up;
		right = av.right;
		break;

	case SPR_VP_PARALLEL:
		// normal sprite
		up = r_viewvectors.up;
		right = r_viewvectors.right;
		break;

	case SPR_VP_PARALLEL_ORIENTED:
		angle = ent->angles[ROLL] * (D3DX_PI / 180.0);
		sr = sin (angle);
		cr = cos (angle);

		av.right[0] = r_viewvectors.right[0] * cr + r_viewvectors.up[0] * sr;
		av.right[1] = r_viewvectors.right[1] * cr + r_viewvectors.up[1] * sr;
		av.right[2] = r_viewvectors.right[2] * cr + r_viewvectors.up[2] * sr;

		av.up[0] = r_viewvectors.right[0] * -sr + r_viewvectors.up[0] * cr;
		av.up[1] = r_viewvectors.right[1] * -sr + r_viewvectors.up[1] * cr;
		av.up[2] = r_viewvectors.right[2] * -sr + r_viewvectors.up[2] * cr;

		up = av.up;
		right = av.right;
		break;

	default:
		// unknown type - just assume it's normal and Con_DPrintf it
		up = r_viewvectors.up;
		right = r_viewvectors.right;
		Con_DPrintf ("D3D_SetupSpriteModel - Unknown Sprite Type %i\n", psprite->type);
		break;
	}

	// batch sprites
	if (cachedspritetexture != frame->texture->d3d_Texture)
	{
		D3DSprite_Flush ();
		D3DSprite_SetState (frame->texture);

		cachedspritetexture = frame->texture->d3d_Texture;
	}

	D3DSprite_SubmitSprite (frame, fixed_origin, up, right, D3DCOLOR_ARGB (BYTE_CLAMP (ent->alphaval), 255, 255, 255));
}


