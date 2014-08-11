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

int LightmapWidth = LIGHTMAP_WIDTH;
int LightmapHeight = LIGHTMAP_HEIGHT;

void D3D_CreateNewLightmapTexture (LPDIRECT3DTEXTURE9 *tex)
{
	// managed seems to run faster; presumably d3d is doing some magic behind the scenes here
	hr = d3d_Device->CreateTexture
	(
		LightmapWidth,
		LightmapHeight,
		1,
		0,
		D3DFMT_X8R8G8B8,
		D3DPOOL_MANAGED,
		tex,
		NULL
	);

	if (SUCCEEDED (hr)) return;

	// oh well; shit happens
	Sys_Error ("Failed to create texture for lightmap\n");
}


// internal lightmap structures
typedef struct d3d_lightmap_s
{
	bool modified;
	int *allocated;
	D3DLOCKED_RECT d3d_LockedRect;
	LPDIRECT3DTEXTURE9 d3d_Texture;
	bool lost;
	RECT DirtyRect;
} d3d_lightmap_t;

#define MAX_LIGHTMAPS	256

d3d_lightmap_t d3d_InternalLightmaps[MAX_LIGHTMAPS];
int d3d_NumLightmaps = 0;

// fixme - do we need to do this on a new map???
void D3D_ReleaseLightmaps (void)
{
	for (int i = 0; i < MAX_LIGHTMAPS; i++)
	{
		d3d_lightmap_t *lm = &d3d_InternalLightmaps[i];

		// release the texture
		SAFE_RELEASE (lm->d3d_Texture);
	}
}


void D3D_UploadLightmaps (void)
{
	int released = 0, modified = 0;

	for (int i = 0; i < MAX_LIGHTMAPS; i++)
	{
		d3d_lightmap_t *lm = &d3d_InternalLightmaps[i];

		if (!lm->d3d_Texture) continue;

		if (!lm->allocated || !lm->allocated[0])
		{
			if (lm->d3d_Texture)
			{
				lm->d3d_Texture->Release ();
				lm->d3d_Texture = NULL;
				released++;
			}

			continue;
		}

		if (lm->modified)
		{
			lm->d3d_Texture->UnlockRect (0);
			lm->d3d_Texture->AddDirtyRect (&lm->DirtyRect);
			// lm->d3d_Texture->PreLoad ();

			// flag as unmodified and clear the dirty region
			lm->modified = false;
			lm->DirtyRect.left = LightmapWidth;
			lm->DirtyRect.right = 0;
			lm->DirtyRect.top = LightmapHeight;
			lm->DirtyRect.bottom = 0;
			modified++;
		}
	}

	//if (released) Con_Printf ("Released %i lightmaps\n", released);
	//if (modified) Con_Printf ("Modified %i lightmaps\n", modified);
}


// main lightmap object
CD3DLightmap *d3d_Lightmaps = NULL;

cvar_t r_lerplightstyle ("r_lerplightstyle", "0", CVAR_ARCHIVE);
cvar_t r_coloredlight ("r_coloredlight", "1");
cvar_t r_overbright ("r_overbright", "1", CVAR_ARCHIVE);
cvar_t r_ambient ("r_ambient", "0");

cvar_alias_t gl_overbright ("gl_overbright", &r_overbright);

// size increased for new max extents
unsigned int *d3d_BlockLights;


void D3D_BuildLightmapForSurface (msurface_t *surf)
{
	// fill in lightmap extents
	surf->smax = (surf->extents[0] >> 4) + 1;
	surf->tmax = (surf->extents[1] >> 4) + 1;

	// attempt to create the lightmap
	if (d3d_Lightmaps && d3d_Lightmaps->AllocBlock (surf))
	{
		surf->d3d_Lightmap->CalcLightmapTexCoords (surf);
		return;
	}

	// didn't create a lightmap so allocate a new one
	// this will always work as a new block that's big enough will just have become free
	surf->d3d_Lightmap = new CD3DLightmap (surf);
	d3d_Lightmaps->AllocBlock (surf);
	surf->d3d_Lightmap->CalcLightmapTexCoords (surf);
}


// fixme - qsort these instead....
void D3D_BuildLightmapTextureChains (brushhdr_t *hdr)
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
		surf->d3d_Lightmap = NULL;

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
			D3D_BuildLightmapForSurface (surf);
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

		// already has a lightmap
		if (surf->d3d_Lightmap) continue;

		// build the lightmap
		D3D_BuildLightmapForSurface (surf);
		nummapsinsurfs++;
	}

	if (nummapsinchains) Con_DPrintf ("Built %i surface lightmaps in texturechains\n", nummapsinchains);
	if (nummapsinsurfs) Con_DPrintf ("Built %i surface lightmaps in surfaces\n", nummapsinsurfs);
}


/*
==================
D3D_BuildLightmaps

Builds the lightmap textures with all the surfaces from all brush models

lightmaps are arranged by surface texture in an attempt to get all surfaces with the same texture into the same lightmap.
this gives us (1) less state changes from texture changes, and (2) larger surface batches to submit in one go.
==================
*/
void D3D_BuildLightmaps (void)
{
	Con_DPrintf ("Loading lightmaps... ");

	// bound lightmap textures to max supported by the device
	if (LightmapWidth > d3d_DeviceCaps.MaxTextureWidth) LightmapWidth = d3d_DeviceCaps.MaxTextureWidth;
	if (LightmapHeight > d3d_DeviceCaps.MaxTextureHeight) LightmapHeight = d3d_DeviceCaps.MaxTextureHeight;

	model_t	*mod;
	extern int MaxExtents[];

	// get max blocklight size
	int smax = (MaxExtents[0] >> 4) + 1;
	int tmax = (MaxExtents[1] >> 4) + 1;

	if (smax > LightmapWidth) Host_Error ("Surface extents (%i) exceeds %i\n", smax, LightmapWidth);
	if (tmax > LightmapHeight) Host_Error ("Surface extents (%i) exceeds %i\n", tmax, LightmapHeight);

	// now alloc the blocklights as big as we need them
	d3d_BlockLights = (unsigned int *) MainHunk->Alloc (smax * tmax * sizeof (unsigned int) * 3);

	// clear down lightmap allocations from the previous map
	for (int i = 0; i < MAX_LIGHTMAPS; i++)
	{
		d3d_lightmap_t *lm = &d3d_InternalLightmaps[i];

		// just null the pointer; we don't release the texture here so that it can be reused
		lm->allocated = NULL;

		if (lm->modified)
		{
			// this should never be needed but let's play safe...
			// lm->d3d_Texture->UnlockRect (0);
			lm->modified = false;
		}
	}

	// clear down any previous lightmaps
	SAFE_DELETE (d3d_Lightmaps);
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
		D3D_BuildLightmapTextureChains (mod->brushhdr);
	}

	// upload all lightmaps
	D3D_UploadLightmaps ();

	Con_DPrintf ("Done\n");
}


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


void D3D_FillLightmap (msurface_t *surf, byte *dest, int stride)
{
}


void D3D_BuildLightmap (msurface_t *surf)
{
	// cache the light
	surf->cached_dlight = (surf->dlightframe == d3d_RenderDef.framecount);

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
		if (cl.maxclients > 1)
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

			// clear to ambient light
			for (int i = 0; i < size; i++, bl += 3)
			{
				bl[0] = r_ambient.integer * 256;
				bl[1] = r_ambient.integer * 256;
				bl[2] = r_ambient.integer * 256;
			}
		}

		// add all the dynamic lights first
		if (surf->dlightframe == d3d_RenderDef.framecount)
			D3D_AddDynamicLights (surf);

		// add all the lightmaps
		if (lightmap)
		{
			for (int maps = 0; maps < MAXLIGHTMAPS && surf->styles[maps] != 255; maps++)
			{
				unsigned int scale = d_lightstylevalue[surf->styles[maps]];

				surf->cached_light[maps] = scale;
				bl = d3d_BlockLights;

				for (int i = 0; i < size; i++, bl += 3)
				{
					bl[0] += *lightmap++ * scale;
					bl[1] += *lightmap++ * scale;
					bl[2] += *lightmap++ * scale;
				}
			}
		}
	}

	int t;
	int shift = 7 + r_overbright.integer;
	d3d_lightmap_t *lm = &d3d_InternalLightmaps[surf->d3d_Lightmap->LightmapNum];
	bl = d3d_BlockLights;

	if (!lm->modified)
	{
		lm->d3d_Texture->LockRect (0, &lm->d3d_LockedRect, NULL, D3DLOCK_NO_DIRTY_UPDATE);
		lm->modified = true;
	}

	int stride = lm->d3d_LockedRect.Pitch - (surf->smax << 2);
	byte *dest = ((byte *) lm->d3d_LockedRect.pBits) + surf->lightmapoffset;

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

	// get the region to make dirty for this update
	// notice how D3D uses top as 0, and right and bottom rather than
	// width and height, making this so much cleaner (== les bug-prone)
	if (surf->LightRect.left < lm->DirtyRect.left) lm->DirtyRect.left = surf->LightRect.left;
	if (surf->LightRect.top < lm->DirtyRect.top) lm->DirtyRect.top = surf->LightRect.top;
	if (surf->LightRect.right > lm->DirtyRect.right) lm->DirtyRect.right = surf->LightRect.right;
	if (surf->LightRect.bottom > lm->DirtyRect.bottom) lm->DirtyRect.bottom = surf->LightRect.bottom;
}


/*
==================
R_AnimateLight
==================
*/
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
		flight = (int) floor (d3d_RenderDef.time * 10.0f);
		clight = (int) ceil (d3d_RenderDef.time * 10.0f);
		lerpfrac = (d3d_RenderDef.time * 10.0f) - flight;
		backlerp = 1.0f - lerpfrac;

		for (j = 0; j < MAX_LIGHTSTYLES; j++)
		{
			if (!cl_lightstyle[j].length)
			{
				d_lightstylevalue[j] = 256;
				continue;
			}
			else if (cl_lightstyle[j].length == 1)
			{
				// single length style so don't bother interpolating
				d_lightstylevalue[j] = 22 * (cl_lightstyle[j].map[0] - 'a');
				continue;
			}

			// interpolate animating light
			// frame just gone
			k = flight % cl_lightstyle[j].length;
			k = cl_lightstyle[j].map[k] - 'a';
			l = (float) (k * 22) * backlerp;

			// upcoming frame
			k = clight % cl_lightstyle[j].length;
			k = cl_lightstyle[j].map[k] - 'a';
			l += (float) (k * 22) * lerpfrac;

			d_lightstylevalue[j] = (int) l;
		}
	}
	else
	{
		// old light animation
		int i = (int) (d3d_RenderDef.time * 10.0f);

		for (int j = 0; j < MAX_LIGHTSTYLES; j++)
		{
			if (!cl_lightstyle[j].length)
			{
				d_lightstylevalue[j] = 256;
				continue;
			}

			int k = i % cl_lightstyle[j].length;
			k = cl_lightstyle[j].map[k] - 'a';
			k = k * 22;
			d_lightstylevalue[j] = k;
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
=============================================================================

DYNAMIC LIGHTS

=============================================================================
*/

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

loc0:

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
					line3,
					dsfrac = ds & 15,
					dtfrac = dt & 15,
					r00 = 0, g00 = 0, b00 = 0,
					r01 = 0, g01 = 0, b01 = 0,
					r10 = 0, g10 = 0, b10 = 0,
					r11 = 0, g11 = 0, b11 = 0;
				float scale;

				line3 = ((surf->extents[0] >> 4) + 1) * 3;

				// LordHavoc: *3 for color
				lightmap = surf->samples + ((dt >> 4) * ((surf->extents[0] >> 4) + 1) + (ds >> 4)) * 3;

				for (maps = 0; maps < MAXLIGHTMAPS && surf->styles[maps] != 255; maps++)
				{
					// consistent scale
					scale = (float) d_lightstylevalue[surf->styles[maps]] * 1.0 / 264.0f;

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

				color[0] += (float) ((int) ((((((((r11 - r10) * dsfrac) >> 4) + r10) -
													((((r01 - r00) * dsfrac) >> 4) + r00)) * dtfrac) >> 4) + ((((r01 - r00) * dsfrac) >> 4) + r00)));

				color[1] += (float) ((int) ((((((((g11 - g10) * dsfrac) >> 4) + g10) -
													((((g01 - g00) * dsfrac) >> 4) + g00)) * dtfrac) >> 4) + ((((g01 - g00) * dsfrac) >> 4) + g00)));

				color[2] += (float) ((int) ((((((((b11 - b10) * dsfrac) >> 4) + b10) -
													((((b01 - b00) * dsfrac) >> 4) + b00)) * dtfrac) >> 4) + ((((b01 - b00) * dsfrac) >> 4) + b00)));
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
		// no light data
		c[0] = c[1] = c[2] = 255.0f;
		return;
	}

	// set start point
	if (e->model->type == mod_brush)
	{
		// pick top-center point as these can have their origins at one bottom corner
		start[0] = ((e->origin[0] + e->model->mins[0]) + (e->origin[0] + e->model->maxs[0])) / 2;
		start[1] = ((e->origin[1] + e->model->mins[1]) + (e->origin[1] + e->model->maxs[1])) / 2;
		start[2] = e->origin[2] + e->model->maxs[2];
	}
	else
	{
		// same as entity origin
		start[0] = e->origin[0];
		start[1] = e->origin[1];
		start[2] = e->origin[2];
	}

	// set end point
	end[0] = start[0];
	end[1] = start[1];
	end[2] = start[2] - 8192;

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

	// minimum light values (should these be done before or after dynamics???)
	if (e == &cl.viewent) R_MinimumLight (c, 72);
	if (e->entnum >= 1 && e->entnum <= cl.maxclients) R_MinimumLight (c, 24);
	if (e->model->flags & EF_ROTATE) R_MinimumLight (c, 72);
	if (e->model->type == mod_brush) R_MinimumLight (c, 18);

	// add dynamic lights
	if (r_dynamic.value)
	{
		float dlscale = (1.0f / 255.0f) * r_dynamic.value;

		for (int lnum = 0; lnum < MAX_DLIGHTS; lnum++)
		{
			if (cl_dlights[lnum].die >= cl.time)
			{
				VectorSubtract (e->origin, cl_dlights[lnum].origin, dist);

				add = (cl_dlights[lnum].radius - Length (dist));

				if (add > 0)
				{
					c[0] += (add * cl_dlights[lnum].rgb[0]) * dlscale;
					c[1] += (add * cl_dlights[lnum].rgb[1]) * dlscale;
					c[2] += (add * cl_dlights[lnum].rgb[2]) * dlscale;
				}
			}
		}
	}

	// clamp minimum to same level as software Quake uses (5)
	// R_MinimumLight (c, 15);

	// rescale for values of r_overbright
	// this is done before clamping so that we get the correct flattening of light in non-overbright modes
	if (r_overbright.value > 0)
		VectorScale (c, 0.5f, c);
	else if (r_overbright.value > 1)
		VectorScale (c, 0.25f, c);
}


/*
==============================================================================================================================

				LIGHTMAP CLASS

==============================================================================================================================
*/

void CD3DLightmap::CheckSurfaceForModification (msurface_t *surf)
{
	// cache the lightmap back to the surface in case it was rebuilt (is this necessary since we moved to the managed pool?)
	surf->d3d_LightmapTex = d3d_InternalLightmaps[surf->d3d_Lightmap->LightmapNum].d3d_Texture;

	// no lightmaps
	if (surf->flags & SURF_DRAWSKY) return;
	if (surf->flags & SURF_DRAWTURB) return;

	// check for overbright or fullbright modification
	if (surf->overbright != r_overbright.integer) goto ModifyLightmap;
	if (surf->fullbright != r_fullbright.integer) goto ModifyLightmap;
	if (surf->ambient != r_ambient.integer) goto ModifyLightmap;

	// no lightmap modifications
	if (!r_dynamic.value) return;

	// cached lightstyle change
	for (int maps = 0; maps < MAXLIGHTMAPS && surf->styles[maps] != 255; maps++)
		if (d_lightstylevalue[surf->styles[maps]] != surf->cached_light[maps])
			goto ModifyLightmap;

	// dynamic this frame || dynamic previous frame
	if (surf->dlightframe == d3d_RenderDef.framecount || surf->cached_dlight) goto ModifyLightmap;

	// no changes
	return;

ModifyLightmap:;
	// rebuild the lightmap
	D3D_BuildLightmap (surf);
}


void CD3DLightmap::CalcLightmapTexCoords (msurface_t *surf)
{
	// note: _controlfp is ineffective here
	for (int i = 0; i < surf->numverts; i++)
	{
		brushpolyvert_t *vert = &surf->verts[i];

		// (why is this a class member when it has everything to do with the surf and nothing to do with the class?)
		// (because it needs to know the lightmap size, that's why)
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
}


CD3DLightmap::CD3DLightmap (msurface_t *surf)
{
	if (d3d_NumLightmaps == MAX_LIGHTMAPS)
		Sys_Error ("Too many lightmaps!");

	this->LightmapNum = d3d_NumLightmaps;
	d3d_NumLightmaps++;

	// link it in
	this->next = d3d_Lightmaps;
	d3d_Lightmaps = this;

	// we can now reuse lightmap textures so only create one if needed
	if (!d3d_InternalLightmaps[this->LightmapNum].d3d_Texture)
		D3D_CreateNewLightmapTexture (&d3d_InternalLightmaps[this->LightmapNum].d3d_Texture);

	// initial dirty rect
	d3d_InternalLightmaps[this->LightmapNum].DirtyRect.left = LightmapWidth;
	d3d_InternalLightmaps[this->LightmapNum].DirtyRect.right = 0;
	d3d_InternalLightmaps[this->LightmapNum].DirtyRect.top = LightmapHeight;
	d3d_InternalLightmaps[this->LightmapNum].DirtyRect.bottom = 0;

	if (!d3d_InternalLightmaps[this->LightmapNum].allocated)
	{
		d3d_InternalLightmaps[this->LightmapNum].allocated = (int *) MainHunk->Alloc (sizeof (int) * LightmapWidth);
		memset (d3d_InternalLightmaps[this->LightmapNum].allocated, 0, sizeof (int) * LightmapWidth);
	}

	// set the initial texture it was created with
	surf->d3d_LightmapTex = d3d_InternalLightmaps[this->LightmapNum].d3d_Texture;
}


CD3DLightmap::~CD3DLightmap (void)
{
	// cascade destructors
	SAFE_DELETE (this->next);
}


bool CD3DLightmap::AllocBlock (msurface_t *surf)
{
	d3d_lightmap_t *lm = &d3d_InternalLightmaps[this->LightmapNum];

	// potentially suspect construct here...
	do
	{
		int best = LightmapHeight;

		for (int i = 0; i < LightmapWidth - surf->smax; i++)
		{
			int j;
			int best2 = 0;

			for (j = 0; j < surf->smax; j++)
			{
				if (lm->allocated[i + j] >= best) break;
				if (lm->allocated[i + j] > best2) best2 = lm->allocated[i + j];
			}

			if (j == surf->smax)
			{
				// this is a valid spot
				surf->LightRect.left = i;
				surf->LightRect.top = best = best2;
			}
		}

		if (best + surf->tmax > LightmapHeight)
			break;

		for (int i = 0; i < surf->smax; i++)
			lm->allocated[surf->LightRect.left + i] = best + surf->tmax;

		// fill in lightmap right and bottom (these exist just because I'm lazy and don't want to add a few numbers during updates)
		surf->LightRect.right = surf->LightRect.left + surf->smax;
		surf->LightRect.bottom = surf->LightRect.top + surf->tmax;
		surf->lightmapoffset = ((surf->LightRect.top * LightmapWidth + surf->LightRect.left) * 4);

		// set lightmap for the surf
		surf->d3d_Lightmap = this;

		// build the lightmap
		D3D_BuildLightmap (surf);

		// done
		return true;
	} while (false);

	if (this->next)
		return this->next->AllocBlock (surf);
	else return false;
}


