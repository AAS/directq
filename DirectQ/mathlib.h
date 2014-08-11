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


// mathlib.h

#define min2(a, b) ((a) < (b) ? (a) : (b))
#define min3(a, b, c) (min2 ((a), min2 ((b), (c))))

#define max2(a, b) ((a) > (b) ? (a) : (b))
#define max3(a, b, c) (max2 ((a), max2 ((b), (c))))

typedef float vec_t;
typedef vec_t vec3_t[3];
typedef vec_t vec5_t[5];

typedef	int	fixed4_t;
typedef	int	fixed8_t;
typedef	int	fixed16_t;

struct mplane_s;

extern vec3_t vec3_origin;
extern	int nanmask;

#define	IS_NAN(x) (((*(int *)&x)&nanmask)==nanmask)

#define DotProduct(x,y) ((x)[0]*(y)[0]+(x)[1]*(y)[1]+(x)[2]*(y)[2])
#define VectorSubtract(vec1,vec2,dst) {(dst)[0]=(vec1)[0]-(vec2)[0];(dst)[1]=(vec1)[1]-(vec2)[1];(dst)[2]=(vec1)[2]-(vec2)[2];}
#define VectorAdd(vec1,vec2,dst) {(dst)[0]=(vec1)[0]+(vec2)[0];(dst)[1]=(vec1)[1]+(vec2)[1];(dst)[2]=(vec1)[2]+(vec2)[2];}
#define VectorCopy(src,dst) {(dst)[0]=(src)[0];(dst)[1]=(src)[1];(dst)[2]=(src)[2];}
#define VectorCopy2(dst,src) {(dst)[0]=(src)[0];(dst)[1]=(src)[1];(dst)[2]=(src)[2];}
#define VectorClear(vec) {(vec)[0] = (vec)[1] = (vec)[2] = 0.0f;}
#define VectorSet(vec,x,y,z) {(vec)[0] = x; (vec)[1] = y; (vec)[2] = z;}

void VectorMad (vec3_t veca, float scale, vec3_t vecb, vec3_t vecc);

vec_t _DotProduct (vec3_t v1, vec3_t v2);
void _VectorSubtract (vec3_t veca, vec3_t vecb, vec3_t out);
void _VectorAdd (vec3_t veca, vec3_t vecb, vec3_t out);
void _VectorCopy (vec3_t in, vec3_t out);

int VectorCompare (vec3_t v1, vec3_t v2);
vec_t Length (vec3_t v);
void CrossProduct (vec3_t v1, vec3_t v2, vec3_t cross);
float VectorNormalize (vec3_t v);		// returns vector length
void VectorInverse (vec3_t v);
void VectorScale (vec3_t in, vec_t scale, vec3_t out);
int Q_log2 (int val);

void R_ConcatRotations (float in1[3][3], float in2[3][3], float out[3][3]);
void R_ConcatTransforms (float in1[3][4], float in2[3][4], float out[3][4]);

void FloorDivMod (float numer, float denom, int *quotient, int *rem);
int GreatestCommonDivisor (int i1, int i2);

typedef struct avectors_s
{
	vec3_t forward;
	vec3_t right;
	vec3_t up;
} avectors_t;


void AngleVectors (vec3_t angles, avectors_t *av);
void AngleVectors (vec3_t angles, vec3_t forward, vec3_t right, vec3_t up);
int BoxOnPlaneSide (vec3_t emins, vec3_t emaxs, struct mplane_s *plane);
float	anglemod (float a);

#define BOX_ON_PLANE_SIDE(emins, emaxs, p)	\
	(((p)->type < 3)?						\
	(										\
		((p)->dist <= (emins)[(p)->type])?	\
			1								\
		:									\
		(									\
			((p)->dist >= (emaxs)[(p)->type])?\
				2							\
			:								\
				3							\
		)									\
	)										\
	:										\
		BoxOnPlaneSide((emins), (emaxs), (p)))

#define CLAMP(min, x, max) ((x) < (min) ? (min) : (x) > (max) ? (max) : (x))

#define Q_rint(x) ((x) > 0 ? (int)((x) + 0.5) : (int)((x) - 0.5))

#define Q_Random(MIN,MAX) ((rand () & 32767) * (((MAX) - (MIN)) * (1.0f / 32767.0f)) + (MIN))

void NonEulerInterpolateAngles (float *currangles, float *lastangles, float lerp, float *outangles);
