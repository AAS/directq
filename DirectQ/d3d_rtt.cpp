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
#include "d3d_Quads.h"


extern LPDIRECT3DTEXTURE9 d3d_WWTexture;
extern D3DPRESENT_PARAMETERS d3d_PresentParams;


/*
==============================================================================================================================

		UNDERWATER WARP

==============================================================================================================================
*/


LPDIRECT3DTEXTURE9 d3d_RTTTexture = NULL;
LPDIRECT3DSURFACE9 d3d_RTTSurface = NULL;
LPDIRECT3DSURFACE9 d3d_MainBackBuffer = NULL;
LPDIRECT3DSURFACE9 d3d_RTTDepthBuffer = NULL;
LPDIRECT3DSURFACE9 d3d_MainDepthBuffer = NULL;

void D3DRTT_SetVertex (quadvert_t *dst, float x, float y, DWORD color)
{
	// correct the half-pixel offset
	dst->xyz[0] = x;
	dst->xyz[1] = y;
	dst->xyz[2] = 0;
	dst->color = color;
}


int D3DRTT_RescaleDimension (int dim)
{
	/*
	// take a power of 2 equal to or below the requested resolution
	// this needs to be re-evaluated at runtime every time as sb_lines can change
	// (although it can probably go into recalc_refdef just as easily...)
	for (int i = 1; ; i <<= 1)
	{
		if (i > dim) return (i >> 1);
		if (i == dim) return i;
	}
	*/

	// never reached; keep compiler happy
	return dim;
}


void D3DRTT_CreateRTTTexture (void)
{
	hr = d3d_Device->CreateTexture
	(
		D3DRTT_RescaleDimension (d3d_CurrentMode.Width),
		D3DRTT_RescaleDimension (d3d_CurrentMode.Height),
		1,
		D3DUSAGE_RENDERTARGET,
		D3DFMT_X8R8G8B8,
		D3DPOOL_DEFAULT,
		&d3d_RTTTexture,
		NULL
	);

	if (FAILED (hr) || !d3d_RTTTexture)
	{
		SAFE_RELEASE (d3d_RTTTexture);
		return;
	}

	// tough shit; we can't create a texture with D3DUSAGE_RENDERTARGET and multisampling so we don't...
	hr = d3d_Device->CreateDepthStencilSurface
	(
		D3DRTT_RescaleDimension (d3d_CurrentMode.Width),
		D3DRTT_RescaleDimension (d3d_CurrentMode.Height),
		d3d_PresentParams.AutoDepthStencilFormat,
		D3DMULTISAMPLE_NONE,
		0,
		TRUE,
		&d3d_RTTDepthBuffer,
		NULL
	);

	if (FAILED (hr) || !d3d_RTTTexture)
	{
		SAFE_RELEASE (d3d_RTTTexture);
		SAFE_RELEASE (d3d_RTTDepthBuffer);
		return;
	}
}


void D3DRTT_ReleaseRTTTexture (void)
{
	SAFE_RELEASE (d3d_MainDepthBuffer);
	SAFE_RELEASE (d3d_RTTDepthBuffer);
	SAFE_RELEASE (d3d_MainBackBuffer);
	SAFE_RELEASE (d3d_RTTSurface);
	SAFE_RELEASE (d3d_RTTTexture);
}


CD3DDeviceLossHandler d3d_RTTHandler (D3DRTT_ReleaseRTTTexture, D3DRTT_CreateRTTTexture);


cvar_t r_waterwarp ("r_waterwarp", 1, CVAR_ARCHIVE);
cvar_t r_waterwarpspeed ("r_waterwarpspeed", 1.0f, CVAR_ARCHIVE);
cvar_t r_waterwarpscale ("r_waterwarpscale", 16.0f, CVAR_ARCHIVE);
cvar_t r_waterwarpfactor ("r_waterwarpfactor", 12.0f, CVAR_ARCHIVE);

void D3DRTT_BeginScene (void)
{
	d3d_RenderDef.RTT = false;

	// ensure that this fires only if we're actually running a map
	if (!cls.maprunning) return;

	// check if we're underwater first
	if (r_waterwarp.integer != 666)
	{
		if (r_waterwarp.value > 1) return;

		// r_refdef.vieworigin is set in V_RenderView so it can be reliably tested here
		// fixme - only eval this once
		mleaf_t *viewleaf = Mod_PointInLeaf (r_refdef.vieworigin, cl.worldmodel);

		// these content types don't have a warp
		// we can't check cl.inwater because it's true if partially submerged and it may have some latency from the server
		if (viewleaf->contents == CONTENTS_EMPTY) return;
		if (viewleaf->contents == CONTENTS_SOLID) return;
		if (viewleaf->contents == CONTENTS_SKY) return;
	}

	// ensure that we can do this
	if (!d3d_RTTTexture)
	{
		// switch to alternate warp
		Cvar_Set (&r_waterwarp, 2.0f);
		return;
	}

	// testing
	// Con_Printf ("doing rtt\n");
	d3d_RenderDef.RTT = true;

	// store out the original backbuffer
	d3d_Device->GetRenderTarget (0, &d3d_MainBackBuffer);
	d3d_Device->GetDepthStencilSurface (&d3d_MainDepthBuffer);

	// switch the render target
	d3d_RTTTexture->GetSurfaceLevel (0, &d3d_RTTSurface);
	d3d_Device->SetRenderTarget (0, d3d_RTTSurface);
	d3d_Device->SetDepthStencilSurface (d3d_RTTDepthBuffer);
}


void D3DRTT_EndScene (void)
{
	// ensure that we're set up for rendering
	if (!d3d_MainBackBuffer) return;
	if (!d3d_RTTSurface) return;

	// end rendering to the RTT texture
	d3d_Device->EndScene ();

	// restore the original render target
	d3d_Device->SetRenderTarget (0, d3d_MainBackBuffer);
	d3d_Device->SetDepthStencilSurface (d3d_MainDepthBuffer);

	// release our other objects
	SAFE_RELEASE (d3d_RTTSurface);
	SAFE_RELEASE (d3d_MainBackBuffer);
	SAFE_RELEASE (d3d_MainDepthBuffer);

	// begin rendering on the original render target
	d3d_Device->BeginScene ();

	// no alpha blending
	D3DState_SetAlphaBlend (FALSE);

	// the 3rd texture needs to wrap as it's drawn shrunken and controls the sine warp
	D3D_SetTextureAddress (0, D3DTADDRESS_CLAMP);
	D3D_SetTextureAddress (1, D3DTADDRESS_CLAMP);
	D3D_SetTextureAddress (2, D3DTADDRESS_WRAP);

	// set up shader for drawing
	D3DHLSL_SetTexture (0, d3d_RTTTexture);
	D3DHLSL_SetTexture (1, d3d_WWTexture);	// edge biasing
	D3DHLSL_SetTexture (2, d3d_WWTexture);	// warping

	// even the baseline RTT texture needs linear filtering as the texcoords will be offset by a sine warp
	D3D_SetTextureMipmap (0, D3DTEXF_LINEAR, D3DTEXF_NONE);
	D3D_SetTextureMipmap (1, D3DTEXF_LINEAR, D3DTEXF_NONE);
	D3D_SetTextureMipmap (2, D3DTEXF_LINEAR, D3DTEXF_NONE);

	D3DHLSL_SetPass (FX_PASS_UNDERWATER);

	// this is percentage of height to use for the t coord
	float th = (float) D3DRTT_RescaleDimension (vid.ref3dsize.height) / (float) D3DRTT_RescaleDimension (d3d_CurrentMode.Height);

	float UnderwaterTexMult[2];

	UnderwaterTexMult[0] = 1.0f;
	UnderwaterTexMult[1] = 1.0f / th;

	D3DHLSL_SetFloatArray ("UnderwaterTexMult", UnderwaterTexMult, 2);
	D3DHLSL_SetFloat ("warptime", cl.time * r_waterwarpspeed.value * 0.666f);

	// prevent division by 0 in the shader
	if (r_waterwarpscale.value < 1) Cvar_Set (&r_waterwarpscale, 1.0f);
	if (r_waterwarpfactor.value < 1) Cvar_Set (&r_waterwarpfactor, 1.0f);

	// rescale to the approx same range as software quake
	float warpscale = (r_waterwarpscale.value * 7.0f) / 10.0f;

	float Scale[3] =
	{
		// this is the correct value to divide the rtt texture into squares as sbheight has already been subtracted from
		// the incoming texcoord
		warpscale * (float) d3d_CurrentMode.Width / (float) d3d_CurrentMode.Height * 0.2f,
		warpscale * 0.2f,
		(r_waterwarpfactor.value / 1000.0f)
	};

	D3DHLSL_SetFloatArray ("Scale", Scale, 3);

	// default blend for when there is no v_blend
	DWORD blendcolor = D3DCOLOR_ARGB (0, 255, 255, 255);

	// and add the v_blend if there is one
	if (v_blend[3] && gl_polyblend.value > 0.0f)
	{
		float alpha = (float) v_blend[3] * gl_polyblend.value;

		blendcolor = D3DCOLOR_ARGB
		(
			BYTE_CLAMP (alpha),
			BYTE_CLAMP (v_blend[0]),
			BYTE_CLAMP (v_blend[1]),
			BYTE_CLAMP (v_blend[2])
		);
	}

	quadvert_t *verts = NULL;

	D3DQuads_Begin (1, &verts);

	// update our RTT verts
	D3DRTT_SetVertex (&verts[0], -1, 1, blendcolor);
	D3DRTT_SetVertex (&verts[1], 1, 1, blendcolor);
	D3DRTT_SetVertex (&verts[2], 1, 1.0f - (th * 2.0f), blendcolor);
	D3DRTT_SetVertex (&verts[3], -1, 1.0f - (th * 2.0f), blendcolor);

	D3DQuads_End ();

	// disable polyblend because we're going to get it for free with RTT
	v_blend[3] = 0;
}


