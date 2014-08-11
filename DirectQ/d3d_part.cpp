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
#include "d3d_Quads.h"

extern cvar_t sv_gravity;

typedef struct brightfield_s
{
	float normal[3];
	float avelocity[2];
	float coord[2];
} brightfield_t;


typedef struct effect_s
{
	float origin[3];
	float vel[3];
	float dvel[3];
	float grav;
	float ramptime;
	float ramp;
	float coord[2];
	float scale;
} effect_t;

LPDIRECT3DVERTEXDECLARATION9 d3d_PartInstDecl = NULL;
LPDIRECT3DVERTEXDECLARATION9 d3d_BrightDecl = NULL;
LPDIRECT3DVERTEXDECLARATION9 d3d_EffectDecl = NULL;

LPDIRECT3DVERTEXBUFFER9 d3d_PartVBO = NULL;
LPDIRECT3DVERTEXBUFFER9 d3d_BrightVBO = NULL;
LPDIRECT3DVERTEXBUFFER9 d3d_EffectVBO = NULL;

LPDIRECT3DINDEXBUFFER9 d3d_ParticleIBO = NULL;

extern float r_avertexnormals[NUMVERTEXNORMALS][3];

void D3DPart_OnLoss (void)
{
	SAFE_RELEASE (d3d_PartVBO);
	SAFE_RELEASE (d3d_BrightVBO);
	SAFE_RELEASE (d3d_EffectVBO);

	SAFE_RELEASE (d3d_ParticleIBO);

	SAFE_RELEASE (d3d_EffectDecl);
	SAFE_RELEASE (d3d_PartInstDecl);
	SAFE_RELEASE (d3d_BrightDecl);
}


typedef struct d3d_partinstvert_s
{
	float xyz[3];
} d3d_partinstvert_t;

void D3DPart_OnRecover (void)
{
	if (!d3d_EffectDecl)
	{
		D3DVERTEXELEMENT9 effectlayout[] =
		{
			VDECL (0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0),		// origin
			VDECL (0, 12, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0),		// vel
			VDECL (0, 24, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1),		// dvel
			VDECL (0, 36, D3DDECLTYPE_FLOAT1, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 2),		// grav
			VDECL (0, 40, D3DDECLTYPE_FLOAT1, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 3),		// ramptime
			VDECL (0, 44, D3DDECLTYPE_FLOAT1, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 4),		// ramp
			VDECL (0, 48, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 5),		// coord
			VDECL (0, 56, D3DDECLTYPE_FLOAT1, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 6),		// scale
			D3DDECL_END ()
		};

		hr = d3d_Device->CreateVertexDeclaration (effectlayout, &d3d_EffectDecl);
		if (FAILED (hr)) Sys_Error ("D3DPart_OnRecover : d3d_Device->CreateVertexDeclaration failed");
	}

	if (!d3d_EffectVBO)
	{
		effect_t *verts = NULL;

		D3DMain_CreateVertexBuffer (1024 * 4 * sizeof (effect_t), D3DUSAGE_WRITEONLY, &d3d_EffectVBO);
		hr = d3d_EffectVBO->Lock (0, 0, (void **) &verts, d3d_GlobalCaps.DefaultLock);
		if (FAILED (hr)) Sys_Error ("D3DPart_OnRecover : failed to lock vertex buffer");

		for (int i = 0; i < 1024; i++, verts += 4)
		{
			float org[3] = {(rand () % 32) - 16, (rand () % 32) - 16, (rand () % 32) - 16};
			float vel[3] = {(rand () % 512) - 256, (rand () % 512) - 256, (rand () % 512) - 256};
			float ramp = rand () & 3;

			for (int j = 0; j < 4; j++)
			{
				verts[j].origin[0] = org[0];
				verts[j].origin[1] = org[1];
				verts[j].origin[2] = org[2];

				verts[j].vel[0] = vel[0];
				verts[j].vel[1] = vel[1];
				verts[j].vel[2] = vel[2];

				if (i & 1)
				{
					verts[j].ramptime = 10;
					verts[j].dvel[0] = verts[j].dvel[1] = verts[j].dvel[2] = 4;
				}
				else
				{
					verts[j].ramptime = 15;
					verts[j].dvel[0] = verts[j].dvel[1] = verts[j].dvel[2] = -1;
				}

				verts[j].ramp = ramp;
				verts[j].grav = -1;
				verts[j].scale = 2.666f;
			}

			verts[0].coord[0] = -1;
			verts[0].coord[1] = -1;

			verts[1].coord[0] = -1;
			verts[1].coord[1] = 1;

			verts[2].coord[0] = 1;
			verts[2].coord[1] = 1;

			verts[3].coord[0] = 1;
			verts[3].coord[1] = -1;
		}

		hr = d3d_EffectVBO->Unlock ();
		d3d_RenderDef.numlock++;
		if (FAILED (hr)) Sys_Error ("D3DPart_OnRecover : failed to unlock vertex buffer");
	}

	if (!d3d_ParticleIBO)
	{
		D3DMain_CreateIndexBuffer16 (1024 * 6, D3DUSAGE_WRITEONLY, &d3d_ParticleIBO);

		// now we fill in the index buffer; this is a non-dynamic index buffer and it only needs to be set once
		unsigned short *ndx = NULL;

		hr = d3d_ParticleIBO->Lock (0, 0, (void **) &ndx, d3d_GlobalCaps.DefaultLock);
		if (FAILED (hr)) Sys_Error ("D3DPart_OnRecover: failed to lock index buffer");

		for (int i = 0; i < 1024 * 4; i += 4, ndx += 6)
		{
			ndx[0] = i + 0;
			ndx[1] = i + 1;
			ndx[2] = i + 2;

			ndx[3] = i + 0;
			ndx[4] = i + 2;
			ndx[5] = i + 3;
		}

		hr = d3d_ParticleIBO->Unlock ();
		d3d_RenderDef.numlock++;
		if (FAILED (hr)) Sys_Error ("D3DPart_OnRecover: failed to unlock index buffer");
	}

	if (!d3d_BrightDecl)
	{
		D3DVERTEXELEMENT9 brightlayout[] =
		{
			VDECL (0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0),
			VDECL (0, 12, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0),
			VDECL (0, 20, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1),
			D3DDECL_END ()
		};

		hr = d3d_Device->CreateVertexDeclaration (brightlayout, &d3d_BrightDecl);
		if (FAILED (hr)) Sys_Error ("D3DPart_OnRecover : d3d_Device->CreateVertexDeclaration failed");
	}

	if (!d3d_BrightVBO)
	{
		brightfield_t *verts = NULL;

		D3DMain_CreateVertexBuffer (NUMVERTEXNORMALS * 4 * sizeof (brightfield_t), D3DUSAGE_WRITEONLY, &d3d_BrightVBO);
		hr = d3d_BrightVBO->Lock (0, 0, (void **) &verts, d3d_GlobalCaps.DefaultLock);
		if (FAILED (hr)) Sys_Error ("D3DPart_OnRecover : failed to lock vertex buffer");

		for (int i = 0; i < NUMVERTEXNORMALS; i++, verts += 4)
		{
			// roll on geometry shader
			float avel[2] =
			{
				(float) (rand () & 255) * 0.01,
				(float) (rand () & 255) * 0.01
			};

			for (int j = 0; j < 4; j++)
			{
				verts[j].normal[0] = r_avertexnormals[i][0] * 64.0f;
				verts[j].normal[1] = r_avertexnormals[i][1] * 64.0f;
				verts[j].normal[2] = r_avertexnormals[i][2] * 64.0f;

				verts[j].avelocity[0] = avel[0];
				verts[j].avelocity[1] = avel[1];
			}

			verts[0].coord[0] = -1;
			verts[0].coord[1] = -1;

			verts[1].coord[0] = -1;
			verts[1].coord[1] = 1;

			verts[2].coord[0] = 1;
			verts[2].coord[1] = 1;

			verts[3].coord[0] = 1;
			verts[3].coord[1] = -1;
		}

		hr = d3d_BrightVBO->Unlock ();
		d3d_RenderDef.numlock++;
		if (FAILED (hr)) Sys_Error ("D3DPart_OnRecover : failed to unlock vertex buffer");
	}

	if (!d3d_PartInstDecl)
	{
		D3DVERTEXELEMENT9 d3d_partinstlayout[] =
		{
			VDECL (0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0),
			D3DDECL_END ()
		};

		hr = d3d_Device->CreateVertexDeclaration (d3d_partinstlayout, &d3d_PartInstDecl);
		if (FAILED (hr)) Sys_Error ("D3DPart_OnRecover : d3d_Device->CreateVertexDeclaration failed");
	}

	if (!d3d_PartVBO)
	{
		d3d_partinstvert_t *verts = NULL;

		D3DMain_CreateVertexBuffer (d3d_GlobalCaps.MaxParticleBatch * 4 * sizeof (d3d_partinstvert_t), D3DUSAGE_WRITEONLY, &d3d_PartVBO);
		hr = d3d_PartVBO->Lock (0, 0, (void **) &verts, d3d_GlobalCaps.DefaultLock);
		if (FAILED (hr)) Sys_Error ("D3DPart_OnRecover : failed to lock vertex buffer");

		for (int i = 0; i < d3d_GlobalCaps.MaxParticleBatch; i++, verts += 4)
		{
			// correct winding order
			verts[0].xyz[0] = -1;
			verts[0].xyz[1] = -1;
			verts[0].xyz[2] = i;

			verts[1].xyz[0] = -1;
			verts[1].xyz[1] = 1;
			verts[1].xyz[2] = i;

			verts[2].xyz[0] = 1;
			verts[2].xyz[1] = 1;
			verts[2].xyz[2] = i;

			verts[3].xyz[0] = 1;
			verts[3].xyz[1] = -1;
			verts[3].xyz[2] = i;
		}

		hr = d3d_PartVBO->Unlock ();
		d3d_RenderDef.numlock++;
		if (FAILED (hr)) Sys_Error ("D3DPart_OnRecover : failed to unlock vertex buffer");
	}
}

CD3DDeviceLossHandler d3d_PartHandler (D3DPart_OnLoss, D3DPart_OnRecover);

cvar_t r_particlesize ("r_particlesize", "1", CVAR_ARCHIVE);
cvar_t r_drawparticles ("r_drawparticles", "1", CVAR_ARCHIVE);
cvar_alias_t r_particles ("r_particles", &r_drawparticles);
cvar_t r_particlestyle ("r_particlestyle", "0", CVAR_ARCHIVE);
cvar_t r_particledistscale ("r_particledistscale", "0.004", CVAR_ARCHIVE);

float d3d_PartScale;
int d3d_NumDrawSprites = 0;


void D3DPart_BeginBrightField (void)
{
	// distance scaling is a function of the current display mode and a user-selectable value
	// if the resolution is smaller distant particles will map to sub-pixel sizes so we try to prevent that
	if (r_particledistscale.value < 0) Cvar_Set (&r_particledistscale, 0.0f);
	if (r_particledistscale.value > 0.02f) Cvar_Set (&r_particledistscale, 0.02f);

	D3DHLSL_SetTexture (0, NULL);
	D3DHLSL_SetPass (r_particlestyle.integer ? FX_PASS_BRIGHTFIELD_SQUARE : FX_PASS_BRIGHTFIELD);

	if (d3d_CurrentMode.Height < d3d_CurrentMode.Width)
		D3DHLSL_SetFloat ("genericscale", (r_particledistscale.value * 480.0f) / (float) d3d_CurrentMode.Height);
	else D3DHLSL_SetFloat ("genericscale", (r_particledistscale.value * 640.0f) / (float) d3d_CurrentMode.Width);

	D3DHLSL_SetFloat ("cltime", cl.time);
	D3DHLSL_SetFloat ("brightscale", 0.3f * r_particlesize.value * 2.666f);
	D3DHLSL_SetFloatArray ("brightcolor", d3d_QuakePalette.colorfloat[0x6f], 4);

	D3D_SetStreamSource (0, d3d_BrightVBO, 0, sizeof (brightfield_t));
	D3D_SetStreamSource (1, NULL, 0, 0);
	D3D_SetStreamSource (2, NULL, 0, 0);

	D3D_SetIndices (d3d_ParticleIBO);

	D3D_SetVertexDeclaration (d3d_BrightDecl);

	D3DHLSL_SetAlpha (1.0f);
}


void D3DPart_EndBrightField (void)
{
}


void D3DPart_DrawBrightField (float *origin)
{
	D3DHLSL_SetFloatArray ("entorigin", origin, 3);
	D3D_DrawIndexedPrimitive (0, NUMVERTEXNORMALS * 4, 0, NUMVERTEXNORMALS * 2);
}


void D3DPart_BeginParticles (void)
{
	// distance scaling is a function of the current display mode and a user-selectable value
	// if the resolution is smaller distant particles will map to sub-pixel sizes so we try to prevent that
	if (r_particledistscale.value < 0) Cvar_Set (&r_particledistscale, 0.0f);
	if (r_particledistscale.value > 0.02f) Cvar_Set (&r_particledistscale, 0.02f);

	D3DHLSL_SetTexture (0, NULL);
	D3DHLSL_SetPass (r_particlestyle.integer ? FX_PASS_PARTICLE_SQUARE : FX_PASS_PARTICLES);

	// keep sizes consistent
	d3d_PartScale = 0.3f * r_particlesize.value;

	if (d3d_CurrentMode.Height < d3d_CurrentMode.Width)
		D3DHLSL_SetFloat ("genericscale", (r_particledistscale.value * 480.0f) / (float) d3d_CurrentMode.Height);
	else D3DHLSL_SetFloat ("genericscale", (r_particledistscale.value * 640.0f) / (float) d3d_CurrentMode.Width);

	D3D_SetStreamSource (0, d3d_PartVBO, 0, sizeof (d3d_partinstvert_t));
	D3D_SetStreamSource (1, NULL, 0, 0);
	D3D_SetStreamSource (2, NULL, 0, 0);

	D3D_SetIndices (d3d_ParticleIBO);

	D3D_SetVertexDeclaration (d3d_PartInstDecl);

	D3DHLSL_SetAlpha (1.0f);

	// no sprite cache
	d3d_NumDrawSprites = 0;
}


void D3DPart_EndParticles (void)
{
	if (d3d_NumDrawSprites)
	{
		// reset the arrays
		D3DXVECTOR4 *positions = (D3DXVECTOR4 *) scratchbuf;
		D3DXVECTOR4 *colours = (positions + d3d_GlobalCaps.MaxParticleBatch);

		D3DHLSL_SetVectorArray ("PartInstancePosition", positions, d3d_NumDrawSprites);
		D3DHLSL_SetVectorArray ("PartInstanceColor", colours, d3d_NumDrawSprites);

		D3D_DrawIndexedPrimitive (0, d3d_NumDrawSprites * 4, 0, d3d_NumDrawSprites * 2);
		d3d_NumDrawSprites = 0;
	}
}


#define D3DPart_SetPosition(pos, xyz, extra) pos.x = xyz[0]; pos.y = xyz[1]; pos.z = xyz[2]; pos.w = extra;
#define D3DPart_SetColor(col, rgb, alpha) col.x = rgb[0]; col.y = rgb[1]; col.z = rgb[2]; col.w = alpha;

void R_AddParticleEmitterToRender (particle_emitter_t *pe)
{
	if (!pe->particles) return;
	if (!pe->numactiveparticles) return;
	if (!r_drawparticles.value) return;
	if (r_particlesize.value < 0.001f) return;

	// we don't have a geometry shader but we do have shader instancing ;)
	// (this will even work on ps 2.0 hardware and is so much better than point sprites)
	D3DXVECTOR4 *positions = (D3DXVECTOR4 *) scratchbuf;
	D3DXVECTOR4 *colours = (positions + d3d_GlobalCaps.MaxParticleBatch);

	float etime = cl.time - pe->spawntime;
	float grav = etime * sv_gravity.value * 0.05f;
	float porg[3];
	int ramp;
	float *color;

	// walk the list starting at the first active particle
	for (particle_t *p = pe->particles; p; p = p->next)
	{
		if (d3d_NumDrawSprites >= d3d_GlobalCaps.MaxParticleBatch)
			D3DPart_EndParticles ();

		// all nicely setup for a move to the GPU...
		// (these ramps should move to texture lookups) (they could be vertex textures - in the geometry shader, using .Load instead of .Sample)
		if (p->colorramp)
		{
			// colour ramps
			ramp = p->ramp + (p->ramptime * etime);

			// adjust color for ramp (don't overflow the ramp array)
			// (this will go away when we move to the GPU)
			if (ramp > 10 || p->colorramp[ramp] < 0)
			{
				// no drawing needed
				p->die = -1;
				continue;
			}

			color = d3d_QuakePalette.colorfloat[p->colorramp[ramp]];
		}
		else color = d3d_QuakePalette.colorfloat[p->color & 255];

		// final origin from velocity (move this to the GPU)
		// (etime is specifically per-emitter so we need to break batches at that)
		// (or add it to the constants on a per-particle basis)
		porg[0] = p->org[0] + (p->vel[0] + (p->dvel[0] * etime)) * etime;
		porg[1] = p->org[1] + (p->vel[1] + (p->dvel[1] * etime)) * etime;
		porg[2] = p->org[2] + (p->vel[2] + (p->dvel[2] * etime) + (grav * p->grav)) * etime;

		D3DPart_SetPosition (positions[d3d_NumDrawSprites], porg, p->scale * d3d_PartScale);
		D3DPart_SetColor (colours[d3d_NumDrawSprites], color, p->alpha);

		d3d_NumDrawSprites++;
	}
}


/*
=============================================================

  CORONAS

  Coronas are just a special case of particles; they use
  slightly different shaders but everything else is the same

=============================================================
*/


void D3DPart_BeginCoronas (void)
{
	D3D_SetStreamSource (0, d3d_PartVBO, 0, sizeof (d3d_partinstvert_t));
	D3D_SetStreamSource (1, NULL, 0, 0);
	D3D_SetStreamSource (2, NULL, 0, 0);

	D3D_SetIndices (d3d_ParticleIBO);
	D3D_SetVertexDeclaration (d3d_PartInstDecl);

	D3DHLSL_SetAlpha (1.0f);
	D3DHLSL_SetFloat ("genericscale", 0.002f);
	D3DHLSL_SetTexture (0, NULL);
	D3DHLSL_SetPass (FX_PASS_CORONA);

	// no sprite cache
	d3d_NumDrawSprites = 0;
}


void D3DPart_CommitCoronas (void)
{
	// reset the arrays
	D3DXVECTOR4 *positions = (D3DXVECTOR4 *) scratchbuf;
	D3DXVECTOR4 *colours = (positions + d3d_GlobalCaps.MaxParticleBatch);

	D3DHLSL_SetVectorArray ("PartInstancePosition", positions, d3d_NumDrawSprites);
	D3DHLSL_SetVectorArray ("PartInstanceColor", colours, d3d_NumDrawSprites);
	D3D_DrawIndexedPrimitive (0, d3d_NumDrawSprites * 4, 0, d3d_NumDrawSprites * 2);

	d3d_NumDrawSprites = 0;
}


void D3DPart_DrawSingleCorona (float *origin, float *color, float radius)
{
	if (d3d_NumDrawSprites >= d3d_GlobalCaps.MaxParticleBatch)
		D3DPart_CommitCoronas ();

	// we don't have a geometry shader but we do have shader instancing ;)
	// (this will even work on ps 2.0 hardware and is so much better than point sprites)
	D3DXVECTOR4 *positions = (D3DXVECTOR4 *) scratchbuf;
	D3DXVECTOR4 *colours = (positions + d3d_GlobalCaps.MaxParticleBatch);

	D3DPart_SetPosition (positions[d3d_NumDrawSprites], origin, radius);
	D3DPart_SetColor (colours[d3d_NumDrawSprites], color, 1);

	d3d_NumDrawSprites++;
}


void D3DPart_BeginEffect (void)
{
	// distance scaling is a function of the current display mode and a user-selectable value
	// if the resolution is smaller distant particles will map to sub-pixel sizes so we try to prevent that
	if (r_particledistscale.value < 0) Cvar_Set (&r_particledistscale, 0.0f);
	if (r_particledistscale.value > 0.02f) Cvar_Set (&r_particledistscale, 0.02f);

	if (d3d_CurrentMode.Height < d3d_CurrentMode.Width)
		D3DHLSL_SetFloat ("genericscale", (r_particledistscale.value * 480.0f) / (float) d3d_CurrentMode.Height);
	else D3DHLSL_SetFloat ("genericscale", (r_particledistscale.value * 640.0f) / (float) d3d_CurrentMode.Width);

	D3DHLSL_SetTexture (0, NULL);
	D3DHLSL_SetAlpha (1.0f);
	D3DHLSL_SetFloat ("partscale", 0.3f * r_particlesize.value);

	D3D_SetVertexDeclaration (d3d_EffectDecl);

	D3D_SetStreamSource (1, NULL, 0, 0);
	D3D_SetStreamSource (2, NULL, 0, 0);

	D3D_SetIndices (d3d_ParticleIBO);

	D3DHLSL_SetPass (r_particlestyle.integer ? FX_PASS_PARTEFFECT_SQUARE : FX_PASS_PARTEFFECT);
}


void D3DPart_EndEffect (void)
{
}


void D3DPart_DrawEffect (particle_effect_t *effect)
{
	if (effect->type == peff_explosion)
	{
		D3DHLSL_SetFloat ("cltime", cl.time - effect->starttime);
		D3DHLSL_SetFloat ("partgrav", (cl.time - effect->starttime) * sv_gravity.value * 0.05f);
		D3DHLSL_SetFloatArray ("entorigin", effect->origin, 3);

		D3D_SetStreamSource (0, d3d_EffectVBO, 0, sizeof (effect_t));
		D3D_DrawIndexedPrimitive (0, 1024 * 4, 0, 1024 * 2);
	}
}


