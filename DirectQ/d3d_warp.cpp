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
// gl_warp.c -- sky and water polygons

#include "quakedef.h"
#include "d3d_quake.h"

extern	model_t	*loadmodel;

int		skytexturenum;

LPDIRECT3DTEXTURE9 solidskytexture = NULL;
LPDIRECT3DTEXTURE9 alphaskytexture = NULL;
LPDIRECT3DVERTEXBUFFER9 d3d_SkySphereVerts = NULL;

typedef struct skysphere_s
{
	float xyz[3];
	float st[2];
} skysphere_t;

msurface_t	*warpface;

extern cvar_t gl_subdivide_size;
extern cvar_t r_oldsky;

void BoundPoly (int numverts, float *verts, vec3_t mins, vec3_t maxs)
{
	int		i, j;
	float	*v;

	mins[0] = mins[1] = mins[2] = 9999;
	maxs[0] = maxs[1] = maxs[2] = -9999;
	v = verts;
	for (i=0 ; i<numverts ; i++)
		for (j=0 ; j<3 ; j++, v++)
		{
			if (*v < mins[j])
				mins[j] = *v;
			if (*v > maxs[j])
				maxs[j] = *v;
		}
}

void SubdividePolygon (int numverts, float *verts)
{
	int		i, j, k;
	vec3_t	mins, maxs;
	float	m;
	float	*v;
	vec3_t	front[64], back[64];
	int		f, b;
	float	dist[64];
	float	frac;
	glpoly_t	*poly;

	if (numverts > 60)
		Sys_Error ("numverts = %i", numverts);

	BoundPoly (numverts, verts, mins, maxs);

	for (i=0 ; i<3 ; i++)
	{
		m = (mins[i] + maxs[i]) * 0.5;
		m = gl_subdivide_size.value * floor (m/gl_subdivide_size.value + 0.5);
		if (maxs[i] - m < 8)
			continue;
		if (m - mins[i] < 8)
			continue;

		// cut it
		v = verts + i;
		for (j=0 ; j<numverts ; j++, v+= 3)
			dist[j] = *v - m;

		// wrap cases
		dist[j] = dist[0];
		v-=i;
		VectorCopy (verts, v);

		f = b = 0;
		v = verts;
		for (j=0 ; j<numverts ; j++, v+= 3)
		{
			if (dist[j] >= 0)
			{
				VectorCopy (v, front[f]);
				f++;
			}
			if (dist[j] <= 0)
			{
				VectorCopy (v, back[b]);
				b++;
			}
			if (dist[j] == 0 || dist[j+1] == 0)
				continue;
			if ( (dist[j] > 0) != (dist[j+1] > 0) )
			{
				// clip point
				frac = dist[j] / (dist[j] - dist[j+1]);
				for (k=0 ; k<3 ; k++)
					front[f][k] = back[b][k] = v[k] + frac*(v[3+k] - v[k]);
				f++;
				b++;
			}
		}

		SubdividePolygon (f, front[0]);
		SubdividePolygon (b, back[0]);
		return;
	}

	poly = (glpoly_t *) Hunk_Alloc (sizeof(glpoly_t) + (numverts-4) * VERTEXSIZE*sizeof(float));
	poly->next = warpface->polys;
	warpface->polys = poly;
	poly->numverts = numverts;

	for (i = 0; i < numverts; i++, verts += 3)
	{
		// take a second copy for transformations
		poly->verts[i][0] = poly->verts[i][7] = verts[0];
		poly->verts[i][1] = poly->verts[i][8] = verts[1];
		poly->verts[i][2] = poly->verts[i][9] = verts[2];

		// store in 5 and 6 so that we can overwrite 3 and 4 at runtime
		poly->verts[i][5] = DotProduct (verts, warpface->texinfo->vecs[0]);
		poly->verts[i][6] = DotProduct (verts, warpface->texinfo->vecs[1]);
	}
}


/*
================
GL_SubdivideSurface

Breaks a polygon up along axial 64 unit
boundaries so that turbulent and sky warps
can be done reasonably.
================
*/
void GL_SubdivideSurface (msurface_t *fa)
{
	vec3_t		verts[64];
	int			numverts;
	int			i;
	int			lindex;
	float		*vec;

	warpface = fa;

	//
	// convert edges back to a normal polygon
	//
	numverts = 0;
	for (i=0 ; i<fa->numedges ; i++)
	{
		lindex = loadmodel->surfedges[fa->firstedge + i];

		if (lindex > 0)
			vec = loadmodel->vertexes[loadmodel->edges[lindex].v[0]].position;
		else
			vec = loadmodel->vertexes[loadmodel->edges[-lindex].v[1]].position;
		VectorCopy (vec, verts[numverts]);
		numverts++;
	}

	SubdividePolygon (numverts, verts[0]);
}

//=========================================================


void D3D_TransformWarpSurface (msurface_t *surf, D3DMATRIX *m)
{
	glpoly_t	*p;
	float		*v;
	int			i;

	for (p = surf->polys; p; p = p->next)
	{
		for (i = 0, v = p->verts[0]; i < p->numverts; i++, v += VERTEXSIZE)
		{
			// transform by the matrix then copy back
			v[0] = v[7] * m->_11 + v[8] * m->_21 + v[9] * m->_31 + m->_41;
			v[1] = v[7] * m->_12 + v[8] * m->_22 + v[9] * m->_32 + m->_42;
			v[2] = v[7] * m->_13 + v[8] * m->_23 + v[9] * m->_33 + m->_43;
		}
	}
}


// speed up sin calculations - Ed
float	turbsin[] =
{
	#include "gl_warp_sin.h"
};
#define TURBSCALE (256.0 / (2 * M_PI))

/*
=============
EmitWaterPolys

Does a water warp on the pre-fragmented glpoly_t chain
=============
*/
void EmitWaterPolys (msurface_t *surf)
{
	glpoly_t	*p;
	float		*v;
	int			i;

	for (p = surf->polys; p; p = p->next)
	{
		for (i = 0, v = p->verts[0]; i < p->numverts; i++, v += VERTEXSIZE)
		{
			// texcoords
			v[3] = (v[5] + turbsin[(int) ((v[6] * 0.125 + realtime) * TURBSCALE) & 255]) * 0.015625f;
			v[4] = (v[6] + turbsin[(int) ((v[5] * 0.125 + realtime) * TURBSCALE) & 255]) * 0.015625f;
		}

		// draw the poly
		d3d_Device->DrawPrimitiveUP (D3DPT_TRIANGLEFAN, p->numverts - 2, p->verts[0], VERTEXSIZE * sizeof (float));
	}
}


void R_ClipSky (msurface_t *skychain)
{
	// disable textureing and writes to the color buffer
	D3D_SetFVFStateManaged (D3DFVF_XYZ);
	d3d_Device->SetTextureStageState (0, D3DTSS_COLOROP, D3DTOP_DISABLE);
	d3d_Device->SetRenderState (D3DRS_COLORWRITEENABLE, 0);

	for (msurface_t *surf = skychain; surf; surf = surf->texturechain)
		for (glpoly_t *p = surf->polys; p; p = p->next)
			d3d_Device->DrawPrimitiveUP (D3DPT_TRIANGLEFAN, p->numverts - 2, p->verts[0], VERTEXSIZE * sizeof (float));

	// revert state
	d3d_Device->SetRenderState (D3DRS_COLORWRITEENABLE, 0x0000000F);
	d3d_Device->SetTextureStageState (0, D3DTSS_COLOROP, D3DTOP_MODULATE);
}


void R_DrawSkySphere (float rotatefactor, float scale, LPDIRECT3DTEXTURE9 skytexture)
{
	d3d_WorldMatrixStack->Push ();
	d3d_WorldMatrixStack->ScaleLocal (scale, scale, scale / 2);
	d3d_WorldMatrixStack->RotateAxisLocal (&XVECTOR, D3DXToRadian (-90));
	d3d_WorldMatrixStack->RotateAxisLocal (&YVECTOR, D3DXToRadian (-22));
	d3d_WorldMatrixStack->RotateAxisLocal (&ZVECTOR, D3DXToRadian (rotatefactor));
	d3d_Device->SetTransform (D3DTS_WORLD, d3d_WorldMatrixStack->GetTop ());

	D3D_BindTexture (skytexture);

	for (int i = 0; i < 220; i += 22)
		d3d_Device->DrawPrimitive (D3DPT_TRIANGLESTRIP, i, 20);

	d3d_WorldMatrixStack->Pop ();
}


void R_DrawNewSky (msurface_t *skychain)
{
	float rotateBack = anglemod (cl.time * 5.0f);
	float rotateFore = anglemod (cl.time * 8.0f);

	// write the regular sky polys into the depth buffer to get a baseline
	R_ClipSky (skychain);

	// flip the depth func so that the regular polys will prevent sphere polys outside their area reaching the framebuffer
	// also disable writing to Z
	d3d_Device->SetRenderState (D3DRS_ZFUNC, D3DCMP_GREATEREQUAL);
	d3d_Device->SetRenderState (D3DRS_ZWRITEENABLE, FALSE);

	// sky layer drawing state
	d3d_Device->SetStreamSource (0, d3d_SkySphereVerts, 0, sizeof (skysphere_t));
	D3D_SetFVFStateManaged (D3DFVF_XYZ | D3DFVF_TEX1);
	d3d_WorldMatrixStack->Push ();
	d3d_WorldMatrixStack->TranslateLocal (r_origin[0], r_origin[1], r_origin[2]);

	// draw back layer
	R_DrawSkySphere (rotateBack, 10, solidskytexture);

	// draw front layer
	d3d_EnableAlphaBlend->Apply ();
	R_DrawSkySphere (rotateFore, 8, alphaskytexture);
	d3d_DisableAlphaBlend->Apply ();

	// restore the depth func
	d3d_Device->SetRenderState (D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	d3d_Device->SetRenderState (D3DRS_ZWRITEENABLE, TRUE);

	// go back to the world matrix
	d3d_WorldMatrixStack->Pop ();
	d3d_WorldMatrixStack->LoadMatrix (d3d_WorldMatrix);
	d3d_Device->SetTransform (D3DTS_WORLD, d3d_WorldMatrixStack->GetTop ());

	// now write the regular polys one more time to clip world geometry
	R_ClipSky (skychain);
}


/*
=================
R_DrawSkyChain
=================
*/
void R_DrawSkyChain (msurface_t *skychain)
{
	// no sky to draw!
	if (!skychain) return;

	if (!r_oldsky.value)
	{
		// draw new sky
		R_DrawNewSky (skychain);
		return;
	}

	msurface_t *surf;
	float speedscale[2];

	D3D_SetFVFStateManaged (D3DFVF_XYZ | D3DFVF_TEX2);

	D3D_BindTexture (0, solidskytexture);
	D3D_BindTexture (1, alphaskytexture);

	d3d_Device->SetTextureStageState (0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	d3d_Device->SetTextureStageState (0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	d3d_Device->SetTextureStageState (0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	d3d_Device->SetTextureStageState (0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

	d3d_Device->SetTextureStageState (1, D3DTSS_COLOROP, D3DTOP_BLENDTEXTUREALPHA);
	d3d_Device->SetTextureStageState (1, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	d3d_Device->SetTextureStageState (1, D3DTSS_COLORARG2, D3DTA_CURRENT);

	d3d_Device->SetTextureStageState (2, D3DTSS_COLOROP, D3DTOP_DISABLE);

	speedscale[0] = realtime * 8;
	speedscale[0] -= (int) speedscale[0] & ~127;

	speedscale[1] = realtime * 16;
	speedscale[1] -= (int) speedscale[1] & ~127;

	for (surf = skychain; surf; surf = surf->texturechain)
	{
		glpoly_t	*p;
		float		*v;
		int			i;
		vec3_t	dir;
		float	length;

		for (p = surf->polys; p; p = p->next)
		{
			for (i = 0, v = p->verts[0]; i < p->numverts; i++, v += VERTEXSIZE)
			{
				VectorSubtract (v, r_origin, dir);
				dir[2] *= 3;	// flatten the sphere

				length = dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2];
				length = sqrt (length);
				length = 6 * 63 / length;

				dir[0] *= length;
				dir[1] *= length;

				v[3] = (speedscale[0] + dir[0]) * 0.0078125f;
				v[4] = (speedscale[0] + dir[1]) * 0.0078125f;

				v[5] = (speedscale[1] + dir[0]) * 0.0078125f;
				v[6] = (speedscale[1] + dir[1]) * 0.0078125f;
			}

			// draw the poly
			d3d_Device->DrawPrimitiveUP (D3DPT_TRIANGLEFAN, p->numverts - 2, p->verts[0], VERTEXSIZE * sizeof (float));
		}
	}

	d3d_Device->SetTextureStageState (1, D3DTSS_COLOROP, D3DTOP_DISABLE);
}


//===============================================================


void D3D_InitSkySphere (void)
{
	// already initialized
	if (d3d_SkySphereVerts) return;

	// set up the sphere vertex buffer
	HRESULT hr = d3d_Device->CreateVertexBuffer
	(
		220 * sizeof (skysphere_t),
		0,
		(D3DFVF_XYZ | D3DFVF_TEX1),
		D3DPOOL_MANAGED,
		&d3d_SkySphereVerts,
		NULL
	);

	// fixme - fall back on DrawPrimitiveUP if this happens!
	if (FAILED (hr)) Sys_Error ("D3D_InitSkySphere: d3d_Device->CreateVertexBuffer failed");

	skysphere_t *verts;

	// lock the full buffers
	d3d_SkySphereVerts->Lock (0, 0, (void **) &verts, 0);

	// fill it in
	float drho = 0.3141592653589;
	float dtheta = 0.6283185307180;

	float ds = 0.1;
	float dt = 0.1;

	float t = 1.0f;	
	float s = 0.0f;

	int i;
	int j;

	int vertnum = 0;

	for (i = 0; i < 10; i++)
	{
		float rho = (float) i * drho;
		float srho = (float) (sin (rho));
		float crho = (float) (cos (rho));
		float srhodrho = (float) (sin (rho + drho));
		float crhodrho = (float) (cos (rho + drho));

		s = 0.0f;

		for (j = 0; j <= 10; j++)
		{
			float theta = (j == 10) ? 0.0f : j * dtheta;
			float stheta = (float) (-sin (theta));
			float ctheta = (float) (cos (theta));

			verts[vertnum].st[0] = s * 12;
			verts[vertnum].st[1] = t * 6;

			verts[vertnum].xyz[0] = stheta * srho * 4096.0;
			verts[vertnum].xyz[1] = ctheta * srho * 4096.0;
			verts[vertnum].xyz[2] = crho * 4096.0;

			vertnum++;

			verts[vertnum].st[0] = s * 12;
			verts[vertnum].st[1] = (t - dt) * 6;

			verts[vertnum].xyz[0] = stheta * srhodrho * 4096.0;
			verts[vertnum].xyz[1] = ctheta * srhodrho * 4096.0;
			verts[vertnum].xyz[2] = crhodrho * 4096.0;

			vertnum++;
			s += ds;
		}

		t -= dt;
	}

	// unlock
	d3d_SkySphereVerts->Unlock ();
	d3d_SkySphereVerts->PreLoad ();
}


/*
=============
R_InitSky

A sky texture is 256*128, with the right side being a masked overlay
==============
*/
void R_InitSky (miptex_t *mt)
{
	// create the sky sphere (one time only)
	D3D_InitSkySphere ();

	int			i, j, p;
	byte		*src;
	unsigned	trans[128 * 128];
	unsigned	transpix;
	int			r, g, b;
	unsigned	*rgba;

	// destroy any current textures we might have
	SAFE_RELEASE (solidskytexture);
	SAFE_RELEASE (alphaskytexture);

	src = (byte *) mt + mt->offsets[0];

	// make an average value for the back to avoid
	// a fringe on the top level
	for (i = 0, r = 0, g = 0, b = 0; i < 128; i++)
	{
		for (j = 0; j < 128; j++)
		{
			p = src[i * 256 + j + 128];

			rgba = &d_8to24table[p];

			trans[(i * 128) + j] = *rgba;

			r += ((byte *) rgba)[0];
			g += ((byte *) rgba)[1];
			b += ((byte *) rgba)[2];
		}
	}

	((byte *) &transpix)[0] = r / (128 * 128);
	((byte *) &transpix)[1] = g / (128 * 128);
	((byte *) &transpix)[2] = b / (128 * 128);
	((byte *) &transpix)[3] = 0;

	// upload it
	D3D_LoadTexture (&solidskytexture, 128, 128, (byte *) trans, NULL, false, false);

	// bottom layer
	for (i = 0; i < 128; i++)
	{
		for (j = 0; j < 128; j++)
		{
			p = src[i * 256 + j];

			if (p == 0)
				trans[(i * 128) + j] = transpix;
			else
				trans[(i * 128) + j] = d_8to24table[p];
		}
	}

	// upload it
	D3D_LoadTexture (&alphaskytexture, 128, 128, (byte *) trans, NULL, false, true);
}

