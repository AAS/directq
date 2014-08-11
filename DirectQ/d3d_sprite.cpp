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

// made this a global because otherwise multiple particle types don't get batched at all!!!
LPDIRECT3DTEXTURE9 cachedspritetexture = NULL;

entity_t *d3d_DrawSprites[D3D_MAX_QUADS];

extern int d3d_NumDrawSprites;

/*
=============================================================

  SPRITE MODELS

=============================================================
*/

/*
=================
Mod_LoadSpriteFrame
=================
*/
void *Mod_LoadSpriteFrame (model_t *mod, msprite_t *thespr, void *pin, mspriteframe_t **ppframe, int framenum)
{
	dspriteframe_t		*pinframe;
	mspriteframe_t		*pspriteframe;
	int					width, height, size, origin[2];
	char				name[64];

	pinframe = (dspriteframe_t *) pin;

	width = pinframe->width;
	height = pinframe->height;

	size = width * height * (thespr->version == SPR32_VERSION ? 4 : 1);

	pspriteframe = (mspriteframe_t *) MainCache->Alloc (sizeof (mspriteframe_t));

	memset (pspriteframe, 0, sizeof (mspriteframe_t));

	*ppframe = pspriteframe;

	pspriteframe->width = width;
	pspriteframe->height = height;
	origin[0] = pinframe->origin[0];
	origin[1] = pinframe->origin[1];

	pspriteframe->up = origin[1];
	pspriteframe->down = origin[1] - height;
	pspriteframe->left = origin[0];
	pspriteframe->right = width + origin[0];

	_snprintf (name, 64, "%s_%i", mod->name, framenum);

	if (thespr->version == SPR32_VERSION)
	{
		// swap to bgra
		byte *data = (byte *) (pinframe + 1);

		for (int i = 0; i < width * height; i++, data += 4)
		{
			byte tmp = data[0];
			data[0] = data[2];
			data[2] = tmp;
		}
	}

	// default paths are good for these
	pspriteframe->texture = D3DTexture_Load
	(
		name,
		width,
		height,
		(byte *) (pinframe + 1),
		IMAGE_MIPMAP | IMAGE_ALPHA | (thespr->version == SPR32_VERSION ? (IMAGE_SPRITE | IMAGE_32BIT) : IMAGE_SPRITE)
	);

	pspriteframe->s = 1;
	pspriteframe->t = 1;

	return (void *) ((byte *) pinframe + sizeof (dspriteframe_t) + size);
}


/*
=================
Mod_LoadSpriteGroup
=================
*/
void *Mod_LoadSpriteGroup (model_t *mod, msprite_t *thespr, void *pin, mspriteframe_t **ppframe, int framenum)
{
	dspritegroup_t		*pingroup;
	mspritegroup_t		*pspritegroup;
	int					i, numframes;
	dspriteinterval_t	*pin_intervals;
	void				*ptemp;

	pingroup = (dspritegroup_t *) pin;
	numframes = pingroup->numframes;

	pspritegroup = (mspritegroup_t *) MainCache->Alloc (sizeof (mspritegroup_t) + (numframes - 1) * sizeof (pspritegroup->frames[0]));

	pspritegroup->numframes = numframes;
	*ppframe = (mspriteframe_t *) pspritegroup;
	pin_intervals = (dspriteinterval_t *) (pingroup + 1);
	pspritegroup->intervals = (float *) MainCache->Alloc (numframes * sizeof (float));

	for (i = 0; i < numframes; i++)
	{
		pspritegroup->intervals[i] = pin_intervals->interval;

		if (pspritegroup->intervals[i] <= 0.0)
			Host_Error ("Mod_LoadSpriteGroup: interval<=0");

		pin_intervals++;
	}

	ptemp = (void *) pin_intervals;

	for (i = 0; i < numframes; i++)
		ptemp = Mod_LoadSpriteFrame (mod, thespr, ptemp, &pspritegroup->frames[i], framenum * 100 + i);

	return ptemp;
}


/*
=================
Mod_LoadSpriteModel
=================
*/
void Mod_LoadSpriteModel (model_t *mod, void *buffer)
{
	int					i;
	int					version;
	dsprite_t			*pin;
	msprite_t			*psprite;
	int					numframes;
	int					size;
	dspriteframetype_t	*pframetype;

	pin = (dsprite_t *) buffer;

	version = pin->version;

	if (version != SPRITE_VERSION && version != SPR32_VERSION)
		Host_Error ("%s has wrong version number (%i should be %i or %i)", mod->name, version, SPRITE_VERSION, SPR32_VERSION);

	numframes = pin->numframes;

	size = sizeof (msprite_t) +	(numframes - 1) * sizeof (psprite->frames);

	psprite = (msprite_t *) MainCache->Alloc (size);

	mod->spritehdr = psprite;

	psprite->type = pin->type;
	psprite->version = version;
	psprite->maxwidth = pin->width;
	psprite->maxheight = pin->height;
	psprite->beamlength = pin->beamlength;
	mod->synctype = (synctype_t) pin->synctype;
	psprite->numframes = numframes;

	mod->mins[0] = mod->mins[1] = -psprite->maxwidth / 2;
	mod->maxs[0] = mod->maxs[1] = psprite->maxwidth / 2;
	mod->mins[2] = -psprite->maxheight / 2;
	mod->maxs[2] = psprite->maxheight / 2;

	// load the frames
	if (numframes < 1)
		Host_Error ("Mod_LoadSpriteModel: Invalid # of frames: %d\n", numframes);

	mod->numframes = numframes;

	pframetype = (dspriteframetype_t *) (pin + 1);

	for (i = 0; i < numframes; i++)
	{
		spriteframetype_t	frametype;

		frametype = (spriteframetype_t) pframetype->type;
		psprite->frames[i].type = frametype;

		if (frametype == SPR_SINGLE)
			pframetype = (dspriteframetype_t *) Mod_LoadSpriteFrame (mod, psprite, pframetype + 1, &psprite->frames[i].frameptr, i);
		else pframetype = (dspriteframetype_t *) Mod_LoadSpriteGroup (mod, psprite, pframetype + 1, &psprite->frames[i].frameptr, i);
	}

	mod->type = mod_sprite;

	// it's always cheaper to just draw sprites
	mod->flags |= EF_NOOCCLUDE;

	// copy it out to the cache
	MainCache->Alloc (mod->name, mod, sizeof (model_t));
}

//=============================================================================

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
			// vec3_t	point;
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
				angle = ent->angles[2] * (D3DX_PI / 180.0);
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

			verts[0].xyz[0] = fixed_origin[0] + (up[0] * frame->up) + (right[0] * frame->left);
			verts[0].xyz[1] = fixed_origin[1] + (up[1] * frame->up) + (right[1] * frame->left);
			verts[0].xyz[2] = fixed_origin[2] + (up[2] * frame->up) + (right[2] * frame->left);
			verts[0].color = color;
			verts[0].st[0] = 0;
			verts[0].st[1] = 0;

			verts[1].xyz[0] = fixed_origin[0] + (up[0] * frame->up) + (right[0] * frame->right);
			verts[1].xyz[1] = fixed_origin[1] + (up[1] * frame->up) + (right[1] * frame->right);
			verts[1].xyz[2] = fixed_origin[2] + (up[2] * frame->up) + (right[2] * frame->right);
			verts[1].color = color;
			verts[1].st[0] = frame->s;
			verts[1].st[1] = 0;

			verts[2].xyz[0] = fixed_origin[0] + (up[0] * frame->down) + (right[0] * frame->right);
			verts[2].xyz[1] = fixed_origin[1] + (up[1] * frame->down) + (right[1] * frame->right);
			verts[2].xyz[2] = fixed_origin[2] + (up[2] * frame->down) + (right[2] * frame->right);
			verts[2].color = color;
			verts[2].st[0] = frame->s;
			verts[2].st[1] = frame->t;

			verts[3].xyz[0] = fixed_origin[0] + (up[0] * frame->down) + (right[0] * frame->left);
			verts[3].xyz[1] = fixed_origin[1] + (up[1] * frame->down) + (right[1] * frame->left);
			verts[3].xyz[2] = fixed_origin[2] + (up[2] * frame->down) + (right[2] * frame->left);
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
	if (cachedspritetexture != ent->currspriteframe->texture)
	{
		D3DSprite_DrawBatch ();
		D3DHLSL_SetTexture (0, ent->currspriteframe->texture);
		cachedspritetexture = ent->currspriteframe->texture;
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



