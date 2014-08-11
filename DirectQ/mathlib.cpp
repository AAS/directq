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

void ProjectPointOnPlane (vec3_t dst, const vec3_t p, const vec3_t normal)
{
	float d;
	vec3_t n;
	float inv_denom;

	inv_denom = 1.0F / DotProduct (normal, normal);

	d = DotProduct (normal, p) * inv_denom;

	n[0] = normal[0] * inv_denom;
	n[1] = normal[1] * inv_denom;
	n[2] = normal[2] * inv_denom;

	dst[0] = p[0] - d * n[0];
	dst[1] = p[1] - d * n[1];
	dst[2] = p[2] - d * n[2];
}

/*
** assumes "src" is normalized
*/
void PerpendicularVector (vec3_t dst, const vec3_t src)
{
	int	pos;
	int i;
	float minelem = 1.0F;
	vec3_t tempvec;

	/*
	** find the smallest magnitude axially aligned vector
	*/
	for (pos = 0, i = 0; i < 3; i++)
	{
		if (fabs (src[i]) < minelem)
		{
			pos = i;
			minelem = fabs (src[i]);
		}
	}

	tempvec[0] = tempvec[1] = tempvec[2] = 0.0F;
	tempvec[pos] = 1.0F;

	/*
	** project the point onto the plane defined by src
	*/
	ProjectPointOnPlane (dst, tempvec, src);

	/*
	** normalize the result
	*/
	VectorNormalize (dst);
}

#ifdef _WIN32
#pragma optimize( "", off )
#endif


void RotatePointAroundVector (vec3_t dst, const vec3_t dir, const vec3_t point, float degrees)
{
	float	m[3][3];
	float	im[3][3];
	float	zrot[3][3];
	float	tmpmat[3][3];
	float	rot[3][3];
	int	i;
	vec3_t vr, vup, vf;

	vf[0] = dir[0];
	vf[1] = dir[1];
	vf[2] = dir[2];

	PerpendicularVector (vr, dir);
	CrossProduct (vr, vf, vup);

	m[0][0] = vr[0];
	m[1][0] = vr[1];
	m[2][0] = vr[2];

	m[0][1] = vup[0];
	m[1][1] = vup[1];
	m[2][1] = vup[2];

	m[0][2] = vf[0];
	m[1][2] = vf[1];
	m[2][2] = vf[2];

	memcpy (im, m, sizeof (im));

	im[0][1] = m[1][0];
	im[0][2] = m[2][0];
	im[1][0] = m[0][1];
	im[1][2] = m[2][1];
	im[2][0] = m[0][2];
	im[2][1] = m[1][2];

	memset (zrot, 0, sizeof (zrot));
	zrot[0][0] = zrot[1][1] = zrot[2][2] = 1.0F;

	zrot[0][0] = cos (D3DXToRadian (degrees));
	zrot[0][1] = sin (D3DXToRadian (degrees));
	zrot[1][0] = -sin (D3DXToRadian (degrees));
	zrot[1][1] = cos (D3DXToRadian (degrees));

	R_ConcatRotations (m, zrot, tmpmat);
	R_ConcatRotations (tmpmat, im, rot);

	for (i = 0; i < 3; i++)
	{
		dst[i] = rot[i][0] * point[0] + rot[i][1] * point[1] + rot[i][2] * point[2];
	}
}

#ifdef _WIN32
#pragma optimize ("", on)
#endif

/*-----------------------------------------------------------------*/


float anglemod (float a)
{
	a = (360.0 / 65536) * ((int) (a * (65536 / 360.0)) & 65535);
	return a;
}


/*
==================
BoxOnPlaneSide

Returns 1, 2, or 1 + 2
==================
*/
__declspec (naked) int BoxOnPlaneSide (vec3_t emins, vec3_t emaxs, mplane_t *p)
{
	static int bops_initialized;
	static int Ljmptab[8];

	__asm
	{
		push ebx

		cmp bops_initialized, 1
		je  initialized
		mov bops_initialized, 1

		mov Ljmptab[0*4], offset Lcase0
		mov Ljmptab[1*4], offset Lcase1
		mov Ljmptab[2*4], offset Lcase2
		mov Ljmptab[3*4], offset Lcase3
		mov Ljmptab[4*4], offset Lcase4
		mov Ljmptab[5*4], offset Lcase5
		mov Ljmptab[6*4], offset Lcase6
		mov Ljmptab[7*4], offset Lcase7

		initialized:

		mov edx, ds: dword ptr[4 + 12 + esp]
		mov ecx, ds: dword ptr[4 + 4 + esp]
		xor eax, eax
		mov ebx, ds: dword ptr[4 + 8 + esp]
		mov al, ds: byte ptr[17 + edx]
		cmp al, 8
		jge Lerror
		fld ds: dword ptr[0 + edx]
		fld st (0)
		jmp dword ptr[Ljmptab + eax*4]
		Lcase0:
		fmul ds: dword ptr[ebx]
		fld ds: dword ptr[0 + 4 + edx]
		fxch st (2)
		fmul ds: dword ptr[ecx]
		fxch st (2)
		fld st (0)
		fmul ds: dword ptr[4 + ebx]
		fld ds: dword ptr[0 + 8 + edx]
		fxch st (2)
		fmul ds: dword ptr[4 + ecx]
		fxch st (2)
		fld st (0)
		fmul ds: dword ptr[8 + ebx]
		fxch st (5)
		faddp st (3), st (0)
		fmul ds: dword ptr[8 + ecx]
		fxch st (1)
		faddp st (3), st (0)
		fxch st (3)
		faddp st (2), st (0)
		jmp LSetSides
		Lcase1:
		fmul ds: dword ptr[ecx]
		fld ds: dword ptr[0 + 4 + edx]
		fxch st (2)
		fmul ds: dword ptr[ebx]
		fxch st (2)
		fld st (0)
		fmul ds: dword ptr[4 + ebx]
		fld ds: dword ptr[0 + 8 + edx]
		fxch st (2)
		fmul ds: dword ptr[4 + ecx]
		fxch st (2)
		fld st (0)
		fmul ds: dword ptr[8 + ebx]
		fxch st (5)
		faddp st (3), st (0)
		fmul ds: dword ptr[8 + ecx]
		fxch st (1)
		faddp st (3), st (0)
		fxch st (3)
		faddp st (2), st (0)
		jmp LSetSides
		Lcase2:
		fmul ds: dword ptr[ebx]
		fld ds: dword ptr[0 + 4 + edx]
		fxch st (2)
		fmul ds: dword ptr[ecx]
		fxch st (2)
		fld st (0)
		fmul ds: dword ptr[4 + ecx]
		fld ds: dword ptr[0 + 8 + edx]
		fxch st (2)
		fmul ds: dword ptr[4 + ebx]
		fxch st (2)
		fld st (0)
		fmul ds: dword ptr[8 + ebx]
		fxch st (5)
		faddp st (3), st (0)
		fmul ds: dword ptr[8 + ecx]
		fxch st (1)
		faddp st (3), st (0)
		fxch st (3)
		faddp st (2), st (0)
		jmp LSetSides
		Lcase3:
		fmul ds: dword ptr[ecx]
		fld ds: dword ptr[0 + 4 + edx]
		fxch st (2)
		fmul ds: dword ptr[ebx]
		fxch st (2)
		fld st (0)
		fmul ds: dword ptr[4 + ecx]
		fld ds: dword ptr[0 + 8 + edx]
		fxch st (2)
		fmul ds: dword ptr[4 + ebx]
		fxch st (2)
		fld st (0)
		fmul ds: dword ptr[8 + ebx]
		fxch st (5)
		faddp st (3), st (0)
		fmul ds: dword ptr[8 + ecx]
		fxch st (1)
		faddp st (3), st (0)
		fxch st (3)
		faddp st (2), st (0)
		jmp LSetSides
		Lcase4:
		fmul ds: dword ptr[ebx]
		fld ds: dword ptr[0 + 4 + edx]
		fxch st (2)
		fmul ds: dword ptr[ecx]
		fxch st (2)
		fld st (0)
		fmul ds: dword ptr[4 + ebx]
		fld ds: dword ptr[0 + 8 + edx]
		fxch st (2)
		fmul ds: dword ptr[4 + ecx]
		fxch st (2)
		fld st (0)
		fmul ds: dword ptr[8 + ecx]
		fxch st (5)
		faddp st (3), st (0)
		fmul ds: dword ptr[8 + ebx]
		fxch st (1)
		faddp st (3), st (0)
		fxch st (3)
		faddp st (2), st (0)
		jmp LSetSides
		Lcase5:
		fmul ds: dword ptr[ecx]
		fld ds: dword ptr[0 + 4 + edx]
		fxch st (2)
		fmul ds: dword ptr[ebx]
		fxch st (2)
		fld st (0)
		fmul ds: dword ptr[4 + ebx]
		fld ds: dword ptr[0 + 8 + edx]
		fxch st (2)
		fmul ds: dword ptr[4 + ecx]
		fxch st (2)
		fld st (0)
		fmul ds: dword ptr[8 + ecx]
		fxch st (5)
		faddp st (3), st (0)
		fmul ds: dword ptr[8 + ebx]
		fxch st (1)
		faddp st (3), st (0)
		fxch st (3)
		faddp st (2), st (0)
		jmp LSetSides
		Lcase6:
		fmul ds: dword ptr[ebx]
		fld ds: dword ptr[0 + 4 + edx]
		fxch st (2)
		fmul ds: dword ptr[ecx]
		fxch st (2)
		fld st (0)
		fmul ds: dword ptr[4 + ecx]
		fld ds: dword ptr[0 + 8 + edx]
		fxch st (2)
		fmul ds: dword ptr[4 + ebx]
		fxch st (2)
		fld st (0)
		fmul ds: dword ptr[8 + ecx]
		fxch st (5)
		faddp st (3), st (0)
		fmul ds: dword ptr[8 + ebx]
		fxch st (1)
		faddp st (3), st (0)
		fxch st (3)
		faddp st (2), st (0)
		jmp LSetSides
		Lcase7:
		fmul ds: dword ptr[ecx]
		fld ds: dword ptr[0 + 4 + edx]
		fxch st (2)
		fmul ds: dword ptr[ebx]
		fxch st (2)
		fld st (0)
		fmul ds: dword ptr[4 + ecx]
		fld ds: dword ptr[0 + 8 + edx]
		fxch st (2)
		fmul ds: dword ptr[4 + ebx]
		fxch st (2)
		fld st (0)
		fmul ds: dword ptr[8 + ecx]
		fxch st (5)
		faddp st (3), st (0)
		fmul ds: dword ptr[8 + ebx]
		fxch st (1)
		faddp st (3), st (0)
		fxch st (3)
		faddp st (2), st (0)
		LSetSides:
		faddp st (2), st (0)
		fcomp ds: dword ptr[12 + edx]
		xor ecx, ecx
		fnstsw ax
		fcomp ds: dword ptr[12 + edx]
		and ah, 1
		xor ah, 1
		add cl, ah
		fnstsw ax
		and ah, 1
		add ah, ah
		add cl, ah
		pop ebx
		mov eax, ecx
		ret
		Lerror:
		int 3
	}
}


void AngleVectors (vec3_t angles, vec3_t forward, vec3_t right, vec3_t up)
{
	if (kurok)
	{
		double angle, sr, sp, sy, cr, cp, cy;

		angle = angles[YAW] * (D3DX_PI * 2 / 360);

		sy = sin (angle);
		cy = cos (angle);

		angle = angles[PITCH] * (D3DX_PI * 2 / 360);

		sp = sin (angle);
		cp = cos (angle);

		if (forward)
		{
			forward[0] = cp * cy;
			forward[1] = cp * sy;
			forward[2] = -sp;
		}

		if (right || up)
		{
			if (angles[ROLL])
			{
				angle = angles[ROLL] * (D3DX_PI * 2 / 360);

				sr = sin(angle);
				cr = cos(angle);

				if (right)
				{
					right[0] = -1 * (sr * sp * cy + cr * -sy);
					right[1] = -1 * (sr * sp * sy + cr * cy);
					right[2] = -1 * (sr * cp);
				}

				if (up)
				{
					up[0] = (cr * sp * cy + -sr * -sy);
					up[1] = (cr * sp * sy + -sr * cy);
					up[2] = cr * cp;
				}
			}
			else
			{
				if (right)
				{
					right[0] = sy;
					right[1] = -cy;
					right[2] = 0;
				}

				if (up)
				{
					up[0] = (sp * cy);
					up[1] = (sp * sy);
					up[2] = cp;
				}
			}
		}

		return;
	}

	float		angle;
	float		sr, sp, sy, cr, cp, cy;

	angle = angles[YAW] * (D3DX_PI * 2 / 360);
	sy = sin (angle);
	cy = cos (angle);

	angle = angles[PITCH] * (D3DX_PI * 2 / 360);
	sp = sin (angle);
	cp = cos (angle);

	angle = angles[ROLL] * (D3DX_PI * 2 / 360);
	sr = sin (angle);
	cr = cos (angle);

	forward[0] = cp * cy;
	forward[1] = cp * sy;
	forward[2] = -sp;

	right[0] = (-1 * sr * sp * cy + -1 * cr * -sy);
	right[1] = (-1 * sr * sp * sy + -1 * cr * cy);
	right[2] = -1 * sr * cp;

	up[0] = (cr * sp * cy + -sr * -sy);
	up[1] = (cr * sp * sy + -sr * cy);
	up[2] = cr * cp;
}

void AngleVectors (vec3_t angles, avectors_t *av)
{
	AngleVectors (angles, av->forward, av->right, av->up);
}


int VectorCompare (vec3_t v1, vec3_t v2)
{
	if (v1[0] != v2[0]) return 0;
	if (v1[1] != v2[1]) return 0;
	if (v1[2] != v2[2]) return 0;

	return 1;
}

void VectorMad (vec3_t veca, float scale, vec3_t vecb, vec3_t vecc)
{
	vecc[0] = veca[0] + scale * vecb[0];
	vecc[1] = veca[1] + scale * vecb[1];
	vecc[2] = veca[2] + scale * vecb[2];
}


vec_t _DotProduct (vec3_t v1, vec3_t v2)
{
	return v1[0] * v2[0] + v1[1] * v2[1] + v1[2] * v2[2];
}

void _VectorSubtract (vec3_t veca, vec3_t vecb, vec3_t out)
{
	out[0] = veca[0] - vecb[0];
	out[1] = veca[1] - vecb[1];
	out[2] = veca[2] - vecb[2];
}

void _VectorAdd (vec3_t veca, vec3_t vecb, vec3_t out)
{
	out[0] = veca[0] + vecb[0];
	out[1] = veca[1] + vecb[1];
	out[2] = veca[2] + vecb[2];
}

void _VectorCopy (vec3_t in, vec3_t out)
{
	out[0] = in[0];
	out[1] = in[1];
	out[2] = in[2];
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


int Q_log2 (int val)
{
	int answer = 0;

	while (val >>= 1)
		answer++;

	return answer;
}


/*
================
R_ConcatRotations
================
*/
void R_ConcatRotations (float in1[3][3], float in2[3][3], float out[3][3])
{
	out[0][0] = in1[0][0] * in2[0][0] + in1[0][1] * in2[1][0] +
				in1[0][2] * in2[2][0];
	out[0][1] = in1[0][0] * in2[0][1] + in1[0][1] * in2[1][1] +
				in1[0][2] * in2[2][1];
	out[0][2] = in1[0][0] * in2[0][2] + in1[0][1] * in2[1][2] +
				in1[0][2] * in2[2][2];
	out[1][0] = in1[1][0] * in2[0][0] + in1[1][1] * in2[1][0] +
				in1[1][2] * in2[2][0];
	out[1][1] = in1[1][0] * in2[0][1] + in1[1][1] * in2[1][1] +
				in1[1][2] * in2[2][1];
	out[1][2] = in1[1][0] * in2[0][2] + in1[1][1] * in2[1][2] +
				in1[1][2] * in2[2][2];
	out[2][0] = in1[2][0] * in2[0][0] + in1[2][1] * in2[1][0] +
				in1[2][2] * in2[2][0];
	out[2][1] = in1[2][0] * in2[0][1] + in1[2][1] * in2[1][1] +
				in1[2][2] * in2[2][1];
	out[2][2] = in1[2][0] * in2[0][2] + in1[2][1] * in2[1][2] +
				in1[2][2] * in2[2][2];
}


/*
================
R_ConcatTransforms
================
*/
void R_ConcatTransforms (float in1[3][4], float in2[3][4], float out[3][4])
{
	out[0][0] = in1[0][0] * in2[0][0] + in1[0][1] * in2[1][0] +
				in1[0][2] * in2[2][0];
	out[0][1] = in1[0][0] * in2[0][1] + in1[0][1] * in2[1][1] +
				in1[0][2] * in2[2][1];
	out[0][2] = in1[0][0] * in2[0][2] + in1[0][1] * in2[1][2] +
				in1[0][2] * in2[2][2];
	out[0][3] = in1[0][0] * in2[0][3] + in1[0][1] * in2[1][3] +
				in1[0][2] * in2[2][3] + in1[0][3];
	out[1][0] = in1[1][0] * in2[0][0] + in1[1][1] * in2[1][0] +
				in1[1][2] * in2[2][0];
	out[1][1] = in1[1][0] * in2[0][1] + in1[1][1] * in2[1][1] +
				in1[1][2] * in2[2][1];
	out[1][2] = in1[1][0] * in2[0][2] + in1[1][1] * in2[1][2] +
				in1[1][2] * in2[2][2];
	out[1][3] = in1[1][0] * in2[0][3] + in1[1][1] * in2[1][3] +
				in1[1][2] * in2[2][3] + in1[1][3];
	out[2][0] = in1[2][0] * in2[0][0] + in1[2][1] * in2[1][0] +
				in1[2][2] * in2[2][0];
	out[2][1] = in1[2][0] * in2[0][1] + in1[2][1] * in2[1][1] +
				in1[2][2] * in2[2][1];
	out[2][2] = in1[2][0] * in2[0][2] + in1[2][1] * in2[1][2] +
				in1[2][2] * in2[2][2];
	out[2][3] = in1[2][0] * in2[0][3] + in1[2][1] * in2[1][3] +
				in1[2][2] * in2[2][3] + in1[2][3];
}


/*
===================
FloorDivMod

Returns mathematically correct (floor-based) quotient and remainder for
numer and denom, both of which should contain no fractional part. The
quotient must fit in 32 bits.
====================
*/

void FloorDivMod (float numer, float denom, int *quotient, int *rem)
{
	int		q, r;
	float	x;

	// this was #ifndef PARANOID - should it have been #ifdef?
	if (denom <= 0.0)
		Sys_Error ("FloorDivMod: bad denominator %d\n", denom);

	if (numer >= 0.0)
	{

		x = floor (numer / denom);
		q = (int) x;
		r = (int) floor (numer - (x * denom));
	}
	else
	{
		// perform operations with positive values, and fix mod to make floor-based
		x = floor (-numer / denom);
		q = - (int) x;
		r = (int) floor (-numer - (x * denom));

		if (r != 0)
		{
			q--;
			r = (int) denom - r;
		}
	}

	*quotient = q;
	*rem = r;
}


/*
===================
GreatestCommonDivisor
====================
*/
int GreatestCommonDivisor (int i1, int i2)
{
	if (i1 > i2)
	{
		if (i2 == 0)
			return (i1);

		return GreatestCommonDivisor (i2, i1 % i2);
	}
	else
	{
		if (i1 == 0)
			return (i2);

		return GreatestCommonDivisor (i1, i2 % i1);
	}
}


#define VectorMAM(scale1, b1, scale2, b2, c) ((c)[0] = (scale1) * (b1)[0] + (scale2) * (b2)[0],(c)[1] = (scale1) * (b1)[1] + (scale2) * (b2)[1],(c)[2] = (scale1) * (b1)[2] + (scale2) * (b2)[2])

void AnglesFromVectors (float *angles, const float *forward, const float *up, bool flippitch)
{
	if (forward[0] == 0 && forward[1] == 0)
	{
		if (forward[2] > 0)
		{
			angles[PITCH] = -D3DX_PI * 0.5;
			angles[YAW] = up ? atan2 (-up[1], -up[0]) : 0;
		}
		else
		{
			angles[PITCH] = D3DX_PI * 0.5;
			angles[YAW] = up ? atan2 (up[1], up[0]) : 0;
		}

		angles[ROLL] = 0;
	}
	else
	{
		angles[YAW] = atan2(forward[1], forward[0]);
		angles[PITCH] = -atan2(forward[2], sqrt(forward[0]*forward[0] + forward[1]*forward[1]));

		if (up)
		{
			vec_t cp = cos (angles[PITCH]), sp = sin (angles[PITCH]);
			vec_t cy = cos (angles[YAW]), sy = sin (angles[YAW]);

			vec3_t tleft, tup;

			tleft[0] = -sy;
			tleft[1] = cy;
			tleft[2] = 0;

			tup[0] = sp * cy;
			tup[1] = sp * sy;
			tup[2] = cp;

			angles[ROLL] = -atan2 (DotProduct (up, tleft), DotProduct (up, tup));
		}
		else angles[ROLL] = 0;
	}

	// now convert radians to degrees, and make all values positive
	VectorScale (angles, 180.0 / D3DX_PI, angles);

	if (flippitch) angles[PITCH] *= -1;
	if (angles[PITCH] < 0) angles[PITCH] += 360;
	if (angles[YAW] < 0) angles[YAW] += 360;
	if (angles[ROLL] < 0) angles[ROLL] += 360;
}


void NonEulerInterpolateAngles (float *currangles, float *lastangles, float lerp, float *outangles)
{
	vec3_t f0, r0, u0, f1, r1, u1;

	AngleVectors (lastangles, f0, r0, u0);
	AngleVectors (currangles, f1, r1, u1);

	VectorMAM (1 - lerp, f0, lerp, f1, f0);
	VectorMAM (1 - lerp, u0, lerp, u1, u0);

	AnglesFromVectors (outangles, f0, u0, false);
}


