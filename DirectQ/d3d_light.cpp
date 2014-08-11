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

// all lighting is kept at RGB throughout the pipeline (for simplicity) - even though we're using a BGR texture format - and
// is then swapped back to BGR during the final texture read in the shader.

#include "quakedef.h"
#include "d3d_model.h"
#include "d3d_quake.h"

cvar_t r_overbright ("r_overbright", 1.0f, CVAR_ARCHIVE);
cvar_t r_hdrlight ("r_hdrlight", 1.0f, CVAR_ARCHIVE);
cvar_alias_t gl_overbright ("gl_overbright", &r_overbright);
cvar_t gl_overbright_models ("gl_overbright_models", 1.0f, CVAR_ARCHIVE);

#define LIGHTMAP_WIDTH		512
#define LIGHTMAP_HEIGHT		512

int LightmapWidth = -1;
int LightmapHeight = -1;

typedef struct d3d_lightmap_s
{
	LPDIRECT3DTEXTURE9 SysTexture;
	LPDIRECT3DTEXTURE9 DefTexture;
	bool Modified;
} d3d_lightmap_t;

d3d_lightmap_t d3d_Lightmaps[MAX_LIGHTMAPS];
int d3d_LightHunkMark = 0;

unsigned short *d3d_LMAllocated[MAX_LIGHTMAPS];

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
		// switch the blendfunc for coronas
		D3DState_SetAlphaBlend (TRUE, D3DBLENDOP_ADD, D3DBLEND_SRCALPHA, D3DBLEND_ONE);
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
		D3DState_SetAlphaBlend (TRUE);

		num_coronadlights = 0;
	}
}

cvar_t v_dlightcshift ("v_dlightcshift", 1.0f);

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
cvar_t r_coloredlight ("r_coloredlight", "1", CVAR_ARCHIVE);
cvar_t r_ambient ("r_ambient", "0");

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

		unsigned *blocklights = (unsigned *) scratchbuf;
		int ladd = 0;

		for (t = 0, ftacc = 0; t < surf->tmax; t++, ftacc += 16)
		{
			if ((td = (int) (local[1] - ftacc)) < 0) td = -td;

			for (s = 0, fsacc = 0; s < surf->smax; s++, fsacc += 16, blocklights += 3)
			{
				if ((sd = Q_ftol (local[0] - fsacc)) < 0) sd = -sd;

				if (sd > td)
					dist = sd + (td >> 1);
				else dist = td + (sd >> 1);

				ladd = (minlight - dist);

				if (ladd > 0)
				{
					blocklights[0] += ladd * dlrgb[0];
					blocklights[1] += ladd * dlrgb[1];
					blocklights[2] += ladd * dlrgb[2];

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
	for (int i = 0; i < 256; i++)
		light_valuetable[i] = ((((signed char) i - 'a') * 22) << 8) / 264;

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
	dl->rgb[0] = r;
	dl->rgb[1] = g;
	dl->rgb[2] = b;
}


/*
=============
R_MarkLights
=============
*/
void R_MakeDLightVector (msurface_t *surf, float *lvec, int ndx, float *impact)
{
	float l = DotProduct (impact, surf->texinfo->vecs[ndx]) + surf->texinfo->vecs[ndx][3] - surf->texturemins[ndx];

	lvec[ndx] = l + 0.5;
	
	if (lvec[ndx] < 0)
		lvec[ndx] = 0;
	else if (lvec[ndx] > surf->extents[ndx])
		lvec[ndx] = surf->extents[ndx];

	lvec[ndx] = l - lvec[ndx];
}


void R_MarkLights (dlight_t *light, int num, mnode_t *node)
{
	float dist;

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

		float lvec[3];

		// clamp center of light to corner and check brightness
		R_MakeDLightVector (surf, lvec, 0, impact);
		R_MakeDLightVector (surf, lvec, 1, impact);

		lvec[2] = dist;

		if (DotProduct (lvec, lvec) < maxdist)
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

msurface_t		*lightsurf;
mplane_t		*lightplane;
vec3_t			lightspot;

void D3DLight_FromSurface (msurface_t *surf, float *color, int ds, int dt)
{
	if (!surf->samples[0]) return;

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
	lightmap = surf->samples[0] + ((dt >> 4) * ((surf->extents[0] >> 4) + 1) + (ds >> 4)) * 3;

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
	color[0] += (float) ((int) ((((((((r11 - r10) * dsfrac) >> 4) + r10) - ((((r01 - r00) * dsfrac) >> 4) + r00)) * dtfrac) >> 4) + ((((r01 - r00) * dsfrac) >> 4) + r00)));
	color[1] += (float) ((int) ((((((((g11 - g10) * dsfrac) >> 4) + g10) - ((((g01 - g00) * dsfrac) >> 4) + g00)) * dtfrac) >> 4) + ((((g01 - g00) * dsfrac) >> 4) + g00)));
	color[2] += (float) ((int) ((((((((b11 - b10) * dsfrac) >> 4) + b10) - ((((b01 - b00) * dsfrac) >> 4) + b00)) * dtfrac) >> 4) + ((((b01 - b00) * dsfrac) >> 4) + b00)));
}


void D3DLight_StaticEnt (entity_t *e, msurface_t *surf, float *color)
{
	int ds = (int) ((float) DotProduct (e->lightspot, surf->texinfo->vecs[0]) + surf->texinfo->vecs[0][3]);
	int dt = (int) ((float) DotProduct (e->lightspot, surf->texinfo->vecs[1]) + surf->texinfo->vecs[1][3]);

	ds -= surf->texturemins[0];
	dt -= surf->texturemins[1];

	lightsurf = e->lightsurf;
	VectorCopy2 (lightspot, e->lightspot);

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

			D3DLight_FromSurface (surf, color, ds, dt);

			// store out the surf that was hit
			lightsurf = surf;

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
	lightsurf = NULL;

	// keep MDL lighting consistent with the world
	if (r_fullbright.integer || !cl.worldmodel->brushhdr->lightdata)
		c[0] = c[1] = c[2] = 32767;
	else
	{
		// clear to ambient
		if (cl.maxclients > 1 || r_ambient.integer < 1)
			c[0] = c[1] = c[2] = 0;
		else c[0] = c[1] = c[2] = r_ambient.integer * 128;

		// add lighting from lightmaps
		if (e->isStatic && e->lightsurf)
			D3DLight_StaticEnt (e, e->lightsurf, c);
		else if (!e->isStatic)
			R_RecursiveLightPoint (c, cl.worldmodel->brushhdr->nodes, start, end);

		// add dynamic lights
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
						c[0] += (fminlight - fdist) * cl_dlights[lnum].rgb[0] * r_dynamic.value * 0.5f;
						c[1] += (fminlight - fdist) * cl_dlights[lnum].rgb[1] * r_dynamic.value * 0.5f;
						c[2] += (fminlight - fdist) * cl_dlights[lnum].rgb[2] * r_dynamic.value * 0.5f;
					}
				}
			}
		}
	}

	// scale back to standard range
	VectorScale (c, (1.0f / 255.0f), c);

	// set minimum light values
	if (e == &cl.viewent) R_MinimumLight (c, 72);
	if (e->entnum >= 1 && e->entnum <= cl.maxclients) R_MinimumLight (c, 24);
	if (e->model->flags & EF_ROTATE) R_MinimumLight (c, 72);

	if (!r_overbright.integer)
	{
		for (int i = 0; i < 3; i++)
		{
			c[i] *= 2.0f;
			if (c[i] > 255.0f) c[i] = 255.0f;
			c[i] *= 0.5f;
		}
	}

	if (!r_coloredlight.integer)
	{
		float white[3] = {0.3f, 0.59f, 0.11f};
		c[0] = c[1] = c[2] = DotProduct (c, white);
	}
}


void D3DLight_PrepStaticEntityLighting (entity_t *ent)
{
	// throw away returned light
	float discard[3];

	// this needs to run first otherwise the ent won't be lit!!!
	D3DLight_LightPoint (ent, discard);

	// mark as static and save out the results
	ent->isStatic = true;
	ent->lightsurf = lightsurf;
	VectorCopy2 (ent->lightspot, lightspot);
}


/*
====================================================================================================================

		LIGHTMAP ALLOCATION AND UPDATING

====================================================================================================================
*/

int d3d_LightProperty = 0;

void D3DLight_BoundPropertyVar (cvar_t *var, int minvar, int maxvar)
{
	if (var->integer < minvar) Cvar_Set (var, minvar);
	if (var->integer > maxvar) Cvar_Set (var, maxvar);
}


void D3DLight_SetProperty (void)
{
	// pack the properties into a single int so that we can more cleanly check and set them
	D3DLight_BoundPropertyVar (&r_ambient, 0, 255);
	D3DLight_BoundPropertyVar (&r_fullbright, 0, 1);
	D3DLight_BoundPropertyVar (&r_coloredlight, 0, 1);
	D3DLight_BoundPropertyVar (&r_overbright, 0, 1);
	D3DLight_BoundPropertyVar (&r_hdrlight, 0, 1);
	D3DLight_BoundPropertyVar (&r_dynamic, 0, 1);

	d3d_LightProperty = (r_ambient.integer << 24) |
		(r_fullbright.integer << 23) |
		(r_coloredlight.integer << 22) |
		(r_overbright.integer << 21) |
		(r_hdrlight.integer << 20) |
		(r_dynamic.integer << 19);

	// note - bit 0 is a special control bit that we'll never change ourselves to ensure that we can do
	// surf->LightProperties = ~d3d_LightProperty and have it valid even if the player resets everything else between frames
	d3d_LightProperty |= 1;
}


void D3DLight_BuildLightmap (msurface_t *surf, int texnum)
{
	if (!d3d_Lightmaps[texnum].SysTexture) return;
	if (!d3d_Lightmaps[texnum].DefTexture) return;

	int size = surf->smax * surf->tmax * 3;
	unsigned *blocklights = (unsigned *) scratchbuf;

	// by default the surf is not updated
	bool updated = false;

	if (surf->LightProperties != d3d_LightProperty)
	{
		// any change in light properties must force an update even if nothing else changes
		surf->LightProperties = d3d_LightProperty;
		updated = true;
	}

	// eval base lighting for this surface
	int baselight = 0;

	if (r_fullbright.integer || !surf->model->brushhdr->lightdata)
		baselight = 32767;
	else if (cl.maxclients > 1 || r_ambient.integer < 1)
		baselight = 0;
	else baselight = r_ambient.integer << 7;

	if (r_fullbright.integer || !surf->model->brushhdr->lightdata || !surf->numstyles)
	{
		// clear to base light
		for (int i = 0; i < size; i += 3, blocklights += 3)
		{
			blocklights[0] = baselight;
			blocklights[1] = baselight;
			blocklights[2] = baselight;
		}

		// must reset this each time it's used so that it will be valid for next time
		blocklights = (unsigned *) scratchbuf;
	}
	else
	{
		// init to the first lightstyle (adding baselight as we go)
		int scale = d_lightstylevalue[surf->styles[0]];
		byte *lightmap = surf->samples[0];

		if (baselight)
		{
			for (int i = 0; i < size; i += 3, blocklights += 3, lightmap += 3)
			{
				blocklights[0] = lightmap[0] * scale + baselight;
				blocklights[1] = lightmap[1] * scale + baselight;
				blocklights[2] = lightmap[2] * scale + baselight;
			}
		}
		else
		{
			for (int i = 0; i < size; i += 3, blocklights += 3, lightmap += 3)
			{
				blocklights[0] = lightmap[0] * scale;
				blocklights[1] = lightmap[1] * scale;
				blocklights[2] = lightmap[2] * scale;
			}
		}

		if (surf->cached_light[0] != scale)
		{
			surf->cached_light[0] = scale;
			updated = true;
		}

		// must reset this each time it's used so that it will be valid for next time
		blocklights = (unsigned *) scratchbuf;

		// add the rest of the styles (...if any...)
		for (int maps = 1; maps < surf->numstyles; maps++)
		{
			scale = d_lightstylevalue[surf->styles[maps]];
			lightmap = surf->samples[maps];

			for (int i = 0; i < size; i += 3, blocklights += 3, lightmap += 3)
			{
				blocklights[0] += lightmap[0] * scale;
				blocklights[1] += lightmap[1] * scale;
				blocklights[2] += lightmap[2] * scale;
			}

			if (surf->cached_light[maps] != scale)
			{
				surf->cached_light[maps] = scale;
				updated = true;
			}

			// must reset this each time it's used so that it will be valid for next time
			blocklights = (unsigned *) scratchbuf;
		}
	}

	if ((!r_fullbright.integer && surf->model->brushhdr->lightdata) && (surf->dlightframe == d3d_RenderDef.framecount))
	{
		// add all the dynamic lights (don't add if r_fullbright or no lightdata...)
		if (D3D_AddDynamicLights (surf))
		{
			// dirty the properties to force an update next frame
			surf->LightProperties = ~d3d_LightProperty;
			updated = true;
		}
	}

	// if the light didn't actually change then don't bother
	if (!updated) return;

	// track number of changed surfs
	d3d_RenderDef.numdlight++;

	// standard lighting
	if (!r_coloredlight.integer)
	{
		// convert lighting from RGB to greyscale; done as a separate pass as we could have different texture formats now
		int white[3] = {307, 604, 113};	// using a bigger scale to preserve precision

		for (int i = 0; i < size; i += 3, blocklights += 3)
		{
			int t = (DotProduct (blocklights, white)) >> 10;
			blocklights[0] = blocklights[1] = blocklights[2] = t;
		}

		blocklights = (unsigned *) scratchbuf;
	}

	// now update the texture rect
	D3DLOCKED_RECT lockrect;

	// here we're just updating the system memory copy so we can lock/unlock per surf.
	// This gives an error with the debug runtimes but it's OK.  (FIXME - can we lock and leave it locked???)
	if (FAILED (d3d_Lightmaps[texnum].SysTexture->LockRect (0, &lockrect, &surf->LightRect, d3d_GlobalCaps.DefaultLock)))
	{
		Con_Printf ("IDirect3DTexture9::LockRect failed for lightmap texture.\n");
		return;
	}

	if (r_overbright.integer && r_hdrlight.integer)
	{
		unsigned *dest = (unsigned *) lockrect.pBits;
		int stride = lockrect.Pitch >> 2;
		int alpha = (kurok ? 128 : 255);
		int maxl, r, g, b, a;

		for (int i = 0; i < surf->tmax; i++)
		{
			for (int j = 0; j < surf->smax; j++, blocklights += 3)
			{
				// fixme - work on blocklights directly here?
				r = blocklights[0] >> 7;
				g = blocklights[1] >> 7;
				b = blocklights[2] >> 7;

				if ((r | g | b) > 255)
				{
					maxl = max3 (r, g, b);

					// we can't << 8 because then 256 would overshoot a byte.  hopefully the compiler does a good job on this.
					r = (r * 255) / maxl;
					g = (g * 255) / maxl;
					b = (b * 255) / maxl;

					a = (alpha * 255) / maxl;
				}
				else a = alpha;

				dest[j] = r | (g << 8) | (b << 16) | (a << 24);
			}

			dest += stride;
		}
	}
	else if (r_overbright.integer)
	{
		byte *dest = (byte *) lockrect.pBits;
		int stride = lockrect.Pitch - (surf->smax << 2);
		int alpha = (kurok ? 64 : 128);

		for (int i = 0, t = 0; i < surf->tmax; i++)
		{
			for (int j = 0; j < surf->smax; j++, blocklights += 3, dest += 4)
			{
				t = blocklights[0] >> 8; dest[0] = min2 (t, 255);
				t = blocklights[1] >> 8; dest[1] = min2 (t, 255);
				t = blocklights[2] >> 8; dest[2] = min2 (t, 255);
				dest[3] = alpha;
			}

			dest += stride;
		}
	}
	else
	{
		byte *dest = (byte *) lockrect.pBits;
		int stride = lockrect.Pitch - (surf->smax << 2);
		int alpha = (kurok ? 128 : 255);

		for (int i = 0, t = 0; i < surf->tmax; i++)
		{
			for (int j = 0; j < surf->smax; j++, blocklights += 3, dest += 4)
			{
				t = blocklights[0] >> 7; dest[0] = min2 (t, 255);
				t = blocklights[1] >> 7; dest[1] = min2 (t, 255);
				t = blocklights[2] >> 7; dest[2] = min2 (t, 255);
				dest[3] = alpha;
			}

			dest += stride;
		}
	}

	d3d_Lightmaps[texnum].SysTexture->UnlockRect (0);
	d3d_Lightmaps[texnum].Modified = true;
}


void D3DLight_CreateTexture (LPDIRECT3DTEXTURE9 *tex, D3DPOOL pool, DWORD usage)
{
	// we need D3DFMT_A8R8G8B8 for the HDR format
	hr = d3d_Device->CreateTexture
	(
		LightmapWidth,
		LightmapHeight,
		1,
		usage,
		D3DFMT_A8R8G8B8,
		pool,
		tex,
		NULL
	);

	if (FAILED (hr))
	{
		hr = d3d_Device->CreateTexture
		(
			LightmapWidth,
			LightmapHeight,
			1,
			0,
			D3DFMT_A8R8G8B8,
			pool,
			tex,
			NULL
		);

		if (FAILED (hr)) Sys_Error ("D3DLight_CreateTexture : IDirect3DDevice9::CreateTexture failed");
	}
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
		// may also want to use it for their own stuff; we'll just put it on the hunk instead
		if (!d3d_LMAllocated[texnum])
		{
			// MainHunk->Alloc no longer does a memset 0
			d3d_LMAllocated[texnum] = (unsigned short *) MainHunk->Alloc (LightmapWidth * sizeof (unsigned short));
			memset (d3d_LMAllocated[texnum], 0, LightmapWidth * sizeof (unsigned short));
		}

		int best = LightmapHeight;

		for (int i = 0; i < LightmapWidth - surf->smax; i++)
		{
			int j;
			int best2 = 0;

			for (j = 0; j < surf->smax; j++)
			{
				if (d3d_LMAllocated[texnum][i + j] >= best) break;
				if (d3d_LMAllocated[texnum][i + j] > best2) best2 = d3d_LMAllocated[texnum][i + j];
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
			d3d_LMAllocated[texnum][surf->LightRect.left + i] = best + surf->tmax;

		// reuse any lightmaps which were previously allocated
		if (!d3d_Lightmaps[texnum].SysTexture) D3DLight_CreateTexture (&d3d_Lightmaps[texnum].SysTexture, D3DPOOL_SYSTEMMEM, 0);
		if (!d3d_Lightmaps[texnum].DefTexture) D3DLight_CreateTexture (&d3d_Lightmaps[texnum].DefTexture, D3DPOOL_DEFAULT, D3DUSAGE_DYNAMIC);

		// setup lightmap pointers
		for (surf->numstyles = 0; surf->numstyles < MAXLIGHTMAPS && surf->styles[surf->numstyles] != 255; surf->numstyles++)
			surf->samples[surf->numstyles] = surf->samples[0] + (surf->numstyles * surf->smax * surf->tmax * 3);

		// ensure no dlight update happens and rebuild the lightmap fully
		surf->dlightframe = -1;

		// initially assign these
		surf->LightmapTextureNum = texnum;
		surf->d3d_LightmapTex = d3d_Lightmaps[surf->LightmapTextureNum].DefTexture;

		// the surf initially has invalid properties set which forces the lightmap to be built here
		surf->LightProperties = ~d3d_LightProperty;

		// and build the map
		D3DLight_BuildLightmap (surf, texnum);

		// this is a valid area
		return;
	}

	// failed to allocate
	Sys_Error ("D3DLight_CreateSurfaceLightmap : failed to allocate a lightmap for surface");
}


int D3DLight_SurfaceSortFunc (msurface_t **s1, msurface_t **s2)
{
	return (*s2)->texinfo->texture - (*s1)->texinfo->texture;
}


void D3DLight_CreateSurfaceLightmaps (model_t *mod)
{
	// these can't go in the scratch buffer because it's used for blocklights
	brushhdr_t *hdr = mod->brushhdr;

	msurface_t **lightsurfs = (msurface_t **) MainHunk->Alloc (hdr->numsurfaces * sizeof (msurface_t *));
	int numlightsurfs = 0;

	for (int i = 0; i < hdr->numsurfaces; i++)
	{
		if (hdr->surfaces[i].flags & SURF_DRAWSKY) continue;
		if (hdr->surfaces[i].flags & SURF_DRAWTURB) continue;

		lightsurfs[numlightsurfs] = &hdr->surfaces[i];
		numlightsurfs++;
	}

	qsort (lightsurfs, numlightsurfs, sizeof (msurface_t *), (sortfunc_t) D3DLight_SurfaceSortFunc);

	for (int i = 0; i < numlightsurfs; i++)
	{
		D3DLight_CreateSurfaceLightmap (lightsurfs[i]);
	}
}


double d3d_LightUpdateTime = 0.0;
bool d3d_LightUpdate = false;

void D3DLight_BeginBuildingLightmaps (void)
{
	// set up the default styles so that (most) lightmaps won't have to be regenerated the first time they're seen
	R_SetDefaultLightStyles ();

	// and the default lighting properties for the current selection of cvars
	D3DLight_SetProperty ();

	// get the initial hunk mark so that we can do the allocation buffers
	d3d_LightHunkMark = MainHunk->GetLowMark ();

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

	// note - some devices have a max height < max width (my ATI does)
	if (d3d_DeviceCaps.MaxTextureHeight < d3d_GlobalCaps.MaxExtents) d3d_GlobalCaps.MaxExtents = d3d_DeviceCaps.MaxTextureHeight;
	if (LightmapWidth < d3d_GlobalCaps.MaxExtents) d3d_GlobalCaps.MaxExtents = LightmapWidth;
	if (LightmapHeight < d3d_GlobalCaps.MaxExtents) d3d_GlobalCaps.MaxExtents = LightmapHeight;

	d3d_GlobalCaps.MaxExtents <<= 4;
	d3d_GlobalCaps.MaxExtents -= 16;

	Con_DPrintf ("max extents: %i\n", d3d_GlobalCaps.MaxExtents);

	// clear down allocation buffers
	for (int i = 0; i < MAX_LIGHTMAPS; i++)
	{
		d3d_Lightmaps[i].Modified = false;
		d3d_LMAllocated[i] = NULL;
	}
}


void D3DLight_EndBuildingLightmaps (void)
{
	// preload everything to prevent runtime stalls
	for (int i = 0; i < MAX_LIGHTMAPS; i++)
	{
		// update any lightmaps which were created and release any left over which were unused
		if (!d3d_Lightmaps[i].SysTexture) continue;

		if (d3d_Lightmaps[i].Modified)
		{
			d3d_Device->UpdateTexture (d3d_Lightmaps[i].SysTexture, d3d_Lightmaps[i].DefTexture);
			d3d_Lightmaps[i].Modified = false;
		}
		else
		{
			// release any lightmaps which were unused
			SAFE_RELEASE (d3d_Lightmaps[i].SysTexture);
			SAFE_RELEASE (d3d_Lightmaps[i].DefTexture);
		}
	}

	MainHunk->FreeToLowMark (d3d_LightHunkMark);
}


void D3DLight_CheckSurfaceForModification (msurface_t *surf)
{
	// no lightmaps
	if (surf->flags & SURF_DRAWSKY) return;
	if (surf->flags & SURF_DRAWTURB) return;

	// ensure that we've got the correct texture for this surf
	surf->d3d_LightmapTex = d3d_Lightmaps[surf->LightmapTextureNum].DefTexture;

	if (!d3d_LightUpdate) return;

	// disable dynamic lights on this surf in coronas-only mode (don't set cached to false here as
	// we want to clear any previous dlights; R_PushDLights will look after not adding more for us)
	if (d3d_CoronaState == CORONA_ONLY)
		surf->dlightframe = ~d3d_RenderDef.framecount;

	// check for lighting parameter modification
	if (surf->LightProperties != d3d_LightProperty)
	{
		D3DLight_BuildLightmap (surf, surf->LightmapTextureNum);
		return;
	}

	// no lightmap modifications
	if (!r_dynamic.integer) return;

	// dynamic light this frame
	if (surf->dlightframe == d3d_RenderDef.framecount)
	{
		D3DLight_BuildLightmap (surf, surf->LightmapTextureNum);
		return;
	}

	// cached lightstyle change
	for (int maps = 0; maps < surf->numstyles; maps++)
	{
		if (d_lightstylevalue[surf->styles[maps]] != surf->cached_light[maps])
		{
			// Con_Printf ("Modified light from %i to %i\n", surf->cached_light[maps], d_lightstylevalue[surf->styles[maps]]);
			D3DLight_BuildLightmap (surf, surf->LightmapTextureNum);
			return;
		}
	}
}


// set this to the number of times per second you want lightmaps to update at, or 0 to always update
// some short-lived dynamic lights may not be visible if you set it too low...
cvar_t r_lightmapupdaterate ("r_lightmapupdaterate", 0.0f, CVAR_ARCHIVE);

void D3DLight_UpdateLightmaps (void)
{
	if (!d3d_Device) return;

	if (r_lightmapupdaterate.value > 0)
	{
		if (cl.time >= d3d_LightUpdateTime)
		{
			d3d_LightUpdateTime = cl.time + (1.0 / r_lightmapupdaterate.value);
			d3d_LightUpdate = true;
		}
		else d3d_LightUpdate = false;
	}
	else d3d_LightUpdate = true;

	for (int i = 0; i < MAX_LIGHTMAPS; i++)
	{
		if (!d3d_Lightmaps[i].SysTexture) break;

		if (!d3d_Lightmaps[i].DefTexture)
		{
			// recreate the default pool texture
			D3DLight_CreateTexture (&d3d_Lightmaps[i].DefTexture, D3DPOOL_DEFAULT, D3DUSAGE_DYNAMIC);

			d3d_Lightmaps[i].SysTexture->AddDirtyRect (NULL);
			d3d_Lightmaps[i].Modified = true;
		}

		// update any lightmaps which were modified
		if (d3d_Lightmaps[i].Modified)
		{
			d3d_Device->UpdateTexture (d3d_Lightmaps[i].SysTexture, d3d_Lightmaps[i].DefTexture);
			d3d_Lightmaps[i].Modified = false;
		}
	}
}


void D3DLight_ReleaseLightmaps (void)
{
	for (int i = 0; i < MAX_LIGHTMAPS; i++)
	{
		SAFE_RELEASE (d3d_Lightmaps[i].SysTexture);
		SAFE_RELEASE (d3d_Lightmaps[i].DefTexture);

		d3d_Lightmaps[i].Modified = false;
	}
}


void D3DLight_OnLostDevice (void)
{
	for (int i = 0; i < MAX_LIGHTMAPS; i++)
	{
		SAFE_RELEASE (d3d_Lightmaps[i].DefTexture);

		if (d3d_Lightmaps[i].SysTexture)
		{
			d3d_Lightmaps[i].SysTexture->AddDirtyRect (NULL);
			d3d_Lightmaps[i].Modified = false;
		}
	}
}


void D3DLight_OnResetDevice (void)
{
	for (int i = 0; i < MAX_LIGHTMAPS; i++)
	{
		if (d3d_Lightmaps[i].SysTexture)
		{
			D3DLight_CreateTexture (&d3d_Lightmaps[i].DefTexture, D3DPOOL_DEFAULT, D3DUSAGE_DYNAMIC);
			d3d_Lightmaps[i].SysTexture->AddDirtyRect (NULL);
			d3d_Device->UpdateTexture (d3d_Lightmaps[i].SysTexture, d3d_Lightmaps[i].DefTexture);
			d3d_Lightmaps[i].Modified = false;
		}
	}
}


CD3DDeviceLossHandler d3d_LightHander (D3DLight_OnLostDevice, D3DLight_OnResetDevice);

