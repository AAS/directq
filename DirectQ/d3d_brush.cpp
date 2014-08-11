
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

// generic brush interface for all non sky and water brushes

#include "quakedef.h"
#include "d3d_model.h"
#include "d3d_quake.h"


void D3DBrush_SetShader (int ShaderNum)
{
	// common
	D3D_SetTextureMipmap (0, D3DTEXF_LINEAR, D3DTEXF_NONE);
	D3D_SetTextureMipmap (1, d3d_TexFilter, d3d_MipFilter);

	switch (ShaderNum)
	{
	case FX_PASS_WORLD_NOLUMA:
	case FX_PASS_WORLD_NOLUMA_ALPHA:
		D3D_SetTextureAddress (0, D3DTADDRESS_CLAMP);
		D3D_SetTextureAddress (1, D3DTADDRESS_WRAP);
		break;

	default:
		// some kind of luma style
		D3D_SetTextureAddress (0, D3DTADDRESS_CLAMP);
		D3D_SetTextureAddress (1, D3DTADDRESS_WRAP);
		D3D_SetTextureAddress (2, D3DTADDRESS_WRAP);
		D3D_SetTextureMipmap (2, d3d_TexFilter, d3d_MipFilter);
		break;
	}

	D3DHLSL_SetPass (ShaderNum);
}


#define MAX_SURF_VERTEXES 0xfffe
#define MAX_SURF_INDEXES 0x400000

// and this is the actual max we'll use
int MaxSurfVertexes = MAX_SURF_VERTEXES;
int MaxSurfIndexes = MAX_SURF_INDEXES;

LPDIRECT3DVERTEXBUFFER9 d3d_SurfVBO = NULL;
LPDIRECT3DINDEXBUFFER9 d3d_SurfIBO = NULL;
LPDIRECT3DVERTEXDECLARATION9 d3d_SurfDecl = NULL;


typedef struct d3d_surfstate_s
{
	int TotalVertexes;
	int TotalIndexes;
	int VertsToLock;
	int IndexesToLock;
} d3d_surfstate_t;


#define MAX_SURF_MODELSURFS		2048

typedef struct d3d_brushsurf_s
{
	msurface_t *surf;
	entity_t *ent;
} d3d_brushsurf_t;

d3d_surfstate_t d3d_SurfState;
d3d_brushsurf_t d3d_BrushSurfs[MAX_SURF_MODELSURFS];
int d3d_NumBrushSurfs = 0;

void D3DBrush_CreateBuffers (void)
{
	if (!d3d_SurfVBO)
	{
		// initial max clamped to hardware limit
		if (MAX_SURF_VERTEXES >= d3d_DeviceCaps.MaxVertexIndex)
			MaxSurfVertexes = d3d_DeviceCaps.MaxVertexIndex;
		else MaxSurfVertexes = MAX_SURF_VERTEXES;

		D3DMain_CreateVertexBuffer (MaxSurfVertexes * sizeof (brushpolyvert_t), D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, &d3d_SurfVBO);
		D3D_PrelockVertexBuffer (d3d_SurfVBO);
		d3d_SurfState.TotalVertexes = 0;
	}

	if (!d3d_SurfIBO)
	{
		// clamp to hardware maximum
		if (MaxSurfIndexes >= (d3d_DeviceCaps.MaxPrimitiveCount * 3))
			MaxSurfIndexes = (d3d_DeviceCaps.MaxPrimitiveCount * 3);
		else MaxSurfIndexes = MAX_SURF_INDEXES;

		D3DMain_CreateIndexBuffer (MaxSurfIndexes, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, &d3d_SurfIBO);
		D3D_PrelockIndexBuffer (d3d_SurfIBO);
		d3d_SurfState.TotalIndexes = 0;
	}

	if (!d3d_SurfDecl)
	{
		D3DVERTEXELEMENT9 d3d_surflayout[] =
		{
			{0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
			{0, 12, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
			{0, 20, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1},
			D3DDECL_END ()
		};

		hr = d3d_Device->CreateVertexDeclaration (d3d_surflayout, &d3d_SurfDecl);
		if (FAILED (hr)) Sys_Error ("D3DPart_CreateBuffers: d3d_Device->CreateVertexDeclaration failed");
	}
}


void D3DBrush_ReleaseBuffers (void)
{
	SAFE_RELEASE (d3d_SurfVBO);
	SAFE_RELEASE (d3d_SurfIBO);
	SAFE_RELEASE (d3d_SurfDecl);
}


CD3DDeviceLossHandler d3d_SurfBuffersHandler (D3DBrush_ReleaseBuffers, D3DBrush_CreateBuffers);


void D3DBrush_SetBuffers (void)
{
	// create the buffers if needed
	D3DBrush_CreateBuffers ();

	// and set up for drawing
	D3D_SetVertexDeclaration (d3d_SurfDecl);

	D3D_SetStreamSource (0, d3d_SurfVBO, 0, sizeof (brushpolyvert_t));
	D3D_SetStreamSource (1, NULL, 0, 0);
	D3D_SetStreamSource (2, NULL, 0, 0);
	D3D_SetIndices (d3d_SurfIBO);
}


brushpolyvert_t *D3DBrush_TransformSurface (brushpolyvert_t *src, brushpolyvert_t *dst, int numverts, D3DMATRIX *m)
{
	for (int i = 0; i < numverts; i++, src++, dst++)
	{
		D3DMatrix_TransformPoint (m, src->xyz, dst->xyz);

		dst->st[0] = src->st[0];
		dst->st[1] = src->st[1];

		dst->lm[0] = src->lm[0];
		dst->lm[1] = src->lm[1];
	}

	return dst;
}


unsigned short *D3DBrush_TransferIndexes (unsigned short *src, unsigned short *dst, int numindexes, int offset)
{
	// > 15 is rare so don't do it
	while (numindexes > 7)
	{
		dst[0] = src[0] + offset;
		dst[1] = src[1] + offset;
		dst[2] = src[2] + offset;
		dst[3] = src[3] + offset;
		dst[4] = src[4] + offset;
		dst[5] = src[5] + offset;
		dst[6] = src[6] + offset;
		dst[7] = src[7] + offset;

		dst += 8;
		src += 8;
		numindexes -= 8;
	}

	// duff's device ftw!
	switch (numindexes)
	{
	case 8: *dst++ = offset + *src++;
	case 7: *dst++ = offset + *src++;
	case 6: *dst++ = offset + *src++;
	case 5: *dst++ = offset + *src++;
	case 4: *dst++ = offset + *src++;
	case 3: *dst++ = offset + *src++;
	case 2: *dst++ = offset + *src++;
	case 1: *dst++ = offset + *src++;
	default: break;
	}

	return dst;
}


void D3DBrush_FlushSurfaces (void)
{
	if (d3d_NumBrushSurfs && d3d_SurfState.VertsToLock && d3d_SurfState.IndexesToLock)
	{
		brushpolyvert_t *verts = NULL;
		unsigned short *ndx = NULL;

		if (d3d_SurfState.TotalVertexes + d3d_SurfState.VertsToLock >= MaxSurfVertexes)
		{
			hr = d3d_SurfVBO->Lock (0, 0, (void **) &verts, D3DLOCK_DISCARD);
			if (FAILED (hr)) Sys_Error ("D3DBrush_FlushSurfaces: failed to lock vertex buffer");

			d3d_SurfState.TotalVertexes = 0;
		}
		else
		{
			hr = d3d_SurfVBO->Lock (d3d_SurfState.TotalVertexes * sizeof (brushpolyvert_t),
				d3d_SurfState.VertsToLock * sizeof (brushpolyvert_t),
				(void **) &verts,
				D3DLOCK_NOOVERWRITE);

			if (FAILED (hr)) Sys_Error ("D3DBrush_FlushSurfaces: failed to lock vertex buffer");
		}

		if (d3d_SurfState.TotalIndexes + d3d_SurfState.IndexesToLock >= MaxSurfIndexes)
		{
			hr = d3d_SurfIBO->Lock (0, 0, (void **) &ndx, D3DLOCK_DISCARD);
			if (FAILED (hr)) Sys_Error ("D3DBrush_FlushSurfaces: failed to lock index buffer");

			d3d_SurfState.TotalIndexes = 0;
		}
		else
		{
			// we expect this to always be true
			hr = d3d_SurfIBO->Lock (d3d_SurfState.TotalIndexes * sizeof (unsigned short),
				d3d_SurfState.IndexesToLock * sizeof (unsigned short),
				(void **) &ndx,
				D3DLOCK_NOOVERWRITE);

			if (FAILED (hr)) Sys_Error ("D3DBrush_FlushSurfaces: failed to lock index buffer");
		}

		for (int i = 0, ndxofs = d3d_SurfState.TotalVertexes; i < d3d_NumBrushSurfs; i++)
		{
			msurface_t *surf = d3d_BrushSurfs[i].surf;
			entity_t *ent = d3d_BrushSurfs[i].ent;

			// an entity always needs to be transformed now as we're not updating the source verts any more
			if (ent)
				verts = D3DBrush_TransformSurface (surf->verts, verts, surf->numverts, &ent->matrix);
			else
			{
				memcpy (verts, surf->verts, surf->numverts * sizeof (brushpolyvert_t));
				verts += surf->numverts;
			}

			// fast-transfer our indexes
			ndx = D3DBrush_TransferIndexes (surf->indexes, ndx, surf->numindexes, ndxofs);
			ndxofs += surf->numverts;
		}

		hr = d3d_SurfVBO->Unlock ();
		if (FAILED (hr)) Sys_Error ("D3DBrush_FlushSurfaces: failed to unlock vertex buffer");
		d3d_RenderDef.numlock++;

		hr = d3d_SurfIBO->Unlock ();
		if (FAILED (hr)) Sys_Error ("D3DBrush_FlushSurfaces: failed to unlock index buffer");
		d3d_RenderDef.numlock++;

		// now draw it all
		D3D_DrawIndexedPrimitive (d3d_SurfState.TotalVertexes, d3d_SurfState.VertsToLock, d3d_SurfState.TotalIndexes, d3d_SurfState.IndexesToLock / 3);

		// reset counters
		d3d_SurfState.TotalIndexes += d3d_SurfState.IndexesToLock;
		d3d_SurfState.TotalVertexes += d3d_SurfState.VertsToLock;
	}

	// these are done outside the loop
	d3d_NumBrushSurfs = 0;
	d3d_SurfState.IndexesToLock = 0;
	d3d_SurfState.VertsToLock = 0;
}


// hmmm - ideally this would just take a pointer to the modelsurf and build a count of them rather than
// needing to build a second list, but then it would break with sky and we wouldn't be able to combine the locks
// oh well, shit happens.  the next one will be better.
void D3DBrush_SubmitSurface (msurface_t *surf, entity_t *ent)
{
	// if the list is full we must flush it before appending more
	if (d3d_NumBrushSurfs >= MAX_SURF_MODELSURFS) D3DBrush_FlushSurfaces ();
	if (d3d_SurfState.VertsToLock >= MaxSurfVertexes) D3DBrush_FlushSurfaces ();
	if (d3d_SurfState.IndexesToLock >= MaxSurfIndexes) D3DBrush_FlushSurfaces ();

	// grab a new one from the list
	d3d_brushsurf_t *ms = &d3d_BrushSurfs[d3d_NumBrushSurfs];
	d3d_NumBrushSurfs++;

	// add this surf to the list
	ms->surf = surf;
	ms->ent = ent;

	// increment the lock size counters
	d3d_SurfState.VertsToLock += surf->numverts;
	d3d_SurfState.IndexesToLock += surf->numindexes;
	d3d_RenderDef.brush_polys++;
}


typedef struct d3d_brushstate_s
{
	int Alpha;
	int ShaderPass;
	LPDIRECT3DTEXTURE9 tmu0tex;
	LPDIRECT3DTEXTURE9 tmu1tex;
	LPDIRECT3DTEXTURE9 tmu2tex;
} d3d_brushstate_t;

d3d_brushstate_t d3d_BrushState;


void D3DBrush_Begin (void)
{
	D3DBrush_SetBuffers ();

	d3d_BrushState.Alpha = -1;
	d3d_BrushState.tmu0tex = NULL;
	d3d_BrushState.tmu1tex = NULL;
	d3d_BrushState.tmu2tex = NULL;
	d3d_BrushState.ShaderPass = FX_PASS_NOTBEGUN;
}


void D3DBrush_End (void)
{
	// draw anything left over
	D3DBrush_FlushSurfaces ();
}


void D3DBrush_EmitSurface (d3d_modelsurf_t *ms)
{
	msurface_t *surf = ms->surf;
	bool recommit = false;
	byte thisalpha = 255;

	// check because we can get a frame update while the verts are NULL
	if (!surf->verts) return;

	// explicit alpha only
	if (ms->surfalpha < 255) thisalpha = ms->surfalpha;

	// check for state changes
	if (thisalpha != d3d_BrushState.Alpha) recommit = true;
	if (ms->textures[TEXTURE_LIGHTMAP] != d3d_BrushState.tmu0tex) recommit = true;
	if (ms->textures[TEXTURE_DIFFUSE] != d3d_BrushState.tmu1tex) recommit = true;
	if (ms->textures[TEXTURE_LUMA] != d3d_BrushState.tmu2tex) recommit = true;

	// if we need to change state here we must flush anything batched so far before doing so
	// either way we flag a commit pending too
	if (recommit) D3DBrush_FlushSurfaces ();

	if (recommit)
	{
		D3DHLSL_SetTexture (0, ms->textures[TEXTURE_LIGHTMAP]);
		D3DHLSL_SetTexture (1, ms->textures[TEXTURE_DIFFUSE]);
		D3DHLSL_SetTexture (2, ms->textures[TEXTURE_LUMA]);

		d3d_BrushState.tmu0tex = ms->textures[TEXTURE_LIGHTMAP];
		d3d_BrushState.tmu1tex = ms->textures[TEXTURE_DIFFUSE];
		d3d_BrushState.tmu2tex = ms->textures[TEXTURE_LUMA];
	}

	if (thisalpha != d3d_BrushState.Alpha)
	{
		D3DHLSL_SetAlpha ((float) thisalpha / 255.0f);
		d3d_BrushState.Alpha = thisalpha;
	}

	if (ms->shaderpass != d3d_BrushState.ShaderPass)
	{
		// now do the pass switch after all state has been set
		D3DBrush_SetShader (ms->shaderpass);
		d3d_BrushState.ShaderPass = ms->shaderpass;
	}

	D3DBrush_SubmitSurface (surf, ms->ent);
}


