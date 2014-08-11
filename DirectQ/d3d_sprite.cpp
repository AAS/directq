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
#include "d3d_vbo.h"

/*
=============================================================

  SPRITE MODELS

=============================================================
*/

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


void D3DSprite_SetState (void *data)
{
	d3d_texturechange_t *tc = (d3d_texturechange_t *) data;

	D3D_SetVertexDeclaration (d3d_VDXyzDiffuseTex1);
	D3D_SetTextureAddressMode (D3DTADDRESS_CLAMP);

	if (d3d_GlobalCaps.usingPixelShaders)
	{
		if (d3d_FXPass == FX_PASS_NOTBEGUN)
		{
			D3D_SetTextureMipmap (1, d3d_TexFilter, d3d_MipFilter);
			d3d_MasterFX->SetTexture ("tmu0Texture", tc->tex);
			D3D_BeginShaderPass (FX_PASS_PARTICLES);
		}
		else if (d3d_FXPass == FX_PASS_PARTICLES)
		{
			D3D_SetTextureMipmap (1, d3d_TexFilter, d3d_MipFilter);
			d3d_MasterFX->SetTexture ("tmu0Texture", tc->tex);
			d3d_FXCommitPending = true;
		}
		else
		{
			D3D_EndShaderPass ();
			D3D_SetTextureMipmap (1, d3d_TexFilter, d3d_MipFilter);
			d3d_MasterFX->SetTexture ("tmu0Texture", tc->tex);
			D3D_BeginShaderPass (FX_PASS_PARTICLES);
		}
	}
	else
	{
		D3D_SetTextureMipmap (0, d3d_TexFilter, d3d_MipFilter);
		D3D_SetTexCoordIndexes (0);

		D3D_SetTextureColorMode (0, D3DTOP_SELECTARG1, D3DTA_TEXTURE, D3DTA_DIFFUSE);
		D3D_SetTextureAlphaMode (0, D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE);

		D3D_SetTextureColorMode (1, D3DTOP_DISABLE);
		D3D_SetTextureAlphaMode (1, D3DTOP_DISABLE);

		D3D_SetTexture (tc->stage, tc->tex);
	}
}


void D3D_SetupSpriteModel (entity_t *ent)
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
	frame = R_GetSpriteFrame (ent);
	psprite = ent->model->spritehdr;

	VectorCopy (ent->origin, fixed_origin);

	switch (psprite->type)
	{
	case SPR_ORIENTED:
		// bullet marks on walls
		AngleVectors (ent->angles, v_forward, v_right, v_up);

		VectorScale (v_forward, -2, temp);
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
		VectorSubtract (ent->origin, r_origin, v_forward);
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
		angle = ent->angles[ROLL] * (D3DX_PI / 180.0);
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
		Con_DPrintf ("D3D_SetupSpriteModel - Unknown Sprite Type %i\n", psprite->type);
		break;
	}

	d3d_texturechange_t tc = {0, frame->texture->d3d_Texture};
	VBO_AddCallback (D3DSprite_SetState, &tc, sizeof (d3d_texturechange_t));
	VBO_AddSprite (frame, fixed_origin, up, right, ent->alphaval);
}


