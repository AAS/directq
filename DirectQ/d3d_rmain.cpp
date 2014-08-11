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
void R_DrawWorld (void);
void R_RenderDlights (void);
void R_DrawParticles (void);
void R_DrawWaterSurfaces (void);
void R_DrawAliasModel (entity_t *e);
void R_DrawSpriteModel (entity_t *e);

entity_t	r_worldentity;

bool	r_cache_thrash;		// compatability

vec3_t		modelorg, r_entorigin;
entity_t	*currententity;

int			r_visframecount;	// bumped when going to a new PVS
int			r_framecount;		// used for dlight push checking

mplane_t	frustum[4];

int			c_brush_polys, c_alias_polys;

bool	envmap;				// true during envmap command capture 

int			currenttexture = -1;		// to avoid unnecessary texture sets

int			cnttextures[2] = {-1, -1};     // cached

int			mirrortexturenum;	// quake texturenum, not gltexturenum
bool	mirror;
mplane_t	*mirror_plane;

//
// view origin
//
vec3_t	vup;
vec3_t	vpn;
vec3_t	vright;
vec3_t	r_origin;

float	r_world_matrix[16];
float	r_base_world_matrix[16];

//
// screen size info
//
refdef_t	r_refdef;

mleaf_t		*r_viewleaf, *r_oldviewleaf;

texture_t	*r_notexture_mip;

int		d_lightstylevalue[256];	// 8.8 fraction of base light value


void R_MarkLeaves (void);

cvar_t	r_norefresh = {"r_norefresh","0"};
cvar_t	r_drawentities = {"r_drawentities","1"};
cvar_t	r_drawviewmodel = {"r_drawviewmodel","1"};
cvar_t	r_speeds = {"r_speeds","0"};
cvar_t	r_fullbright = {"r_fullbright","0"};
cvar_t	r_lightmap = {"r_lightmap","0"};
cvar_t	r_shadows = {"r_shadows","0"};
cvar_t	r_mirroralpha = {"r_mirroralpha","1"};
cvar_t	r_wateralpha = {"r_wateralpha","1"};
cvar_t	r_dynamic = {"r_dynamic","1"};
cvar_t	r_novis = {"r_novis","0"};

cvar_t	gl_finish = {"gl_finish","0"};
cvar_t	gl_clear = {"gl_clear","0", true};
cvar_t	gl_cull = {"gl_cull","1"};
cvar_t	gl_texsort = {"gl_texsort","1"};
cvar_t	gl_smoothmodels = {"gl_smoothmodels","1"};
cvar_t	gl_affinemodels = {"gl_affinemodels","0"};
cvar_t	gl_polyblend = {"gl_polyblend","1"};
cvar_t	gl_flashblend = {"gl_flashblend","0"};
cvar_t	gl_playermip = {"gl_playermip","0"};
cvar_t	gl_nocolors = {"gl_nocolors","0"};
cvar_t	gl_keeptjunctions = {"gl_keeptjunctions","0"};
cvar_t	gl_reporttjunctions = {"gl_reporttjunctions","0"};
cvar_t	gl_doubleeyes = {"gl_doubleeys", "1"};

extern	cvar_t	gl_ztrick;

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


void D3D_RotateForEntity (entity_t *e, bool shadow)
{
	// calculate transforms
	d3d_WorldMatrixStack->TranslateLocal (e->origin[0], e->origin[1], e->origin[2]);

	d3d_WorldMatrixStack->RotateAxisLocal (&ZVECTOR, D3DXToRadian (e->angles[1]));

	if (!shadow)
	{
		d3d_WorldMatrixStack->RotateAxisLocal (&YVECTOR, D3DXToRadian (-e->angles[0]));
		d3d_WorldMatrixStack->RotateAxisLocal (&XVECTOR, D3DXToRadian (e->angles[2]));
	}

	// set the transforms
	d3d_Device->SetTransform (D3DTS_WORLD, d3d_WorldMatrixStack->GetTop ());
}


void D3D_RotateForEntity (entity_t *e)
{
	D3D_RotateForEntity (e, false);
}


//==================================================================================

D3DMATERIAL9 d3d_Material;
D3DLIGHT9 d3d_Light0;

void D3D_SetAliasRenderState (bool enable)
{
	if (enable)
	{
		// set alias model state
		if (gl_smoothmodels.value) d3d_Device->SetRenderState (D3DRS_SHADEMODE, D3DSHADE_GOURAUD);

		//	if (gl_affinemodels.value)
		//		glHint (GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST);

		// set fvf to include normals as we're going to light these using d3d lighting
		D3D_SetFVFStateManaged (D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
	}
	else
	{
		//	if (gl_affinemodels.value)
		//		glHint (GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);

		// flat shading
		d3d_Device->SetRenderState (D3DRS_SHADEMODE, D3DSHADE_FLAT);

		// go back to the world matrix
		d3d_Device->SetTransform (D3DTS_WORLD, d3d_WorldMatrix);
	}
}


/*
=============
R_DrawEntitiesOnList
=============
*/
void R_DrawEntitiesOnList (void)
{
	int i;
	int numalias = 0;
	int numsprite = 0;

	if (!r_drawentities.value) return;

	// draw sprites seperately, because of alpha blending
	for (i = 0; i < cl_numvisedicts; i++)
	{
		currententity = cl_visedicts[i];

		switch (currententity->model->type)
		{
		case mod_alias:
			if (!numalias) D3D_SetAliasRenderState (true);

			R_DrawAliasModel (currententity);

			numalias++;
			break;

		default:
			break;
		}
	}

	// revert state
	if (numalias) D3D_SetAliasRenderState (false);

	for (i = 0; i < cl_numvisedicts; i++)
	{
		currententity = cl_visedicts[i];

		switch (currententity->model->type)
		{
		case mod_sprite:
			if (!numsprite)
			{
				d3d_EnableAlphaTest->Apply ();
				D3D_SetFVFStateManaged (D3DFVF_XYZ | D3DFVF_TEX1);
			}

			R_DrawSpriteModel (currententity);

			numsprite++;
			break;
		}
	}

	if (numsprite)
	{
		d3d_DisableAlphaTest->Apply ();
	}
}

/*
=============
R_DrawViewModel
=============
*/
void R_DrawViewModel (void)
{
	currententity = &cl.viewent;

	if (!r_drawviewmodel.value) return;
	if (chase_active.value) return;
	if (envmap) return;
	if (!r_drawentities.value) return;
	if (cl.items & IT_INVISIBILITY) return;
	if (cl.stats[STAT_HEALTH] <= 0) return;
	if (!currententity->model) return;

	// hack the depth range to prevent view model from poking into walls
	d3d_GunViewport->Apply ();

	// set alias model state
	D3D_SetAliasRenderState (true);

	// create a new projection matrix for the gun, so as to keep it properly visible at different values of fov
	// this is the last thing drawn in the 3d render so we don't bother setting it back to the way it was...
	D3DXMatrixIdentity (&d3d_ProjectionMatrix);
	D3DXMatrixPerspectiveFovRH (&d3d_ProjectionMatrix, 1.1989051, (float) r_refdef.vrect.width / (float) r_refdef.vrect.height, 4, 4096);
	d3d_Device->SetTransform (D3DTS_PROJECTION, &d3d_ProjectionMatrix);

	R_DrawAliasModel (currententity);

	D3D_SetAliasRenderState (false);
}


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
		RotatePointAroundVector( frustum[0].normal, vup, vpn, -(90-r_refdef.fov_x / 2 ) );
		// rotate VPN left by FOV_X/2 degrees
		RotatePointAroundVector( frustum[1].normal, vup, vpn, 90-r_refdef.fov_x / 2 );
		// rotate VPN up by FOV_X/2 degrees
		RotatePointAroundVector( frustum[2].normal, vright, vpn, 90-r_refdef.fov_y / 2 );
		// rotate VPN down by FOV_X/2 degrees
		RotatePointAroundVector( frustum[3].normal, vright, vpn, -( 90 - r_refdef.fov_y / 2 ) );
	}

	for (i=0 ; i<4 ; i++)
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
	if (cl.maxclients > 1)
		Cvar_Set ("r_fullbright", "0");

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
R_SetupGL
=============
*/
void R_SetupGL (void)
{
	// projection matrix - this can't be set once and kept forever as refdef might change (should actually be in refdef change code!)
	D3DXMatrixIdentity (&d3d_ProjectionMatrix);
	D3DXMatrixPerspectiveFovRH (&d3d_ProjectionMatrix, D3DXToRadian (r_refdef.fov_y), (float) r_refdef.vrect.width / (float) r_refdef.vrect.height, 4, 4096);
	D3D_InfiniteProjectionRH (&d3d_ProjectionMatrix);
	d3d_Device->SetTransform (D3DTS_PROJECTION, &d3d_ProjectionMatrix);

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
	d3d_WorldMatrix = d3d_WorldMatrixStack->GetTop ();

	// depth testing and writing
	d3d_Device->SetRenderState (D3DRS_ZENABLE, D3DZB_TRUE);
	d3d_Device->SetRenderState (D3DRS_ZWRITEENABLE, TRUE);

	// turn off smooth shading
	d3d_Device->SetRenderState (D3DRS_SHADEMODE, D3DSHADE_FLAT);

	// backface culling
	d3d_Device->SetRenderState (D3DRS_CULLMODE, D3DCULL_CCW);

	// disable all alpha ops
	d3d_DisableAlphaTest->Apply ();
	d3d_DisableAlphaBlend->Apply ();
}


/*
================
R_RenderScene

r_refdef must be set before the first call
================
*/
void R_RenderScene (void)
{
	R_SetupFrame ();

	R_SetFrustum ();

	R_SetupGL ();

	R_MarkLeaves ();	// done here so we know if we're in water

	R_DrawWorld ();		// adds static entities to the list

	S_ExtraUpdate ();	// don't let sound get messed up if going slow

	R_DrawEntitiesOnList ();

	R_RenderDlights ();

	R_DrawWaterSurfaces ();

	R_DrawParticles ();

	// note - must be last as it messes with the viewport and the projection matrix
	R_DrawViewModel ();
}


/*
=============
R_Mirror
=============
*/
void R_Mirror (void)
{
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

	mirror = false;

	// render normal view
	R_RenderScene ();

	if (r_speeds.value)
	{
		time2 = Sys_FloatTime ();
		Con_Printf ("%3i ms  %4i wpoly %4i epoly\n", (int)((time2-time1)*1000), c_brush_polys, c_alias_polys); 
	}
}
