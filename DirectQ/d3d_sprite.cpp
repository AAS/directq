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

/*
=============================================================

  SPRITE MODELS

=============================================================
*/

typedef struct sprverts_s
{
	float x, y, z;
	float s, t;
} sprverts_t;

sprverts_t sprverts[4];

/*
================
R_GetSpriteFrame
================
*/
mspriteframe_t *R_GetSpriteFrame (entity_t *currententity)
{
	msprite_t		*psprite;
	mspritegroup_t	*pspritegroup;
	mspriteframe_t	*pspriteframe;
	int				i, numframes, frame;
	float			*pintervals, fullinterval, targettime, time;

	psprite = currententity->model->sh;
	frame = currententity->frame;

	if ((frame >= psprite->numframes) || (frame < 0))
	{
		Con_Printf ("R_DrawSprite: no such frame %d\n", frame);
		frame = 0;
	}

	if (psprite->frames[frame].type == SPR_SINGLE)
	{
		pspriteframe = psprite->frames[frame].frameptr;
	}
	else
	{
		pspritegroup = (mspritegroup_t *)psprite->frames[frame].frameptr;
		pintervals = pspritegroup->intervals;
		numframes = pspritegroup->numframes;
		fullinterval = pintervals[numframes-1];

		time = cl.time + currententity->syncbase;

		// when loading in Mod_LoadSpriteGroup, we guaranteed all interval values
		// are positive, so we don't have to worry about division by 0
		targettime = time - ((int)(time / fullinterval)) * fullinterval;

		for (i=0 ; i<(numframes-1) ; i++)
		{
			if (pintervals[i] > targettime)
				break;
		}

		pspriteframe = pspritegroup->frames[i];
	}

	return pspriteframe;
}


/*
=================
R_DrawSpriteModel

=================
*/
#define VectorScalarMult(a,b,c) {(c)[0] = (a)[0] * (b); (c)[1] = (a)[1] * (b); (c)[2] = (a)[2] * (b);}

void R_DrawSpriteModel (entity_t *e)
{
	vec3_t	point;
	mspriteframe_t	*frame;
	float		*up, *right;
	vec3_t		v_forward, v_right, v_up;
	msprite_t	*psprite;
	vec3_t		fixed_origin;
	vec3_t		temp;
	vec3_t		tvec;
	float		angle, sr, cr;

	// don't even bother culling, because it's just a single
	// polygon without a surface cache
	frame = R_GetSpriteFrame (e);
	psprite = currententity->model->sh;

	VectorCopy (e->origin, fixed_origin);

	switch (psprite->type)
	{
	case SPR_ORIENTED:
		// bullet marks on walls
		AngleVectors (currententity->angles, v_forward, v_right, v_up);

		VectorScalarMult (v_forward, -2, temp);
		VectorAdd (temp, fixed_origin, fixed_origin);

		up = v_up;
		right = v_right;
		break;

	case SPR_VP_PARALLEL_UPRIGHT:
		v_up[0] = 0;
		v_up[1] = 0;
		v_up[2] = 1;

		up = v_up;
		right = vright;
		break;

	case SPR_FACING_UPRIGHT:
		VectorSubtract (currententity->origin, r_origin, v_forward);
		v_forward[2] = 0;
		VectorNormalize (v_forward);

		v_right[0] = v_forward[1];
		v_right[1] = -v_forward[0];
		v_right[2] = 0;

		v_up[0] = 0;
		v_up[1] = 0;
		v_up[2] = 1;

		up = v_up;
		right = v_right;
		break;

	case SPR_VP_PARALLEL:
		// normal sprite
		up = vup;
		right = vright;
		break;

	case SPR_VP_PARALLEL_ORIENTED:
		angle = currententity->angles[ROLL] * (M_PI / 180.0);
		sr = sin (angle);
		cr = cos (angle);

		v_right[0] = vright[0] * cr + vup[0] * sr;
		v_right[1] = vright[1] * cr + vup[1] * sr;
		v_right[2] = vright[2] * cr + vup[2] * sr;

		v_up[0] = vright[0] * -sr + vup[0] * cr;
		v_up[1] = vright[1] * -sr + vup[1] * cr;
		v_up[2] = vright[2] * -sr + vup[2] * cr;

		up = v_up;
		right = v_right;
		break;

	default:
		// unknown type - just assume it's normal and Con_DPrintf it
		up = vup;
		right = vright;
		Con_DPrintf ("R_DrawSpriteModel - Unknown Sprite Type %i\n", psprite->type);
		break;
	}

	d3d_SpriteFX.SetTexture ((LPDIRECT3DTEXTURE9) frame->texture);

	VectorMA (fixed_origin, frame->down, up, point);
	VectorMA (point, frame->left, right, point);
	sprverts[0].x = point[0]; sprverts[0].y = point[1]; sprverts[0].z = point[2]; sprverts[0].s = 0; sprverts[0].t = 1;

	VectorMA (fixed_origin, frame->up, up, point);
	VectorMA (point, frame->left, right, point);
	sprverts[1].x = point[0]; sprverts[1].y = point[1]; sprverts[1].z = point[2]; sprverts[1].s = 0; sprverts[1].t = 0;

	VectorMA (fixed_origin, frame->up, up, point);
	VectorMA (point, frame->right, right, point);
	sprverts[2].x = point[0]; sprverts[2].y = point[1]; sprverts[2].z = point[2]; sprverts[2].s = 1; sprverts[2].t = 0;

	VectorMA (fixed_origin, frame->down, up, point);
	VectorMA (point, frame->right, right, point);
	sprverts[3].x = point[0]; sprverts[3].y = point[1]; sprverts[3].z = point[2]; sprverts[3].s = 1; sprverts[3].t = 1;

	d3d_SpriteFX.Draw (D3DPT_TRIANGLEFAN, 2, sprverts, sizeof (sprverts_t));
}


void D3D_DrawSpriteModels (void)
{
	if (!r_drawentities.value) return;
	if (!(r_renderflags & R_RENDERSPRITE)) return;

	d3d_Device->SetVertexDeclaration (d3d_V3ST2Declaration);

	d3d_SpriteFX.BeginRender ();
	d3d_SpriteFX.SetWPMatrix (&(d3d_WorldMatrix * d3d_PerspectiveMatrix));
	d3d_SpriteFX.SwitchToPass (0);

	d3d_EnableAlphaBlend->Apply ();
	d3d_Device->SetRenderState (D3DRS_ZWRITEENABLE, FALSE);

	for (int i = 0; i < cl_numvisedicts; i++)
	{
		currententity = cl_visedicts[i];

		// not a sprite model
		if (currententity->model->type != mod_sprite) continue;

		// draw it
		R_DrawSpriteModel (currententity);
	}

	d3d_SpriteFX.EndRender ();

	d3d_Device->SetRenderState (D3DRS_ZWRITEENABLE, TRUE);
	d3d_DisableAlphaBlend->Apply ();
}

