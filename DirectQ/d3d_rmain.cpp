/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
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
#include "d3d_quake.h"


void RotatePointAroundVector (vec3_t dst, const vec3_t dir, const vec3_t point, float degrees);
void R_AnimateLight (void);
void V_CalcBlend (void);
void D3D_DrawSkyChain (void);
void D3D_DrawWorld (void);
void D3D_PrepareWorld (void);
void D3D_PrepareParticles (void);
void D3D_DrawParticles (int flag);
void D3D_PrepareWaterSurfaces (void);
void D3D_DrawAlphaWaterSurfaces (void);
void D3D_DrawOpaqueWaterSurfaces (void);
void D3D_PrepareAliasModels (void);
void D3D_DrawOpaqueAliasModels (void);
void D3D_DrawViewModel (void);

bool R_DrawBrushModel (entity_t *e);
bool R_PrepareBrushEntity (entity_t *e);

void D3D_BeginUnderwaterWarp (void);
void D3D_EndUnderwaterWarp (void);
void D3D_AutomapReset (void);

// model drawing funcs
void D3D_DrawTranslucentAliasModel (entity_t *ent);
void D3D_DrawSpriteModel (entity_t *e);

void D3D_RenderFlashDlights (void);

extern DWORD d3d_ZbufferEnableFunction;

// render definition for this frame
d3d_renderdef_t d3d_RenderDef;

vec3_t		modelorg, r_entorigin;

mplane_t	frustum[5];

//
// view origin
//
vec3_t	vup;
vec3_t	vpn;
vec3_t	vright;
vec3_t	r_origin;

//
// screen size info
//
refdef_t	r_refdef;

texture_t	*r_notexture_mip;

int		d_lightstylevalue[256];	// 8.8 fraction of base light value


void R_MarkLeaves (void);

cvar_t	r_norefresh ("r_norefresh","0");
cvar_t	r_drawentities ("r_drawentities","1");
cvar_t	r_drawviewmodel ("r_drawviewmodel","1");
cvar_t	r_speeds ("r_speeds","0");
cvar_t	r_fullbright ("r_fullbright","0");
cvar_t	r_lightmap ("r_lightmap","0");
cvar_t	r_shadows ("r_shadows","0");
cvar_t	r_wateralpha ("r_wateralpha", 1.0f, CVAR_ARCHIVE);
cvar_t	r_dynamic ("r_dynamic","1");
cvar_t	r_novis ("r_novis","0");

cvar_t	gl_finish ("gl_finish","0");
cvar_t	gl_clear ("gl_clear","1", CVAR_ARCHIVE);
cvar_t	gl_cull ("gl_cull","1");
cvar_t	gl_smoothmodels ("gl_smoothmodels","1");
cvar_t	gl_affinemodels ("gl_affinemodels","0");
cvar_t	gl_polyblend ("gl_polyblend","1");
cvar_t	gl_nocolors ("gl_nocolors","0");
cvar_t	gl_doubleeyes ("gl_doubleeys", "1");

// fog
cvar_t gl_fogenable ("gl_fogenable", 0.0f, CVAR_ARCHIVE);
cvar_t gl_fogsky ("gl_fogsky", 1.0f, CVAR_ARCHIVE);
cvar_t gl_fogred ("gl_fogred", 0.85f, CVAR_ARCHIVE);
cvar_t gl_foggreen ("gl_foggreen", 0.55f, CVAR_ARCHIVE);
cvar_t gl_fogblue ("gl_fogblue", 0.3f, CVAR_ARCHIVE);
cvar_t gl_fogstart ("gl_fogstart", 10.0f, CVAR_ARCHIVE);
cvar_t gl_fogend ("gl_fogend", 2048.0f, CVAR_ARCHIVE);
cvar_t gl_fogdensity ("gl_fogdensity", 0.001f, CVAR_ARCHIVE);
cvar_t gl_underwaterfog ("gl_underwaterfog", 1, CVAR_ARCHIVE);
cvar_t r_lockfrustum ("r_lockfrustum", 0.0f);
cvar_t r_normqrain ("r_normqrain", 0.0f, CVAR_ARCHIVE);
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
	if (emaxs[0] < frustum[0].dist) return true;
	if (emins[0] > frustum[1].dist) return true;
	if (emaxs[1] < frustum[2].dist) return true;
	if (emins[1] > frustum[3].dist) return true;

	// not culled
	automap_culls--;
	return false;
}


// fixed func drawing
void D3D_DrawPrimitive (D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount, CONST void *pVertexStreamZeroData, UINT VertexStreamZeroStride)
{
	D3D_CheckDirtyMatrixes ();
	d3d_Device->DrawPrimitiveUP (PrimitiveType, PrimitiveCount, pVertexStreamZeroData, VertexStreamZeroStride);
}


void D3D_DrawPrimitive (D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount)
{
	D3D_CheckDirtyMatrixes ();
	d3d_Device->DrawPrimitive (PrimitiveType, StartVertex, PrimitiveCount);
}


void D3D_DrawPrimitive (D3DPRIMITIVETYPE PrimitiveType, INT BaseVertexIndex, UINT MinIndex, UINT NumVertices, UINT StartIndex, UINT PrimitiveCount)
{
	D3D_CheckDirtyMatrixes ();
	d3d_Device->DrawIndexedPrimitive (PrimitiveType, BaseVertexIndex, MinIndex, NumVertices, StartIndex, PrimitiveCount);
}


void D3D_DrawPrimitive (D3DPRIMITIVETYPE PrimitiveType, UINT MinVertexIndex, UINT NumVertices, UINT PrimitiveCount, const void *pIndexData, D3DFORMAT IndexDataFormat, CONST void* pVertexStreamZeroData, UINT VertexStreamZeroStride)
{
	D3D_CheckDirtyMatrixes ();

	d3d_Device->DrawIndexedPrimitiveUP
	(
		PrimitiveType,
		MinVertexIndex,
		NumVertices,
		PrimitiveCount,
		pIndexData,
		IndexDataFormat,
		pVertexStreamZeroData,
		VertexStreamZeroStride
	);
}


void D3D_BeginVisedicts (void)
{
	d3d_RenderDef.numvisedicts = 0;
	d3d_RenderDef.numtransedicts = 0;
}


void D3D_AddVisEdict (entity_t *ent, bool noculledict)
{
	// set here because cl_main doesn't know what a D3DXMATRIX is.
	// also cleaner because i only need to check and set in one place rather than every place an entity is alloced
	// finally, we do it in this function so that we can be certain that every entity will have a matrix
	if (!ent->matrix) ent->matrix = (D3DXMATRIX *) Pool_Alloc (POOL_MAP, sizeof (D3DXMATRIX));

	// check for entities with no models
	if (!ent->model) return;

	// only add entities with supported model types
	if (ent->model->type == mod_alias || ent->model->type == mod_brush || ent->model->type == mod_sprite)
	{
		// tough fucking shit
		if ((ent->model->flags & EF_RMQRAIN) && r_normqrain.integer) return;

		ent->nocullbox = noculledict;

		// sprites always have translucency as they need to be drawn with alpha and no depthwriting
		if (ent->alphaval < 255 || ent->model->type == mod_sprite)
		{
			if (d3d_RenderDef.numtransedicts < MAX_VISEDICTS)
			{
				d3d_RenderDef.transedicts[d3d_RenderDef.numtransedicts] = ent;
				d3d_RenderDef.numtransedicts++;
			}
		}
		else
		{
			if (d3d_RenderDef.numvisedicts < MAX_VISEDICTS)
			{
				d3d_RenderDef.visedicts[d3d_RenderDef.numvisedicts] = ent;
				d3d_RenderDef.numvisedicts++;
			}
		}
	}
	else Con_DPrintf ("D3D_AddVisEdict: Unknown entity model type for %s\n", ent->model->name);
}


/*
=================
R_CullBox

Returns true if the box is completely outside the frustum
Only checks the sides; meaning that there is optimization scope (check behind)
=================
*/
bool R_CullBox (vec3_t emins, vec3_t emaxs)
{
	int i;
	mplane_t *p;

	// different culling for the automap
	if (d3d_RenderDef.automap) return R_AutomapCull (emins, emaxs);

	// test order is left/right/front/bottom/top
	for (i = 0; i < 5; i++)
	{
		p = frustum + i;

		switch (p->signbits)
		{
			// signbits signify which corner of the bbox is nearer the plane
			// note - as only one point/dist test is done per bbox, there is NO advantage to using a bounding sphere
		default:
		case 0:
			if (p->normal[0] * emaxs[0] + p->normal[1] * emaxs[1] + p->normal[2] * emaxs[2] < p->dist)
				return true;
			break;

		case 1:
			if (p->normal[0] * emins[0] + p->normal[1] * emaxs[1] + p->normal[2] * emaxs[2] < p->dist)
				return true;
			break;

		case 2:
			if (p->normal[0] * emaxs[0] + p->normal[1] * emins[1] + p->normal[2] * emaxs[2] < p->dist)
				return true;
			break;

		case 3:
			if (p->normal[0] * emins[0] + p->normal[1] * emins[1] + p->normal[2] * emaxs[2] < p->dist)
				return true;
			break;

		case 4:
			if (p->normal[0] * emaxs[0] + p->normal[1] * emaxs[1] + p->normal[2] * emins[2] < p->dist)
				return true;
			break;

		case 5:
			if (p->normal[0] * emins[0] + p->normal[1] * emaxs[1] + p->normal[2] * emins[2] < p->dist)
				return true;
			break;

		case 6:
			if (p->normal[0] * emaxs[0] + p->normal[1] * emins[1] + p->normal[2] * emins[2] < p->dist)
				return true;
			break;

		case 7:
			if (p->normal[0] * emins[0] + p->normal[1] * emins[1] + p->normal[2] * emins[2] < p->dist)
				return true;
			break;
		}
	}

	return false;
}


void D3D_RotateForEntity (entity_t *e)
{
	// calculate transforms
	d3d_WorldMatrixStack->Translate (e->origin[0], e->origin[1], e->origin[2]);
	d3d_WorldMatrixStack->Rotate (0, 0, 1, e->angles[1]);
	d3d_WorldMatrixStack->Rotate (0, 1, 0, -e->angles[0]);
	d3d_WorldMatrixStack->Rotate (1, 0, 0, e->angles[2]);
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
TurnVector -- johnfitz

turn forward towards side on the plane defined by forward and side
if angle = 90, the result will be equal to side
assumes side and forward are perpendicular, and normalized
to turn away from side, use a negative angle
===============
*/
#define DEG2RAD(a) ((a) * (M_PI / 180.0f))

void TurnVector (vec3_t out, const vec3_t forward, const vec3_t side, float angle)
{
	float scale_forward, scale_side;

	scale_forward = cos (DEG2RAD (angle));
	scale_side = sin (DEG2RAD (angle));

	out[0] = scale_forward * forward[0] + scale_side * side[0];
	out[1] = scale_forward * forward[1] + scale_side * side[1];
	out[2] = scale_forward * forward[2] + scale_side * side[2];
}


void R_SetFrustum (void)
{
	int		i;

	// retain the old frustum unless we're in the first few frames in which case we want one to be done
	// as a baseline
	if (r_lockfrustum.integer && d3d_RenderDef.framecount > 5) return;

	if (d3d_RenderDef.automap)
	{
		// scale and translate to automap space for R_AutomapCull
		frustum[0].dist = r_automap_x - ((vid.width / 2) * r_automap_scale);
		frustum[1].dist = r_automap_x + ((vid.width / 2) * r_automap_scale);
		frustum[2].dist = r_automap_y - ((vid.height / 2) * r_automap_scale);
		frustum[3].dist = r_automap_y + ((vid.height / 2) * r_automap_scale);
		return;
	}

	// frustum cull check order is left/right/front/bottom/top
	TurnVector (frustum[0].normal, vpn, vright, r_refdef.fov_x / 2 - 90);  // left plane
	TurnVector (frustum[1].normal, vpn, vright, 90 - r_refdef.fov_x / 2);  // right plane
	TurnVector (frustum[3].normal, vpn, vup, 90 - r_refdef.fov_y / 2);  // bottom plane
	TurnVector (frustum[4].normal, vpn, vup, r_refdef.fov_y / 2 - 90);  // top plane

	// also build a front clipping plane to cull objects behind the view
	// this won't be much as the other 4 planes will intersect a little bit behind, but it is effective in some
	// situations and does provide a minor boost
	frustum[2].normal[0] = (frustum[0].normal[0] + frustum[1].normal[0] + frustum[3].normal[0] + frustum[4].normal[0]) / 4.0f;
	frustum[2].normal[1] = (frustum[0].normal[1] + frustum[1].normal[1] + frustum[3].normal[1] + frustum[4].normal[1]) / 4.0f;
	frustum[2].normal[2] = (frustum[0].normal[2] + frustum[1].normal[2] + frustum[3].normal[2] + frustum[4].normal[2]) / 4.0f;

	for (i = 0; i < 5; i++)
	{
		// is this necessary???
		VectorNormalize (frustum[i].normal);

		frustum[i].type = PLANE_ANYZ;
		frustum[i].dist = DotProduct (r_origin, frustum[i].normal);
		frustum[i].signbits = SignbitsForPlane (&frustum[i]);
	}
}


/*
===============
R_SetupFrame
===============
*/
void R_SetupFrame (void)
{
	// don't allow cheats in multiplayer
	if (cl.maxclients > 1) Cvar_Set ("r_fullbright", "0");

	R_AnimateLight ();

	d3d_RenderDef.framecount++;

	// current viewleaf
	d3d_RenderDef.oldviewleaf = d3d_RenderDef.viewleaf;
	d3d_RenderDef.viewleaf = Mod_PointInLeaf (r_refdef.vieworg, cl.worldmodel);

	VectorCopy (r_refdef.vieworg, r_origin);
	AngleVectors (r_refdef.viewangles, vpn, vright, vup);
}


/*
=============
D3D_PrepareRender
=============
*/
void D3D_PrepareRender (void)
{
	// always clear the zbuffer
	DWORD d3d_ClearFlags = D3DCLEAR_ZBUFFER;
	D3DVIEWPORT9 d3d_3DViewport;

	// accumulate everything else we want to clear
	// fixme - don't clear if we're doing a viewleaf contents transition
	if (gl_clear.value) d3d_ClearFlags |= D3DCLEAR_TARGET;

	// if we have a stencil buffer make sure we clear that too otherwise perf will SUFFER
	if (d3d_GlobalCaps.DepthStencilFormat == D3DFMT_D24S8) d3d_ClearFlags |= D3DCLEAR_STENCIL;

	// get dimensions of viewport
	// we do this here so that we can set up the clear rectangle properly
	d3d_3DViewport.X = r_refdef.vrect.x * d3d_CurrentMode.Width / vid.width;
	d3d_3DViewport.Width = r_refdef.vrect.width * d3d_CurrentMode.Width / vid.width;
	d3d_3DViewport.Y = r_refdef.vrect.y * d3d_CurrentMode.Height / vid.height;
	d3d_3DViewport.Height = r_refdef.vrect.height * d3d_CurrentMode.Height / vid.height;

	// adjust for rounding errors by expanding the viewport by 1 in each direction
	// if it's smaller than the screen size
	if (d3d_3DViewport.X > 0) d3d_3DViewport.X--;
	if (d3d_3DViewport.Y > 0) d3d_3DViewport.Y--;
	if (d3d_3DViewport.Width < d3d_CurrentMode.Width) d3d_3DViewport.Width++;
	if (d3d_3DViewport.Height < d3d_CurrentMode.Height) d3d_3DViewport.Height++;

	// set z range
	d3d_3DViewport.MinZ = 0.0f;
	d3d_3DViewport.MaxZ = 1.0f;

	// set the viewport
	d3d_Device->SetViewport (&d3d_3DViewport);

	d3d_ViewMatrixStack->Reset ();
	d3d_ViewMatrixStack->LoadIdentity ();

	d3d_ProjMatrixStack->Reset ();
	d3d_ProjMatrixStack->LoadIdentity ();

	d3d_WorldMatrixStack->Reset ();
	d3d_WorldMatrixStack->LoadIdentity ();

	DWORD clearcolor = 0xff000000;

	// match winquake's grey if we're noclipping
	if (d3d_RenderDef.viewleaf->contents == CONTENTS_SOLID) clearcolor = 0xff242424;

	if (r_wireframe.integer)
	{
		d3d_ClearFlags |= D3DCLEAR_TARGET;
		clearcolor = 0xff242424;
	}
	else if (d3d_RenderDef.oldviewleaf)
	{
		if (d3d_RenderDef.viewleaf->contents != d3d_RenderDef.oldviewleaf->contents)
			d3d_ClearFlags &= ~D3DCLEAR_TARGET;
	}

	// projection matrix depends on drawmode
	if (d3d_RenderDef.automap)
	{
		// change clear color to black and force a clear
		clearcolor = 0xff000000;
		d3d_ClearFlags |= D3DCLEAR_TARGET;

		// nothing has been culled yet
		automap_culls = 0;

		float maxdepth = cl.worldmodel->maxs[2];
		if (fabs (cl.worldmodel->mins[2]) > maxdepth) maxdepth = fabs (cl.worldmodel->mins[2]);
		maxdepth += 100;

		// set a near clipping plane based on the refdef vieworg
		// here we retain the same space as the world coords
		d3d_ProjMatrixStack->Ortho2D (0, vid.width * r_automap_scale, 0, vid.height * r_automap_scale, -maxdepth, maxdepth);

		// translate camera by origin
		d3d_ViewMatrixStack->Translate
		(
			-(r_automap_x - (vid.width / 2) * r_automap_scale),
			-(r_automap_y - (vid.height / 2) * r_automap_scale),
			-r_refdef.vieworg[2]
		);
	}
	else
	{
		// standard perspective; this is converted to infinite in the member function
		d3d_ProjMatrixStack->Perspective3D (r_refdef.fov_y, (float) r_refdef.vrect.width / (float) r_refdef.vrect.height, 4, 4096);

		// put z going up
		d3d_ViewMatrixStack->Rotate (1, 0, 0, -90);
		d3d_ViewMatrixStack->Rotate (0, 0, 1, 90);

		// rotate camera by angles
		d3d_ViewMatrixStack->Rotate (1, 0, 0, -r_refdef.viewangles[2]);
		d3d_ViewMatrixStack->Rotate (0, 1, 0, -r_refdef.viewangles[0]);
		d3d_ViewMatrixStack->Rotate (0, 0, 1, -r_refdef.viewangles[1]);

		// translate camera by origin
		d3d_ViewMatrixStack->Translate (-r_refdef.vieworg[0], -r_refdef.vieworg[1], -r_refdef.vieworg[2]);
	}

	// we only need to clear if we're rendering 3D
	d3d_Device->Clear (0, NULL, d3d_ClearFlags, clearcolor, 1.0f, 0);

	// depth testing and writing
	D3D_SetRenderState (D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	D3D_SetRenderState (D3DRS_ZENABLE, d3d_ZbufferEnableFunction);

	// turn off smooth shading
	D3D_SetRenderState (D3DRS_SHADEMODE, D3DSHADE_FLAT);

	// backface culling
	D3D_BackfaceCull (D3DCULL_CCW);

	// disable all alpha ops
	D3D_SetRenderState (D3DRS_ALPHATESTENABLE, FALSE);
	D3D_DisableAlphaBlend ();

	texture_t *t;

	for (int i = 0; i < cl.worldbrush->numtextures; i++)
	{
		// e2m3 gets this
		if (!(t = cl.worldbrush->textures[i])) continue;

		// null it
		t->texturechain = NULL;
		t->chaintail = NULL;
		t->visframe = -1;
	}

	// renderflags
	d3d_RenderDef.renderflags = 0;

	// surface drawing chains
	d3d_RenderDef.skychain = NULL;

	// poly counts for r_speeds 1
	d3d_RenderDef.brush_polys = 0;
	d3d_RenderDef.alias_polys = 0;
}


// this is basically the "a lof of this goes away" thing in the old gl_refrag...
// or at least one version of it.  see also CL_FindTouchedLeafs and the various struct defs
void R_AddStaticEntitiesForLeaf (mleaf_t *leaf)
{
	for (staticent_t *se = leaf->statics; se; se = se->next)
	{
		// static entity was removed
		if (se->ent->staticremoved) continue;

		// already added
		if (se->ent->visframe == d3d_RenderDef.framecount) continue;

		// the leafs containing the entities have already been bbox culled, so there's no need to check the entity!!!
		D3D_AddVisEdict (se->ent, true);

		// mark as visible for this frame
		se->ent->visframe = d3d_RenderDef.framecount;
	}
}


void D3D_PrepareVisedicts (void)
{
	// merge entities into d3d_RenderDef.renderflags
	// so that we know in advance what we need to render
	for (int i = 0; i < d3d_RenderDef.numvisedicts; i++)
	{
		entity_t *ent = d3d_RenderDef.visedicts[i];

		if (ent->model->type == mod_alias)
			d3d_RenderDef.renderflags |= R_RENDERALIAS;
		else if (ent->model->type == mod_sprite)
			d3d_RenderDef.renderflags |= R_RENDERSPRITE;
		else if (ent->model->type == mod_brush)
		{
			// two different types of brush model - yuck yuck yuck
			if (ent->model->name[0] == '*')
				d3d_RenderDef.renderflags |= R_RENDERINLINEBRUSH;
			else d3d_RenderDef.renderflags |= R_RENDERINSTANCEDBRUSH;
		}
	}

	// prepare individual entity types for rendering
	D3D_PrepareAliasModels ();
}


void D3D_PerpareFog (void)
{
	// no fog
	if (!gl_fogenable.integer) return;
	if (d3d_RenderDef.automap) return;

	// sanity check cvars
	if (gl_fogstart.value < 10) Cvar_Set (&gl_fogstart, 10.0f);
	if (gl_fogend.value < 10) Cvar_Set (&gl_fogend, 10.0f);

	if (gl_fogdensity.value < 0) Cvar_Set (&gl_fogdensity, 0.0f);
	if (gl_fogdensity.value > 0.01f) Cvar_Set (&gl_fogdensity, 0.01f);

	if (gl_fogend.value < gl_fogstart.value)
	{
		Cvar_Set (&gl_fogstart, 10.0f);
		Cvar_Set (&gl_fogend, 2048.0f);
	}

	D3D_SetRenderState (D3DRS_FOGENABLE, TRUE);

	DWORD fogcolor = D3DCOLOR_XRGB
	(
		BYTE_CLAMP (gl_fogred.value * 255),
		BYTE_CLAMP (gl_foggreen.value * 255),
		BYTE_CLAMP (gl_fogblue.value * 255)
	);

	D3D_SetRenderState (D3DRS_FOGCOLOR, fogcolor);

	DWORD fvm = D3DFOG_LINEAR;
	DWORD fpm = D3DFOG_NONE;

	switch (gl_fogenable.integer)
	{
	case 2:
		fvm = D3DFOG_EXP;
		fpm = D3DFOG_NONE;
		break;

	case 3:
		fvm = D3DFOG_EXP2;
		fpm = D3DFOG_NONE;
		break;

	case 4:
		fvm = D3DFOG_NONE;
		fpm = D3DFOG_LINEAR;
		break;

	case 5:
		fvm = D3DFOG_NONE;
		fpm = D3DFOG_EXP;
		break;

	case 6:
		fvm = D3DFOG_NONE;
		fpm = D3DFOG_EXP2;
		break;

	default:
		break;
	}

	D3D_SetRenderState (D3DRS_FOGVERTEXMODE, fvm);
	D3D_SetRenderState (D3DRS_FOGTABLEMODE, fpm);
	D3D_SetRenderStatef (D3DRS_FOGDENSITY, gl_fogdensity.value);
	D3D_SetRenderStatef (D3DRS_FOGSTART, gl_fogstart.value);
	D3D_SetRenderStatef (D3DRS_FOGEND, gl_fogend.value);
}


void D3D_UpdateContentsColor (void)
{
	if (d3d_RenderDef.viewleaf->contents == CONTENTS_EMPTY)
	{
		// same as cshift_empty
		cl.cshifts[CSHIFT_CONTENTS].destcolor[0] = 130;
		cl.cshifts[CSHIFT_CONTENTS].destcolor[1] = 80;
		cl.cshifts[CSHIFT_CONTENTS].destcolor[2] = 50;
		cl.cshifts[CSHIFT_CONTENTS].percent = 0;
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

	// now calc the blend
	V_CalcBlend ();
}


int D3D_EntityDepthCompare (const void *a, const void *b)
{
	entity_t *enta = *((entity_t **) a);
	entity_t *entb = *((entity_t **) b);

	// sort back to front
	if (enta->dist > entb->dist)
		return 1;
	else if (enta->dist < entb->dist)
		return -1;
	else return 0;
}


void D3D_DrawTranslucentEntities (void)
{
	if (!r_drawentities.value) return;
	if (!d3d_RenderDef.numtransedicts) return;

	// if there's only one then the list is already sorted!
	if (d3d_RenderDef.numtransedicts > 1)
	{
		// evaluate distances
		for (int i = 0; i < d3d_RenderDef.numtransedicts; i++)
		{
			entity_t *ent = d3d_RenderDef.transedicts[i];

			// set distance from viewer - no need to sqrt them as the order will be the same
			// (fixme - should we, and then subtract a radius?)
			ent->dist = (ent->origin[0] - r_origin[0]) * (ent->origin[0] - r_origin[0]) +
				(ent->origin[1] - r_origin[1]) * (ent->origin[1] - r_origin[1]) +
				(ent->origin[2] - r_origin[2]) * (ent->origin[2] - r_origin[2]);
		}

		if (d3d_RenderDef.numtransedicts == 2)
		{
			// trivial case - 2 entities
			if (d3d_RenderDef.transedicts[0]->dist < d3d_RenderDef.transedicts[1]->dist)
			{
				// reorder correctly
				entity_t *tmp = d3d_RenderDef.transedicts[1];
				d3d_RenderDef.transedicts[1] = d3d_RenderDef.transedicts[0];
				d3d_RenderDef.transedicts[0] = tmp;
			}
		}
		else
		{
			// general case - depth sort the transedicts from back to front
			qsort ((void *) d3d_RenderDef.transedicts, d3d_RenderDef.numtransedicts, sizeof (entity_t *), D3D_EntityDepthCompare);
		}
	}

	// now draw 'em
	// we can't state-batch these as they need correct ordering so we'll just live with the state changes...
	// sprites can have the same z so don't write it
	for (int i = 0; i < d3d_RenderDef.numtransedicts; i++)
	{
		d3d_RenderDef.currententity = d3d_RenderDef.transedicts[i];

		if (d3d_RenderDef.currententity->model->type == mod_sprite)
		{
			D3D_EnableAlphaBlend (D3DBLENDOP_ADD, D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA);
			D3D_DrawSpriteModel (d3d_RenderDef.currententity);
		}
		else if (d3d_RenderDef.currententity->model->type == mod_alias)
		{
			D3D_EnableAlphaBlend (D3DBLENDOP_ADD, D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA, false);
			D3D_DrawTranslucentAliasModel (d3d_RenderDef.currententity);
		}
		else if (d3d_RenderDef.currententity->model->type == mod_brush)
		{
			D3D_EnableAlphaBlend (D3DBLENDOP_ADD, D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA, false);
			R_DrawBrushModel (d3d_RenderDef.currententity);
		}
		else
		{
			// just print a warning
			Con_DPrintf
			(
				"Unknown model type %i for %s\n",
				d3d_RenderDef.currententity->model->type,
				d3d_RenderDef.currententity->model->name
			);
		}
	}

	// take down global state
	D3D_DisableAlphaBlend ();
}


void R_PrepareEntitiesOnList (entity_t **list, int count, int type, entityfunc_t prepfunc)
{
	for (int i = 0; i < count; i++)
	{
		entity_t *e = list[i];

		// the entity is not visible until we say so!
		e->visframe = -1;

		// these aren't the models you're looking for
		if (!e->model) continue;
		if (e->model->type != type) continue;

		// update the cache even if the model is culled
		if (e->model->cacheent)
		{
			// if we're caching models just store out it's last status
			// so that the automap can at least draw it at the position/orientation/etc it was
			// the last time that the player saw it.  this is only done for brush models as
			// other entities share models.
			entity_t *e2 = e->model->cacheent;
			// memcpy (e2, e, sizeof (entity_t));

			// we don't actually need the full contents of the entity_t struct here
			VectorCopy (e->angles, e2->angles);
			VectorCopy (e->origin, e2->origin);
			VectorCopy (e->shadelight, e2->shadelight);

			e2->alphaval = e->alphaval;
			e2->matrix = e->matrix;
			e2->model = e->model;
			e2->frame = e->frame;
			e2->nocullbox = e->nocullbox;

			e2->visframe = d3d_RenderDef.framecount;

			// set the cached ent back to itself (ensure)
			e2->model->cacheent = e2;
		}

		// prep the entity for drawing
		if (!prepfunc (e)) continue;

		// the entity is visible now
		e->visframe = d3d_RenderDef.framecount;
		e->model->wasseen = true;

		// merge renderflags
		if (e->model->type == mod_alias)
			d3d_RenderDef.renderflags |= R_RENDERALIAS;
		else if (e->model->type == mod_sprite)
			d3d_RenderDef.renderflags |= R_RENDERSPRITE;
		else if (e->model->type == mod_brush)
		{
			// two different types of brush model - yuck yuck yuck
			if (e->model->name[0] == '*')
				d3d_RenderDef.renderflags |= R_RENDERINLINEBRUSH;
			else d3d_RenderDef.renderflags |= R_RENDERINSTANCEDBRUSH;
		}
	}
}


void R_DrawEntitiesOnList (entity_t **list, int count, int type, entityfunc_t drawfunc)
{
	for (int i = 0; i < count; i++)
	{
		entity_t *e = list[i];

		// these aren't the models you're looking for
		if (e->visframe != d3d_RenderDef.framecount) continue;
		if (!e->model) continue;
		if (e->model->type != type) continue;

		// draw the model
		drawfunc (e);
	}
}


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
		D3D_AddVisEdict (m->cacheent, false);
	}
}


/*
================
R_RenderScene

r_refdef must be set before the first call

FIXMEEEEEEEEEE - bring all entity drawing into the new framework
================
*/
void R_RenderScene (void)
{
	// set up to draw
	R_SetupFrame ();
	R_SetFrustum ();

	// done here so we know if we're in water
	R_MarkLeaves ();

	// commence to draw (set up the underwater warp if required)
	if (!d3d_RenderDef.automap) D3D_BeginUnderwaterWarp ();

	// setup to draw
	D3D_PrepareRender ();

	if (r_wireframe.integer)
		D3D_SetRenderState (D3DRS_FILLMODE, D3DFILL_WIREFRAME);
	else D3D_SetRenderState (D3DRS_FILLMODE, D3DFILL_SOLID);

	// add the world and any static entities to the draw lists
	D3D_PrepareWorld ();

	// prepare brush models here - so that any affected lightmaps can be uploaded in batch below
	R_PrepareEntitiesOnList (d3d_RenderDef.visedicts, d3d_RenderDef.numvisedicts, mod_brush, R_PrepareBrushEntity);
	R_PrepareEntitiesOnList (d3d_RenderDef.transedicts, d3d_RenderDef.numtransedicts, mod_brush, R_PrepareBrushEntity);

	// the automap needs to add extra inline models
	D3D_AddExtraInlineModelsToListsForAutomap ();

	// upload all lightmaps that were modified
	D3D_UploadModifiedLightmaps ();

	// setup the visedicts for drawing; done after the world so that it will include static entities
	D3D_PrepareVisedicts ();

	// setup the water warp (global stuff for water render)
	D3D_PrepareWaterSurfaces ();

	// prepare and sort particles
	D3D_PrepareParticles ();

	// contents colours are deferred to here as they may be dependent on what was drawn/seen
	D3D_UpdateContentsColor ();

	// invert the sky/fog order if we're fogging sky or not
	if (gl_fogsky.integer)
	{
		// set up fog
		D3D_PerpareFog ();

		// draw sky first, otherwise the z-fail technique will flood the framebuffer with sky
		// rather than just drawing it where skybrushes appear
		D3D_DrawSkyChain ();
	}
	else
	{
		// draw sky first, otherwise the z-fail technique will flood the framebuffer with sky
		// rather than just drawing it where skybrushes appear
		D3D_DrawSkyChain ();

		// set up fog
		D3D_PerpareFog ();
	}

	// the world is done first so that we have a full valid depth buffer for clipping everything else against
	D3D_DrawWorld ();

	// don't let sound get messed up if going slow
	S_ExtraUpdate ();

	// draw opaque entities
	R_DrawEntitiesOnList (d3d_RenderDef.visedicts, d3d_RenderDef.numvisedicts, mod_brush, R_DrawBrushModel);
	D3D_DrawOpaqueAliasModels ();

	// draw opaque water surfs here so that z writing doesn't screw up
	D3D_DrawOpaqueWaterSurfaces ();

	// draw particle batch for contents opposite the viewleaf contents
	if (d3d_RenderDef.viewleaf->contents == CONTENTS_EMPTY)
		D3D_DrawParticles (R_RENDERWATERPARTICLE);
	else D3D_DrawParticles (R_RENDEREMPTYPARTICLE);

	// draw alpha surfaces (fixme - these will have to integrate with entities and be depth sorted too...)
	D3D_DrawAlphaWaterSurfaces ();

	// draw alpha entities (sprites always have alpha)
	D3D_DrawTranslucentEntities ();

	// dynamic lights flash rendering
	D3D_RenderFlashDlights ();

	// draw particle batch for contents same as the viewleaf contents
	if (d3d_RenderDef.viewleaf->contents == CONTENTS_EMPTY)
		D3D_DrawParticles (R_RENDEREMPTYPARTICLE);
	else D3D_DrawParticles (R_RENDERWATERPARTICLE);

	// the viewmodel is always drawn last and frontmost in the scene
	D3D_DrawViewModel ();

	// back to solid mode
	D3D_SetRenderState (D3DRS_FILLMODE, D3DFILL_SOLID);

	// finish up
	if (!d3d_RenderDef.automap)
		D3D_EndUnderwaterWarp ();
	else D3D_AutomapReset ();

	// disable fog (always)
	D3D_SetRenderState (D3DRS_FOGENABLE, FALSE);
}


/*
================
R_RenderView

r_refdef must be set before the first call
================
*/
void R_RenderView (void)
{
	double	time1, time2;

	if (r_norefresh.value) return;
	if (!d3d_RenderDef.worldentity.model || !cl.worldmodel || !cl.worldbrush) return;

	// always ensure this
	cl.worldbrush->brushtype = MOD_BRUSH_WORLD;

	static float old_frametime = 0;

	// get frametime - this was cl.oldtime, creating a framerate dependency
	d3d_RenderDef.frametime = cl.time - old_frametime;
	old_frametime = cl.time;

	if (r_speeds.value)
		time1 = Sys_FloatTime ();

	// render normal view
	R_RenderScene ();

	if (r_speeds.value)
	{
		time2 = Sys_FloatTime ();
		Con_Printf
		(
			"%3i ms  %4i wpoly %4i epoly\n",
			(int) ((time2 - time1) * 1000),
			d3d_RenderDef.brush_polys,
			d3d_RenderDef.alias_polys
		);
	}
}
