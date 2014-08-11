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
void D3DBrush_Begin (void);
void D3DBrush_End (void);
void D3DBrush_FlushSurfaces (void);
void D3DBrush_SubmitSurface (msurface_t *surf, entity_t *ent);


LPDIRECT3DTEXTURE9 d3d_WaterWarpTexture = NULL;


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

	D3D_SetTextureMipmap (1, D3DTEXF_LINEAR, D3DTEXF_NONE);
	D3D_SetTextureAddress (0, D3DTADDRESS_WRAP);
	D3D_SetTextureAddress (1, D3DTADDRESS_WRAP);

	D3DHLSL_SetTexture (1, d3d_WaterWarpTexture);	// warping

	D3DBrush_Begin ();

	if (r_waterripple.value && !r_nowaterripple.value)
		D3DHLSL_SetPass (FX_PASS_LIQUID_RIPPLE);
	else D3DHLSL_SetPass (FX_PASS_LIQUID);

	previouswarptexture = NULL;
	previouswarpalpha = -1;
}


void D3DWarp_TakeDownTurbState (void)
{
	D3DBrush_End ();
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

		// transform the surface midpoint by the modelsurf matrix so that it goes into the proper place
		D3DMatrix_TransformPoint (&modelsurf->ent->matrix, midpoint, modelsurf->surf->midpoint);

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


void D3DWarp_DrawWaterSurfaces (d3d_modelsurf_t **chains, int numchains)
{
	bool stateset = false;

	for (int i = 0; i < numchains; i++)
	{
		if (!chains[i]) continue;

		for (d3d_modelsurf_t *ms = chains[i]; ms; ms = ms->next)
		{
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

		// NULL the chain here
		chains[i] = NULL;
	}

	// determine if we need to take down state
	if (stateset) D3DWarp_TakeDownTurbState ();
}


/*
==============================================================================================================================

		UNDERWATER WARP

==============================================================================================================================
*/


LPDIRECT3DTEXTURE9 d3d_RTTTexture = NULL;
LPDIRECT3DSURFACE9 d3d_RTTSurface = NULL;
LPDIRECT3DSURFACE9 d3d_RTTBackBuffer = NULL;
LPDIRECT3DVERTEXDECLARATION9 d3d_UnderwaterDecl = NULL;

typedef struct rttverts_s
{
	float xyz[3];
	D3DCOLOR c;
	float st1[2];
	float st2[2];
} rttverts_t;


rttverts_t d3d_RTTVerts[4];


void D3DRTT_SetVertex (rttverts_t *dst, float x, float y, float s1, float t1, float s2, float t2)
{
	// correct the half-pixel offset
	dst->xyz[0] = x;
	dst->xyz[1] = y;
	dst->xyz[2] = 0;

	dst->st1[0] = s1;
	dst->st1[1] = t1;

	dst->st2[0] = s2;
	dst->st2[1] = t2;
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
	int i = D3DRTT_RescaleDimension (480);
	i = D3DRTT_RescaleDimension (500);
	i = D3DRTT_RescaleDimension (512);
	i = D3DRTT_RescaleDimension (520);

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

	if (!d3d_UnderwaterDecl)
	{
		D3DVERTEXELEMENT9 d3d_underwaterlayout[] =
		{
			{0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
			{0, 12, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0},
			{0, 16, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
			{0, 24, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1},
			D3DDECL_END ()
		};

		hr = d3d_Device->CreateVertexDeclaration (d3d_underwaterlayout, &d3d_UnderwaterDecl);
		if (FAILED (hr)) Sys_Error ("D3DRTT_CreateRTTTexture: d3d_Device->CreateVertexDeclaration failed");
	}
}


void D3DRTT_ReleaseRTTTexture (void)
{
	SAFE_RELEASE (d3d_UnderwaterDecl);
	SAFE_RELEASE (d3d_RTTBackBuffer);
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
	if (cls.state != ca_connected) return;

	// check if we're underwater first
	if (r_waterwarp.integer != 666)
	{
		if (r_waterwarp.value > 1) return;

		// ha!  this causes a host_error when trying to connect online before a map is downloaded!  just bypass it for now
		if (!cl.worldmodel) return;
		if (!cl.worldmodel->brushhdr->nodes) return;

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
	d3d_Device->GetRenderTarget (0, &d3d_RTTBackBuffer);

	// switch the render target
	d3d_RTTTexture->GetSurfaceLevel (0, &d3d_RTTSurface);
	d3d_Device->SetRenderTarget (0, d3d_RTTSurface);
}


void D3DRTT_EndScene (void)
{
	// ensure that we're set up for rendering
	if (!d3d_RTTBackBuffer) return;
	if (!d3d_RTTSurface) return;

	// end rendering to the RTT texture
	d3d_Device->EndScene ();

	// restore the original render target
	d3d_Device->SetRenderTarget (0, d3d_RTTBackBuffer);

	// release our other objects
	SAFE_RELEASE (d3d_RTTSurface);
	SAFE_RELEASE (d3d_RTTBackBuffer);

	// begin rendering on the original render target
	d3d_Device->BeginScene ();

	// actually draw it here!!!
	D3DMATRIX m;
	D3DMatrix_Identity (&m);
	D3DMatrix_OrthoOffCenterRH (&m, 0, d3d_CurrentMode.Width, d3d_CurrentMode.Height, 0, 0, 1);

	// no alpha blending
	D3D_SetRenderState (D3DRS_ALPHABLENDENABLE, FALSE);

	// the 3rd texture needs to wrap as it's drawn shrunken and controls the sine warp
	D3D_SetTextureAddress (0, D3DTADDRESS_CLAMP);
	D3D_SetTextureAddress (1, D3DTADDRESS_CLAMP);
	D3D_SetTextureAddress (2, D3DTADDRESS_WRAP);

	// set up shader for drawing
	D3DHLSL_SetWorldMatrix (&m);
	D3DHLSL_SetTexture (0, d3d_RTTTexture);
	D3DHLSL_SetTexture (1, d3d_WaterWarpTexture);	// edge biasing
	D3DHLSL_SetTexture (2, d3d_WaterWarpTexture);	// warping

	// even the baseline RTT texture needs linear filtering as the texcoords will be offset by a sine warp
	D3D_SetTextureMipmap (0, D3DTEXF_LINEAR, D3DTEXF_NONE);
	D3D_SetTextureMipmap (1, D3DTEXF_LINEAR, D3DTEXF_NONE);
	D3D_SetTextureMipmap (2, D3DTEXF_LINEAR, D3DTEXF_NONE);

	D3DHLSL_SetPass (FX_PASS_UNDERWATER);

	// unbind because we're going to use DPUP here
	D3D_UnbindStreams ();

	// default blend for when there is no v_blend
	DWORD blendcolor = D3DCOLOR_ARGB (0, 255, 255, 255);

	// and add the v_blend if there is one
	if (v_blend[3])
	{
		blendcolor = D3DCOLOR_ARGB
		(
			BYTE_CLAMP (v_blend[3]),
			BYTE_CLAMP (v_blend[0]),
			BYTE_CLAMP (v_blend[1]),
			BYTE_CLAMP (v_blend[2])
		);
	}

	// this is percentage of height to use for the t coord
	float th = (float) D3DRTT_RescaleDimension (vid.ref3dsize.height) / (float) D3DRTT_RescaleDimension (d3d_CurrentMode.Height);

	// update our RTT verts
	D3DRTT_SetVertex (&d3d_RTTVerts[0], 0, 0, 0, 0, 0, 0);
	D3DRTT_SetVertex (&d3d_RTTVerts[1], vid.ref3dsize.width, 0, 1, 0, 1, 0);
	D3DRTT_SetVertex (&d3d_RTTVerts[2], vid.ref3dsize.width, vid.ref3dsize.height, 1, th, 1, 1);
	D3DRTT_SetVertex (&d3d_RTTVerts[3], 0, vid.ref3dsize.height, 0, th, 0, 1);

	// set up the correct color for the RTT
	d3d_RTTVerts[0].c = blendcolor;
	d3d_RTTVerts[1].c = blendcolor;
	d3d_RTTVerts[2].c = blendcolor;
	d3d_RTTVerts[3].c = blendcolor;

	D3DHLSL_SetFloat ("warptime", cl.time * r_waterwarpspeed.value);

	// prevent division by 0 in the shader
	if (r_waterwarpscale.value < 1) Cvar_Set (&r_waterwarpscale, 1.0f);
	if (r_waterwarpfactor.value < 1) Cvar_Set (&r_waterwarpfactor, 1.0f);

	float Scale[3] =
	{
		// this is the correct value to divide the rtt texture into squares as sbheight has already been subtracted from
		// the incoming texcoord
		r_waterwarpscale.value * (float) d3d_CurrentMode.Width / (float) d3d_CurrentMode.Height * 0.2f,
		r_waterwarpscale.value * 0.2f,
		(r_waterwarpfactor.value / 1000.0f)
	};

	D3DHLSL_SetFloatArray ("Scale", Scale, 3);

	// ensure that everything is up to date
	D3D_SetVertexDeclaration (d3d_UnderwaterDecl);
	D3DHLSL_CheckCommit ();

	// maintaining a VBO for this seems too much like hard work so we won't
	d3d_Device->DrawPrimitiveUP (D3DPT_TRIANGLEFAN, 2, d3d_RTTVerts, sizeof (rttverts_t));
	d3d_RenderDef.numdrawprim++;

	// disable polyblend because we're going to get it for free with RTT
	v_blend[3] = 0;
}


