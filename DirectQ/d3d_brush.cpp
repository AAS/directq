
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
		D3D_SetTextureAddressMode (D3DTADDRESS_CLAMP, D3DTADDRESS_WRAP);
		break;

	case FX_PASS_WORLD_LUMA:
	case FX_PASS_WORLD_LUMA_ALPHA:
		D3D_SetTextureAddressMode (D3DTADDRESS_CLAMP, D3DTADDRESS_WRAP, D3DTADDRESS_WRAP);
		D3D_SetTextureMipmap (2, d3d_TexFilter, d3d_MipFilter);
		break;

	default:
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

	int VBFrame;
} d3d_surfstate_t;


#define MAX_SURF_MODELSURFS		2048

typedef struct d3d_brushsurf_s
{
	msurface_t *surf;
	entity_t *ent;
	int vbframe;
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

		hr = d3d_Device->CreateVertexBuffer
		(
			MaxSurfVertexes * sizeof (brushpolyvert_t),
			D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY,
			0,
			D3DPOOL_DEFAULT,
			&d3d_SurfVBO,
			NULL
		);

		if (FAILED (hr)) Sys_Error ("D3DPart_CreateBuffers: d3d_Device->CreateVertexBuffer failed");

		D3D_PrelockVertexBuffer (d3d_SurfVBO);

		// need to go to a new VB frame to force caches to invalidate here
		d3d_SurfState.VBFrame++;
		d3d_SurfState.TotalVertexes = 0;
	}

	if (!d3d_SurfIBO)
	{
		// clamp to hardware maximum
		if (MaxSurfIndexes >= (d3d_DeviceCaps.MaxPrimitiveCount * 3))
			MaxSurfIndexes = (d3d_DeviceCaps.MaxPrimitiveCount * 3);
		else MaxSurfIndexes = MAX_SURF_INDEXES;

		hr = d3d_Device->CreateIndexBuffer
		(
			MaxSurfIndexes * sizeof (unsigned short),
			D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY,
			D3DFMT_INDEX16,
			D3DPOOL_DEFAULT,
			&d3d_SurfIBO,
			NULL
		);

		if (FAILED (hr)) Sys_Error ("D3DPart_CreateBuffers: d3d_Device->CreateIndexBuffer failed");

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

	// invalidate any cached surfs to make sure that nothing will use these buffers after they've been released!!!
	d3d_SurfState.VBFrame++;
}


CD3DDeviceLossHandler d3d_SurfBuffersHandler (D3DBrush_ReleaseBuffers, D3DBrush_CreateBuffers);


void D3DBrush_SetBuffers (void)
{
	// create the buffers if needed
	D3DBrush_CreateBuffers ();

	// and set up for drawing
	D3D_SetVertexDeclaration (d3d_SurfDecl);

	// VBOs are only used with hardware T&L
	D3D_SetStreamSource (0, d3d_SurfVBO, 0, sizeof (brushpolyvert_t));
	D3D_SetStreamSource (1, NULL, 0, 0);
	D3D_SetStreamSource (2, NULL, 0, 0);
	D3D_SetIndices (d3d_SurfIBO);
}


void D3DBrush_TransformSurface (msurface_t *surf, entity_t *ent)
{
	// rebuild the verts
	D3DMATRIX *m = &ent->matrix;
	brushhdr_t *hdr = ent->model->brushhdr;

	// rebuild the vert positions from scratch and apply the transform; a full transform is only needed
	// if the model needs to be rotated, otherwise we apply a simpler translate only (save CPU overhead)
	if (ent->rotated)
	{
		// full matrix transform
		for (int i = 0; i < surf->numverts; i++)
		{
			float *vec;
			int lindex = hdr->surfedges[surf->firstedge + i];

			if (lindex > 0)
				vec = hdr->vertexes[hdr->edges[lindex].v[0]].position;
			else vec = hdr->vertexes[hdr->edges[-lindex].v[1]].position;

			surf->verts[i].xyz[0] = vec[0] * m->_11 + vec[1] * m->_21 + vec[2] * m->_31 + m->_41;
			surf->verts[i].xyz[1] = vec[0] * m->_12 + vec[1] * m->_22 + vec[2] * m->_32 + m->_42;
			surf->verts[i].xyz[2] = vec[0] * m->_13 + vec[1] * m->_23 + vec[2] * m->_33 + m->_43;
		}
	}
	else if (ent->translated)
	{
		// lightweight translate only
		for (int i = 0; i < surf->numverts; i++)
		{
			float *vec;
			int lindex = hdr->surfedges[surf->firstedge + i];

			if (lindex > 0)
				vec = hdr->vertexes[hdr->edges[lindex].v[0]].position;
			else vec = hdr->vertexes[hdr->edges[-lindex].v[1]].position;

			surf->verts[i].xyz[0] = vec[0] + m->_41;
			surf->verts[i].xyz[1] = vec[1] + m->_42;
			surf->verts[i].xyz[2] = vec[2] + m->_43;
		}
	}
}


void D3DBrush_FlushSurfaces (void)
{
	if (d3d_NumBrushSurfs)
	{
		bool vblocked = false;
		bool iblocked = false;
		brushpolyvert_t *verts = NULL;
		unsigned short *ndx = NULL;

		if (d3d_SurfState.TotalVertexes + d3d_SurfState.VertsToLock >= MaxSurfVertexes)
		{
			hr = d3d_SurfVBO->Lock (0, 0, (void **) &verts, D3DLOCK_DISCARD);
			if (FAILED (hr)) Sys_Error ("D3DBrush_FlushSurfaces: failed to lock vertex buffer");

			d3d_SurfState.TotalVertexes = 0;
			d3d_SurfState.VBFrame++;
			vblocked = true;
		}
		else if (d3d_SurfState.VertsToLock)
		{
			hr = d3d_SurfVBO->Lock (d3d_SurfState.TotalVertexes * sizeof (brushpolyvert_t),
				d3d_SurfState.VertsToLock * sizeof (brushpolyvert_t),
				(void **) &verts,
				D3DLOCK_NOOVERWRITE);

			if (FAILED (hr)) Sys_Error ("D3DBrush_FlushSurfaces: failed to lock vertex buffer");
			vblocked = true;
		}

		if (d3d_SurfState.TotalIndexes + d3d_SurfState.IndexesToLock >= MaxSurfIndexes)
		{
			hr = d3d_SurfIBO->Lock (0, 0, (void **) &ndx, D3DLOCK_DISCARD);
			if (FAILED (hr)) Sys_Error ("D3DBrush_FlushSurfaces: failed to lock index buffer");

			d3d_SurfState.TotalIndexes = 0;
			iblocked = true;
		}
		else if (d3d_SurfState.IndexesToLock)
		{
			// we expect this to always be true
			hr = d3d_SurfIBO->Lock (d3d_SurfState.TotalIndexes * sizeof (unsigned short),
				d3d_SurfState.IndexesToLock * sizeof (unsigned short),
				(void **) &ndx,
				D3DLOCK_NOOVERWRITE);

			if (FAILED (hr)) Sys_Error ("D3DBrush_FlushSurfaces: failed to lock index buffer");
			iblocked = true;
		}
		else
		{
			// this should never happen.....
			// Con_Printf ("Nothing to lock!\n");
		}

		// initial DIP params
		int FirstVertex = d3d_SurfState.TotalVertexes;
		int FirstIndex = d3d_SurfState.TotalIndexes;
		int NumVertexes = 0;
		int NumIndexes = 0;

		for (int i = 0; i < d3d_NumBrushSurfs; i++)
		{
			d3d_brushsurf_t *ms = &d3d_BrushSurfs[i];
			msurface_t *surf = ms->surf;
			entity_t *ent = ms->ent;

			if (verts && ms->vbframe != d3d_SurfState.VBFrame)
			{
				// if an entity needs to be transformed transform it now
				if (ent && (ent->translated || ent->rotated))
					D3DBrush_TransformSurface (surf, ent);

				// copy vertexes
				if (surf->numverts == 4)
				{
					// if the surf has 4 verts we express it as a strip which might help certain hardware
					// (5 verts can also be expressed as a strip???) (to do - move to load time)
					memcpy (verts, surf->verts, sizeof (brushpolyvert_t) * 2);
					memcpy (&verts[2], &surf->verts[3], sizeof (brushpolyvert_t));
					memcpy (&verts[3], &surf->verts[2], sizeof (brushpolyvert_t));
				}
				else memcpy (verts, surf->verts, surf->numverts * sizeof (brushpolyvert_t));

				verts += surf->numverts;

				// store out cache position for this surf
				surf->iboffset = d3d_SurfState.TotalVertexes;
				surf->vbframe = d3d_SurfState.VBFrame;

				d3d_SurfState.TotalVertexes += surf->numverts;
			}

			// correct the first vertex param
			if (surf->iboffset < FirstVertex) FirstVertex = surf->iboffset;

			if (surf->numverts == 4)
			{
				// 0/1/2 1/3/2 2/3/4
				// if the surf has 4 verts we express it as a strip which might help certain hardware
				// (5 verts can also be expressed as a strip???) (to do - move to load time) (2/3/4 for the third)
				ndx[0] = 0 + surf->iboffset;
				ndx[1] = 1 + surf->iboffset;
				ndx[2] = 2 + surf->iboffset;
				ndx[3] = 2 + surf->iboffset;
				ndx[4] = 1 + surf->iboffset;
				ndx[5] = 3 + surf->iboffset;
				ndx += 6;
			}
			else
			{
				for (int n = 2; n < surf->numverts; n++, ndx += 3)
				{
					ndx[0] = surf->iboffset;
					ndx[1] = surf->iboffset + n - 1;
					ndx[2] = surf->iboffset + n;
				}
			}

			d3d_SurfState.TotalIndexes += surf->numindexes;

			// these counts are always incremented
			NumIndexes += surf->numindexes;
			NumVertexes += surf->numverts;
		}

		if (vblocked)
		{
			hr = d3d_SurfVBO->Unlock ();
			if (FAILED (hr)) Sys_Error ("D3DBrush_FlushSurfaces: failed to unlock vertex buffer");
			d3d_RenderDef.numlock++;
		}

		if (iblocked)
		{
			hr = d3d_SurfIBO->Unlock ();
			if (FAILED (hr)) Sys_Error ("D3DBrush_FlushSurfaces: failed to unlock index buffer");
			d3d_RenderDef.numlock++;
		}

		// now draw it all
		D3D_DrawIndexedPrimitive (FirstVertex, NumVertexes, FirstIndex, NumIndexes / 3);

		// Con_Printf ("%i brush surfs\n", d3d_NumBrushSurfs);
	}

	// reset counters
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

	// only check for movement if we're not orphaning the buffers; if we are doing so all cached versions become invalid anyway
	if (ent && ent->brushstate.bmmoved) surf->vbframe = -1;

	// don't use the cache on a software T&L device because hopping around randomly in the buffer is painful for it
	if (!d3d_GlobalCaps.supportHardwareTandL) surf->vbframe = -1;

	// grab a new one from the list
	d3d_brushsurf_t *ms = &d3d_BrushSurfs[d3d_NumBrushSurfs];
	d3d_NumBrushSurfs++;

	// add this surf to the list
	ms->surf = surf;
	ms->ent = ent;
	ms->vbframe = surf->vbframe;

	// increment the lock size counters
	if (ms->vbframe != d3d_SurfState.VBFrame)
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


