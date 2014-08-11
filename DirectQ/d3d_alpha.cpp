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
// r_misc.c

#include "quakedef.h"
#include "d3d_model.h"
#include "d3d_quake.h"
#include "particles.h"


/*
============================================================================================================

		ALPHA SORTING

============================================================================================================
*/

#define MAX_ALPHA_ITEMS		65536

// list of alpha items
typedef struct d3d_alphalist_s
{
	int Type;
	float Dist;

	// added for brush surfaces so that they don't need to allocate a modelsurf (yuck)
	entity_t *SurfEntity;

	union
	{
		dlight_t *DLight;
		entity_t *Entity;
		particle_emitter_t *Particle;
		particle_effect_t *Effect;
		msurface_t *surf;
		void *data;
	};
} d3d_alphalist_t;


#define D3D_ALPHATYPE_PARTICLE		2
#define D3D_ALPHATYPE_WATERWARP		3
#define D3D_ALPHATYPE_SURFACE		4
#define D3D_ALPHATYPE_FENCE			5
#define D3D_ALPHATYPE_CORONA		6
#define D3D_ALPHATYPE_ALIAS			7
#define D3D_ALPHATYPE_BRUSH			8
#define D3D_ALPHATYPE_SPRITE		9
#define D3D_ALPHATYPE_IQM			10
#define D3D_ALPHATYPE_BRIGHTFIELD	11
#define D3D_ALPHATYPE_PARTEFFECT	12

d3d_alphalist_t **d3d_AlphaList = NULL;
int d3d_NumAlphaList = 0;

void D3DAlpha_NewMap (void)
{
	d3d_AlphaList = (d3d_alphalist_t **) RenderZone->Alloc (MAX_ALPHA_ITEMS * sizeof (d3d_alphalist_t *));
}


float D3DAlpha_GetDist (float *origin)
{
	// no need to sqrt these as all we're concerned about is relative distances
	// (if x < y then sqrt (x) is also < sqrt (y))
	return
	(
		(origin[0] - r_refdef.vieworigin[0]) * (origin[0] - r_refdef.vieworigin[0]) +
		(origin[1] - r_refdef.vieworigin[1]) * (origin[1] - r_refdef.vieworigin[1]) +
		(origin[2] - r_refdef.vieworigin[2]) * (origin[2] - r_refdef.vieworigin[2])
	);
}


void D3DAlpha_AddToList (int type, void *data, float dist)
{
	if (d3d_NumAlphaList == MAX_ALPHA_ITEMS) return;
	if (!d3d_AlphaList[d3d_NumAlphaList]) d3d_AlphaList[d3d_NumAlphaList] = (d3d_alphalist_t *) RenderZone->Alloc (sizeof (d3d_alphalist_t));

	d3d_AlphaList[d3d_NumAlphaList]->Type = type;
	d3d_AlphaList[d3d_NumAlphaList]->data = data;
	d3d_AlphaList[d3d_NumAlphaList]->Dist = dist;

	d3d_NumAlphaList++;
}


void D3DAlpha_AddToList (entity_t *ent)
{
	if (ent->model->type == mod_alias)
		D3DAlpha_AddToList (D3D_ALPHATYPE_ALIAS, ent, D3DAlpha_GetDist (ent->origin));
	else if (ent->model->type == mod_iqm)
		D3DAlpha_AddToList (D3D_ALPHATYPE_IQM, ent, D3DAlpha_GetDist (ent->origin));
	else if (ent->model->type == mod_brush)
		D3DAlpha_AddToList (D3D_ALPHATYPE_BRUSH, ent, D3DAlpha_GetDist (ent->origin));
	else if (ent->model->type == mod_sprite)
		D3DAlpha_AddToList (D3D_ALPHATYPE_SPRITE, ent, D3DAlpha_GetDist (ent->origin));
	else;
}


void D3DAlpha_AddToList (msurface_t *surf, entity_t *ent, float *midpoint)
{
	// we only support turb surfaces for now
	if (surf->flags & SURF_DRAWTURB)
		D3DAlpha_AddToList (D3D_ALPHATYPE_WATERWARP, surf, D3DAlpha_GetDist (midpoint));
	else if (surf->flags & SURF_DRAWFENCE)
		D3DAlpha_AddToList (D3D_ALPHATYPE_FENCE, surf, D3DAlpha_GetDist (midpoint));
	else D3DAlpha_AddToList (D3D_ALPHATYPE_SURFACE, surf, D3DAlpha_GetDist (midpoint));

	// eeewwww
	d3d_AlphaList[d3d_NumAlphaList - 1]->SurfEntity = ent;
}


void D3DAlpha_AddToList (particle_emitter_t *particle)
{
	D3DAlpha_AddToList (D3D_ALPHATYPE_PARTICLE, particle, D3DAlpha_GetDist (particle->spawnorg));
}


void D3DAlpha_AddToList (particle_effect_t *particle)
{
	D3DAlpha_AddToList (D3D_ALPHATYPE_PARTEFFECT, particle, D3DAlpha_GetDist (particle->origin));
}


void D3DAlpha_AddToList (dlight_t *dl)
{
	D3DAlpha_AddToList (D3D_ALPHATYPE_CORONA, dl, D3DAlpha_GetDist (dl->origin));
}


void D3DAlpha_AddToList (entity_t *ent, int effects)
{
	if (effects & EF_BRIGHTFIELD) D3DAlpha_AddToList (D3D_ALPHATYPE_BRIGHTFIELD, ent, D3DAlpha_GetDist (ent->origin));
}


int D3DAlpha_SortFunc (const void *a, const void *b)
{
	d3d_alphalist_t *al1 = *(d3d_alphalist_t **) a;
	d3d_alphalist_t *al2 = *(d3d_alphalist_t **) b;

	// back to front ordering
	// this is more correct as it will order surfs properly if less than 1 unit separated
	if (al2->Dist > al1->Dist)
		return 1;
	else if (al2->Dist < al1->Dist)
		return -1;
	else return 0;
}


void D3DAlias_SetupAliasModel (entity_t *e);
void D3DAlias_DrawAliasBatch (entity_t **ents, int numents);
void D3D_SetupSpriteModel (entity_t *ent);
void R_AddParticleEmitterToRender (particle_emitter_t *pe);

void D3DWarp_SetupTurbState (void);
void D3DWarp_TakeDownTurbState (void);
void D3DWarp_DrawSurface (msurface_t *surf, entity_t *ent);

void D3DPart_BeginParticles (void);
void D3DPart_EndParticles (void);

void D3DSprite_Begin (void);
void D3DSprite_End (void);

void D3DLight_BeginCoronas (void);
void D3DLight_EndCoronas (void);
void D3DLight_DrawCorona (dlight_t *dl);
texture_t *R_TextureAnimation (entity_t *ent, texture_t *base);

void D3DIQM_SetCommonState (void);
void D3DIQM_DrawIQM (entity_t *ent);

void D3DPart_BeginBrightField (void);
void D3DPart_EndBrightField (void);
void D3DPart_DrawBrightField (float *origin);

void D3DPart_BeginEffect (void);
void D3DPart_EndEffect (void);
void D3DPart_DrawEffect (particle_effect_t *effect);


void D3DAlpha_Cull (void)
{
	D3D_BackfaceCull (D3DCULL_CCW);
}


void D3DAlpha_NoCull (void)
{
	D3D_BackfaceCull (D3DCULL_NONE);
}


void D3DAlpha_StageChange (d3d_alphalist_t *oldone, d3d_alphalist_t *newone)
{
	// fixme - these should be callback functions
	if (oldone)
	{
		switch (oldone->Type)
		{
		case D3D_ALPHATYPE_PARTEFFECT:
			D3DPart_EndEffect ();
			break;

		case D3D_ALPHATYPE_BRIGHTFIELD:
			D3DPart_EndBrightField ();
			break;

		case D3D_ALPHATYPE_PARTICLE:
			D3DPart_EndParticles ();
			break;

		case D3D_ALPHATYPE_SPRITE:
			D3DSprite_End ();
		case D3D_ALPHATYPE_ALIAS:
		case D3D_ALPHATYPE_IQM:
		case D3D_ALPHATYPE_BRUSH:
			D3DState_SetZBuffer (D3DZB_TRUE, FALSE);
			break;

		case D3D_ALPHATYPE_SURFACE:
			D3DBrush_End ();
			break;

		case D3D_ALPHATYPE_FENCE:
			D3DBrush_End ();
			D3DState_SetAlphaTest (FALSE);
			break;

		case D3D_ALPHATYPE_WATERWARP:
			D3DWarp_TakeDownTurbState ();
			D3DAlpha_Cull ();
			D3DState_SetZBuffer (D3DZB_TRUE, FALSE);
			break;

		case D3D_ALPHATYPE_CORONA:
			D3DLight_EndCoronas ();
			break;

		default:
			break;
		}
	}

	if (newone)
	{
		switch (newone->Type)
		{
		case D3D_ALPHATYPE_CORONA:
			D3DLight_BeginCoronas ();
			break;

		case D3D_ALPHATYPE_IQM:
			D3DIQM_SetCommonState ();
			D3DState_SetZBuffer (D3DZB_TRUE, TRUE);
			break;

		case D3D_ALPHATYPE_SPRITE:
			D3DSprite_Begin ();
		case D3D_ALPHATYPE_ALIAS:
		case D3D_ALPHATYPE_BRUSH:
			D3DState_SetZBuffer (D3DZB_TRUE, TRUE);
			break;

		case D3D_ALPHATYPE_FENCE:
			// hack - solid edges
			D3DState_SetAlphaTest (TRUE, D3DCMP_GREATEREQUAL, (DWORD) 0x000000aa);
			D3DBrush_Begin ();
			break;

		case D3D_ALPHATYPE_WATERWARP:
			D3DWarp_SetupTurbState ();
			D3DAlpha_NoCull ();
			D3DState_SetZBuffer (D3DZB_TRUE, TRUE);
			break;

		case D3D_ALPHATYPE_PARTICLE:
			D3DPart_BeginParticles ();
			break;

		case D3D_ALPHATYPE_SURFACE:
			D3DBrush_Begin ();
			break;

		case D3D_ALPHATYPE_BRIGHTFIELD:
			D3DPart_BeginBrightField ();
			break;

		case D3D_ALPHATYPE_PARTEFFECT:
			D3DPart_BeginEffect ();
			break;

		default:
			break;
		}
	}
}


void D3DAlpha_Setup (void)
{
	// enable blending
	D3DState_SetAlphaBlend (TRUE);
	D3DState_SetZBuffer (D3DZB_TRUE, FALSE);
}


void D3DAlpha_Takedown (void)
{
	// disable blending (done)
	// the cull type may have been modified going through here so put it back the way it was
	D3D_BackfaceCull (D3DCULL_CCW);
	D3DState_SetZBuffer (D3DZB_TRUE, TRUE);
	D3DState_SetAlphaBlend (FALSE);
	D3DState_SetAlphaTest (FALSE);
}


void D3DAlpha_RenderList (void)
{
	// nothing to add
	if (!d3d_NumAlphaList) return;

	// sort the alpha list
	if (d3d_NumAlphaList == 1)
		; // no need to sort
	else if (d3d_NumAlphaList == 2)
	{
		// exchange if necessary
		if (d3d_AlphaList[1]->Dist > d3d_AlphaList[0]->Dist)
		{
			// exchange - this was [1] and [2] - how come it never crashed?
			d3d_alphalist_t *Temp = d3d_AlphaList[0];
			d3d_AlphaList[0] = d3d_AlphaList[1];
			d3d_AlphaList[1] = Temp;
		}
	}
	else
	{
		// sort fully
		qsort (d3d_AlphaList, d3d_NumAlphaList, sizeof (d3d_alphalist_t *), D3DAlpha_SortFunc);
	}

	d3d_alphalist_t *previous = NULL;
	D3DAlpha_Setup ();

	// now add all the items in it to the alpha buffer
	for (int i = 0; i < d3d_NumAlphaList; i++)
	{
		// check for state change
		if (previous)
		{
			// fixme - these should be callbacks...
			if (d3d_AlphaList[i]->Type != previous->Type)
				D3DAlpha_StageChange (previous, d3d_AlphaList[i]);
		}
		else D3DAlpha_StageChange (NULL, d3d_AlphaList[i]);

		previous = d3d_AlphaList[i];

		switch (d3d_AlphaList[i]->Type)
		{
		case D3D_ALPHATYPE_PARTEFFECT:
			D3DPart_DrawEffect (d3d_AlphaList[i]->Effect);
			break;

		case D3D_ALPHATYPE_BRIGHTFIELD:
			D3DPart_DrawBrightField (d3d_AlphaList[i]->Entity->origin);
			break;

		case D3D_ALPHATYPE_ALIAS:
			D3DAlias_DrawAliasBatch (&d3d_AlphaList[i]->Entity, 1);
			break;

		case D3D_ALPHATYPE_IQM:
			D3DIQM_DrawIQM (d3d_AlphaList[i]->Entity);
			break;

		case D3D_ALPHATYPE_BRUSH:
			// not implemented - bmodels are added and sorted per-surface
			break;

		case D3D_ALPHATYPE_SPRITE:
			D3D_SetupSpriteModel (d3d_AlphaList[i]->Entity);
			break;

		case D3D_ALPHATYPE_PARTICLE:
			R_AddParticleEmitterToRender (d3d_AlphaList[i]->Particle);
			break;

		case D3D_ALPHATYPE_WATERWARP:
			D3DWarp_DrawSurface (d3d_AlphaList[i]->surf, d3d_AlphaList[i]->SurfEntity);
			break;

		case D3D_ALPHATYPE_SURFACE:
		case D3D_ALPHATYPE_FENCE:
			{
				msurface_t *surf = d3d_AlphaList[i]->surf;
				entity_t *ent = d3d_AlphaList[i]->SurfEntity;
				texture_t *tex = R_TextureAnimation (ent, surf->texinfo->texture);

				D3DBrush_EmitSurface (surf, tex, ent, ent->alphaval);
			}
			break;

		case D3D_ALPHATYPE_CORONA:
			D3DLight_DrawCorona (d3d_AlphaList[i]->DLight);
			break;

		default:
			// nothing to add
			break;
		}
	}

	// take down the final state used (in case it was a HLSL state)
	D3DAlpha_StageChange (previous, NULL);

	D3DAlpha_Takedown ();

	// reset alpha list
	// Con_Printf ("%i items in alpha list\n", d3d_NumAlphaList);
	d3d_NumAlphaList = 0;
}


