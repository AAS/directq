/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
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
#include "d3d_quake.h"

// 256 might be more efficient for rendering, but it will mean that most lightmaps are hit in any given frame
// we might yet take it down to 64, but right now 128 seems a good balance...
#define LIGHTMAP_SIZE	128

cvar_t r_lerplightstyle ("r_lerplightstyle", "1", CVAR_ARCHIVE);
cvar_t r_monolight ("r_monolight", "1", CVAR_ARCHIVE);

int r_numlightmaps = 0;


typedef struct d3d_lightmap_s
{
	// the texture object
	LPDIRECT3DTEXTURE9 lm_Texture;

	// size of allocations
	int allocated[LIGHTMAP_SIZE];

	// dirty region
	RECT DirtyRect;

	// locked texture rectangle
	D3DLOCKED_RECT LockedRect;

	// data pointer
	// (don't expect this to be valid outside of a lock/unlock pair)
	void *data;

	// true if modified
	bool modified;

	// 1 or 2 (byte or short)
	int lmbytes;

	// lightmap number
	int lmnum;

	// next lightmap in the chain
	struct d3d_lightmap_s *next;
} d3d_lightmap_t;

d3d_lightmap_t *d3d_Lightmaps = NULL;

// unbounded
unsigned int **d3d_blocklights;

void D3D_CreateBlockLights (void)
{
	extern int MaxExtents[];

	// get max blocklight size
	int blsize = ((MaxExtents[0] >> 4) + 1) * ((MaxExtents[1] >> 4) + 1);

	// rgb array
	d3d_blocklights = (unsigned int **) Heap_TagAlloc (TAG_BRUSHMODELS, sizeof (int *) * 3);

	// each component
	d3d_blocklights[0] = (unsigned int *) Heap_TagAlloc (TAG_BRUSHMODELS, sizeof (int) * blsize);
	d3d_blocklights[1] = (unsigned int *) Heap_TagAlloc (TAG_BRUSHMODELS, sizeof (int) * blsize);
	d3d_blocklights[2] = (unsigned int *) Heap_TagAlloc (TAG_BRUSHMODELS, sizeof (int) * blsize);
}


void D3D_ReleaseLightmaps (void)
{
	for (d3d_lightmap_t *lm = d3d_Lightmaps; lm; lm = lm->next)
	{
		if (lm->lm_Texture)
		{
			// unlock, release and NULL
			lm->lm_Texture->UnlockRect (0);
			lm->data = NULL;
			lm->lm_Texture->Release ();
			lm->lm_Texture = NULL;
		}

		// clear allocations
		memset (lm->allocated, 0, sizeof (lm->allocated));

		// flag as unmodified
		lm->modified = false;
	}

	r_numlightmaps = 0;
}


void D3D_CreateLightmapTexture (d3d_lightmap_t *lm)
{
	extern cvar_t r_64bitlightmaps;

	if (d3d_GlobalCaps.AllowA16B16G16R16 && r_64bitlightmaps.integer)
	{
		// attempt to create it in 64 bit mode
		// there's no RGB version of D3DFMT_A16B16G16R16 so we must use BGR
		// (fixme - allow single component here where the source data is also single component)
		// (fixme - this needs to work with the r_monolightmap cvar too...)
		HRESULT hr = d3d_Device->CreateTexture
		(
			LIGHTMAP_SIZE,
			LIGHTMAP_SIZE,
			1,
			0,
			D3DFMT_A16B16G16R16,
			D3DPOOL_MANAGED,
			&lm->lm_Texture,
			NULL
		);

		if (SUCCEEDED (hr))
		{
			lm->lmbytes = 8;
			return;
		}
	}

	// attempt to create it in 32 bit mode
	// (fixme - allow single component here where the source data is also single component)
	// (fixme - this needs to work with the r_monolightmap cvar too...)
	HRESULT hr = d3d_Device->CreateTexture
	(
		LIGHTMAP_SIZE,
		LIGHTMAP_SIZE,
		1,
		0,
		D3DFMT_A8R8G8B8,
		D3DPOOL_MANAGED,
		&lm->lm_Texture,
		NULL
	);

	if (FAILED (hr))
	{
		Sys_Error ("D3D_CreateLightmapTexture: Failed to create a Lightmap Texture");
		return;
	}

	lm->lmbytes = 4;
}


d3d_lightmap_t *D3D_LMAllocBlock (int w, int h, byte *x, byte *y)
{
	int		i, j;
	int		best, best2;
	int		texnum;
	d3d_lightmap_t *lm;

	for (lm = d3d_Lightmaps; lm; lm = lm->next)
	{
		best = LIGHTMAP_SIZE;

		for (i = 0; i < LIGHTMAP_SIZE - w; i++)
		{
			best2 = 0;

			for (j = 0; j < w; j++)
			{
				if (lm->allocated[i + j] >= best) break;
				if (lm->allocated[i + j] > best2) best2 = lm->allocated[i + j];
			}

			if (j == w)
			{
				// this is a valid spot
				*x = i;
				*y = best = best2;
			}
		}

		if (best + h > LIGHTMAP_SIZE)
			continue;

		for (i = 0; i < w; i++)
			lm->allocated[*x + i] = best + h;

		if (!lm->modified)
		{
			// create the lightmap texture
			D3D_CreateLightmapTexture (lm);

			// now we lock the texture rectangle with D3DLOCK_NO_DIRTY_UPDATE because we want to manage the dirty regions ourselves
			lm->lm_Texture->LockRect (0, &lm->LockedRect, NULL, D3DLOCK_NO_DIRTY_UPDATE);
			lm->data = lm->LockedRect.pBits;

			// flag as being modified here
			lm->modified = true;
		}

		return lm;
	}

	// didn't find a lightmap so allocate a new one
	lm = (d3d_lightmap_t *) Heap_QMalloc (sizeof (d3d_lightmap_t));

	// link it in
	lm->next = d3d_Lightmaps;
	d3d_Lightmaps = lm;

	// no lightmap texture
	lm->lm_Texture = NULL;

	// texture isn't locked
	lm->modified = false;

	// set number
	lm->lmnum = r_numlightmaps++;

	// clear allocations
	memset (lm->allocated, 0, sizeof (lm->allocated));

	// call recursively to get the new one we've just allocated
	return D3D_LMAllocBlock (w, h, x, y);
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

	for (lnum = 0; lnum < MAX_DLIGHTS; lnum++)
	{
		// light is dead
		if (cl_dlights[lnum].die < cl.time) continue;

		// not hit by this light
		if (!(surf->dlightbits[lnum >> 5] & (1 << (lnum & 31)))) continue;

		rad = cl_dlights[lnum].radius;

		dist = DotProduct (cl_dlights[lnum].origin, surf->plane->normal) - surf->plane->dist;

		rad -= fabs(dist);
		minlight = cl_dlights[lnum].minlight;

		if (rad < minlight) continue;

		minlight = rad - minlight;

		for (i = 0; i < 3; i++)
			impact[i] = cl_dlights[lnum].origin[i] - surf->plane->normal[i] * dist;

		local[0] = DotProduct (impact, tex->vecs[0]) + tex->vecs[0][3];
		local[1] = DotProduct (impact, tex->vecs[1]) + tex->vecs[1][3];

		local[0] -= surf->texturemins[0];
		local[1] -= surf->texturemins[1];
		
		for (t = 0; t < surf->tmax; t++)
		{
			td = local[1] - t * 16;
			int tsmax = (int) surf->smax * t;

			if (td < 0) td = -td;

			for (s = 0; s < surf->smax; s++, tsmax++)
			{
				sd = local[0] - s * 16;

				if (sd < 0) sd = -sd;

				if (sd > td)
					dist = sd + (td >> 1);
				else
					dist = td + (sd >> 1);

				if (dist < minlight)
				{
					d3d_blocklights[0][tsmax] += (rad - dist) * cl_dlights[lnum].rgb[0];
					d3d_blocklights[1][tsmax] += (rad - dist) * cl_dlights[lnum].rgb[1];
					d3d_blocklights[2][tsmax] += (rad - dist) * cl_dlights[lnum].rgb[2];
				}
			}
		}
	}
}


unsigned short D3D_AddBlockLight16 (unsigned int bl)
{
	// we can't just store it in as they can sometimes exceed 65535
	// this still gives 2^6 * GLQuake granularity, but it needs a modulate 4x blend
	return (bl > 131071) ? 65535 : bl >> 1;
}

byte D3D_AddBlockLight8 (unsigned int bl)
{
	// back to regular granularity
	// note - modulate 2x blend is needed here... i feel a global coming on...
	return (byte) (bl > 65280 ? 255 : bl >> 8);
}

void D3D_BuildLightMap (msurface_t *surf, int dataofs, int stride)
{
	int			i, j, size;
	byte		*lightmap;
	unsigned	scale;
	int			maps;
	unsigned	*bl;

	d3d_lightmap_t *lm = (d3d_lightmap_t *) surf->d3d_Lightmap;
	surf->cached_dlight = (surf->dlightframe == r_framecount);

	size = surf->smax * surf->tmax;
	lightmap = surf->samples;

	// set to full bright if no light data
	if (r_fullbright.value || !cl.worldbrush->lightdata)
	{
		for (i = 0; i < size; i++)
		{
			d3d_blocklights[0][i] = 255 * 256;
			d3d_blocklights[1][i] = 255 * 256;
			d3d_blocklights[2][i] = 255 * 256;
		}

		goto store;
	}

	// clear to no light
	for (i = 0; i < size; i++)
	{
		d3d_blocklights[0][i] = 0;
		d3d_blocklights[1][i] = 0;
		d3d_blocklights[2][i] = 0;
	}

	// add all the dynamic lights first
	if (surf->dlightframe == r_framecount)
		D3D_AddDynamicLights (surf);

	// add all the lightmaps
	if (lightmap)
	{
		for (maps = 0; maps < MAXLIGHTMAPS && surf->styles[maps] != 255; maps++)
		{
			scale = d_lightstylevalue[surf->styles[maps]];
			surf->cached_light[maps] = scale;

			for (i = 0; i < size; i++)
			{
				d3d_blocklights[0][i] += *lightmap++ * scale;
				d3d_blocklights[1][i] += *lightmap++ * scale;
				d3d_blocklights[2][i] += *lightmap++ * scale;
			}
		}
	}

store:;
	// retrieve pointers to the blocklights
	unsigned int *blr = d3d_blocklights[0];
	unsigned int *blg = d3d_blocklights[1];
	unsigned int *blb = d3d_blocklights[2];

	// both are 4 component
	stride -= (surf->smax << 2);

	if (lm->lmbytes == 8)
	{
		// 64 bit lightmap texture
		unsigned short *dest = ((unsigned short *) lm->data) + dataofs;

		for (i = 0; i < surf->tmax; i++, dest += stride)
		{
			for (j = 0; j < surf->smax; j++)
			{
				// note - this is the opposite to what you think it should be.  ABGR format = upload in RGBA
				dest[0] = D3D_AddBlockLight16 (*blr++);
				dest[1] = D3D_AddBlockLight16 (*blg++);
				dest[2] = D3D_AddBlockLight16 (*blb++);
				dest[3] = 65535;

				dest += 4;
			}
		}
	}
	else if (lm->lmbytes == 4)
	{
		// 32 bit lightmap texture
		byte *dest = ((byte *) lm->data) + dataofs;

		for (i = 0; i < surf->tmax; i++, dest += stride)
		{
			for (j = 0; j < surf->smax; j++)
			{
				// note - this is the opposite to what you think it should be.  ARGB format = upload in BGRA
				dest[2] = D3D_AddBlockLight8 (*blr++);
				dest[1] = D3D_AddBlockLight8 (*blg++);
				dest[0] = D3D_AddBlockLight8 (*blb++);
				dest[3] = 255;

				dest += 4;
			}
		}
	}
	else if (lm->lmbytes == 2)
	{
	}
	else if (lm->lmbytes == 1)
	{
	}
	else Sys_Error ("D3D_BuildLightMap: unknown lightmap byte size");
}


void D3D_CreateSurfaceLightmap (msurface_t *surf)
{
	if (surf->flags & SURF_DRAWSKY) return;
	if (surf->flags & SURF_DRAWTURB) return;

	// fill in lightmap extents
	surf->smax = (surf->extents[0] >> 4) + 1;
	surf->tmax = (surf->extents[1] >> 4) + 1;

	// create a lightmap for the surf (also fills in light_l and light_t
	surf->d3d_Lightmap = D3D_LMAllocBlock (surf->smax, surf->tmax, &surf->light_l, &surf->light_t);

	// fill in lightmap right and bottom (these exist just because I'm lazy and don't want to add a few numbers during updates)
	surf->light_r = surf->light_l + surf->smax;
	surf->light_b = surf->light_t + surf->tmax;

	// data offset in components, not in bits or bytes!
	int dataofs = (surf->light_t * LIGHTMAP_SIZE + surf->light_l) * 4;

	// build the lightmap
	D3D_BuildLightMap (surf, dataofs, LIGHTMAP_SIZE * 4);
}


void D3D_UploadLightmaps (void)
{
	// upload all lightmaps
	for (d3d_lightmap_t *lm = d3d_Lightmaps; lm; lm = lm->next)
	{
		// because we only delete textures rather than d3d_lightmap_t structs we need to check this
		// (should be true for every lightmap we build)
		if (lm->modified)
		{
			// mark the entire texture as dirty
			lm->lm_Texture->AddDirtyRect (NULL);

			// unlock it
			lm->lm_Texture->UnlockRect (0);
			lm->data = NULL;

			// tell D3D we're going to need this managed resource shortly
			lm->lm_Texture->PreLoad ();
		}

		// dirty region is nothing at the start
		lm->DirtyRect.left = LIGHTMAP_SIZE;
		lm->DirtyRect.right = 0;
		lm->DirtyRect.top = LIGHTMAP_SIZE;
		lm->DirtyRect.bottom = 0;

		// not modified by default
		lm->modified = false;
	}
}


LPDIRECT3DTEXTURE9 D3D_GetLightmap (void *lm)
{
	return ((d3d_lightmap_t *) lm)->lm_Texture;
}


void D3D_UnlockLightmaps (void)
{
	for (d3d_lightmap_t *lm = d3d_Lightmaps; lm; lm = lm->next)
	{
		// see was it modified
		if (lm->modified)
		{
			// add a dirty region for that which was modified
			lm->lm_Texture->AddDirtyRect (&lm->DirtyRect);
			lm->lm_Texture->UnlockRect (0);
			lm->data = NULL;

			// not modified now
			lm->modified = false;

			// reset dirty region
			lm->DirtyRect.left = LIGHTMAP_SIZE;
			lm->DirtyRect.right = 0;
			lm->DirtyRect.top = LIGHTMAP_SIZE;
			lm->DirtyRect.bottom = 0;
		}
	}
}


void D3D_ModifySurfaceLightmap (msurface_t *surf)
{
	// retrieve the lightmap
	d3d_lightmap_t *lm = (d3d_lightmap_t *) surf->d3d_Lightmap;

	if (!lm->modified)
	{
		// flag as modified
		lm->lm_Texture->LockRect (0, &lm->LockedRect, NULL, D3DLOCK_NO_DIRTY_UPDATE);
		lm->data = lm->LockedRect.pBits;
		lm->modified = true;
	}

	// get the region to make dirty for this update
	// notice how D3D uses top as 0, and right and bottom rather than width and height, making this so much cleaner (== les bug-prone)
	if (surf->light_l < lm->DirtyRect.left) lm->DirtyRect.left = surf->light_l;
	if (surf->light_t < lm->DirtyRect.top) lm->DirtyRect.top = surf->light_t;
	if (surf->light_r > lm->DirtyRect.right) lm->DirtyRect.right = surf->light_r;
	if (surf->light_b > lm->DirtyRect.bottom) lm->DirtyRect.bottom = surf->light_b;

	// data offset in components, not in bits or bytes!
	int dataofs = (surf->light_t * LIGHTMAP_SIZE + surf->light_l) * 4;

	// rebuild the lightmap
	D3D_BuildLightMap (surf, dataofs, LIGHTMAP_SIZE * 4);
}


void D3D_CheckLightmapModification (msurface_t *surf)
{
	// no lightmaps
	if (surf->flags & SURF_DRAWSKY) return;
	if (surf->flags & SURF_DRAWTURB) return;

	// no lightmap modifications
	if (!r_dynamic.value) return;

	// check for lightmap modification - cached lightstyle change
	for (int maps = 0; maps < MAXLIGHTMAPS && surf->styles[maps] != 255; maps++)
	{
		if (d_lightstylevalue[surf->styles[maps]] != surf->cached_light[maps])
		{
			D3D_ModifySurfaceLightmap (surf);
			return;
		}
	}

	// dynamic this frame || dynamic previous frame
	if (surf->dlightframe == r_framecount || surf->cached_dlight) D3D_ModifySurfaceLightmap (surf);
}


void D3D_MakeLightmapTexCoords (msurface_t *surf, float *v, float *st)
{
	st[0] = DotProduct (v, surf->texinfo->vecs[0]) + surf->texinfo->vecs[0][3];
	st[0] -= surf->texturemins[0];
	st[0] += (int) surf->light_l * 16;
	st[0] += 8;
	st[0] /= (float) (LIGHTMAP_SIZE * 16);

	st[1] = DotProduct (v, surf->texinfo->vecs[1]) + surf->texinfo->vecs[1][3];
	st[1] -= surf->texturemins[1];
	st[1] += (int) surf->light_t * 16;
	st[1] += 8;
	st[1] /= (float) (LIGHTMAP_SIZE * 16);
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
		flight = (int) floor (cl.time * 10);
		clight = (int) ceil (cl.time * 10);
		lerpfrac = (cl.time * 10) - flight;
		backlerp = 1.0f - lerpfrac;

		for (j = 0; j < MAX_LIGHTSTYLES; j++)
		{
			if (!cl_lightstyle[j].length)
			{
				// was 256, changed to 264 for consistency
				d_lightstylevalue[j] = 264;
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
		int			i,j,k;

		i = (int)(cl.time*10);
		for (j=0 ; j<MAX_LIGHTSTYLES ; j++)
		{
			if (!cl_lightstyle[j].length)
			{
				d_lightstylevalue[j] = 264;
				continue;
			}

			k = i % cl_lightstyle[j].length;
			k = cl_lightstyle[j].map[k] - 'a';
			k = k*22;
			d_lightstylevalue[j] = k;
		}
	}
}


void R_ColourDLight (dlight_t *dl, unsigned short r, unsigned short g, unsigned short b)
{
	// leave dlight with white value it had at allocation
	if (r_monolight.value) return;

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

	if (node->contents < 0) return;

	splitplane = node->plane;
	dist = DotProduct (light->origin, splitplane->normal) - splitplane->dist;

	if (dist > light->radius)
	{
		R_MarkLights (light, num, node->children[0]);
		return;
	}

	if (dist < -light->radius)
	{
		R_MarkLights (light, num, node->children[1]);
		return;
	}

	// mark the polygons
	surf = cl.worldbrush->surfaces + node->firstsurface;

	for (i = 0; i < node->numsurfaces; i++, surf++)
	{
		// no lights on these
		if (surf->flags & SURF_DRAWTURB) continue;
		if (surf->flags & SURF_DRAWSKY) continue;

		// skip backfacing surfs
		//if ((dist = DotProduct (light->origin, surf->plane->normal) - surf->plane->dist) >= 0)
		//	sidebit = 0;
		//else sidebit = SURF_PLANEBACK;

		//if ((surf->flags & SURF_PLANEBACK) != sidebit) continue;

		if (surf->dlightframe != r_framecount)
		{
			// first time hit
			surf->dlightbits[0] = surf->dlightbits[1] = surf->dlightbits[2] = surf->dlightbits[3] = 0;
			surf->dlightframe = r_framecount;
		}

		// mark the surf for this dlight
		surf->dlightbits[num >> 5] |= 1 << (num & 31);
	}

	R_MarkLights (light, num, node->children[0]);
	R_MarkLights (light, num, node->children[1]);
}


/*
=============
R_PushDlights
=============
*/
void R_PushDlights (mnode_t *headnode)
{
	int		i;
	dlight_t	*l;

	l = cl_dlights;

	for (i = 0; i < MAX_DLIGHTS; i++, l++)
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
		front = DotProduct(start, node->plane->normal) - node->plane->dist;
		back = DotProduct(end, node->plane->normal) - node->plane->dist;
	}

	// LordHavoc: optimized recursion
	if ((back < 0) == (front < 0))
	{
		node = node->children[front < 0];
		goto loc0;
	}

	frac = front / (front-back);
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

		surf = cl.worldbrush->surfaces + node->firstsurface;

		for (i = 0; i < node->numsurfaces; i++, surf++)
		{
			// no lightmaps
			if (surf->flags & SURF_DRAWTILED) continue;

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
					r00 = 0, 
					g00 = 0,
					b00 = 0,
					r01 = 0,
					g01 = 0,
					b01 = 0,
					r10 = 0,
					g10 = 0,
					b10 = 0,
					r11 = 0,
					g11 = 0,
					b11 = 0;
				float scale;

				line3 = ((surf->extents[0] >> 4) + 1) * 3;

				// LordHavoc: *3 for color
				lightmap = surf->samples + ((dt >> 4) * ((surf->extents[0] >> 4) + 1) + (ds >> 4)) * 3;

				for (maps = 0; maps < MAXLIGHTMAPS && surf->styles[maps] != 255; maps++)
				{
					scale = (float) d_lightstylevalue[surf->styles[maps]] * 1.0 / 256.0;

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


void R_LightPoint (entity_t *e, float *c)
{
	vec3_t		start;
	vec3_t		end;
	int			lnum;
	float		add;
	vec3_t		dist;

	if (!cl.worldbrush->lightdata)
	{
		// no light data
		if (e->model->type == mod_brush)
			c[0] = c[1] = c[2] = 1.0f;
		else c[0] = c[1] = c[2] = 255.0f;

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
	end[0] = e->origin[0];
	end[1] = e->origin[1];
	end[2] = e->origin[2] - 8192;

	// initially nothing
	c[0] = c[1] = c[2] = 0;

	// get lighting
	R_RecursiveLightPoint (c, cl.worldbrush->nodes, start, end);

	// minimum light value on gun (24)
	if (e == &cl.viewent)
	{
		add = 72.0f - (c[0] + c[1] + c[2]);

		if (add > 0.0f)
		{
			c[0] += add / 3.0f;
			c[1] += add / 3.0f;
			c[2] += add / 3.0f;
		}
	}

	// minimum light value on players (8)
	if (e->entnum >= 1 && e->entnum <= cl.maxclients)
	{
		add = 24.0f - (c[0] + c[1] + c[2]);

		if (add > 0.0f)
		{
			c[0] += add / 3.0f;
			c[1] += add / 3.0f;
			c[2] += add / 3.0f;
		}
	}

	// always highlight pickups (24)
	if (e->model->flags & EF_ROTATE)
	{
		add = 72.0f - (c[0] + c[1] + c[2]);

		if (add > 0.0f)
		{
			c[0] += add / 3.0f;
			c[1] += add / 3.0f;
			c[2] += add / 3.0f;
		}
	}

	// add dynamic lights
	for (lnum = 0; lnum < MAX_DLIGHTS; lnum++)
	{
		if (cl_dlights[lnum].die >= cl.time)
		{
			VectorSubtract (e->origin, cl_dlights[lnum].origin, dist);

			add = (cl_dlights[lnum].radius - Length (dist));

			if (add > 0)
			{
				c[0] += (add * cl_dlights[lnum].rgb[0]) / 255.0f;
				c[1] += (add * cl_dlights[lnum].rgb[1]) / 255.0f;
				c[2] += (add * cl_dlights[lnum].rgb[2]) / 255.0f;
			}
		}
	}

	// if a brush model we divide by 255 - note - the "no light data" setting above will still be OK (I think...!)
	if (e->model->type == mod_brush)
	{
		c[0] /= 255;
		c[1] /= 255;
		c[2] /= 255;
	}
}


