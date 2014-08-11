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
// r_misc.c

#include "quakedef.h"
#include "d3d_model.h"
#include "d3d_quake.h"
#include "resource.h"

extern HWND d3d_Window;

/*
============================================================================================================

		SETUP

============================================================================================================
*/

void R_InitParticles (void);
void R_ClearParticles (void);
void D3DLight_BuildAllLightmaps (void);
void D3DSky_LoadSkyBox (char *basename, bool feedback);

LPDIRECT3DTEXTURE9 r_notexture = NULL;
extern LPDIRECT3DTEXTURE9 crosshairtexture;


/*
==================
R_InitTextures
==================
*/
void R_InitTextures (void)
{
	// create a simple checkerboard texture for the default
	r_notexture_mip = (texture_t *) Zone_Alloc (sizeof (texture_t) + 4 * 4);

	r_notexture_mip->size[0] = r_notexture_mip->size[1] = 4;
	byte *dest = (byte *) (r_notexture_mip + 1);

	for (int y = 0; y < 4; y++)
	{
		for (int x = 0; x < 4; x++)
		{
			if ((y < 2) ^ (x < 2))
				*dest++ = 0;
			else *dest++ = 0xf;
		}
	}
}


void Draw_FreeCrosshairs (void);
void D3DWarp_WWTexOnRecover (void);
void D3DPart_OnRecover (void);

void R_ReleaseResourceTextures (void)
{
	SAFE_RELEASE (crosshairtexture);
	SAFE_RELEASE (r_notexture);

	// and replacement crosshairs too
	Draw_FreeCrosshairs ();
}


void R_InitResourceTextures (void)
{
	D3DPart_OnRecover ();
	D3DWarp_WWTexOnRecover ();

	// load any textures contained in exe resources
	D3D_LoadResourceTexture ("crosshairs", &crosshairtexture, IDR_CROSSHAIR, 0);

	// load the notexture properly
	D3D_UploadTexture (&r_notexture, (byte *) (r_notexture_mip + 1), r_notexture_mip->size[0], r_notexture_mip->size[1], IMAGE_MIPMAP);
}


/*
===============
R_Init
===============
*/
cvar_t r_lerporient ("r_lerporient", "1", CVAR_ARCHIVE);
cvar_t r_lerpframe ("r_lerpframe", "1", CVAR_ARCHIVE);

// allow the old QER names as aliases
cvar_alias_t r_interpolate_model_animation ("r_interpolate_model_animation", &r_lerpframe);
cvar_alias_t r_interpolate_model_transform ("r_interpolate_model_transform", &r_lerporient);

cmd_t R_ReadPointFile_f_Cmd ("pointfile", R_ReadPointFile_f);

extern image_t d3d_PlayerSkins[];

void R_Init (void)
{
	R_InitParticles ();
	R_InitResourceTextures ();

	for (int i = 0; i < 256; i++)
	{
		SAFE_RELEASE (d3d_PlayerSkins[i].d3d_Texture);
		d3d_PlayerSkins[i].LastUsage = 0;
	}
}


/*
============================================================================================================

		NEW MAP

============================================================================================================
*/

/*
===============
R_NewMap
===============
*/
void S_InitAmbients (void);
void D3DSky_ParseWorldSpawn (void);


bool R_RecursiveLeafContents (mnode_t *node)
{
	int			side;
	mplane_t	*plane;
	msurface_t	*surf;
	float		dot;

	if (node->contents == CONTENTS_SOLID) return true;
	if (node->visframe != d3d_RenderDef.visframecount) return true;

	// if a leaf node, draw stuff
	if (node->contents < 0)
	{
		// update contents colour
		if (((mleaf_t *) node)->contents == d3d_RenderDef.viewleaf->contents)
			((mleaf_t *) node)->contentscolor = d3d_RenderDef.viewleaf->contentscolor;
		else
		{
			// don't cross contents boundaries
			return false;
		}

		// leaf visframes are never marked?
		((mleaf_t *) node)->visframe = d3d_RenderDef.visframecount;
		return true;
	}

	// go down both sides
	if (!R_RecursiveLeafContents (node->children[0])) return false;

	return R_RecursiveLeafContents (node->children[1]);
}


void R_LeafVisibility (byte *vis);

void R_SetLeafContents (void)
{
	d3d_RenderDef.visframecount = -1;

	for (int i = 0; i < cl.worldmodel->brushhdr->numleafs; i++)
	{
		mleaf_t *leaf = &cl.worldmodel->brushhdr->leafs[i];

		// explicit NULLs
		if (leaf->contents == CONTENTS_EMPTY)
			leaf->contentscolor = NULL;
		else if (leaf->contents == CONTENTS_SOLID)
			leaf->contentscolor = NULL;
		else if (leaf->contents == CONTENTS_SKY)
			leaf->contentscolor = NULL;

		// no contents
		if (!leaf->contentscolor) continue;

		// go to a new visframe, reverse order so that we don't get mixed up with the main render
		d3d_RenderDef.visframecount--;
		d3d_RenderDef.viewleaf = leaf;

		// get pvs for this leaf
		byte *vis = Mod_LeafPVS (leaf, cl.worldmodel);

		// eval visibility
		R_LeafVisibility (vis);

		// update leaf contents
		R_RecursiveLeafContents (cl.worldmodel->brushhdr->nodes);
	}

	d3d_RenderDef.visframecount = 0;
}


void LOC_LoadLocations (void);

extern byte *fatpvs;

void Con_RemoveConsole (void);
void Menu_RemoveMenu (void);
void D3DSky_RevalidateSkybox (void);
void D3D_ModelSurfsBeginMap (void);
void Fog_ParseWorldspawn (void);
void IN_ClearMouseState (void);
void D3DAlias_CreateBuffers (void);
void D3DAlpha_NewMap (void);
void Mod_InitForMap (model_t *mod);
void D3DTexture_RegisterChains (void);
void D3DBrush_CreateVBOs (void);
void D3DLight_EndBuildingLightmaps (void);
void D3DBrush_BuildBModelVBOs (void);
void Host_InitTimers (void);

void R_NewMap (void)
{
	// set up the pvs arrays (these will already have been done by the server if it's active
	if (!sv.active) Mod_InitForMap (cl.worldmodel);

	// init frame counters
	d3d_RenderDef.framecount = 1;
	d3d_RenderDef.visframecount = 0;

	// clear out efrags (one short???)
	for (int i = 0; i < cl.worldmodel->brushhdr->numleafs; i++)
		cl.worldmodel->brushhdr->leafs[i].efrags = NULL;

	// world entity baseline
	memset (&d3d_RenderDef.worldentity, 0, sizeof (entity_t));
	d3d_RenderDef.worldentity.model = cl.worldmodel;
	d3d_RenderDef.worldentity.alphaval = 255;

	// fix up the worldmodel surfaces so it's consistent and we can reuse code
	cl.worldmodel->brushhdr->firstmodelsurface = 0;
	cl.worldmodel->brushhdr->nummodelsurfaces = cl.worldmodel->brushhdr->numsurfaces;

	// init edict pools
	d3d_RenderDef.numvisedicts = 0;

	// no viewpoint
	d3d_RenderDef.viewleaf = NULL;
	d3d_RenderDef.oldviewleaf = NULL;
	d3d_RenderDef.lastgoodcontents = NULL;

	// setup stuff
	R_ClearParticles ();

	// finish building lightmaps for this map
	D3DLight_EndBuildingLightmaps ();
	d3d_RenderDef.WorldModelLoaded = false;

	D3D_FlushTextures ();
	R_SetLeafContents ();
	D3DSky_ParseWorldSpawn ();
	D3D_ModelSurfsBeginMap ();
	D3DAlpha_NewMap ();
	Fog_ParseWorldspawn ();
	D3DAlias_CreateBuffers ();

	// release cached skins to save memory
	for (int i = 0; i < 256; i++) SAFE_RELEASE (d3d_PlayerSkins[i].d3d_Texture);

	// as sounds are now cleared between maps these sounds also need to be
	// reloaded otherwise the known_sfx will go out of sequence for them
	// (this isn't the case any more but it does no harm)
	CL_InitTEnts ();
	S_InitAmbients ();
	LOC_LoadLocations ();
	D3DTexture_RegisterChains ();
	D3DBrush_CreateVBOs ();
	D3DBrush_BuildBModelVBOs ();

	// see do we need to switch off the menus or console
	if (key_dest != key_game && (cls.demoplayback || cls.demorecording || cls.timedemo))
	{
		Con_RemoveConsole ();
		Menu_RemoveMenu ();

		// switch to game
		key_dest = key_game;
	}

	// activate the mouse and flush the directinput buffers
	// (pretend we're fullscreen because we definitely want to hide the mouse here)
	IN_SetMouseState (true);

	// revalidate the skybox in case the last one was cleared
	D3DSky_RevalidateSkybox ();

	// reset these again here as they can be changed during load processing
	d3d_RenderDef.framecount = 1;
	d3d_RenderDef.visframecount = 0;

	// flush all the input buffers and go back to a default state
	IN_ClearMouseState ();

	// go to the next registration sequence
	d3d_RenderDef.RegistrationSequence++;

	// reinit the timers to keep fx consistent
	Host_InitTimers ();

	// we're running a map now
	cls.maprunning = true;
}


