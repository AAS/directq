
#include "quakedef.h"
#include "d3d_quake.h"

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

	psprite = (msprite_t *) currententity->model->cache.data;
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
void R_DrawSpriteModel (entity_t *e)
{
	vec3_t	point;
	mspriteframe_t	*frame;
	float		*up, *right;
	vec3_t		v_forward, v_right, v_up;
	msprite_t		*psprite;

	// don't even bother culling, because it's just a single
	// polygon without a surface cache
	frame = R_GetSpriteFrame (e);
	psprite = (msprite_t *) currententity->model->cache.data;

	if (psprite->type == SPR_ORIENTED)
	{	// bullet marks on walls
		AngleVectors (currententity->angles, v_forward, v_right, v_up);
		up = v_up;
		right = v_right;
	}
	else
	{	// normal sprite
		up = vup;
		right = vright;
	}

	D3D_BindTexture ((LPDIRECT3DTEXTURE9) frame->texture);

	VectorMA (e->origin, frame->down, up, point);
	VectorMA (point, frame->left, right, point);
	sprverts[0].x = point[0]; sprverts[0].y = point[1]; sprverts[0].z = point[2]; sprverts[0].s = 0; sprverts[0].t = 1;

	VectorMA (e->origin, frame->up, up, point);
	VectorMA (point, frame->left, right, point);
	sprverts[1].x = point[0]; sprverts[1].y = point[1]; sprverts[1].z = point[2]; sprverts[1].s = 0; sprverts[1].t = 0;

	VectorMA (e->origin, frame->up, up, point);
	VectorMA (point, frame->right, right, point);
	sprverts[2].x = point[0]; sprverts[2].y = point[1]; sprverts[2].z = point[2]; sprverts[2].s = 1; sprverts[2].t = 0;

	VectorMA (e->origin, frame->down, up, point);
	VectorMA (point, frame->right, right, point);
	sprverts[3].x = point[0]; sprverts[3].y = point[1]; sprverts[3].z = point[2]; sprverts[3].s = 1; sprverts[3].t = 1;

	d3d_Device->DrawPrimitiveUP (D3DPT_TRIANGLEFAN, 2, sprverts, sizeof (sprverts_t));
}


