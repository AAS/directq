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

#define LIGHTMAP_WIDTH		512
#define LIGHTMAP_HEIGHT		512

int LightmapWidth = -1;
int LightmapHeight = -1;

typedef struct d3d_lightmap_s
{
	LPDIRECT3DTEXTURE9 Texture;
	RECT DirtyRect;
	byte *Data;
	unsigned short *Allocated;
} d3d_lightmap_t;

d3d_lightmap_t d3d_Lightmaps[MAX_LIGHTMAPS];

byte lm_gammatable[256];

void D3DLight_BuildGammaTable (cvar_t *var)
{
	float gammaval = var->value;

	if (gammaval < 0.25f) gammaval = 0.25f;
	if (gammaval > 1.75f) gammaval = 1.75f;

	for (int i = 0; i < 256; i++)
	{
		// the same gamma calc as GLQuake had some "gamma creep" where DirectQ would gradually get brighter
		// the more it was run; this hopefully fixes it once and for all
		float f = pow ((float) i / 255.0f, gammaval);
		float inf = f * 255 + 0.5;

		// return what we got
		lm_gammatable[i] = BYTE_CLAMP ((int) inf);
	}
}


cvar_t lm_gamma ("lm_gamma", "1", CVAR_ARCHIVE, D3DLight_BuildGammaTable);

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

int d3d_CoronaState = 0;

#define CORONA_ONLY				1
#define LIGHTMAP_ONLY			2
#define CORONA_PLUS_LIGHTMAP	3

void D3DLight_SetCoronaState (void)
{
	if (gl_flashblend.value)
	{
		// override everything else, if it's set then we're in coronas-only mode irrespective
		d3d_CoronaState = CORONA_ONLY;
		return;
	}

	// can't use .integer for DP-compatibility
	if (r_coronas.value)
		d3d_CoronaState = CORONA_PLUS_LIGHTMAP;
	else d3d_CoronaState = LIGHTMAP_ONLY;
}


typedef struct r_coronadlight_s
{
	dlight_t *dl;
	float radius;
	float color[3];
} r_coronadlight_t;


r_coronadlight_t corona_dlights[MAX_DLIGHTS];
int num_coronadlights;

void D3DLight_BeginCoronas (void)
{
	// fix up cvars
	if (r_coronaradius.integer < 0) Cvar_Set (&r_coronaradius, 0.0f);
	if (r_coronaintensity.value < 0) Cvar_Set (&r_coronaintensity, 0.0f);

	// nothing yet
	num_coronadlights = 0;
}


void D3DPart_BeginCoronas (void);
void D3DPart_DrawSingleCorona (float *origin, float *color, float radius);
void D3DPart_CommitCoronas (void);

void D3DLight_EndCoronas (void)
{
	if (num_coronadlights)
	{
		D3D_SetRenderState (D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
		D3D_SetRenderState (D3DRS_DESTBLEND, D3DBLEND_ONE);

		D3DPart_BeginCoronas ();

		for (int i = 0; i < num_coronadlights; i++)
		{
			r_coronadlight_t *cdl = &corona_dlights[i];
			dlight_t *dl = cdl->dl;

			D3DPart_DrawSingleCorona (dl->origin, cdl->color, cdl->radius);
		}

		// draw anything that needs to be drawn
		D3DPart_CommitCoronas ();

		// revert the blend mode change
		D3D_SetRenderState (D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
		D3D_SetRenderState (D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

		num_coronadlights = 0;
	}
}


void D3DLight_AddLightBlend (float r, float g, float b, float a2)
{
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

	blend[0] = blend[1] * (1 - a2) + r * a2;
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
	if (d3d_CoronaState == CORONA_PLUS_LIGHTMAP)
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
		if (d3d_CoronaState == CORONA_ONLY)
			D3DLight_AddLightBlend ((float) dl->rgb[0] / 512.0f, (float) dl->rgb[1] / 512.0f, (float) dl->rgb[2] / 512.0f, dl->radius * 0.0003);

		return;
	}

	colormul /= 255.0f;

	// store it for drawing later
	// we set this array to MAX_DLIGHTS so we don't need to bounds-check it ;)
	corona_dlights[num_coronadlights].dl = dl;
	corona_dlights[num_coronadlights].radius = rad;

	corona_dlights[num_coronadlights].color[0] = (float) dl->rgb[0] * r_coronaintensity.value * colormul;
	corona_dlights[num_coronadlights].color[1] = (float) dl->rgb[1] * r_coronaintensity.value * colormul;
	corona_dlights[num_coronadlights].color[2] = (float) dl->rgb[2] * r_coronaintensity.value * colormul;

	// go to the next light
	num_coronadlights++;
}


void D3DLight_AddCoronas (void)
{
	// add coronas
	if (d3d_CoronaState != LIGHTMAP_ONLY)
	{
		for (int lnum = 0; lnum < MAX_DLIGHTS; lnum++)
		{
			// light is dead
			if (cl_dlights[lnum].die < cl.time) continue;
			if (cl_dlights[lnum].radius < 0.0f) continue;
			if (cl_dlights[lnum].flags & DLF_NOCORONA) continue;

			float mins[3], maxs[3];

			for (int i = 0; i < 3; i++)
			{
				mins[i] = cl_dlights[lnum].origin[i] - cl_dlights[lnum].radius;
				maxs[i] = cl_dlights[lnum].origin[i] + cl_dlights[lnum].radius;
			}

			if (R_CullBox (mins, maxs, frustum)) continue;

			D3DAlpha_AddToList (&cl_dlights[lnum]);
		}
	}
}


/*
=============================================================================

DYNAMIC LIGHTS

=============================================================================
*/

cvar_t r_lerplightstyle ("r_lerplightstyle", "0", CVAR_ARCHIVE);
cvar_t r_coloredlight ("r_coloredlight", "1");
cvar_t r_overbright ("r_overbright", "1", CVAR_ARCHIVE);
cvar_t r_ambient ("r_ambient", "0");

cvar_alias_t gl_overbright ("gl_overbright", &r_overbright);

// size increased for new max extents
unsigned int *d3d_BlockLights;

// used by dlights, should move to mathlib
#pragma warning (disable:4035)
__declspec (naked) long Q_ftol (float f)
{
	static int tmp;
	__asm fld dword ptr [esp+4]
	__asm fistp tmp
	__asm mov eax, tmp
	__asm ret
}
#pragma warning (default:4035)

float d3d_DLightCutoff = 64.0f;


void D3DLight_DLightCutoffChange (cvar_t *var)
{
	if (var->value < 0)
	{
		d3d_DLightCutoff = 0;
		return;
	}
	else if (var->value > 2048)
	{
		d3d_DLightCutoff = 2048;
		return;
	}

	d3d_DLightCutoff = var->value;
}


cvar_t r_dlightcutoff ("r_dlightcutoff", "64", CVAR_ARCHIVE, D3DLight_DLightCutoffChange);

/*
===============
D3D_AddDynamicLights
===============
*/
bool D3D_AddDynamicLights (msurface_t *surf)
{
	int			lnum;
	int			sd, td;
	float		dist, rad, minlight;
	vec3_t		impact, local;
	int			s, t;
	float		ftacc, fsacc;
	int			i;
	mtexinfo_t	*tex = surf->texinfo;
	bool updated = false;

	if (!r_dynamic.value) return false;
	if (d3d_CoronaState == CORONA_ONLY) return false;

	for (lnum = 0; lnum < MAX_DLIGHTS; lnum++)
	{
		// light is dead
		if (cl_dlights[lnum].die < cl.time) continue;

		// not hit by this light
		if (!(surf->dlightbits[lnum >> 5] & (1 << (lnum & 31)))) continue;

		rad = cl_dlights[lnum].radius;
		dist = DotProduct (cl_dlights[lnum].origin, surf->plane->normal) - surf->plane->dist;
		rad -= fabs (dist);
		minlight = d3d_DLightCutoff;

		if (rad < minlight) continue;

		minlight = rad - minlight;

		impact[0] = cl_dlights[lnum].origin[0] - surf->plane->normal[0] * dist;
		impact[1] = cl_dlights[lnum].origin[1] - surf->plane->normal[1] * dist;
		impact[2] = cl_dlights[lnum].origin[2] - surf->plane->normal[2] * dist;

		local[0] = (DotProduct (impact, tex->vecs[0]) + tex->vecs[0][3]) - surf->texturemins[0];
		local[1] = (DotProduct (impact, tex->vecs[1]) + tex->vecs[1][3]) - surf->texturemins[1];

		// prevent this multiplication from having to happen for each point
		float dlrgb[] =
		{
			(float) cl_dlights[lnum].rgb[0] * r_dynamic.value,
			(float) cl_dlights[lnum].rgb[1] * r_dynamic.value,
			(float) cl_dlights[lnum].rgb[2] * r_dynamic.value
		};

		unsigned *bl = d3d_BlockLights;

		for (t = 0, ftacc = 0; t < surf->tmax; t++, ftacc += 16)
		{
			if ((td = local[1] - ftacc) < 0) td = -td;

			for (s = 0, fsacc = 0; s < surf->smax; s++, fsacc += 16, bl += 3)
			{
				if ((sd = Q_ftol (local[0] - fsacc)) < 0) sd = -sd;

				if (sd > td)
					dist = sd + (td >> 1);
				else dist = td + (sd >> 1);

				if (dist < minlight)
				{
					// swap to BGR
					bl[0] += (minlight - dist) * dlrgb[2];
					bl[1] += (minlight - dist) * dlrgb[1];
					bl[2] += (minlight - dist) * dlrgb[0];

					// the lightmap is updated now
					updated = true;
				}
			}
		}
	}

	return updated;
}


/*
==================
R_AnimateLight
==================
*/
int light_valuetable[256];

void R_SetDefaultLightStyles (void)
{
	// correct lightscaling so that 'm' is 256, not 264
	for (int i = 0; i < 256; i++)
	{
		// make it explicit that yeah, it's a *signed* char we want to use here
		// so that values > 127 will behave as expected
		float fv = (float) ((signed char) i - 'a') * 22;

		fv *= 256.0f;
		fv /= 264.0f;

		// in general a mod should never provide values for these that are outside of the 'a'..'z' range,
		// but the argument could be made that ID Quake/QC does/did nothing to prevent it, so it's legal
		if (fv < 0)
			light_valuetable[i] = (int) (fv - 0.5f);
		else light_valuetable[i] = (int) (fv + 0.5f);
	}

	// normal light value - making this consistent with a value of 'm' in R_AnimateLight
	// will prevent the upload of lightmaps when a surface is first seen
	for (int i = 0; i < 256; i++) d_lightstylevalue[i] = light_valuetable['m'];
}


void R_AnimateLight (float time)
{
	// made this cvar-controllable!
	if (r_lerplightstyle.value)
	{
		// interpolated light animations
		int			j, k;
		float		l;
		int			flight;
		int			clight;
		float		lerpfrac;
		float		backlerp;

		// light animations
		// 'm' is normal light, 'a' is no light, 'z' is double bright
		flight = (int) floor (time * 10.0f);
		clight = (int) ceil (time * 10.0f);
		lerpfrac = (time * 10.0f) - flight;
		backlerp = 1.0f - lerpfrac;

		for (j = 0; j < MAX_LIGHTSTYLES; j++)
		{
			if (!cl_lightstyle[j].length)
			{
				d_lightstylevalue[j] = light_valuetable['m'];
				continue;
			}
			else if (cl_lightstyle[j].length == 1)
			{
				// single length style so don't bother interpolating
				d_lightstylevalue[j] = light_valuetable[cl_lightstyle[j].map[0]];
				continue;
			}

			// interpolate animating light
			l = (float) light_valuetable[cl_lightstyle[j].map[flight % cl_lightstyle[j].length]] * backlerp;
			l += (float) light_valuetable[cl_lightstyle[j].map[clight % cl_lightstyle[j].length]] * lerpfrac;

			d_lightstylevalue[j] = (int) l;
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
				d_lightstylevalue[j] = light_valuetable['m'];
				continue;
			}
			else if (cl_lightstyle[j].length == 1)
			{
				// single length style so don't bother interpolating
				d_lightstylevalue[j] = light_valuetable[cl_lightstyle[j].map[0]];
				continue;
			}

			d_lightstylevalue[j] = light_valuetable[cl_lightstyle[j].map[i % cl_lightstyle[j].length]];
		}
	}
}


void R_ColourDLight (dlight_t *dl, unsigned short r, unsigned short g, unsigned short b)
{
	// leave dlight with white value it had at allocation
	if (!r_coloredlight.value) return;

	dl->rgb[0] = r;
	dl->rgb[1] = g;
	dl->rgb[2] = b;
}


/*
=============
R_MarkLights
=============
*/
void R_MarkLights (dlight_t *light, int num, mnode_t *node)
{
	float dist;
	float l, s, t;

	// hey!  no goto!!!
	while (1)
	{
		if (node->contents < 0) return;

		dist = SV_PlaneDist (node->plane, light->origin);

		if (dist > light->radius - d3d_DLightCutoff)
		{
			node = node->children[0];
			continue;
		}

		if (dist < -light->radius + d3d_DLightCutoff)
		{
			node = node->children[1];
			continue;
		}

		break;
	}

	// mark the polygons
	msurface_t *surf = node->surfaces;
	float maxdist = light->radius * light->radius;

	for (int i = 0; i < node->numsurfaces; i++, surf++)
	{
		// no lights on these
		if (surf->flags & SURF_DRAWTURB) continue;
		if (surf->flags & SURF_DRAWSKY) continue;

		float impact[3] =
		{
			light->origin[0] - surf->plane->normal[0] * dist,
			light->origin[1] - surf->plane->normal[1] * dist,
			light->origin[2] - surf->plane->normal[2] * dist
		};

		// clamp center of light to corner and check brightness
		l = DotProduct (impact, surf->texinfo->vecs[0]) + surf->texinfo->vecs[0][3] - surf->texturemins[0];
		s = l + 0.5; if (s < 0) s = 0; else if (s > surf->extents[0]) s = surf->extents[0];
		s = l - s;

		l = DotProduct (impact, surf->texinfo->vecs[1]) + surf->texinfo->vecs[1][3] - surf->texturemins[1];
		t = l + 0.5; if (t < 0) t = 0; else if (t > surf->extents[1]) t = surf->extents[1];
		t = l - t;

		if ((s * s + t * t + dist * dist) < maxdist)
		{
			if (surf->dlightframe != d3d_RenderDef.framecount)
			{
				// first time hit
				surf->dlightbits[0] = surf->dlightbits[1] = surf->dlightbits[2] = surf->dlightbits[3] = 0;
				surf->dlightframe = d3d_RenderDef.framecount;
			}

			// mark the surf for this dlight
			surf->dlightbits[num >> 5] |= 1 << (num & 31);
		}
	}

	if (node->children[0]->contents >= 0) R_MarkLights (light, num, node->children[0]);
	if (node->children[1]->contents >= 0) R_MarkLights (light, num, node->children[1]);
}


/*
=============
R_PushDlights
=============
*/
void R_PushDlights (mnode_t *headnode)
{
	if (!r_dynamic.value) return;
	if (d3d_CoronaState == CORONA_ONLY) return;

	dlight_t *l = cl_dlights;

	for (int i = 0; i < MAX_DLIGHTS; i++, l++)
	{
		if (l->die < cl.time || !l->radius)
			continue;

		sv_frame--;
		R_MarkLights (l, i, headnode);
	}
}


/*
=============================================================================

LIGHT SAMPLING

=============================================================================
*/

mplane_t		*lightplane;
vec3_t			lightspot;

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
		VectorCopy (mid, lightspot);
		lightplane = node->plane;

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

			if (surf->samples)
			{
				// LordHavoc: enhanced to interpolate lighting
				byte *lightmap;
				int maps,
					dsfrac = ds & 15,
					dtfrac = dt & 15,
					r00 = 0, g00 = 0, b00 = 0,
					r01 = 0, g01 = 0, b01 = 0,
					r10 = 0, g10 = 0, b10 = 0,
					r11 = 0, g11 = 0, b11 = 0;
				float scale;
				int line3 = ((surf->extents[0] >> 4) + 1) * 3;

				// LordHavoc: *3 for color
				lightmap = surf->samples + ((dt >> 4) * ((surf->extents[0] >> 4) + 1) + (ds >> 4)) * 3;

				for (maps = 0; maps < MAXLIGHTMAPS && surf->styles[maps] != 255; maps++)
				{
					// keep this consistent with BSP lighting
					scale = (float) d_lightstylevalue[surf->styles[maps]];

					r00 += (float) lightmap[0] * scale;
					g00 += (float) lightmap[1] * scale;
					b00 += (float) lightmap[2] * scale;

					r01 += (float) lightmap[3] * scale;
					g01 += (float) lightmap[4] * scale;
					b01 += (float) lightmap[5] * scale;

					r10 += (float) lightmap[line3 + 0] * scale;
					g10 += (float) lightmap[line3 + 1] * scale;
					b10 += (float) lightmap[line3 + 2] * scale;

					r11 += (float) lightmap[line3 + 3] * scale;
					g11 += (float) lightmap[line3 + 4] * scale;
					b11 += (float) lightmap[line3 + 5] * scale;

					// LordHavoc: *3 for colored lighting
					lightmap += ((surf->extents[0] >> 4) + 1) * ((surf->extents[1] >> 4) + 1) * 3;
				}

				// somebody's just taking the piss here...
				// (lightmap is BGR so swap back to RGB)
				color[2] += (float) ((int) ((((((((r11 - r10) * dsfrac) >> 4) + r10) - ((((r01 - r00) * dsfrac) >> 4) + r00)) * dtfrac) >> 4) + ((((r01 - r00) * dsfrac) >> 4) + r00)));
				color[1] += (float) ((int) ((((((((g11 - g10) * dsfrac) >> 4) + g10) - ((((g01 - g00) * dsfrac) >> 4) + g00)) * dtfrac) >> 4) + ((((g01 - g00) * dsfrac) >> 4) + g00)));
				color[0] += (float) ((int) ((((((((b11 - b10) * dsfrac) >> 4) + b10) - ((((b01 - b00) * dsfrac) >> 4) + b00)) * dtfrac) >> 4) + ((((b01 - b00) * dsfrac) >> 4) + b00)));
			}

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


void D3DLight_LightPoint (entity_t *e, float *c)
{
	float add;
	vec3_t dist;
	vec3_t start;
	vec3_t end;

	// using bbox center point can give black models as it's now adjusted for the 
	// correct frame bbox, so just revert back to good ol' origin
	VectorCopy (e->origin, start);

	// set end point (back to 2048 for less BSP tree tracing)
	end[0] = start[0];
	end[1] = start[1];
	end[2] = start[2] - 2048;

	lightplane = NULL;

	// keep MDL lighting consistent with the world
	if (r_fullbright.integer || !cl.worldmodel->brushhdr->lightdata)
		c[0] = c[1] = c[2] = 255 * (256 >> r_overbright.integer);
	else
	{
		// clear to ambient
		if (cl.maxclients > 1 || r_ambient.integer < 1)
			c[0] = c[1] = c[2] = 0;
		else c[0] = c[1] = c[2] = r_ambient.integer * (256 >> r_overbright.integer);

		// add dynamic lights first (consistency with the world)
		if (r_dynamic.value && (d3d_CoronaState != CORONA_ONLY))
		{
			for (int lnum = 0; lnum < MAX_DLIGHTS; lnum++)
			{
				if (cl_dlights[lnum].die >= cl.time)
				{
					VectorSubtract (e->origin, cl_dlights[lnum].origin, dist);

					float frad = cl_dlights[lnum].radius;
					float fdist = fabs (Length (dist));
					float fminlight = d3d_DLightCutoff;

					if (frad < fminlight) continue;

					fminlight = frad - fminlight;

					if (fdist < fminlight)
					{
						c[0] += (fminlight - fdist) * cl_dlights[lnum].rgb[0] * r_dynamic.value;
						c[1] += (fminlight - fdist) * cl_dlights[lnum].rgb[1] * r_dynamic.value;
						c[2] += (fminlight - fdist) * cl_dlights[lnum].rgb[2] * r_dynamic.value;
					}
				}
			}
		}

		// add lighting from lightmaps
		R_RecursiveLightPoint (c, cl.worldmodel->brushhdr->nodes, start, end);
	}

	// keep MDL lighting consistent with the world
	for (int i = 0; i < 3; i++)
	{
		if (kurok)
		{
			int rgb = ((int) (c[i] + 0.5f)) >> (6 + r_overbright.integer);
			c[i] = (float) lm_gammatable[BYTE_CLAMP (rgb)];
		}
		else
		{
			int rgb = ((int) (c[i] + 0.5f)) >> (7 + r_overbright.integer);
			c[i] = (float) lm_gammatable[BYTE_CLAMP (rgb)];
		}
	}

	// set minimum light values
	if (e == &cl.viewent) R_MinimumLight (c, 72);
	if (e->entnum >= 1 && e->entnum <= cl.maxclients) R_MinimumLight (c, 24);
	if (e->model->flags & EF_ROTATE) R_MinimumLight (c, 72);
	if (e->model->type == mod_brush) R_MinimumLight (c, 18);
}


/*
====================================================================================================================

		LIGHTMAP ALLOCATION AND UPDATING

====================================================================================================================
*/

__inline void D3DLight_ClearToAmbient (unsigned *bl, int size, int amb)
{
	int s = size * 3;
	int n = (s + 7) >> 3;

	switch (s % 8)
	{
	case 0: do {*bl++ = amb;
	case 7: *bl++ = amb;
	case 6: *bl++ = amb;
	case 5: *bl++ = amb;
	case 4: *bl++ = amb;
	case 3: *bl++ = amb;
	case 2: *bl++ = amb;
	case 1: *bl++ = amb;
	} while (--n > 0);
	}
}


#define DUFFLIGHT_NOGAMMA \
	t = *bl++ >> shift; *dest++ = BYTE_CLAMP (t); \
	t = *bl++ >> shift; *dest++ = BYTE_CLAMP (t); \
	t = *bl++ >> shift; *dest++ = BYTE_CLAMP (t); \
	*dest++ = 255;

#define DUFFLIGHT_WITHGAMMA \
	t = *bl++ >> shift; *dest++ = lm_gammatable[BYTE_CLAMP (t)]; \
	t = *bl++ >> shift; *dest++ = lm_gammatable[BYTE_CLAMP (t)]; \
	t = *bl++ >> shift; *dest++ = lm_gammatable[BYTE_CLAMP (t)]; \
	*dest++ = 255;

void D3DLight_BuildLightmap (msurface_t *surf, int texnum)
{
	// cache these at the levels they were created or updated at
	surf->overbright = r_overbright.integer;
	surf->fullbright = r_fullbright.integer;
	surf->ambient = r_ambient.integer;
	surf->lm_gamma = lm_gamma.value;

	int size = surf->smax * surf->tmax;
	byte *lightmap = surf->samples;
	unsigned *bl = d3d_BlockLights = (unsigned *) scratchbuf;

	// by default the lightmap is not updated
	bool updated = false;

	// set to full bright if no light data (cl.worldmodel hasn't yet been set so we need to track it this way)
	if (r_fullbright.integer || !surf->model->brushhdr->lightdata)
	{
		D3DLight_ClearToAmbient (d3d_BlockLights, size, 255 * (256 >> r_overbright.integer));
		updated = true;
	}
	else
	{
		// clear to ambient
		if (cl.maxclients > 1 || r_ambient.integer < 1)
			D3DLight_ClearToAmbient (d3d_BlockLights, size, 0);
		else D3DLight_ClearToAmbient (d3d_BlockLights, size, r_ambient.integer * (256 >> r_overbright.integer));

		// if we had a cached dynamic light we must force an update to clear the light
		if (surf->cached_dlight) updated = true;

		// add all the dynamic lights first
		if (surf->dlightframe == d3d_RenderDef.framecount)
		{
			// ensure that it actually did get an update
			if (D3D_AddDynamicLights (surf))
			{
				surf->cached_dlight = true;
				updated = true;
			}
			else surf->cached_dlight = false;
		}
		else surf->cached_dlight = false;

		// add all the lightmaps
		if (lightmap)
		{
			// can we not do this better?  like store 4 lightmaps per surf, send the scales as
			// uniforms and do the update shader-side?  what about dynamics then?  attenuation maps?
			for (int maps = 0; maps < MAXLIGHTMAPS && surf->styles[maps] != 255; maps++)
			{
				unsigned int scale = d_lightstylevalue[surf->styles[maps]];
				int n = ((size * 3) + 7) >> 3;

				bl = d3d_BlockLights;

				switch ((size * 3) % 8)
				{
				case 0: do {*bl++ += scale * *lightmap++;
				case 7: *bl++ += scale * *lightmap++;
				case 6: *bl++ += scale * *lightmap++;
				case 5: *bl++ += scale * *lightmap++;
				case 4: *bl++ += scale * *lightmap++;
				case 3: *bl++ += scale * *lightmap++;
				case 2: *bl++ += scale * *lightmap++;
				case 1: *bl++ += scale * *lightmap++;
				} while (--n > 0);
				}

				if (surf->cached_light[maps] != scale)
				{
					surf->cached_light[maps] = scale;
					updated = true;
				}
			}
		}
	}

	// if the light wasn't updated then don't update the texture
	if (!updated) return;

	if (!d3d_Lightmaps[texnum].Data)
	{
		D3DLOCKED_RECT lockrect;

		hr = d3d_Lightmaps[texnum].Texture->LockRect (0, &lockrect, NULL, D3DLOCK_NO_DIRTY_UPDATE);

		// if we didn't get a lock we just don't bother updating it
		if (FAILED (hr)) return;

		d3d_Lightmaps[texnum].Data = (byte *) lockrect.pBits;
	}

	int t;
	int shift = 7 + r_overbright.integer;
	int stride = (LightmapWidth << 2) - (surf->smax << 2);
	byte *dest = d3d_Lightmaps[texnum].Data + surf->LightBase;

	// slightly evil this...
	if (kurok)
		shift = 6 + r_overbright.integer;
	else shift = 7 + r_overbright.integer;

	bl = d3d_BlockLights;

	if (lm_gamma.value == 1)
	{
		for (int i = 0; i < surf->tmax; i++, dest += stride)
		{
			int n = (surf->smax + 7) >> 3;

			switch (surf->smax % 8)
			{
			case 0: do {DUFFLIGHT_NOGAMMA;
			case 7: DUFFLIGHT_NOGAMMA;
			case 6: DUFFLIGHT_NOGAMMA;
			case 5: DUFFLIGHT_NOGAMMA;
			case 4: DUFFLIGHT_NOGAMMA;
			case 3: DUFFLIGHT_NOGAMMA;
			case 2: DUFFLIGHT_NOGAMMA;
			case 1: DUFFLIGHT_NOGAMMA;
			} while (--n > 0);
			}
		}
	}
	else
	{
		for (int i = 0; i < surf->tmax; i++, dest += stride)
		{
			int n = (surf->smax + 7) >> 3;

			switch (surf->smax % 8)
			{
			case 0: do {DUFFLIGHT_WITHGAMMA;
			case 7: DUFFLIGHT_WITHGAMMA;
			case 6: DUFFLIGHT_WITHGAMMA;
			case 5: DUFFLIGHT_WITHGAMMA;
			case 4: DUFFLIGHT_WITHGAMMA;
			case 3: DUFFLIGHT_WITHGAMMA;
			case 2: DUFFLIGHT_WITHGAMMA;
			case 1: DUFFLIGHT_WITHGAMMA;
			} while (--n > 0);
			}
		}
	}

	RECT *DirtyRect = &d3d_Lightmaps[texnum].DirtyRect;
	RECT *LightRect = &surf->LightRect;

	// expand the dirty region to include this surf
	if (LightRect->left < DirtyRect->left) DirtyRect->left = LightRect->left;
	if (LightRect->right > DirtyRect->right) DirtyRect->right = LightRect->right;
	if (LightRect->top < DirtyRect->top) DirtyRect->top = LightRect->top;
	if (LightRect->bottom > DirtyRect->bottom) DirtyRect->bottom = LightRect->bottom;
}


void D3DLight_CreateSurfaceLightmap (msurface_t *surf)
{
	if (surf->flags & SURF_DRAWSKY) return;
	if (surf->flags & SURF_DRAWTURB) return;

	// store these out st that we don't have to recalculate them every time
	surf->smax = (surf->extents[0] >> 4) + 1;
	surf->tmax = (surf->extents[1] >> 4) + 1;

	for (int texnum = 0; texnum < MAX_LIGHTMAPS; texnum++)
	{
		// it's unsafe to use the scratch buffer for this because it happens during map loading and other loads
		// may also want to use it for their own stuff; we'll just put it on the zone instead
		if (!d3d_Lightmaps[texnum].Allocated)
			d3d_Lightmaps[texnum].Allocated = (unsigned short *) MainZone->Alloc (LightmapWidth * sizeof (unsigned short));

		int best = LightmapHeight;

		for (int i = 0; i < LightmapWidth - surf->smax; i++)
		{
			int j;
			int best2 = 0;

			for (j = 0; j < surf->smax; j++)
			{
				if (d3d_Lightmaps[texnum].Allocated[i + j] >= best) break;
				if (d3d_Lightmaps[texnum].Allocated[i + j] > best2) best2 = d3d_Lightmaps[texnum].Allocated[i + j];
			}

			if (j == surf->smax)
			{
				// this is a valid spot
				surf->LightRect.left = i;
				surf->LightRect.top = best = best2;
			}
		}

		if (best + surf->tmax > LightmapHeight)
			continue;

		// fill in lightmap right and bottom (these exist just because I'm lazy and don't want to add a few numbers during updates)
		surf->LightRect.right = surf->LightRect.left + surf->smax;
		surf->LightRect.bottom = surf->LightRect.top + surf->tmax;
		surf->LightmapOffset = ((surf->LightRect.top * LightmapWidth + surf->LightRect.left) * 4);

		// mark allocated regions in this surf
		for (int i = 0; i < surf->smax; i++)
			d3d_Lightmaps[texnum].Allocated[surf->LightRect.left + i] = best + surf->tmax;

		// reuse any lightmaps which were previously allocated
		if (!d3d_Lightmaps[texnum].Texture)
		{
			hr = d3d_Device->CreateTexture
			(
				LightmapWidth,
				LightmapHeight,
				1,
				0,
				D3DFMT_X8R8G8B8,
				D3DPOOL_MANAGED,
				&d3d_Lightmaps[texnum].Texture,
				NULL
			);

			// oh shit
			if (FAILED (hr)) Sys_Error ("D3DLight_CreateSurfaceLightmap : failed to create a texture in D3DPOOL_MANAGED");
		}

		// we can't create lightmap texcoords until the positioning of the surf within the
		// lightmap texture is known so it's deferred until here
		// note: _controlfp is ineffective here
		for (int i = 0; i < surf->numverts; i++)
		{
			brushpolyvert_t *vert = &surf->verts[i];

			vert->st[1][0] = DotProduct (vert->xyz, surf->texinfo->vecs[0]) + surf->texinfo->vecs[0][3];
			vert->st[1][0] -= surf->texturemins[0];
			vert->st[1][0] += (int) surf->LightRect.left * 16;
			vert->st[1][0] += 8;
			vert->st[1][0] /= (float) (LightmapWidth * 16);

			vert->st[1][1] = DotProduct (vert->xyz, surf->texinfo->vecs[1]) + surf->texinfo->vecs[1][3];
			vert->st[1][1] -= surf->texturemins[1];
			vert->st[1][1] += (int) surf->LightRect.top * 16;
			vert->st[1][1] += 8;
			vert->st[1][1] /= (float) (LightmapHeight * 16);
		}

		// ensure no dlight update happens and rebuild the lightmap fully
		surf->dlightframe = -1;

		// initially assign these
		surf->LightmapTextureNum = texnum;
		surf->d3d_LightmapTex = d3d_Lightmaps[surf->LightmapTextureNum].Texture;

		// offset of the data for this surf in the texture
		surf->LightBase = (surf->LightRect.top * LightmapWidth + surf->LightRect.left) * 4;

		// (hack) invalidate the cache to force an update
		surf->cached_dlight = true;

		// and build the map
		D3DLight_BuildLightmap (surf, texnum);

		// this is a valid area
		return;
	}

	// failed to allocate
	Sys_Error ("D3DLight_CreateSurfaceLightmap : failed to allocate a lightmap for surface");
}


void D3DLight_BumpLightmaps (void)
{
	// prevent allocations in preceding lightmaps
	for (int i = 0; i < MAX_LIGHTMAPS; i++)
	{
		if (!d3d_Lightmaps[i].Allocated) continue;

		for (int j = 0; j < LightmapWidth; j++)
			d3d_Lightmaps[i].Allocated[j] = 65535;
	}
}


void D3DLight_CreateSurfaceLightmaps (model_t *mod)
{
	brushhdr_t *hdr = mod->brushhdr;

	// fixme - ensure that all surfs with the same texture have the same lightmap too; doing something with the chains
	// in d3d_surf???
	for (int i = 0; i < hdr->numtextures; i++)
	{
		if (!hdr->textures[i]) continue;
		hdr->textures[i]->chain = NULL;
	}

	for (int i = 0; i < hdr->numsurfaces; i++)
	{
		hdr->surfaces[i].LightmapTextureNum = -1;

		if (hdr->surfaces[i].flags & SURF_DRAWSKY) continue;
		if (hdr->surfaces[i].flags & SURF_DRAWTURB) continue;

		hdr->surfaces[i].chain = hdr->surfaces[i].texinfo->texture->chain;
		hdr->surfaces[i].texinfo->texture->chain = &hdr->surfaces[i];
	}

	for (int i = 0; i < hdr->numtextures; i++)
	{
		if (!hdr->textures[i]) continue;

		for (msurface_t *surf = hdr->textures[i]->chain; surf; surf = surf->chain)
			D3DLight_CreateSurfaceLightmap (surf);

		hdr->textures[i]->chain = NULL;
	}

	for (int i = 0; i < hdr->numsurfaces; i++)
	{
		if (hdr->surfaces[i].flags & SURF_DRAWSKY) continue;
		if (hdr->surfaces[i].flags & SURF_DRAWTURB) continue;

		if (hdr->surfaces[i].LightmapTextureNum == -1)
			D3DLight_CreateSurfaceLightmap (&hdr->surfaces[i]);
	}
}


void D3DLight_BeginBuildingLightmaps (void)
{
	// build the default lightmaps gamma table (ensures that we have valid values in lm_gammatable even
	// if the cvar is not changed - which will be the case on initial map load)
	D3DLight_BuildGammaTable (&lm_gamma);

	// set up the default styles so that (most) lightmaps won't have to be regenerated the first time they're seen
	R_SetDefaultLightStyles ();

	if (LightmapWidth < 0 || LightmapHeight < 0)
	{
		LightmapWidth = LIGHTMAP_WIDTH;
		LightmapHeight = LIGHTMAP_HEIGHT;

		// bound lightmap textures to max supported by the device
		if (LightmapWidth > d3d_DeviceCaps.MaxTextureWidth) LightmapWidth = d3d_DeviceCaps.MaxTextureWidth;
		if (LightmapHeight > d3d_DeviceCaps.MaxTextureHeight) LightmapHeight = d3d_DeviceCaps.MaxTextureHeight;
	}

	// eval max allowable extents
	d3d_GlobalCaps.MaxExtents = d3d_DeviceCaps.MaxTextureWidth;

	if (d3d_DeviceCaps.MaxTextureHeight < d3d_GlobalCaps.MaxExtents) d3d_GlobalCaps.MaxExtents = d3d_DeviceCaps.MaxTextureHeight;
	if (LightmapWidth < d3d_GlobalCaps.MaxExtents) d3d_GlobalCaps.MaxExtents = LightmapWidth;
	if (LightmapHeight < d3d_GlobalCaps.MaxExtents) d3d_GlobalCaps.MaxExtents = LightmapHeight;

	d3d_GlobalCaps.MaxExtents <<= 4;
	d3d_GlobalCaps.MaxExtents -= 16;

	Con_DPrintf ("max extents: %i\n", d3d_GlobalCaps.MaxExtents);

	// unlock all lightmap textures that may be locked
	for (int i = 0; i < MAX_LIGHTMAPS; i++)
	{
		// ensure that the lightmap is unlocked
		if (d3d_Lightmaps[i].Data)
		{
			d3d_Lightmaps[i].Texture->UnlockRect (0);
			d3d_Lightmaps[i].Data = NULL;
		}

		// clear down allocation buffers
		if (d3d_Lightmaps[i].Allocated)
		{
			MainZone->Free (d3d_Lightmaps[i].Allocated);
			d3d_Lightmaps[i].Allocated = NULL;
		}
	}
}


void D3DLight_EndBuildingLightmaps (void)
{
	// preload everything to prevent runtime stalls
	for (int i = 0; i < MAX_LIGHTMAPS; i++)
	{
		// clear down allocation buffers
		if (d3d_Lightmaps[i].Allocated)
		{
			MainZone->Free (d3d_Lightmaps[i].Allocated);
			d3d_Lightmaps[i].Allocated = NULL;
		}

		// update any lightmaps which were created and release any left over which were unused
		if (d3d_Lightmaps[i].Data)
		{
			// dirty the entire texture rectangle
			d3d_Lightmaps[i].Texture->UnlockRect (0);
			d3d_Lightmaps[i].Texture->AddDirtyRect (NULL);
			d3d_Lightmaps[i].Texture->PreLoad ();
			d3d_Lightmaps[i].Data = NULL;
		}
		else
		{
			// release any lightmaps which were unused
			SAFE_RELEASE (d3d_Lightmaps[i].Texture);
		}

		// clear all dirty rects
		d3d_Lightmaps[i].DirtyRect.left = LightmapWidth;
		d3d_Lightmaps[i].DirtyRect.right = 0;
		d3d_Lightmaps[i].DirtyRect.top = LightmapHeight;
		d3d_Lightmaps[i].DirtyRect.bottom = 0;
	}
}


void D3DLight_CheckSurfaceForModification (msurface_t *surf)
{
	// no lightmaps
	if (surf->flags & SURF_DRAWSKY) return;
	if (surf->flags & SURF_DRAWTURB) return;

	// hack the cached dynamic light to force an update on a properties change
	bool oldcache = surf->cached_dlight;
	surf->cached_dlight = true;

	// check for overbright or fullbright modification
	// these always override values of r_dynamic or light cache so that we ensure everything is updated
	// if they ever change (should some of this be just encoded in d_lightstylevalue so that it will happen automatically???)
	if (surf->overbright != r_overbright.integer) goto ModifyLightmap;
	if (surf->fullbright != r_fullbright.integer) goto ModifyLightmap;
	if (surf->ambient != r_ambient.integer) goto ModifyLightmap;
	if (surf->lm_gamma != lm_gamma.value) goto ModifyLightmap;

	// now restore the original cached dlight
	surf->cached_dlight = oldcache;

	// no lightmap modifications
	if (!r_dynamic.value) return;

	// disable dynamic lights on this surf in coronas-only mode
	if (d3d_CoronaState == CORONA_ONLY)
	{
		surf->dlightframe = ~d3d_RenderDef.framecount;
		surf->cached_dlight = false;
	}

	// cached lightstyle change
	for (int maps = 0; maps < MAXLIGHTMAPS && surf->styles[maps] != 255; maps++)
	{
		if (d_lightstylevalue[surf->styles[maps]] != surf->cached_light[maps])
		{
			// Con_Printf ("Modified light from %i to %i\n", surf->cached_light[maps], d_lightstylevalue[surf->styles[maps]]);
			goto ModifyLightmap;
		}
	}

	// dynamic this frame || dynamic previous frame
	if (surf->dlightframe == d3d_RenderDef.framecount || surf->cached_dlight)
		goto ModifyLightmap;

	// no changes
	return;

ModifyLightmap:;
	// rebuild the lightmap
	D3DLight_BuildLightmap (surf, surf->LightmapTextureNum);
}


void D3DLight_UpdateLightmaps (void)
{
	if (!d3d_Device) return;

	for (int i = 0; i < MAX_LIGHTMAPS; i++)
	{
		if (!d3d_Lightmaps[i].Texture) continue;

		// clear down allocation buffers
		if (d3d_Lightmaps[i].Allocated)
		{
			MainZone->Free (d3d_Lightmaps[i].Allocated);
			d3d_Lightmaps[i].Allocated = NULL;
		}

		// update any lightmaps which were modified
		if (d3d_Lightmaps[i].Data)
		{
			RECT *DirtyRect = &d3d_Lightmaps[i].DirtyRect;

			d3d_Lightmaps[i].Texture->UnlockRect (0);
			d3d_Lightmaps[i].Texture->AddDirtyRect (DirtyRect);

			DirtyRect->left = LightmapWidth;
			DirtyRect->right = 0;
			DirtyRect->top = LightmapHeight;
			DirtyRect->bottom = 0;

			d3d_Lightmaps[i].Data = NULL;
		}
	}
}


void D3DLight_ReleaseLightmaps (void)
{
	for (int i = 0; i < MAX_LIGHTMAPS; i++)
	{
		// clear down allocation buffers
		if (d3d_Lightmaps[i].Allocated)
		{
			MainZone->Free (d3d_Lightmaps[i].Allocated);
			d3d_Lightmaps[i].Allocated = NULL;
		}

		// ensure that the lightmap is unlocked
		if (d3d_Lightmaps[i].Data)
		{
			d3d_Lightmaps[i].Texture->UnlockRect (0);
			d3d_Lightmaps[i].Data = NULL;
		}

		SAFE_RELEASE (d3d_Lightmaps[i].Texture);
	}
}


