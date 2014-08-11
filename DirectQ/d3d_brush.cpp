
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

// always use batching by defining no point at which we switch back to unbatched
// (in practice we always unbatch if there is only one surface as it's a single draw call anyway, so updating the index buffer is unnecessary overhead)
cvar_t r_surfacebatchcutoff ("r_surfacebatchcutoff", "0", CVAR_ARCHIVE);

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
#define MAX_SURF_INDEXES 0x1fff8

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
	entity_t *SurfEnt;
	int Alpha;
	int ShaderPass;
	LPDIRECT3DTEXTURE9 tmu0tex;
	LPDIRECT3DTEXTURE9 tmu1tex;
	LPDIRECT3DTEXTURE9 tmu2tex;
} d3d_surfstate_t;


#define MAX_SURF_MODELSURFS		4096

d3d_surfstate_t d3d_SurfState;
msurface_t *d3d_BrushSurfs[MAX_SURF_MODELSURFS];
int d3d_NumBrushSurfs = 0;


// fixme - shift this to load time
void D3DBrush_CreateVBOs (void)
{
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
}


void D3DBrush_ReleaseVBOs (void)
{
	SAFE_RELEASE (d3d_SurfVBO);
	SAFE_RELEASE (d3d_SurfIBO);
	SAFE_RELEASE (d3d_SurfDecl);
}


CD3DDeviceLossHandler d3d_SurfVBOHandler (D3DBrush_ReleaseVBOs, D3DBrush_CreateVBOs);


void D3DBrush_SetBuffers (void)
{
	// and set up for drawing
	D3D_SetVertexDeclaration (d3d_SurfDecl);

	D3D_SetStreamSource (0, d3d_SurfVBO, 0, sizeof (brushpolyvert_t));
	D3D_SetStreamSource (1, NULL, 0, 0);
	D3D_SetStreamSource (2, NULL, 0, 0);
	D3D_SetIndices (d3d_SurfIBO);
}


typedef struct d3d_lockstate_s
{
	UINT offset;
	UINT size;
	DWORD flags;
} d3d_lockstate_t;


// fixme - instead of locking each frame we should just reuse previous buffer contents wherever possible
// (this would be a complete pain in the arse with moving objects though)
void D3DBrush_FlushSurfaces (void)
{
	// no rendering
	if (!d3d_NumBrushSurfs) return;

	d3d_lockstate_t vblock = {0, 0, D3DLOCK_DISCARD};
	d3d_lockstate_t iblock = {0, 0, D3DLOCK_DISCARD};

	brushpolyvert_t *verts = NULL;
	unsigned short *ndx = NULL;

	if (d3d_SurfState.TotalVertexes + d3d_SurfState.VertsToLock >= MaxSurfVertexes)
		d3d_SurfState.TotalVertexes = 0;
	else
	{
		vblock.offset = d3d_SurfState.TotalVertexes * sizeof (brushpolyvert_t);
		vblock.size = d3d_SurfState.VertsToLock * sizeof (brushpolyvert_t);
		vblock.flags = D3DLOCK_NOOVERWRITE;
	}

	// we always lock the vertex buffer in both modes
	hr = d3d_SurfVBO->Lock (vblock.offset, vblock.size, (void **) &verts, vblock.flags);
	if (FAILED (hr)) Sys_Error ("D3DBrush_FlushSurfaces: failed to lock vertex buffer");

	// always cutoff at 2 as it will be a single draw call anyway so there's no need to use the index buffer
	if (d3d_NumBrushSurfs < r_surfacebatchcutoff.integer || d3d_NumBrushSurfs < 2)
	{
		// with a lower number of surfs we can just call drapprimitive directly on them
		// this is a balancing act between the overhead of extra draw calls vs the overhead of
		// filling a dynamic index buffer in addition to a dynamic vertex buffer
		for (int i = 0; i < d3d_NumBrushSurfs; i++)
		{
			msurface_t *surf = d3d_BrushSurfs[i];

			// no more software transforms (yayy!)
			memcpy (verts, surf->verts, surf->numverts * sizeof (brushpolyvert_t));
			verts += surf->numverts;
		}

		hr = d3d_SurfVBO->Unlock ();
		if (FAILED (hr)) Sys_Error ("D3DBrush_FlushSurfaces: failed to unlock vertex buffer");
		d3d_RenderDef.numlock++;

		// check that all of our states are nice and correct
		D3DHLSL_CheckCommit ();

		for (int i = 0; i < d3d_NumBrushSurfs; i++)
		{
			msurface_t *surf = d3d_BrushSurfs[i];

			d3d_Device->DrawPrimitive (D3DPT_TRIANGLESTRIP, d3d_SurfState.TotalVertexes, surf->numverts - 2);
			d3d_SurfState.TotalVertexes += surf->numverts;
			d3d_RenderDef.numdrawprim++;
		}
	}
	else if (d3d_NumBrushSurfs)
	{
		if (d3d_SurfState.TotalIndexes + d3d_SurfState.IndexesToLock >= MaxSurfIndexes)
			d3d_SurfState.TotalIndexes = 0;
		else
		{
			iblock.offset = d3d_SurfState.TotalIndexes * sizeof (unsigned short);
			iblock.size = d3d_SurfState.IndexesToLock * sizeof (unsigned short);
			iblock.flags = D3DLOCK_NOOVERWRITE;
		}

		hr = d3d_SurfIBO->Lock (iblock.offset, iblock.size, (void **) &ndx, iblock.flags);
		if (FAILED (hr)) Sys_Error ("D3DBrush_FlushSurfaces: failed to lock index buffer");

		for (int i = 0, ndxofs = d3d_SurfState.TotalVertexes; i < d3d_NumBrushSurfs; i++)
		{
			msurface_t *surf = d3d_BrushSurfs[i];
			unsigned short *srcindexes = surf->indexes;
			int n = (surf->numindexes + 7) >> 3;

			// we can't just memcpy the indexes as we need to add an offset to each so instead we'll Duff the bastards
			switch (surf->numindexes % 8)
			{
			case 0: do {*ndx++ = ndxofs + *srcindexes++;
			case 7: *ndx++ = ndxofs + *srcindexes++;
			case 6: *ndx++ = ndxofs + *srcindexes++;
			case 5: *ndx++ = ndxofs + *srcindexes++;
			case 4: *ndx++ = ndxofs + *srcindexes++;
			case 3: *ndx++ = ndxofs + *srcindexes++;
			case 2: *ndx++ = ndxofs + *srcindexes++;
			case 1: *ndx++ = ndxofs + *srcindexes++;
			} while (--n > 0);
			}

			// no more software transforms (yayy!)
			memcpy (verts, surf->verts, surf->numverts * sizeof (brushpolyvert_t));
			verts += surf->numverts;
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
	if (ent != d3d_SurfState.SurfEnt)
	{
		// flush everything using the previous entity matrix
		D3DBrush_FlushSurfaces ();

		if (ent)
		{
			// build the correct transformation for this entity
			D3DMATRIX m;

			D3DMatrix_Multiply (&m, &ent->matrix, &d3d_ModelViewProjMatrix);
			D3DHLSL_SetWorldMatrix (&m);

			D3DMatrix_Multiply (&m, &ent->matrix, &d3d_ViewMatrix);
			D3DHLSL_SetFogMatrix (&m);
		}
		else
		{
			// restore the world matrix
			D3DHLSL_SetWorldMatrix (&d3d_ModelViewProjMatrix);
			D3DHLSL_SetFogMatrix (&d3d_ViewMatrix);
		}

		d3d_SurfState.SurfEnt = ent;
	}

	// if the list is full we must flush it before appending more
	if (d3d_NumBrushSurfs >= MAX_SURF_MODELSURFS) D3DBrush_FlushSurfaces ();
	if (d3d_SurfState.VertsToLock >= MaxSurfVertexes) D3DBrush_FlushSurfaces ();
	if (d3d_SurfState.IndexesToLock >= MaxSurfIndexes) D3DBrush_FlushSurfaces ();

	// grab a new one from the list
	d3d_BrushSurfs[d3d_NumBrushSurfs++] = surf;

	// increment the lock size counters
	d3d_SurfState.VertsToLock += surf->numverts;
	d3d_SurfState.IndexesToLock += surf->numindexes;
	d3d_RenderDef.brush_polys++;
}


void D3DBrush_Begin (void)
{
	D3DBrush_SetBuffers ();

	d3d_SurfState.SurfEnt = NULL;
	d3d_SurfState.Alpha = -1;
	d3d_SurfState.tmu0tex = NULL;
	d3d_SurfState.tmu1tex = NULL;
	d3d_SurfState.tmu2tex = NULL;
	d3d_SurfState.ShaderPass = FX_PASS_NOTBEGUN;
}


void D3DBrush_End (void)
{
	// draw anything left over
	D3DBrush_FlushSurfaces ();

	if (d3d_SurfState.SurfEnt)
	{
		// restore the world matrix
		D3DHLSL_SetWorldMatrix (&d3d_ModelViewProjMatrix);
		D3DHLSL_SetFogMatrix (&d3d_ViewMatrix);
	}
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
	if (thisalpha != d3d_SurfState.Alpha) recommit = true;
	if (ms->textures[TEXTURE_LIGHTMAP] != d3d_SurfState.tmu0tex) recommit = true;
	if (ms->textures[TEXTURE_DIFFUSE] != d3d_SurfState.tmu1tex) recommit = true;
	if (ms->textures[TEXTURE_LUMA] != d3d_SurfState.tmu2tex) recommit = true;

	// if we need to change state here we must flush anything batched so far before doing so
	// either way we flag a commit pending too
	if (recommit) D3DBrush_FlushSurfaces ();

	if (recommit)
	{
		D3DHLSL_SetTexture (0, ms->textures[TEXTURE_LIGHTMAP]);
		D3DHLSL_SetTexture (1, ms->textures[TEXTURE_DIFFUSE]);
		D3DHLSL_SetTexture (2, ms->textures[TEXTURE_LUMA]);

		d3d_SurfState.tmu0tex = ms->textures[TEXTURE_LIGHTMAP];
		d3d_SurfState.tmu1tex = ms->textures[TEXTURE_DIFFUSE];
		d3d_SurfState.tmu2tex = ms->textures[TEXTURE_LUMA];
	}

	if (thisalpha != d3d_SurfState.Alpha)
	{
		D3DHLSL_SetAlpha ((float) thisalpha / 255.0f);
		d3d_SurfState.Alpha = thisalpha;
	}

	if (ms->shaderpass != d3d_SurfState.ShaderPass)
	{
		// now do the pass switch after all state has been set
		D3DBrush_SetShader (ms->shaderpass);
		d3d_SurfState.ShaderPass = ms->shaderpass;
	}

	D3DBrush_SubmitSurface (surf, ms->ent);
}


