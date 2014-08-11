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
// we're already using 9 of these so we'll limit batch sizes to 100 and save the rest for headroom
#define MAX_PARTICLE_BATCH	120

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
		hr = d3d_PartVBO->Lock (0, 0, (void **) &verts, 0);
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
		if (FAILED (hr)) Sys_Error ("D3DPart_OnRecover : failed to unlock vertex buffer");
	}

	if (!d3d_PartIBO)
	{
		D3DMain_CreateIndexBuffer (MAX_PARTICLE_BATCH * 6, D3DUSAGE_WRITEONLY, &d3d_PartIBO);

		// now we fill in the index buffer; this is a non-dynamic index buffer and it only needs to be set once
		unsigned short *ndx = NULL;
		int NumQuadVerts = MAX_PARTICLE_BATCH * 4;

		hr = d3d_PartIBO->Lock (0, 0, (void **) &ndx, 0);
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
		if (FAILED (hr)) Sys_Error ("D3DPart_OnRecover: failed to unlock index buffer");
	}
}

CD3DDeviceLossHandler d3d_PartHandler (D3DPart_OnLoss, D3DPart_OnRecover);

cvar_t r_particlesize ("r_particlesize", "1", CVAR_ARCHIVE);
cvar_t r_drawparticles ("r_drawparticles", "1", CVAR_ARCHIVE);
cvar_t r_particlestyle ("r_particlestyle", "0", CVAR_ARCHIVE);
cvar_t r_particledistscale ("r_particledistscale", "0.004", CVAR_ARCHIVE);

float d3d_PartScale;

// made this a global because otherwise multiple particle types don't get batched at all!!!
LPDIRECT3DTEXTURE9 cachedspritetexture = NULL;

// sprite batching
#define MAX_DRAWSPRITES	1024

entity_t *d3d_DrawSprites[MAX_DRAWSPRITES];
int d3d_NumDrawSprites = 0;


void D3DPart_BeginParticles (void)
{
	D3DHLSL_SetTexture (0, NULL);

	// toggle square particles
	if (r_particlestyle.integer)
		D3DHLSL_SetPass (FX_PASS_PARTICLE_SQUARE);
	else D3DHLSL_SetPass (FX_PASS_PARTICLES);

	D3D_SetStreamSource (0, d3d_PartVBO, 0, sizeof (d3d_partinstvert_t));
	D3D_SetStreamSource (1, NULL, 0, 0);
	D3D_SetStreamSource (2, NULL, 0, 0);

	D3D_SetIndices (d3d_PartIBO);

	D3D_SetVertexDeclaration (d3d_PartInstDecl);

	// distance scaling is a function of the current display mode and a user-selectable value
	// if the resolution is smaller distant particles will map to sub-pixel sizes so we try to prevent that
	if (r_particledistscale.value < 0) Cvar_Set (&r_particledistscale, 0.0f);
	if (r_particledistscale.value > 0.02f) Cvar_Set (&r_particledistscale, 0.02f);

	D3DHLSL_SetAlpha (1.0f);

	if (d3d_CurrentMode.Height < d3d_CurrentMode.Width)
		D3DHLSL_SetFloat ("genericscale", (r_particledistscale.value * 480.0f) / (float) d3d_CurrentMode.Height);
	else D3DHLSL_SetFloat ("genericscale", (r_particledistscale.value * 640.0f) / (float) d3d_CurrentMode.Width);

	// keep sizes consistent
	d3d_PartScale = 0.3f * r_particlesize.value;

	// no sprite cache
	d3d_NumDrawSprites = 0;
}


void D3DPart_EndParticles (void)
{
	if (d3d_NumDrawSprites)
	{
		// reset the arrays
		D3DXVECTOR4 *positions = (D3DXVECTOR4 *) scratchbuf;
		D3DXVECTOR4 *colours = (positions + MAX_PARTICLE_BATCH);

		D3DHLSL_SetVectorArray ("PartInstancePosition", positions, d3d_NumDrawSprites);
		D3DHLSL_SetVectorArray ("PartInstanceColor", colours, d3d_NumDrawSprites);
		D3D_DrawIndexedPrimitive (0, d3d_NumDrawSprites * 4, 0, d3d_NumDrawSprites * 2);

		d3d_NumDrawSprites = 0;
	}
}


void R_AddParticleTypeToRender (particle_type_t *pt)
{
	if (!pt->particles) return;
	if (!pt->numactiveparticles) return;
	if (!r_drawparticles.value) return;
	if (r_particlesize.value < 0.001f) return;

	// we don't have a geometry shader but we do have shader instancing ;)
	// (this will even work on ps 2.0 hardware and is so much better than point sprites)
	D3DXVECTOR4 *positions = (D3DXVECTOR4 *) scratchbuf;
	D3DXVECTOR4 *colours = (positions + MAX_PARTICLE_BATCH);

	// walk the list starting at the first active particle
	for (particle_t *p = pt->particles; p; p = p->next)
	{
		if (d3d_NumDrawSprites == MAX_PARTICLE_BATCH)
			D3DPart_EndParticles ();

		float *color = d3d_QuakePalette.colorfloat[(int) p->color & 255];

		positions[d3d_NumDrawSprites].x = p->org[0];
		positions[d3d_NumDrawSprites].y = p->org[1];
		positions[d3d_NumDrawSprites].z = p->org[2];
		positions[d3d_NumDrawSprites].w = p->scale * d3d_PartScale;

		colours[d3d_NumDrawSprites].x = color[0];
		colours[d3d_NumDrawSprites].y = color[1];
		colours[d3d_NumDrawSprites].z = color[2];
		colours[d3d_NumDrawSprites].w = p->alpha;

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
	// same!!!
	D3DPart_EndParticles ();
}


void D3DPart_DrawSingleCorona (float *origin, float *color, float radius)
{
	if (d3d_NumDrawSprites == MAX_PARTICLE_BATCH)
		D3DPart_EndParticles ();

	// we don't have a geometry shader but we do have shader instancing ;)
	// (this will even work on ps 2.0 hardware and is so much better than point sprites)
	D3DXVECTOR4 *positions = (D3DXVECTOR4 *) scratchbuf;
	D3DXVECTOR4 *colours = (positions + MAX_PARTICLE_BATCH);

	positions[d3d_NumDrawSprites].x = origin[0];
	positions[d3d_NumDrawSprites].y = origin[1];
	positions[d3d_NumDrawSprites].z = origin[2];
	positions[d3d_NumDrawSprites].w = radius;

	colours[d3d_NumDrawSprites].x = color[0];
	colours[d3d_NumDrawSprites].y = color[1];
	colours[d3d_NumDrawSprites].z = color[2];
	colours[d3d_NumDrawSprites].w = 255;

	d3d_NumDrawSprites++;
}


/*
=============================================================

  SPRITE MODELS

=============================================================
*/

typedef struct spritevert_s
{
	float xyz[3];
	DWORD color;
	float st[2];
} spritevert_t;


LPDIRECT3DVERTEXDECLARATION9 d3d_SpriteDecl = NULL;

void D3DSprite_OnLoss (void)
{
	SAFE_RELEASE (d3d_SpriteDecl);
}


void D3DSprite_OnRecover (void)
{
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
		if (FAILED (hr)) Sys_Error ("D3DSprite_OnRecover: d3d_Device->CreateVertexDeclaration failed");
	}
}

CD3DDeviceLossHandler d3d_SpriteHandler (D3DSprite_OnLoss, D3DSprite_OnRecover);


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
		D3DSprite_OnRecover ();
		D3D_UnbindStreams ();
		D3D_SetVertexDeclaration (d3d_SpriteDecl);

		spritevert_t *verts = (spritevert_t *) scratchbuf;
		unsigned short *ndx = (unsigned short *) (verts + d3d_NumDrawSprites * 4);

		for (int i = 0, v = 0; i < d3d_NumDrawSprites; i++, v += 4)
		{
			vec3_t	point;
			float		*up, *right;
			avectors_t	av;
			vec3_t		fixed_origin;
			vec3_t		temp;
			vec3_t		tvec;
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

			D3DCOLOR color = D3DCOLOR_ARGB (BYTE_CLAMP (ent->alphaval), 255, 255, 255);

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

			ndx[0] = v + 0;
			ndx[1] = v + 1;
			ndx[2] = v + 2;
			ndx[3] = v + 0;
			ndx[4] = v + 2;
			ndx[5] = v + 3;

			verts += 4;
			ndx += 6;
		}

		verts = (spritevert_t *) scratchbuf;
		ndx = (unsigned short *) (verts + d3d_NumDrawSprites * 4);

		D3DHLSL_CheckCommit ();

		d3d_Device->DrawIndexedPrimitiveUP (D3DPT_TRIANGLELIST, 0, d3d_NumDrawSprites * 4,
			d3d_NumDrawSprites * 2, ndx, D3DFMT_INDEX16, verts, sizeof (spritevert_t));

		d3d_RenderDef.numdrawprim++;
		d3d_NumDrawSprites = 0;
	}
}


void D3D_SetupSpriteModel (entity_t *ent)
{
	if (d3d_NumDrawSprites == MAX_DRAWSPRITES)
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


LPDIRECT3DTEXTURE9 smokepufftexture = NULL;


typedef struct r_smokepuff_s
{
	float origin[3];
	float size;
	float alpha;
	double time;

	struct r_smokepuff_s *next;
} r_smokepuff_t;

r_smokepuff_t *r_freesmokepuffs = NULL;
r_smokepuff_t *r_activesmokepuffs = NULL;

void D3DPart_SmokePuffNewMap (void)
{
	r_freesmokepuffs = NULL;
	r_activesmokepuffs = NULL;
}


void D3DPart_AddSmokePuff (float *origin, int numpuffs, int spread)
{
	for (int i = 0; i < numpuffs; i++)
	{
		if (!r_freesmokepuffs)
		{
			r_freesmokepuffs = (r_smokepuff_t *) RenderZone->Alloc (128 * sizeof (r_smokepuff_t));

			for (int j = 0; j < 127; j++)
			{
				r_freesmokepuffs[j].next = &r_freesmokepuffs[j + 1];
				r_freesmokepuffs[j + 1].next = NULL;
			}
		}

		r_smokepuff_t *sp = r_freesmokepuffs;
		r_freesmokepuffs = sp->next;

		sp->next = r_activesmokepuffs;
		r_activesmokepuffs = sp;

		sp->origin[0] = origin[0] + (rand () % spread) - (rand () % spread);
		sp->origin[1] = origin[1] + (rand () % spread) - (rand () % spread);
		sp->origin[2] = origin[2] + ((rand () % spread) - (rand () % spread)) / 2;

		sp->size = 1;
		sp->alpha = 255 - (rand () & 255);
		sp->time = cl.time;
	}
}


void D3DPart_DrawSmokePuffs (double frametime)
{
	if (!r_activesmokepuffs) return;

	for (;;)
	{
		r_smokepuff_t *kill = r_activesmokepuffs;

		if (kill && kill->alpha < 0)
		{
			r_activesmokepuffs = kill->next;
			kill->next = r_freesmokepuffs;
			r_freesmokepuffs = kill;
			continue;
		}

		break;
	}

	if (!r_activesmokepuffs) return;

	bool stateset = false;
	int numpuffs = 0;

	spritevert_t *verts = (spritevert_t *) scratchbuf;
	unsigned short *indexes = (unsigned short *) (verts + 4096);
	float up[3], right[3], p_up[3], p_right[3], p_upright[3];

	VectorScale (r_viewvectors.up, 1.5, up);
	VectorScale (r_viewvectors.right, 1.5, right);

	for (r_smokepuff_t *sp = r_activesmokepuffs; sp; sp = sp->next)
	{
		for (;;)
		{
			r_smokepuff_t *kill = sp->next;

			if (kill && kill->alpha < 0)
			{
				sp->next = kill->next;
				kill->next = r_freesmokepuffs;
				r_freesmokepuffs = kill;
				continue;
			}

			break;
		}

		if (!stateset)
		{
			D3DSprite_OnRecover ();

			D3D_SetTextureAddress (0, D3DTADDRESS_CLAMP);
			D3D_SetTextureMipmap (0, d3d_TexFilter);

			D3DHLSL_SetAlpha (1.0f);
			D3DHLSL_SetPass (FX_PASS_SPRITE);

			D3D_UnbindStreams ();
			D3D_SetVertexDeclaration (d3d_SpriteDecl);

			D3DHLSL_SetTexture (0, smokepufftexture);

			// enable blending
			D3D_SetRenderState (D3DRS_ALPHABLENDENABLE, TRUE);
			D3D_SetRenderState (D3DRS_BLENDOP, D3DBLENDOP_ADD);
			D3D_SetRenderState (D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
			D3D_SetRenderState (D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
			D3D_SetRenderState (D3DRS_ZWRITEENABLE, FALSE);
			D3D_BackfaceCull (D3DCULL_NONE);

			stateset = true;
		}

		// check for overflow
		if (numpuffs > 1024)
		{
			verts = (spritevert_t *) scratchbuf;
			indexes = (unsigned short *) (verts + 4096);

			D3DHLSL_CheckCommit ();

			d3d_Device->DrawIndexedPrimitiveUP (D3DPT_TRIANGLELIST, 0, numpuffs * 4,
				numpuffs * 2, indexes, D3DFMT_INDEX16, verts, sizeof (spritevert_t));

			d3d_RenderDef.numdrawprim++;

			numpuffs = 0;
		}

		// billboard and add to the list
		D3DCOLOR color = D3DCOLOR_ARGB (BYTE_CLAMP (sp->alpha), 255, 255, 255);

		verts[0].xyz[0] = sp->origin[0] - up[0] * sp->size - right[0] * sp->size;
		verts[0].xyz[1] = sp->origin[1] - up[1] * sp->size - right[1] * sp->size;
		verts[0].xyz[2] = sp->origin[2] - up[2] * sp->size - right[2] * sp->size;
		verts[0].color = color;
		verts[0].st[0] = 0;
		verts[0].st[1] = 0;

		verts[1].xyz[0] = sp->origin[0] - up[0] * sp->size + right[0] * sp->size;
		verts[1].xyz[1] = sp->origin[1] - up[1] * sp->size + right[1] * sp->size;
		verts[1].xyz[2] = sp->origin[2] - up[2] * sp->size + right[2] * sp->size;
		verts[1].color = color;
		verts[1].st[0] = 1;
		verts[1].st[1] = 0;

		verts[2].xyz[0] = sp->origin[0] + up[0] * sp->size + right[0] * sp->size;
		verts[2].xyz[1] = sp->origin[1] + up[1] * sp->size + right[1] * sp->size;
		verts[2].xyz[2] = sp->origin[2] + up[2] * sp->size + right[2] * sp->size;
		verts[2].color = color;
		verts[2].st[0] = 1;
		verts[2].st[1] = 1;

		verts[3].xyz[0] = sp->origin[0] + up[0] * sp->size - right[0] * sp->size;
		verts[3].xyz[1] = sp->origin[1] + up[1] * sp->size - right[1] * sp->size;
		verts[3].xyz[2] = sp->origin[2] + up[2] * sp->size - right[2] * sp->size;
		verts[3].color = color;
		verts[3].st[0] = 0;
		verts[3].st[1] = 1;

		indexes[0] = (numpuffs * 4) + 0;
		indexes[1] = (numpuffs * 4) + 1;
		indexes[2] = (numpuffs * 4) + 2;
		indexes[3] = (numpuffs * 4) + 0;
		indexes[4] = (numpuffs * 4) + 2;
		indexes[5] = (numpuffs * 4) + 3;

		// fade alpha
		sp->alpha -= frametime * 333.0f;
		sp->size += frametime * 16.666f;
		sp->origin[2] += frametime * 6.66f;

		numpuffs++;
		verts += 4;
		indexes += 6;
	}

	if (numpuffs)
	{
		verts = (spritevert_t *) scratchbuf;
		indexes = (unsigned short *) (verts + 4096);

		D3DHLSL_CheckCommit ();

		d3d_Device->DrawIndexedPrimitiveUP (D3DPT_TRIANGLELIST, 0, numpuffs * 4,
			numpuffs * 2, indexes, D3DFMT_INDEX16, verts, sizeof (spritevert_t));

		d3d_RenderDef.numdrawprim++;
	}

	// draw anything left over
	if (stateset)
	{
		D3D_BackfaceCull (D3DCULL_CCW);
		D3D_SetRenderState (D3DRS_ZWRITEENABLE, TRUE);
		D3D_SetRenderState (D3DRS_ALPHABLENDENABLE, FALSE);
		D3D_SetRenderState (D3DRS_ALPHATESTENABLE, FALSE);
	}
}

