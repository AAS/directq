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

// simple quad batcher shared by various parts of the code; demonstrates how to use the discard/nooverwrite paradigm

#include "quakedef.h"
#include "d3d_model.h"
#include "d3d_quake.h"
#include "d3d_Quads.h"


LPDIRECT3DVERTEXBUFFER9 d3d_QuadBuffer = NULL;
LPDIRECT3DINDEXBUFFER9 d3d_QuadIndexes = NULL;
LPDIRECT3DVERTEXDECLARATION9 d3d_QuadDecl = NULL;

int d3d_NumQuads = 0;


void D3DQuads_OnLoss (void)
{
	SAFE_RELEASE (d3d_QuadIndexes);
	SAFE_RELEASE (d3d_QuadBuffer);
	SAFE_RELEASE (d3d_QuadDecl);
}


void D3DQuads_OnRecover (void)
{
	if (!d3d_QuadBuffer)
	{
		D3DMain_CreateVertexBuffer (D3D_MAX_QUADS * 4 * sizeof (quadvert_t), D3DUSAGE_DYNAMIC, &d3d_QuadBuffer);
		d3d_NumQuads = 0;
	}

	if (!d3d_QuadIndexes)
	{
		unsigned short *ndx = NULL;

		D3DMain_CreateIndexBuffer16 (D3D_MAX_QUADS * 6, 0, &d3d_QuadIndexes);
		d3d_QuadIndexes->Lock (0, 0, (void **) &ndx, d3d_GlobalCaps.DefaultLock);

		// strips?  go hang yer bollocks on them.  with a 4-entry vertex cache this is just fine
		for (int i = 0, v = 0; i < D3D_MAX_QUADS; i++, v += 4, ndx += 6)
		{
			ndx[0] = v + 0;
			ndx[1] = v + 1;
			ndx[2] = v + 2;
			ndx[3] = v + 0;
			ndx[4] = v + 2;
			ndx[5] = v + 3;
		}

		d3d_QuadIndexes->Unlock ();
		d3d_RenderDef.numlock++;
	}

	if (!d3d_QuadDecl)
	{
		D3DVERTEXELEMENT9 d3d_quadlayout[] =
		{
			VDECL (0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0),
			VDECL (0, 12, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0),
			VDECL (0, 16, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0),
			D3DDECL_END ()
		};

		hr = d3d_Device->CreateVertexDeclaration (d3d_quadlayout, &d3d_QuadDecl);
		if (FAILED (hr)) Sys_Error ("D3DQuads_OnRecover: d3d_Device->CreateVertexDeclaration failed");
	}
}

CD3DDeviceLossHandler d3d_QuadsHandler (D3DQuads_OnLoss, D3DQuads_OnRecover);


int d3d_QuadsToDraw = 0;

void D3DQuads_Begin (int numquads, quadvert_t **quadverts)
{
	D3DQuads_OnRecover ();

	D3D_SetStreamSource (0, d3d_QuadBuffer, 0, sizeof (quadvert_t));
	D3D_SetStreamSource (1, NULL, 0, 0);
	D3D_SetStreamSource (2, NULL, 0, 0);
	D3D_SetStreamSource (3, NULL, 0, 0);
	D3D_SetIndices (d3d_QuadIndexes);

	D3D_SetVertexDeclaration (d3d_QuadDecl);

	if (d3d_NumQuads + numquads >= D3D_MAX_QUADS)
	{
		d3d_QuadBuffer->Lock (0, 0, (void **) quadverts, d3d_GlobalCaps.DiscardLock);
		d3d_NumQuads = 0;
	}
	else
	{
		d3d_QuadBuffer->Lock (d3d_NumQuads * 4 * sizeof (quadvert_t),
			numquads * 4 * sizeof (quadvert_t),
			(void **) quadverts,
			d3d_GlobalCaps.NoOverwriteLock);
	}

	d3d_QuadsToDraw = numquads;
}


void D3DQuads_End (void)
{
	d3d_QuadBuffer->Unlock ();
	d3d_RenderDef.numlock++;

	if (d3d_QuadsToDraw == 1)
		D3D_DrawPrimitive (D3DPT_TRIANGLEFAN, d3d_NumQuads * 4, 2);
	else D3D_DrawIndexedPrimitive (d3d_NumQuads * 4, d3d_QuadsToDraw * 4, d3d_NumQuads * 6, d3d_QuadsToDraw * 2);

	d3d_NumQuads += d3d_QuadsToDraw;
}


