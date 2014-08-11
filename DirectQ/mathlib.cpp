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
// mathlib.c -- math primitives

#include "versions.h"

#include <math.h>
#include "quakedef.h"
#include "d3d_model.h"

void Sys_Error (char *error, ...);

vec3_t vec3_origin = {0, 0, 0};
int nanmask = 255 << 23;


/*-----------------------------------------------------------------*/


float anglemod (float a)
{
	a = (360.0 / 65536) * ((int) (a * (65536 / 360.0)) & 65535);
	return a;
}


/*
==================
SphereOnPlaneSide

Faster but coarser than BoxOnPlaneSide
==================
*/
int SphereOnPlaneSide (float *center, float radius, mplane_t *p)
{
	if (p->type < 3)
	{
		// fast axial case
		if (p->dist <= center[p->type] - radius)
			return BOX_INSIDE_PLANE;
		else if (p->dist >= center[p->type] + radius)
			return BOX_OUTSIDE_PLANE;
		else return BOX_INTERSECT_PLANE;
	}
	else
	{
		float dist = DotProduct (center, p->normal) - p->dist;

		if (dist <= -radius)
			return BOX_OUTSIDE_PLANE;
		else if (dist >= radius)
			return BOX_INSIDE_PLANE;
		else return BOX_INTERSECT_PLANE;
	}
}


void AngleVectors (vec3_t angles, vec3_t forward, vec3_t right, vec3_t up)
{
	QMATRIX m;

	m.SetFromYawPitchRoll (-angles[0], -angles[2], -angles[1]);
	m.ToVectors (forward, up, right);
}


void AngleVectors (vec3_t angles, QMATRIX *m)
{
	m->SetFromYawPitchRoll (-angles[0], -angles[2], -angles[1]);
}


void AngleVectors (vec3_t angles, avectors_t *av)
{
	QMATRIX m;

	m.SetFromYawPitchRoll (-angles[0], -angles[2], -angles[1]);
	m.ToVectors (av->forward, av->up, av->right);
}


int VectorCompare (vec3_t v1, vec3_t v2)
{
	if (v1[0] != v2[0]) return 0;
	if (v1[1] != v2[1]) return 0;
	if (v1[2] != v2[2]) return 0;

	return 1;
}

void VectorMad (vec3_t add, float scale, vec3_t mul, vec3_t out)
{
	out[0] = add[0] + scale * mul[0];
	out[1] = add[1] + scale * mul[1];
	out[2] = add[2] + scale * mul[2];
}


void CrossProduct (vec3_t v1, vec3_t v2, vec3_t cross)
{
	cross[0] = v1[1] * v2[2] - v1[2] * v2[1];
	cross[1] = v1[2] * v2[0] - v1[0] * v2[2];
	cross[2] = v1[0] * v2[1] - v1[1] * v2[0];
}

float sqrt (float x);

vec_t Length (vec3_t v)
{
	int		i;
	float	length;

	length = 0;

	for (i = 0; i < 3; i++)
		length += v[i] * v[i];

	length = sqrt (length);		// FIXME

	return length;
}

float VectorNormalize (vec3_t v)
{
	float	length, ilength;

	length = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
	length = sqrt (length);		// FIXME

	if (length)
	{
		ilength = 1 / length;
		v[0] *= ilength;
		v[1] *= ilength;
		v[2] *= ilength;
	}

	return length;

}

void VectorInverse (vec3_t v)
{
	v[0] = -v[0];
	v[1] = -v[1];
	v[2] = -v[2];
}

void VectorScale (vec3_t in, vec_t scale, vec3_t out)
{
	out[0] = in[0] * scale;
	out[1] = in[1] * scale;
	out[2] = in[2] * scale;
}



