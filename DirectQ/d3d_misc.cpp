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

LPDIRECT3DTEXTURE9 d3d_PaletteRowTextures[16] = {NULL};
LPDIRECT3DTEXTURE9 r_notexture = NULL;

extern LPDIRECT3DTEXTURE9 crosshairtexture;
extern LPDIRECT3DTEXTURE9 particletexture;
extern LPDIRECT3DTEXTURE9 shadetexture;


void D3DMisc_CreatePalette (void)
{
	// reconvert backward ranges to forward so that we can do correct lookups on them
	for (int i = 0; i < 16; i++)
	{
		byte palrow[16];

		for (int j = 0, k = 15; j < 16; j++, k--)
			palrow[j] = i * 16 + ((i > 7 && i < 14) ? k : j);

		D3D_UploadTexture (&d3d_PaletteRowTextures[i], (byte *) palrow, 16, 1, 0);
	}
}


void D3DMisc_ReleasePalette (void)
{
	for (int i = 0; i < 16; i++)
		SAFE_RELEASE (d3d_PaletteRowTextures[i]);
}


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


void FractalNoise (unsigned char *noise, int size, int startgrid)
{
	int x, y, g, g2, amplitude, min, max, size1 = size - 1, sizepower, gridpower;
	int *noisebuf;
	int hunkmark = MainHunk->GetLowMark ();

#define n(x, y) noisebuf[((y) & size1) * size + ((x) & size1)]

	for (sizepower = 0; (1 << sizepower) < size; sizepower++);

	if (size != (1 << sizepower))
	{
		Con_Printf ("fractalnoise: size must be power of 2\n");
		return;
	}

	for (gridpower = 0; (1 << gridpower) < startgrid; gridpower++);

	if (startgrid != (1 << gridpower))
	{
		Con_Printf ("fractalnoise: grid must be power of 2\n");
		return;
	}

	if (startgrid < 0) startgrid = 0;
	if (startgrid > size) startgrid = size;

	amplitude = 0xffff; // this gets halved before use
	noisebuf = (int *) MainHunk->Alloc (size * size * sizeof (int));
	memset (noisebuf, 0, size * size * sizeof (int));

	for (g2 = startgrid; g2; g2 >>= 1)
	{
		// brownian motion (at every smaller level there is random behavior)
		amplitude >>= 1;

		for (y = 0; y < size; y += g2)
			for (x = 0; x < size; x += g2)
				n (x, y) += (rand () & amplitude);

		g = g2 >> 1;

		if (g)
		{
			// subdivide, diamond-square algorithm (really this has little to do with squares)
			// diamond
			for (y = 0; y < size; y += g2)
				for (x = 0; x < size; x += g2)
					n (x + g, y + g) = (n (x, y) + n (x + g2, y) + n (x, y + g2) + n (x + g2, y + g2)) >> 2;

			// square
			for (y = 0; y < size; y += g2)
			{
				for (x = 0; x < size; x += g2)
				{
					n (x + g, y) = (n (x, y) + n (x + g2, y) + n (x + g, y - g) + n (x + g, y + g)) >> 2;
					n (x, y + g) = (n (x, y) + n (x, y + g2) + n (x - g, y + g) + n (x + g, y + g)) >> 2;
				}
			}
		}
	}

	// find range of noise values
	min = max = 0;

	for (y = 0; y < size; y++)
	{
		for (x = 0; x < size; x++)
		{
			if (n (x, y) < min) min = n (x, y);
			if (n (x, y) > max) max = n (x, y);
		}
	}

	max -= min;
	max++;

	// normalize noise and copy to output
	for (y = 0; y < size; y++)
		for (x = 0; x < size; x++)
			*noise++ = (unsigned char) (((n (x, y) - min) * 256) / max);

	MainHunk->FreeToLowMark (hunkmark);
#undef n
}


void FractalNoise32 (unsigned int *noise, int size, int startgrid)
{
	int hunkmark = MainHunk->GetLowMark ();

	byte *b = (byte *) MainHunk->Alloc (size * size);
	byte *g = (byte *) MainHunk->Alloc (size * size);
	byte *r = (byte *) MainHunk->Alloc (size * size);
	byte *a = (byte *) MainHunk->Alloc (size * size);

	FractalNoise (b, size, startgrid);
	FractalNoise (g, size, startgrid);
	FractalNoise (r, size, startgrid);
	FractalNoise (a, size, startgrid);

	byte *bgra = (byte *) noise;

	for (int i = 0; i < size * size; i++, bgra += 4)
	{
		bgra[0] = b[i];
		bgra[1] = g[i];
		bgra[2] = r[i];
		bgra[3] = a[i];
	}

	MainHunk->FreeToLowMark (hunkmark);
}


void Draw_FreeCrosshairs (void);
void D3DWarp_WWTexOnRecover (void);
void D3DPart_OnRecover (void);

void R_ReleaseResourceTextures (void)
{
	SAFE_RELEASE (crosshairtexture);
	SAFE_RELEASE (particletexture);
	SAFE_RELEASE (r_notexture);
	SAFE_RELEASE (shadetexture);

	// and replacement crosshairs too
	Draw_FreeCrosshairs ();
}


void R_FlattenTexture (unsigned *data, int width, int height)
{
	byte *bgra = (byte *) data;

	for (int h = 0; h < height; h++)
	{
		for (int w = 0; w < width; w++, bgra += 4)
		{
			for (int c = 0; c < 4; c++)
			{
				int in = bgra[c];

				in >>= 1;
				in += 64;

				bgra[c] = in;
			}
		}
	}
}


void D3D_MakeAlphaTexture (LPDIRECT3DTEXTURE9 tex);

void R_InitResourceTextures (void)
{
	// load the notexture properly
	// D3D_UploadTexture (&r_notexture, (byte *) (r_notexture_mip + 1), r_notexture_mip->size[0], r_notexture_mip->size[1], IMAGE_MIPMAP);
	r_notexture_mip->teximage = D3D_LoadTexture ("notexture", r_notexture_mip->size[0], r_notexture_mip->size[1], (byte *) (r_notexture_mip + 1), IMAGE_MIPMAP);
	r_notexture_mip->lumaimage = NULL;

	D3DMisc_CreatePalette ();
	D3DPart_OnRecover ();
	D3DWarp_WWTexOnRecover ();

	// load any textures contained in exe resources
	D3D_LoadResourceTexture ("crosshairs", &crosshairtexture, IDR_CROSSHAIR, IMAGE_ALPHA);
	D3D_LoadResourceTexture ("particles", &particletexture, IDR_PARTICLES, IMAGE_ALPHA);

	// convert them to alpha mask textures
	D3D_MakeAlphaTexture (crosshairtexture);
	D3D_MakeAlphaTexture (particletexture);

	byte shade[256][4];

	for (int i = 0; i < 256; i++)
	{
		float f = (float) i / 127.5f;

		// wtf - this reproduces anorm_dots within as reasonable a degree of tolerance as the >= 0 case 
		if (f < 1)
		{
			f -= 1.0f;
			f *= (13.0f / 44.0f);
			f += 1.0f;
		}

		f *= 127.5f;

		shade[i][0] = BYTE_CLAMP (f);
		shade[i][1] = BYTE_CLAMP (f);
		shade[i][2] = BYTE_CLAMP (f);
		shade[i][3] = BYTE_CLAMP (f);
	}

	// hmmm - this means using a fifth texture for MDLs - maybe branching would be OK after all????
	D3D_UploadTexture (&shadetexture, shade, 256, 1, IMAGE_32BIT);
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

void R_Init (void)
{
	R_InitParticles ();
	R_InitResourceTextures ();
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
void Fog_ParseWorldspawn (void);
void IN_ClearMouseState (void);
void D3DAlias_CreateBuffers (void);
void D3DAlpha_NewMap (void);
void Mod_InitForMap (model_t *mod);
void D3DBrush_CreateVBOs (void);
void D3DLight_EndBuildingLightmaps (void);
void Host_ResetFixedTime (void);
void D3DSurf_BuildWorldCache (void);
void ClearAllStates (void);;

void R_NewMap (void)
{
	SAFE_DELETE (RenderZone);
	RenderZone = new CQuakeZone ();

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
	D3DAlpha_NewMap ();
	Fog_ParseWorldspawn ();
	D3DAlias_CreateBuffers ();

	// as sounds are now cleared between maps these sounds also need to be
	// reloaded otherwise the known_sfx will go out of sequence for them
	// (this isn't the case any more but it does no harm)
	CL_InitTEnts ();
	S_InitAmbients ();
	LOC_LoadLocations ();
	D3DBrush_CreateVBOs ();
	D3DSurf_BuildWorldCache ();

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
	ClearAllStates ();
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
	Host_ResetFixedTime ();

	// we're running a map now
	cls.maprunning = true;
}


