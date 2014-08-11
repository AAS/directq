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

// since we're just updating small subrects now we can have these be quite large to produce better
// texture chaining
#define LIGHTMAP_WIDTH		256
#define LIGHTMAP_HEIGHT		256
#define MAX_LIGHTMAPS		256

int LightmapWidth = -1;
int LightmapHeight = -1;
LPDIRECT3DTEXTURE9 d3d_Lightmaps[MAX_LIGHTMAPS];
int d3d_NumLightmaps = 0;


/*
========================================================================================================================

		CORONA RENDERING

========================================================================================================================
*/

void D3DAlpha_AddToList (dlight_t *dl);

cvar_t gl_flashblend ("gl_flashblend", "0", CVAR_ARCHIVE);
cvar_t r_coronas ("r_coronas", "0", CVAR_ARCHIVE);
cvar_t r_coronadetail ("r_coronadetail", "16", CVAR_ARCHIVE);
cvar_t r_coronaradius ("r_coronaradius", "1", CVAR_ARCHIVE);
cvar_t r_coronaintensity ("r_coronaintensity", "1", CVAR_ARCHIVE);

bool r_coronastateset = false;

#define CORONA_ONLY				1
#define LIGHTMAP_ONLY			2
#define CORONA_PLUS_LIGHTMAP	3

int d3d_CoronaState = 0;

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


// oh well; at least it lets me reuse the declaration...
extern LPDIRECT3DVERTEXDECLARATION9 d3d_DrawDecl;

typedef struct r_coronavertex_s
{
	// oh well; at least it lets me reuse the declaration...
	float x, y, z;
	D3DCOLOR c;
	float dummy[2];
} r_coronavertex_t;


// fixme - move these to a vertex buffer
r_coronavertex_t *r_coronavertexes = NULL;
unsigned short *r_coronaindexes = NULL;

int r_numcoronavertexes = 0;
int r_numcoronaindexes = 0;

#define R_MAXCORONAVERTEXES		8192
#define R_MAXCORONAINDEXES		65536

void D3DLight_BeginCoronas (void)
{
	// we may not be drawing coronas (if the view is inside them) so do it this way instead
	r_coronastateset = false;

	// fix up cvars
	if (r_coronadetail.integer < 2) Cvar_Set (&r_coronadetail, 2.0f);
	if (r_coronadetail.integer > 256) Cvar_Set (&r_coronadetail, 256.0f);

	if (r_coronaradius.integer < 0) Cvar_Set (&r_coronaradius, 0.0f);
	if (r_coronaintensity.value < 0) Cvar_Set (&r_coronaintensity, 0.0f);

	r_coronavertexes = (r_coronavertex_t *) scratchbuf;
	r_coronaindexes = (unsigned short *) (r_coronavertexes + R_MAXCORONAVERTEXES);

	r_numcoronavertexes = 0;
	r_numcoronaindexes = 0;
}


void D3DLight_FlushCoronas (void)
{
	// reset for drawing
	r_coronavertexes = (r_coronavertex_t *) scratchbuf;
	r_coronaindexes = (unsigned short *) (r_coronavertexes + R_MAXCORONAVERTEXES);

	if (r_numcoronavertexes && r_numcoronaindexes)
	{
		D3DHLSL_CheckCommit ();

		// maintaining a VBO for this seems too much like hard work so we won't
		// ...although we reallu should cos DIPUP is too slow...
		d3d_Device->DrawIndexedPrimitiveUP
			(D3DPT_TRIANGLELIST, 0, r_numcoronavertexes,
			r_numcoronaindexes / 3,
			r_coronaindexes,
			D3DFMT_INDEX16,
			r_coronavertexes,
			sizeof (r_coronavertex_t));

		d3d_RenderDef.numdrawprim++;
		// Con_Printf ("%i v %i i\n", r_numcoronavertexes, r_numcoronaindexes);
	}

	r_numcoronavertexes = 0;
	r_numcoronaindexes = 0;
}


void D3DLight_EndCoronas (void)
{
	if (r_coronastateset)
	{
		// draw anything left over
		D3DLight_FlushCoronas ();

		// revert the blend mode change
		D3D_SetRenderState (D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
		D3D_SetRenderState (D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

		r_coronastateset = false;
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
	if (r_coronaintensity.value < 0) return;

	VectorSubtract (dl->origin, r_viewvectors.origin, v);
	float dist = Length (v);

	// fixme - optimize this out before adding... (done - ish)
	if (dist < rad)
	{
		// view is inside the dlight
		if (d3d_CoronaState == CORONA_ONLY)
			D3DLight_AddLightBlend ((float) dl->rgb[0] / 512.0f, (float) dl->rgb[1] / 512.0f, (float) dl->rgb[2] / 512.0f, dl->radius * 0.0003);

		return;
	}

	if (!r_coronastateset)
	{
		// unbind all streams because we're going to UP mode here
		D3D_SetStreamSource (0, NULL, 0, 0);
		D3D_SetStreamSource (1, NULL, 0, 0);
		D3D_SetStreamSource (2, NULL, 0, 0);
		D3D_SetIndices (NULL);

		// switch shader (just reuse this)
		// note that because of the blend mode used coronas don;t need special handling for fog ;)
		D3DHLSL_SetPass (FX_PASS_DRAWCOLORED);

		// changed blend mode
		D3D_SetRenderState (D3DRS_SRCBLEND, D3DBLEND_ONE);
		D3D_SetRenderState (D3DRS_DESTBLEND, D3DBLEND_ONE);

		// oh well; at least it lets me reuse the declaration...
		D3D_SetVertexDeclaration (d3d_DrawDecl);

		// now we're drawing coronas
		r_coronastateset = true;
	}

	// get vertex and index counts
	int numverts = r_coronadetail.integer + 2;
	int numindexes = (numverts - 2) * 3;

	// we have a valid corona now; check for overflow
	if (r_numcoronavertexes + numverts >= R_MAXCORONAVERTEXES || r_numcoronaindexes + numindexes >= R_MAXCORONAINDEXES)
		D3DLight_FlushCoronas ();

	// heh, cool; this one lets us put loadsa verts into a single corona without memory pressure
	r_coronavertex_t *verts = &r_coronavertexes[r_numcoronavertexes];
	unsigned short *indexes = &r_coronaindexes[r_numcoronaindexes];

	float colour[3] =
	{
		(float) dl->rgb[0] * r_coronaintensity.value * colormul,
		(float) dl->rgb[1] * r_coronaintensity.value * colormul,
		(float) dl->rgb[2] * r_coronaintensity.value * colormul
	};

	verts[0].x = dl->origin[0] - r_viewvectors.forward[0] * rad;
	verts[0].y = dl->origin[1] - r_viewvectors.forward[1] * rad;
	verts[0].z = dl->origin[2] - r_viewvectors.forward[2] * rad;
	verts[0].c = D3DCOLOR_ARGB (255, BYTE_CLAMP (colour[0]), BYTE_CLAMP (colour[1]), BYTE_CLAMP (colour[2]));

	for (int i = r_coronadetail.integer, j = 1; i >= 0; i--, j++)
	{
		float a = (float) i / r_coronadetail.value * D3DX_PI * 2;
		float sinarad = sin (a) * rad;
		float cosarad = cos (a) * rad;

		verts[j].x = dl->origin[0] + r_viewvectors.right[0] * cosarad + r_viewvectors.up[0] * sinarad;
		verts[j].y = dl->origin[1] + r_viewvectors.right[1] * cosarad + r_viewvectors.up[1] * sinarad;
		verts[j].z = dl->origin[2] + r_viewvectors.right[2] * cosarad + r_viewvectors.up[2] * sinarad;
		verts[j].c = D3DCOLOR_ARGB (255, 0, 0, 0);
	}

	for (int i = 2; i < numverts; i++, indexes += 3)
	{
		indexes[0] = r_numcoronavertexes;
		indexes[1] = r_numcoronavertexes + i - 1;
		indexes[2] = r_numcoronavertexes + i;
	}

	r_numcoronavertexes += numverts;
	r_numcoronaindexes += numindexes;
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


/*
===============
D3D_AddDynamicLights
===============
*/
void D3D_AddDynamicLights (msurface_t *surf)
{
	int			lnum;
	int			sd, td;
	float		dist, rad, minlight;
	vec3_t		impact, local;
	int			s, t;
	int			i;
	mtexinfo_t	*tex = surf->texinfo;

	if (!r_dynamic.value) return;
	if (d3d_CoronaState == CORONA_ONLY) return;

	for (lnum = 0; lnum < MAX_DLIGHTS; lnum++)
	{
		// light is dead
		if (cl_dlights[lnum].die < cl.time) continue;

		// not hit by this light
		if (!(surf->dlightbits[lnum >> 5] & (1 << (lnum & 31)))) continue;

		rad = cl_dlights[lnum].radius;
		dist = DotProduct (cl_dlights[lnum].origin, surf->plane->normal) - surf->plane->dist;

		rad -= fabs (dist);
		minlight = cl_dlights[lnum].minlight;

		if (rad < minlight) continue;

		minlight = rad - minlight;

		for (i = 0; i < 3; i++)
			impact[i] = cl_dlights[lnum].origin[i] - surf->plane->normal[i] * dist;

		local[0] = DotProduct (impact, tex->vecs[0]) + tex->vecs[0][3];
		local[1] = DotProduct (impact, tex->vecs[1]) + tex->vecs[1][3];

		local[0] -= surf->texturemins[0];
		local[1] -= surf->texturemins[1];

		float dlrgb[] =
		{
			(float) cl_dlights[lnum].rgb[0] * r_dynamic.value,
			(float) cl_dlights[lnum].rgb[1] * r_dynamic.value,
			(float) cl_dlights[lnum].rgb[2] * r_dynamic.value
		};

		unsigned *bl = d3d_BlockLights;

		for (t = 0; t < surf->tmax; t++)
		{
			td = local[1] - t * 16;

			if (td < 0) td = -td;

			for (s = 0; s < surf->smax; s++, bl += 3)
			{
				sd = local[0] - s * 16;

				if (sd < 0) sd = -sd;

				if (sd > td)
					dist = sd + (td >> 1);
				else dist = td + (sd >> 1);

				if (dist < minlight)
				{
					bl[0] += (rad - dist) * dlrgb[0];
					bl[1] += (rad - dist) * dlrgb[1];
					bl[2] += (rad - dist) * dlrgb[2];
				}
			}
		}
	}
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


void R_AnimateLight (void)
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
		flight = (int) floor (cl.time * 10.0f);
		clight = (int) ceil (cl.time * 10.0f);
		lerpfrac = (cl.time * 10.0f) - flight;
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
		int i = (int) (cl.time * 10.0f);

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
	mplane_t	*splitplane;
	float		dist;
	msurface_t	*surf;
	int			i;
	int			sidebit;

	// hey!  no goto!!!
	while (1)
	{
		if (node->contents < 0) return;

		splitplane = node->plane;
		dist = DotProduct (light->origin, splitplane->normal) - splitplane->dist;

		if (dist > light->radius)
		{
			node = node->children[0];
			continue;
		}

		if (dist < -light->radius)
		{
			node = node->children[1];
			continue;
		}

		break;
	}

	// mark the polygons
	surf = cl.worldmodel->brushhdr->surfaces + node->firstsurface;

	for (i = 0; i < node->numsurfaces; i++, surf++)
	{
		// no lights on these
		if (surf->flags & SURF_DRAWTURB) continue;
		if (surf->flags & SURF_DRAWSKY) continue;

		if (surf->dlightframe != d3d_RenderDef.framecount)
		{
			// first time hit
			surf->dlightbits[0] = surf->dlightbits[1] = surf->dlightbits[2] = surf->dlightbits[3] = 0;
			surf->dlightframe = d3d_RenderDef.framecount;
		}

		// mark the surf for this dlight
		surf->dlightbits[num >> 5] |= 1 << (num & 31);
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

		surf = cl.worldmodel->brushhdr->surfaces + node->firstsurface;

		for (i = 0; i < node->numsurfaces; i++, surf++)
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
				color[0] += (float) ((int) ((((((((r11 - r10) * dsfrac) >> 4) + r10) - ((((r01 - r00) * dsfrac) >> 4) + r00)) * dtfrac) >> 4) + ((((r01 - r00) * dsfrac) >> 4) + r00)));
				color[1] += (float) ((int) ((((((((g11 - g10) * dsfrac) >> 4) + g10) - ((((g01 - g00) * dsfrac) >> 4) + g00)) * dtfrac) >> 4) + ((((g01 - g00) * dsfrac) >> 4) + g00)));
				color[2] += (float) ((int) ((((((((b11 - b10) * dsfrac) >> 4) + b10) - ((((b01 - b00) * dsfrac) >> 4) + b00)) * dtfrac) >> 4) + ((((b01 - b00) * dsfrac) >> 4) + b00)));
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

	if (!cl.worldmodel->brushhdr->lightdata)
	{
		// no light data (consistency with world)
		c[0] = c[1] = c[2] = (256 >> r_overbright.integer);
		return;
	}

	// using bbox center point can give black models as it's now adjusted for the 
	// correct frame bbox, so just revert back to good ol' origin
	VectorCopy (e->origin, start);

	// set end point (back to 2048 for less BSP tree tracing)
	end[0] = start[0];
	end[1] = start[1];
	end[2] = start[2] - 2048;

	// initially nothing
	c[0] = c[1] = c[2] = 0;
	lightplane = NULL;

	// get lighting
	R_RecursiveLightPoint (c, cl.worldmodel->brushhdr->nodes, start, end);

	if (cl.maxclients < 2)
	{
		// add ambient factor (SP only)
		c[0] += r_ambient.value;
		c[1] += r_ambient.value;
		c[2] += r_ambient.value;
	}

	// add dynamic lights
	if (r_dynamic.value && (d3d_CoronaState != CORONA_ONLY))
	{
		for (int lnum = 0; lnum < MAX_DLIGHTS; lnum++)
		{
			if (cl_dlights[lnum].die >= cl.time)
			{
				VectorSubtract (e->origin, cl_dlights[lnum].origin, dist);

				add = (cl_dlights[lnum].radius - Length (dist));

				if (add > 0)
				{
					c[0] += (add * cl_dlights[lnum].rgb[0]) * r_dynamic.value;
					c[1] += (add * cl_dlights[lnum].rgb[1]) * r_dynamic.value;
					c[2] += (add * cl_dlights[lnum].rgb[2]) * r_dynamic.value;
				}
			}
		}
	}

	// keep MDL lighting consistent with the world
	for (int i = 0; i < 3; i++)
	{
		int rgb = ((int) (c[i] + 0.5f)) >> (7 + r_overbright.integer);
		c[i] = (float) BYTE_CLAMP (rgb);
	}

	// minimum light values (should these be done before or after dynamics???)
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

void D3DLight_BuildLightmap (msurface_t *surf, LPDIRECT3DTEXTURE9 lm)
{
	// cache these at the levels they were created or updated at
	surf->overbright = r_overbright.integer;
	surf->fullbright = r_fullbright.integer;
	surf->ambient = r_ambient.integer;

	int size = surf->smax * surf->tmax;
	byte *lightmap = surf->samples;
	unsigned *bl = d3d_BlockLights;

	// set to full bright if no light data
	if (r_fullbright.integer || !cl.worldmodel->brushhdr->lightdata)
	{
		bl = d3d_BlockLights;

		for (int i = 0; i < size; i++, bl += 3)
		{
			// ensure correct scale for overbrighting
			bl[0] = 255 * (256 >> r_overbright.integer);
			bl[1] = 255 * (256 >> r_overbright.integer);
			bl[2] = 255 * (256 >> r_overbright.integer);
		}
	}
	else
	{
		if (cl.maxclients > 1 || r_ambient.integer < 1)
		{
			bl = d3d_BlockLights;

			// no ambient light in multiplayer
			for (int i = 0; i < size; i++, bl += 3)
			{
				bl[0] = 0;
				bl[1] = 0;
				bl[2] = 0;
			}
		}
		else
		{
			bl = d3d_BlockLights;
			unsigned int amb = r_ambient.integer * (256 >> r_overbright.integer);

			// clear to ambient light
			for (int i = 0; i < size; i++, bl += 3)
			{
				bl[0] = amb;
				bl[1] = amb;
				bl[2] = amb;
			}
		}

		// add all the dynamic lights first
		if (surf->dlightframe == d3d_RenderDef.framecount)
		{
			D3D_AddDynamicLights (surf);
			surf->cached_dlight = true;
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

				surf->cached_light[maps] = scale;
				bl = d3d_BlockLights;

				for (int i = 0; i < size; i++, bl += 3, lightmap += 3)
				{
					bl[0] += lightmap[0] * scale;
					bl[1] += lightmap[1] * scale;
					bl[2] += lightmap[2] * scale;
				}
			}
		}
	}

	int t;
	int shift = 7 + r_overbright.integer;
	D3DLOCKED_RECT lockrect;

	// this lets us get into and out of the lock as quickly as possible and also minimizes the lock size
	// we also can't rely on the driver to optimize when dirty updates happen so we manage it ourselves
	hr = lm->LockRect (0, &lockrect, &surf->LightRect, 0);

	// if we didn't get a lock we just don't bother updating it
	if (FAILED (hr)) return;

	int stride = lockrect.Pitch - (surf->smax << 2);
	byte *dest = (byte *) lockrect.pBits;
	bl = d3d_BlockLights;

	for (int i = 0; i < surf->tmax; i++, dest += stride)
	{
		for (int j = 0; j < surf->smax; j++, dest += 4, bl += 3)
		{
			// because dest is a hardware resource we must update it in order
			t = bl[2] >> shift; dest[0] = BYTE_CLAMP (t);
			t = bl[1] >> shift; dest[1] = BYTE_CLAMP (t);
			t = bl[0] >> shift; dest[2] = BYTE_CLAMP (t);
			dest[3] = 255;
		}
	}

	// only mark the region that actually gets updated as dirty
	lm->UnlockRect (0);
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
		// there's that handy-dandy buffer again; we don't need to keep this hanging around after building lightmaps
		// so put it in our temporary memory; at width 256 this has room for 2048 lightmaps which is more than we use.
		unsigned short *Allocated = (unsigned short *) (scratchbuf + (texnum * LightmapWidth * sizeof (unsigned short)));

		int best = LightmapHeight;

		for (int i = 0; i < LightmapWidth - surf->smax; i++)
		{
			int j;
			int best2 = 0;

			for (j = 0; j < surf->smax; j++)
			{
				if (Allocated[i + j] >= best) break;
				if (Allocated[i + j] > best2) best2 = Allocated[i + j];
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
			Allocated[surf->LightRect.left + i] = best + surf->tmax;

		// reuse any lightmaps which were previously allocated
		if (!d3d_Lightmaps[texnum])
		{
			hr = d3d_Device->CreateTexture
			(
				LightmapWidth,
				LightmapHeight,
				1,
				0,
				D3DFMT_X8R8G8B8,
				D3DPOOL_MANAGED,
				&d3d_Lightmaps[texnum],
				NULL
			);

			// if we can't create a managed pool texture we're kinda fucked
			if (FAILED (hr)) Sys_Error ("D3DLight_CreateSurfaceLightmap : Failed to create a texture in D3DPOOL_MANAGED");
		}

		// do this outside the if otherwise if the texture isn't created it won't increase!!!
		if (d3d_NumLightmaps < texnum) d3d_NumLightmaps = texnum;

		// we can't create lightmap texcoords until the positioning of the surf within the
		// lightmap texture is known so it's deferred until here
		// note: _controlfp is ineffective here
		for (int i = 0; i < surf->numverts; i++)
		{
			brushpolyvert_t *vert = &surf->verts[i];

			vert->lm[0] = DotProduct (vert->xyz, surf->texinfo->vecs[0]) + surf->texinfo->vecs[0][3];
			vert->lm[0] -= surf->texturemins[0];
			vert->lm[0] += (int) surf->LightRect.left * 16;
			vert->lm[0] += 8;
			vert->lm[0] /= (float) (LightmapWidth * 16);

			vert->lm[1] = DotProduct (vert->xyz, surf->texinfo->vecs[1]) + surf->texinfo->vecs[1][3];
			vert->lm[1] -= surf->texturemins[1];
			vert->lm[1] += (int) surf->LightRect.top * 16;
			vert->lm[1] += 8;
			vert->lm[1] /= (float) (LightmapHeight * 16);
		}

		// ensure no dlight update happens and rebuild the lightmap fully
		surf->cached_dlight = false;
		surf->dlightframe = -1;

		// initially assign these
		surf->LightmapTextureNum = texnum;
		surf->d3d_LightmapTex = d3d_Lightmaps[texnum];

		D3DLight_BuildLightmap (surf, d3d_Lightmaps[texnum]);

		// this is a valid area
		return;
	}

	// failed to allocate
	Sys_Error ("D3DLight_CreateSurfaceLightmap : failed to allocate a lightmap for surface");
}


void D3DLight_BuildLightmapTextureChains (brushhdr_t *hdr)
{
	int nummapsinchains = 0;
	int nummapsinsurfs = 0;

	// clear down chains
	for (int i = 0; i < hdr->numtextures; i++)
	{
		texture_t *tex = hdr->textures[i];

		if (!tex) continue;

		tex->texturechain = NULL;
	}

	// now build them up
	for (int i = 0; i < hdr->numsurfaces; i++)
	{
		msurface_t *surf = &hdr->surfaces[i];

		// ensure
		surf->LightmapTextureNum = -1;

		// no lightmaps on these surface types
		if (surf->flags & SURF_DRAWSKY) continue;
		if (surf->flags & SURF_DRAWTURB) continue;

		texture_t *tex = surf->texinfo->texture;

		surf->texturechain = tex->texturechain;
		tex->texturechain = surf;
	}

	// now build lightmaps for all surfaces in each chain
	for (int i = 0; i < hdr->numtextures; i++)
	{
		texture_t *tex = hdr->textures[i];

		if (!tex) continue;
		if (!tex->texturechain) continue;

		// build lightmaps for all surfs in this chain
		for (msurface_t *surf = tex->texturechain; surf; surf = surf->texturechain)
		{
			D3DLight_CreateSurfaceLightmap (surf);
			nummapsinchains++;
		}

		// clear down
		tex->texturechain = NULL;
	}

	// finally pick up any surfaces we may have missed above
	// (there should be none)
	for (int i = 0; i < hdr->numsurfaces; i++)
	{
		msurface_t *surf = &hdr->surfaces[i];

		// no lightmaps on these surface types
		if (surf->flags & SURF_DRAWSKY) continue;
		if (surf->flags & SURF_DRAWTURB) continue;

		// check if it already has a lightmap
		if (surf->LightmapTextureNum < 0)
		{
			// build the lightmap
			D3DLight_CreateSurfaceLightmap (surf);
			nummapsinsurfs++;
		}
	}

	if (nummapsinchains) Con_DPrintf ("Built %i surface lightmaps in texturechains\n", nummapsinchains);
	if (nummapsinsurfs) Con_DPrintf ("Built %i surface lightmaps in surfaces\n", nummapsinsurfs);
}


void D3DLight_BuildAllLightmaps (void)
{
	if (LightmapWidth < 0 || LightmapHeight < 0)
	{
		LightmapWidth = LIGHTMAP_WIDTH;
		LightmapHeight = LIGHTMAP_HEIGHT;

		// bound lightmap textures to max supported by the device
		if (LightmapWidth > d3d_DeviceCaps.MaxTextureWidth) LightmapWidth = d3d_DeviceCaps.MaxTextureWidth;
		if (LightmapHeight > d3d_DeviceCaps.MaxTextureHeight) LightmapHeight = d3d_DeviceCaps.MaxTextureHeight;
	}

	model_t	*mod;
	extern int MaxExtents[];

	// ensure it's within constraints
	if (((MaxExtents[0] >> 4) + 1) > LightmapWidth) Sys_Error ("((MaxExtents[0] >> 4) + 1) > LightmapWidth");
	if (((MaxExtents[1] >> 4) + 1) > LightmapHeight) Sys_Error ("((MaxExtents[1] >> 4) + 1) > LightmapHeight");
	if (LightmapWidth > d3d_DeviceCaps.MaxTextureWidth) Sys_Error ("LightmapWidth > d3d_DeviceCaps.MaxTextureWidth");
	if (LightmapHeight > d3d_DeviceCaps.MaxTextureHeight) Sys_Error ("LightmapHeight > d3d_DeviceCaps.MaxTextureHeight");

	// now alloc the blocklights as big as we need them
	d3d_BlockLights = (unsigned int *) MainHunk->Alloc (((MaxExtents[0] >> 4) + 1) * ((MaxExtents[1] >> 4) + 1) * sizeof (unsigned int) * 3);

	// we're going to use the scratchbuf to track allocations so clear it out now as it could have literally anything in it
	memset (scratchbuf, 0, SCRATCHBUF_SIZE);

	// no lightmaps yet...
	d3d_NumLightmaps = 0;

	// note - the player is model 0 in the precache list
	for (int j = 1; j < MAX_MODELS; j++)
	{
		// note - we set the end of the precache list to NULL in cl_parse to ensure this test is valid
		if (!(mod = cl.model_precache[j])) break;

		mod->cacheent = NULL;

		if (mod->type != mod_brush) continue;

		// alloc space for caching the entity state
		if (mod->name[0] == '*')
		{
			mod->cacheent = (entity_t *) MainHunk->Alloc (sizeof (entity_t));
			continue;
		}

		// build the lightmaps by texturechains - using sorts seems to give higher r_speeds
		// counts somehow; could probably fix it but why bother when there's another way?
		D3DLight_BuildLightmapTextureChains (mod->brushhdr);
	}

	// add an additional lightmap because the count is based on texnum in D3DLight_CreateSurfaceLightmap which is 0-based
	d3d_NumLightmaps++;

	// preload everything to prevent runtime stalls
	for (int i = 0; i < MAX_LIGHTMAPS; i++)
	{
		if (i < d3d_NumLightmaps)
			d3d_Lightmaps[i]->PreLoad ();
		else
		{
			// release any lightmaps which were unused
			SAFE_RELEASE (d3d_Lightmaps[i]);
		}
	}
}


void D3DLight_CheckSurfaceForModification (d3d_modelsurf_t *ms)
{
	// retrieve the surf used by this modelsurf
	msurface_t *surf = ms->surf;

	// no lightmaps
	if (surf->flags & SURF_DRAWSKY) return;
	if (surf->flags & SURF_DRAWTURB) return;

	// set the correct lightmap texture to use for this surface
	surf->d3d_LightmapTex = d3d_Lightmaps[surf->LightmapTextureNum];

	// check for overbright or fullbright modification
	// these always override values of r_dynamic or light cache so that we ensure everything is updated
	// if they ever change (should some of this be just encoded in d_lightstylevalue so that it will happen automatically???)
	if (surf->overbright != r_overbright.integer) goto ModifyLightmap;
	if (surf->fullbright != r_fullbright.integer) goto ModifyLightmap;
	if (surf->ambient != r_ambient.integer) goto ModifyLightmap;

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
	if (surf->dlightframe == d3d_RenderDef.framecount || surf->cached_dlight) goto ModifyLightmap;

	// no changes
	return;

ModifyLightmap:;
	// rebuild the lightmap
	D3DLight_BuildLightmap (surf, d3d_Lightmaps[surf->LightmapTextureNum]);
}


void D3DLight_ReleaseLightmaps (void)
{
	for (int i = 0; i < MAX_LIGHTMAPS; i++)
	{
		SAFE_RELEASE (d3d_Lightmaps[i]);
	}
}
