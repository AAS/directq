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
#define LM_BLOCK_SIZE	128

typedef struct d3d_lightmap_s
{
	// the texture object
	LPDIRECT3DTEXTURE9 lm_Texture;

	// size of allocations
	int allocated[LM_BLOCK_SIZE];

	// true if it needs to be re-uploaded
	bool modified;

	// true if locked
	// (note - this, modified and data can be made serve the same purpose)
	bool texturelocked;

	// 1 or 2 (byte or short)
	int lmbytes;

	// pointer to the data
	// (don't expect this to be valid outside of a lock/unlock pair)
	void *data;

	// next lightmap in the chain
	struct d3d_lightmap_s *next;
} d3d_lightmap_t;

d3d_lightmap_t *d3d_Lightmaps = NULL;

unsigned int d3d_blocklights[3][18 * 18];


void D3D_ReleaseLightmaps (void)
{
	for (d3d_lightmap_t *lm = d3d_Lightmaps; lm; lm = lm->next)
	{
		// release the texture
		SAFE_RELEASE (lm->lm_Texture);

		// clear allocations
		memset (lm->allocated, 0, sizeof (lm->allocated));
	}
}


void D3D_CreateLightmapTexture (d3d_lightmap_t *lm)
{
	if (d3d_GlobalCaps.AllowA16B16G16R16)
	{
		// attempt to create it in 64 bit mode
		// there's no RGB version of D3DFMT_A16B16G16R16 so we must use BGR
		// (fixme - allow single component here where the source data is also single component)
		HRESULT hr = d3d_Device->CreateTexture
		(
			LM_BLOCK_SIZE,
			LM_BLOCK_SIZE,
			1,
			0,
			D3DFMT_A16B16G16R16,
			D3DPOOL_MANAGED,
			&lm->lm_Texture,
			NULL
		);

		if (SUCCEEDED (hr))
		{
			lm->lmbytes = 2;
			return;
		}
	}

	// attempt to create it in 32 bit mode
	// we also use BGR here to keep it consistent for simpler code
	HRESULT hr = d3d_Device->CreateTexture
	(
		LM_BLOCK_SIZE,
		LM_BLOCK_SIZE,
		1,
		0,
		D3DFMT_A8R8G8B8,
		D3DPOOL_MANAGED,
		&lm->lm_Texture,
		NULL
	);

	if (FAILED (hr))
	{
		Sys_Error ("D3D_LMAllocBlock: Failed to create a Lightmap Texture");
		return;
	}

	lm->lmbytes = 1;
}


d3d_lightmap_t *D3D_LMAllocBlock (int w, int h, int *x, int *y)
{
	int		i, j;
	int		best, best2;
	int		texnum;
	d3d_lightmap_t *lm;

	for (lm = d3d_Lightmaps; lm; lm = lm->next)
	{
		best = LM_BLOCK_SIZE;

		for (i = 0; i < LM_BLOCK_SIZE - w; i++)
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

		if (best + h > LM_BLOCK_SIZE)
			continue;

		for (i = 0; i < w; i++)
			lm->allocated[*x + i] = best + h;

		if (!lm->texturelocked)
		{
			// the rect we're going to lock
			D3DLOCKED_RECT LockRect;

			// create the lightmap texture
			D3D_CreateLightmapTexture (lm);

			// now we lock the texture rectangle
			lm->lm_Texture->LockRect (0, &LockRect, NULL, 0);

			// obtain a pointer to the data
			lm->data = LockRect.pBits;

			// flag as being locked
			lm->texturelocked = true;
		}

		return lm;
	}

	// didn't find a lightmap so allocate a new one
	lm = (d3d_lightmap_t *) malloc (sizeof (d3d_lightmap_t));

	// link it in
	lm->next = d3d_Lightmaps;
	d3d_Lightmaps = lm;

	// no lightmap texture
	lm->lm_Texture = NULL;

	// texture isn't locked
	lm->texturelocked = false;

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
	int			smax, tmax;
	mtexinfo_t	*tex;

	smax = (surf->extents[0] >> 4) + 1;
	tmax = (surf->extents[1] >> 4) + 1;
	tex = surf->texinfo;

	for (lnum = 0; lnum < MAX_DLIGHTS; lnum++)
	{
		// not hit by this light
		if (!(surf->dlightbits & (1 << lnum))) continue;

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
		
		for (t = 0; t < tmax; t++)
		{
			td = local[1] - t * 16;

			if (td < 0) td = -td;

			for (s = 0; s < smax; s++)
			{
				sd = local[0] - s * 16;

				if (sd < 0) sd = -sd;

				if (sd > td)
					dist = sd + (td >> 1);
				else
					dist = td + (sd >> 1);

				if (dist < minlight)
				{
					d3d_blocklights[0][t * smax + s] += (rad - dist) * 256;
					d3d_blocklights[1][t * smax + s] += (rad - dist) * 256;
					d3d_blocklights[2][t * smax + s] += (rad - dist) * 256;
				}
			}
		}
	}
}


unsigned short D3D_AddBlockLight16 (unsigned int bl)
{
	// we can't just store it in as they can soometimes exceed 65535
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
	int			smax, tmax;
	int			i, j, size;
	byte		*lightmap;
	unsigned	scale;
	int			maps;
	unsigned	*bl;

	d3d_lightmap_t *lm = (d3d_lightmap_t *) surf->d3d_Lightmap;

	surf->cached_dlight = (surf->dlightframe == r_framecount);

	smax = (surf->extents[0] >> 4) + 1;
	tmax = (surf->extents[1] >> 4) + 1;
	size = smax * tmax;
	lightmap = surf->samples;

	// set to full bright if no light data
	if (r_fullbright.value || !cl.worldmodel->lightdata)
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

	// add all the lightmaps
	if (lightmap)
	{
		for (maps = 0; maps < MAXLIGHTMAPS && surf->styles[maps] != 255; maps++)
		{
			scale = d_lightstylevalue[surf->styles[maps]];
			surf->cached_light[maps] = scale;

			for (i = 0; i < size; i++)
			{
				d3d_blocklights[0][i] += lightmap[i] * scale;
				d3d_blocklights[1][i] += lightmap[i] * scale;
				d3d_blocklights[2][i] += lightmap[i] * scale;
			}

			// skip to next lightmap
			lightmap += size;
		}
	}

	// add all the dynamic lights
	if (surf->dlightframe == r_framecount)
		D3D_AddDynamicLights (surf);

store:;
	// retrieve pointers to the blocklights
	unsigned int *blr = d3d_blocklights[0];
	unsigned int *blg = d3d_blocklights[1];
	unsigned int *blb = d3d_blocklights[2];

	// both are 4 component
	stride -= (smax << 2);

	if (lm->lmbytes == 1)
	{
		// 32 bit lightmap texture
		byte *dest = ((byte *) lm->data) + dataofs;

		for (i = 0; i < tmax; i++, dest += stride)
		{
			for (j = 0; j < smax; j++)
			{
				// locked in rgba format
				dest[0] = D3D_AddBlockLight8 (*blr++);
				dest[1] = D3D_AddBlockLight8 (*blg++);
				dest[2] = D3D_AddBlockLight8 (*blb++);
				dest[3] = 255;

				dest += 4;
			}
		}
	}
	else
	{
		// 64 bit lightmap texture
		unsigned short *dest = ((unsigned short *) lm->data) + dataofs;

		for (i = 0; i < tmax; i++, dest += stride)
		{
			for (j = 0; j < smax; j++)
			{
				// locked in rgba format
				dest[0] = D3D_AddBlockLight16 (*blr++);
				dest[1] = D3D_AddBlockLight16 (*blg++);
				dest[2] = D3D_AddBlockLight16 (*blb++);
				dest[3] = 65535;

				dest += 4;
			}
		}
	}
}


void D3D_CreateSurfaceLightmap (msurface_t *surf)
{
	if (surf->flags & SURF_DRAWSKY) return;
	if (surf->flags & SURF_DRAWTURB) return;

	int smax = (surf->extents[0] >> 4) + 1;
	int tmax = (surf->extents[1] >> 4) + 1;

	// create a lightmap for the surf
	surf->d3d_Lightmap = D3D_LMAllocBlock (smax, tmax, &surf->light_s, &surf->light_t);

	// data offset in components, not in bits or bytes!
	int dataofs = (surf->light_t * LM_BLOCK_SIZE + surf->light_s) * 4;

	// build the lightmap
	D3D_BuildLightMap (surf, dataofs, LM_BLOCK_SIZE * 4);
}


void D3D_UploadLightmaps (void)
{
	for (d3d_lightmap_t *lm = d3d_Lightmaps; lm; lm = lm->next)
	{
		// unlock the texture rectangle
		// (we expect this to always be used!)
		if (lm->texturelocked)
		{
			lm->lm_Texture->UnlockRect (0);
			lm->lm_Texture->PreLoad ();
		}

		// set defaults
		lm->modified = false;
		lm->texturelocked = false;
		lm->data = NULL;
	}
}


void D3D_BindLightmap (int stage, void *lm)
{
	D3D_BindTexture (stage, ((d3d_lightmap_t *) lm)->lm_Texture);
}


void D3D_UnlockLightmaps (void)
{
	for (d3d_lightmap_t *lm = d3d_Lightmaps; lm; lm = lm->next)
	{
		// unlock the texture rectangle
		if (lm->texturelocked)
		{
			lm->lm_Texture->UnlockRect (0);
			lm->texturelocked = false;
			lm->data = NULL;
		}
	}
}


void D3D_ModifySurfaceLightmap (msurface_t *surf)
{
	// retrieve the lightmap
	d3d_lightmap_t *lm = (d3d_lightmap_t *) surf->d3d_Lightmap;

	// mark as modified
	lm->modified = true;

	if (!lm->texturelocked)
	{
		// lock the texture rectangle
		// we don't know in advance how much of it we need to lock, so we just lock it all!
		// this might be where 64 * 64 lightmaps are a good idea - ensuring that we always
		// lock a small amount... the max surf lightmap size is 18, so we could even go down
		// as far as 32 * 32...
		D3DLOCKED_RECT LockRect;

		// lock it
		lm->lm_Texture->LockRect (0, &LockRect, NULL, 0);

		// retrieve the data pointer
		lm->data = LockRect.pBits;

		// flag as locked
		lm->texturelocked = true;
	}

	int smax = (surf->extents[0] >> 4) + 1;
	int tmax = (surf->extents[1] >> 4) + 1;

	// data offset in components, not in bits or bytes!
	int dataofs = (surf->light_t * LM_BLOCK_SIZE + surf->light_s) * 4;

	// rebuild the lightmap
	D3D_BuildLightMap (surf, dataofs, LM_BLOCK_SIZE * 4);
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
	st[0] += surf->light_s * 16;
	st[0] += 8;
	st[0] /= LM_BLOCK_SIZE * 16;

	st[1] = DotProduct (v, surf->texinfo->vecs[1]) + surf->texinfo->vecs[1][3];
	st[1] -= surf->texturemins[1];
	st[1] += surf->light_t * 16;
	st[1] += 8;
	st[1] /= LM_BLOCK_SIZE * 16;
}


/*
==================
R_AnimateLight
==================
*/
void R_AnimateLight (void)
{
	// to do - make this cvar-controllable!
	if (1)
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

/*
=============================================================================

DYNAMIC LIGHTS BLEND RENDERING

=============================================================================
*/

void AddLightBlend (float r, float g, float b, float a2)
{
	/*
	float	a;

	v_blend[3] = a = v_blend[3] + a2*(1-v_blend[3]);

	a2 = a2/a;

	v_blend[0] = v_blend[1]*(1-a2) + r*a2;
	v_blend[1] = v_blend[1]*(1-a2) + g*a2;
	v_blend[2] = v_blend[2]*(1-a2) + b*a2;
	*/
}


void R_RenderDlight (dlight_t *light)
{
	/*
	int		i, j;
	float	a;
	vec3_t	v;
	float	rad;

	rad = light->radius * 0.35;

	VectorSubtract (light->origin, r_origin, v);
	if (Length (v) < rad)
	{	// view is inside the dlight
		AddLightBlend (1, 0.5, 0, light->radius * 0.0003);
		return;
	}

	glBegin (GL_TRIANGLE_FAN);
	glColor3f (0.2,0.1,0.0);
	for (i=0 ; i<3 ; i++)
		v[i] = light->origin[i] - vpn[i]*rad;
	glVertex3fv (v);
	glColor3f (0,0,0);
	for (i=16 ; i>=0 ; i--)
	{
		a = i/16.0 * M_PI*2;
		for (j=0 ; j<3 ; j++)
			v[j] = light->origin[j] + vright[j]*cos(a)*rad
				+ vup[j]*sin(a)*rad;
		glVertex3fv (v);
	}
	glEnd ();
	*/
}

/*
=============
R_RenderDlights
=============
*/
void R_RenderDlights (void)
{
	int		i;
	dlight_t	*l;

	if (!gl_flashblend.value)
		return;

	/*
	glDepthMask (0);
	glDisable (GL_TEXTURE_2D);
	glShadeModel (GL_SMOOTH);
	glEnable (GL_BLEND);
	glBlendFunc (GL_ONE, GL_ONE);

	l = cl_dlights;
	for (i=0 ; i<MAX_DLIGHTS ; i++, l++)
	{
		if (l->die < cl.time || !l->radius)
			continue;
		R_RenderDlight (l);
	}

	glColor3f (1,1,1);
	glDisable (GL_BLEND);
	glEnable (GL_TEXTURE_2D);
	glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDepthMask (1);
	*/
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
void R_MarkLights (dlight_t *light, int bit, mnode_t *node)
{
	mplane_t	*splitplane;
	float		dist;
	msurface_t	*surf;
	int			i;
	
	if (node->contents < 0)
		return;

	splitplane = node->plane;
	dist = DotProduct (light->origin, splitplane->normal) - splitplane->dist;
	
	if (dist > light->radius)
	{
		R_MarkLights (light, bit, node->children[0]);
		return;
	}
	if (dist < -light->radius)
	{
		R_MarkLights (light, bit, node->children[1]);
		return;
	}
		
// mark the polygons
	surf = cl.worldmodel->surfaces + node->firstsurface;

	for (i=0 ; i<node->numsurfaces ; i++, surf++)
	{
		if (surf->dlightframe != r_framecount)
		{
			surf->dlightbits = 0;
			surf->dlightframe = r_framecount;
		}

		surf->dlightbits |= bit;
	}

	R_MarkLights (light, bit, node->children[0]);
	R_MarkLights (light, bit, node->children[1]);
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

	if (gl_flashblend.value) return;

	l = cl_dlights;

	for (i = 0; i < MAX_DLIGHTS; i++, l++)
	{
		if (l->die < cl.time || !l->radius)
			continue;

		R_MarkLights (l, 1 << i, headnode);
	}
}


/*
=============================================================================

LIGHT SAMPLING

=============================================================================
*/

mplane_t		*lightplane;
vec3_t			lightspot;

int RecursiveLightPoint (mnode_t *node, vec3_t start, vec3_t end)
{
	int			r;
	float		front, back, frac;
	int			side;
	mplane_t	*plane;
	vec3_t		mid;
	msurface_t	*surf;
	int			s, t, ds, dt;
	int			i;
	mtexinfo_t	*tex;
	byte		*lightmap;
	unsigned	scale;
	int			maps;

	if (node->contents < 0)
		return -1;		// didn't hit anything
	
// calculate mid point

// FIXME: optimize for axial
	plane = node->plane;
	front = DotProduct (start, plane->normal) - plane->dist;
	back = DotProduct (end, plane->normal) - plane->dist;
	side = front < 0;
	
	if ( (back < 0) == side)
		return RecursiveLightPoint (node->children[side], start, end);
	
	frac = front / (front-back);
	mid[0] = start[0] + (end[0] - start[0])*frac;
	mid[1] = start[1] + (end[1] - start[1])*frac;
	mid[2] = start[2] + (end[2] - start[2])*frac;
	
// go down front side	
	r = RecursiveLightPoint (node->children[side], start, mid);
	if (r >= 0)
		return r;		// hit something
		
	if ( (back < 0) == side )
		return -1;		// didn't hit anuthing
		
// check for impact on this node
	VectorCopy (mid, lightspot);
	lightplane = plane;

	surf = cl.worldmodel->surfaces + node->firstsurface;
	for (i=0 ; i<node->numsurfaces ; i++, surf++)
	{
		if (surf->flags & SURF_DRAWTILED)
			continue;	// no lightmaps

		tex = surf->texinfo;
		
		s = DotProduct (mid, tex->vecs[0]) + tex->vecs[0][3];
		t = DotProduct (mid, tex->vecs[1]) + tex->vecs[1][3];;

		if (s < surf->texturemins[0] ||
		t < surf->texturemins[1])
			continue;
		
		ds = s - surf->texturemins[0];
		dt = t - surf->texturemins[1];
		
		if ( ds > surf->extents[0] || dt > surf->extents[1] )
			continue;

		if (!surf->samples)
			return 0;

		ds >>= 4;
		dt >>= 4;

		lightmap = surf->samples;
		r = 0;
		if (lightmap)
		{

			lightmap += dt * ((surf->extents[0]>>4)+1) + ds;

			for (maps = 0 ; maps < MAXLIGHTMAPS && surf->styles[maps] != 255 ;
					maps++)
			{
				scale = d_lightstylevalue[surf->styles[maps]];
				r += *lightmap * scale;
				lightmap += ((surf->extents[0]>>4)+1) *
						((surf->extents[1]>>4)+1);
			}
			
			r >>= 8;
		}
		
		return r;
	}

// go down back side
	return RecursiveLightPoint (node->children[!side], mid, end);
}


void R_LightPoint (entity_t *e, int *c)
{
	vec3_t		end;
	int			r;
	int			lnum;
	int			add;
	vec3_t		dist;

	if (!cl.worldmodel->lightdata)
	{
		c[0] = 255;
		return;
	}

	end[0] = e->origin[0];
	end[1] = e->origin[1];
	end[2] = e->origin[2] - 2048;

	r = RecursiveLightPoint (cl.worldmodel->nodes, e->origin, end);

	for (lnum = 0; lnum < MAX_DLIGHTS; lnum++)
	{
		if (cl_dlights[lnum].die >= cl.time)
		{
			VectorSubtract (e->origin, cl_dlights[lnum].origin, dist);

			add = (cl_dlights[lnum].radius - Length (dist));

			if (add > 0) r += add;
		}
	}

	if (r == -1) r = 0;

	c[0] = r;
}

