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

/*
automap system

the automap isn't MEANT to be particularly robust or performant.  it's intended for use as a quick and dirty "where the fuck am i?" system
*/

#include "quakedef.h"
#include "d3d_quake.h"
#include "d3d_hlsl.h"

#include <vector>


bool r_automap;
extern bool scr_drawmapshot;
extern bool scr_drawloading;
bool d3d_AutomapDraw = false;

extern cvar_t r_lightscale;

void Cmd_ToggleAutomap_f (void)
{
	r_automap = !r_automap;
}

cmd_t Cmd_ToggleAutomap ("toggleautomap", Cmd_ToggleAutomap_f);

void R_PlaneSide (mplane_t *plane, double *dot, int *side);
void R_MarkLeafSurfs (mleaf_t *leaf, int visframe);
void R_SetupRenderState (void);
void D3D_DrawWorld (void);
void R_DrawWaterSurfaces (void);
void D3D_AddInlineBModelsToTextureChains (void);

int c_automapsurfs = 0;

void D3D_AutomapRecursiveNode (mnode_t *node)
{
	int			side;
	mplane_t	*plane;
	msurface_t	*surf;
	double		dot;

	if (node->contents == CONTENTS_SOLID) return;
	if (!node->seen) return;

	if (node->contents < 0)
	{
		// just mark the surfs
		R_MarkLeafSurfs ((mleaf_t *) node, r_framecount);
		return;
	}

	// find which side of the node we are on
	R_PlaneSide (node->plane, &dot, &side);

	// recurse down the children, front side first
	D3D_AutomapRecursiveNode (node->children[side]);

	// add stuff to the draw lists
	int c = node->numsurfaces;

	if (c)
	{
		surf = cl.worldbrush->surfaces + node->firstsurface;

		if (dot < 0 -BACKFACE_EPSILON)
			side = SURF_PLANEBACK;
		else if (dot > BACKFACE_EPSILON)
			side = 0;

		for (; c; c--, surf++)
		{
			if (surf->visframe != r_framecount) continue;
			if (((dot < 0) ^ !!(surf->flags & SURF_PLANEBACK))) continue;

			if (surf->flags & SURF_DRAWSKY) continue;

			if (surf->mins[2] > r_refdef.vieworg[2]) continue;

			// get the correct animation sequence
			texture_t *tex = R_TextureAnimation (surf->texinfo->texture);

			if (surf->flags & SURF_DRAWTURB)
			{
				// link it in (back to front)
				surf->texturechain = tex->texturechain;
				tex->texturechain = surf;

				// mark the texture as having been visible
				tex->visframe = r_framecount;

				// flag for rendering
				r_renderflags |= R_RENDERWATERSURFACE;
			}
			else
			{
				// link it in (front to back)
				if (!tex->chaintail)
					tex->texturechain = surf;
				else tex->chaintail->texturechain = surf;

				tex->chaintail = surf;
				surf->texturechain = NULL;
			}

			c_automapsurfs++;
		}
	}

	// recurse down back side
	D3D_AutomapRecursiveNode (node->children[!side]);
}


bool D3D_DrawAutomap (void)
{
	if (!r_automap) return false;
	if (cls.state != ca_connected) return false;
	if (scr_drawmapshot) return false;
	if (scr_drawloading) return false;
	if (cl.intermission == 1 && key_dest == key_game) return false;
	if (cl.intermission == 2 && key_dest == key_game) return false;

	// always clear the zbuffer
	DWORD d3d_ClearFlags = D3DCLEAR_ZBUFFER | D3DCLEAR_TARGET;

	// accumulate everything else we want to clear
	if (d3d_GlobalCaps.DepthStencilFormat == D3DFMT_D24S8) d3d_ClearFlags |= D3DCLEAR_STENCIL;

	// always fully clear to black
	d3d_Device->Clear (0, NULL, d3d_ClearFlags, 0, 1.0f, 0);

	// need to advance the count here...
	r_framecount++;

	// keep a count of surfs
	c_automapsurfs = 0;

	entity_t	ent;

	memset (&ent, 0, sizeof(ent));
	ent.model = cl.worldmodel;

	VectorCopy (r_refdef.vieworg, modelorg);
	currententity = &ent;

	float pw = cl.worldmodel->maxs[0] - cl.worldmodel->mins[0];
	float ph = cl.worldmodel->maxs[1] - cl.worldmodel->mins[1];

	if (pw > ph)
	{
	}
	else
	{
	}

	// projection matrix - invert it so that controls work as expected
	D3DXMatrixIdentity (&d3d_PerspectiveMatrix);
	D3DXMatrixOrthoOffCenterRH (&d3d_PerspectiveMatrix, 0, pw, 0, ph, cl.worldmodel->mins[2] - 100, cl.worldmodel->maxs[2] + 100);

	// world matrix
	d3d_WorldMatrixStack->LoadIdentity ();
	d3d_WorldMatrixStack->TranslateLocal (-cl.worldmodel->mins[0], -cl.worldmodel->mins[1], 0);

	// save the current world matrix
	memcpy (&d3d_WorldMatrix, d3d_WorldMatrixStack->GetTop (), sizeof (D3DXMATRIX));

	// depth testing and writing
	d3d_Device->SetRenderState (D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	d3d_Device->SetRenderState (D3DRS_ZENABLE, D3DZB_TRUE);
	d3d_Device->SetRenderState (D3DRS_ZWRITEENABLE, TRUE);

	// turn off smooth shading
	d3d_Device->SetRenderState (D3DRS_SHADEMODE, D3DSHADE_FLAT);

	// backface culling
	D3D_BackfaceCull (D3DCULL_NONE);

	// draw it all
	R_SetupRenderState ();
	D3D_AutomapRecursiveNode (cl.worldbrush->nodes);
	D3D_AddInlineBModelsToTextureChains ();
	D3D_DrawWorld ();
	R_DrawWaterSurfaces ();

	Con_DPrintf ("%i automap surfs\n", c_automapsurfs);

	return true;
}

