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

// occlusion queries for anything that needs/uses them
// there are still some parts of D3D that look like they were designed by a team of monkeys on LSD
// this is one of them

#include "quakedef.h"
#include "d3d_model.h"
#include "d3d_quake.h"

cvar_t r_showocclusionbboxes ("r_showocclusionbboxes", "0");
cvar_t r_showbboxes ("r_showbboxes", "0");

typedef struct oquery_s
{
	bool Running;
	LPDIRECT3DQUERY9 Query;
	entity_t *ent;
} oquery_t;


#define MAX_QUERIES		2048

oquery_t d3d_OQueries[MAX_QUERIES];

void D3DOQ_RegisterQuery (entity_t *ent)
{
	// entities that don't have a model don't get queries because they are not drawn!!!
	if (!ent->model) return;
	if (ent == &cl.viewent) return;
	if (!d3d_GlobalCaps.supportOcclusion) return;

	// the entity is not occluded by default
	ent->Occluded = false;

	oquery_t *qexpired = NULL;
	oquery_t *qfree = NULL;

	// find a suitable query to use
	for (int i = 0; i < MAX_QUERIES; i++)
	{
		// don't take any queries which are running!!!
		if (d3d_OQueries[i].Running) continue;
		if (d3d_OQueries[i].ent) continue;

		// try to avoid creating new objects at runtime as much as possible
		// by reusing any objects which have expired
		if (!d3d_OQueries[i].Query)
			qfree = &d3d_OQueries[i];
		else
		{
			qexpired = &d3d_OQueries[i];
			break;
		}
	}

	oquery_t *qreal = NULL;

	// pick the most suitable; if none were found we just don't do it
	if (qexpired)
		qreal = qexpired;
	else if (qfree)
		qreal = qfree;
	else return;

	if (!qreal->Query)
	{
		hr = d3d_Device->CreateQuery (D3DQUERYTYPE_OCCLUSION, &qreal->Query);

		if (FAILED (hr))
		{
			// don't crash, just don't use queries...
			return;
		}
	}

	qreal->ent = ent;
	qreal->Running = false;
}


void D3DOQ_CleanEntity (entity_t *ent)
{
	// the entity does not have an associated occlusion query and it is not occluded
	ent->Occluded = false;
}


typedef struct r_oqvertex_s
{
	float xyz[3];
	D3DCOLOR color;
	float dummy[2];
} r_oqvertex_t;


LPDIRECT3DVERTEXBUFFER9 d3d_QueryVBO = NULL;
LPDIRECT3DINDEXBUFFER9 d3d_QueryIBO = NULL;
LPDIRECT3DVERTEXDECLARATION9 d3d_OQDecl = NULL;


void D3DOQ_PopulateVBO (void)
{
	r_oqvertex_t *bboxverts = NULL;

	hr = d3d_QueryVBO->Lock (0, 0, (void **) &bboxverts, 0);
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

	hr = d3d_QueryVBO->Unlock ();
	if (FAILED (hr)) Sys_Error ("D3DOQ_CreateBuffers: failed to unlock vertex buffer");
}


void D3DOQ_PopulateIBO (void)
{
	unsigned short *ndx = NULL;
	unsigned short bboxindexes[36] =
	{
		0, 2, 6, 0, 6, 4, 1, 3, 7, 1, 7, 5, 0, 1, 3, 0, 3, 2,
		4, 5, 7, 4, 7, 6, 0, 1, 5, 0, 5, 4, 2, 3, 7, 2, 7, 6
	};

	hr = d3d_QueryIBO->Lock (0, 0, (void **) &ndx, 0);
	if (FAILED (hr)) Sys_Error ("D3DOQ_CreateBuffers: failed to lock index buffer");

	memcpy (ndx, bboxindexes, 36 * sizeof (unsigned short));

	hr = d3d_QueryIBO->Unlock ();
	if (FAILED (hr)) Sys_Error ("D3DOQ_CreateBuffers: failed to unlock index buffer");
}


void D3DOQ_CreateBuffers (void)
{
	if (!d3d_OQDecl)
	{
		D3DVERTEXELEMENT9 d3d_oqlayout[] =
		{
			{0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
			{0, 12, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0},
			{0, 16, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
			D3DDECL_END ()
		};

		hr = d3d_Device->CreateVertexDeclaration (d3d_oqlayout, &d3d_OQDecl);
		if (FAILED (hr)) Sys_Error ("D3DDraw_CreateBuffers: d3d_Device->CreateVertexDeclaration failed");
	}

	if (!d3d_QueryVBO)
	{
		D3DMain_CreateVertexBuffer (8 * sizeof (r_oqvertex_t), D3DUSAGE_WRITEONLY, &d3d_QueryVBO);
		D3DOQ_PopulateVBO ();
	}

	if (!d3d_QueryIBO)
	{
		D3DMain_CreateIndexBuffer (36, D3DUSAGE_WRITEONLY, &d3d_QueryIBO);
		D3DOQ_PopulateIBO ();
	}
}


bool D3DOQ_ViewInsideBBox (entity_t *ent)
{
	if (r_viewvectors.origin[0] < ent->mins[0]) return false;
	if (r_viewvectors.origin[1] < ent->mins[1]) return false;
	if (r_viewvectors.origin[2] < ent->mins[2]) return false;

	if (r_viewvectors.origin[0] > ent->maxs[0]) return false;
	if (r_viewvectors.origin[1] > ent->maxs[1]) return false;
	if (r_viewvectors.origin[2] > ent->maxs[2]) return false;

	// inside
	return true;
}


void D3DOQ_RunQueries (void)
{
	if (!d3d_GlobalCaps.supportOcclusion) return;
	if (cl.paused) return;

	int NumQueries = 0;
	bool stateset = false;

	D3DOQ_CreateBuffers ();

	for (int i = 0; i < MAX_QUERIES; i++)
	{
		// not yet allocated
		if (!d3d_OQueries[i].Query) continue;

		entity_t *ent = d3d_OQueries[i].ent;

		if (d3d_OQueries[i].Running)
		{
			// check the status of this query
			DWORD qResult = 0;
			hr = d3d_OQueries[i].Query->GetData ((void *) &qResult, sizeof (DWORD), 0);

			if (hr == S_OK)
			{
				// interpret the result
				if (ent)
				{
					if (qResult)
						ent->Occluded = false;
					else
					{
						// if the associated entity went offscreen we don't know what state it's going be in when it
						// comes back onscreen, so we must assume that it's going to be visible even if the test says otherwise.
						if (ent->visframe != d3d_RenderDef.framecount)
							ent->Occluded = false;
						else ent->Occluded = true;
					}
				}

				// the query is now available for reuse
				d3d_OQueries[i].Running = false;
			}
			else if (hr != S_FALSE)
			{
				// query fucked up and must be recreated
				SAFE_RELEASE (d3d_OQueries[i].Query);
				d3d_OQueries[i].ent = NULL;
				d3d_OQueries[i].Running = false;
			}

			NumQueries++;
		}
		else if (ent)
		{
			if (!ent->model)
			{
				ent->Occluded = false;
				d3d_OQueries[i].ent = NULL;
				continue;
			}

			if (D3DOQ_ViewInsideBBox (ent))
			{
				ent->Occluded = false;
				continue;
			}

			if (ent->visframe != d3d_RenderDef.framecount)
			{
				// don't clear the entity because statics never re-register... (fix this by registering when we add a visedict...)
				ent->Occluded = false;
				// d3d_OQueries[i].ent = NULL;
				continue;
			}

			// to do - check model complexity...
			if (ent->model->type == mod_sprite)
			{
				ent->Occluded = false;
				continue;
			}

			if (!stateset)
			{
				// DIPUP is too slow for this...
				D3D_SetStreamSource (0, d3d_QueryVBO, 0, sizeof (r_oqvertex_t));
				D3D_SetStreamSource (1, NULL, 0, 0);
				D3D_SetStreamSource (2, NULL, 0, 0);
				D3D_SetIndices (d3d_QueryIBO);

				// if we're already showing bboxes we don't bother showing them again this time
				if (!r_showocclusionbboxes.value && !r_showbboxes.value)
					D3D_SetRenderState (D3DRS_COLORWRITEENABLE, 0);

				D3D_SetRenderState (D3DRS_ZWRITEENABLE, FALSE);
				D3D_SetRenderState (D3DRS_CULLMODE, D3DCULL_NONE);

				D3DHLSL_SetPass (FX_PASS_DRAWCOLORED);
				D3D_SetVertexDeclaration (d3d_OQDecl);

				stateset = true;
			}

			// start a new query
			d3d_OQueries[i].Query->Issue (D3DISSUE_BEGIN);

			// draw the bounding box
			D3DMATRIX m;
			D3DMatrix_Identity (&m);
			D3DMatrix_Translate (&m, ent->trueorigin);
			D3DMatrix_Scale (&m, ent->bboxscale);
			D3DHLSL_SetEntMatrix (&m);
			D3D_DrawIndexedPrimitive (0, 8, 0, 12);

			// we can't batch bboxes because the query has to begin and end on a per-entity basis
			d3d_OQueries[i].Query->Issue (D3DISSUE_END);
			d3d_OQueries[i].Running = true;
		}
	}

	if (stateset)
	{
		D3D_SetRenderState (D3DRS_COLORWRITEENABLE, 0x0000000F);
		D3D_BackfaceCull (D3DCULL_CCW);
		D3D_SetRenderState (D3DRS_ZWRITEENABLE, TRUE);
	}

	// if (NumQueries) Con_Printf ("Running %i queries\n", NumQueries);
}


void D3DOQ_ReleaseQueries (void)
{
	SAFE_RELEASE (d3d_QueryVBO);
	SAFE_RELEASE (d3d_QueryIBO);
	SAFE_RELEASE (d3d_OQDecl);

	for (int i = 0; i < MAX_QUERIES; i++)
	{
		// always ensure
		d3d_OQueries[i].ent = NULL;

		// not yet allocated
		if (!d3d_OQueries[i].Query)
		{
			d3d_OQueries[i].Running = false;
			continue;
		}

		if (d3d_OQueries[i].Running)
		{
			// force the query to terminate
			DWORD qResult = 0;

			d3d_OQueries[i].Query->GetData ((void *) &qResult, sizeof (DWORD), D3DGETDATA_FLUSH);
			d3d_OQueries[i].Running = false;
		}

		// release it
		SAFE_RELEASE (d3d_OQueries[i].Query);
	}
}


CD3DDeviceLossHandler d3d_OQHandler (D3DOQ_ReleaseQueries, D3DOQ_CreateBuffers);


// this really has nothing to do with occlusion queries but is included here for convenience
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
			D3D_SetStreamSource (0, d3d_QueryVBO, 0, sizeof (r_oqvertex_t));
			D3D_SetStreamSource (1, NULL, 0, 0);
			D3D_SetStreamSource (2, NULL, 0, 0);
			D3D_SetIndices (d3d_QueryIBO);

			D3D_SetRenderState (D3DRS_ZWRITEENABLE, FALSE);
			D3D_SetRenderState (D3DRS_CULLMODE, D3DCULL_NONE);

			D3DHLSL_SetPass (FX_PASS_DRAWCOLORED);
			D3D_SetVertexDeclaration (d3d_OQDecl);

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

		if (r_showbboxes.integer > 1)
			D3D_SetRenderState (D3DRS_ALPHABLENDENABLE, FALSE);
		else D3D_SetRenderState (D3DRS_FILLMODE, D3DFILL_SOLID);
	}
}
