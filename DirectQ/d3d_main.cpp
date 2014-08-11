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
// r_main.c

#include "quakedef.h"
#include "d3d_model.h"
#include "d3d_quake.h"
#include "d3d_Quads.h"
#include "iqm.h"


cvar_t gl_fullbrights ("gl_fullbrights", "1", CVAR_ARCHIVE);
cvar_t r_draworder ("r_draworder", "0");
cvar_t r_colorfilter ("r_colorfilter", "7");

void D3DMain_CreateVertexBuffer (UINT length, DWORD usage, LPDIRECT3DVERTEXBUFFER9 *buf)
{
	// enforce
	usage |= D3DUSAGE_WRITEONLY;

	hr = d3d_Device->CreateVertexBuffer (length, usage, 0, D3DPOOL_DEFAULT, buf, NULL);
	if (SUCCEEDED (hr)) return;

	Sys_Error ("D3DMain_CreateVertexBuffer : IDirect3DDevice9::CreateVertexBuffer failed");
}


void D3DMain_CreateIndexBuffer16 (UINT numindexes, DWORD usage, LPDIRECT3DINDEXBUFFER9 *buf)
{
	UINT length = numindexes * sizeof (unsigned short);

	// enforce
	usage |= D3DUSAGE_WRITEONLY;

	hr = d3d_Device->CreateIndexBuffer (length, usage, D3DFMT_INDEX16, D3DPOOL_DEFAULT, buf, NULL);
	if (SUCCEEDED (hr)) return;

	Sys_Error ("D3DMain_CreateIndexBuffer16 : IDirect3DDevice9::CreateIndexBuffer failed");
}


void D3DMain_CreateIndexBuffer32 (UINT numindexes, DWORD usage, LPDIRECT3DINDEXBUFFER9 *buf)
{
	UINT length = numindexes * sizeof (unsigned int);

	// enforce
	usage |= D3DUSAGE_WRITEONLY;

	hr = d3d_Device->CreateIndexBuffer (length, usage, D3DFMT_INDEX32, D3DPOOL_DEFAULT, buf, NULL);
	if (SUCCEEDED (hr)) return;

	Sys_Error ("D3DMain_CreateIndexBuffer32 : IDirect3DDevice9::CreateIndexBuffer failed");
}


extern LPDIRECT3DTEXTURE9 char_texture;

void D3D_DrawPrimitive (D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount)
{
	// check that all of our states are nice and correct
	D3DHLSL_CheckCommit ();

	// and now we can draw
	if (PrimitiveCount > 1)
		d3d_Device->DrawPrimitive (PrimitiveType, StartVertex, PrimitiveCount);
	else d3d_Device->DrawPrimitive (D3DPT_TRIANGLELIST, StartVertex, 1);

	d3d_RenderDef.numdrawprim++;
}


void D3D_DrawIndexedPrimitive (D3DPRIMITIVETYPE PrimitiveType, int FirstVertex, int NumVertexes, int FirstIndex, int NumPrimitives)
{
	// check that all of our states are nice and correct
	D3DHLSL_CheckCommit ();

	// and now we can draw
	hr = d3d_Device->DrawIndexedPrimitive (PrimitiveType, 0, FirstVertex, NumVertexes, FirstIndex, NumPrimitives);
	d3d_RenderDef.numdrawprim++;
}


// this one wraps DIP so that I can check for commit and anything else i need to do before drawing
void D3D_DrawIndexedPrimitive (int FirstVertex, int NumVertexes, int FirstIndex, int NumPrimitives)
{
	// check that all of our states are nice and correct
	D3DHLSL_CheckCommit ();

	// and now we can draw
	hr = d3d_Device->DrawIndexedPrimitive (D3DPT_TRIANGLELIST, 0, FirstVertex, NumVertexes, FirstIndex, NumPrimitives);
	d3d_RenderDef.numdrawprim++;
}


void D3D_PrelockVertexBuffer (LPDIRECT3DVERTEXBUFFER9 vb)
{
	void *blah = NULL;

	hr = vb->Lock (0, 0, &blah, d3d_GlobalCaps.DiscardLock);
	if (FAILED (hr)) Sys_Error ("D3D_PrelockVertexBuffer: failed to lock vertex buffer");

	hr = vb->Unlock ();
	d3d_RenderDef.numlock++;
	if (FAILED (hr)) Sys_Error ("D3D_PrelockVertexBuffer: failed to unlock vertex buffer");
}


void D3D_PrelockIndexBuffer (LPDIRECT3DINDEXBUFFER9 ib)
{
	void *blah = NULL;

	hr = ib->Lock (0, 0, &blah, d3d_GlobalCaps.DiscardLock);
	if (FAILED (hr)) Sys_Error ("D3D_PrelockVertexBuffer: failed to lock index buffer");

	hr = ib->Unlock ();
	d3d_RenderDef.numlock++;
	if (FAILED (hr)) Sys_Error ("D3D_PrelockVertexBuffer: failed to unlock index buffer");
}


void R_AnimateLight (float time);
void V_CalcBlend (void);

void D3DSurf_DrawWorld (void);
void D3D_AddWorldSurfacesToRender (void);

void D3DWarp_InitializeTurb (void);
void D3D_DrawAlphaWaterSurfaces (void);
void D3D_DrawOpaqueWaterSurfaces (void);

void D3DAlias_DrawViewModel (void);
void D3DAlias_RenderAliasModels (void);
void D3DIQM_DrawIQMs (void);
void D3D_PrepareAliasModel (entity_t *e);
void D3D_AddParticesToAlphaList (void);
void D3D_AddParticesToAlphaList (void);

void D3DLight_SetCoronaState (void);
void D3DLight_AddCoronas (void);

void D3DOC_ShowBBoxes (void);
void R_CullDynamicLights (void);

QMATRIX d3d_ViewMatrix;
QMATRIX d3d_WorldMatrix;
QMATRIX d3d_ModelViewProjMatrix;
QMATRIX d3d_ProjMatrix;

// render definition for this frame
d3d_renderdef_t d3d_RenderDef;

mplane_t	frustum[4];

// view origin
r_viewvecs_t	r_viewvectors;

// screen size info
refdef_t	r_refdef;

texture_t	*r_notexture_mip;

cvar_t	r_norefresh ("r_norefresh", "0");
cvar_t	r_drawentities ("r_drawentities", "1");
cvar_t	r_drawviewmodel ("r_drawviewmodel", "1");
cvar_t	r_speeds ("r_speeds", "0");
cvar_t	r_lightmap ("r_lightmap", "0");
cvar_t	r_shadows ("r_shadows", "0", CVAR_ARCHIVE);
cvar_t	r_wateralpha ("r_wateralpha", 1.0f);

cvar_t	gl_cull ("gl_cull", "1");
cvar_t	gl_smoothmodels ("gl_smoothmodels", "1");
cvar_t	gl_affinemodels ("gl_affinemodels", "0");
cvar_t	gl_polyblend ("gl_polyblend", "1", CVAR_ARCHIVE);
cvar_t	gl_nocolors ("gl_nocolors", "0");
cvar_t	gl_doubleeyes ("gl_doubleeys", "1");
cvar_t	gl_clear ("gl_clear", "0");
cvar_t	r_clearcolor ("r_clearcolor", "0");

// renamed this because I had chosen a bad default... (urff)
cvar_t gl_underwaterfog ("r_underwaterfog", 0.0f, CVAR_ARCHIVE);

void R_ForceRecache (cvar_t *var)
{
	// force a rebuild of the PVS if any of the cvars attached to this change
	d3d_RenderDef.oldviewleaf = NULL;
	d3d_RenderDef.rebuildworld = true;
}

cvar_t r_novis ("r_novis", "0", 0, R_ForceRecache);
cvar_t r_lockpvs ("r_lockpvs", "0", 0, R_ForceRecache);
cvar_t r_lockfrustum ("r_lockfrustum", 0.0f, 0, R_ForceRecache);
cvar_t r_wireframe ("r_wireframe", 0.0f);


void D3D_BeginVisedicts (void)
{
	// these can't go into the scratchbuf because of SCR_UpdateScreen calls in places like SCR_BeginLoadingPlaque
	if (!d3d_RenderDef.visedicts) d3d_RenderDef.visedicts = (entity_t **) MainZone->Alloc (MAX_VISEDICTS * sizeof (entity_t *));

	d3d_RenderDef.numvisedicts = 0;
	d3d_RenderDef.relinkframe++;
}


void D3DAlias_SetupFrame (entity_t *ent, aliashdr_t *hdr);
void D3DAlias_LerpToFrame (entity_t *ent, int pose, float interval);

void D3DMain_BBoxForEnt (entity_t *ent)
{
	if (!ent->model) return;

	D3DXVECTOR3 mins;
	D3DXVECTOR3 maxs;
	D3DXVECTOR3 bbox[8];
	avectors_t av;
	float angles[3];

	if (ent->model->type == mod_alias)
	{
		// use per-frame bboxes for entities
		aliasbbox_t *bboxes = ent->model->aliashdr->bboxes;

		// set up interpolation here to ensure that we get all entities
		// this also keeps interpolation frames valid even if the model has been culled away (bonus!)
		D3DAlias_SetupFrame (ent, ent->model->aliashdr);

		// use per-frame interpolated bboxes
		D3DXVec3Lerp (&mins, &bboxes[ent->lastpose].mins, &bboxes[ent->currpose].mins, ent->poseblend);
		D3DXVec3Lerp (&maxs, &bboxes[ent->lastpose].maxs, &bboxes[ent->currpose].maxs, ent->poseblend);
	}
	else if (ent->model->type == mod_iqm)
	{
		// lerp the frames
		D3DAlias_LerpToFrame (ent, ent->frame, (ent->lerpflags & LERP_FINISH) ? ent->lerpinterval : 0.1f);

		// use the real bboxes, not the physics bboxes
		VectorCopy (ent->model->iqmheader->mins, mins);
		VectorCopy (ent->model->iqmheader->maxs, maxs);
	}
	else if (ent->model->type == mod_brush)
	{
		VectorCopy (ent->model->brushhdr->bmins, mins);
		VectorCopy (ent->model->brushhdr->bmaxs, maxs);
	}
	else
	{
		VectorCopy (ent->model->mins, mins);
		VectorCopy (ent->model->maxs, maxs);
	}

	// compute a full bounding box
	for (int i = 0; i < 8; i++)
	{
		// the bounding box is expanded by 2 units in each direction so
		// that it won't z-fight with the model (if it's a tight box)
		bbox[i][0] = (i & 1) ? mins[0] - 2.0f : maxs[0] + 2.0f;
		bbox[i][1] = (i & 2) ? mins[1] - 2.0f : maxs[1] + 2.0f;
		bbox[i][2] = (i & 4) ? mins[2] - 2.0f : maxs[2] + 2.0f;
	}

	// these factors hold valid for both MDLs and brush models; tested brush models with rmq rotate test
	// and ne_tower; tested alias models by assigning bobjrotate to angles 0/1/2 and observing the result
	// i guess that ID just left out angles[2] because it never really happened in the original game
	if (ent->model->type == mod_brush)
	{
		angles[0] = -ent->angles[0];
		angles[1] = -ent->angles[1];
		angles[2] = -ent->angles[2];
	}
	else
	{
		angles[0] = ent->angles[0];
		angles[1] = -ent->angles[1];
		angles[2] = -ent->angles[2];
	}

	// derive forward/right/up vectors from the angles
	AngleVectors (angles, &av);

	// compute the rotated bbox corners
	Mod_ClearBoundingBox (mins, maxs);

	// and rotate the bounding box
	for (int i = 0; i < 8; i++)
	{
		D3DXVECTOR3 tmp;

		VectorCopy (bbox[i], tmp);

		bbox[i][0] = D3DXVec3Dot (&av.forward, &tmp);
		bbox[i][1] = -D3DXVec3Dot (&av.right, &tmp);
		bbox[i][2] = D3DXVec3Dot (&av.up, &tmp);

		// and convert them to mins and maxs
		Mod_AccumulateBox (mins, maxs, bbox[i]);
	}

	// compute scaling factors
	ent->bboxscale[0] = (maxs[0] - mins[0]) * 0.5f;
	ent->bboxscale[1] = (maxs[1] - mins[1]) * 0.5f;
	ent->bboxscale[2] = (maxs[2] - mins[2]) * 0.5f;

	// translate the bbox to it's final position at the entity origin
	VectorAdd (ent->origin, mins, ent->mins);
	VectorAdd (ent->origin, maxs, ent->maxs);

	// true origin of entity is at bbox center point (needed for bmodels
	// where the origin could be at (0, 0, 0) or at a corner)
	ent->trueorigin[0] = ent->mins[0] + (ent->maxs[0] - ent->mins[0]) * 0.5f;
	ent->trueorigin[1] = ent->mins[1] + (ent->maxs[1] - ent->mins[1]) * 0.5f;
	ent->trueorigin[2] = ent->mins[2] + (ent->maxs[2] - ent->mins[2]) * 0.5f;
}


void D3D_AddVisEdict (entity_t *ent)
{
	// check for entities with no models
	if (!ent->model) return;

	// prevent entities from being added twice (as static entities will be added every frame the renderer runs
	// but the client may run less frequently)
	if (ent->relinkframe == d3d_RenderDef.relinkframe) return;

	// only add entities with supported model types
	if (ent->model->type == mod_alias || ent->model->type == mod_brush || ent->model->type == mod_sprite || ent->model->type == mod_iqm)
	{
		ent->nocullbox = false;

		// check for rotation; this is used for bmodel culling and also for faster transforms for all model types
		if (ent->angles[0] || ent->angles[1] || ent->angles[2])
			ent->rotated = true;
		else ent->rotated = false;

		if (d3d_RenderDef.numvisedicts < MAX_VISEDICTS)
		{
			d3d_RenderDef.visedicts[d3d_RenderDef.numvisedicts] = ent;
			d3d_RenderDef.numvisedicts++;
		}

		// now mark that it's been added to the visedicts list for this client frame
		ent->relinkframe = d3d_RenderDef.relinkframe;
	}
	else Con_DPrintf ("D3D_AddVisEdict: Unknown entity model type for %s\n", ent->model->name);
}


bool R_CullSphere (float *center, float radius, int clipflags)
{
	int		i;
	mplane_t *p;

	for (i = 0, p = frustum; i < 4; i++, p++)
	{
		if (!(clipflags & (1 << i))) continue;
		if ((DotProduct (center, p->normal) - p->dist) <= -radius) return true;
	}

	return false;
}


/*
=================
R_CullBox

Returns true if the box is completely outside the frustum
=================
*/
bool R_CullBox (vec3_t emins, vec3_t emaxs, mplane_t *frustumplanes, int clipflags)
{
	// this can only handle identifying the fully outside case; a box that passes all tests may still be
	// either fully inside or untersect one or more of the planes.  It is however one less DotProduct per plane
	for (int i = 0; i < 4; i++)
	{
		if (!(clipflags & (1 << i))) continue;

		mplane_t *p = &frustumplanes[i];

		switch (p->signbits)
		{
		default:
		case 0:
			if ((p->normal[0] * emaxs[0] + p->normal[1] * emaxs[1] + p->normal[2] * emaxs[2]) < p->dist)
				return true;
			break;

		case 1:
			if ((p->normal[0] * emins[0] + p->normal[1] * emaxs[1] + p->normal[2] * emaxs[2]) < p->dist)
				return true;
			break;

		case 2:
			if ((p->normal[0] * emaxs[0] + p->normal[1] * emins[1] + p->normal[2] * emaxs[2]) < p->dist)
				return true;
			break;

		case 3:
			if ((p->normal[0] * emins[0] + p->normal[1] * emins[1] + p->normal[2] * emaxs[2]) < p->dist)
				return true;
			break;

		case 4:
			if ((p->normal[0] * emaxs[0] + p->normal[1] * emaxs[1] + p->normal[2] * emins[2]) < p->dist)
				return true;
			break;

		case 5:
			if ((p->normal[0] * emins[0] + p->normal[1] * emaxs[1] + p->normal[2] * emins[2]) < p->dist)
				return true;
			break;

		case 6:
			if ((p->normal[0] * emaxs[0] + p->normal[1] * emins[1] + p->normal[2] * emins[2]) < p->dist)
				return true;
			break;

		case 7:
			if ((p->normal[0] * emins[0] + p->normal[1] * emins[1] + p->normal[2] * emins[2]) < p->dist)
				return true;
			break;
		}
	}

	return false;
}


//==================================================================================

int SignbitsForPlane (mplane_t *out)
{
	int	bits, j;

	// for fast box on planeside test - identify which corner(s) of the box to text against the plane
	bits = 0;

	for (j = 0; j < 3; j++)
	{
		if (out->normal[j] < 0)
			bits |= 1 << j;
	}

	return bits;
}


/*
===============
R_SetupFrame
===============
*/
void R_SetupFrame (void)
{
	// initialize r_speeds and flags
	d3d_RenderDef.brush_polys = 0;
	d3d_RenderDef.alias_polys = 0;

	// don't allow cheats in multiplayer
	if (cl.maxclients > 1) Cvar_Set ("r_fullbright", "0");

	R_AnimateLight (cl.time);

	// current viewleaf
	d3d_RenderDef.viewleaf = Mod_PointInLeaf (r_refdef.vieworigin, cl.worldmodel);

	d3d_RenderDef.fov_x = r_refdef.fov_x;
	d3d_RenderDef.fov_y = r_refdef.fov_y;
}


void D3D_SetViewport (DWORD x, DWORD y, DWORD w, DWORD h, float zn, float zf)
{
	D3DVIEWPORT9 d3d_Viewport;

	// get dimensions of viewport
	d3d_Viewport.X = x;
	d3d_Viewport.Y = y;
	d3d_Viewport.Width = w;
	d3d_Viewport.Height = h;

	// set z range
	d3d_Viewport.MinZ = zn;
	d3d_Viewport.MaxZ = zf;

	// set the viewport
	d3d_Device->SetViewport (&d3d_Viewport);
}


void D3D_SetupProjection (float fovx, float fovy, float zn, float zf)
{
	d3d_ProjMatrix.LoadIdentity ();
	d3d_ProjMatrix.Projection (fovx, fovy, zn, zf);

	if (r_waterwarp.value > 1)
	{
		int contents = Mod_PointInLeaf (r_refdef.vieworigin, cl.worldmodel)->contents;

		if (contents == CONTENTS_WATER || contents == CONTENTS_SLIME || contents == CONTENTS_LAVA)
		{
			d3d_ProjMatrix._21 = sin (cl.time * 1.125f) * 0.0666f;
			d3d_ProjMatrix._12 = cos (cl.time * 1.125f) * 0.0666f;

			d3d_ProjMatrix.Scale ((cos (cl.time * 0.75f) + 20.0f) * 0.05f, (sin (cl.time * 0.75f) + 20.0f) * 0.05f, 1);
		}
	}
}


/*
=============
D3D_PrepareRender
=============
*/
int D3DRTT_RescaleDimension (int dim);

float r_oldvieworigin[3];
float r_oldviewangles[3];

void D3DMain_SetupD3D (void)
{
	vec3_t o, a;

	VectorSubtract (r_refdef.vieworigin, r_oldvieworigin, o);
	VectorSubtract (r_refdef.viewangles, r_oldviewangles, a);

	// this prevents greyflash when going to intermission (the client pos can interpolate between the changelevel and the intermission point)
	if (d3d_RenderDef.viewleaf && d3d_RenderDef.viewleaf->contents == CONTENTS_SOLID) d3d_RenderDef.rebuildworld = true;

	// also check for and catch cases where a rebuildworld is flagged elsewhere so that we can update the old values correctly
	// (tighter frustum culling means that we can't use the looser check from the original)
	if (Length (o) >= 0.5f || Length (a) >= 0.2f || d3d_RenderDef.rebuildworld)
	{
		// Con_Printf ("rebuild on frame %i\n", d3d_RenderDef.framecount);
		d3d_RenderDef.rebuildworld = true;
		VectorCopy2 (r_oldvieworigin, r_refdef.vieworigin);
		VectorCopy2 (r_oldviewangles, r_refdef.viewangles);
	}

	// r_wireframe 1 is cheating in multiplayer
	if (r_wireframe.integer)
	{
		if (sv.active)
		{
			if (svs.maxclients > 1)
			{
				// force off for the owner of a listen server
				Cvar_Set (&r_wireframe, 0.0f);
			}
		}
		else if (cls.demoplayback)
		{
			// do nothing
		}
		else if (cls.state == ca_connected)
		{
			// force r_wireframe off if not running a local server
			Cvar_Set (&r_wireframe, 0.0f);
		}
	}

	// always clear the zbuffer
	DWORD d3d_ClearFlags = D3DCLEAR_ZBUFFER;

	// if we have a stencil buffer make sure we clear that too otherwise perf will SUFFER
	if (d3d_GlobalCaps.DepthStencilFormat == D3DFMT_D24S8) d3d_ClearFlags |= D3DCLEAR_STENCIL;

	// keep an identity view matrix at all times; this simplifies the setup and also lets us
	// skip a matrix multiplication per vertex in our vertex shaders. ;)
	d3d_ViewMatrix.LoadIdentity ();
	d3d_WorldMatrix.LoadIdentity ();
	d3d_ProjMatrix.LoadIdentity ();

	DWORD clearcolor = 0xff000000;

	// select correct color clearing mode
	// and mode that clears the color buffer needs to change the HUD
	if (!d3d_RenderDef.viewleaf || d3d_RenderDef.viewleaf->contents == CONTENTS_SOLID || !d3d_RenderDef.viewleaf->compressed_vis)
	{
		// match winquake's grey if we're noclipping
		// (note - this should really be a palette index)
		clearcolor = 0xff1f1f1f;
		d3d_ClearFlags |= D3DCLEAR_TARGET;
		// Con_Printf ("Clear to grey\n");
	}
	else if (r_colorfilter.integer != 7)
	{
		clearcolor = 0xff000000;
		d3d_ClearFlags |= D3DCLEAR_TARGET;
	}
	else if (r_wireframe.integer)
	{
		clearcolor = 0xff1f1f1f;
		d3d_ClearFlags |= D3DCLEAR_TARGET;
	}
	else if (r_lockfrustum.integer || r_lockpvs.integer)
	{
		// set to orange for r_lockfrustum, r_lockpvs, etc tests
		clearcolor = 0xffff8000;
		d3d_ClearFlags |= D3DCLEAR_TARGET;
	}
	else if (gl_clear.value)
	{
		clearcolor = d3d_QuakePalette.standard32[r_clearcolor.integer & 255];
		d3d_ClearFlags |= D3DCLEAR_TARGET;
	}

	extern float r_farclip;

	// put z going up (this is done in the view matrix so that world is kept clean and we can derive the view vectors from it)
	d3d_ViewMatrix.YawPitchRoll (0, -90, 90);

	// the world matrix takes the actual position transform
	d3d_WorldMatrix.YawPitchRoll (-r_refdef.viewangles[0], -r_refdef.viewangles[2], -r_refdef.viewangles[1]);
	d3d_WorldMatrix.Translate (-r_refdef.vieworigin[0], -r_refdef.vieworigin[1], -r_refdef.vieworigin[2]);

	// create an initial projection matrix for deriving the frustum from; as Quake only culls against the top/bottom/left/right
	// planes we don't need to worry about the far clipping distance yet; we'll just set it to what it was last frame so it has
	// a good chance of being as close as possible anyway.  this will be set for real as soon as we gather surfaces together in
	// the refresh (which we can't do before this as we need to frustum cull there, and we don't know what the frustum is yet!)
	D3D_SetupProjection (d3d_RenderDef.fov_x, d3d_RenderDef.fov_y, 4, r_farclip);

	// derive these properly
	d3d_WorldMatrix.ToVectors (&r_viewvectors);

	// calculate concatenated final matrix for use by shaders
	// because it's only needed once per frame instead of once per vertex we can save some vs instructions
	QMATRIX::UpdateMVP (&d3d_ModelViewProjMatrix, &d3d_WorldMatrix, &d3d_ViewMatrix, &d3d_ProjMatrix);

	// extract the frustum from it
	// retain the old frustum unless we're in the first few frames in which case we want one to be done
	// as a baseline
	if (!r_lockfrustum.integer || d3d_RenderDef.framecount < 5)
		d3d_ModelViewProjMatrix.ExtractFrustum (frustum);

	for (int i = 0; i < 4; i++)
	{
		frustum[i].type = PLANE_ANYZ;
		frustum[i].dist = DotProduct (r_refdef.vieworigin, frustum[i].normal); //FIXME: shouldn't this always be zero?
		frustum[i].signbits = SignbitsForPlane (&frustum[i]);
	}

	// we only need to clear if we're rendering 3D
	// we clear *before* we set the viewport as clearing a subrect of the rendertarget is slower
	// than just clearing the full thing - see IDirect3DDevice9::Clear in the SDK

	if (r_draworder.value)
	{
		// note - the comment in id quake cvar.h is incorrect; the correct default for r_draworder is 0 (per id quake r_main.c)
		d3d_Device->Clear (0, NULL, d3d_ClearFlags, clearcolor, 0.0f, 1);
		D3DState_SetZBuffer (D3DZB_TRUE, TRUE, D3DCMP_GREATEREQUAL);
	}
	else
	{
		// note - the comment in id quake cvar.h is incorrect; the correct default for r_draworder is 0 (per id quake r_main.c)
		d3d_Device->Clear (0, NULL, d3d_ClearFlags, clearcolor, 1.0f, 1);
		D3DState_SetZBuffer (D3DZB_TRUE, TRUE, D3DCMP_LESSEQUAL);
	}

	// set up the scaled viewport taking account of the status bar area
	if (d3d_RenderDef.RTT)
		D3D_SetViewport (0, 0, D3DRTT_RescaleDimension (vid.ref3dsize.width), D3DRTT_RescaleDimension (vid.ref3dsize.height), 0, 1);
	else D3D_SetViewport (0, 0, vid.ref3dsize.width, vid.ref3dsize.height, 0, 1);

	// backface culling
	D3D_BackfaceCull (D3DCULL_CCW);

	// ensure that our states are correct for this
	D3DState_SetAlphaBlend (FALSE);
	D3DState_SetAlphaTest (FALSE);

	// optionally enable wireframe mode
	if (r_wireframe.integer) D3D_SetRenderState (D3DRS_FILLMODE, D3DFILL_WIREFRAME);
}


cvar_t r_truecontentscolour ("r_truecontentscolour", "1", CVAR_ARCHIVE);

float r_underwaterfogcolours[4];


void D3D_UpdateContentsColor (void)
{
	// the cshift builtin fills the empty colour so we need to handle that
	extern cshift_t cshift_empty;

	switch (d3d_RenderDef.viewleaf->contents)
	{
	case CONTENTS_EMPTY:
		cl.cshifts[CSHIFT_CONTENTS].destcolor[0] = cshift_empty.destcolor[0];
		cl.cshifts[CSHIFT_CONTENTS].destcolor[1] = cshift_empty.destcolor[1];
		cl.cshifts[CSHIFT_CONTENTS].destcolor[2] = cshift_empty.destcolor[2];
		cl.cshifts[CSHIFT_CONTENTS].percent = cshift_empty.percent;
		// fall through

	case CONTENTS_SOLID:
	case CONTENTS_SKY:
		d3d_RenderDef.lastgoodcontents = NULL;
		break;

	default:
		// water, slime or lava
		if (d3d_RenderDef.viewleaf->contentscolor)
		{
			// let the player decide which behaviour they want
			if (r_truecontentscolour.integer)
			{
				// if the viewleaf has a contents colour set we just override with it
				cl.cshifts[CSHIFT_CONTENTS].destcolor[0] = d3d_RenderDef.viewleaf->contentscolor[0];
				cl.cshifts[CSHIFT_CONTENTS].destcolor[1] = d3d_RenderDef.viewleaf->contentscolor[1];
				cl.cshifts[CSHIFT_CONTENTS].destcolor[2] = d3d_RenderDef.viewleaf->contentscolor[2];

				// these now have more colour so reduce the percent to compensate
				if (d3d_RenderDef.viewleaf->contents == CONTENTS_WATER)
					cl.cshifts[CSHIFT_CONTENTS].percent = 48;
				else if (d3d_RenderDef.viewleaf->contents == CONTENTS_LAVA)
					cl.cshifts[CSHIFT_CONTENTS].percent = 112;
				else if (d3d_RenderDef.viewleaf->contents == CONTENTS_SLIME)
					cl.cshifts[CSHIFT_CONTENTS].percent = 80;
				else cl.cshifts[CSHIFT_CONTENTS].percent = 0;
			}
			else V_SetContentsColor (d3d_RenderDef.viewleaf->contents);

			// this was the last good colour used
			d3d_RenderDef.lastgoodcontents = d3d_RenderDef.viewleaf->contentscolor;
			break;
		}
		else if (d3d_RenderDef.lastgoodcontents)
		{
			// the leaf tracing at load time can occasionally miss a leaf so we take it from the last
			// good one we used unless we've had a contents change since.  this seems to only happen
			// with watervised maps that have been through bsp2prt so it seems as though there is
			// something wonky in the BSP tree on these maps.....
			if (d3d_RenderDef.oldviewleaf->contents == d3d_RenderDef.viewleaf->contents)
			{
				// update it and call recursively to get the now updated colour
				// Con_Printf ("D3D_UpdateContentsColor : fallback to last good contents!\n");
				d3d_RenderDef.viewleaf->contentscolor = d3d_RenderDef.lastgoodcontents;
				D3D_UpdateContentsColor ();
				return;
			}
		}

		// either we've had no last good contents colour or a contents transition so we
		// just fall back to the hard-coded default.  be sure to clear the last good as
		// we may have had a contents transition!!!
		V_SetContentsColor (d3d_RenderDef.viewleaf->contents);
		d3d_RenderDef.lastgoodcontents = NULL;
		break;
	}

	if (gl_underwaterfog.value > 0 && cl.cshifts[CSHIFT_CONTENTS].percent)
	{
		// derive an underwater fog colour from the contents shift
		r_underwaterfogcolours[0] = (float) cl.cshifts[CSHIFT_CONTENTS].destcolor[0] / 255.0f;
		r_underwaterfogcolours[1] = (float) cl.cshifts[CSHIFT_CONTENTS].destcolor[1] / 255.0f;
		r_underwaterfogcolours[2] = (float) cl.cshifts[CSHIFT_CONTENTS].destcolor[2] / 255.0f;
		r_underwaterfogcolours[3] = (float) cl.cshifts[CSHIFT_CONTENTS].percent / 100.0f;

		// reduce the contents shift
		cl.cshifts[CSHIFT_CONTENTS].percent *= 2;
		cl.cshifts[CSHIFT_CONTENTS].percent /= 3;
	}
	else r_underwaterfogcolours[3] = 0;

	// and now calc the final blend
	V_CalcBlend ();
}


/*
================
R_ModParanoia

mods sometimes send skin, frame and other numbers that are not valid for the model
so here we need to fix them up...
================
*/
void R_ModParanoia (entity_t *ent)
{
	model_t *mod = ent->model;

	switch (mod->type)
	{
	case mod_alias:
		// fix up skins
		if (ent->skinnum >= mod->aliashdr->numskins) ent->skinnum = 0;
		if (ent->skinnum < 0) ent->skinnum = 0;

		// fix up frame
		if (ent->frame >= mod->aliashdr->numframes) ent->frame = 0;
		if (ent->frame < 0) ent->frame = 0;

		break;

	case mod_brush:
		// only 2 frames in brushmodels for regular and alternate anims
		ent->frame = !!ent->frame;
		break;

	case mod_sprite:
		// fix up frame
		if (ent->frame >= mod->spritehdr->numframes) ent->frame = 0;
		if (ent->frame < 0) ent->frame = 0;

		break;

	default:
		// do nothing
		break;
	}
}


int r_speedstime = -1;


/*
================
R_RenderView

r_refdef must be set before the first call
================
*/

cvar_t r_skyfog ("r_skyfog", 0.5f, CVAR_ARCHIVE);

void D3DHLSL_SelectShader (int desiredshader);

void D3DRMain_HLSLSetup (void)
{
	extern float d3d_FogColor[];
	extern float d3d_FogDensity;
	extern cvar_t gl_fogenable;
	extern cvar_t gl_fogdensityscale;

	int desiredshader = HLSL_PLAIN;

	if (d3d_FogDensity > 0 || r_underwaterfogcolours[3] > 0) desiredshader |= HLSL_FOG;

	// select the appropriate shaders to use
	D3DHLSL_SelectShader (desiredshader);

	// add basic params for drawing the world
	// (the world matrix is deferred until after we find the correct max depth to use)
	D3DHLSL_SetFloat ("Overbright", 1.0f);

	D3DHLSL_SetFloatArray ("r_origin", r_refdef.vieworigin, 3);
	D3DHLSL_SetFloatArray ("viewangles", r_refdef.viewangles, 3);

	D3DHLSL_SetFloatArray ("viewforward", r_viewvectors.forward, 3);
	D3DHLSL_SetFloatArray ("viewright", r_viewvectors.right, 3);
	D3DHLSL_SetFloatArray ("viewup", r_viewvectors.up, 3);

	float RealFogDensity = 0;

	if (r_underwaterfogcolours[3] > 0)
	{
		// Con_Printf ("enabled underwater fog\n");
		// approximate; looks OK
		D3DHLSL_SetFloatArray ("FogColor", r_underwaterfogcolours, 4);
		D3DHLSL_SetFloat ("SkyFog", 0.025f);
		RealFogDensity = (r_underwaterfogcolours[3] / 640.0f) * gl_underwaterfog.value;
	}
	else
	{
		D3DHLSL_SetFloatArray ("FogColor", d3d_FogColor, 4);

		if (nehahra)
		{
			// nehahra assumes that sky is drawn at a more or less infinite distance
			// and we try to replicate the original within a reasonable tolerance
			if (d3d_FogDensity > 0 && gl_fogenable.value)
				D3DHLSL_SetFloat ("SkyFog", 0.025f);
			else D3DHLSL_SetFloat ("SkyFog", 1.0f);

			// nehahra fog divides by 100
			if (gl_fogdensityscale.value > 0)
				RealFogDensity = (d3d_FogDensity / gl_fogdensityscale.value);
			else RealFogDensity = (d3d_FogDensity / 100.0f);
		}
		else
		{
			if (d3d_FogDensity > 0 && r_skyfog.value > 0 && gl_fogenable.value)
				D3DHLSL_SetFloat ("SkyFog", 1.0f - r_skyfog.value);
			else D3DHLSL_SetFloat ("SkyFog", 1.0f);

			// fitz fog divides by 64
			if (gl_fogdensityscale.value > 0)
				RealFogDensity = d3d_FogDensity / gl_fogdensityscale.value;
			else RealFogDensity = d3d_FogDensity / 64.0f;
		}
	}

	// precalc this to save some shader instructions
	RealFogDensity = -RealFogDensity * RealFogDensity;

	// divide by log(2) precalced for correct scale and speed
	D3DHLSL_SetFloat ("FogDensity", RealFogDensity * 0.6931471f);
}


void D3DMain_PolyBlend (void)
{
	if (!(gl_polyblend.value > 0.0f)) return;
	if (v_blend[3] < 1) return;
	if (d3d_RenderDef.RTT) return;

	float alpha = (float) v_blend[3] * gl_polyblend.value;

	DWORD blendcolor = D3DCOLOR_ARGB
	(
		BYTE_CLAMP (alpha),
		BYTE_CLAMP (v_blend[0]),
		BYTE_CLAMP (v_blend[1]),
		BYTE_CLAMP (v_blend[2])
	);

	D3DHLSL_SetPass (FX_PASS_POLYBLEND);

	D3DState_SetZBuffer (D3DZB_FALSE, FALSE);
	D3DState_SetAlphaBlend (TRUE);

	quadvert_t *verts = NULL;

	D3DQuads_Begin (1, &verts);

	VectorSet (verts[0].xyz, -1, 1, 0);
	verts[0].color = blendcolor;

	VectorSet (verts[1].xyz, 1, 1, 0);
	verts[1].color = blendcolor;

	VectorSet (verts[2].xyz, 1, -1, 0);
	verts[2].color = blendcolor;

	VectorSet (verts[3].xyz, -1, -1, 0);
	verts[3].color = blendcolor;

	D3DQuads_End ();

	v_blend[3] = 0;
}


void D3DSurf_DrawBrushModel (entity_t *ent);
void D3DAlias_AddModelToList (entity_t *ent);

void D3DMain_SetupEntitiesOnList (void)
{
	// deferred until after the world so that we get statics on the list too
	for (int i = 0; i < d3d_RenderDef.numvisedicts; i++)
	{
		entity_t *ent = d3d_RenderDef.visedicts[i];

		if (!ent->model) continue;

		// this ent had it's model merged to the world
		if (ent->mergeframe == d3d_RenderDef.framecount) continue;

		// add entities to the draw lists
		if (!r_drawentities.integer) continue;

		R_ModParanoia (ent);
		D3DMain_BBoxForEnt (ent);

		// to save on extra passes through the list we setup alias and sprite models here too
		if (ent->model->type == mod_alias)
			D3DAlias_AddModelToList (ent);
		else if (ent->model->type == mod_brush)
			D3DSurf_DrawBrushModel (ent);
		else if (ent->model->type == mod_sprite)
			D3DAlpha_AddToList (ent);
		else if (ent->model->type == mod_iqm)
			;
		else Con_Printf ("Unknown model type for entity: %i\n", ent->model->type);
	}

	// reset to the original world matrix
	D3DHLSL_SetWorldMatrix (&d3d_ModelViewProjMatrix);
}


void V_AdjustContentCShift (int contents);
void D3DAlias_InitList (void);

void R_RenderView (double frametime)
{
	double dTime1 = 0, dTime2 = 0;

	if (!cls.maprunning) return;

	if (r_norefresh.value)
	{
		d3d_Device->Clear (0, NULL, D3DCLEAR_TARGET, 0xff332200, 1.0f, 1);
		return;
	}

	if (r_speeds.value) dTime1 = Sys_FloatTime ();

	// initialize stuff
	D3DLight_SetCoronaState ();
	R_SetupFrame ();
	D3DWarp_InitializeTurb ();
	D3D_UpdateContentsColor ();
	V_AdjustContentCShift (d3d_RenderDef.viewleaf->contents);

	// set up to render
	D3DMain_SetupD3D ();
	D3DRMain_HLSLSetup ();
	D3DAlias_InitList ();

	// enable simplistic colour filtering for night goggle/etc effects
	if (r_colorfilter.integer != 7) D3D_SetRenderState (D3DRS_COLORWRITEENABLE, 8 | (r_colorfilter.integer & 7));

	// bbcull dynamic lights
	R_CullDynamicLights ();

	// build the world model
	D3DSurf_DrawWorld ();

	// prep entities for drawing (and also draw any leftover bmodels)
	D3DMain_SetupEntitiesOnList ();

	// draw our alias models
	D3DAlias_RenderAliasModels ();
	D3DIQM_DrawIQMs ();

	// add particles to alpha list always
	D3D_AddParticesToAlphaList ();

	// add coronas to the alpha list
	D3DLight_AddCoronas ();

	// draw all items on the alpha list
	D3DAlpha_RenderList ();

	// optionally show bboxes
	D3DOC_ShowBBoxes ();

	// the viewmodel comes last
	// note - the gun model code assumes that it's the last thing drawn in the 3D view
	D3DAlias_DrawViewModel ();

	if (r_colorfilter.integer != 7) D3D_SetRenderState (D3DRS_COLORWRITEENABLE, 0xf);

	// ensure
	D3D_SetRenderState (D3DRS_FILLMODE, D3DFILL_SOLID);

	D3DMain_PolyBlend ();

	if (r_speeds.value)
	{
		dTime2 = Sys_FloatTime ();
		r_speedstime = (int) ((dTime2 - dTime1) * 1000.0);
	}
	else r_speedstime = -1;
}


