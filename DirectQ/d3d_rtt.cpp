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
#include "d3d_vbo.h"

LPDIRECT3DSURFACE9 d3d_RTTSurface = NULL;
LPDIRECT3DTEXTURE9 d3d_RTTTexture = NULL;
LPD3DXRENDERTOSURFACE d3d_RTTRender = NULL;
D3DDISPLAYMODE d3d_RTTMode;
D3DDISPLAYMODE *d3d_ActiveMode = NULL;

/*
==============================================================================================================================

		RENDER TO TEXTURE

==============================================================================================================================
*/

bool SceneTextureValid = false;
cvar_t r_waterwarp ("r_waterwarp", 1, CVAR_ARCHIVE);
bool d3d_Update3DScene = false;


void D3D_Kill3DSceneTexture (void)
{
	SAFE_RELEASE (d3d_RTTSurface);
	SAFE_RELEASE (d3d_RTTTexture);
	SAFE_RELEASE (d3d_RTTRender);
}


void D3D_SetRTTMode (D3DDISPLAYMODE *mode)
{
	// width and height are two thirds the current mode
	// this gets perf with rtt about the same as perf without at 800 x 600
	mode->Width = ((d3d_CurrentMode.Width * 4 / 5) + 7) & ~7;
	mode->Height = ((d3d_CurrentMode.Height * 4 / 5) + 7) & ~7;

	// just for completeness
	mode->Format = d3d_CurrentMode.Format;
	mode->RefreshRate = d3d_CurrentMode.RefreshRate;
}


void D3D_Init3DSceneStuff (void)
{
	// update the mode we're going to use
	D3D_SetRTTMode (&d3d_RTTMode);

	// setup isn't valid yet
	D3D_Kill3DSceneTexture ();

	// rendertargets seem to relax power of 2 requirements, best of luck finding THAT in the documentation
	hr = d3d_Device->CreateTexture
	(
		d3d_RTTMode.Width,
		d3d_RTTMode.Height,
		1,
		D3DUSAGE_RENDERTARGET,
		d3d_CurrentMode.Format,
		D3DPOOL_DEFAULT,
		&d3d_RTTTexture,
		NULL
	);

	// couldn't create it so don't do underwater updates
	if (FAILED (hr))
	{
		// destroy anything that was created
		D3D_Kill3DSceneTexture ();
		return;
	}

	hr = QD3DXCreateRenderToSurface
	(
		d3d_Device,
		d3d_RTTMode.Width,
		d3d_RTTMode.Height,
		d3d_CurrentMode.Format,
		TRUE,
		d3d_GlobalCaps.DepthStencilFormat,
		&d3d_RTTRender
	);

	// couldn't create it so don't do underwater updates
	if (FAILED (hr))
	{
		// destroy anything that was created
		D3D_Kill3DSceneTexture ();
		return;
	}

	// get the surface that we're going to render to
	hr = d3d_RTTTexture->GetSurfaceLevel (0, &d3d_RTTSurface);

	// couldn't create it so don't do underwater updates
	if (FAILED (hr))
	{
		// destroy anything that was created
		D3D_Kill3DSceneTexture ();
		return;
	}
}


void D3D_Init3DSceneTexture (void)
{
	// ensure that it's all gone before creating
	D3D_Kill3DSceneTexture ();

	// assume that it's invalid until we prove otherwise
	SceneTextureValid = false;

	D3D_Init3DSceneStuff ();

	// if both failed we don't use screen updates at all
	if (!d3d_RTTRender) return;

	// we're good now
	SceneTextureValid = true;
}


bool IsUnderwater (void)
{
	extern cshift_t cshift_empty;

	// sanity check
	if (!d3d_RenderDef.viewleaf) return false;

	// 2 is a valid value here but we don't want to warp with it
	if (r_waterwarp.integer != 1) return false;

	// no warps present on these leafs
	if ((d3d_RenderDef.viewleaf->contents == CONTENTS_EMPTY ||
		d3d_RenderDef.viewleaf->contents == CONTENTS_SKY ||
		d3d_RenderDef.viewleaf->contents == CONTENTS_SOLID) &&
		!cl.inwater) return false;

	// additional check for noclipping
	if (d3d_RenderDef.viewleaf->contents == CONTENTS_EMPTY && cshift_empty.percent == 0) return false;

	// skip the first few frames because we get invalid viewleaf contents in them
	if (d3d_RenderDef.framecount < 3) return false;

	// we're underwater now
	return true;
}


bool d3d_IsUnderwater = false;

bool D3D_Begin3DScene (void)
{
	d3d_ActiveMode = &d3d_CurrentMode;
	d3d_IsUnderwater = false;

	// we already got a beginscene from somewhere
	if (d3d_SceneBegun) return false;

	// couldn't create the underwater texture or we don't want to warp
	if (!SceneTextureValid) return false;

	// no viewleaf yet
	if (!d3d_RenderDef.viewleaf) return false;

	d3d_IsUnderwater = IsUnderwater ();

	if (d3d_RenderDef.automap) return false;

	// 10 PRINT "HELLO"
	// 20 GOTO 10
	if (d3d_IsUnderwater) goto doRTT;

	// you should have seen the mess before I put in the goto
	if (!v_blend[3]) return false;
	if (!gl_polyblend.integer) return false;

doRTT:;
	// begin the render to texture scene
	if (FAILED (hr = d3d_RTTRender->BeginScene (d3d_RTTSurface, NULL))) return false;

	// flag that we need to draw the warp update
	d3d_Update3DScene = true;

	// select it as the active mode
	d3d_ActiveMode = &d3d_RTTMode;

	// we're rendering to texture now
	return true;
}


cvar_t r_waterwarptime ("r_waterwarptime", 2.0f, CVAR_ARCHIVE);
cvar_t r_waterwarpscale ("r_waterwarpscale", 0.125f, CVAR_ARCHIVE);
cvar_t r_waterwarptess ("r_waterwarptess", 32.0f, CVAR_ARCHIVE);

void D3DRTT_Callback (void *blah)
{
	// now we end the rtt scene and definitively begin the main scene
	// this is a FUCK of a lot of state... would a stateblock be better here?
	d3d_RTTRender->EndScene (D3DX_FILTER_LINEAR);
	d3d_Device->BeginScene ();
	d3d_SceneBegun = true;

	// ensure
	D3D_DisableAlphaBlend ();

	// disable depth testing and writing
	D3D_SetRenderState (D3DRS_ZENABLE, D3DZB_FALSE);
	D3D_SetRenderState (D3DRS_ZWRITEENABLE, FALSE);

	D3D_SetViewport (0, 0, d3d_CurrentMode.Width, d3d_CurrentMode.Height, 0, 1);
	D3D_SetVertexDeclaration (d3d_VDXyzDiffuseTex1);

	D3D_SetTextureAddressMode (D3DTADDRESS_CLAMP);

	// our projection should be the same dimensions as the rtt texture viewport
	D3DXMATRIX m;
	D3D_LoadIdentity (&m);
	QD3DXMatrixOrthoOffCenterRH (&m, 0, d3d_CurrentMode.Width, d3d_CurrentMode.Height, 0, 0, 1);

	if (d3d_GlobalCaps.usingPixelShaders)
	{
		if (d3d_FXPass != FX_PASS_NOTBEGUN)
			D3D_EndShaderPass ();

		// need a linear filter because we're now drawing smaller that the screen size
		D3D_SetTextureMipmap (1, D3DTEXF_LINEAR, D3DTEXF_NONE);

		d3d_MasterFX->SetMatrix ("WorldMatrix", &m);
		d3d_MasterFX->SetTexture ("tmu0Texture", d3d_RTTTexture);

		D3D_BeginShaderPass (FX_PASS_RTT);
	}
	else
	{
		d3d_Device->SetTransform (D3DTS_PROJECTION, &m);

		D3D_LoadIdentity (&m);
		d3d_Device->SetTransform (D3DTS_VIEW, &m);

		D3D_LoadIdentity (&m);
		d3d_Device->SetTransform (D3DTS_WORLD, &m);

		// need a linear filter because we're now drawing smaller that the screen size
		D3D_SetTextureMipmap (0, D3DTEXF_LINEAR, D3DTEXF_NONE);

		D3D_SetTextureColorMode (0, D3DTOP_BLENDDIFFUSEALPHA, D3DTA_TEXTURE, D3DTA_DIFFUSE);
		D3D_SetTextureAlphaMode (0, D3DTOP_DISABLE);

		D3D_SetTextureColorMode (1, D3DTOP_DISABLE);
		D3D_SetTextureAlphaMode (1, D3DTOP_DISABLE);

		D3D_SetTextureColorMode (2, D3DTOP_DISABLE);
		D3D_SetTextureAlphaMode (2, D3DTOP_DISABLE);

		D3D_SetTexture (0, d3d_RTTTexture);
	}
}


void D3D_EndRTTCallback (void *blah)
{
	if (d3d_GlobalCaps.usingPixelShaders)
		D3D_EndShaderPass ();
}


void D3D_End3DScene (void)
{
	// no warps
	if (!d3d_Update3DScene) return;

	// for the next frame
	d3d_Update3DScene = false;

	// restore the correct mode
	d3d_ActiveMode = &d3d_CurrentMode;

	VBO_AddCallback (D3DRTT_Callback);

	// blend by polyblend colour
	D3DCOLOR blend = D3DCOLOR_ARGB
	(
	// invert alpha so that we can use D3DTA_TEXTURE, D3DTA_DIFFUSE above
		BYTE_CLAMP ((255 - v_blend[3])),
		BYTE_CLAMP (v_blend[0]),
		BYTE_CLAMP (v_blend[1]),
		BYTE_CLAMP (v_blend[2])
	);

	// inverted alpha range for D3DTOP_BLENDDIFFUSEALPHA means that 00 is full opaque
	if (!gl_polyblend.integer) blend = 0xffffffff;

	if (d3d_IsUnderwater)
	{
		// bound tess factor
		if (r_waterwarptess.integer < 2) Cvar_Set (&r_waterwarptess, 2);
		if (r_waterwarptess.integer > 64) Cvar_Set (&r_waterwarptess, 64);

		// submit to render
		VBO_RTTWarpScreen (r_waterwarptess.integer, cl.time * r_waterwarptime.value, blend);
	}
	else
	{
		// submit
		VBO_RTTBlendScreen (blend);
	}

	// disable the polyblend because we're blending it with the texture
	v_blend[3] = 0;

	VBO_AddCallback (D3D_EndRTTCallback);
}


