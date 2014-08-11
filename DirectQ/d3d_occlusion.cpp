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

// fixme - this is no longer anything to do with occlusion queries...

#include "quakedef.h"
#include "d3d_model.h"
#include "d3d_quake.h"

cvar_t r_showbboxes ("r_showbboxes", "0");

typedef struct r_bbvertex_s
{
	float xyz[3];
	D3DCOLOR color;
	float dummy[2];
} r_bbvertex_t;


LPDIRECT3DVERTEXBUFFER9 d3d_BBoxVBO = NULL;
LPDIRECT3DINDEXBUFFER9 d3d_BBoxIBO = NULL;
LPDIRECT3DVERTEXDECLARATION9 d3d_BBoxDecl = NULL;


void D3DOQ_PopulateVBO (void)
{
	r_bbvertex_t *bboxverts = NULL;

	hr = d3d_BBoxVBO->Lock (0, 0, (void **) &bboxverts, 0);
	if (FAILED (hr)) Sys_Error ("D3DOQ_CreateBuffers: failed to lock vertex buffer");

	// and fill it in properly
	for (int i = 0; i < 8; i++)
	{
		bboxverts[i].xyz[0] = (i & 1) ? -1.0f : 1.0f;
		bboxverts[i].xyz[1] = (i & 2) ? -1.0f : 1.0f;
		bboxverts[i].xyz[2] = (i & 4) ? -1.0f : 1.0f;

		if (r_showbboxes.integer > 1)
			bboxverts[i].color = D3DCOLOR_ARGB (96, 96, 96, 96);
		else bboxverts[i].color = D3DCOLOR_ARGB (255, 160, 160, 160);
	}

	hr = d3d_BBoxVBO->Unlock ();
	if (FAILED (hr)) Sys_Error ("D3DOQ_CreateBuffers: failed to unlock vertex buffer");
}


void D3DOQ_CreateBuffers (void)
{
	if (!d3d_BBoxDecl)
	{
		D3DVERTEXELEMENT9 d3d_oqlayout[] =
		{
			{0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
			{0, 12, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0},
			{0, 16, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
			D3DDECL_END ()
		};

		hr = d3d_Device->CreateVertexDeclaration (d3d_oqlayout, &d3d_BBoxDecl);
		if (FAILED (hr)) Sys_Error ("D3DDraw_CreateBuffers: d3d_Device->CreateVertexDeclaration failed");
	}

	if (!d3d_BBoxVBO)
	{
		D3DMain_CreateVertexBuffer (8 * sizeof (r_bbvertex_t), D3DUSAGE_WRITEONLY, &d3d_BBoxVBO);
		D3DOQ_PopulateVBO ();
	}

	if (!d3d_BBoxIBO)
	{
		D3DMain_CreateIndexBuffer (36, D3DUSAGE_WRITEONLY, &d3d_BBoxIBO);

		unsigned short *ndx = NULL;
		unsigned short bboxindexes[36] =
		{
			0, 2, 6, 0, 6, 4, 1, 3, 7, 1, 7, 5, 0, 1, 3, 0, 3, 2,
			4, 5, 7, 4, 7, 6, 0, 1, 5, 0, 5, 4, 2, 3, 7, 2, 7, 6
		};

		hr = d3d_BBoxIBO->Lock (0, 0, (void **) &ndx, 0);
		if (FAILED (hr)) Sys_Error ("D3DOQ_CreateBuffers: failed to lock index buffer");

		memcpy (ndx, bboxindexes, 36 * sizeof (unsigned short));

		hr = d3d_BBoxIBO->Unlock ();
		if (FAILED (hr)) Sys_Error ("D3DOQ_CreateBuffers: failed to unlock index buffer");
	}
}


bool D3DOQ_ViewInsideBBox (entity_t *ent)
{
	if (r_refdef.vieworigin[0] < ent->mins[0]) return false;
	if (r_refdef.vieworigin[1] < ent->mins[1]) return false;
	if (r_refdef.vieworigin[2] < ent->mins[2]) return false;

	if (r_refdef.vieworigin[0] > ent->maxs[0]) return false;
	if (r_refdef.vieworigin[1] > ent->maxs[1]) return false;
	if (r_refdef.vieworigin[2] > ent->maxs[2]) return false;

	// inside
	return true;
}


void D3DOQ_ReleaseQueries (void)
{
	SAFE_RELEASE (d3d_BBoxVBO);
	SAFE_RELEASE (d3d_BBoxIBO);
	SAFE_RELEASE (d3d_BBoxDecl);
}


CD3DDeviceLossHandler d3d_OQHandler (D3DOQ_ReleaseQueries, D3DOQ_CreateBuffers);


// this really has nothing to do with occlusion queries but is included here for convenience as it shares code and structures
void D3DOC_ShowBBoxes (void)
{
	if (!r_showbboxes.value) return;
	if (!r_drawentities.value) return;

	// ensure that the VBO/IBO we need are all up
	D3DOQ_CreateBuffers ();

	bool stateset = false;
	static int oldbboxes = r_showbboxes.integer;

	if (oldbboxes != r_showbboxes.integer)
	{
		D3DOQ_PopulateVBO ();
		oldbboxes = r_showbboxes.integer;
	}

	for (int i = 0; i < d3d_RenderDef.numvisedicts; i++)
	{
		entity_t *ent = d3d_RenderDef.visedicts[i];

		if (!ent->model) continue;
		if (ent->visframe != d3d_RenderDef.framecount) continue;

		if (!stateset)
		{
			// DIPUP is too slow for this...
			D3D_SetStreamSource (0, d3d_BBoxVBO, 0, sizeof (r_bbvertex_t));
			D3D_SetStreamSource (1, NULL, 0, 0);
			D3D_SetStreamSource (2, NULL, 0, 0);
			D3D_SetIndices (d3d_BBoxIBO);

			D3D_SetRenderState (D3DRS_ZWRITEENABLE, FALSE);
			D3D_SetRenderState (D3DRS_CULLMODE, D3DCULL_NONE);

			D3DHLSL_SetPass (FX_PASS_BBOXES);
			D3D_SetVertexDeclaration (d3d_BBoxDecl);

			if (r_showbboxes.integer > 1)
			{
				D3D_SetRenderState (D3DRS_ALPHABLENDENABLE, TRUE);
				D3D_SetRenderState (D3DRS_BLENDOP, D3DBLENDOP_ADD);
				D3D_SetRenderState (D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
				D3D_SetRenderState (D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
			}
			else D3D_SetRenderState (D3DRS_FILLMODE, D3DFILL_WIREFRAME);

			stateset = true;
		}

		// draw the bounding box
		D3DMATRIX m;
		D3DMatrix_Identity (&m);
		D3DMatrix_Translate (&m, ent->trueorigin);
		D3DMatrix_Scale (&m, ent->bboxscale);
		D3DHLSL_SetEntMatrix (&m);
		D3D_DrawIndexedPrimitive (0, 8, 0, 12);
	}

	if (stateset)
	{
		D3D_BackfaceCull (D3DCULL_CCW);
		D3D_SetRenderState (D3DRS_ZWRITEENABLE, TRUE);

		if (r_showbboxes.integer > 1)
			D3D_SetRenderState (D3DRS_ALPHABLENDENABLE, FALSE);
		else D3D_SetRenderState (D3DRS_FILLMODE, D3DFILL_SOLID);
	}
}
