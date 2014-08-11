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

extern	model_t	*loadmodel;


/*
==============================================================================================================================

		WARP SURFACE GENERATION

==============================================================================================================================
*/

cvar_t gl_subdivide_size ("gl_subdivide_size", 128, CVAR_ARCHIVE);
CQuakeZone *TurbHeap = NULL;

void R_GenerateTurbSurface (model_t *mod, msurface_t *surf)
{
	// set up initial verts for the HLSL pass
	surf->numverts = surf->bspverts;
	brushpolyvert_t *surfverts = (brushpolyvert_t *) TurbHeap->Alloc (surf->numverts * sizeof (brushpolyvert_t));
	surf->verts = surfverts;

	// this pass is also good for getting the midpoint of the surf
	VectorClear (surf->midpoint);

	// copy out base verts for subdividing from
	for (int i = 0; i < surf->numverts; i++, surfverts++)
	{
		int lindex = mod->brushhdr->surfedges[surf->firstedge + i];

		if (lindex > 0)
		{
			surfverts->xyz[0] = mod->brushhdr->vertexes[mod->brushhdr->edges[lindex].v[0]].position[0];
			surfverts->xyz[1] = mod->brushhdr->vertexes[mod->brushhdr->edges[lindex].v[0]].position[1];
			surfverts->xyz[2] = mod->brushhdr->vertexes[mod->brushhdr->edges[lindex].v[0]].position[2];
		}
		else
		{
			surfverts->xyz[0] = mod->brushhdr->vertexes[mod->brushhdr->edges[-lindex].v[1]].position[0];
			surfverts->xyz[1] = mod->brushhdr->vertexes[mod->brushhdr->edges[-lindex].v[1]].position[1];
			surfverts->xyz[2] = mod->brushhdr->vertexes[mod->brushhdr->edges[-lindex].v[1]].position[2];
		}

		// setup s/t (don't need to cache a spare copy with these but we do anyway)
		surfverts->st[0] = surfverts->st2[0] = DotProduct (surfverts->xyz, surf->texinfo->vecs[0]);
		surfverts->st[1] = surfverts->st2[1] = DotProduct (surfverts->xyz, surf->texinfo->vecs[1]);

		// accumulate into midpoint
		VectorAdd (surf->midpoint, surfverts->xyz, surf->midpoint);
	}

	// get final midpoint
	VectorScale (surf->midpoint, 1.0f / (float) surf->numverts, surf->midpoint);

	// regenerate the indexes
	surf->numindexes = (surf->numverts - 2) * 3;
	surf->indexes = (unsigned short *) TurbHeap->Alloc (surf->numindexes * sizeof (unsigned short));

	unsigned short *ndx = surf->indexes;

	for (int i = 2; i < surf->numverts; i++, ndx += 3)
	{
		ndx[0] = 0;
		ndx[1] = i - 1;
		ndx[2] = i;
	}
}


static void R_BoundPoly (int numverts, float *verts, vec3_t mins, vec3_t maxs)
{
	int		i, j;
	float	*v;

	mins[0] = mins[1] = mins[2] = 9999;
	maxs[0] = maxs[1] = maxs[2] = -9999;
	v = verts;

	for (i = 0; i < numverts; i++)
	{
		for (j = 0; j < 3; j++, v++)
		{
			if (*v < mins[j]) mins[j] = *v;
			if (*v > maxs[j]) maxs[j] = *v;
		}
	}
}


static void R_SubdividePolygon (msurface_t *surf, int numverts, float *verts, float *dstverts, unsigned short *dstindexes)
{
	vec3_t	    mins, maxs;
	vec3_t	    front[64], back[64];
	float	    dist[64];

	if (numverts > 60)
		Sys_Error ("SubdividePolygon: excessive numverts %i", numverts);

	R_BoundPoly (numverts, verts, mins, maxs);

	for (int i = 0; i < 3; i++)
	{
		float m = (mins[i] + maxs[i]) * 0.5;
		m = gl_subdivide_size.value * floor (m / gl_subdivide_size.value + 0.5);

		// don't subdivide
		if (maxs[i] - m < 8) continue;
		if (m - mins[i] < 8) continue;

		// prevent scratch buffer overflow
		if (surf->numverts >= 4096) continue;
		if (surf->numindexes >= 16384) continue;

		// cut it
		float *v = verts + i;

		for (int j = 0; j < numverts; j++, v += 3)
			dist[j] = *v - m;

		// wrap cases
		dist[numverts] = dist[0];
		v -= i;
		VectorCopy (verts, v);
		v = verts;

		int f = 0, b = 0;

		for (int j = 0; j < numverts; j++, v += 3)
		{
			if (dist[j] >= 0)
			{
				VectorCopy (v, front[f]);
				f++;
			}

			if (dist[j] <= 0)
			{
				VectorCopy (v, back[b]);
				b++;
			}

			if (dist[j] == 0 || dist[j + 1] == 0)
				continue;

			if ((dist[j] > 0) != (dist[j + 1] > 0))
			{
				// clip point
				float frac = dist[j] / (dist[j] - dist[j + 1]);

				for (int k = 0; k < 3; k++)
					front[f][k] = back[b][k] = v[k] + frac * (v[3 + k] - v[k]);

				f++;
				b++;
			}
		}

		// subdivide
		R_SubdividePolygon (surf, f, front[0], dstverts, dstindexes);
		R_SubdividePolygon (surf, b, back[0], dstverts, dstindexes);
		return;
	}

	// set up vertexes
	float *dst = dstverts + (surf->numverts * 3);

	// for now we're just copying out the position to a temp buffer; this will be done
	// for real (and texcoords will be generated) after we remove duplicate verts.
	for (int i = 0; i < numverts; i++, verts += 3, dst += 3)
		VectorCopy (verts, dst);

	// ideally we would just skip this step but we need the correct indexes initially
	unsigned short *ndx = dstindexes + surf->numindexes;

	for (int i = 2; i < numverts; i++, ndx += 3)
	{
		ndx[0] = surf->numverts;
		ndx[1] = surf->numverts + i - 1;
		ndx[2] = surf->numverts + i;
	}

	// accumulate
	surf->numverts += numverts;
	surf->numindexes += (numverts - 2) * 3;
}


void R_SubdivideSurface (model_t *mod, msurface_t *surf)
{
	vec3_t verts[64];

	// verts - we set a max of 4096 in the subdivision routine so all of this is safe
	unsigned short *dstindexes = (unsigned short *) scratchbuf;
	float *dstverts = (float *) (dstindexes + 16384);
	brushpolyvert_t *finalverts = (brushpolyvert_t *) (dstverts + 4096 * 3);

	// this pass is also good for getting the midpoint of the surf
	VectorClear (surf->midpoint);

	// copy out the verts for subdividing from
	for (int i = 0; i < surf->bspverts; i++)
	{
		int lindex = mod->brushhdr->surfedges[surf->firstedge + i];

		// no ; because of macro expansion!!!
		if (lindex > 0)
			VectorCopy (mod->brushhdr->vertexes[mod->brushhdr->edges[lindex].v[0]].position, verts[i])
		else VectorCopy (mod->brushhdr->vertexes[mod->brushhdr->edges[-lindex].v[1]].position, verts[i])

		// accumulate into midpoint
		VectorAdd (surf->midpoint, verts[i], surf->midpoint);
	}

	// get final midpoint
	VectorScale (surf->midpoint, 1.0f / (float) surf->bspverts, surf->midpoint);

	// nothing to see here yet
	surf->numverts = 0;
	surf->numindexes = 0;

	// subdivide the polygon
	R_SubdividePolygon (surf, surf->bspverts, verts[0], dstverts, dstindexes);

	// begin again at 0 verts
	surf->numverts = 0;

	// remove duplicate verts from the surface
	// (we could optimize these further for the vertex cache, but they're already mostly
	// optimal and doing so would only slow things down and make it complicated...)
	for (int i = 0; i < surf->numindexes; i++)
	{
		// retrieve the vert
		float *v = &dstverts[dstindexes[i] * 3];
		int vnum;

		for (vnum = 0; vnum < surf->numverts; vnum++)
		{
			// only verts need to be compared
			if (v[0] != finalverts[vnum].xyz[0]) continue;
			if (v[1] != finalverts[vnum].xyz[1]) continue;
			if (v[2] != finalverts[vnum].xyz[2]) continue;

			// already exists
			dstindexes[i] = vnum;
			break;
		}

		if (vnum == surf->numverts)
		{
			// new vert and index
			dstindexes[i] = surf->numverts;

			finalverts[surf->numverts].xyz[0] = v[0];
			finalverts[surf->numverts].xyz[1] = v[1];
			finalverts[surf->numverts].xyz[2] = v[2];

			// texcoord generation can be deferred to here...
			// cache the unwarped s/t in the second set of texcoords for reuse in warping
			finalverts[surf->numverts].st2[0] = DotProduct (finalverts[surf->numverts].xyz, surf->texinfo->vecs[0]);
			finalverts[surf->numverts].st2[1] = DotProduct (finalverts[surf->numverts].xyz, surf->texinfo->vecs[1]);
			surf->numverts++;
		}
	}

	// create dest buffers
	surf->verts = (brushpolyvert_t *) TurbHeap->Alloc (surf->numverts * sizeof (brushpolyvert_t));
	surf->indexes = (unsigned short *) TurbHeap->Alloc (surf->numindexes * sizeof (unsigned short));

	// copy from scratchbuf to destination
	Q_MemCpy (surf->verts, finalverts, surf->numverts * sizeof (brushpolyvert_t));
	Q_MemCpy (surf->indexes, dstindexes, surf->numindexes * sizeof (unsigned short));
}


static int last_subdiv = -1;

void D3D_InitSubdivision (void)
{
	// called on map load to force an initial subdivision
	last_subdiv = -1;
}


void Mod_CalcSurfaceBoundsAndExtents (model_t *mod, msurface_t *surf);

void D3D_SubdivideWater (void)
{
	// no need
	if (cls.state != ca_connected) return;

	// the pixel shader surfs may also need to be regenerated
	if (d3d_GlobalCaps.usingPixelShaders)
	{
		// this value indicates that the surf was generated for the hlsl warp
		if (last_subdiv != -666)
			goto genwarp;
		return;
	}

	// bound to prevent user silliness
	if (gl_subdivide_size.integer < 8) Cvar_Set (&gl_subdivide_size, 8);

	// unchanged
	if (gl_subdivide_size.integer == last_subdiv) return;

genwarp:;
	// delete previous
	SAFE_DELETE (TurbHeap);
	TurbHeap = new CQuakeZone ();

	// the player is model 0...
	for (int i = 1; i < MAX_MODELS; i++)
	{
		model_t *m = cl.model_precache[i];

		if (!m) break;
		if (m->type != mod_brush) continue;
		if (m->name[0] == '*') continue;

		brushhdr_t *hdr = m->brushhdr;
		msurface_t *surf = hdr->surfaces;

		for (int s = 0; s < hdr->numsurfaces; s++, surf++)
		{
			// turb only
			if (!(surf->flags & SURF_DRAWTURB)) continue;

			// generate the surface
			if (d3d_GlobalCaps.usingPixelShaders)
				R_GenerateTurbSurface (m, surf);
			else R_SubdivideSurface (m, surf);

			// needs to be run here as it will be invalid at load time!
			Mod_CalcSurfaceBoundsAndExtents (m, surf);
		}
	}

	// store back.  the hlsl warp needs to force an update of the surface if r_hlsl is switched off,
	// even if gl_subdivide_size doesn't change
	if (d3d_GlobalCaps.usingPixelShaders)
		last_subdiv = -666;
	else last_subdiv = gl_subdivide_size.integer;
}


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
float *r_warpsin = NULL;


void D3D_InitializeTurb (void)
{
	// to do - can we use this for underwater too?
	if (!r_warpsin)
	{
		r_warpsin = (float *) Zone_Alloc (256 * sizeof (float));

		for (int i = 0; i < 256; i++)
		{
			float f = (float) i / 40.743665f;

			// just take the sin so that we can multiply it by a variable warp factor
			r_warpsin[i] = sin (f);
		}
	}

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
	warptime = cl.time * 10.18591625f * r_warpspeed.value;

	// update once only in the master shader
	if (d3d_GlobalCaps.usingPixelShaders)
	{
		d3d_MasterFX->SetFloat ("warptime", warptime);
		d3d_MasterFX->SetFloat ("warpfactor", r_warpfactor.value);
		d3d_MasterFX->SetFloat ("warpscale", r_warpscale.value);
	}
}


int previouswarptexture = 0;
int previouswarpalpha = -1;

void D3DWarp_SetupCallback (void *blah)
{
	D3D_SetTextureMipmap (0, d3d_TexFilter, d3d_MipFilter);
	D3D_SetTextureAddressMode (D3DTADDRESS_WRAP);
	D3D_SetVertexDeclaration (d3d_VDXyzTex1);

	if (d3d_GlobalCaps.usingPixelShaders)
	{
		if (d3d_FXPass == FX_PASS_NOTBEGUN)
		{
			D3D_BeginShaderPass (FX_PASS_LIQUID);
		}
		else if (d3d_FXPass != FX_PASS_LIQUID)
		{
			D3D_EndShaderPass ();
			D3D_BeginShaderPass (FX_PASS_LIQUID);
		}
	}
	else
	{
		D3D_SetTexCoordIndexes (0);
		D3D_SetTextureColorMode (0, D3DTOP_SELECTARG1, D3DTA_TEXTURE, D3DTA_DIFFUSE);
		D3D_SetTextureColorMode (1, D3DTOP_DISABLE);
		D3D_SetTextureAlphaMode (1, D3DTOP_DISABLE);
	}
}


void D3D_SetupTurbState (void)
{
	VBO_AddCallback (D3DWarp_SetupCallback);

	previouswarptexture = 0;
	previouswarpalpha = -1;
}


void D3DWarp_TakeDownCallback (void *blah)
{
	if (d3d_GlobalCaps.usingPixelShaders)
		D3D_EndShaderPass ();
	else D3D_SetRenderState (D3DRS_TEXTUREFACTOR, 0xffffffff);
}


void D3D_TakeDownTurbState (void)
{
	VBO_AddCallback (D3DWarp_TakeDownCallback);
}


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
		D3D_AddToAlphaList (modelsurf);

		// restore the original midpoint
		modelsurf->surf->midpoint[0] = midpoint[0];
		modelsurf->surf->midpoint[1] = midpoint[1];
		modelsurf->surf->midpoint[2] = midpoint[2];
	}
	else
	{
		// just add it
		D3D_AddToAlphaList (modelsurf);
	}
}


void D3D_EmitModelSurfsToAlpha (d3d_modelsurf_t *surfchain)
{
	for (d3d_modelsurf_t *modelsurf = surfchain; modelsurf; modelsurf = modelsurf->surfchain)
	{
		// emit this surf (both automatic and explicit alpha)
		D3D_EmitModelSurfToAlpha (modelsurf);
	}
}


#define WARPCALC(s,t) ((s + r_warpsin[(int) ((t * 2) + (cl.time * 40.743665f)) & 255] * 8.0f) * 0.015625f)
#define WARPCALC2(s,t) ((s + r_warpsin[(int) ((t * 0.125 + cl.time) * 40.743665f) & 255] * 8.0f) * 0.015625f)

void D3DWarp_TextureChange (void *data)
{
	d3d_texturechange_t *tc = (d3d_texturechange_t *) data;

	if (d3d_GlobalCaps.usingPixelShaders)
		d3d_MasterFX->SetTexture ("tmu0Texture", tc->tex);
	else D3D_SetTexture (tc->stage, tc->tex);
}


void D3DWarp_AlphaChange (void *data)
{
	byte thisalpha = ((byte *) data)[0];

	if (d3d_GlobalCaps.usingPixelShaders)
		d3d_MasterFX->SetFloat ("AlphaVal", (float) thisalpha / 255.0f);
	else
	{
		if (thisalpha < 255)
			D3D_SetTextureAlphaMode (0, D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_TFACTOR);
		else D3D_SetTextureAlphaMode (0, D3DTOP_DISABLE);

		D3D_SetRenderState (D3DRS_TEXTUREFACTOR, D3DCOLOR_ARGB (BYTE_CLAMP (thisalpha), 255, 255, 255));
	}
}


void D3DWarp_HLSLCommit (void *blah)
{
	// to do - generalise this!!!
	d3d_FXCommitPending = true;
}


void D3D_EmitWarpSurface (d3d_modelsurf_t *modelsurf)
{
	msurface_t *surf = modelsurf->surf;
	image_t *tex = modelsurf->tex->teximage;
	bool recommit = false;
	byte thisalpha = 255;

	// check because we can get a frame update while the verts are NULL
	if (!modelsurf->surf->verts) return;

	d3d_RenderDef.brush_polys++;

	// automatic alpha
	if (surf->flags & SURF_DRAWWATER) thisalpha = d3d_WaterAlpha;
	if (surf->flags & SURF_DRAWLAVA) thisalpha = d3d_LavaAlpha;
	if (surf->flags & SURF_DRAWTELE) thisalpha = d3d_TeleAlpha;
	if (surf->flags & SURF_DRAWSLIME) thisalpha = d3d_SlimeAlpha;

	// explicit alpha
	if (surf->alphaval < 255) thisalpha = surf->alphaval;

	if (thisalpha != previouswarpalpha) recommit = true;
	if ((int) tex->d3d_Texture != previouswarptexture) recommit = true;

	// check for a texture or alpha change
	// (either of these will trigger the recommit above)
	if ((int) tex->d3d_Texture != previouswarptexture)
	{
		d3d_texturechange_t tc = {0, tex->d3d_Texture};
		VBO_AddCallback (D3DWarp_TextureChange, &tc, sizeof (d3d_texturechange_t));
		previouswarptexture = (int) tex->d3d_Texture;
	}

	if (thisalpha != previouswarpalpha)
	{
		VBO_AddCallback (D3DWarp_AlphaChange, &thisalpha, 1);
		previouswarpalpha = thisalpha;
	}

	// HLSL needs to commit a param change
	if (recommit && d3d_GlobalCaps.usingPixelShaders)
		VBO_AddCallback (D3DWarp_HLSLCommit);

	if (!d3d_GlobalCaps.usingPixelShaders)
	{
		/*
		why is this not in a shader?
		because it's the fallback mode for when shaders aren't available!
		*/
		brushpolyvert_t *src = surf->verts;

		// batch calculate the warp
		for (int i = 0; i < surf->numverts; i++, src++)
		{
			// the original s/t were stored in st2 during setup so now warp them into st
			if (gl_subdivide_size.integer > 48)
			{
				src->st[0] = WARPCALC2 (src->st2[0], src->st2[1]);
				src->st[1] = WARPCALC2 (src->st2[1], src->st2[0]);
			}
			else
			{
				src->st[0] = WARPCALC (src->st2[0], src->st2[1]);
				src->st[1] = WARPCALC (src->st2[1], src->st2[0]);
			}
		}
	}

	VBO_AddWarpSurf (surf, modelsurf->ent);
}


void D3D_DrawWaterSurfaces (void)
{
	bool stateset = false;

	// even if we're fully alpha we still pass through here so that we can add items to the alpha list
	for (int i = 0; i < d3d_NumRegisteredTextures; i++)
	{
		d3d_registeredtexture_t *rt = d3d_RegisteredTextures[i];

		// chains which are empty or non-turb are skipped over
		if (!rt->surfchain) continue;
		if (!(rt->surfchain->surf->flags & SURF_DRAWTURB)) continue;

		// skip over alpha surfaces - automatic skip unless the surface explicitly overrides it (same path)
		if ((rt->surfchain->surf->flags & SURF_DRAWLAVA) && d3d_LavaAlpha < 255) {D3D_EmitModelSurfsToAlpha (rt->surfchain); continue;}
		if ((rt->surfchain->surf->flags & SURF_DRAWTELE) && d3d_TeleAlpha < 255) {D3D_EmitModelSurfsToAlpha (rt->surfchain); continue;}
		if ((rt->surfchain->surf->flags & SURF_DRAWSLIME) && d3d_SlimeAlpha < 255) {D3D_EmitModelSurfsToAlpha (rt->surfchain); continue;}
		if ((rt->surfchain->surf->flags & SURF_DRAWWATER) && d3d_WaterAlpha < 255) {D3D_EmitModelSurfsToAlpha (rt->surfchain); continue;}

		// now draw all surfaces with this texture
		for (d3d_modelsurf_t *modelsurf = rt->surfchain; modelsurf; modelsurf = modelsurf->surfchain)
		{
			// determine if we need a state update; this is just for setting the
			// initial state; we always update the texture on a texture change
			// we check in here because we might be skipping some of these surfaces if they have alpha set
			if (!stateset)
			{
				D3D_SetupTurbState ();
				stateset = true;
			}

			// this one used for both alpha and non-alpha
			D3D_EmitWarpSurface (modelsurf);
		}
	}

	// determine if we need to take down state
	if (stateset) D3D_TakeDownTurbState ();
}


