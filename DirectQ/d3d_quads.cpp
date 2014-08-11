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

#include "quakedef.h"
#include "d3d_model.h"
#include "d3d_quake.h"
#include "d3d_quads.h"

// quad batcher
// we could use ID3DXSprite but:
// - it addrefs a texture which is slow
// - it locks each 4 verts which is slow
// - it's not flexible enough for our needs
// so we don't


d3d_quadstate_t d3d_QuadState;


// each drawn quad has 4 vertexes and 6 indexes
#define MAX_QUAD_VERTEXES	60000
#define MAX_QUAD_INDEXES	90000

LPDIRECT3DINDEXBUFFER9 d3d_QuadIBO = NULL;
LPDIRECT3DVERTEXBUFFER9 d3d_QuadVBO = NULL;
LPDIRECT3DVERTEXDECLARATION9 d3d_QuadDecl = NULL;


void D3DQuad_CreateBuffers (void)
{
	if (!d3d_QuadVBO)
	{
		D3DMain_CreateVertexBuffer (MAX_QUAD_VERTEXES * sizeof (quadvert_t), D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, &d3d_QuadVBO);
		D3D_PrelockVertexBuffer (d3d_QuadVBO);
		d3d_QuadState.TotalVertexes = 0;
		d3d_QuadState.TotalIndexes = 0;
		d3d_QuadState.LockOffset = 0;
	}

	if (!d3d_QuadDecl)
	{
		D3DVERTEXELEMENT9 d3d_quadlayout[] =
		{
			{0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
			{0, 12, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0},
			{0, 16, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
			D3DDECL_END ()
		};

		hr = d3d_Device->CreateVertexDeclaration (d3d_quadlayout, &d3d_QuadDecl);
		if (FAILED (hr)) Sys_Error ("D3DQuad_CreateBuffers: d3d_Device->CreateVertexDeclaration failed");
	}

	if (!d3d_QuadIBO)
	{
		D3DMain_CreateIndexBuffer (MAX_QUAD_INDEXES, D3DUSAGE_WRITEONLY, &d3d_QuadIBO);

		// now we fill in the index buffer; this is a non-dynamic index buffer and it only needs to be set once
		unsigned short *ndx = NULL;
		int NumQuadVerts = (MAX_QUAD_INDEXES / 6) * 4;

		hr = d3d_QuadIBO->Lock (0, 0, (void **) &ndx, 0);
		if (FAILED (hr)) Sys_Error ("D3DQuad_CreateBuffers: failed to lock index buffer");

		for (int i = 0; i < NumQuadVerts; i += 4, ndx += 6)
		{
			ndx[0] = i + 0;
			ndx[1] = i + 1;
			ndx[2] = i + 2;

			ndx[3] = i + 0;
			ndx[4] = i + 2;
			ndx[5] = i + 3;
		}

		hr = d3d_QuadIBO->Unlock ();
		if (FAILED (hr)) Sys_Error ("D3DMain_CreateBuffers: failed to unlock index buffer");
	}
}


void D3DQuad_ReleaseBuffers (void)
{
	SAFE_RELEASE (d3d_QuadIBO);
	SAFE_RELEASE (d3d_QuadVBO);
	SAFE_RELEASE (d3d_QuadDecl);
}


CD3DDeviceLossHandler d3d_QuadBuffersHandler (D3DQuad_ReleaseBuffers, D3DQuad_CreateBuffers);


void D3DQuad_Flush (void)
{
	// an unlock should always be needed
	if (d3d_QuadState.QuadVerts)
	{
		hr = d3d_QuadVBO->Unlock ();
		if (FAILED (hr)) Sys_Error ("D3DQuad_Flush: failed to unlock a vertex buffer");
		d3d_RenderDef.numlock++;
		d3d_QuadState.QuadVerts = NULL;
	}

	if (d3d_QuadState.NumVertexes)
	{
		D3D_DrawIndexedPrimitive (d3d_QuadState.FirstVertex,
			d3d_QuadState.NumVertexes,
			d3d_QuadState.FirstIndex,
			d3d_QuadState.NumIndexes / 3);
	}

	d3d_QuadState.FirstVertex = d3d_QuadState.TotalVertexes;
	d3d_QuadState.FirstIndex = d3d_QuadState.TotalIndexes;
	d3d_QuadState.NumVertexes = 0;
	d3d_QuadState.NumIndexes = 0;
}


void D3DQuad_Begin (LPDIRECT3DVERTEXDECLARATION9 decloverride)
{
	// create the buffers if needed
	D3DQuad_CreateBuffers ();

	// initial states
	d3d_QuadState.NumVertexes = 0;
	d3d_QuadState.NumIndexes = 0;
	d3d_QuadState.QuadVerts = NULL;

	// allow to override the default vertex declaration
	if (decloverride)
		D3D_SetVertexDeclaration (decloverride);
	else D3D_SetVertexDeclaration (d3d_QuadDecl);

	// and set up for drawing
	D3D_SetStreamSource (0, d3d_QuadVBO, 0, sizeof (quadvert_t));
	D3D_SetStreamSource (1, NULL, 0, 0);
	D3D_SetStreamSource (2, NULL, 0, 0);
	D3D_SetIndices (d3d_QuadIBO);
}


void D3DQuad_CheckLock (int numvertexes)
{
	if (d3d_QuadState.TotalVertexes + numvertexes >= MAX_QUAD_VERTEXES)
	{
		D3DQuad_Flush ();

		hr = d3d_QuadVBO->Lock (0, 0, (void **) &d3d_QuadState.QuadVerts, d3d_GlobalCaps.DiscardLock);
		if (FAILED (hr)) Sys_Error ("D3DQuad_CheckLock: failed to lock vertex buffer");

		d3d_QuadState.FirstVertex = d3d_QuadState.TotalVertexes = d3d_QuadState.LockOffset = 0;
		d3d_QuadState.FirstIndex = d3d_QuadState.TotalIndexes = 0;
	}
	else if (!d3d_QuadState.QuadVerts)
	{
		hr = d3d_QuadVBO->Lock
		(
			d3d_QuadState.LockOffset,
			numvertexes * sizeof (quadvert_t),
			(void **) &d3d_QuadState.QuadVerts,
			d3d_GlobalCaps.NoOverwriteLock
		);

		if (FAILED (hr)) Sys_Error ("D3DQuad_CheckLock: failed to lock vertex buffer");
	}

	// advance by the actual amount that was locked, not the amount that may or may not be drawn
	d3d_QuadState.TotalVertexes += numvertexes;
	d3d_QuadState.TotalIndexes += (numvertexes >> 2) * 6;
	d3d_QuadState.LockOffset += numvertexes * sizeof (quadvert_t);
}


void D3DQuad_Advance (void)
{
	d3d_QuadState.QuadVerts += 4;
	d3d_QuadState.NumVertexes += 4;
	d3d_QuadState.NumIndexes += 6;
}


