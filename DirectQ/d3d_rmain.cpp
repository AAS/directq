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
#include "d3d_hlsl.h"

void RotatePointAroundVector (vec3_t dst, const vec3_t dir, const vec3_t point, float degrees);
void R_AnimateLight (void);
void V_CalcBlend (void);
void R_DrawWorld (void);
void R_DrawParticles (void);
void R_DrawWaterSurfaces (void);
void R_DrawSpriteModel (entity_t *e);
void D3D_DrawAliasModels (void);
void D3D_DrawInstancedBrushModels (void);
void D3D_DrawSpriteModels (void);

void D3D_BeginUnderwaterWarp (void);
void D3D_EndUnderwaterWarp (void);

entity_t	r_worldentity;
float r_frametime;

bool	r_cache_thrash;		// compatability

vec3_t		modelorg, r_entorigin;
entity_t	*currententity;

int			r_visframecount;	// bumped when going to a new PVS
int			r_framecount;		// used for dlight push checking (and surf visibility)

mplane_t	frustum[4];

int			c_brush_polys, c_alias_polys;

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

mleaf_t		*r_viewleaf, *r_oldviewleaf;

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
cvar_t	r_wateralpha ("r_wateralpha", 0.5, CVAR_ARCHIVE);
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


/*
=================
R_CullBox

Returns true if the box is completely outside the frustom
=================
*/
bool R_CullBox (vec3_t mins, vec3_t maxs)
{
	int		i;

	for (i=0 ; i<4 ; i++)
		if (BoxOnPlaneSide (mins, maxs, &frustum[i]) == 2)
			return true;
	return false;
}


void D3D_RotateForEntity (entity_t *e, bool shadow = false)
{
	// calculate transforms
	d3d_WorldMatrixStack->TranslateLocal (e->origin[0], e->origin[1], e->origin[2]);

	d3d_WorldMatrixStack->RotateAxisLocal (&ZVECTOR, D3DXToRadian (e->angles[1]));

	if (!shadow)
	{
		d3d_WorldMatrixStack->RotateAxisLocal (&YVECTOR, D3DXToRadian (-e->angles[0]));
		d3d_WorldMatrixStack->RotateAxisLocal (&XVECTOR, D3DXToRadian (e->angles[2]));
	}
}


//==================================================================================

int SignbitsForPlane (mplane_t *out)
{
	int	bits, j;

	// for fast box on planeside test

	bits = 0;
	for (j=0 ; j<3 ; j++)
	{
		if (out->normal[j] < 0)
			bits |= 1<<j;
	}
	return bits;
}


void R_SetFrustum (void)
{
	int		i;

	if (r_refdef.fov_x == 90) 
	{
		// front side is visible
		VectorAdd (vpn, vright, frustum[0].normal);
		VectorSubtract (vpn, vright, frustum[1].normal);

		VectorAdd (vpn, vup, frustum[2].normal);
		VectorSubtract (vpn, vup, frustum[3].normal);
	}
	else
	{
		// rotate VPN right by FOV_X/2 degrees
		RotatePointAroundVector (frustum[0].normal, vup, vpn, -(90 - r_refdef.fov_x / 2));

		// rotate VPN left by FOV_X/2 degrees
		RotatePointAroundVector (frustum[1].normal, vup, vpn, 90 - r_refdef.fov_x / 2);

		// rotate VPN up by FOV_X/2 degrees
		RotatePointAroundVector (frustum[2].normal, vright, vpn, 90 - r_refdef.fov_y / 2);

		// rotate VPN down by FOV_X/2 degrees
		RotatePointAroundVector (frustum[3].normal, vright, vpn, -(90 - r_refdef.fov_y / 2));
	}

	for (i = 0; i < 4; i++)
	{
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

	r_framecount++;

	// build the transformation matrix for the given view angles
	VectorCopy (r_refdef.vieworg, r_origin);

	AngleVectors (r_refdef.viewangles, vpn, vright, vup);

	// current viewleaf
	r_oldviewleaf = r_viewleaf;
	r_viewleaf = Mod_PointInLeaf (r_origin, cl.worldmodel);

	V_SetContentsColor (r_viewleaf->contents);
	V_CalcBlend ();

	r_cache_thrash = false;

	c_brush_polys = 0;
	c_alias_polys = 0;
}


void D3D_InfiniteProjectionRH (D3DMATRIX *mi)
{
	mi->_33 = -1;
	mi->_43 = -1;
}


/*
=============
R_SetupD3D
=============
*/
// need to store this so that we can hack it for the gun and restore it after warp updates
D3DVIEWPORT9 d3d_3DViewport;
float FovYRadians = 0;

void D3D_BackfaceCull (DWORD D3D_CULLTYPE)
{
	if (!gl_cull.value)
		d3d_Device->SetRenderState (D3DRS_CULLMODE, D3DCULL_NONE);
	else d3d_Device->SetRenderState (D3DRS_CULLMODE, D3D_CULLTYPE);
}


void R_SetupD3D (void)
{
	// always clear the zbuffer
	DWORD d3d_ClearFlags = D3DCLEAR_ZBUFFER;

	// accumulate everything else we want to clear
	if (gl_clear.value) d3d_ClearFlags |= D3DCLEAR_TARGET;
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

#if 0
	// Only clear the 3D refresh area
	// note - this is slower than clearing the full screen, as the driver can optimise a fullscreen
	// clear better.  i've left it in but #if'ed out as a warning to myself not to do it again. ;)
	D3DRECT ClearRect;

	ClearRect.x1 = d3d_3DViewport.X;
	ClearRect.y1 = d3d_3DViewport.Y;
	ClearRect.x2 = d3d_3DViewport.X + d3d_3DViewport.Width;
	ClearRect.y2 = d3d_3DViewport.Y + d3d_3DViewport.Height;

	// we only need to clear if we're rendering 3D
	d3d_Device->Clear (1, &ClearRect, d3d_ClearFlags, 0x00000000, 1.0f, 0);
#else
	// we only need to clear if we're rendering 3D
	d3d_Device->Clear (0, NULL, d3d_ClearFlags, 0x00000000, 1.0f, 0);
#endif

	d3d_Device->BeginScene ();

	// load identity onto the view matrix (it seems as though some cards don't like it being set one time only)
	// we can get rid of this when we complete the transition from FVF to shaders
	D3DXMatrixIdentity (&d3d_ViewMatrix);
	d3d_Device->SetTransform (D3DTS_VIEW, &d3d_ViewMatrix);

	// set z range
	d3d_3DViewport.MinZ = 0.0f;
	d3d_3DViewport.MaxZ = 1.0f;

	// set the viewport
	d3d_Device->SetViewport (&d3d_3DViewport);

	// inconsequential in terms of optimization but may help prevent bugs elsewhere...
	FovYRadians = D3DXToRadian (r_refdef.fov_y);

	// projection matrix - this can't be set once and kept forever as refdef might change
	D3DXMatrixIdentity (&d3d_PerspectiveMatrix);
	D3DXMatrixPerspectiveFovRH (&d3d_PerspectiveMatrix, FovYRadians, (float) r_refdef.vrect.width / (float) r_refdef.vrect.height, 4, 4096);
	D3D_InfiniteProjectionRH (&d3d_PerspectiveMatrix);
	d3d_Device->SetTransform (D3DTS_PROJECTION, &d3d_PerspectiveMatrix);

	// world matrix
	d3d_WorldMatrixStack->LoadIdentity ();

	// put z going up
	d3d_WorldMatrixStack->RotateAxisLocal (&XVECTOR, D3DXToRadian (-90));
	d3d_WorldMatrixStack->RotateAxisLocal (&ZVECTOR, D3DXToRadian (90));

	// rotate by angles
	d3d_WorldMatrixStack->RotateAxisLocal (&XVECTOR, D3DXToRadian (-r_refdef.viewangles[2]));
	d3d_WorldMatrixStack->RotateAxisLocal (&YVECTOR, D3DXToRadian (-r_refdef.viewangles[0]));
	d3d_WorldMatrixStack->RotateAxisLocal (&ZVECTOR, D3DXToRadian (-r_refdef.viewangles[1]));

	// translate by origin
	d3d_WorldMatrixStack->TranslateLocal (-r_refdef.vieworg[0], -r_refdef.vieworg[1], -r_refdef.vieworg[2]);

	// set the transform
	d3d_Device->SetTransform (D3DTS_WORLD, d3d_WorldMatrixStack->GetTop ());

	// save the current world matrix
	memcpy (&d3d_WorldMatrix, d3d_WorldMatrixStack->GetTop (), sizeof (D3DXMATRIX));

	// depth testing and writing
	d3d_Device->SetRenderState (D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	d3d_Device->SetRenderState (D3DRS_ZENABLE, D3DZB_TRUE);
	d3d_Device->SetRenderState (D3DRS_ZWRITEENABLE, TRUE);

	// turn off smooth shading
	d3d_Device->SetRenderState (D3DRS_SHADEMODE, D3DSHADE_FLAT);

	// backface culling
	D3D_BackfaceCull (D3DCULL_CCW);

	// disable all alpha ops
	d3d_DisableAlphaTest->Apply ();
	d3d_DisableAlphaBlend->Apply ();
}


void R_MergeEntityToRenderFlags (entity_t *ent)
{
	if (ent->model->type == mod_alias)
		r_renderflags |= R_RENDERALIAS;
	else if (ent->model->type == mod_sprite)
		r_renderflags |= R_RENDERSPRITE;
	else if (ent->model->type == mod_brush)
	{
		// two different types of brush model - yuck yuck yuck
		if (ent->model->name[0] == '*')
			r_renderflags |= R_RENDERINLINEBRUSH;
		else r_renderflags |= R_RENDERINSTANCEDBRUSH;
	}
	else
	{
		// it's a content error if this happens
		Sys_Error ("R_MergeEntityToRenderFlags: Unimplemented Model Type");
		return;
	}
}


void R_SetupRenderState (void)
{
	texture_t *t;
	extern msurface_t *skychain;
	int i;

	for (i = 0; i < cl.worldbrush->numtextures; i++)
	{
		// e2m3 gets this
		if (!(t = cl.worldbrush->textures[i])) continue;

		// null it
		t->texturechain = NULL;
		t->visframe = -1;
	}

	// renderflags
	r_renderflags = 0;

	// sky as well
	skychain = NULL;

	// now check the entities and merge into r_renderflags
	// so that we know in advance what we need to render
	for (i = 0; i < cl_numvisedicts; i++) R_MergeEntityToRenderFlags (cl_visedicts[i]);
}


// this is basically the "a lof of this goes away" thing in the old gl_refrag...
// or at least one version of it.  see also CL_FindTouchedLeafs and the various struct defs
void R_AddStaticEntitiesForLeaf (mleaf_t *leaf)
{
	for (staticent_t *se = leaf->statics; se; se = se->next)
	{
		// already added
		if (se->ent->visframe == r_framecount) continue;

		// add it (only if we have visible edict slots to spare)
		if (cl_numvisedicts < MAX_VISEDICTS)
		{
			// the leafs containing the entities have already been bbox culled, so there's no need to check the entity!!!
			se->ent->nocullbox = true;
			cl_visedicts[cl_numvisedicts++] = se->ent;

			// merge into renderflags
			R_MergeEntityToRenderFlags (se->ent);

			// mark as visible for this frame
			se->ent->visframe = r_framecount;
		}
	}
}


/*
================
R_RenderScene

r_refdef must be set before the first call
================
*/
void R_RenderScene (void)
{
	D3D_BeginUnderwaterWarp ();

	// setup to draw
	R_SetupD3D ();
	R_SetupRenderState ();

	// adds the world and any static entities to the list
	R_DrawWorld ();

	// don't let sound get messed up if going slow
	S_ExtraUpdate ();

	// draw entities
	// note - sprites need to be drawn the same time as particles
	D3D_DrawInstancedBrushModels ();

	// draw everything else
	R_DrawWaterSurfaces ();

	D3D_DrawAliasModels ();

	R_DrawParticles ();

	// sprites deferred to same time as particles as they have similar properties & characteristics
	D3D_DrawSpriteModels ();

	// required by render to texture...
	d3d_Device->EndScene ();

	D3D_EndUnderwaterWarp ();
}


/*
================
R_RenderView

r_refdef must be set before the first call
================
*/
void R_RenderView (void)
{
	// get frametime
	r_frametime = cl.time - cl.oldtime;

	double	time1, time2;

	if (r_norefresh.value)
		return;

	if (!r_worldentity.model || !cl.worldmodel)
		Sys_Error ("R_RenderView: NULL worldmodel");

	if (r_speeds.value)
	{
		time1 = Sys_FloatTime ();
		c_brush_polys = 0;
		c_alias_polys = 0;
	}

	// set up to draw
	R_SetupFrame ();

	R_SetFrustum ();

	// done here so we know if we're in water
	R_MarkLeaves ();

	// render normal view
	R_RenderScene ();

	if (r_speeds.value)
	{
		time2 = Sys_FloatTime ();
		Con_Printf ("%3i ms  %4i wpoly %4i epoly\n", (int) ((time2 - time1) * 1000), c_brush_polys, c_alias_polys);
	}
}
