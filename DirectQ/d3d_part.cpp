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

LPDIRECT3DTEXTURE9 particletexture = NULL;
LPDIRECT3DVERTEXDECLARATION9 d3d_PartInstDecl = NULL;

LPDIRECT3DINDEXBUFFER9 d3d_PartIBO = NULL;
LPDIRECT3DVERTEXBUFFER9 d3d_PartVBO = NULL;

void D3DPart_OnLoss (void)
{
	SAFE_RELEASE (d3d_PartVBO);
	SAFE_RELEASE (d3d_PartIBO);
	SAFE_RELEASE (d3d_PartInstDecl);
}


typedef struct d3d_partinstvert_s
{
	float xyz[3];
} d3d_partinstvert_t;

// vs 2.0 guarantees 256 constants table registers available
// we're already using 9 of these so we'll limit batch sizes to 120 and save the rest for headroom
#define MAX_PARTICLE_BATCH	120

// the enhanced system needs to store positions, colours and texcoords
#define MAX_PARTICLE_BATCH_ENHANCED		80

void D3DPart_OnRecover (void)
{
	if (!d3d_PartInstDecl)
	{
		D3DVERTEXELEMENT9 d3d_partinstlayout[] =
		{
			{0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
			D3DDECL_END ()
		};

		hr = d3d_Device->CreateVertexDeclaration (d3d_partinstlayout, &d3d_PartInstDecl);
		if (FAILED (hr)) Sys_Error ("D3DPart_OnRecover : d3d_Device->CreateVertexDeclaration failed");
	}

	if (!d3d_PartVBO)
	{
		// can reuse the quads index buffer from d3d_quads
		d3d_partinstvert_t *verts = NULL;

		D3DMain_CreateVertexBuffer (MAX_PARTICLE_BATCH * 4 * sizeof (d3d_partinstvert_t), D3DUSAGE_WRITEONLY, &d3d_PartVBO);
		hr = d3d_PartVBO->Lock (0, 0, (void **) &verts, d3d_GlobalCaps.DefaultLock);
		if (FAILED (hr)) Sys_Error ("D3DPart_OnRecover : failed to lock vertex buffer");

		for (int i = 0; i < MAX_PARTICLE_BATCH; i++, verts += 4)
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

	if (!d3d_PartIBO)
	{
		D3DMain_CreateIndexBuffer (MAX_PARTICLE_BATCH * 6, D3DUSAGE_WRITEONLY, &d3d_PartIBO);

		// now we fill in the index buffer; this is a non-dynamic index buffer and it only needs to be set once
		unsigned short *ndx = NULL;
		int NumQuadVerts = MAX_PARTICLE_BATCH * 4;

		hr = d3d_PartIBO->Lock (0, 0, (void **) &ndx, d3d_GlobalCaps.DefaultLock);
		if (FAILED (hr)) Sys_Error ("D3DPart_OnRecover: failed to lock index buffer");

		for (int i = 0; i < NumQuadVerts; i += 4, ndx += 6)
		{
			ndx[0] = i + 0;
			ndx[1] = i + 1;
			ndx[2] = i + 2;

			ndx[3] = i + 0;
			ndx[4] = i + 2;
			ndx[5] = i + 3;
		}

		hr = d3d_PartIBO->Unlock ();
		d3d_RenderDef.numlock++;
		if (FAILED (hr)) Sys_Error ("D3DPart_OnRecover: failed to unlock index buffer");
	}
}

CD3DDeviceLossHandler d3d_PartHandler (D3DPart_OnLoss, D3DPart_OnRecover);

cvar_t r_particlesize ("r_particlesize", "1", CVAR_ARCHIVE);
cvar_t r_drawparticles ("r_drawparticles", "1", CVAR_ARCHIVE);
cvar_alias_t r_particles ("r_particles", &r_drawparticles);
cvar_t r_particlestyle ("r_particlestyle", "0", CVAR_ARCHIVE);
cvar_t r_particledistscale ("r_particledistscale", "0.004", CVAR_ARCHIVE);

float d3d_PartScale;

// made this a global because otherwise multiple particle types don't get batched at all!!!
LPDIRECT3DTEXTURE9 cachedspritetexture = NULL;

entity_t *d3d_DrawSprites[D3D_MAX_QUADS];
int d3d_NumDrawSprites = 0;


void D3DPart_BeginParticles (void)
{
	// distance scaling is a function of the current display mode and a user-selectable value
	// if the resolution is smaller distant particles will map to sub-pixel sizes so we try to prevent that
	if (r_particledistscale.value < 0) Cvar_Set (&r_particledistscale, 0.0f);
	if (r_particledistscale.value > 0.02f) Cvar_Set (&r_particledistscale, 0.02f);

	if (r_particlestyle.integer > 1)
	{
		D3DHLSL_SetTexture (0, particletexture);
		D3DHLSL_SetPass (FX_PASS_PARTICLE_ENHANCED);

		// keep sizes consistent
		d3d_PartScale = r_particlesize.value;

		// take the hack-scale-up factor down a little so it's not excessive but still enough to be effective
		if (d3d_CurrentMode.Height < d3d_CurrentMode.Width)
			D3DHLSL_SetFloat ("genericscale", (r_particledistscale.value * 240.0f) / (float) d3d_CurrentMode.Height);
		else D3DHLSL_SetFloat ("genericscale", (r_particledistscale.value * 320.0f) / (float) d3d_CurrentMode.Width);
	}
	else
	{
		D3DHLSL_SetTexture (0, NULL);
		D3DHLSL_SetPass (r_particlestyle.integer ? FX_PASS_PARTICLE_SQUARE : FX_PASS_PARTICLES);

		// keep sizes consistent
		d3d_PartScale = 0.3f * r_particlesize.value;

		if (d3d_CurrentMode.Height < d3d_CurrentMode.Width)
			D3DHLSL_SetFloat ("genericscale", (r_particledistscale.value * 480.0f) / (float) d3d_CurrentMode.Height);
		else D3DHLSL_SetFloat ("genericscale", (r_particledistscale.value * 640.0f) / (float) d3d_CurrentMode.Width);
	}

	D3D_SetStreamSource (0, d3d_PartVBO, 0, sizeof (d3d_partinstvert_t));
	D3D_SetStreamSource (1, NULL, 0, 0);
	D3D_SetStreamSource (2, NULL, 0, 0);

	D3D_SetIndices (d3d_PartIBO);

	D3D_SetVertexDeclaration (d3d_PartInstDecl);

	D3DHLSL_SetAlpha (1.0f);

	// no sprite cache
	d3d_NumDrawSprites = 0;
}


void D3DPart_EndParticles (void)
{
	if (d3d_NumDrawSprites)
	{
		if (r_particlestyle.integer > 1)
		{
			D3DXVECTOR4 *positions = (D3DXVECTOR4 *) scratchbuf;
			D3DXVECTOR4 *colours = (positions + MAX_PARTICLE_BATCH_ENHANCED);
			D3DXVECTOR4 *texcoords = (colours + MAX_PARTICLE_BATCH_ENHANCED);

			D3DHLSL_SetVectorArray ("PartInstancePositionEnh", positions, d3d_NumDrawSprites);
			D3DHLSL_SetVectorArray ("PartInstanceColorEnh", colours, d3d_NumDrawSprites);
			D3DHLSL_SetVectorArray ("PartInstanceTexCoordEnh", texcoords, d3d_NumDrawSprites);
		}
		else
		{
			// reset the arrays
			D3DXVECTOR4 *positions = (D3DXVECTOR4 *) scratchbuf;
			D3DXVECTOR4 *colours = (positions + MAX_PARTICLE_BATCH);

			D3DHLSL_SetVectorArray ("PartInstancePosition", positions, d3d_NumDrawSprites);
			D3DHLSL_SetVectorArray ("PartInstanceColor", colours, d3d_NumDrawSprites);
		}

		D3D_DrawIndexedPrimitive (0, d3d_NumDrawSprites * 4, 0, d3d_NumDrawSprites * 2);
		d3d_NumDrawSprites = 0;
	}
}


#define D3DPart_SetPosition(pos, xyz, extra) pos.x = xyz[0]; pos.y = xyz[1]; pos.z = xyz[2]; pos.w = extra;
#define D3DPart_SetColor(col, rgb, alpha) col.x = rgb[0]; col.y = rgb[1]; col.z = rgb[2]; col.w = alpha;
#define D3DPart_SetTexCoord(tc, stbase, stadd) tc.x = stbase[0]; tc.y = stbase[1]; tc.z = stadd[0]; tc.w = stadd[1];

void R_AddParticleTypeToRender (particle_type_t *pt)
{
	if (!pt->particles) return;
	if (!pt->numactiveparticles) return;
	if (!r_drawparticles.value) return;
	if (r_particlesize.value < 0.001f) return;

	if (r_particlestyle.integer > 1)
	{
		// we don't have a geometry shader but we do have shader instancing ;)
		// (this will even work on ps 2.0 hardware and is so much better than point sprites)
		D3DXVECTOR4 *positions = (D3DXVECTOR4 *) scratchbuf;
		D3DXVECTOR4 *colours = (positions + MAX_PARTICLE_BATCH_ENHANCED);
		D3DXVECTOR4 *texcoords = (colours + MAX_PARTICLE_BATCH_ENHANCED);

		for (particle_t *p = pt->particles; p; p = p->next)
		{
			// clamp at either of the two highest so that we can billboard on the GPU
			if (d3d_NumDrawSprites >= MAX_PARTICLE_BATCH_ENHANCED)
				D3DPart_EndParticles ();

			float *color = d3d_QuakePalette.colorfloat[(int) p->color & 255];

			D3DPart_SetPosition (positions[d3d_NumDrawSprites], p->org, p->scale * d3d_PartScale);
			D3DPart_SetColor (colours[d3d_NumDrawSprites], color, p->alpha);
			D3DPart_SetTexCoord (texcoords[d3d_NumDrawSprites], p->st->stbase, p->st->stadd);

			d3d_NumDrawSprites++;
		}
	}
	else
	{
		// we don't have a geometry shader but we do have shader instancing ;)
		// (this will even work on ps 2.0 hardware and is so much better than point sprites)
		D3DXVECTOR4 *positions = (D3DXVECTOR4 *) scratchbuf;
		D3DXVECTOR4 *colours = (positions + MAX_PARTICLE_BATCH);

		// walk the list starting at the first active particle
		for (particle_t *p = pt->particles; p; p = p->next)
		{
			if (d3d_NumDrawSprites >= MAX_PARTICLE_BATCH)
				D3DPart_EndParticles ();

			float *color = d3d_QuakePalette.colorfloat[(int) p->color & 255];

			D3DPart_SetPosition (positions[d3d_NumDrawSprites], p->org, p->scale * d3d_PartScale);
			D3DPart_SetColor (colours[d3d_NumDrawSprites], color, p->alpha);

			d3d_NumDrawSprites++;
		}
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

	D3D_SetIndices (d3d_PartIBO);
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
	D3DXVECTOR4 *colours = (positions + MAX_PARTICLE_BATCH);

	D3DHLSL_SetVectorArray ("PartInstancePosition", positions, d3d_NumDrawSprites);
	D3DHLSL_SetVectorArray ("PartInstanceColor", colours, d3d_NumDrawSprites);
	D3D_DrawIndexedPrimitive (0, d3d_NumDrawSprites * 4, 0, d3d_NumDrawSprites * 2);

	d3d_NumDrawSprites = 0;
}


void D3DPart_DrawSingleCorona (float *origin, float *color, float radius)
{
	if (d3d_NumDrawSprites >= MAX_PARTICLE_BATCH)
		D3DPart_CommitCoronas ();

	// we don't have a geometry shader but we do have shader instancing ;)
	// (this will even work on ps 2.0 hardware and is so much better than point sprites)
	D3DXVECTOR4 *positions = (D3DXVECTOR4 *) scratchbuf;
	D3DXVECTOR4 *colours = (positions + MAX_PARTICLE_BATCH);

	D3DPart_SetPosition (positions[d3d_NumDrawSprites], origin, radius);
	D3DPart_SetColor (colours[d3d_NumDrawSprites], color, 1);

	d3d_NumDrawSprites++;
}


/*
=============================================================

  SPRITE MODELS

=============================================================
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


void D3DSprite_DrawBatch (void)
{
	if (d3d_NumDrawSprites)
	{
		quadvert_t *verts = NULL;

		D3DQuads_Begin (d3d_NumDrawSprites, &verts);

		for (int i = 0; i < d3d_NumDrawSprites; i++, verts += 4)
		{
			vec3_t	point;
			float		*up, *right;
			avectors_t	av;
			vec3_t		fixed_origin;
			vec3_t		temp;
			float		angle, sr, cr;
			entity_t	*ent = d3d_DrawSprites[i];

			// don't even bother culling, because it's just a single
			// polygon without a surface cache
			mspriteframe_t *frame = ent->currspriteframe;
			msprite_t *psprite = ent->model->spritehdr;

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
				VectorSubtract (ent->origin, r_refdef.vieworigin, av.forward);
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

			D3DCOLOR color;
			
			if (ent->alphaval > 0 && ent->alphaval < 255)
				color = D3DCOLOR_ARGB (BYTE_CLAMP (ent->alphaval), 255, 255, 255);
			else color = 0xffffffff;

			VectorMad (fixed_origin, frame->up, up, point);
			VectorMad (point, frame->left, right, verts[0].xyz);
			verts[0].color = color;
			verts[0].st[0] = 0;
			verts[0].st[1] = 0;

			VectorMad (fixed_origin, frame->up, up, point);
			VectorMad (point, frame->right, right, verts[1].xyz);
			verts[1].color = color;
			verts[1].st[0] = frame->s;
			verts[1].st[1] = 0;

			VectorMad (fixed_origin, frame->down, up, point);
			VectorMad (point, frame->right, right, verts[2].xyz);
			verts[2].color = color;
			verts[2].st[0] = frame->s;
			verts[2].st[1] = frame->t;

			VectorMad (fixed_origin, frame->down, up, point);
			VectorMad (point, frame->left, right, verts[3].xyz);
			verts[3].color = color;
			verts[3].st[0] = 0;
			verts[3].st[1] = frame->t;
		}

		D3DQuads_End ();

		d3d_NumDrawSprites = 0;
	}
}


void D3D_SetupSpriteModel (entity_t *ent)
{
	if (d3d_NumDrawSprites >= D3D_MAX_QUADS)
		D3DSprite_DrawBatch ();

	// cache the sprite frame so that we can check for changes
	ent->currspriteframe = R_GetSpriteFrame (ent);

	// check for texture change
	if (cachedspritetexture != ent->currspriteframe->texture->d3d_Texture)
	{
		D3DSprite_DrawBatch ();
		D3DHLSL_SetTexture (0, ent->currspriteframe->texture->d3d_Texture);
		cachedspritetexture = ent->currspriteframe->texture->d3d_Texture;
	}

	d3d_DrawSprites[d3d_NumDrawSprites] = ent;
	d3d_NumDrawSprites++;
}


void D3DSprite_Begin (void)
{
	D3D_SetTextureAddress (0, D3DTADDRESS_CLAMP);
	D3D_SetTextureMipmap (0, d3d_TexFilter);

	D3DHLSL_SetAlpha (1.0f);
	D3DHLSL_SetPass (FX_PASS_SPRITE);

	// no texture cache
	cachedspritetexture = NULL;

	// no sprite cache
	d3d_NumDrawSprites = 0;
}


void D3DSprite_End (void)
{
	// it's assumed that there will always be something to draw at the end
	D3DSprite_DrawBatch ();
}



