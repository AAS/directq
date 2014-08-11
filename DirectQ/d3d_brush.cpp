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

#define BRUSH_HWTLMODE (d3d_GlobalCaps.supportHardwareTandL)

void D3DBrush_SetShader (int ShaderNum)
{
	// common
	D3D_SetTextureMipmap (0, d3d_TexFilter, d3d_MipFilter);
	D3D_SetTextureMipmap (1, D3DTEXF_LINEAR, D3DTEXF_NONE);

	D3D_SetTextureAddress (0, D3DTADDRESS_WRAP);
	D3D_SetTextureAddress (1, D3DTADDRESS_CLAMP);

	switch (ShaderNum)
	{
	case FX_PASS_WORLD_NOLUMA:
	case FX_PASS_WORLD_NOLUMA_ALPHA:
		break;

	default:
		// some kind of luma style
		D3D_SetTextureAddress (2, D3DTADDRESS_WRAP);
		D3D_SetTextureMipmap (2, d3d_TexFilter, d3d_MipFilter);
		break;
	}

	D3DHLSL_SetPass (ShaderNum);
}


typedef struct d3d_surfstate_s
{
	int NumVertexes;
	int NumListIndexes;
	int NumStripIndexes;

	int MaxIndexes;
	int MaxVertexes;

	int FirstIndex;
	int FirstVertex;

	int Alpha;
	int ShaderPass;
	int StreamOffset;

	entity_t *CurrentEnt;

	D3DFORMAT LightmapFormat;

	LPDIRECT3DTEXTURE9 Lightmap;
	LPDIRECT3DTEXTURE9 Diffuse;
	LPDIRECT3DTEXTURE9 Luma;

	msurface_t *PreviousSurf;
	msurface_t **BatchedSurfaces;
	int NumSurfaces;
} d3d_surfstate_t;


d3d_surfstate_t d3d_SurfState;

#define MAX_STREAM_VERTS	60000

LPDIRECT3DINDEXBUFFER9 d3d_BrushIBO;
LPDIRECT3DVERTEXBUFFER9 d3d_BrushVBO;
LPDIRECT3DVERTEXDECLARATION9 d3d_SurfDecl = NULL;
brushpolyvert_t *d3d_BrushVerts = NULL;

void Mod_RecalcNodeBBox (mnode_t *node);
void Mod_CalcBModelBBox (model_t *mod, brushhdr_t *hdr);

// qbsp guarantees no more than 64 verts per surf
unsigned short surfindexes[(64 - 2) * 3];
unsigned short stripindexes[64];

extern int LightmapWidth, LightmapHeight;

void D3DBrush_BuildPolygonForSurface (brushhdr_t *hdr, msurface_t *surf, brushpolyvert_t *verts)
{
	surf->mins[0] = surf->mins[1] = surf->mins[2] = 99999999;
	surf->maxs[0] = surf->maxs[1] = surf->maxs[2] = -99999999;

	for (int v = 0, v2 = surf->numvertexes; v < surf->numvertexes; v++, v2--)
	{
		int stripdst = v ? (v * 2 - 1) : 0;
		int lindex = hdr->dsurfedges[surf->firstedge + v];
		float *vec;

		if (stripdst >= surf->numvertexes) stripdst = v2 * 2;

		if (lindex > 0)
			vec = hdr->dvertexes[hdr->dedges[lindex].v[0]].point;
		else vec = hdr->dvertexes[hdr->dedges[-lindex].v[1]].point;

		for (int x = 0; x < 3; x++)
		{
			verts[stripdst].xyz[x] = vec[x];

			if (surf->mins[x] > vec[x]) surf->mins[x] = vec[x];
			if (surf->maxs[x] < vec[x]) surf->maxs[x] = vec[x];
		}

		float st[2] =
		{
			(DotProduct (vec, surf->texinfo->vecs[0]) + surf->texinfo->vecs[0][3]),
			(DotProduct (vec, surf->texinfo->vecs[1]) + surf->texinfo->vecs[1][3])
		};

		if (surf->texinfo->texture == r_notexture_mip)
		{
			verts[stripdst].st[0][0] = verts[stripdst].st[1][0] = (st[0] - surf->texinfo->vecs[0][3]) / 8.0f;
			verts[stripdst].st[0][1] = verts[stripdst].st[1][1] = (st[1] - surf->texinfo->vecs[1][3]) / 8.0f;
		}
		else if (surf->flags & SURF_DRAWTURB)
		{
			verts[stripdst].st[0][0] = verts[stripdst].st[1][0] = (st[0] - surf->texinfo->vecs[0][3]) / 64.0f;
			verts[stripdst].st[0][1] = verts[stripdst].st[1][1] = (st[1] - surf->texinfo->vecs[1][3]) / 64.0f;
		}
		else
		{
			verts[stripdst].st[0][0] = st[0] / surf->texinfo->texture->size[0];
			verts[stripdst].st[0][1] = st[1] / surf->texinfo->texture->size[1];
		}

		if (!(surf->flags & SURF_DRAWSKY) && !(surf->flags & SURF_DRAWTURB))
		{
			verts[stripdst].st[1][0] = ((st[0] - surf->texturemins[0]) + (surf->LightRect.left * 16) + 8) / (float) (LightmapWidth * 16);
			verts[stripdst].st[1][1] = ((st[1] - surf->texturemins[1]) + (surf->LightRect.top * 16) + 8) / (float) (LightmapHeight * 16);
		}
	}

	for (int v = 0; v < 3; v++)
	{
		// expand the bbox by 1 unit in each direction to ensure that marginal surfs don't get culled
		// (needed for R_RecursiveWorldNode avoidance)
		surf->mins[v] -= 1.0f;
		surf->maxs[v] += 1.0f;

		// get final mindpoint
		surf->midpoint[v] = surf->mins[v] + (surf->maxs[v] - surf->mins[v]) * 0.5f;
	}
}


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

	// ensure because this gets called at map load as well as at vid_restart
	SAFE_RELEASE (d3d_BrushIBO);
	SAFE_RELEASE (d3d_BrushVBO);

	// we can't do this at load time because we don't know how many vertexes we're going to need yet...
	int totalverts = 0;
	int totalsurfs = 0;

	if (!cl.model_precache) return;

	// first pass counts the verts and sets up the firstvertex member for each surf (we could merge this with the loader but it's no big deal...)
	for (int j = 1; j < MAX_MODELS; j++)
	{
		model_t *m = cl.model_precache[j];

		if (!m) break;
		if (m->type != mod_brush) continue;
		if (m->name[0] == '*') continue;

		totalverts += m->brushhdr->numsurfvertexes;
		totalsurfs += m->brushhdr->numsurfaces;
	}

	if (!totalsurfs) return;
	if (!totalverts) return;

	d3d_SurfState.MaxVertexes = 0;
	d3d_SurfState.MaxIndexes = 0;
	d3d_SurfState.BatchedSurfaces = (msurface_t **) RenderZone->Alloc (totalsurfs * sizeof (msurface_t *));
	d3d_SurfState.NumSurfaces = 0;

	brushpolyvert_t *verts = NULL;
	unsigned short *ndx = NULL;
	int streamoffset = 0, streamverts = 0;

	if (!BRUSH_HWTLMODE)
	{
		D3DMain_CreateVertexBuffer (totalverts * sizeof (brushpolyvert_t), D3DUSAGE_DYNAMIC, &d3d_BrushVBO);
		d3d_BrushVerts = (brushpolyvert_t *) RenderZone->Alloc (totalverts * sizeof (brushpolyvert_t));
		verts = d3d_BrushVerts;
		d3d_SurfState.FirstVertex = 0;
	}
	else
	{
		D3DMain_CreateVertexBuffer (totalverts * sizeof (brushpolyvert_t), 0, &d3d_BrushVBO);

		if (FAILED (d3d_BrushVBO->Lock (0, 0, (void **) &verts, d3d_GlobalCaps.DefaultLock)))
			Sys_Error ("D3DBrush_CreateVBOs : IDirect3DVertexBuffer9::Lock failed\n");
	}

	totalverts = 0;

	for (int j = 1; j < MAX_MODELS; j++)
	{
		model_t *m = cl.model_precache[j];

		if (!m) break;
		if (m->type != mod_brush) continue;

		if (m->name[0] == '*')
		{
			// the model has already had it's surfs calced so just recalc it's bbox
			Mod_CalcBModelBBox (m, m->brushhdr);
			continue;
		}

		// alloc the surfs into the vertex buffer in as close to the same order they will be drawn as possible
		texture_t *tex = NULL;
		brushhdr_t *hdr = m->brushhdr;
		msurface_t *surf = hdr->surfaces;

		for (int i = 0; i < hdr->numsurfaces; i++, surf++)
		{
			// chain by texture
			surf->texturechain = surf->texinfo->texture->texturechain;
			surf->texinfo->texture->texturechain = surf;

			// accumumate to index batch
			d3d_SurfState.MaxIndexes += surf->numindexes;
			d3d_SurfState.MaxVertexes += surf->numvertexes;
		}

		msurface_t *lightmap_surfaces[MAX_LIGHTMAPS];

		for (int i = 0; i < hdr->numtextures; i++)
		{
			if (!(tex = hdr->textures[i])) continue;
			if (!(surf = tex->texturechain)) continue;

			for (int k = 0; k < MAX_LIGHTMAPS; k++)
				lightmap_surfaces[k] = NULL;

			for (; surf; surf = surf->texturechain)
			{
				// this is a fixup for sky and turb surfs
				if (surf->LightmapTextureNum < 0)
					surf->LightmapTextureNum = 0;

				surf->lightmapchain = lightmap_surfaces[surf->LightmapTextureNum];
				lightmap_surfaces[surf->LightmapTextureNum] = surf;
			}

			for (int k = 0; k < MAX_LIGHTMAPS; k++)
			{
				if (!(surf = lightmap_surfaces[k])) continue;

				int batchverts = 0;

				for (; surf; surf = surf->lightmapchain)
				{
					D3DBrush_BuildPolygonForSurface (hdr, surf, verts);

					if (!BRUSH_HWTLMODE)
					{
						surf->streamoffset = -1;
						surf->firstvertex = totalverts;
						surf->vertexrange = surf->firstvertex + surf->numvertexes;
						totalverts += surf->numvertexes;
					}
					else
					{
						// partition the vertex buffer so that we can reasonably index into it
						if (totalverts + surf->numvertexes > MAX_STREAM_VERTS)
						{
							streamoffset = streamverts * sizeof (brushpolyvert_t);
							totalverts = 0;
						}

						surf->streamoffset = streamoffset;
						surf->firstvertex = totalverts;
						surf->vertexrange = surf->firstvertex + surf->numvertexes;

						totalverts += surf->numvertexes;
						streamverts += surf->numvertexes;
					}

					verts += surf->numvertexes;
					batchverts += surf->numvertexes;
				}
			}

			tex->texturechain = NULL;
		}

		Mod_RecalcNodeBBox (hdr->nodes);
		Mod_CalcBModelBBox (m, hdr);
	}

	if (BRUSH_HWTLMODE)
	{
		d3d_BrushVBO->Unlock ();
		d3d_RenderDef.numlock++;
	}

	// take a minimum index buffer size of 1 MB (512k indexes) - it doesn't have to do a discard
	// lock so often, and when it does it can do it quickly enough (measured)
	if (d3d_SurfState.MaxIndexes < 512 * 1024) d3d_SurfState.MaxIndexes = 512 * 1024;

	// also ensure room for degenerate tri indexes
	if (d3d_SurfState.MaxIndexes < d3d_SurfState.MaxVertexes + ((totalsurfs - 1) * 5)) d3d_SurfState.MaxIndexes = d3d_SurfState.MaxVertexes + ((totalsurfs - 1) * 5);

	// now create our index buffer
	D3DMain_CreateIndexBuffer (d3d_SurfState.MaxIndexes, D3DUSAGE_DYNAMIC, &d3d_BrushIBO);
	hr = d3d_BrushIBO->Lock (0, 0, (void **) &ndx, d3d_GlobalCaps.DiscardLock);

	if (FAILED (hr)) Sys_Error ("D3DBrush_BatchSurface : IDirect3DIndexBuffer9::Lock failed\n");

	d3d_BrushIBO->Unlock ();
	d3d_RenderDef.numlock++;
	d3d_SurfState.FirstIndex = 0;

	// calc base surf indexes (we really only need to do this once but it does no harm)
	for (int i = 0; i < 64; i++) stripindexes[i] = i;

	// and now for converting a strip to a tri list
	ndx = surfindexes;

	// qbsp guarantees no more than 64 verts per surf
	for (int i = 2; i < 64; i++, ndx += 3)
	{
		ndx[0] = i - 2;
		ndx[1] = (i & 1) ? i : i - 1;
		ndx[2] = (i & 1) ? i - 1 : i;
	}
}


void D3DBrush_ReleaseVBOs (void)
{
	SAFE_RELEASE (d3d_BrushIBO);
	SAFE_RELEASE (d3d_BrushVBO);
	SAFE_RELEASE (d3d_SurfDecl);
}


CD3DDeviceLossHandler d3d_SurfVBOHandler (D3DBrush_ReleaseVBOs, D3DBrush_CreateVBOs);

void D3DBrush_ResetBatch (void)
{
	d3d_SurfState.NumVertexes = 0;
	d3d_SurfState.NumListIndexes = 0;
	d3d_SurfState.NumStripIndexes = 0;
	d3d_SurfState.NumSurfaces = 0;
	d3d_SurfState.PreviousSurf = NULL;
}


// we can't just memcpy the indexes as we need to add an offset to each so instead we'll Duff the bastards
// (8 seems optimal in testing)
#define D3DBrush_TransferIndexes(offset, count, source) \
	{ \
		int n = (count + 7) >> 3; \
		unsigned short *src = source; \
		 \
		switch (count % 8) \
		{ \
		case 0: do {*ndx++ = offset + *src++; \
		case 7: *ndx++ = offset + *src++; \
		case 6: *ndx++ = offset + *src++; \
		case 5: *ndx++ = offset + *src++; \
		case 4: *ndx++ = offset + *src++; \
		case 3: *ndx++ = offset + *src++; \
		case 2: *ndx++ = offset + *src++; \
		case 1: *ndx++ = offset + *src++; \
		} while (--n > 0); \
		} \
	}


#define D3DBrush_TransferVertex(dest, source) \
	(dest)->xyz[0] = (source)->xyz[0]; \
	(dest)->xyz[1] = (source)->xyz[1]; \
	(dest)->xyz[2] = (source)->xyz[2]; \
	(dest)->st[0][0] = (source)->st[0][0]; \
	(dest)->st[0][1] = (source)->st[0][1]; \
	(dest)->st[1][0] = (source)->st[1][0]; \
	(dest)->st[1][1] = (source)->st[1][1]; \
	(dest)++; \
	(source)++;

#define D3DBrush_TransferVertexes(dest, source, count) \
	{ \
		int n = (count + 7) >> 3; \
		brushpolyvert_t *src = source; \
		 \
		switch (count % 8) \
		{ \
		case 0: do {D3DBrush_TransferVertex (dest, src); \
		case 7: D3DBrush_TransferVertex (dest, src); \
		case 6: D3DBrush_TransferVertex (dest, src); \
		case 5: D3DBrush_TransferVertex (dest, src); \
		case 4: D3DBrush_TransferVertex (dest, src); \
		case 3: D3DBrush_TransferVertex (dest, src); \
		case 2: D3DBrush_TransferVertex (dest, src); \
		case 1: D3DBrush_TransferVertex (dest, src); \
		} while (--n > 0); \
		} \
	}


void D3DBrush_LockVBO (void **verts, int numverts)
{
	if (d3d_SurfState.FirstVertex + numverts >= d3d_SurfState.MaxVertexes)
	{
		if (FAILED (d3d_BrushVBO->Lock (0, 0, verts, d3d_GlobalCaps.DiscardLock)))
			Sys_Error ("D3DBrush_FlushSurfaces : IDirect3DVertexBuffer9::Lock failed");

		d3d_SurfState.FirstVertex = 0;
	}
	else
	{
		hr = d3d_BrushVBO->Lock (d3d_SurfState.FirstVertex * sizeof (brushpolyvert_t),
			numverts * sizeof (brushpolyvert_t),
			verts,
			d3d_GlobalCaps.NoOverwriteLock);

		if (FAILED (hr)) Sys_Error ("D3DBrush_FlushSurfaces : IDirect3DVertexBuffer9::Lock failed");
	}
}


void D3DBrush_LockIBO (void **indexes, int numindexes)
{
	// the index buffer was sized so that the entire draw can fit in so that we can avoid checking for overflow per surf
	if (d3d_SurfState.FirstIndex + numindexes >= d3d_SurfState.MaxIndexes)
	{
		// discard lock
		if (FAILED (d3d_BrushIBO->Lock (0, 0, indexes, d3d_GlobalCaps.DiscardLock)))
			Sys_Error ("D3DBrush_BatchSurface : IDirect3DIndexBuffer9::Lock failed\n");

		d3d_SurfState.FirstIndex = 0;
	}
	else
	{
		// no overwrite lock
		hr = d3d_BrushIBO->Lock (d3d_SurfState.FirstIndex * sizeof (unsigned short),
			numindexes * sizeof (unsigned short),
			indexes,
			d3d_GlobalCaps.NoOverwriteLock);

		if (FAILED (hr)) Sys_Error ("D3DBrush_BatchSurface : IDirect3DIndexBuffer9::Lock failed\n");
	}
}


void D3DBrush_DrawIndexedTriStripSW (void)
{
	brushpolyvert_t *verts = NULL;
	unsigned short *ndx = NULL;

	D3DBrush_LockVBO ((void **) &verts, d3d_SurfState.NumVertexes);
	D3DBrush_LockIBO ((void **) &ndx, d3d_SurfState.NumStripIndexes);

	int numstripverts = 0;
	int numprimitives = 0;

	// the first surf forms the beginning of the strip
	msurface_t *surf = d3d_SurfState.BatchedSurfaces[0];

	D3DBrush_TransferVertexes (verts, &d3d_BrushVerts[surf->firstvertex], surf->numvertexes);
	D3DBrush_TransferIndexes (numstripverts, surf->numvertexes, stripindexes);
	numstripverts += surf->numvertexes;
	numprimitives += surf->numvertexes - 2;

	for (int s = 1; s < d3d_SurfState.NumSurfaces; s++)
	{
		// note - this is the previous surface...
		if (surf->numvertexes & 1)
		{
			// odd number of verts in previous strip, duplicate the last vertex for a single degenerate tri
			*ndx++ = numstripverts - 1;
			numprimitives++;
		}

		// add degenerate tris to join the strips
		*ndx++ = numstripverts - 1;
		*ndx++ = numstripverts;
		*ndx++ = numstripverts;
		*ndx++ = numstripverts + 1;

		// not sure that i see the logic of why this adds 6 and not 4, unless we're compensating for
		// the two we removed from the previous surface... (which we probably are)
		numprimitives += 6;

		// ...now set the current surface
		surf = d3d_SurfState.BatchedSurfaces[s];

		// now add the new surface as extra verts to the strip
		D3DBrush_TransferVertexes (verts, &d3d_BrushVerts[surf->firstvertex], surf->numvertexes);
		D3DBrush_TransferIndexes (numstripverts, surf->numvertexes, stripindexes);
		numprimitives += surf->numvertexes - 2;
		numstripverts += surf->numvertexes;
	}

	d3d_BrushIBO->Unlock ();
	d3d_RenderDef.numlock++;

	d3d_BrushVBO->Unlock ();
	d3d_RenderDef.numlock++;

	// d3d_SurfState.NumVertexes should be == numstripverts but we need to keep numstripverts so that we can get the correct offset for each index
	D3D_SetStreamSource (0, d3d_BrushVBO, d3d_SurfState.FirstVertex * sizeof (brushpolyvert_t), sizeof (brushpolyvert_t));
	D3D_DrawIndexedPrimitive (D3DPT_TRIANGLESTRIP, 0, d3d_SurfState.NumVertexes, d3d_SurfState.FirstIndex, numprimitives);

	d3d_SurfState.FirstVertex += d3d_SurfState.NumVertexes;
	d3d_SurfState.FirstIndex += d3d_SurfState.NumStripIndexes;
}


void D3DBrush_DrawDegenTriStrip (void)
{
	brushpolyvert_t *verts = NULL;

	// make extra space for degenerate tri verts
	d3d_SurfState.NumVertexes += ((d3d_SurfState.NumSurfaces - 1) * 5);

	D3DBrush_LockVBO ((void **) &verts, d3d_SurfState.NumVertexes);

	msurface_t *surf;
	int numstripverts = 0;

	// the first surf forms the beginning of the strip
	surf = d3d_SurfState.BatchedSurfaces[0];

	D3DBrush_TransferVertexes (verts, &d3d_BrushVerts[surf->firstvertex], surf->numvertexes);
	numstripverts += surf->numvertexes;

	for (int s = 1; s < d3d_SurfState.NumSurfaces; s++)
	{
		// fixme - would these degenerate tris go better if we used indexes (we would just need to dupe the indexes,
		// not the full verts)
		if (surf->numvertexes & 1)
		{
			// odd number of verts in previous strip, duplicate the last vertex for a single degenerate tri
			D3DBrush_TransferVertexes (verts, &d3d_BrushVerts[surf->vertexrange - 1], 1);
			numstripverts++;
		}

		// add degenerate tris to join the trips
		D3DBrush_TransferVertexes (verts, &d3d_BrushVerts[surf->vertexrange - 1], 1);

		// now update the surf
		surf = d3d_SurfState.BatchedSurfaces[s];

		D3DBrush_TransferVertexes (verts, &d3d_BrushVerts[surf->firstvertex], 1);
		D3DBrush_TransferVertexes (verts, &d3d_BrushVerts[surf->firstvertex], 1);
		D3DBrush_TransferVertexes (verts, &d3d_BrushVerts[surf->firstvertex + 1], 1);

		numstripverts += 4;

		// now add the new surface as extra verts to the strip
		D3DBrush_TransferVertexes (verts, &d3d_BrushVerts[surf->firstvertex], surf->numvertexes);
		numstripverts += surf->numvertexes;
	}

	d3d_BrushVBO->Unlock ();
	d3d_RenderDef.numlock++;

	D3D_SetStreamSource (0, d3d_BrushVBO, 0, sizeof (brushpolyvert_t));
	D3D_DrawPrimitive (D3DPT_TRIANGLESTRIP, d3d_SurfState.FirstVertex, numstripverts - 2);

	d3d_SurfState.FirstVertex += d3d_SurfState.NumVertexes;
}


void D3DBrush_DrawIndexedListSW (void)
{
	unsigned short *ndx = NULL;
	brushpolyvert_t *verts = NULL;

	D3DBrush_LockVBO ((void **) &verts, d3d_SurfState.NumVertexes);
	D3DBrush_LockIBO ((void **) &ndx, d3d_SurfState.NumListIndexes);

	int NumPrimitives = 0;
	int IndexOffset = 0;

	for (int s = 0; s < d3d_SurfState.NumSurfaces; s++)
	{
		msurface_t *surf = d3d_SurfState.BatchedSurfaces[s];

		// transfer
		D3DBrush_TransferVertexes (verts, &d3d_BrushVerts[surf->firstvertex], surf->numvertexes);
		D3DBrush_TransferIndexes (IndexOffset, surf->numindexes, surfindexes);

		NumPrimitives += surf->numvertexes - 2;
		IndexOffset += surf->numvertexes;
	}

	d3d_BrushVBO->Unlock ();
	d3d_RenderDef.numlock++;
	d3d_BrushIBO->Unlock ();
	d3d_RenderDef.numlock++;

	D3D_SetStreamSource (0, d3d_BrushVBO, d3d_SurfState.FirstVertex * sizeof (brushpolyvert_t), sizeof (brushpolyvert_t));
	D3D_DrawIndexedPrimitive (0, d3d_SurfState.NumVertexes, d3d_SurfState.FirstIndex, NumPrimitives);

	d3d_SurfState.FirstVertex += d3d_SurfState.NumVertexes;
	d3d_SurfState.FirstIndex += d3d_SurfState.NumListIndexes;
}


void D3DBrush_DrawUnbatched (void)
{
	D3D_SetStreamSource (0, d3d_BrushVBO, d3d_SurfState.StreamOffset, sizeof (brushpolyvert_t));

	for (int s = 0; s < d3d_SurfState.NumSurfaces; s++)
	{
		msurface_t *surf = d3d_SurfState.BatchedSurfaces[s];

		D3D_DrawPrimitive (D3DPT_TRIANGLESTRIP, surf->firstvertex, surf->numvertexes - 2);
	}
}


void D3DBrush_DrawIndexedListHW (void)
{
	unsigned short *ndx = NULL;

	D3DBrush_LockIBO ((void **) &ndx, d3d_SurfState.NumListIndexes);

	int FirstVertex = 0x7fffffff;
	int LastVertex = 0;
	int PrimitiveCount = 0;

	// now blast in the indexes
	for (int s = 0; s < d3d_SurfState.NumSurfaces; s++)
	{
		msurface_t *surf = d3d_SurfState.BatchedSurfaces[s];

		D3DBrush_TransferIndexes (surf->firstvertex, surf->numindexes, surfindexes);

		// eval DIP params
		if (surf->firstvertex < FirstVertex) FirstVertex = surf->firstvertex;
		if (surf->vertexrange > LastVertex) LastVertex = surf->vertexrange;

		// this should be the same as NumIndexes / 3 always
		PrimitiveCount += (surf->numvertexes - 2);
	}

	d3d_BrushIBO->Unlock ();
	d3d_RenderDef.numlock++;

	D3D_SetStreamSource (0, d3d_BrushVBO, d3d_SurfState.StreamOffset, sizeof (brushpolyvert_t));

	// note: the documentation is seriously misleading here; numvertexes is not the number of vertexes used in the
	// draw call, it is the range from the lowest numbered vert to the highest numbered, inclusive.
	D3D_DrawIndexedPrimitive (FirstVertex,
		LastVertex - FirstVertex,
		d3d_SurfState.FirstIndex,
		PrimitiveCount);

	d3d_SurfState.FirstIndex += d3d_SurfState.NumListIndexes;
}


void D3DBrush_DrawIndexedTriStripHW (void)
{
	unsigned short *ndx = NULL;

	D3DBrush_LockIBO ((void **) &ndx, d3d_SurfState.NumStripIndexes);

	int FirstVertex = 0x7fffffff;
	int LastVertex = 0;
	int PrimitiveCount = 0;
	int IndexTransfer = 0;

	// the first surf forms the beginning of the strip
	msurface_t *surf = d3d_SurfState.BatchedSurfaces[0];

	D3DBrush_TransferIndexes (surf->firstvertex, surf->numvertexes, stripindexes);
	PrimitiveCount += surf->numvertexes - 2;
	IndexTransfer += surf->numvertexes;

	if (surf->firstvertex < FirstVertex) FirstVertex = surf->firstvertex;
	if (surf->vertexrange > LastVertex) LastVertex = surf->vertexrange;

	for (int s = 1; s < d3d_SurfState.NumSurfaces; s++)
	{
		// note - this is the previous surface...
		if (surf->numvertexes & 1)
		{
			// odd number of verts in previous strip, duplicate the last vertex for a single degenerate tri
			*ndx++ = surf->vertexrange - 1;
			PrimitiveCount++;
			IndexTransfer++;
		}

		// add degenerate tris to join the strips
		*ndx++ = surf->vertexrange - 1;

		// ...now set the current surface
		surf = d3d_SurfState.BatchedSurfaces[s];

		*ndx++ = surf->firstvertex;
		*ndx++ = surf->firstvertex;
		*ndx++ = surf->firstvertex + 1;

		// not sure that i see the logic of why this adds 6 and not 4, unless we're compensating for
		// the two we removed from the previous surface... (which we probably are)
		PrimitiveCount += 6;
		IndexTransfer += 4;

		// now add the new surface as extra verts to the strip
		D3DBrush_TransferIndexes (surf->firstvertex, surf->numvertexes, stripindexes);
		PrimitiveCount += surf->numvertexes - 2;
		IndexTransfer += surf->numvertexes;

		if (surf->firstvertex < FirstVertex) FirstVertex = surf->firstvertex;
		if (surf->vertexrange > LastVertex) LastVertex = surf->vertexrange;
	}

	d3d_BrushIBO->Unlock ();
	d3d_RenderDef.numlock++;

	D3D_SetStreamSource (0, d3d_BrushVBO, d3d_SurfState.StreamOffset, sizeof (brushpolyvert_t));

	D3D_DrawIndexedPrimitive (D3DPT_TRIANGLESTRIP,
		FirstVertex,
		LastVertex - FirstVertex,
		d3d_SurfState.FirstIndex,
		PrimitiveCount);

	// hardware T&L doesn't like us if we have unused regions in the index buffer
	d3d_SurfState.FirstIndex += d3d_SurfState.NumStripIndexes;
}


void D3DBrush_DrawSingleSurfaceHW (void)
{
	msurface_t *surf = d3d_SurfState.BatchedSurfaces[0];

	D3D_SetStreamSource (0, d3d_BrushVBO, d3d_SurfState.StreamOffset, sizeof (brushpolyvert_t));
	D3D_DrawPrimitive (D3DPT_TRIANGLESTRIP, surf->firstvertex, surf->numvertexes - 2);
}


cvar_t r_batchmode ("r_batchmode", "1");

void D3DBrush_FlushSurfaces (void)
{
	if (d3d_SurfState.NumSurfaces)
	{
		if (!BRUSH_HWTLMODE)
		{
			if (d3d_SurfState.NumSurfaces == 1)
				D3DBrush_DrawDegenTriStrip ();
			else if (r_batchmode.integer == 0)
				D3DBrush_DrawDegenTriStrip ();
			else if (r_batchmode.integer == 1)
				D3DBrush_DrawIndexedListSW ();
			else D3DBrush_DrawIndexedTriStripSW ();
		}
		else
		{
			if (d3d_SurfState.NumSurfaces == 1)
				D3DBrush_DrawSingleSurfaceHW ();
			else if (r_batchmode.integer == 0)
				D3DBrush_DrawUnbatched ();
			else if (r_batchmode.integer == 1)
				D3DBrush_DrawIndexedListHW ();
			else D3DBrush_DrawIndexedTriStripHW ();
		}

		D3DBrush_ResetBatch ();
	}
}


void D3DBrush_BatchSurface (msurface_t *surf)
{
	d3d_SurfState.NumVertexes += surf->numvertexes;
	d3d_SurfState.NumListIndexes += surf->numindexes;
	d3d_SurfState.NumStripIndexes += surf->numvertexes;

	if (d3d_SurfState.PreviousSurf)
	{
		if (d3d_SurfState.PreviousSurf->numvertexes & 1)
			d3d_SurfState.NumStripIndexes += 5;
		else d3d_SurfState.NumStripIndexes += 4;
	}

	d3d_SurfState.PreviousSurf = surf;
	d3d_SurfState.BatchedSurfaces[d3d_SurfState.NumSurfaces] = surf;
	d3d_SurfState.NumSurfaces++;
	d3d_RenderDef.brush_polys++;
}


void D3DBrush_PrecheckSurface (msurface_t *surf, entity_t *ent)
{
	if (surf->streamoffset != d3d_SurfState.StreamOffset)
	{
		D3DBrush_FlushSurfaces ();
		d3d_SurfState.StreamOffset = surf->streamoffset;
	}

	if (ent != d3d_SurfState.CurrentEnt)
	{
		D3DBrush_FlushSurfaces ();

		if (ent)
			D3DHLSL_SetWorldMatrix (&ent->matrix);
		else D3DHLSL_SetWorldMatrix (&d3d_ModelViewProjMatrix);

		d3d_SurfState.CurrentEnt = ent;
	}
}


void D3DBrush_Begin (void)
{
	D3D_SetVertexDeclaration (d3d_SurfDecl);

	// stream 0 is set later
	D3D_SetStreamSource (1, NULL, 0, 0);
	D3D_SetStreamSource (2, NULL, 0, 0);
	D3D_SetStreamSource (3, NULL, 0, 0);
	D3D_SetIndices (d3d_BrushIBO);

	D3DBrush_ResetBatch ();

	d3d_SurfState.StreamOffset = -1;

	d3d_SurfState.Alpha = -1;
	d3d_SurfState.CurrentEnt = NULL;
	d3d_SurfState.LightmapFormat = D3DFMT_UNKNOWN;
	d3d_SurfState.Lightmap = NULL;
	d3d_SurfState.Diffuse = NULL;
	d3d_SurfState.Luma = NULL;
	d3d_SurfState.ShaderPass = FX_PASS_NOTBEGUN;
}


void D3DBrush_End (void)
{
	D3DBrush_FlushSurfaces ();

	if (d3d_SurfState.CurrentEnt)
	{
		// restore the matrix if it had changed
		D3DHLSL_SetWorldMatrix (&d3d_ModelViewProjMatrix);
	}
}


void D3DLight_CheckSurfaceForModification (msurface_t *surf);

void D3DBrush_EmitSurface (msurface_t *surf, texture_t *tex, entity_t *ent, int alpha)
{
	// figure the shader to use
	int shaderpass = FX_PASS_NOTBEGUN;

	// check the surface for lightmap modification and also ensures that the correct lightmap tex is set
	D3DLight_CheckSurfaceForModification (surf);

	// precheck for baseline changes
	D3DBrush_PrecheckSurface (surf, ent);

	if (alpha != d3d_SurfState.Alpha)
	{
		D3DBrush_FlushSurfaces ();
		d3d_SurfState.Alpha = alpha;
		D3DHLSL_SetAlpha ((float) d3d_SurfState.Alpha / 255.0f);
	}

	// texture change checking needs to remain split out here for alpha surfaces
	if (tex->teximage->d3d_Texture != d3d_SurfState.Diffuse)
	{
		D3DBrush_FlushSurfaces ();
		d3d_SurfState.Diffuse = tex->teximage->d3d_Texture;
		D3DHLSL_SetTexture (0, d3d_SurfState.Diffuse);
	}

	if (surf->d3d_LightmapTex != d3d_SurfState.Lightmap)
	{
		D3DBrush_FlushSurfaces ();
		d3d_SurfState.Lightmap = surf->d3d_LightmapTex;
		D3DHLSL_SetTexture (1, d3d_SurfState.Lightmap);
	}

	if (tex->lumaimage && gl_fullbrights.integer)
	{
		if (tex->lumaimage->d3d_Texture != d3d_SurfState.Luma)
		{
			D3DBrush_FlushSurfaces ();
			d3d_SurfState.Luma = tex->lumaimage->d3d_Texture;
			D3DHLSL_SetTexture (2, d3d_SurfState.Luma);
		}

		shaderpass = alpha < 255 ? FX_PASS_WORLD_LUMA_ALPHA : FX_PASS_WORLD_LUMA;
	}
	else shaderpass = alpha < 255 ? FX_PASS_WORLD_NOLUMA_ALPHA : FX_PASS_WORLD_NOLUMA;

	if (shaderpass != d3d_SurfState.ShaderPass)
	{
		// now do the pass switch after all state has been set
		D3DBrush_FlushSurfaces ();
		D3DBrush_SetShader (shaderpass);
		d3d_SurfState.ShaderPass = shaderpass;
	}

	D3DBrush_BatchSurface (surf);
}


