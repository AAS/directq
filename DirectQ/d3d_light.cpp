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
// r_light.c


#include "quakedef.h"
#include "d3d_model.h"
#include "d3d_quake.h"

extern cvar_t r_aliaslightscale;

// note - we need to retain the MAX_LIGHTMAPS limit for texturechain building so we put this in a static array instead of a vector
class LIGHTMAP
{
public:
	LPDIRECT3DTEXTURE9 Texture;
	D3DLOCKED_RECT LockRect;
	RECT DirtyRect;
	int RegistrationSequence;

	// let's get rid of some globals
	static int LightProperty;
	static int LightHunkMark;
	static int NumLightmaps;
	static int LightRegistrationSequence;
	static unsigned short *Allocated;
};

// eeeewww - disgusting language
int LIGHTMAP::LightProperty = 0;
int LIGHTMAP::LightHunkMark = 0;
int LIGHTMAP::NumLightmaps = 0;
int LIGHTMAP::LightRegistrationSequence = 0;
unsigned short *LIGHTMAP::Allocated = NULL;

void D3DLight_PropertyChange (cvar_t *var)
{
	// go to a new property set
	LIGHTMAP::LightProperty++;
}

cvar_t r_ambient ("r_ambient", "0", 0, D3DLight_PropertyChange);
cvar_t r_fullbright ("r_fullbright", "0", 0, D3DLight_PropertyChange);
cvar_t r_coloredlight ("r_coloredlight", "1", CVAR_ARCHIVE, D3DLight_PropertyChange);
cvar_t r_overbright ("r_overbright", 1.0f, CVAR_ARCHIVE, D3DLight_PropertyChange);
cvar_t r_hdrlight ("r_hdrlight", 1.0f, CVAR_ARCHIVE, D3DLight_PropertyChange);
cvar_t r_dynamic ("r_dynamic", "1", 0, D3DLight_PropertyChange);

cvar_t r_lerplightstyle ("r_lerplightstyle", "0", CVAR_ARCHIVE);
cvar_alias_t gl_overbright ("gl_overbright", &r_overbright);
cvar_t gl_overbright_models ("gl_overbright_models", 1.0f, CVAR_ARCHIVE);

cvar_t v_dlightcshift ("v_dlightcshift", 1.0f);

static LIGHTMAP d3d_Lightmaps[MAX_LIGHTMAPS];

typedef struct r_coronadlight_s
{
	dlight_t *dl;
	float radius;
	float color[3];
} r_coronadlight_t;

// let's get rid of some more globals
typedef struct lightglobals_s
{
	int CoronaState;
	int NumCoronas;
	float DLightCutoff;

	int StyleValue[MAX_LIGHTSTYLES];	// 8.8 fraction of base light value
	int ValueTable[256];
	r_coronadlight_t Coronas[MAX_DLIGHTS];
	msurface_t *LightSurf;
	mplane_t *LightPlane;
	vec3_t LightSpot;
} lightglobals_t;

lightglobals_t D3DLightGlobals = {0, 0, 64.0f};

/*
========================================================================================================================

		CORONA RENDERING

========================================================================================================================
*/

cvar_t gl_flashblend ("gl_flashblend", "0", CVAR_ARCHIVE);
cvar_t r_coronas ("r_coronas", "0", CVAR_ARCHIVE);
cvar_t r_coronaradius ("r_coronaradius", "1", CVAR_ARCHIVE);
cvar_t r_coronaintensity ("r_coronaintensity", "1", CVAR_ARCHIVE);

void D3DAlpha_AddToList (dlight_t *dl);

#define CORONA_ONLY				1
#define LIGHTMAP_ONLY			2
#define CORONA_PLUS_LIGHTMAP	3

void D3DLight_SetCoronaState (void)
{
	if (gl_flashblend.value)
	{
		// override everything else, if it's set then we're in coronas-only mode irrespective
		D3DLightGlobals.CoronaState = CORONA_ONLY;
		return;
	}

	// can't use .integer for DP-compatibility
	if (r_coronas.value)
		D3DLightGlobals.CoronaState = CORONA_PLUS_LIGHTMAP;
	else D3DLightGlobals.CoronaState = LIGHTMAP_ONLY;
}


void D3DLight_BeginCoronas (void)
{
	// fix up cvars
	if (r_coronaradius.integer < 0) Cvar_Set (&r_coronaradius, 0.0f);
	if (r_coronaintensity.value < 0) Cvar_Set (&r_coronaintensity, 0.0f);

	// nothing yet
	D3DLightGlobals.NumCoronas = 0;
}


void D3DPart_BeginCoronas (void);
void D3DPart_DrawSingleCorona (float *origin, float *color, float radius);
void D3DPart_CommitCoronas (void);

void D3DLight_EndCoronas (void)
{
	if (D3DLightGlobals.NumCoronas)
	{
		// switch the blendfunc for coronas
		D3DState_SetAlphaBlend (TRUE, D3DBLENDOP_ADD, D3DBLEND_SRCALPHA, D3DBLEND_ONE);
		D3DPart_BeginCoronas ();

		for (int i = 0; i < D3DLightGlobals.NumCoronas; i++)
		{
			r_coronadlight_t *cdl = &D3DLightGlobals.Coronas[i];
			dlight_t *dl = cdl->dl;

			D3DPart_DrawSingleCorona (dl->origin, cdl->color, cdl->radius);
		}

		// draw anything that needs to be drawn
		D3DPart_CommitCoronas ();

		// revert the blend mode change
		D3DState_SetAlphaBlend (TRUE);

		D3DLightGlobals.NumCoronas = 0;
	}
}


void D3DLight_AddLightBlend (float r, float g, float b, float a2)
{
	if (v_dlightcshift.value >= 0) a2 *= v_dlightcshift.value;

	float	a;
	float blend[4] =
	{
		(float) v_blend[0] * 0.00390625f, 
		(float) v_blend[1] * 0.00390625f, 
		(float) v_blend[2] * 0.00390625f, 
		(float) v_blend[3] * 0.00390625f
	};

	blend[3] = a = blend[3] + a2 * (1 - blend[3]);

	a2 = a2 / a;

	blend[0] = blend[0] * (1 - a2) + r * a2;
	blend[1] = blend[1] * (1 - a2) + g * a2;
	blend[2] = blend[2] * (1 - a2) + b * a2;

	v_blend[0] = BYTE_CLAMPF (blend[0]);
	v_blend[1] = BYTE_CLAMPF (blend[1]);
	v_blend[2] = BYTE_CLAMPF (blend[2]);
	v_blend[3] = BYTE_CLAMPF (blend[3]);
}


void D3DLight_DrawCorona (dlight_t *dl)
{
	float v[3];
	float colormul = 0.075f;
	float rad = dl->radius * 0.35 * r_coronaradius.value;

	// reduce corona size and boost intensity a little (to make them easier to see) with r_coronas 2...
	if (D3DLightGlobals.CoronaState == CORONA_PLUS_LIGHTMAP)
	{
		rad *= 0.5f;

		// ?? DP allows different values of it to do this ??
		colormul = 0.1f * r_coronas.value;
	}

	// catch anything that's too small before it's even drawn
	if (rad <= 0) return;
	if (r_coronaintensity.value <= 0) return;

	VectorSubtract (dl->origin, r_refdef.vieworigin, v);
	float dist = Length (v);

	// fixme - optimize this out before adding... (done - ish)
	if (dist < rad)
	{
		// view is inside the dlight
		if (D3DLightGlobals.CoronaState == CORONA_ONLY)
			D3DLight_AddLightBlend ((float) dl->rgb[0] / 512.0f, (float) dl->rgb[1] / 512.0f, (float) dl->rgb[2] / 512.0f, dl->radius * 0.0003);

		return;
	}

	colormul /= 255.0f;

	// store it for drawing later
	// we set this array to MAX_DLIGHTS so we don't need to bounds-check it ;)
	D3DLightGlobals.Coronas[D3DLightGlobals.NumCoronas].dl = dl;
	D3DLightGlobals.Coronas[D3DLightGlobals.NumCoronas].radius = rad;

	D3DLightGlobals.Coronas[D3DLightGlobals.NumCoronas].color[0] = (float) dl->rgb[0] * r_coronaintensity.value * colormul;
	D3DLightGlobals.Coronas[D3DLightGlobals.NumCoronas].color[1] = (float) dl->rgb[1] * r_coronaintensity.value * colormul;
	D3DLightGlobals.Coronas[D3DLightGlobals.NumCoronas].color[2] = (float) dl->rgb[2] * r_coronaintensity.value * colormul;

	// go to the next light
	D3DLightGlobals.NumCoronas++;
}


void D3DLight_AddCoronas (void)
{
	// add coronas
	if (D3DLightGlobals.CoronaState != LIGHTMAP_ONLY)
	{
		for (int lnum = 0; lnum < MAX_DLIGHTS; lnum++)
		{
			// light is dead
			if (cl_dlights[lnum].die < cl.time) continue;
			if (cl_dlights[lnum].radius < 0.01f) continue;
			if (cl_dlights[lnum].flags & DLF_NOCORONA) continue;
			if (cl_dlights[lnum].visframe != d3d_RenderDef.framecount) continue;

			D3DAlpha_AddToList (&cl_dlights[lnum]);
		}
	}
}


/*
=============================================================================

DYNAMIC LIGHTS

=============================================================================
*/

// used by dlights, should move to mathlib
#pragma warning (disable:4035)
__declspec (naked) long Q_ftol (float f)
{
	static int tmp;
	__asm fld dword ptr [esp + 4]
	__asm fistp tmp
	__asm mov eax, tmp
	__asm ret
}
#pragma warning (default:4035)


void D3DLight_DLightCutoffChange (cvar_t *var)
{
	if (var->value < 0)
	{
		D3DLightGlobals.DLightCutoff = 0;
		return;
	}
	else if (var->value > 2048)
	{
		D3DLightGlobals.DLightCutoff = 2048;
		return;
	}

	D3DLightGlobals.DLightCutoff = var->value;
}


cvar_t r_dlightcutoff ("r_dlightcutoff", "64", CVAR_ARCHIVE, D3DLight_DLightCutoffChange);

/*
===============
D3DLight_AddDynamics
===============
*/
bool D3DLight_AddDynamics (msurface_t *surf, unsigned *dest, bool forcedirty)
{
	mtexinfo_t *tex = surf->texinfo;
	float dynamic = r_dynamic.value;
	bool updated = false;

	if (!(r_dynamic.value > 0)) return false;
	if (D3DLightGlobals.CoronaState == CORONA_ONLY) return false;

	for (int lnum = 0; lnum < MAX_DLIGHTS; lnum++)
	{
		// light is dead or has no radius
		if (cl_dlights[lnum].die < cl.time) continue;
		if (cl_dlights[lnum].radius <= 0) continue;

		// if the dlight is not dirty it doesn't need to be updated unless the surf was otherwise updated (in which case it does)
		if (!cl_dlights[lnum].dirty && !forcedirty) continue;

		// not hit by this light
		if (!(surf->dlightbits[lnum >> 5] & (1 << (lnum & 31)))) continue;

		float rad = cl_dlights[lnum].radius;
		float dist = DotProduct (cl_dlights[lnum].transformed, surf->plane->normal) - surf->plane->dist;

		rad -= fabs (dist);

		float minlight = D3DLightGlobals.DLightCutoff;

		if (rad < minlight) continue;

		minlight = rad - minlight;

		float impact[] =
		{
			cl_dlights[lnum].transformed[0] - surf->plane->normal[0] * dist,
			cl_dlights[lnum].transformed[1] - surf->plane->normal[1] * dist,
			cl_dlights[lnum].transformed[2] - surf->plane->normal[2] * dist
		};

		float local[] =
		{
			(DotProduct (impact, tex->vecs[0]) + tex->vecs[0][3]) - surf->texturemins[0],
			(DotProduct (impact, tex->vecs[1]) + tex->vecs[1][3]) - surf->texturemins[1]
		};

		// prevent this multiplication from having to happen for each point
		float dlrgb[] =
		{
			(float) cl_dlights[lnum].rgb[0] * dynamic,
			(float) cl_dlights[lnum].rgb[1] * dynamic,
			(float) cl_dlights[lnum].rgb[2] * dynamic
		};

		unsigned *blocklights = dest;
		int sd, td;

		for (int t = 0, ftacc = 0; t < surf->tmax; t++, ftacc += 16)
		{
			if ((td = Q_ftol (local[1] - ftacc)) < 0) td = -td;

			for (int s = 0, fsacc = 0; s < surf->smax; s++, fsacc += 16, blocklights += 3)
			{
				if ((sd = Q_ftol (local[0] - fsacc)) < 0) sd = -sd;

				if (sd > td)
					dist = sd + (td >> 1);
				else dist = td + (sd >> 1);

				int ladd = (minlight - dist);

				if (ladd > 0)
				{
					blocklights[0] += ladd * dlrgb[0];
					blocklights[1] += ladd * dlrgb[1];
					blocklights[2] += ladd * dlrgb[2];

					// the light is updated now
					updated = true;
				}
			}
		}
	}

	for (int j = 0; j < (MAX_DLIGHTS >> 5); j++)
		surf->dlightbits[j] = 0;

	return updated;
}


/*
==================
R_AnimateLight
==================
*/
void R_SetDefaultLightStyles (void)
{
	for (int i = 0; i < 256; i++)
		D3DLightGlobals.ValueTable[i] = (signed char) i - 'a';

	// normal light value - making this consistent with a value of 'm' in R_AnimateLight
	// will prevent the upload of lightmaps when a surface is first seen
	for (int i = 0; i < MAX_LIGHTSTYLES; i++) D3DLightGlobals.StyleValue[i] = D3DLightGlobals.ValueTable['m'];
}


void R_AnimateLight (float time)
{
	// made this cvar-controllable!
	if (!(r_dynamic.value > 0))
	{
		// set everything to median light
		for (int i = 0; i < MAX_LIGHTSTYLES; i++)
			D3DLightGlobals.StyleValue[i] = D3DLightGlobals.ValueTable['m'];
	}
	else if (r_lerplightstyle.value)
	{
		// interpolated light animations
		int			j;
		float		l;
		int			flight;
		int			clight;
		float		lerpfrac;
		float		backlerp;

		// light animations
		// 'm' is normal light, 'a' is no light, 'z' is double bright
		// to do - coarsen this lerp so that we don't overdo the updates
		flight = (int) floor (time * 10.0f);
		clight = (int) ceil (time * 10.0f);
		lerpfrac = (time * 10.0f) - flight;
		backlerp = 1.0f - lerpfrac;

		for (j = 0; j < MAX_LIGHTSTYLES; j++)
		{
			if (!cl_lightstyle[j].length)
			{
				D3DLightGlobals.StyleValue[j] = D3DLightGlobals.ValueTable['m'];
				continue;
			}
			else if (cl_lightstyle[j].length == 1)
			{
				// single length style so don't bother interpolating
				D3DLightGlobals.StyleValue[j] = D3DLightGlobals.ValueTable[cl_lightstyle[j].map[0]];
				continue;
			}

			// interpolate animating light
			l = (float) D3DLightGlobals.ValueTable[cl_lightstyle[j].map[flight % cl_lightstyle[j].length]] * backlerp;
			l += (float) D3DLightGlobals.ValueTable[cl_lightstyle[j].map[clight % cl_lightstyle[j].length]] * lerpfrac;

			D3DLightGlobals.StyleValue[j] = (int) l;
		}
	}
	else
	{
		// old light animation
		int i = (int) (time * 10.0f);

		for (int j = 0; j < MAX_LIGHTSTYLES; j++)
		{
			if (!cl_lightstyle[j].length)
			{
				D3DLightGlobals.StyleValue[j] = D3DLightGlobals.ValueTable['m'];
				continue;
			}
			else if (cl_lightstyle[j].length == 1)
			{
				// single length style so don't bother interpolating
				D3DLightGlobals.StyleValue[j] = D3DLightGlobals.ValueTable[cl_lightstyle[j].map[0]];
				continue;
			}

			D3DLightGlobals.StyleValue[j] = D3DLightGlobals.ValueTable[cl_lightstyle[j].map[i % cl_lightstyle[j].length]];
		}
	}
}


/*
=============
R_MarkLights
=============
*/
void R_MarkLights (dlight_t *light, int num, mnode_t *node, bool bspmodel)
{
start:;
	if (node->contents < 0) return;

	float dist = SV_PlaneDist (node->plane, light->transformed);

	if (dist > light->radius)
	{
		node = node->children[0];
		goto start;
	}

	if (dist < -light->radius)
	{
		node = node->children[1];
		goto start;
	}

	msurface_t *surf = node->surfaces;
	float maxdist = light->radius * light->radius;

	for (int i = 0; i < node->numsurfaces; i++, surf++)
	{
		// no lights on these
		if (surf->flags & SURF_DRAWTURB) continue;
		if (surf->flags & SURF_DRAWSKY) continue;

		float impact[3] =
		{
			light->transformed[0] - surf->plane->normal[0] * dist,
			light->transformed[1] - surf->plane->normal[1] * dist,
			light->transformed[2] - surf->plane->normal[2] * dist
		};

		// clamp center of light to corner and check brightness
		float l[2] =
		{
			DotProduct (impact, surf->texinfo->vecs[0]) + surf->texinfo->vecs[0][3] - surf->texturemins[0],
			DotProduct (impact, surf->texinfo->vecs[1]) + surf->texinfo->vecs[1][3] - surf->texturemins[1]
		};

		int s = l[0] + 0.5; if (s < 0) s = 0; else if (s > surf->extents[0]) s = surf->extents[0];
		int t = l[1] + 0.5; if (t < 0) t = 0; else if (t > surf->extents[1]) t = surf->extents[1];

		s = l[0] - s;
		t = l[1] - t;

		// compare to minimum light
		if ((s * s + t * t + dist * dist) < maxdist)
		{
			if (surf->dlightframe != d3d_RenderDef.dlightframecount)
			{
				// if it's a BSP model surf force a full light update so that reused surfs will be handled properly
				// this sucks but it's no worse than we were before and at least the world will get scaled back updates
				if (bspmodel) surf->LightProperties = ~LIGHTMAP::LightProperty;

				// first time hit
				for (int j = 0; j < (MAX_DLIGHTS >> 5); j++)
					surf->dlightbits[j] = 0;

				surf->dlightframe = d3d_RenderDef.dlightframecount;
			}

			// mark the surf for this dlight
			surf->dlightbits[num >> 5] |= 1 << (num & 31);
		}
	}

	if (node->children[0]->contents >= 0) R_MarkLights (light, num, node->children[0], bspmodel);
	if (node->children[1]->contents >= 0) R_MarkLights (light, num, node->children[1], bspmodel);
}


/*
=============
R_PushDlights
=============
*/
void R_PushDlights (entity_t *ent, mnode_t *headnode)
{
	if (!(r_dynamic.value > 0)) return;
	if (D3DLightGlobals.CoronaState == CORONA_ONLY) return;

	dlight_t *l = cl_dlights;
	QMATRIX entmatrix;
	bool bspmodel = false;

	// get the inverse matrix for moving the dlight back to the entity
	if (ent)
	{
		// detect if it's a BSP model surf so we can force a light update if hit by dynamics
		if (ent->model && ent->model->name[0] != '*') bspmodel = true;

		// if inverting the matrix fails we just load identity and don't bother
		if (!D3DXMatrixInverse (&entmatrix, NULL, &ent->matrix))
		{
			entmatrix.LoadIdentity ();
			ent = NULL;
		}
	}

	// because we're now able to dynamically light BSP models too (yayy!) we need to use a framecount per-entity to
	// force an update of lighting from a previous time this model may have been used in the current frame
	d3d_RenderDef.dlightframecount++;

	for (int i = 0; i < MAX_DLIGHTS; i++, l++)
	{
		if (l->die < cl.time || l->radius < 0.1f) continue;
		if (l->visframe != d3d_RenderDef.framecount) continue;

		if (ent)
		{
			// transform the light into the correct space for the entity
			entmatrix.TransformPoint (l->origin, l->transformed);
		}
		else
		{
			// light is already in the correct space
			VectorCopy (l->origin, l->transformed);
		}

		sv_frame--;
		R_MarkLights (l, i, headnode, bspmodel);
	}
}


void R_CullDynamicLights (void)
{
	dlight_t *l = cl_dlights;

	for (int i = 0; i < MAX_DLIGHTS; i++, l++)
	{
		if (l->die < cl.time || l->radius < 0.1f) continue;

		// bbcull dlights
		float mins[3], maxs[3];

		for (int j = 0; j < 3; j++)
		{
			mins[j] = l->origin[j] - l->radius;
			maxs[j] = l->origin[j] + l->radius;
		}

		if (R_CullBox (mins, maxs, frustum)) continue;

		// light is unculled this frame...
		l->visframe = d3d_RenderDef.framecount;
	}
}


/*
=============================================================================

LIGHT SAMPLING

=============================================================================
*/

void D3DLight_FromSurface (msurface_t *surf, float *color, int ds, int dt)
{
	// idea - store the lightmap texture num and it's position and sample that?????????
	// limitation - if the surf is not in the PVS then we must update it's lighting to get the colour right....
	// can this accumulate light from more than 1 surface???  and does it really matter????
	// lighting on the GPU - can handle this requirement without needing texture uploads.
	// tradeoff - pixel shader gets complex.
	if (!surf->samples) return;

	// the interpolation code was broken as it could sample points outside of the surface's lightmap.  in the worst case 3 of the 4 points
	// could have been outside (and they could also overflow the lightdata).  this really became noticeable when RMQ moved to RGB10 lightmaps.
	byte *lightmap = surf->samples + ((dt >> 4) * surf->smax + (ds >> 4)) * 3;

	// texcoords for sampling the lightmap
	// s = (float) (surf->LightRect.left + (ds >> 4)) / (float) LIGHTMAP_SIZE;
	// t = (float) (surf->LightRect.top + (dt >> 4)) / (float) LIGHTMAP_SIZE;

	for (int maps = 0; maps < MAX_SURFACE_STYLES && surf->styles[maps] != 255; maps++)
	{
		// keep this consistent with BSP lighting
		// D3DLightGlobals.StyleValue was scaled down to fit in a 64-bit texture so bring it back up again to the same scale as the world
		float scale = (float) D3DLightGlobals.StyleValue[surf->styles[maps]] * 22;

		color[0] += (float) lightmap[0] * scale;
		color[1] += (float) lightmap[1] * scale;
		color[2] += (float) lightmap[2] * scale;

		lightmap += surf->smax * surf->tmax * 3;
	}
}


void D3DLight_StaticEnt (entity_t *e, msurface_t *surf, float *color)
{
	int ds = (int) ((float) DotProduct (e->lightspot, surf->texinfo->vecs[0]) + surf->texinfo->vecs[0][3]);
	int dt = (int) ((float) DotProduct (e->lightspot, surf->texinfo->vecs[1]) + surf->texinfo->vecs[1][3]);

	ds -= surf->texturemins[0];
	dt -= surf->texturemins[1];

	D3DLightGlobals.LightSurf = e->lightsurf;
	VectorCopy2 (D3DLightGlobals.LightSpot, e->lightspot);

	D3DLight_FromSurface (surf, color, ds, dt);
}


bool R_RecursiveLightPoint (vec3_t color, mnode_t *node, vec3_t start, vec3_t end)
{
	float		front, back, frac;
	vec3_t		mid;

loc0:;
	// didn't hit anything
	if (node->contents < 0) return false;

	// calculate mid point
	if (node->plane->type < 3)
	{
		front = start[node->plane->type] - node->plane->dist;
		back = end[node->plane->type] - node->plane->dist;
	}
	else
	{
		front = DotProduct (start, node->plane->normal) - node->plane->dist;
		back = DotProduct (end, node->plane->normal) - node->plane->dist;
	}

	// LordHavoc: optimized recursion
	if ((back < 0) == (front < 0))
	{
		node = node->children[front < 0];
		goto loc0;
	}

	frac = front / (front - back);
	mid[0] = start[0] + (end[0] - start[0]) * frac;
	mid[1] = start[1] + (end[1] - start[1]) * frac;
	mid[2] = start[2] + (end[2] - start[2]) * frac;

	// go down front side
	if (R_RecursiveLightPoint (color, node->children[front < 0], start, mid))
	{
		// hit something
		return true;
	}
	else
	{
		int i, ds, dt;
		msurface_t *surf;

		// check for impact on this node
		VectorCopy (mid, D3DLightGlobals.LightSpot);
		D3DLightGlobals.LightPlane = node->plane;

		for (i = 0, surf = node->surfaces; i < node->numsurfaces; i++, surf++)
		{
			// no lightmaps
			if (surf->flags & SURF_DRAWSKY) continue;
			if (surf->flags & SURF_DRAWTURB) continue;

			ds = (int) ((float) DotProduct (mid, surf->texinfo->vecs[0]) + surf->texinfo->vecs[0][3]);
			dt = (int) ((float) DotProduct (mid, surf->texinfo->vecs[1]) + surf->texinfo->vecs[1][3]);

			// out of range
			if (ds < surf->texturemins[0] || dt < surf->texturemins[1]) continue;

			ds -= surf->texturemins[0];
			dt -= surf->texturemins[1];

			// out of range
			if (ds > surf->extents[0] || dt > surf->extents[1]) continue;

			D3DLight_FromSurface (surf, color, ds, dt);

			// store out the surf that was hit
			D3DLightGlobals.LightSurf = surf;

			// success
			return true;
		}

		// go down back side
		return R_RecursiveLightPoint (color, node->children[front >= 0], mid, end);
	}
}


void R_MinimumLight (float *c, float factor)
{
	float add = factor - (c[0] + c[1] + c[2]);

	if (add > 0.0f)
	{
		c[0] += add / 3.0f;
		c[1] += add / 3.0f;
		c[2] += add / 3.0f;
	}
}


void D3DLight_LightPoint (entity_t *ent)
{
	vec3_t dist;
	vec3_t start;
	vec3_t end;
	float colour[3];

	// using bbox center point can give black models as it's now adjusted for the 
	// correct frame bbox, so just revert back to good ol' origin
	VectorCopy (ent->origin, start);

	// set end point (back to 2048 for less BSP tree tracing)
	end[0] = start[0];
	end[1] = start[1];
	end[2] = start[2] - 2048;

	D3DLightGlobals.LightPlane = NULL;
	D3DLightGlobals.LightSurf = NULL;

	// keep MDL lighting consistent with the world
	if (r_fullbright.integer || !cl.worldmodel->brushhdr->lightdata)
		colour[0] = colour[1] = colour[2] = 32767;
	else
	{
		// clear to ambient
		if (cl.maxclients > 1 || r_ambient.integer < 1)
			colour[0] = colour[1] = colour[2] = 0;
		else colour[0] = colour[1] = colour[2] = r_ambient.integer * 128;

		// add lighting from lightmaps
		if (ent->isStatic && ent->lightsurf)
			D3DLight_StaticEnt (ent, ent->lightsurf, colour);
		else if (!ent->isStatic)
			R_RecursiveLightPoint (colour, cl.worldmodel->brushhdr->nodes, start, end);

		// add dynamic lights
		if (r_dynamic.value && (D3DLightGlobals.CoronaState != CORONA_ONLY))
		{
			for (int lnum = 0; lnum < MAX_DLIGHTS; lnum++)
			{
				if (cl_dlights[lnum].die >= cl.time)
				{
					VectorSubtract (ent->origin, cl_dlights[lnum].origin, dist);

					float frad = cl_dlights[lnum].radius;
					float fdist = fabs (Length (dist));
					float fminlight = D3DLightGlobals.DLightCutoff;

					if (frad < fminlight) continue;

					fminlight = frad - fminlight;

					if (fdist < fminlight)
					{
						colour[0] += (fminlight - fdist) * cl_dlights[lnum].rgb[0] * r_dynamic.value * 0.5f;
						colour[1] += (fminlight - fdist) * cl_dlights[lnum].rgb[1] * r_dynamic.value * 0.5f;
						colour[2] += (fminlight - fdist) * cl_dlights[lnum].rgb[2] * r_dynamic.value * 0.5f;
					}
				}
			}
		}
	}

	// scale back to standard range
	VectorScale (colour, (1.0f / 255.0f), colour);

	// set minimum light values
	if (ent == &cl.viewent) R_MinimumLight (colour, 72);
	if (ent->entnum >= 1 && ent->entnum <= cl.maxclients) R_MinimumLight (colour, 24);
	if (ent->model->flags & EF_ROTATE) R_MinimumLight (colour, 72);

	if (!r_overbright.integer)
	{
		for (int i = 0; i < 3; i++)
		{
			colour[i] *= 2.0f;
			if (colour[i] > 255.0f) colour[i] = 255.0f;
			colour[i] *= 0.5f;
		}
	}

	if (!r_coloredlight.integer)
	{
		float white[3] = {0.299f, 0.587f, 0.114f};
		colour[0] = colour[1] = colour[2] = DotProduct (colour, white);
	}

	// nehahra assumes that fullbrights are not available in the engine
	if ((nehahra || !gl_fullbrights.integer) && (ent->model->aliashdr->drawflags & AM_FULLBRIGHT))
	{
		colour[0] = 255;
		colour[1] = 255;
		colour[2] = 255;
	}

	// take to final range
	if (ent->lerpflags & LERP_RESETLIGHT)
	{
		ent->shadelight[0] = colour[0] * (r_aliaslightscale.value / 255.0f);
		ent->shadelight[1] = colour[1] * (r_aliaslightscale.value / 255.0f);
		ent->shadelight[2] = colour[2] * (r_aliaslightscale.value / 255.0f);
		ent->lerpflags &= ~LERP_RESETLIGHT;
	}
	else
	{
		ent->shadelight[0] = ((colour[0] * (r_aliaslightscale.value / 255.0f)) + ent->shadelight[0]) * 0.5f;
		ent->shadelight[1] = ((colour[1] * (r_aliaslightscale.value / 255.0f)) + ent->shadelight[1]) * 0.5f;
		ent->shadelight[2] = ((colour[2] * (r_aliaslightscale.value / 255.0f)) + ent->shadelight[2]) * 0.5f;
	}

	// shadevector
	float an = (ent->angles[1] + ent->angles[0]) / 180 * D3DX_PI;

	ent->shadevector[0] = cos (-an);
	ent->shadevector[1] = sin (-an);
	ent->shadevector[2] = 1;
	VectorNormalize (ent->shadevector);

	// these 3 really belong in a struct
	VectorCopy2 (ent->lightspot, D3DLightGlobals.LightSpot);
	ent->lightsurf = D3DLightGlobals.LightSurf;
	ent->lightplane = D3DLightGlobals.LightPlane;
}


void D3DLight_PrepStaticEntityLighting (entity_t *ent)
{
	// this needs to run first otherwise the ent won't be lit!!!
	D3DLight_LightPoint (ent);

	// mark as static and save out the results
	ent->isStatic = true;

	// these 3 really belong in a struct
	ent->lightsurf = D3DLightGlobals.LightSurf;
	ent->lightplane = D3DLightGlobals.LightPlane;
	VectorCopy2 (ent->lightspot, D3DLightGlobals.LightSpot);

	// hack - this only ever gets called from parsestatic and needs to be reset to get new statics on the list
	d3d_RenderDef.rebuildworld = true;
}


/*
====================================================================================================================

		LIGHTMAP ALLOCATION AND UPDATING

====================================================================================================================
*/

void D3DLight_ResetDirtyRect (LIGHTMAP *lm)
{
	lm->DirtyRect.left = LIGHTMAP_SIZE;
	lm->DirtyRect.right = 0;
	lm->DirtyRect.top = LIGHTMAP_SIZE;
	lm->DirtyRect.bottom = 0;
}

DWORD WINAPI D3DLight_UpdateLightmap (LPVOID lpParameter)
{
	LIGHTMAP *lm = (LIGHTMAP *) lpParameter;

	lm->Texture->UnlockRect (0);
	lm->Texture->AddDirtyRect (&lm->DirtyRect);
	lm->Texture->PreLoad ();

	D3DLight_ResetDirtyRect (lm);

	return 0;
}


void D3DLight_UnlockLightmaps (void)
{
	// we need to unlock any modified lightmaps before we can draw so do it now
	for (int i = 0; i < MAX_LIGHTMAPS; i++)
	{
		if (!d3d_Lightmaps[i].Texture) continue;

		if (d3d_Lightmaps[i].LockRect.pBits)
		{
			// create with D3DCREATE_MULTITHREADED if you use this!!!
			// currently breaks on BSP model lighting (what else!)
#if 0
			QueueUserWorkItem (D3DLight_UpdateLightmap, &d3d_Lightmaps[i], WT_EXECUTEDEFAULT);
#else
			D3DLight_UpdateLightmap (&d3d_Lightmaps[i]);
#endif

			// done separately so that it will always be NULLed
			d3d_Lightmaps[i].LockRect.pBits = NULL;

			// track number of changed lights
			d3d_RenderDef.numdlight++;
		}
	}
}


void D3DLight_BuildLightmap (msurface_t *surf)
{
	LIGHTMAP *lm = &d3d_Lightmaps[surf->LightmapTextureNum];

	if (!lm->Texture) return;

	int size = surf->smax * surf->tmax * 3;
	int *blocklights = (int *) scratchbuf;
	byte *lightmap = NULL;
	bool updated = false;

	// recache properties here because adding dynamic lights may uncache them
	if (surf->LightProperties != LIGHTMAP::LightProperty)
	{
		surf->LightProperties = LIGHTMAP::LightProperty;
		updated = true;
	}

	// eval base lighting for this surface
	int baselight = 0;

	if (r_fullbright.integer || !surf->model->brushhdr->lightdata)
		baselight = 32767;
	else if (cl.maxclients > 1 || r_ambient.integer < 1)
		baselight = 0;
	else baselight = r_ambient.integer << 7;

	if (!r_fullbright.integer && surf->model->brushhdr->lightdata)
	{
		if ((lightmap = surf->samples) != NULL)
		{
			for (int maps = 0; maps < MAX_SURFACE_STYLES && surf->styles[maps] != 255; maps++)
			{
				// this * 22 will go away when we move to 64-bit lightmaps
				int scale = D3DLightGlobals.StyleValue[surf->styles[maps]] * 22;

				// must reset this each time it's used so that it will be valid for next time
				blocklights = (int *) scratchbuf;

				// avoid an additional pass over blocklights by initializing it on the first map
				if (maps == 0 && scale > 0)
				{
					for (int i = 0; i < size; i += 3, blocklights += 3, lightmap += 3)
					{
						blocklights[0] = lightmap[0] * scale + baselight;
						blocklights[1] = lightmap[1] * scale + baselight;
						blocklights[2] = lightmap[2] * scale + baselight;
					}
				}
				else if (maps == 0)
				{
					for (int i = 0; i < size; i += 3, blocklights += 3, lightmap += 3)
					{
						blocklights[0] = baselight;
						blocklights[1] = baselight;
						blocklights[2] = baselight;
					}
				}
				else if (scale > 0)
				{
					for (int i = 0; i < size; i += 3, blocklights += 3, lightmap += 3)
					{
						blocklights[0] += lightmap[0] * scale;
						blocklights[1] += lightmap[1] * scale;
						blocklights[2] += lightmap[2] * scale;
					}
				}
				else lightmap += size;

				// recache current style
				if (surf->cached_light[maps] != D3DLightGlobals.StyleValue[surf->styles[maps]])
				{
					surf->cached_light[maps] = D3DLightGlobals.StyleValue[surf->styles[maps]];
					updated = true;
				}
			}
		}
		else
		{
			// no lightmaps; clear to base light
			for (int i = 0; i < size; i += 3, blocklights += 3)
			{
				blocklights[0] = baselight;
				blocklights[1] = baselight;
				blocklights[2] = baselight;
			}
		}

		if (surf->dlightframe == d3d_RenderDef.dlightframecount)
		{
			// add all the dynamic lights (don't add if r_fullbright or no lightdata...)
			if (D3DLight_AddDynamics (surf, (unsigned *) scratchbuf, updated))
			{
				// and dirty the properties to force an update next frame in order to clear the light
				surf->LightProperties = ~LIGHTMAP::LightProperty;
				updated = true;
			}
		}
	}
	else
	{
		// clear to base light
		for (int i = 0; i < size; i += 3, blocklights += 3)
		{
			blocklights[0] = baselight;
			blocklights[1] = baselight;
			blocklights[2] = baselight;
		}
	}

	// clear the dlightbits so that the surf won't subsequently keep being updated even if it's been subsequently hit by no lights
	// this is always done so that nothing is left hanging over from a previous frame
	for (int j = 0; j < (MAX_DLIGHTS >> 5); j++)
		surf->dlightbits[j] = 0;

	// and the frame
	surf->dlightframe = -1;

	if (!updated) return;

	if (!lm->LockRect.pBits)
	{
		if (FAILED (lm->Texture->LockRect (0, &lm->LockRect, NULL, d3d_GlobalCaps.DynamicLock)))
		{
			Con_Printf ("IDirect3DTexture9::LockRect failed for lightmap texture.\n");
			return;
		}
	}

	unsigned *dest = (unsigned *) lm->LockRect.pBits;
	int stride = lm->LockRect.Pitch >> 2;

	// track number of changed surfs
	// d3d_RenderDef.numdlight++;

	// standard lighting
	if (!r_coloredlight.integer)
	{
		// 0.299f, 0.587f, 0.114f
		// convert lighting from RGB to greyscale using a bigger scale to preserve precision
		int white[3] = {306, 601, 117};

		blocklights = (int *) scratchbuf;

		for (int i = 0; i < size; i += 3, blocklights += 3)
		{
			int t = (DotProduct (blocklights, white)) >> 10;
			blocklights[0] = blocklights[1] = blocklights[2] = t;
		}
	}

	// get actual pointer to the lightdata for this surf
	dest += (surf->LightRect.top * stride) + surf->LightRect.left;

	blocklights = (int *) scratchbuf;

	// this will all go away with 1.9.0 because we'll be using a 64-bit texture
	if (r_overbright.integer && r_hdrlight.integer)
	{
		int alpha = 255;
		int maxl, a;
		int alphaHigh = alpha << 7;

		for (int i = 0; i < surf->tmax; i++)
		{
			for (int j = 0; j < surf->smax; j++, blocklights += 3)
			{
				if ((blocklights[0] | blocklights[1] | blocklights[2]) > 32767)
				{
					// gives better accuracy and neither component ever goes > 255
					maxl = max3 (blocklights[0], blocklights[1], blocklights[2]) / 254;

					blocklights[0] /= maxl;
					blocklights[1] /= maxl;
					blocklights[2] /= maxl;

					a = alphaHigh / maxl;
				}
				else
				{
					blocklights[0] >>= 7;
					blocklights[1] >>= 7;
					blocklights[2] >>= 7;

					a = alpha;
				}

				dest[j] = blocklights[0] | (blocklights[1] << 8) | (blocklights[2] << 16) | (a << 24);
			}

			dest += stride;
		}
	}
	else
	{
		int alpha = r_overbright.integer ? 128 : 255;
		int shift = r_overbright.integer ? 8 : 7;
		int r, g, b;

		for (int i = 0; i < surf->tmax; i++)
		{
			for (int j = 0; j < surf->smax; j++, blocklights += 3)
			{
				r = (blocklights[0] >> shift) - 255; r = (r & (r >> 31)) + 255;
				g = (blocklights[1] >> shift) - 255; g = (g & (g >> 31)) + 255;
				b = (blocklights[2] >> shift) - 255; b = (b & (b >> 31)) + 255;

				dest[j] = (alpha << 24) | (r << 0) | (g << 8) | (b << 16);
			}

			dest += stride;
		}
	}

	if (surf->LightRect.left < lm->DirtyRect.left) lm->DirtyRect.left = surf->LightRect.left;
	if (surf->LightRect.right > lm->DirtyRect.right) lm->DirtyRect.right = surf->LightRect.right;
	if (surf->LightRect.top < lm->DirtyRect.top) lm->DirtyRect.top = surf->LightRect.top;
	if (surf->LightRect.bottom > lm->DirtyRect.bottom) lm->DirtyRect.bottom = surf->LightRect.bottom;
}


bool D3DLight_AllocBlock (int w, int h, LONG *x, LONG *y)
{
	int		i, j;
	int		best, best2;

	best = LIGHTMAP_SIZE;

	for (i = 0; i < LIGHTMAP_SIZE - w; i++)
	{
		best2 = 0;

		for (j = 0; j < w; j++)
		{
			if (LIGHTMAP::Allocated[i + j] >= best) break;
			if (LIGHTMAP::Allocated[i + j] > best2) best2 = LIGHTMAP::Allocated[i + j];
		}

		if (j == w)
		{
			// this is a valid spot
			*x = i;
			*y = best = best2;
		}
	}

	if (best + h > LIGHTMAP_SIZE)
		return false;

	for (i = 0; i < w; i++)
		LIGHTMAP::Allocated[*x + i] = best + h;

	return true;
}


int D3DLight_SurfaceSortFunc (msurface_t **s1, msurface_t **s2)
{
	return (*s2)->texinfo->texture - (*s1)->texinfo->texture;
}


void D3DLight_BuildAllLightmaps (void)
{
	// go to a new registration sequence so that we can tract which lightmaps are actually used in the current map
	LIGHTMAP::LightRegistrationSequence++;

	// set up the default styles so that (most) lightmaps won't have to be regenerated the first time they're seen
	R_SetDefaultLightStyles ();

	// go to a new property set
	LIGHTMAP::LightProperty++;

	// get the initial hunk mark so that we can do the allocation buffers
	LIGHTMAP::LightHunkMark = MainHunk->GetLowMark ();

	for (int i = 0; i < MAX_LIGHTMAPS; i++)
		D3DLight_ResetDirtyRect (&d3d_Lightmaps[i]);

	// initialize block allocation
	LIGHTMAP::Allocated = (unsigned short *) MainHunk->Alloc (LIGHTMAP_SIZE * sizeof (unsigned short));
	memset (LIGHTMAP::Allocated, 0, LIGHTMAP_SIZE * sizeof (unsigned short));
	LIGHTMAP::NumLightmaps = 0;

	for (int j = 1; j < MAX_MODELS; j++)
	{
		model_t *mod;

		if (!(mod = cl.model_precache[j])) break;
		if (mod->type != mod_brush) continue;

		if (mod->name[0] == '*') continue;

		// catch null models
		if (!mod->brushhdr) continue;
		if (!mod->brushhdr->numsurfaces) continue;

		brushhdr_t *hdr = mod->brushhdr;
		int hunkmark = MainHunk->GetLowMark ();

		// these can't go in the scratch buffer because it's used for blocklights
		msurface_t **lightsurfs = (msurface_t **) MainHunk->Alloc (hdr->numsurfaces * sizeof (msurface_t *));
		int numlightsurfs = 0;

		for (int i = 0; i < hdr->numsurfaces; i++)
		{
			if (hdr->surfaces[i].flags & SURF_DRAWSKY) continue;
			if (hdr->surfaces[i].flags & SURF_DRAWTURB) continue;

			lightsurfs[numlightsurfs] = &hdr->surfaces[i];
			numlightsurfs++;
		}

		Con_DPrintf ("LIGHTMAP::NumLightmaps : incoming : %i\n", LIGHTMAP::NumLightmaps);

		qsort (lightsurfs, numlightsurfs, sizeof (msurface_t *), (sortfunc_t) D3DLight_SurfaceSortFunc);

		for (int i = 0; i < numlightsurfs; i++)
		{
			msurface_t *surf = lightsurfs[i];

			if (surf->flags & SURF_DRAWSKY) continue;
			if (surf->flags & SURF_DRAWTURB) continue;

			// store these out so that we don't have to recalculate them every time
			surf->smax = (surf->extents[0] >> 4) + 1;
			surf->tmax = (surf->extents[1] >> 4) + 1;

			if (!D3DLight_AllocBlock (surf->smax, surf->tmax, &surf->LightRect.left, &surf->LightRect.top))
			{
				// go to a new block
				if ((++LIGHTMAP::NumLightmaps) >= MAX_LIGHTMAPS) Sys_Error ("D3DLight_CreateSurfaceLightmaps : MAX_LIGHTMAPS exceeded");
				memset (LIGHTMAP::Allocated, 0, LIGHTMAP_SIZE * sizeof (unsigned short));

				if (!D3DLight_AllocBlock (surf->smax, surf->tmax, &surf->LightRect.left, &surf->LightRect.top))
					Sys_Error ("D3DLight_CreateSurfaceLightmaps : consecutive calls to D3DLight_AllocBlock failed");
			}

			// fill in lightmap right and bottom (these exist just because I'm lazy and don't want to add a few numbers during updates)
			surf->LightRect.right = surf->LightRect.left + surf->smax;
			surf->LightRect.bottom = surf->LightRect.top + surf->tmax;

			// reuse any lightmaps which were previously allocated
			if (!d3d_Lightmaps[LIGHTMAP::NumLightmaps].Texture)
			{
				hr = d3d_Device->CreateTexture
				(
					LIGHTMAP_SIZE,
					LIGHTMAP_SIZE,
					1,
					0,
					D3DFMT_A8R8G8B8,
					D3DPOOL_MANAGED,
					&d3d_Lightmaps[LIGHTMAP::NumLightmaps].Texture,
					NULL
				);

				if (FAILED (hr)) Sys_Error ("D3DLight_CreateSurfaceLightmap : IDirect3DDevice9::CreateTexture failed");
			}

			// ensure no dlight update happens and rebuild the lightmap fully
			surf->dlightframe = -1;

			// initially assign these
			surf->LightmapTextureNum = LIGHTMAP::NumLightmaps;
			surf->d3d_LightmapTex = d3d_Lightmaps[surf->LightmapTextureNum].Texture;

			// mark this lightmap as currently used
			d3d_Lightmaps[surf->LightmapTextureNum].RegistrationSequence = LIGHTMAP::LightRegistrationSequence;

			// the surf initially has invalid properties set which forces the lightmap to be built here
			surf->LightProperties = ~LIGHTMAP::LightProperty;

			// also invalidate the light cache to force a recache at the correct values on build
			surf->cached_light[0] = surf->cached_light[1] = surf->cached_light[2] = surf->cached_light[3] = -1;

			// and build the map
			D3DLight_BuildLightmap (surf);
		}

		MainHunk->FreeToLowMark (hunkmark);
		Con_DPrintf ("LIGHTMAP::NumLightmaps : outgoing : %i\n", LIGHTMAP::NumLightmaps);
	}

	// any future attempts to access this should crash
	LIGHTMAP::Allocated = NULL;
	LIGHTMAP::NumLightmaps++;

	// preload everything to prevent runtime stalls
	for (int i = 0; i < MAX_LIGHTMAPS; i++)
	{
		// update any lightmaps which were created and release any left over which were unused
		if (!d3d_Lightmaps[i].Texture) continue;

		if (d3d_Lightmaps[i].LockRect.pBits)
		{
			d3d_Lightmaps[i].Texture->UnlockRect (0);
			d3d_Lightmaps[i].LockRect.pBits = NULL;
		}

		if (d3d_Lightmaps[i].RegistrationSequence == LIGHTMAP::LightRegistrationSequence)
		{
			d3d_Lightmaps[i].Texture->AddDirtyRect (NULL);
			d3d_Lightmaps[i].Texture->PreLoad ();

			D3DLight_ResetDirtyRect (&d3d_Lightmaps[i]);
		}
		else
		{
			// release any lightmaps which were unused
			SAFE_RELEASE (d3d_Lightmaps[i].Texture);
		}
	}

	MainHunk->FreeToLowMark (LIGHTMAP::LightHunkMark);
}


void D3DLight_CheckSurfaceForModification (msurface_t *surf)
{
	// no lightmaps
	if (surf->flags & SURF_DRAWSKY) return;
	if (surf->flags & SURF_DRAWTURB) return;

	// ensure that we've got the correct texture for this surf
	surf->d3d_LightmapTex = d3d_Lightmaps[surf->LightmapTextureNum].Texture;

	// disable dynamic lights on this surf in coronas-only mode (don't set cached to false here as
	// we want to clear any previous dlights; R_PushDLights will look after not adding more for us)
	if (D3DLightGlobals.CoronaState == CORONA_ONLY)
		surf->dlightframe = ~d3d_RenderDef.dlightframecount;

	// check for lighting parameter modification
	if (surf->LightProperties != LIGHTMAP::LightProperty)
	{
		D3DLight_BuildLightmap (surf);
		return;
	}

	// note - r_dynamic sets to median light so we must still check the styles as they may need to change from non-median.
	// we must also clear any previously added dlights; not adding new dlights is handled in the dlight adding functions.
	// dynamic light this frame
	if (surf->dlightframe == d3d_RenderDef.dlightframecount)
	{
		D3DLight_BuildLightmap (surf);
		return;
	}

	// cached lightstyle change
	for (int maps = 0; maps < MAX_SURFACE_STYLES && surf->styles[maps] != 255; maps++)
	{
		if (surf->cached_light[maps] != D3DLightGlobals.StyleValue[surf->styles[maps]])
		{
			// Con_Printf ("Modified light from %i to %i\n", surf->cached_light[maps], D3DLightGlobals.StyleValue[surf->styles[maps]]);
			D3DLight_BuildLightmap (surf);
			return;
		}
	}
}


void D3DLight_ReleaseLightmaps (void)
{
	for (int i = 0; i < MAX_LIGHTMAPS; i++)
	{
		SAFE_RELEASE (d3d_Lightmaps[i].Texture);

		D3DLight_ResetDirtyRect (&d3d_Lightmaps[i]);

		d3d_Lightmaps[i].LockRect.pBits = NULL;
		d3d_Lightmaps[i].RegistrationSequence = 0;
	}
}


