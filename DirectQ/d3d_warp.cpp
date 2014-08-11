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
// gl_warp.c -- sky and water polygons

#include "quakedef.h"
#include "d3d_model.h"
#include "d3d_quake.h"

// surface interface
void D3DBrush_SetBuffers (void);
void D3DBrush_FlushSurfaces (void);
void D3DBrush_SubmitSurface (msurface_t *surf, entity_t *ent);



/*
==============================================================================================================================

		WATER WARP RENDERING

==============================================================================================================================
*/


byte d3d_WaterAlpha = 255;
byte d3d_LavaAlpha = 255;
byte d3d_SlimeAlpha = 255;
byte d3d_TeleAlpha = 255;

cvar_t r_lavaalpha ("r_lavaalpha", 1);
cvar_t r_telealpha ("r_telealpha", 1);
cvar_t r_slimealpha ("r_slimealpha", 1);
cvar_t r_warpspeed ("r_warpspeed", 4, CVAR_ARCHIVE);
cvar_t r_warpscale ("r_warpscale", 8, CVAR_ARCHIVE);
cvar_t r_warpfactor ("r_warpfactor", 2, CVAR_ARCHIVE);

// lock water/slime/tele/lava alpha sliders
cvar_t r_lockalpha ("r_lockalpha", "1");

float warptime = 10;

void D3DWarp_InitializeTurb (void)
{
	// store alpha values
	// this needs to be checked first because the actual values we'll use depend on whether or not the sliders are locked
	// multiply by 256 to prevent float rounding errors
	if (r_lockalpha.value)
	{
		// locked sliders
		d3d_WaterAlpha = BYTE_CLAMP (r_wateralpha.value * 256);
		d3d_LavaAlpha = BYTE_CLAMP (r_wateralpha.value * 256);
		d3d_SlimeAlpha = BYTE_CLAMP (r_wateralpha.value * 256);
		d3d_TeleAlpha = BYTE_CLAMP (r_wateralpha.value * 256);
	}
	else
	{
		// independent sliders
		d3d_WaterAlpha = BYTE_CLAMP (r_wateralpha.value * 256);
		d3d_LavaAlpha = BYTE_CLAMP (r_lavaalpha.value * 256);
		d3d_SlimeAlpha = BYTE_CLAMP (r_slimealpha.value * 256);
		d3d_TeleAlpha = BYTE_CLAMP (r_telealpha.value * 256);
	}

	// bound factor
	if (r_warpfactor.value < 0) Cvar_Set (&r_warpfactor, 0.0f);
	if (r_warpfactor.value > 8) Cvar_Set (&r_warpfactor, 8);

	// bound scale
	if (r_warpscale.value < 0) Cvar_Set (&r_warpscale, 0.0f);
	if (r_warpscale.value > 32) Cvar_Set (&r_warpscale, 32);

	// bound speed
	if (r_warpspeed.value < 1) Cvar_Set (&r_warpspeed, 1);
	if (r_warpspeed.value > 32) Cvar_Set (&r_warpspeed, 32);

	// set the warp time and factor (moving this calculation out of a loop)
	warptime = d3d_RenderDef.time * 10.18591625f * r_warpspeed.value;

	// update once only in the master shader
	D3DHLSL_SetFloat ("warptime", warptime);
	D3DHLSL_SetFloat ("warpfactor", r_warpfactor.value);
	D3DHLSL_SetFloat ("warpscale", r_warpscale.value);
}


LPDIRECT3DTEXTURE9 previouswarptexture = NULL;
int previouswarpalpha = -1;

void D3DWarp_SetupTurbState (void)
{
	D3D_SetTextureMipmap (0, d3d_TexFilter, d3d_MipFilter);
	D3D_SetTextureAddressMode (D3DTADDRESS_WRAP);

	D3DBrush_SetBuffers ();
	D3DHLSL_SetPass (FX_PASS_LIQUID);

	previouswarptexture = NULL;
	previouswarpalpha = -1;
}


void D3DWarp_TakeDownTurbState (void)
{
	D3DBrush_FlushSurfaces ();
}


// fix me - is this warp or brush???
void D3D_EmitModelSurfToAlpha (d3d_modelsurf_t *modelsurf)
{
	if (modelsurf->ent)
	{
		// copy out the midpoint for matrix transforms
		float midpoint[3] =
		{
			modelsurf->surf->midpoint[0],
			modelsurf->surf->midpoint[1],
			modelsurf->surf->midpoint[2]
		};

		// keep the code easier to read
		D3DMATRIX *m = &modelsurf->ent->matrix;

		// transform the surface midpoint by the modelsurf matrix so that it goes into the proper place
		modelsurf->surf->midpoint[0] = midpoint[0] * m->_11 + midpoint[1] * m->_21 + midpoint[2] * m->_31 + m->_41;
		modelsurf->surf->midpoint[1] = midpoint[0] * m->_12 + midpoint[1] * m->_22 + midpoint[2] * m->_32 + m->_42;
		modelsurf->surf->midpoint[2] = midpoint[0] * m->_13 + midpoint[1] * m->_23 + midpoint[2] * m->_33 + m->_43;

		// now add it
		D3DAlpha_AddToList (modelsurf);

		// restore the original midpoint
		modelsurf->surf->midpoint[0] = midpoint[0];
		modelsurf->surf->midpoint[1] = midpoint[1];
		modelsurf->surf->midpoint[2] = midpoint[2];
	}
	else
	{
		// just add it
		D3DAlpha_AddToList (modelsurf);
	}
}


void D3DWarp_DrawSurface (d3d_modelsurf_t *modelsurf)
{
	msurface_t *surf = modelsurf->surf;
	bool recommit = false;
	byte thisalpha = 255;

	// check because we can get a frame update while the verts are NULL
	if (!modelsurf->surf->verts) return;

	// automatic alpha
	if (surf->flags & SURF_DRAWWATER) thisalpha = d3d_WaterAlpha;
	if (surf->flags & SURF_DRAWLAVA) thisalpha = d3d_LavaAlpha;
	if (surf->flags & SURF_DRAWTELE) thisalpha = d3d_TeleAlpha;
	if (surf->flags & SURF_DRAWSLIME) thisalpha = d3d_SlimeAlpha;

	// explicit alpha
	if (modelsurf->surfalpha < 255) thisalpha = modelsurf->surfalpha;
	if (thisalpha != previouswarpalpha) recommit = true;
	if (modelsurf->textures[TEXTURE_DIFFUSE] != previouswarptexture) recommit = true;

	// if we need to change state here we must flush anything batched so far before doing so
	// either way we flag a commit pending too
	if (recommit) D3DBrush_FlushSurfaces ();

	// check for a texture or alpha change
	// (either of these will trigger the recommit above)
	if (modelsurf->textures[TEXTURE_DIFFUSE] != previouswarptexture)
	{
		D3DHLSL_SetTexture (0, modelsurf->textures[TEXTURE_DIFFUSE]);
		previouswarptexture = modelsurf->textures[TEXTURE_DIFFUSE];
	}

	if (thisalpha != previouswarpalpha)
	{
		D3DHLSL_SetAlpha ((float) thisalpha / 255.0f);
		previouswarpalpha = thisalpha;
	}

	D3DBrush_SubmitSurface (surf, modelsurf->ent);
}


void D3DWarp_DrawWaterSurfaces (d3d_modelsurf_t **modelsurfs, int nummodelsurfs)
{
	bool stateset = false;

	// even if we're fully alpha we still pass through here so that we can add items to the alpha list
	for (int i = 0; i < nummodelsurfs; i++)
	{
		d3d_modelsurf_t *ms = modelsurfs[i];
		msurface_t *surf = ms->surf;

		if (!(surf->flags & SURF_DRAWTURB)) continue;

		// skip over alpha surfaces - automatic skip unless the surface explicitly overrides it (same path)
		if ((surf->flags & SURF_DRAWLAVA) && d3d_LavaAlpha < 255) {D3D_EmitModelSurfToAlpha (ms); continue;}
		if ((surf->flags & SURF_DRAWTELE) && d3d_TeleAlpha < 255) {D3D_EmitModelSurfToAlpha (ms); continue;}
		if ((surf->flags & SURF_DRAWSLIME) && d3d_SlimeAlpha < 255) {D3D_EmitModelSurfToAlpha (ms); continue;}
		if ((surf->flags & SURF_DRAWWATER) && d3d_WaterAlpha < 255) {D3D_EmitModelSurfToAlpha (ms); continue;}

		// determine if we need a state update; this is just for setting the
		// initial state; we always update the texture on a texture change
		// we check in here because we might be skipping some of these surfaces if they have alpha set
		if (!stateset)
		{
			D3DWarp_SetupTurbState ();
			stateset = true;
		}

		// this one used for both alpha and non-alpha
		D3DWarp_DrawSurface (ms);
	}

	// determine if we need to take down state
	if (stateset) D3DWarp_TakeDownTurbState ();
}


