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
#include "d3d_vbo.h"

#define QUERY_IDLE	0
#define QUERY_BUSY	1

// a query is either idle or busy; makes more sense than d3d's "signalled"/etc nonsense.
// this struct contains the IDirect3DQuery9 object as well as a flag indicating it's state.  
typedef struct d3d_occlusionquery_s
{
	LPDIRECT3DQUERY9 Query;
	int State;
	entity_t *Entity;
	int BusyTime;
} d3d_occlusionquery_t;


CQuakeZone *OcclusionsHeap = NULL;
d3d_occlusionquery_t **d3d_OcclusionQueries = NULL;
int d3d_NumOcclusionQueries = 0;
int numoccluded = 0;
int numactive = 0;

bool R_ViewInsideBBox (float *mins, float *maxs)
{
	if (r_origin[0] < mins[0]) return false;
	if (r_origin[1] < mins[1]) return false;
	if (r_origin[2] < mins[2]) return false;

	if (r_origin[0] > maxs[0]) return false;
	if (r_origin[1] > maxs[1]) return false;
	if (r_origin[2] > maxs[2]) return false;

	// inside
	return true;
}


bool R_ViewInsideBBox (entity_t *ent)
{
	float bbmins[3];
	float bbmaxs[3];

	if (ent->model->type == mod_brush && (ent->angles[0] || ent->angles[1] || ent->angles[2]))
	{
		// rotating brush model
		for (int i = 0; i < 3; i++)
		{
			bbmins[i] = ent->origin[i] - ent->model->radius;
			bbmaxs[i] = ent->origin[i] + ent->model->radius;
		}
	}
	else
	{
		VectorAdd (ent->origin, ent->model->mins, bbmins);
		VectorAdd (ent->origin, ent->model->maxs, bbmaxs);
	}

	return (R_ViewInsideBBox (bbmins, bbmaxs));
}


void D3D_RegisterOcclusionQuery (entity_t *ent)
{
	// occlusions not supported
	if (!d3d_GlobalCaps.supportOcclusion) return;

	if (ent->effects & EF_NEVEROCCLUDE) return;

	// already has one
	if (ent->occlusion)
	{
		if (ent->occlusion->Entity != ent)
		{
			// if this changes we need to remove the occlusion from the entity
			// as it will be an invalid query because the entity that owns the occlusion
			// will be different to the one that is owned by the occlusion
			ent->occlusion = NULL;
			ent->occluded = false;
		}

		// still OK
		if (ent->occlusion) return;
	}

	if (!OcclusionsHeap)
	{
		OcclusionsHeap = new CQuakeZone ();
		d3d_OcclusionQueries = (d3d_occlusionquery_t **) OcclusionsHeap->Alloc (MAX_VISEDICTS * sizeof (d3d_occlusionquery_t *));
		d3d_NumOcclusionQueries = 0;
	}

	// find an idle query
	for (int i = 0; i < d3d_NumOcclusionQueries; i++)
	{
		if (!d3d_OcclusionQueries[i]) continue;
		if (!d3d_OcclusionQueries[i]->Query) continue;

		if (d3d_OcclusionQueries[i]->State == QUERY_IDLE)
		{
			// take this one
			ent->occlusion = d3d_OcclusionQueries[i];
			ent->occlusion->State = QUERY_IDLE;

			if (ent->occlusion->Entity)
			{
				// clean up the previous entity that used this query
				// but keep it's previous occlusion state
				ent->occlusion->Entity->occlusion = NULL;
			}

			ent->occlusion->Entity = ent;

			// entity is not occluded be default
			ent->occluded = false;

			// got it
			return;
		}
	}

	// create a new query
	d3d_occlusionquery_t *q = (d3d_occlusionquery_t *) OcclusionsHeap->Alloc (sizeof (d3d_occlusionquery_t));
	ent->occlusion = q;
	d3d_OcclusionQueries[d3d_NumOcclusionQueries] = ent->occlusion;
	d3d_NumOcclusionQueries++;

	ent->occlusion->Query = NULL;
	ent->occlusion->State = QUERY_IDLE;
	ent->occlusion->Entity = ent;

	// entity is not occluded be default
	ent->occluded = false;
}


void D3D_ClearOcclusionQueries (void)
{
	if (!OcclusionsHeap) return;
	if (!d3d_GlobalCaps.supportOcclusion) return;

	for (int i = 0; i < d3d_NumOcclusionQueries; i++)
	{
		if (d3d_OcclusionQueries[i]->Entity)
		{
			d3d_OcclusionQueries[i]->Entity->occlusion = NULL;
			d3d_OcclusionQueries[i]->Entity->occluded = false;
		}

		// to do - flush the query here...?
		SAFE_RELEASE (d3d_OcclusionQueries[i]->Query);
	}

	d3d_OcclusionQueries = NULL;
	d3d_NumOcclusionQueries = 0;
	SAFE_DELETE (OcclusionsHeap);
}


void D3DOcclusion_BeginCallback (void *data)
{
	d3d_occlusionquery_t *q = (d3d_occlusionquery_t *) data;
	q->Query->Issue (D3DISSUE_BEGIN);
}


void D3DOcclusion_EndCallback (void *data)
{
	d3d_occlusionquery_t *q = (d3d_occlusionquery_t *) data;
	q->Query->Issue (D3DISSUE_END);
}


cvar_t r_occlusion_bbox_expand ("r_occlusion_bbox_expand", 2.0f);

void D3D_IssueQuery (float *origin, float *mins, float *maxs, d3d_occlusionquery_t *q)
{
	// create on demand
	if (!q->Query)
	{
		// all queries are created idle (if only the rest of life was like that!)
		d3d_Device->CreateQuery (D3DQUERYTYPE_OCCLUSION, &q->Query);
		q->State = QUERY_IDLE;
	}

	// if this is true then the query is currently busy doing something and cannot be issued
	if (q->State != QUERY_IDLE) return;

	// issue the query
	VBO_AddCallback (D3DOcclusion_BeginCallback, q, sizeof (d3d_occlusionquery_t));
	VBO_AddBBox (origin, mins, maxs, r_occlusion_bbox_expand.value);
	VBO_AddCallback (D3DOcclusion_EndCallback, q, sizeof (d3d_occlusionquery_t));

	// the query is now waiting
	q->State = QUERY_BUSY;
	q->BusyTime = 0;
}


void D3DOcclusion_BeginState (void *blah)
{
	D3D_SetVertexDeclaration (d3d_VDXyz);
	D3D_SetRenderState (D3DRS_COLORWRITEENABLE, 0);
	D3D_SetRenderState (D3DRS_ZWRITEENABLE, FALSE);
	D3D_SetRenderState (D3DRS_CULLMODE, D3DCULL_NONE);

	if (d3d_GlobalCaps.usingPixelShaders)
	{
		if (d3d_FXPass == FX_PASS_NOTBEGUN)
		{
			d3d_MasterFX->SetFloat ("AlphaVal", 1.0f);
			D3D_BeginShaderPass (FX_PASS_SHADOW);
		}
		else if (d3d_FXPass == FX_PASS_SHADOW)
		{
			d3d_MasterFX->SetFloat ("AlphaVal", 1.0f);
			d3d_FXCommitPending = true;
		}
		else
		{
			D3D_EndShaderPass ();
			d3d_MasterFX->SetFloat ("AlphaVal", 1.0f);
			D3D_BeginShaderPass (FX_PASS_SHADOW);
		}
	}
	else D3D_SetTextureColorMode (0, D3DTOP_DISABLE);
}


void D3DOcclusion_EndState (void *blah)
{
	if (d3d_GlobalCaps.usingPixelShaders)
	{
		if (d3d_FXPass == FX_PASS_SHADOW)
			D3D_EndShaderPass ();
	}

	D3D_SetRenderState (D3DRS_COLORWRITEENABLE, 0x0000000F);
	D3D_BackfaceCull (D3DCULL_CCW);
	D3D_SetRenderState (D3DRS_ZWRITEENABLE, TRUE);
}


cvar_t r_occlusionqueries ("r_occlusionqueries", "3", CVAR_ARCHIVE);

void D3D_OcclusionOffscreenEntity (entity_t *ent)
{
	// remove the "never occlude" flag so that a temp slot for a rocket/etc doesn't prevent another
	// entity that may subsequently use it from being potentially occluded
	ent->effects &= ~EF_NEVEROCCLUDE;

	// entity has gone offscreen
	if (ent->occlusion)
	{
		// clear it's query and mark it as not occluded so it will always show in the first frame it comes back onscreen
		ent->occlusion->Entity = NULL;
		ent->occlusion = NULL;
	}

	// we must assume that it's not occluded so that it shows properly next time it's seen
	ent->occluded = false;
}


cvar_t r_showocclusions ("r_showocclusions", "0");
cvar_t r_occlusion_mdl_chop ("r_occlusion_mdl_chop", 200);
cvar_t r_occlusion_bsp_chop ("r_occlusion_bsp_chop", 200);

void D3D_RunOcclusionQueries (void)
{
	if (!d3d_GlobalCaps.supportOcclusion) return;

	bool stateset = false;

	if (r_showocclusions.integer)
		Con_Printf ("%i occlusion queries (%i active) (%i objects occluded)\n", d3d_NumOcclusionQueries, numactive, numoccluded);

	for (int i = 0; i < d3d_RenderDef.numvisedicts; i++)
	{
		entity_t *ent = d3d_RenderDef.visedicts[i];

		// some ents never get occlusion queries on them
		if (ent->effects & EF_NEVEROCCLUDE)
		{
			D3D_OcclusionOffscreenEntity (ent);

			// restore flag as the above func removed it (hack)
			ent->effects |= EF_NEVEROCCLUDE;
			continue;
		}

		// sprites are never occluded
		if (ent->model->type == mod_sprite)
		{
			D3D_OcclusionOffscreenEntity (ent);
			continue;
		}

		// some entities are so simple that the overhead of creating, issuing and reading a query isn't worth it
		// this also helps to keep the number of queries down, which gets them executing faster, which gets them bringing in results faster
		if (ent->model->type == mod_alias && ent->model->aliashdr->numtris < r_occlusion_mdl_chop.integer)
		{
			D3D_OcclusionOffscreenEntity (ent);
			continue;
		}

		if (ent->model->type == mod_brush && ent->model->brushhdr->nummodelsurfaces < r_occlusion_bsp_chop.integer)
		{
			D3D_OcclusionOffscreenEntity (ent);
			continue;
		}

		if (!r_occlusionqueries.integer)
		{
			D3D_OcclusionOffscreenEntity (ent);
			continue;
		}

		if ((r_occlusionqueries.integer == 1) && (ent->model->type != mod_alias))
		{
			D3D_OcclusionOffscreenEntity (ent);
			continue;
		}

		if ((r_occlusionqueries.integer == 2) && (ent->model->type != mod_brush))
		{
			D3D_OcclusionOffscreenEntity (ent);
			continue;
		}

		// entity was off screen this frame (whether or not it was occluded doesn't matter here)
		if (ent->visframe != d3d_RenderDef.framecount)
		{
			// mark as not occluded so that it will be visible the first frame it comes back onscreen
			D3D_OcclusionOffscreenEntity (ent);
			continue;
		}

		// if the view is inside the bbox don't check for occlusion on the entity
		if (R_ViewInsideBBox (ent))
		{
			// Con_Printf ("view inside bbox for %s\n", ent->model->name);
			ent->occluded = false;
			continue;
		}

		// register a query for the entity
		D3D_RegisterOcclusionQuery (ent);

		// no occlusion object (should never happen...)
		if (!ent->occlusion)
		{
			// not occluded
			ent->occluded = false;
			continue;
		}

		if (!stateset)
		{
			// only change state if we need to
			VBO_AddCallback (D3DOcclusion_BeginState);
			stateset = true;
		}

		if (ent->model->type == mod_brush && (ent->angles[0] || ent->angles[1] || ent->angles[2]))
		{
			float bbmins[3];
			float bbmaxs[3];

			// rotating brush model
			for (int j = 0; j < 3; j++)
			{
				bbmins[j] = -ent->model->radius;
				bbmaxs[j] = ent->model->radius;
			}

			// issue this set
			D3D_IssueQuery (ent->origin, bbmins, bbmaxs, ent->occlusion);
		}
		else
		{
			// issue this set
			D3D_IssueQuery (ent->origin, ent->model->mins, ent->model->maxs, ent->occlusion);
		}
	}

	if (stateset) VBO_AddCallback (D3DOcclusion_EndState);
}


bool D3D_ReadOcclusionQuery (d3d_occlusionquery_t *q)
{
	DWORD Visible = 1;

	bool occluded = false;

	// query does not exist or has not yet been issued
	if (!q->Query || q->State != QUERY_BUSY) return occluded;

	numactive++;

	// query status but don't flush
	hr = q->Query->GetData ((void *) &Visible, sizeof (DWORD), 0);

	if (hr == S_OK)
	{
		if (Visible)
			occluded = false;
		else
		{
			occluded = true;
			numoccluded++;
		}

		q->State = QUERY_IDLE;
		q->BusyTime = 0;
	}
	else if (hr == S_FALSE)
	{
		// if results are unavailable we must assume that the ent is visible
		occluded = false;
	}
	else
	{
		// something went wrong
		// auto-destruct
		SAFE_RELEASE (q->Query);
		occluded = false;

		if (q->Entity)
		{
			// remove the query from the ent
			q->Entity->occlusion = NULL;
			q->Entity = NULL;
		}
	}

	// if we're busy for more than one frame we likewise must assume that the ent is visible
	// otherwise we risk the result being invalid by the time it comes in
	if ((++q->BusyTime) > 1)
	{
		occluded = false;

		if (q->Entity)
		{
			// remove the query from the ent
			q->Entity->occlusion = NULL;
			q->Entity = NULL;
		}
	}

	return occluded;
}


void D3D_UpdateOcclusionQueries (void)
{
	if (!d3d_GlobalCaps.supportOcclusion) return;
	if (!d3d_NumOcclusionQueries) return;
	if (cls.state != ca_connected) return;

	numoccluded = 0;
	numactive = 0;

	// this needs to be run for entities that are also not in the PVS/frustum so that queries which have expired
	// have the best chance of being available for testing again the next time the entity is visible.
	for (int i = 0; i < d3d_NumOcclusionQueries; i++)
	{
		d3d_occlusionquery_t *q = d3d_OcclusionQueries[i];

		if (q->Entity)
		{
			if (!q->Entity->occlusion) continue;
			if (q->Entity->effects & EF_NEVEROCCLUDE) continue;

			bool occluded = D3D_ReadOcclusionQuery (q);

			// this *looks* like madness but D3D_ReadOcclusionQuery can actually remove the entity from q
			// (for example if the query runs too long or if something bad happens) so it's needed.
			if (q->Entity)
			{
				if (q->Entity->visframe != d3d_RenderDef.framecount)
					D3D_OcclusionOffscreenEntity (q->Entity);
				else q->Entity->occluded = occluded;
			}
		}
		else
		{
			// unknown type
			// just read it and discard the result
			// we need to send it through here so that we can mark the state as QUERY_IDLE when it goes idle
			D3D_ReadOcclusionQuery (q);
		}
	}
}


