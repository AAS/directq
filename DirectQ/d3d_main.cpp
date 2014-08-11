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

cvar_t gl_fullbrights ("gl_fullbrights", "1", CVAR_ARCHIVE);

void D3DMain_CreateVertexBuffer (UINT length, DWORD usage, LPDIRECT3DVERTEXBUFFER9 *buf)
{
	DWORD swprocflag = d3d_GlobalCaps.supportHardwareTandL ? 0 : D3DUSAGE_SOFTWAREPROCESSING;

	// make attempts to create the vertex buffer with various combinations of usage flags and memory pools
	hr = d3d_Device->CreateVertexBuffer (length, usage | swprocflag, 0, D3DPOOL_DEFAULT, buf, NULL);
	if (SUCCEEDED (hr)) return;

	hr = d3d_Device->CreateVertexBuffer (length, D3DUSAGE_WRITEONLY | swprocflag, 0, D3DPOOL_MANAGED, buf, NULL);
	if (SUCCEEDED (hr)) return;

	hr = d3d_Device->CreateVertexBuffer (length, swprocflag, 0, D3DPOOL_MANAGED, buf, NULL);
	if (SUCCEEDED (hr)) return;

	if (!d3d_GlobalCaps.supportHardwareTandL)
	{
		hr = d3d_Device->CreateVertexBuffer (length, usage, 0, D3DPOOL_DEFAULT, buf, NULL);
		if (SUCCEEDED (hr)) return;

		hr = d3d_Device->CreateVertexBuffer (length, D3DUSAGE_WRITEONLY, 0, D3DPOOL_MANAGED, buf, NULL);
		if (SUCCEEDED (hr)) return;

		hr = d3d_Device->CreateVertexBuffer (length, 0, 0, D3DPOOL_MANAGED, buf, NULL);
		if (SUCCEEDED (hr)) return;
	}

	// nope, can't do it
	Sys_Error ("D3DMain_CreateVertexBuffer : IDirect3DDevice9::CreateVertexBuffer failed");
}


void D3DMain_CreateIndexBuffer (UINT numindexes, DWORD usage, LPDIRECT3DINDEXBUFFER9 *buf)
{
	DWORD swprocflag = d3d_GlobalCaps.supportHardwareTandL ? 0 : D3DUSAGE_SOFTWAREPROCESSING;
	UINT length = numindexes * sizeof (unsigned short);

	// make attempts to create the vertex buffer with various combinations of usage flags and memory pools
	hr = d3d_Device->CreateIndexBuffer (length, usage | swprocflag, D3DFMT_INDEX16, D3DPOOL_DEFAULT, buf, NULL);
	if (SUCCEEDED (hr)) return;

	hr = d3d_Device->CreateIndexBuffer (length, D3DUSAGE_WRITEONLY | swprocflag, D3DFMT_INDEX16, D3DPOOL_MANAGED, buf, NULL);
	if (SUCCEEDED (hr)) return;

	hr = d3d_Device->CreateIndexBuffer (length, swprocflag, D3DFMT_INDEX16, D3DPOOL_MANAGED, buf, NULL);
	if (SUCCEEDED (hr)) return;

	if (!d3d_GlobalCaps.supportHardwareTandL)
	{
		hr = d3d_Device->CreateIndexBuffer (length, usage, D3DFMT_INDEX16, D3DPOOL_DEFAULT, buf, NULL);
		if (SUCCEEDED (hr)) return;

		hr = d3d_Device->CreateIndexBuffer (length, D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_MANAGED, buf, NULL);
		if (SUCCEEDED (hr)) return;

		hr = d3d_Device->CreateIndexBuffer (length, 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, buf, NULL);
		if (SUCCEEDED (hr)) return;
	}

	// nope, can't do it
	Sys_Error ("D3DMain_CreateIndexBuffer : IDirect3DDevice9::CreateIndexBuffer failed");
}


// this must be big enough for the largest of draw, sprite or particles
#define MAX_MAIN_INDEXES	90000

LPDIRECT3DINDEXBUFFER9 d3d_MainIBO = NULL;

void D3DMain_CreateBuffers (void)
{
	if (!d3d_MainIBO)
	{
		D3DMain_CreateIndexBuffer (MAX_MAIN_INDEXES, D3DUSAGE_WRITEONLY, &d3d_MainIBO);

		// now we fill in the index buffer; this is a non-dynamic index buffer and it only needs to be set once
		unsigned short *ndx = NULL;
		int NumMainVerts = (MAX_MAIN_INDEXES / 6) * 4;

		hr = d3d_MainIBO->Lock (0, 0, (void **) &ndx, 0);
		if (FAILED (hr)) Sys_Error ("D3DMain_CreateBuffers: failed to lock index buffer");

		for (int i = 0; i < NumMainVerts; i += 4, ndx += 6)
		{
			// index these as strips to help out certain hardware
			ndx[0] = i + 0;
			ndx[1] = i + 1;
			ndx[2] = i + 2;

			ndx[3] = i + 1;
			ndx[4] = i + 3;
			ndx[5] = i + 2;
		}

		hr = d3d_MainIBO->Unlock ();
		if (FAILED (hr)) Sys_Error ("D3DMain_CreateBuffers: failed to unlock index buffer");
	}
}


void D3DMain_ReleaseBuffers (void)
{
	SAFE_RELEASE (d3d_MainIBO);
}


CD3DDeviceLossHandler d3d_MainBuffersHandler (D3DMain_ReleaseBuffers, D3DMain_CreateBuffers);


extern LPDIRECT3DTEXTURE9 char_texture;

// this one wraps DIP so that I can check for commit and anything else i need to do before drawing
void D3D_DrawIndexedPrimitive (int FirstVertex, int NumVertexes, int FirstIndex, int NumPrimitives)
{
	// check that all of our states are nice and correct
	D3DHLSL_CheckCommit ();

	// and now we can draw
	d3d_Device->DrawIndexedPrimitive (D3DPT_TRIANGLELIST, 0, FirstVertex, NumVertexes, FirstIndex, NumPrimitives);
	d3d_RenderDef.numdrawprim++;
}


void D3D_PrelockVertexBuffer (LPDIRECT3DVERTEXBUFFER9 vb)
{
	void *blah = NULL;

	hr = vb->Lock (0, 0, &blah, D3DLOCK_DISCARD);
	if (FAILED (hr)) Sys_Error ("D3D_PrelockVertexBuffer: failed to lock vertex buffer");

	hr = vb->Unlock ();
	if (FAILED (hr)) Sys_Error ("D3D_PrelockVertexBuffer: failed to unlock vertex buffer");
}


void D3D_PrelockIndexBuffer (LPDIRECT3DINDEXBUFFER9 ib)
{
	void *blah = NULL;

	hr = ib->Lock (0, 0, &blah, D3DLOCK_DISCARD);
	if (FAILED (hr)) Sys_Error ("D3D_PrelockVertexBuffer: failed to lock index buffer");

	hr = ib->Unlock ();
	if (FAILED (hr)) Sys_Error ("D3D_PrelockVertexBuffer: failed to unlock index buffer");
}


// use unit scales that are also used in most map editors
cvar_t r_automapscroll_x ("r_automapscroll_x", -64.0f, CVAR_ARCHIVE);
cvar_t r_automapscroll_y ("r_automapscroll_y", -64.0f, CVAR_ARCHIVE);
cvar_t r_automapscroll_z ("r_automapscroll_z", 32.0f, CVAR_ARCHIVE);

bool r_automap;
extern bool scr_drawmapshot;

// default automap viewpoint
// x and y are updated on entry to the automap
float r_automap_x = 0;
float r_automap_y = 0;
float r_automap_z = 0;
float r_automap_scale = 5;
int automap_key = -1;
int screenshot_key = -1;

void Cmd_ToggleAutomap_f (void)
{
	if (cls.state != ca_connected) return;

	r_automap = !r_automap;

	// toggle a paused state
	if (key_dest == key_automap)
		key_dest = key_game;
	else key_dest = key_automap;

	// find which key is bound to the automap
	automap_key = Key_GetBinding ("toggleautomap");

	// find any other key defs we want to allow
	screenshot_key = Key_GetBinding ("screenshot");

	r_automap_x = r_refdef.vieworg[0];
	r_automap_y = r_refdef.vieworg[1];
	r_automap_z = 0;
}


void Key_Automap (int key)
{
	switch (key)
	{
	case K_PGUP:
		r_automap_z += r_automapscroll_z.value;
		break;

	case K_PGDN:
		r_automap_z -= r_automapscroll_z.value;
		break;

	case K_UPARROW:
		r_automap_y -= r_automapscroll_y.value;
		break;

	case K_DOWNARROW:
		r_automap_y += r_automapscroll_y.value;
		break;

	case K_LEFTARROW:
		r_automap_x += r_automapscroll_x.value;
		break;

	case K_RIGHTARROW:
		r_automap_x -= r_automapscroll_x.value;
		break;

	case K_HOME:
		r_automap_scale -= 0.5f;
		break;

	case K_END:
		r_automap_scale += 0.5f;
		break;

	default:

		if (key == automap_key)
			Cmd_ToggleAutomap_f ();
		else if (key == screenshot_key)
		{
			Cbuf_InsertText ("screenshot\n");
			Cbuf_Execute ();
		}

		break;
	}

	if (r_automap_scale < 0.5) r_automap_scale = 0.5;
	if (r_automap_scale > 20) r_automap_scale = 20;
}


cmd_t Cmd_ToggleAutomap ("toggleautomap", Cmd_ToggleAutomap_f);

int c_automapsurfs = 0;

bool D3D_DrawAutomap (void)
{
	if (!r_automap) return false;
	if (cls.state != ca_connected) return false;
	if (scr_drawmapshot) return false;
	if (cl.intermission == 1 && key_dest == key_game) return false;
	if (cl.intermission == 2 && key_dest == key_game) return false;

	c_automapsurfs = 0;
	return true;
}


void D3D_AutomapReset (void)
{
}


void RotatePointAroundVector (vec3_t dst, const vec3_t dir, const vec3_t point, float degrees);
void R_AnimateLight (void);
void V_CalcBlend (void);

void D3D_BuildWorld (void);
void D3D_AddWorldSurfacesToRender (void);

void D3DWarp_InitializeTurb (void);
void D3D_DrawAlphaWaterSurfaces (void);
void D3D_DrawOpaqueWaterSurfaces (void);

void D3DAlias_DrawViewModel (void);

void D3DAlias_RenderAliasModels (void);

void D3D_PrepareAliasModel (entity_t *e);

void D3D_AutomapReset (void);

DWORD D3D_OVERBRIGHT_MODULATE = D3DTOP_MODULATE2X;
float d3d_OverbrightModulate = 2.0f;

D3DMATRIX d3d_ViewMatrix;
D3DMATRIX d3d_WorldMatrix;
D3DMATRIX d3d_ModelViewProjMatrix;
D3DMATRIX d3d_ProjMatrix;

// render definition for this frame
d3d_renderdef_t d3d_RenderDef;

// right left
mplane_t	frustum[4];

// view origin
vec3_t	vup;
vec3_t	vpn;
vec3_t	vright;
vec3_t	r_origin;

// screen size info
refdef_t	r_refdef;

texture_t	*r_notexture_mip;

int		d_lightstylevalue[256];	// 8.8 fraction of base light value

cvar_t	r_norefresh ("r_norefresh", "0");
cvar_t	r_drawentities ("r_drawentities", "1");
cvar_t	r_drawviewmodel ("r_drawviewmodel", "1");
cvar_t	r_speeds ("r_speeds", "0");
cvar_t	r_fullbright ("r_fullbright", "0");
cvar_t	r_lightmap ("r_lightmap", "0");
cvar_t	r_shadows ("r_shadows", "0", CVAR_ARCHIVE);
cvar_t	r_wateralpha ("r_wateralpha", 1.0f);
cvar_t	r_dynamic ("r_dynamic", "1");
cvar_t	r_novis ("r_novis", "0");

cvar_t	gl_cull ("gl_cull", "1");
cvar_t	gl_smoothmodels ("gl_smoothmodels", "1");
cvar_t	gl_affinemodels ("gl_affinemodels", "0");
cvar_t	gl_polyblend ("gl_polyblend", "1");
cvar_t	gl_nocolors ("gl_nocolors", "0");
cvar_t	gl_doubleeyes ("gl_doubleeys", "1");
cvar_t	gl_clear ("gl_clear", "0");

// hmmm- this does nothing yet...
cvar_t gl_underwaterfog ("gl_underwaterfog", 1, CVAR_ARCHIVE);

cvar_t r_lockfrustum ("r_lockfrustum", 0.0f);
cvar_t r_wireframe ("r_wireframe", 0.0f);

extern float r_automap_x;
extern float r_automap_y;
extern float r_automap_z;
extern float r_automap_scale;
int automap_culls = 0;
cvar_t r_automap_nearclip ("r_automap_nearclip", "48", CVAR_ARCHIVE);

bool R_AutomapCull (vec3_t emins, vec3_t emaxs)
{
	// assume it's going to be culled
	automap_culls++;

	// too near
	if (emins[2] > (r_refdef.vieworg[2] + r_automap_nearclip.integer + r_automap_z)) return true;

	// check against screen dimensions which have been scaled and translated to automap space
	if (emaxs[0] < frustum[0].dist) return 1;
	if (emins[0] > frustum[1].dist) return 1;
	if (emaxs[1] < frustum[2].dist) return 1;
	if (emins[1] > frustum[3].dist) return 1;

	// not culled
	automap_culls--;
	return false;
}


void D3D_BeginVisedicts (void)
{
	// these can't go into the scratchbuf because of SCR_UpdateScreen calls in places like SCR_BeginLoadingPlaque
	if (!d3d_RenderDef.visedicts) d3d_RenderDef.visedicts = (entity_t **) MainZone->Alloc (MAX_VISEDICTS * sizeof (entity_t *));

	d3d_RenderDef.numvisedicts = 0;
}


void D3D_AddVisEdict (entity_t *ent)
{
	// check for entities with no models
	if (!ent->model) return;

	// only add entities with supported model types
	if (ent->model->type == mod_alias || ent->model->type == mod_brush || ent->model->type == mod_sprite)
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
	}
	else Con_DPrintf ("D3D_AddVisEdict: Unknown entity model type for %s\n", ent->model->name);
}


/*
=================
R_CullBox

Returns true if the box is completely outside the frustum
=================
*/
bool R_CullBox (vec3_t emins, vec3_t emaxs)
{
	// different culling for the automap
	if (d3d_RenderDef.automap) return R_AutomapCull (emins, emaxs);

	for (int i = 0; i < 4; i++)
		if (BoxOnPlaneSide (emins, emaxs, frustum + i) == 2)
			return true;

	return false;
}


bool R_CullBox (mnode_t *node)
{
	// different culling for the automap
	if (d3d_RenderDef.automap) return R_AutomapCull (node->mins, node->maxs);

	// if the node's parent was fully outside the frustum then the node is also fully outside the frustum
	// (note that this should be a trivial reject case as the children of nodes fully outside should never be traversed)
	if (node->parent && node->parent->bops == FULLY_OUTSIDE_FRUSTUM)
	{
		node->bops = FULLY_OUTSIDE_FRUSTUM;
		return true;
	}

	// if the node's parent was fully inside the frustum then the node is also fully inside the frustum
	if (node->parent && node->parent->bops == FULLY_INSIDE_FRUSTUM)
	{
		node->bops = FULLY_INSIDE_FRUSTUM;
		return false;
	}

	// the node's parent intersected the frustum (or the node has no parent) so run a full check
	// BoxOnPlaneSide result flags are unknown
	node->bops = 0;

	// need to check all 4 sides
	for (int i = 0; i < 4; i++)
	{
		// if the parent is inside the frustum on this side then so is this node
		if (node->parent && node->parent->sides[i] == INSIDE_FRUSTUM) continue;

		// need to check
		node->sides[i] |= BoxOnPlaneSide (node->mins, node->maxs, frustum + i);

		// if the node is outside the frustum on any side then the entire node is rejected
		if (node->sides[i] == OUTSIDE_FRUSTUM)
		{
			node->bops = FULLY_OUTSIDE_FRUSTUM;
			return true;
		}

		if (node->sides[i] == INTERSECT_FRUSTUM)
		{
			// if any side intersects the frustum we mark it all as intersecting so that we can do comparisons more cleanly
			node->bops = FULLY_INTERSECT_FRUSTUM;
			return false;
		}
	}

	// the node will be fully inside the frustum here because none of the sides evaluated to outside or intersecting
	// set the bops flag correctly as it may have been skipped above
	node->bops = FULLY_INSIDE_FRUSTUM;
	return false;
}


void D3D_RotateForEntity (entity_t *e, D3DMATRIX *m)
{
	D3DMatrix_Translate (m, e->origin[0], e->origin[1], e->origin[2]);
	D3DMatrix_Rotate (m, 0, 0, 1, e->angles[1]);
	D3DMatrix_Rotate (m, 0, 1, 0, -e->angles[0]);
	D3DMatrix_Rotate (m, 1, 0, 0, e->angles[2]);
}


void D3D_RotateForEntity (entity_t *e)
{
	D3D_RotateForEntity (e, &e->matrix);
}


//==================================================================================

int SignbitsForPlane (mplane_t *out)
{
	int	bits, j;

	// for fast box on planeside test
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

	R_AnimateLight ();

	// current viewleaf
	d3d_RenderDef.oldviewleaf = d3d_RenderDef.viewleaf;
	d3d_RenderDef.viewleaf = Mod_PointInLeaf (r_refdef.vieworg, cl.worldmodel);

	d3d_RenderDef.fov_x = r_refdef.fov_x;
	d3d_RenderDef.fov_y = r_refdef.fov_y;

	VectorCopy (r_refdef.vieworg, r_origin);
	AngleVectors (r_refdef.viewangles, vpn, vright, vup);
}


void D3D_PrepareOverbright (void)
{
	// bound 0 to 2 - (float) 0 is required for overload
	if (r_overbright.integer < 0) Cvar_Set (&r_overbright, 0.0f);
	if (r_overbright.integer > 2) Cvar_Set (&r_overbright, 2.0f);

	if (r_overbright.integer < 1)
	{
		D3D_OVERBRIGHT_MODULATE = D3DTOP_MODULATE;
		d3d_OverbrightModulate = 1.0f;
	}
	else if (r_overbright.integer > 1)
	{
		D3D_OVERBRIGHT_MODULATE = D3DTOP_MODULATE4X;
		d3d_OverbrightModulate = 4.0f;
	}
	else
	{
		D3D_OVERBRIGHT_MODULATE = D3DTOP_MODULATE2X;
		d3d_OverbrightModulate = 2.0f;
	}
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


void D3D_SetupProjection (float fovx, float fovy)
{
#define NEARCLIP 1.0
	float nudge = 1.0 - 1.0 / (1 << 23);

	float right = NEARCLIP * tan (fovx * D3DX_PI / 360.0);
	float left = -right;

	float top = NEARCLIP * tan (fovy * D3DX_PI / 360.0);
	float bottom = -top;

	d3d_ProjMatrix._11 = (2 * NEARCLIP) / (right - left);
	d3d_ProjMatrix._21 = 0;
	d3d_ProjMatrix._31 = (right + left) / (right - left);
	d3d_ProjMatrix._41 = 0;

	d3d_ProjMatrix._12 = 0;
	d3d_ProjMatrix._22 = (2 * NEARCLIP) / (top - bottom);
	d3d_ProjMatrix._32 = (top + bottom) / (top - bottom);
	d3d_ProjMatrix._42 = 0;

	d3d_ProjMatrix._13 = 0;
	d3d_ProjMatrix._23 = 0;
	d3d_ProjMatrix._33 = -1 * nudge;
	d3d_ProjMatrix._43 = -2 * NEARCLIP * nudge;

	d3d_ProjMatrix._14 = 0;
	d3d_ProjMatrix._24 = 0;
	d3d_ProjMatrix._34 = -1;
	d3d_ProjMatrix._44 = 0;

	if (r_waterwarp.value > 1)
	{
		int contents = Mod_PointInLeaf (r_origin, cl.worldmodel)->contents;

		if (contents == CONTENTS_WATER || contents == CONTENTS_SLIME || contents == CONTENTS_LAVA)
		{
			d3d_ProjMatrix._21 = sin (d3d_RenderDef.time * 1.125f) * 0.0666f;
			d3d_ProjMatrix._12 = cos (d3d_RenderDef.time * 1.125f) * 0.0666f;

			D3DMatrix_Scale (&d3d_ProjMatrix, (cos (d3d_RenderDef.time * 0.75f) + 20.0f) * 0.05f, (sin (d3d_RenderDef.time * 0.75f) + 20.0f) * 0.05f, 1);
		}
	}
}


void D3D_ExtractFrustum (void)
{
	// retain the old frustum unless we're in the first few frames in which case we want one to be done
	// as a baseline
	if (r_lockfrustum.integer && d3d_RenderDef.framecount > 5) return;

	if (d3d_RenderDef.automap)
	{
		// scale and translate to automap space for R_AutomapCull
		// normal, dist, type are unused here
		frustum[0].dist = r_automap_x - ((vid.width / 2) * r_automap_scale);
		frustum[1].dist = r_automap_x + ((vid.width / 2) * r_automap_scale);
		frustum[2].dist = r_automap_y - ((vid.height / 2) * r_automap_scale);
		frustum[3].dist = r_automap_y + ((vid.height / 2) * r_automap_scale);
		return;
	}

	// frustum 0 (right plane)
	frustum[0].normal[0] = d3d_ModelViewProjMatrix._14 - d3d_ModelViewProjMatrix._11;
	frustum[0].normal[1] = d3d_ModelViewProjMatrix._24 - d3d_ModelViewProjMatrix._21;
	frustum[0].normal[2] = d3d_ModelViewProjMatrix._34 - d3d_ModelViewProjMatrix._31;

	// frustum 1 (left plane)
	frustum[1].normal[0] = d3d_ModelViewProjMatrix._14 + d3d_ModelViewProjMatrix._11;
	frustum[1].normal[1] = d3d_ModelViewProjMatrix._24 + d3d_ModelViewProjMatrix._21;
	frustum[1].normal[2] = d3d_ModelViewProjMatrix._34 + d3d_ModelViewProjMatrix._31;

	// frustum 2 (bottom plane)
	frustum[2].normal[0] = d3d_ModelViewProjMatrix._14 + d3d_ModelViewProjMatrix._12;
	frustum[2].normal[1] = d3d_ModelViewProjMatrix._24 + d3d_ModelViewProjMatrix._22;
	frustum[2].normal[2] = d3d_ModelViewProjMatrix._34 + d3d_ModelViewProjMatrix._32;

	// frustum 3 (top plane)
	frustum[3].normal[0] = d3d_ModelViewProjMatrix._14 - d3d_ModelViewProjMatrix._12;
	frustum[3].normal[1] = d3d_ModelViewProjMatrix._24 - d3d_ModelViewProjMatrix._22;
	frustum[3].normal[2] = d3d_ModelViewProjMatrix._34 - d3d_ModelViewProjMatrix._32;

	for (int i = 0; i < 4; i++)
	{
		VectorNormalize (frustum[i].normal);

		frustum[i].type = PLANE_ANYZ;
		frustum[i].dist = DotProduct (r_origin, frustum[i].normal); //FIXME: shouldn't this always be zero?
		frustum[i].signbits = SignbitsForPlane (&frustum[i]);
	}
}


/*
=============
D3D_PrepareRender
=============
*/
void D3DMain_SetupD3D (void)
{
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
	D3DMatrix_Identity (&d3d_ViewMatrix);
	D3DMatrix_Identity (&d3d_WorldMatrix);
	D3DMatrix_Identity (&d3d_ProjMatrix);

	DWORD clearcolor = 0xff000000;
	extern cvar_t r_lockpvs;

	// select correct color clearing mode
	// and mode that clears the color buffer needs to change the HUD
	if (!d3d_RenderDef.viewleaf || d3d_RenderDef.viewleaf->contents == CONTENTS_SOLID || !d3d_RenderDef.viewleaf->compressed_vis)
	{
		// match winquake's grey if we're noclipping
		// (note - this should really be a palette index)
		clearcolor = 0xff1f1f1f;
		d3d_ClearFlags |= D3DCLEAR_TARGET;
		HUD_Changed ();
		// Con_Printf ("Clear to grey\n");
	}
	else if (r_wireframe.integer)
	{
		clearcolor = 0xff1f1f1f;
		d3d_ClearFlags |= D3DCLEAR_TARGET;
		HUD_Changed ();
	}
	else if (r_lockfrustum.integer || r_lockpvs.integer)
	{
		// set to orange for r_lockfrustum, r_lockpvs, etc tests
		clearcolor = 0xffff8000;
		d3d_ClearFlags |= D3DCLEAR_TARGET;
		HUD_Changed ();
	}
	else if (gl_clear.value)
	{
		clearcolor = 0xff000000;
		d3d_ClearFlags |= D3DCLEAR_TARGET;
		HUD_Changed ();
	}

	// projection matrix depends on drawmode
	if (d3d_RenderDef.automap)
	{
		// change clear color to black and force a clear
		clearcolor = 0xff000000;
		d3d_ClearFlags |= D3DCLEAR_TARGET;
		HUD_Changed ();

		// nothing has been culled yet
		automap_culls = 0;

		float maxdepth = cl.worldmodel->maxs[2];

		if (fabs (cl.worldmodel->mins[2]) > maxdepth) maxdepth = fabs (cl.worldmodel->mins[2]);

		maxdepth += 100;

		// set a near clipping plane based on the refdef vieworg
		// here we retain the same space as the world coords
		D3DMatrix_OrthoOffCenterRH
		(
			&d3d_ProjMatrix,
			0,
			vid.width * r_automap_scale,
			0,
			vid.height * r_automap_scale,
			-maxdepth,
			maxdepth
		);

		// translate camera by origin
		D3DMatrix_Translate
		(
			&d3d_WorldMatrix,
			-(r_automap_x - (vid.width / 2) * r_automap_scale),
			-(r_automap_y - (vid.height / 2) * r_automap_scale),
			-r_refdef.vieworg[2]
		);
	}
	else
	{
		// put z going up
		D3DMatrix_Rotate (&d3d_ViewMatrix, 1, 0, 0, -90);
		D3DMatrix_Rotate (&d3d_ViewMatrix, 0, 0, 1, 90);

		// rotate camera by angles
		D3DMatrix_Rotate (&d3d_ViewMatrix, 1, 0, 0, -r_refdef.viewangles[2]);
		D3DMatrix_Rotate (&d3d_ViewMatrix, 0, 1, 0, -r_refdef.viewangles[0]);
		D3DMatrix_Rotate (&d3d_ViewMatrix, 0, 0, 1, -r_refdef.viewangles[1]);

		// translate camera by origin
		D3DMatrix_Translate (&d3d_ViewMatrix, -r_refdef.vieworg[0], -r_refdef.vieworg[1], -r_refdef.vieworg[2]);

		// set projection frustum (infinite projection)
		D3D_SetupProjection (d3d_RenderDef.fov_x, d3d_RenderDef.fov_y);
	}

	// calculate concatenated final matrix for use by shaders
	// because it's only needed once per frame instead of once per vertex we can save some vs instructions
	D3DMatrix_Multiply (&d3d_ModelViewProjMatrix, &d3d_ViewMatrix, &d3d_WorldMatrix);
	D3DMatrix_Multiply (&d3d_ModelViewProjMatrix, &d3d_ProjMatrix);

	// extract the frustum from it
	D3D_ExtractFrustum ();

	// we only need to clear if we're rendering 3D
	// we clear *before* we set the viewport as clearing a subrect of the rendertarget is slower
	// than just clearing the full thing - see IDirect3DDevice9::Clear in the SDK
	d3d_Device->Clear (0, NULL, d3d_ClearFlags, clearcolor, 1.0f, 1);

	// figure the sbar height at the active mode scale
	int sbheight = sb_lines;
	sbheight *= d3d_CurrentMode.Height;
	sbheight /= vid.height;

	// set up the scaled viewport taking account of the status bar area
	D3D_SetViewport (0, 0, d3d_CurrentMode.Width, d3d_CurrentMode.Height - sbheight, 0, 1);

	// depth testing and writing
	D3D_SetRenderState (D3DRS_ZFUNC, D3DCMP_LESSEQUAL);

	// backface culling
	D3D_BackfaceCull (D3DCULL_CCW);

	// ensure that our states are correct for this
	D3D_SetRenderState (D3DRS_ALPHATESTENABLE, FALSE);
	D3D_SetRenderState (D3DRS_ALPHABLENDENABLE, FALSE);
	D3D_SetRenderState (D3DRS_ZWRITEENABLE, TRUE);
	D3D_SetRenderState (D3DRS_ZENABLE, D3DZB_TRUE);

	// optionally enable wireframe mode
	if (r_wireframe.integer) D3D_SetRenderState (D3DRS_FILLMODE, D3DFILL_WIREFRAME);
}


void D3D_PrepareFog (void)
{
}


/*
========================================================================================

	BBB   L     OO    OO   DDD      AA   N  N  DDD     DDD   EEEE   AA  TTTTT  H  H
	B  B  L    O  O  O  O  D  D    A  A  NN N  D  D    D  D  E     A  A   T    H  H
	BBB   L    O  O  O  O  D  D    A  A  N NN  D  D    D  D  EEE   A  A   T    HHHH
	B  B  L    O  O  O  O  D  D    AAAA  N  N  D  D    D  D  E     AAAA   T    H  H
	BBB   LLLL  OO    OO   DDD     A  A  N  N  DDD     DDD   EEEE  A  A   T    H  H

========================================================================================
*/

cvar_t cl_deathfade ("cl_deathfade", 0.1f, CVAR_ARCHIVE);

void D3D_DeathBlend (void)
{
	// no death blend initially
	cl.cshifts[CSHIFT_DEATH].percent = 0;

	// don't draw it if in the automap or not connected
	if (d3d_RenderDef.automap) return;
	if (cls.state != ca_connected) return;

	// console is full screen
	if (scr_con_current == vid.height) return;

	// no death in intermission
	if (cl.intermission) return;

	// don't draw it in multiplayer as some people might like to take a good
	// look around after they die, and god knows there's all sorts of folks in the world
	if (cl.maxclients > 1) return;

	if (cl_deathfade.value > 0.0f)
	{
		static float deathred = 0;
		static float deathalpha = 0;
		static bool wasalive = true;

		// see if we're still alive
		if (cl.stats[STAT_HEALTH] > 0)
		{
			// we're still alive
			wasalive = true;
			return;
		}

		// ok, we're dead - see did we just die or have we been dead for a while?
		if (wasalive)
		{
			// init the color and blend
			deathalpha = 0;
			deathred = 0.666;

			// signal that we've been dead before
			wasalive = false;
		}

		// set up the death shift
		cl.cshifts[CSHIFT_DEATH].destcolor[0] = (deathred * 255.0f);
		cl.cshifts[CSHIFT_DEATH].destcolor[1] = 0;
		cl.cshifts[CSHIFT_DEATH].destcolor[2] = 0;
		cl.cshifts[CSHIFT_DEATH].percent = (deathalpha * 255.0f);

		// update blend (attempt to get close to DP's scale)
		deathalpha += (d3d_RenderDef.frametime * 10.0f * cl_deathfade.value) / 3.0f;
		deathred -= (d3d_RenderDef.frametime * 10.0f * cl_deathfade.value) / 5.0f;
	}
}


void D3D_UpdateContentsColor (void)
{
	if (d3d_RenderDef.viewleaf->contents == CONTENTS_EMPTY)
	{
		// brought to you today by "disgusting hacks 'r' us"...
		// stuffcmd must be the most EVIL thing ever invented, and as soon as QC 1337 h4x0rz start
		// doing things RIGHT a lot of good will come into the world.
		extern cshift_t cshift_empty;

		cl.cshifts[CSHIFT_CONTENTS].destcolor[0] = cshift_empty.destcolor[0];
		cl.cshifts[CSHIFT_CONTENTS].destcolor[1] = cshift_empty.destcolor[1];
		cl.cshifts[CSHIFT_CONTENTS].destcolor[2] = cshift_empty.destcolor[2];
		cl.cshifts[CSHIFT_CONTENTS].percent = cshift_empty.percent;
	}
	else if (d3d_RenderDef.viewleaf->contentscolor)
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
	else
	{
		// empty or undefined
		V_SetContentsColor (d3d_RenderDef.viewleaf->contents);
	}

	// add the death blend to the cshifts
	D3D_DeathBlend ();

	// now calc the blend
	V_CalcBlend ();
}


void R_SetupSpriteModels (void)
{
	if (!r_drawentities.integer) return;

	for (int i = 0; i < d3d_RenderDef.numvisedicts; i++)
	{
		entity_t *ent = d3d_RenderDef.visedicts[i];

		if (!ent->model) continue;
		if (ent->model->type != mod_sprite) continue;

		// sprites always have alpha
		D3DAlpha_AddToList (ent);
	}
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
		if (ent->frame) ent->frame = 1;
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


void R_SetupEntitiesOnList (void)
{
	// brush models include the world
	D3D_BuildWorld ();
	R_SetupSpriteModels ();

	// deferred until after the world so that we get statics on the list too
	for (int i = 0; i < d3d_RenderDef.numvisedicts; i++)
	{
		entity_t *ent = d3d_RenderDef.visedicts[i];

		if (!ent->model) continue;

		R_ModParanoia (ent);
	}
}


int r_speedstime = -1;


void D3D_AddExtraInlineModelsToListsForAutomap (void)
{
	if (!d3d_RenderDef.automap) return;

	for (int i = 1; i < MAX_MODELS; i++)
	{
		model_t *m = cl.model_precache[i];

		// end of the list
		if (!m) break;

		// no entity cached
		if (!m->cacheent) continue;

		// model was never seen by the player
		if (!m->wasseen) continue;

		// model was already added to the list
		if (m->cacheent->visframe == d3d_RenderDef.framecount) continue;

		// add it to the list and mark as visible
		m->cacheent->visframe = d3d_RenderDef.framecount;
		D3D_AddVisEdict (m->cacheent);
	}
}


/*
================
R_RenderView

r_refdef must be set before the first call
================
*/

cvar_t r_skyfog ("r_skyfog", 0.5f, CVAR_ARCHIVE);

void D3DRMain_HLSLSetup (void)
{
	extern float d3d_FogColor[];
	extern float d3d_FogDensity;
	extern cvar_t gl_fogenable;
	extern cvar_t gl_fogdensityscale;

	// basic params for drawing the world
	D3DHLSL_SetWorldMatrix (&d3d_ModelViewProjMatrix);
	D3DHLSL_SetFogMatrix (&d3d_ViewMatrix);

	D3DHLSL_SetFloat ("Overbright", d3d_OverbrightModulate);
	D3DHLSL_SetFloatArray ("r_origin", r_origin, 3);
	D3DHLSL_SetFloatArray ("viewangles", r_refdef.viewangles, 3);

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
			D3DHLSL_SetFloat ("FogDensity", (d3d_FogDensity / gl_fogdensityscale.value));
		else D3DHLSL_SetFloat ("FogDensity", (d3d_FogDensity / 100.0f));
	}
	else
	{
		if (d3d_FogDensity > 0 && r_skyfog.value > 0 && gl_fogenable.value)
			D3DHLSL_SetFloat ("SkyFog", 1.0f - r_skyfog.value);
		else D3DHLSL_SetFloat ("SkyFog", 1.0f);

		// fitz fog divides by 64
		if (gl_fogdensityscale.value > 0)
			D3DHLSL_SetFloat ("FogDensity", (d3d_FogDensity / gl_fogdensityscale.value));
		else D3DHLSL_SetFloat ("FogDensity", (d3d_FogDensity / 64.0f));
	}
}


void R_RenderView (DWORD dwTimePassed)
{
	// if (sv.active && sv.paused) Con_Printf ("server paused\n");

	// always pause in single player if in the console or the menus
	if (!sv.paused && (svs.maxclients > 1 || key_dest == key_game))
	{
		// keep timings steady
		d3d_RenderDef.oldtime = d3d_RenderDef.time;
		d3d_RenderDef.dwTime += dwTimePassed;
		d3d_RenderDef.time = (float) d3d_RenderDef.dwTime / 1000.0f;
	}

	// this needs to be updated here instead of in a callback otherwise it will go out of sync
	d3d_RenderDef.framecount++;

	DWORD dwTime1, dwTime2;

	if (!d3d_RenderDef.worldentity.model || !cl.worldmodel || !cl.worldmodel->brushhdr) return;

	if (r_norefresh.value)
	{
		d3d_Device->Clear (0, NULL, D3DCLEAR_TARGET, 0xff332200, 1.0f, 1);
		HUD_Changed ();
		return;
	}

	// get frametime (this may not be the same as timepassed; e.g. if we returned above)
	d3d_RenderDef.frametime = d3d_RenderDef.time - d3d_RenderDef.oldtime;

	if (r_speeds.value) dwTime1 = Sys_Milliseconds ();

	// initialize stuff
	R_SetupFrame ();
	D3DWarp_InitializeTurb ();
	D3D_UpdateContentsColor ();
	D3D_PrepareOverbright ();
	D3D_AddExtraInlineModelsToListsForAutomap ();

	// set up to render
	D3DMain_SetupD3D ();
	D3DRMain_HLSLSetup ();

	// setup everything we're going to draw
	R_SetupEntitiesOnList ();

	// draw our alias models
	D3DAlias_RenderAliasModels ();

	// draw all items on the alpha list
	D3DAlpha_RenderList ();

	// the viewmodel comes last
	// note - the gun model code assumes that it's the last thing drawn in the 3D view
	D3DAlias_DrawViewModel ();

	// ensure
	D3D_SetRenderState (D3DRS_FILLMODE, D3DFILL_SOLID);

	if (r_speeds.value)
	{
		dwTime2 = Sys_Milliseconds ();
		r_speedstime = dwTime2 - dwTime1;
	}
	else r_speedstime = -1;
}


