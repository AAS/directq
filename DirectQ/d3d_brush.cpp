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


__inline void D3DBrush_TransferSurfaceMesh (msurface_t *surf, entity_t *ent, brushpolyvert_t **verts, unsigned short **ndx, int ndxofs)
{
	unsigned short *srcindexes = surf->indexes;
	int n = (surf->numindexes + 7) >> 3;

	// we can't just memcpy the indexes as we need to add an offset to each so instead we'll Duff the bastards
	switch (surf->numindexes % 8)
	{
	case 0: do {*ndx[0]++ = ndxofs + *srcindexes++;
	case 7: *ndx[0]++ = ndxofs + *srcindexes++;
	case 6: *ndx[0]++ = ndxofs + *srcindexes++;
	case 5: *ndx[0]++ = ndxofs + *srcindexes++;
	case 4: *ndx[0]++ = ndxofs + *srcindexes++;
	case 3: *ndx[0]++ = ndxofs + *srcindexes++;
	case 2: *ndx[0]++ = ndxofs + *srcindexes++;
	case 1: *ndx[0]++ = ndxofs + *srcindexes++;
	} while (--n > 0);
	}

	// transform in software to save on draw call and locking overhead (wouldn't IDirect3DDevice9::ProcessVertices be better???)
	// perf is about even with hardware T&L, with software we're transforming in software anyway
	if (ent)
	{
		// transform to a temp buffer so that we can fast-copy the transformed verts to the final buffer
		// this works out better than transforming directly to the VBO as we can get a DMA copy and transforms
		// can make better use of CPU cache and optimized instructions
		brushpolyvert_t *srcverts = surf->vertexes;
		static brushpolyvert_t dstverts[64];	// quake mandates no more than 64 verts on a surface thru qbsp
		D3DMATRIX *matrix = &ent->matrix;

		for (int v = 0; v < surf->numvertexes; v++, srcverts++)
		{
			D3DMatrix_TransformPoint (matrix, srcverts->xyz, dstverts[v].xyz);

			dstverts[v].st[0][0] = srcverts->st[0][0];
			dstverts[v].st[0][1] = srcverts->st[0][1];
			dstverts[v].st[1][0] = srcverts->st[1][0];
			dstverts[v].st[1][1] = srcverts->st[1][1];
		}

		memcpy (verts[0], dstverts, surf->numvertexes * sizeof (brushpolyvert_t));
		verts[0] += surf->numvertexes;
	}
	else
	{
		memcpy (verts[0], surf->vertexes, surf->numvertexes * sizeof (brushpolyvert_t));
		verts[0] += surf->numvertexes;
	}
}


__inline void D3DBrush_TransferSurface (msurface_t *surf, entity_t *ent, brushpolyvert_t *verts, unsigned short *ndx, int ndxofs)
{
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

	// transform in software to save on draw call and locking overhead (wouldn't IDirect3DDevice9::ProcessVertices be better???)
	// perf is about even with hardware T&L, with software we're transforming in software anyway
	if (ent)
	{
		// transform to a temp buffer so that we can fast-copy the transformed verts to the final buffer
		// this works out better than transforming directly to the VBO as we can get a DMA copy and transforms
		// can make better use of CPU cache and optimized instructions
		brushpolyvert_t *srcverts = surf->vertexes;
		static brushpolyvert_t dstverts[64];	// quake mandates no more than 64 verts on a surface thru qbsp
		D3DMATRIX *matrix = &ent->matrix;

		for (int v = 0; v < surf->numvertexes; v++, srcverts++)
		{
			D3DMatrix_TransformPoint (matrix, srcverts->xyz, dstverts[v].xyz);

			dstverts[v].st[0][0] = srcverts->st[0][0];
			dstverts[v].st[0][1] = srcverts->st[0][1];
			dstverts[v].st[1][0] = srcverts->st[1][0];
			dstverts[v].st[1][1] = srcverts->st[1][1];
		}

		memcpy (verts, dstverts, surf->numvertexes * sizeof (brushpolyvert_t));
	}
	else memcpy (verts, surf->vertexes, surf->numvertexes * sizeof (brushpolyvert_t));
}


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


// fit in our 1 MB buffer
#define MAX_SURF_VERTEXES 32768
#define MAX_SURF_INDEXES 65536

LPDIRECT3DVERTEXDECLARATION9 d3d_SurfDecl = NULL;
LPDIRECT3DVERTEXDECLARATION9 d3d_NodeDecl = NULL;


typedef struct d3d_surfstate_s
{
	int NumVertexes;
	int NumIndexes;
	int Alpha;
	int ShaderPass;
	LPDIRECT3DTEXTURE9 tmu0tex;
	LPDIRECT3DTEXTURE9 tmu1tex;
	LPDIRECT3DTEXTURE9 tmu2tex;
} d3d_surfstate_t;


#define MAX_SURF_MODELSURFS		4096

d3d_surfstate_t d3d_SurfState;

typedef struct d3d_brushsurf_s
{
	msurface_t *surf;
	entity_t *ent;
} d3d_brushsurf_t;

d3d_brushsurf_t d3d_BrushSurfs[MAX_SURF_MODELSURFS];
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
		if (FAILED (hr)) Sys_Error ("D3DBrush_CreateVBOs: d3d_Device->CreateVertexDeclaration failed");
	}

	if (!d3d_NodeDecl)
	{
		D3DVERTEXELEMENT9 d3d_nodelayout[] =
		{
			{0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
			{0, 12, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0},
			{0, 16, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
			D3DDECL_END ()
		};

		hr = d3d_Device->CreateVertexDeclaration (d3d_nodelayout, &d3d_NodeDecl);
		if (FAILED (hr)) Sys_Error ("D3DBrush_CreateVBOs: d3d_Device->CreateVertexDeclaration failed");
	}
}


void D3DBrush_ReleaseVBOs (void)
{
	SAFE_RELEASE (d3d_SurfDecl);
	SAFE_RELEASE (d3d_NodeDecl);
}


CD3DDeviceLossHandler d3d_SurfVBOHandler (D3DBrush_ReleaseVBOs, D3DBrush_CreateVBOs);


LPDIRECT3DVERTEXBUFFER9 d3d_BModelVBOs[MAX_MODELS];
LPDIRECT3DINDEXBUFFER9 d3d_BModelIBOs[MAX_MODELS];


typedef struct d3d_drawcall_s
{
	int FirstVertex;
	int FirstIndex;
	int NumVertexes;
	int NumIndexes;
	int ShaderPass;
	LPDIRECT3DTEXTURE9 tmu0tex;
	LPDIRECT3DTEXTURE9 tmu1tex;
	LPDIRECT3DTEXTURE9 tmu2tex;

	// only used by bmodels
	LPDIRECT3DVERTEXBUFFER9 vbo;
	LPDIRECT3DINDEXBUFFER9 ibo;
	texture_t *basetex;
} d3d_drawcall_t;


void D3DBrush_InitDrawCall (d3d_drawcall_t *dc)
{
	dc->FirstIndex = 0;
	dc->FirstVertex = 0;
	dc->NumIndexes = 0;
	dc->NumVertexes = 0;
	dc->tmu0tex = NULL;
	dc->tmu1tex = NULL;
	dc->tmu2tex = NULL;
	dc->ShaderPass = FX_PASS_NOTBEGUN;

	// only bmodels use these
	dc->vbo = NULL;
	dc->ibo = NULL;
	dc->basetex = NULL;
}


void D3DBrush_ReleaseBModelVBOs (void)
{
	for (int i = 1; i < MAX_MODELS; i++)
	{
		SAFE_RELEASE (d3d_BModelVBOs[i]);
		SAFE_RELEASE (d3d_BModelIBOs[i]);
	}
}


typedef struct minsurf_s
{
	msurface_t *surf;
} minsurf_t;


int D3DBrush_SortFunc (minsurf_t *s1, minsurf_t *s2)
{
	if (s1->surf->texinfo->texture == s2->surf->texinfo->texture)
		return ((int) s1->surf->LightmapTextureNum - (int) s2->surf->LightmapTextureNum);
	else return ((int) s1->surf->texinfo->texture - (int) s2->surf->texinfo->texture);
}


// this is quite an arbitrary number and is just intended to cut off smaller boxes where it may be
// more efficient to just fill the goddam dynamic buffer.  it was largely tuned by going to the immediate
// back-right corner after you come out of the starting cave in ne_tower, looking up, and seeing which
// number gave the overall best average framerate.
cvar_t r_brushvbocutoff ("r_brushvbocutoff", "20");

void D3DBrush_BuildBModelVBOs (void)
{
	D3DBrush_ReleaseBModelVBOs ();

	if (!cl.model_precache) return;
	//if (!d3d_GlobalCaps.supportHardwareTandL) return;

	// to do - pack all of these into a single vbo???
	for (int i = 1; i < MAX_MODELS; i++)
	{
		model_t *mod = cl.model_precache[i];

		if (!mod) continue;
		if (mod->type != mod_brush) continue;
		if (!(mod->flags & MOD_BMODEL)) continue;

		mod->brushhdr->DrawCalls = NULL;
		mod->brushhdr->NumDrawCalls = 0;

		int numvbosurfaces = 0;
		int numvbovertexes = 0;
		int numvboindexes = 0;
		int maxminsurfs = SCRATCHBUF_SIZE / (sizeof (minsurf_t) + sizeof (d3d_drawcall_t));
		msurface_t *surf = mod->brushhdr->surfaces + mod->brushhdr->firstmodelsurface;
		minsurf_t *minsurfs = (minsurf_t *) scratchbuf;

		// worst case - each surf is it's own draw call
		// in practice that never happens, so we don't care really.  but we make room for them all the same.
		if (mod->brushhdr->nummodelsurfaces > maxminsurfs) continue;

		for (int s = 0; s < mod->brushhdr->nummodelsurfaces; s++, surf++)
		{
			if (surf->flags & SURF_DRAWTURB) continue;
			if (surf->flags & SURF_DRAWSKY) continue;
			if (!surf->texinfo->texture) continue;
			if (!surf->texinfo->texture->teximage) continue;

			// copy out minimal data
			minsurfs[numvbosurfaces].surf = surf;

			numvbosurfaces++;
			numvbovertexes += surf->numvertexes;
			numvboindexes += surf->numindexes;
		}

		if (numvbosurfaces < 1) continue;
		if (numvbosurfaces < r_brushvbocutoff.integer) continue;
		if (numvbovertexes > 60000) continue;

		// sort the surfaces for cache locality
		qsort (minsurfs, numvbosurfaces, sizeof (minsurf_t), (sortfunc_t) D3DBrush_SortFunc);

		brushpolyvert_t *verts = NULL;
		unsigned short *ndx = NULL;

		// create and lock the buffers for this model
		D3DMain_CreateVertexBuffer (numvbovertexes * sizeof (brushpolyvert_t), D3DUSAGE_WRITEONLY, &d3d_BModelVBOs[i]);
		hr = d3d_BModelVBOs[i]->Lock (0, 0, (void **) &verts, 0);
		if (FAILED (hr)) Sys_Error ("D3DBrush_BuildBModelVBOs : failed to lock vertex buffer");

		D3DMain_CreateIndexBuffer (numvboindexes, D3DUSAGE_WRITEONLY, &d3d_BModelIBOs[i]);
		hr = d3d_BModelIBOs[i]->Lock (0, 0, (void **) &ndx, 0);
		if (FAILED (hr)) Sys_Error ("D3DBrush_BuildBModelVBOs : failed to lock index buffer");

		// fill in buffers and draw calls
		d3d_drawcall_t *dc = (d3d_drawcall_t *) (minsurfs + numvbosurfaces);
		int numdc = 0;

		D3DBrush_InitDrawCall (dc);

		LPDIRECT3DTEXTURE9 lasttex = NULL;
		LPDIRECT3DTEXTURE9 lastlm = NULL;

		int numverts = 0;
		int numindexes = 0;
		int totalvertexes = 0;

		for (int s = 0; s < numvbosurfaces; s++)
		{
			surf = minsurfs[s].surf;

			if (surf->texinfo->texture->teximage->d3d_Texture != lasttex || surf->d3d_LightmapTex != lastlm)
			{
				if (numdc) dc++;
				numdc++;

				// fill in the rest of this draw call
				dc->basetex = surf->texinfo->texture;
				dc->FirstVertex = numverts;
				dc->FirstIndex = numindexes;
				dc->NumVertexes = 0;
				dc->NumIndexes = 0;

				// store back
				lasttex = surf->texinfo->texture->teximage->d3d_Texture;
				lastlm = surf->d3d_LightmapTex;
			}

			// write in the surface mesh
			D3DBrush_TransferSurfaceMesh (surf, NULL, &verts, &ndx, totalvertexes);

			// advance counters
			dc->NumVertexes += surf->numvertexes;
			dc->NumIndexes += surf->numindexes;
			dc->vbo = d3d_BModelVBOs[i];
			dc->ibo = d3d_BModelIBOs[i];
			dc->tmu0tex = surf->d3d_LightmapTex;

			numverts += surf->numvertexes;
			numindexes += surf->numindexes;
			totalvertexes += surf->numvertexes;
		}

		hr = d3d_BModelVBOs[i]->Unlock ();
		if (FAILED (hr)) Sys_Error ("D3DBrush_BuildBModelVBOs : failed to unlock vertex buffer");

		hr = d3d_BModelIBOs[i]->Unlock ();
		if (FAILED (hr)) Sys_Error ("D3DBrush_BuildBModelVBOs : failed to unlock index buffer");

		mod->brushhdr->NumDrawCalls = numdc;
		mod->brushhdr->DrawCalls = (d3d_drawcall_t *) RenderZone->Alloc (sizeof (d3d_drawcall_t) * numdc);
		memcpy (mod->brushhdr->DrawCalls, (minsurfs + numvbosurfaces), sizeof (d3d_drawcall_t) * numdc);

		// Con_Printf ("model %s has %i draw calls (%i v %i s)\n", mod->name, numdc, numvbovertexes, numvbosurfaces);
	}
}


CD3DDeviceLossHandler d3d_BModelVBOHandler (D3DBrush_ReleaseBModelVBOs, D3DBrush_BuildBModelVBOs);

// experiment with batch breaking
cvar_t r_surfbatchcutoff ("r_surfbatchcutoff", "1");


void D3DBrush_FlushSurfaces (void)
{
	D3D_SetVertexDeclaration (d3d_SurfDecl);
	D3D_UnbindStreams ();

	if (d3d_NumBrushSurfs > r_surfbatchcutoff.integer)
	{
		brushpolyvert_t *verts = (brushpolyvert_t *) scratchbuf;
		unsigned short *ndx = (unsigned short *) (verts + MAX_SURF_VERTEXES);

		int numverts = 0;
		int numindexes = 0;

		for (int i = 0; i < d3d_NumBrushSurfs; i++)
		{
			msurface_t *surf = d3d_BrushSurfs[i].surf;

			D3DBrush_TransferSurface (surf, d3d_BrushSurfs[i].ent, &verts[numverts], &ndx[numindexes], numverts);

			numverts += surf->numvertexes;
			numindexes += surf->numindexes;
		}

		D3DHLSL_CheckCommit ();

		d3d_Device->DrawIndexedPrimitiveUP (D3DPT_TRIANGLELIST, 0, numverts, numindexes / 3,
			(((brushpolyvert_t *) scratchbuf) + MAX_SURF_VERTEXES),
			D3DFMT_INDEX16, scratchbuf, sizeof (brushpolyvert_t));

		d3d_RenderDef.numdrawprim++;
	}
	else if (d3d_NumBrushSurfs)
	{
		D3DHLSL_CheckCommit ();

		for (int i = 0; i < d3d_NumBrushSurfs; i++)
		{
			msurface_t *surf = d3d_BrushSurfs[i].surf;
			entity_t *ent = d3d_BrushSurfs[i].ent;

			if (ent)
			{
				brushpolyvert_t *srcverts = surf->vertexes;
				static brushpolyvert_t dstverts[64];	// quake mandates no more than 64 verts on a surface thru qbsp
				D3DMATRIX *matrix = &ent->matrix;

				for (int v = 0; v < surf->numvertexes; v++, srcverts++)
				{
					D3DMatrix_TransformPoint (matrix, srcverts->xyz, dstverts[v].xyz);

					dstverts[v].st[0][0] = srcverts->st[0][0];
					dstverts[v].st[0][1] = srcverts->st[0][1];
					dstverts[v].st[1][0] = srcverts->st[1][0];
					dstverts[v].st[1][1] = srcverts->st[1][1];
				}

				d3d_Device->DrawPrimitiveUP (D3DPT_TRIANGLEFAN, surf->numvertexes - 2, dstverts, sizeof (brushpolyvert_t));
			}
			else d3d_Device->DrawPrimitiveUP (D3DPT_TRIANGLEFAN, surf->numvertexes - 2, surf->vertexes, sizeof (brushpolyvert_t));

			d3d_RenderDef.numdrawprim++;
		}
	}

	// these are done outside the loop
	d3d_NumBrushSurfs = 0;
	d3d_SurfState.NumIndexes = 0;
	d3d_SurfState.NumVertexes = 0;
}


// hmmm - ideally this would just take a pointer to the modelsurf and build a count of them rather than
// needing to build a second list, but then it would break with sky and we wouldn't be able to combine the locks
// oh well, shit happens.  the next one will be better.
void D3DBrush_SubmitSurface (msurface_t *surf, entity_t *ent)
{
	// if the list is full we must flush it before appending more
	if (d3d_NumBrushSurfs >= MAX_SURF_MODELSURFS) D3DBrush_FlushSurfaces ();
	if (d3d_SurfState.NumVertexes >= MAX_SURF_VERTEXES) D3DBrush_FlushSurfaces ();
	if (d3d_SurfState.NumIndexes >= MAX_SURF_INDEXES) D3DBrush_FlushSurfaces ();

	// grab a new one from the list
	d3d_BrushSurfs[d3d_NumBrushSurfs].surf = surf;
	d3d_BrushSurfs[d3d_NumBrushSurfs].ent = ent;
	d3d_NumBrushSurfs++;

	// increment the lock size counters
	d3d_SurfState.NumVertexes += surf->numvertexes;
	d3d_SurfState.NumIndexes += surf->numindexes;

	d3d_RenderDef.brush_polys++;
}


void D3DBrush_Begin (void)
{
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
}


void D3DBrush_EmitSurface (d3d_modelsurf_t *ms)
{
	msurface_t *surf = ms->surf;
	bool recommit = false;
	byte thisalpha = 255;

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


void D3DBrush_IssueDrawCalls (d3d_drawcall_t *dc, int num_drawcalls)
{
	for (int i = 0; i < num_drawcalls; i++, dc++)
	{
		D3DHLSL_SetTexture (0, dc->tmu0tex);
		D3DHLSL_SetTexture (1, dc->tmu1tex);
		D3DHLSL_SetTexture (2, dc->tmu2tex);
		D3DBrush_SetShader (dc->ShaderPass);

		D3D_DrawIndexedPrimitive (dc->FirstVertex, dc->NumVertexes, dc->FirstIndex, dc->NumIndexes / 3);
	}
}


// fixme - we're getting too many locks here so we need to lock once only, transfer everything, then run through this drawing
void D3DBrush_TransferMainSurfaces (d3d_modelsurf_t **lightchains)
{
	D3DBrush_Begin ();

	for (int l = 0; l < MAX_LIGHTMAPS; l++)
	{
		if (!lightchains[l]) continue;

		for (d3d_modelsurf_t *ms = lightchains[l]; ms; ms = ms->chain)
		{
			D3DBrush_EmitSurface (ms);
			d3d_RenderDef.brush_polys++;
		}

		lightchains[l] = NULL;
	}

	D3DBrush_End ();
}


int D3DBrush_ModelSortFunc (entity_t **e1, entity_t **e2)
{
	return (int) (e1[0]->model - e2[0]->model);
}


void D3DBrush_DrawVBOSurfaces (void)
{
	bool drawsurfs = false;
	entity_t **d3d_BrushEntities = (entity_t **) scratchbuf;
	int d3d_NumBrushEntities = 0;

	// build a list of all such entities that we're going to draw
	for (int i = 0; i < d3d_RenderDef.numvisedicts; i++)
	{
		entity_t *ent = d3d_RenderDef.visedicts[i];
		model_t *mod = ent->model;

		if (ent->visframe != d3d_RenderDef.framecount) continue;
		if (ent->model->type != mod_brush) continue;
		if (ent->alphaval < 255) continue;
		if (!ent->update_type) continue;

		if (!mod->brushhdr->DrawCalls) continue;

		d3d_BrushEntities[d3d_NumBrushEntities] = ent;
		d3d_NumBrushEntities++;
	}

	// nothing to draw
	if (!d3d_NumBrushEntities) return;

	// sort by model for VBO cache locality
	qsort (d3d_BrushEntities, d3d_NumBrushEntities, sizeof (entity_t *), (sortfunc_t) D3DBrush_ModelSortFunc);

	// now draw them
	for (int i = 0; i < d3d_NumBrushEntities; i++)
	{
		entity_t *ent = d3d_BrushEntities[i];
		model_t *mod = ent->model;

		brushhdr_t *hdr = mod->brushhdr;
		d3d_drawcall_t *dc = hdr->DrawCalls;

		// build the correct transformation for this entity
		D3DMATRIX m;

		D3DMatrix_Multiply (&m, &ent->matrix, &d3d_ModelViewProjMatrix);
		D3DHLSL_SetWorldMatrix (&m);

		D3DMatrix_Multiply (&m, &ent->matrix, &d3d_ViewMatrix);
		D3DHLSL_SetFogMatrix (&m);

		drawsurfs = true;

		// fixme - add filtering here???
		// although the calling functions will filter too...
		for (int d = 0; d < hdr->NumDrawCalls; d++, dc++)
		{
			D3D_SetStreamSource (0, dc->vbo, 0, sizeof (brushpolyvert_t));
			D3D_SetStreamSource (1, NULL, 0, 0);
			D3D_SetStreamSource (2, NULL, 0, 0);
			D3D_SetStreamSource (3, NULL, 0, 0);

			D3D_SetIndices (dc->ibo);

			texture_t *tex = R_TextureAnimation (ent, dc->basetex);

			D3DHLSL_SetTexture (0, dc->tmu0tex);
			D3DHLSL_SetTexture (1, tex->teximage->d3d_Texture);

			if (tex->lumaimage && gl_fullbrights.integer)
			{
				dc->ShaderPass = FX_PASS_WORLD_LUMA;
				D3DHLSL_SetTexture (2, tex->lumaimage->d3d_Texture);
			}
			else dc->ShaderPass = FX_PASS_WORLD_NOLUMA;

			D3DBrush_SetShader (dc->ShaderPass);

			D3D_DrawIndexedPrimitive (dc->FirstVertex, dc->NumVertexes, dc->FirstIndex, dc->NumIndexes / 3);
		}
	}

	if (drawsurfs)
	{
		// restore the world matrix
		D3DHLSL_SetWorldMatrix (&d3d_ModelViewProjMatrix);
		D3DHLSL_SetFogMatrix (&d3d_ViewMatrix);
	}
}


void D3DBrush_ShowNodesBegin (void)
{
	D3D_SetVertexDeclaration (d3d_NodeDecl);
	D3D_UnbindStreams ();

	D3DHLSL_SetPass (FX_PASS_DRAWCOLORED);
	D3DHLSL_CheckCommit ();
}


typedef struct snvert_s
{
	float xyz[3];
	D3DCOLOR color;
	float pad[2];
} snvert_t;

extern cvar_t r_drawflat;
extern cvar_t r_shownodedepth;

void D3DBrush_ShowNodesDrawSurfaces (mnode_t *node, int depth)
{
	D3DCOLOR color = d3d_QuakePalette.standard32[((int) node) & 255];
	snvert_t *snsurf = (snvert_t *) scratchbuf;

	if (r_shownodedepth.value)
		color = D3DCOLOR_XRGB (depth & 255, depth & 255, depth & 255);

	if (node->numsurfaces)
	{
		msurface_t *surf = node->surfaces;

		// add stuff to the draw lists
		for (int c = node->numsurfaces; c; c--, surf++)
		{
			if (r_drawflat.value)
				color = d3d_QuakePalette.standard32[((int) node->plane) & 255];

			for (int i = 0; i < surf->numvertexes; i++)
			{
				snsurf[i].xyz[0] = surf->vertexes[i].xyz[0];
				snsurf[i].xyz[1] = surf->vertexes[i].xyz[1];
				snsurf[i].xyz[2] = surf->vertexes[i].xyz[2];
				snsurf[i].color = color;
			}

			d3d_Device->DrawPrimitiveUP (D3DPT_TRIANGLEFAN, surf->numvertexes - 2, snsurf, sizeof (snvert_t));
		}
	}
}


void D3DBrush_ShowLeafsDrawSurfaces (mleaf_t *leaf)
{
	D3DCOLOR color = d3d_QuakePalette.standard32[((int) leaf) & 255];
	snvert_t *snsurf = (snvert_t *) scratchbuf;

	if (leaf->nummarksurfaces)
	{
		// add stuff to the draw lists
		for (int c = 0; c < leaf->nummarksurfaces; c++)
		{
			msurface_t *surf = leaf->firstmarksurface[c];

			if (r_drawflat.value)
				color = d3d_QuakePalette.standard32[((int) surf) & 255];

			for (int i = 0; i < surf->numvertexes; i++)
			{
				snsurf[i].xyz[0] = surf->vertexes[i].xyz[0];
				snsurf[i].xyz[1] = surf->vertexes[i].xyz[1];
				snsurf[i].xyz[2] = surf->vertexes[i].xyz[2];
				snsurf[i].color = color;
			}

			d3d_Device->DrawPrimitiveUP (D3DPT_TRIANGLEFAN, surf->numvertexes - 2, snsurf, sizeof (snvert_t));
		}
	}
}


