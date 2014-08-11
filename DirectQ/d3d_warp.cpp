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


LPDIRECT3DTEXTURE9 d3d_WWTexture = NULL;


byte D3DWarp_SineWarp (int num, int detail)
{
	float ang = (num % detail);

	ang /= (float) detail;
	ang *= 360.0f;

	ang = sin (D3DXToRadian (ang));
	ang += 1.0f;
	ang /= 2.0f;

	return BYTE_CLAMPF (ang);
}


byte D3DWarp_EdgeWarp (int num, int detail)
{
	float ang = (num % detail);

	// 0..0 range
	ang /= (float) (detail - 1);
	ang *= 180.0f;

	// extreme clamp to force the bounding regions to the edges
	ang = sin (D3DXToRadian (ang));
	ang *= 4.0f;

	return BYTE_CLAMPF (ang);
}


void D3DWarp_CreateWWTex (int detaillevel)
{
	if (!d3d_Device) return;

	int detail;

	if (!d3d_GlobalCaps.supportNonPow2)
		for (detail = 1; detail < detaillevel; detail <<= 1);
	else detail = (detaillevel + 3) & ~3;

	if (detail < 8) detail = 8;
	if (detail > 256) detail = 256;
	if (detail > d3d_DeviceCaps.MaxTextureWidth) detail = d3d_DeviceCaps.MaxTextureWidth;
	if (detail > d3d_DeviceCaps.MaxTextureHeight) detail = d3d_DeviceCaps.MaxTextureHeight;

	// protect against multiple calls
	SAFE_RELEASE (d3d_WWTexture);

	hr = d3d_Device->CreateTexture (detail, detail, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &d3d_WWTexture, NULL);
	if (FAILED (hr)) Sys_Error ("D3DWarp_CreateWWTex : d3d_Device->CreateTexture failed");

	D3DLOCKED_RECT lockrect;

	d3d_WWTexture->LockRect (0, &lockrect, NULL, d3d_GlobalCaps.DefaultLock);

	byte *bgra = (byte *) lockrect.pBits;

	for (int y = 0; y < detail; y++)
	{
		for (int x = 0; x < detail; x++, bgra += 4)
		{
			bgra[0] = D3DWarp_EdgeWarp (x, detail);
			bgra[1] = D3DWarp_SineWarp (x, detail);
			bgra[2] = D3DWarp_SineWarp (y, detail);
			bgra[3] = D3DWarp_EdgeWarp (y, detail);
		}
	}

	d3d_WWTexture->UnlockRect (0);
	// D3DXSaveTextureToFile ("wwtex.png", D3DXIFF_PNG, d3d_WWTexture, NULL);
}


void D3DWarp_SetWaterQuality (cvar_t *var)
{
	D3DWarp_CreateWWTex (var->integer * 8);
}


// fitz compatibility, although it does slightly different stuff in DirectQ
cvar_t r_waterquality ("r_waterquality", "8", CVAR_ARCHIVE, D3DWarp_SetWaterQuality);

void D3DWarp_WWTexOnLoss (void)
{
	SAFE_RELEASE (d3d_WWTexture);
}


void D3DWarp_WWTexOnRecover (void)
{
	D3DWarp_CreateWWTex (r_waterquality.integer * 8);
}


CD3DDeviceLossHandler d3d_WWTexHandler (D3DWarp_WWTexOnLoss, D3DWarp_WWTexOnRecover);

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

// for nehahra and anyone who wants them...
cvar_t r_waterripple ("r_waterripple", 0.0f, CVAR_ARCHIVE);
cvar_t r_nowaterripple ("r_nowaterripple", 1.0f, CVAR_ARCHIVE);

// lock water/slime/tele/lava alpha sliders
cvar_t r_lockalpha ("r_lockalpha", "1");

float warptime = 10;
float ripplefactors[2] = {0, 0};

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

	// set the warp time and factor (moving this calculation out of a loop)
	warptime = cl.time * 10.18591625f * r_warpspeed.value;

	// all of these below are premultiplied by various factors to save on PS instructions
	// some day I'll #define them properly so that we don't have scary magic numbers all over the place
	D3DHLSL_SetFloat ("warptime", warptime * 0.0039215f);
	D3DHLSL_SetFloat ("warpfactor", r_warpfactor.value * 64.0f * 0.0039215f);
	D3DHLSL_SetFloat ("warpscale", r_warpscale.value * 2.0f * (1.0f / 64.0f));

	// determine if we're rippling
	if (r_waterripple.value && !r_nowaterripple.value)
	{
		ripplefactors[0] = r_waterripple.value;
		ripplefactors[1] = cl.time * 3.0f;
	}
	else
	{
		ripplefactors[0] = 0;
		ripplefactors[1] = 0;
	}

	D3DHLSL_SetFloatArray ("ripple", ripplefactors, 2);
}


LPDIRECT3DTEXTURE9 previouswarptexture = NULL;
int previouswarpalpha = -1;

cvar_t r_mipwater ("r_mipwater", "1", CVAR_ARCHIVE);

void D3DWarp_SetupTurbState (void)
{
	// how anyone could think this looks better is beyond me, but oh well, people do...
	// (I suppose it's OK for teleports, but making the general case suffer in order to handle a special case seems iffy)
	if (r_mipwater.integer)
		D3D_SetTextureMipmap (0, d3d_TexFilter, d3d_MipFilter);
	else D3D_SetTextureMipmap (0, d3d_TexFilter, D3DTEXF_NONE);

	D3D_SetTextureMipmap (1, D3DTEXF_LINEAR, D3DTEXF_LINEAR);
	D3D_SetTextureAddress (0, D3DTADDRESS_WRAP);
	D3D_SetTextureAddress (1, D3DTADDRESS_WRAP);

	D3DHLSL_SetTexture (1, d3d_WWTexture);	// warping

	D3DBrush_Begin ();

	if (r_waterripple.value && !r_nowaterripple.value)
		D3DHLSL_SetPass (FX_PASS_LIQUID_RIPPLE);
	else D3DHLSL_SetPass (FX_PASS_LIQUID);

	previouswarptexture = NULL;
	previouswarpalpha = -1;
}


void D3DWarp_TakeDownTurbState (void)
{
	D3DBrush_FlushSurfaces ();
	D3DBrush_End ();
}


void D3DWarp_DrawSurface (msurface_t *surf, entity_t *ent)
{
	byte thisalpha = 255;

	// automatic alpha always
	if (surf->flags & SURF_DRAWWATER) thisalpha = d3d_WaterAlpha;
	if (surf->flags & SURF_DRAWLAVA) thisalpha = d3d_LavaAlpha;
	if (surf->flags & SURF_DRAWTELE) thisalpha = d3d_TeleAlpha;
	if (surf->flags & SURF_DRAWSLIME) thisalpha = d3d_SlimeAlpha;

	// entity override
	if (ent && (ent->alphaval > 0 && ent->alphaval < 255)) thisalpha = ent->alphaval;

	// precheck is done before any other state changes
	D3DBrush_PrecheckSurface (surf, ent);

	// check for a texture or alpha change
	// (either of these will trigger the recommit above)
	if (surf->texinfo->texture->teximage->d3d_Texture != previouswarptexture)
	{
		D3DBrush_FlushSurfaces ();
		D3DHLSL_SetTexture (0, surf->texinfo->texture->teximage->d3d_Texture);
		previouswarptexture = surf->texinfo->texture->teximage->d3d_Texture;
	}

	if (thisalpha != previouswarpalpha)
	{
		D3DBrush_FlushSurfaces ();
		D3DHLSL_SetAlpha ((float) thisalpha / 255.0f);
		previouswarpalpha = thisalpha;
	}

	D3DBrush_BatchSurface (surf);
}


void D3DSurf_EmitSurfToAlpha (msurface_t *surf, entity_t *ent);

void D3DWarp_DrawWaterSurfaces (brushhdr_t *hdr, msurface_t *chain, entity_t *ent)
{
	bool stateset = false;

	// reverse the texture chain to front-to-back and sort by texture
	for (; chain; chain = chain->lightmapchain)
	{
		chain->texturechain = chain->texinfo->texture->texturechain;
		chain->texinfo->texture->texturechain = chain;
	}

	// now draw it all
	for (int i = 0; i < hdr->numtextures; i++)
	{
		texture_t *tex;

		if (!(tex = hdr->textures[i])) continue;
		if (!(chain = tex->texturechain)) continue;

		for (; chain; chain = chain->texturechain)
		{
			// skip over alpha surfaces - automatic skip unless the surface explicitly overrides it (same path)
			if ((chain->flags & SURF_DRAWLAVA) && d3d_LavaAlpha < 255) {D3DSurf_EmitSurfToAlpha (chain, ent); continue;}
			if ((chain->flags & SURF_DRAWTELE) && d3d_TeleAlpha < 255) {D3DSurf_EmitSurfToAlpha (chain, ent); continue;}
			if ((chain->flags & SURF_DRAWSLIME) && d3d_SlimeAlpha < 255) {D3DSurf_EmitSurfToAlpha (chain, ent); continue;}
			if ((chain->flags & SURF_DRAWWATER) && d3d_WaterAlpha < 255) {D3DSurf_EmitSurfToAlpha (chain, ent); continue;}

			// determine if we need a state update; this is just for setting the
			// initial state; we always update the texture on a texture change
			// we check in here because we might be skipping some of these surfaces if they have alpha set
			if (!stateset)
			{
				D3DWarp_SetupTurbState ();
				stateset = true;
			}

			// ent is NULL because it's already been transformed
			D3DWarp_DrawSurface (chain, NULL);
		}

		tex->texturechain = NULL;
	}

	// this is just a simple flush now; the full takedown (which includes resetting MVP) is reserved for alpha surfaces
	D3DBrush_FlushSurfaces ();
	// determine if we need to take down state
	//	if (stateset) D3DWarp_TakeDownTurbState ();
}


