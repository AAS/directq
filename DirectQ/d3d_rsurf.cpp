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


#include "quakedef.h"
#include "d3d_model.h"
#include "d3d_quake.h"
#include "d3d_vbo.h"

void R_PushDlights (mnode_t *headnode);
void D3D_AddSkySurfaceToRender (msurface_t *surf, entity_t *ent);
void D3D_DrawWaterSurfaces (void);
void D3D_FinalizeSky (void);
void R_MarkLeaves (void);
void D3D_EmitModelSurfToAlpha (d3d_modelsurf_t *modelsurf);

cvar_t r_lockpvs ("r_lockpvs", "0");

void D3D_RotateForEntity (entity_t *e);
void D3D_RotateForEntity (entity_t *e, D3DMATRIX *m);


/*
=============================================================

	ALL BRUSH MODELS

=============================================================
*/

#define MAX_MODELSURFS 65536

d3d_modelsurf_t **d3d_ModelSurfs = NULL;
int d3d_NumModelSurfs = 0;

void D3D_ModelSurfsBeginMap (void)
{
	d3d_ModelSurfs = (d3d_modelsurf_t **) MainHunk->Alloc (MAX_MODELSURFS * sizeof (d3d_modelsurf_t *));

	// allocate an initial batch
	for (int i = 0; i < 4096; i++)
		d3d_ModelSurfs[i] = (d3d_modelsurf_t *) MainHunk->Alloc (sizeof (d3d_modelsurf_t));

	d3d_NumModelSurfs = 0;
}


void D3D_AllocModelSurf (msurface_t *surf, texture_t *tex, entity_t *ent)
{
	// ensure that there is space
	if (d3d_NumModelSurfs >= MAX_MODELSURFS) return;

	// allocate as required
	if (!d3d_ModelSurfs[d3d_NumModelSurfs])
		d3d_ModelSurfs[d3d_NumModelSurfs] = (d3d_modelsurf_t *) MainHunk->Alloc (sizeof (d3d_modelsurf_t));

	// take the next modelsurf
	d3d_modelsurf_t *ms = d3d_ModelSurfs[d3d_NumModelSurfs++];

	if (surf->d3d_Lightmap)
	{
		// ensure that the surface lightmap texture is correct (it may not be if we lost the device)
		surf->d3d_Lightmap->EnsureSurfaceTexture (surf);

		// cache the lightmap texture change
		ms->tc[TEXTURECHANGE_LIGHTMAP].stage = 0;
		ms->tc[TEXTURECHANGE_LIGHTMAP].tex = surf->d3d_LightmapTex;
	}
	else
	{
		// no lightmap
		ms->tc[TEXTURECHANGE_LIGHTMAP].stage = 0;
		ms->tc[TEXTURECHANGE_LIGHTMAP].tex = NULL;
	}

	if (tex->lumaimage)
	{
		// cache the luma texture change
		ms->tc[TEXTURECHANGE_LUMA].stage = d3d_GlobalCaps.NumTMUs > 2 ? 2 : 0;
		ms->tc[TEXTURECHANGE_LUMA].tex = tex->lumaimage->d3d_Texture;
	}
	else
	{
		// no luma cached
		ms->tc[TEXTURECHANGE_LUMA].stage = 0;
		ms->tc[TEXTURECHANGE_LUMA].tex = NULL;
	}

	if (tex->teximage)
	{
		ms->tc[TEXTURECHANGE_DIFFUSE].stage = 1;
		ms->tc[TEXTURECHANGE_DIFFUSE].tex = tex->teximage->d3d_Texture;
	}
	else
	{
		// sky doesn't have diffuse
		ms->tc[TEXTURECHANGE_DIFFUSE].stage = 0;
		ms->tc[TEXTURECHANGE_DIFFUSE].tex = NULL;
	}

	ms->surf = surf;
	ms->tex = tex;
	ms->ent = ent;
}


void D3DSurf_LumaEndCallback (void *blah)
{
	// disable blend mode
	D3D_SetRenderState (D3DRS_ZWRITEENABLE, TRUE);
	D3D_SetRenderState (D3DRS_BLENDOP, D3DBLENDOP_ADD);
	D3D_SetRenderState (D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	D3D_SetRenderState (D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	D3D_SetRenderState (D3DRS_ALPHABLENDENABLE, FALSE);
}


void D3DSurf_Luma3Callback (void *blah)
{
	D3D_SetVertexDeclaration (d3d_VDXyzTex2);

	if (d3d_GlobalCaps.usingPixelShaders)
	{
		D3D_SetTextureAddressMode (D3DTADDRESS_CLAMP, D3DTADDRESS_WRAP, D3DTADDRESS_WRAP);
		D3D_SetTextureMipmap (0, D3DTEXF_LINEAR, D3DTEXF_NONE);
		D3D_SetTextureMipmap (1, d3d_TexFilter, d3d_MipFilter);
		D3D_SetTextureMipmap (2, d3d_TexFilter, d3d_MipFilter);

		if (d3d_FXPass == FX_PASS_NOTBEGUN)
			D3D_BeginShaderPass (FX_PASS_WORLD_LUMA);
		else if (d3d_FXPass != FX_PASS_WORLD_LUMA)
		{
			D3D_EndShaderPass ();
			D3D_BeginShaderPass (FX_PASS_WORLD_LUMA);
		}
	}
	else
	{
		D3D_SetTextureAddressMode (D3DTADDRESS_CLAMP, D3DTADDRESS_WRAP, D3DTADDRESS_WRAP);
		D3D_SetTextureMipmap (0, D3DTEXF_LINEAR, D3DTEXF_NONE);
		D3D_SetTextureMipmap (1, d3d_TexFilter, d3d_MipFilter);
		D3D_SetTextureMipmap (2, d3d_TexFilter, d3d_MipFilter);

		D3D_SetTexCoordIndexes (1, 0, 0);
		D3D_SetTextureColorMode (0, D3DTOP_SELECTARG1, D3DTA_TEXTURE, D3DTA_CURRENT);
		D3D_SetTextureColorMode (1, D3D_OVERBRIGHT_MODULATE, D3DTA_TEXTURE, D3DTA_CURRENT);
		D3D_SetTextureColorMode (2, D3DTOP_ADD, D3DTA_TEXTURE, D3DTA_CURRENT);
	}
}


void D3DSurf_Luma2Callback (void *blah)
{
	// enable blend mode for 2nd pass
	D3D_SetRenderState (D3DRS_ZWRITEENABLE, FALSE);
	D3D_SetRenderState (D3DRS_ALPHABLENDENABLE, TRUE);

	D3D_SetRenderState (D3DRS_BLENDOP, D3DBLENDOP_ADD);
	D3D_SetRenderState (D3DRS_SRCBLEND, D3DBLEND_ONE);
	D3D_SetRenderState (D3DRS_DESTBLEND, D3DBLEND_ONE);

	// set correct filtering and indexing
	D3D_SetVertexDeclaration (d3d_VDXyzTex1);
	D3D_SetTextureAddressMode (D3DTADDRESS_WRAP);
	D3D_SetTextureMipmap (0, d3d_TexFilter, d3d_MipFilter);

	D3D_SetTexCoordIndexes (0);
	D3D_SetTextureColorMode (0, D3DTOP_SELECTARG1, D3DTA_TEXTURE, D3DTA_CURRENT);
	D3D_SetTextureColorMode (1, D3DTOP_DISABLE);
}


void D3DSurf_NoLumaCallback (void *blah)
{
	D3D_SetVertexDeclaration (d3d_VDXyzTex2);
	D3D_SetTextureAddressMode (D3DTADDRESS_CLAMP, D3DTADDRESS_WRAP);
	D3D_SetTextureMipmap (0, D3DTEXF_LINEAR, D3DTEXF_NONE);
	D3D_SetTextureMipmap (1, d3d_TexFilter, d3d_MipFilter);

	if (d3d_GlobalCaps.usingPixelShaders)
	{
		if (d3d_FXPass == FX_PASS_NOTBEGUN)
			D3D_BeginShaderPass (FX_PASS_WORLD_NOLUMA);
		else if (d3d_FXPass != FX_PASS_WORLD_NOLUMA)
		{
			D3D_EndShaderPass ();
			D3D_BeginShaderPass (FX_PASS_WORLD_NOLUMA);
		}
	}
	else
	{
		D3D_SetTexCoordIndexes (1, 0);
		D3D_SetTextureColorMode (0, D3DTOP_SELECTARG1, D3DTA_TEXTURE, D3DTA_CURRENT);
		D3D_SetTextureColorMode (1, D3D_OVERBRIGHT_MODULATE, D3DTA_TEXTURE, D3DTA_CURRENT);
		D3D_SetTextureColorMode (2, D3DTOP_DISABLE);
	}
}


D3DXHANDLE shaderstages[] = {"tmu0Texture", "tmu1Texture", "tmu2Texture"};

int lumaremap[] = {0, 1, 2};
int lumanolumaremap[] = {2, 0, 1};
int *maptable;

void D3DSurf_TextureChangeCallback (void *data)
{
	d3d_texturechange_t *tc = (d3d_texturechange_t *) data;

	if (d3d_GlobalCaps.usingPixelShaders)
	{
		d3d_MasterFX->SetTexture (shaderstages[tc->stage], tc->tex);
		d3d_FXCommitPending = true;
	}
	else D3D_SetTexture (tc->stage, tc->tex);
}


void D3D_DrawModelSurfs (bool luma)
{
	// set correct texture blend modes
	if (luma && d3d_GlobalCaps.NumTMUs > 2)
		VBO_AddCallback (D3DSurf_Luma3Callback);
	else if (luma)
		VBO_AddCallback (D3DSurf_Luma2Callback);
	else VBO_AddCallback (D3DSurf_NoLumaCallback);

	for (int i = 0; i < d3d_NumRegisteredTextures; i++)
	{
		d3d_registeredtexture_t *rt = d3d_RegisteredTextures[i];

		if (!rt->texture) continue;
		if (!rt->surfchain) continue;
		if (!rt->texture->teximage) continue;
		if (rt->surfchain->surf->flags & SURF_DRAWSKY) continue;
		if (rt->surfchain->surf->flags & SURF_DRAWTURB) continue;

		texture_t *tex = rt->texture;
		d3d_texturechange_t *tc = rt->surfchain->tc;
		msurface_t *surf;

		// take it from the texture change so that we can modify the textures used at runtime
		if (luma && !tc[TEXTURECHANGE_LUMA].tex) continue;
		if (!luma && tc[TEXTURECHANGE_LUMA].tex && d3d_GlobalCaps.NumTMUs > 2) continue;

		// a texture change always invalidates the current set
		if (luma && d3d_GlobalCaps.NumTMUs < 3)
			VBO_AddCallback (D3DSurf_TextureChangeCallback, &tc[TEXTURECHANGE_LUMA], sizeof (d3d_texturechange_t));
		else if (luma)
		{
			VBO_AddCallback (D3DSurf_TextureChangeCallback, &tc[TEXTURECHANGE_DIFFUSE], sizeof (d3d_texturechange_t));
			VBO_AddCallback (D3DSurf_TextureChangeCallback, &tc[TEXTURECHANGE_LUMA], sizeof (d3d_texturechange_t));
		}
		else VBO_AddCallback (D3DSurf_TextureChangeCallback, &tc[TEXTURECHANGE_DIFFUSE], sizeof (d3d_texturechange_t));

		for (d3d_modelsurf_t *modelsurf = rt->surfchain; modelsurf; modelsurf = modelsurf->surfchain)
		{
			if (!(surf = modelsurf->surf)->verts) continue;
			if (!surf->d3d_Lightmap) continue;

			// chain the surfs in lightmap order; this will also do the
			// final reversal of surf order so that we get proper f2b.
			surf->d3d_Lightmap->ChainModelSurf (modelsurf);
		}

		// draw all chained surfs in lightmap order
		d3d_Lightmaps->DrawSurfaceChain (luma);
	}

	if (luma && d3d_GlobalCaps.NumTMUs < 3) VBO_AddCallback (D3DSurf_LumaEndCallback);
}


void D3DSurf_CommonCallback (void *data)
{
	// decode alpha val
	int alphaval = ((int *) data)[0];

	// to do - decode blah to alpha and move the whole crap to here
	D3D_SetVertexDeclaration (d3d_VDXyzTex2);

	if (d3d_GlobalCaps.usingPixelShaders)
	{
		if (d3d_FXPass != FX_PASS_NOTBEGUN)
			D3D_EndShaderPass ();

		d3d_MasterFX->SetFloat ("AlphaVal", (float) alphaval / 255.0f);
	}
	else
	{
		// stage 0 is the lightmap (and may be modulated with alpha)
		if (alphaval < 255)
		{
			// fixme - this seems incorrect
			D3D_SetTextureAlphaMode (0, D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_TFACTOR);
			D3D_SetRenderState (D3DRS_TEXTUREFACTOR, D3DCOLOR_ARGB (BYTE_CLAMP (alphaval), 255, 255, 255));
		}
		else
		{
			D3D_SetTextureAlphaMode (0, D3DTOP_DISABLE);
			D3D_SetRenderState (D3DRS_TEXTUREFACTOR, 0xffffffff);
		}

		// no alpha ops here irrespective
		D3D_SetTextureAlphaMode (1, D3DTOP_DISABLE);
	}
}


void D3DSurf_TakeDownCallback (void *blah)
{
	if (d3d_GlobalCaps.usingPixelShaders)
	{
		// end and do the usual thing of disabling shaders fully
		if (d3d_FXPass != FX_PASS_NOTBEGUN)
			D3D_EndShaderPass ();
	}
}


void D3D_AddSurfacesToRender (int alpha)
{
	// this should only ever happen if the scene is filled with water or sky
	if (!d3d_NumModelSurfs) return;

	bool drawluma = false;
	bool drawnoluma = false;
	bool drawsky = false;

	// output to texturechains in back-to-front order; the final per-lightmap chaining will
	// reverse this again to front-to-back giving the correct order.  order doesn't matter for sky.
	for (int i = 0; i < d3d_NumModelSurfs; i++)
	{
		d3d_modelsurf_t *modelsurf = d3d_ModelSurfs[i];
		msurface_t *surf = modelsurf->surf;

		// a bmodel will have already been added to the alpha list
		if (surf->flags & SURF_DRAWSKY)
		{
			// sky is rendered immediately as it passes
			D3D_AddSkySurfaceToRender (modelsurf->surf, modelsurf->ent);
			drawsky = true;
			continue;
		}

		if (surf->flags & SURF_DRAWTURB)
		{
			if (surf->alphaval < 255)
			{
				// this surface goes on the alpha list instead
				D3D_EmitModelSurfToAlpha (modelsurf);
				continue;
			}
		}

		// now we handle the standard solid surf
		d3d_registeredtexture_t *rt = modelsurf->tex->registration;

		// flag if we're doing lumas here
		if (rt->texture->lumaimage)
			drawluma = true;
		else drawnoluma = true;

		// chain the texture
		modelsurf->surfchain = rt->surfchain;
		rt->surfchain = modelsurf;
	}

	// everything that is drawn is conditional
	// some sky modes require extra stuff to be added after the sky surfs so handle those now
	if (drawsky) D3D_FinalizeSky ();

	// common solid brush state
	if (drawluma || drawnoluma) VBO_AddCallback (D3DSurf_CommonCallback, &alpha, sizeof (int));

	// now emit from the list of registered textures in two passes for fb/no fb
	if (drawnoluma) D3D_DrawModelSurfs (false);
	if (drawluma) D3D_DrawModelSurfs (true);

	// common solid brush state
	if (drawluma || drawnoluma) VBO_AddCallback (D3DSurf_TakeDownCallback);
}


/*
=============================================================

	WORLD MODEL BUILDING

=============================================================
*/

extern float r_clipdist;

texture_t *R_TextureAnimation (entity_t *ent, texture_t *base)
{
	int count;
	int relative;
	texture_t *cached = base;

	if (ent->frame)
	{
		if (base->alternate_anims)
			base = base->alternate_anims;
	}

	if (!base->anim_total) return base;

	relative = (int) (cl.time * 10) % base->anim_total;

	count = 0;	

	while (base->anim_min > relative || base->anim_max <= relative)
	{
		base = base->anim_next;

		// prevent crash here
		if (!base) return cached;
		if (++count > 100) return cached;
	}

	return base;
}


#define R_MarkLeafSurfs(leaf) \
{ \
	msurface_t **mark = (leaf)->firstmarksurface; \
	int c = (leaf)->nummarksurfaces; \
	if (c) \
	{ \
		do \
		{ \
			(*mark)->intersect = ((leaf)->bops == FULLY_INTERSECT_FRUSTUM); \
			(*mark)->visframe = d3d_RenderDef.framecount; \
			mark++; \
		} while (--c); \
	} \
}


void R_StoreEfrags (efrag_t **ppefrag);
int numrrwn = 0;

void R_RecursiveWorldNode (mnode_t *node)
{
	numrrwn++;

loc0:;
	// node is just a decision point, so go down the appropriate sides
	// find which side of the node we are on
	switch (node->plane->type)
	{
	case PLANE_X:
		node->dot = d3d_RenderDef.worldentity.modelorg[0] - node->plane->dist;
		break;
	case PLANE_Y:
		node->dot = d3d_RenderDef.worldentity.modelorg[1] - node->plane->dist;
		break;
	case PLANE_Z:
		node->dot = d3d_RenderDef.worldentity.modelorg[2] - node->plane->dist;
		break;
	default:
		node->dot = DotProduct (d3d_RenderDef.worldentity.modelorg, node->plane->normal) - node->plane->dist;
		break;
	}

	// check max dot
	if (node->dot > r_clipdist) r_clipdist = node->dot;
	if (-node->dot > r_clipdist) r_clipdist = -node->dot;

	// find which side we're on
	node->side = (node->dot >= 0 ? 0 : 1);

	// recurse down the children, front side first
	if (node->children[node->side]->contents == CONTENTS_SOLID) goto rrwnnofront;
	if (node->children[node->side]->visframe != d3d_RenderDef.visframecount) goto rrwnnofront;
	if (R_CullBox (node->children[node->side])) goto rrwnnofront;

	// check for a leaf
	if (node->children[node->side]->contents < 0)
	{
		R_MarkLeafSurfs ((mleaf_t *) node->children[node->side]);
		R_StoreEfrags (&((mleaf_t *) node->children[node->side])->efrags);
		goto rrwnnofront;
	}

	// now we can recurse
	R_RecursiveWorldNode (node->children[node->side]);

rrwnnofront:;
	if (node->numsurfaces)
	{
		msurface_t *surf = cl.worldmodel->brushhdr->surfaces + node->firstsurface;
		int sidebit = (node->dot >= 0 ? 0 : SURF_PLANEBACK);

		// add stuff to the draw lists
		for (int c = node->numsurfaces; c; c--, surf++)
		{
			// the SURF_PLANEBACK test never actually evaluates to true with GLQuake as the surf
			// will have the same plane and facing as the node here.  oh well...
			if (surf->visframe != d3d_RenderDef.framecount) continue;
			if ((surf->flags & SURF_PLANEBACK) != sidebit) continue;

			// only check for culling if both the leaf and the node containing this surf intersect the frustum
			if (surf->intersect && (node->bops == FULLY_INTERSECT_FRUSTUM))
				if (R_CullBox (surf->mins, surf->maxs)) continue;

			// it's OK to add it now
			// world surfs never have explicit alpha
			// (they may pick it up from the value of r_wateralpha)
			surf->alphaval = 255;

			// check for lightmap modifications
			if (surf->d3d_Lightmap) surf->d3d_Lightmap->CheckSurfaceForModification (surf);

			texture_t *tex = R_TextureAnimation (&d3d_RenderDef.worldentity, surf->texinfo->texture);

			// this only ever comes from the world so entity is always NULL
			D3D_AllocModelSurf (surf, tex, NULL);

			// in case a bad BSP overlaps the surfs in it's nodes
			surf->visframe = -1;
		}
	}

	// recurse down the back side
	// the compiler should be performing this optimization anyway
	node = node->children[!node->side];

	// check the back side
	if (node->contents == CONTENTS_SOLID) return;
	if (node->visframe != d3d_RenderDef.visframecount) return;
	if (R_CullBox (node)) return;

	// if a leaf node, draw stuff
	if (node->contents < 0)
	{
		R_MarkLeafSurfs ((mleaf_t *) node);
		R_StoreEfrags (&((mleaf_t *) node)->efrags);
		return;
	}

	goto loc0;
}


void R_AutomapSurfaces (void)
{
	// don't bother depth-sorting these
	// some Quake weirdness here
	// this info taken from qbsp source code:
	// leaf 0 is a common solid with no faces
	for (int i = 1; i <= cl.worldmodel->brushhdr->numleafs; i++)
	{
		mleaf_t *leaf = &cl.worldmodel->brushhdr->leafs[i];

		// show all leafs that have previously been seen
		if (!leaf->seen) continue;

		// need to cull here too otherwise we'll get static ents we shouldn't
		if (R_CullBox (leaf->minmaxs, leaf->minmaxs + 3)) continue;

		// mark the surfs
		R_MarkLeafSurfs (leaf);

		// add static entities contained in the leaf to the list
		R_StoreEfrags (&leaf->efrags);
	}

	// setup modelorg to a point so far above the viewpoint that it may as well be infinite
	d3d_RenderDef.worldentity.modelorg[0] = (cl.worldmodel->mins[0] + cl.worldmodel->maxs[0]) / 2;
	d3d_RenderDef.worldentity.modelorg[1] = (cl.worldmodel->mins[1] + cl.worldmodel->maxs[1]) / 2;
	d3d_RenderDef.worldentity.modelorg[2] = ((cl.worldmodel->mins[2] + cl.worldmodel->maxs[2]) / 2) + 16777216.0f;

	extern cvar_t r_automap_nearclip;
	extern float r_automap_z;

	for (int i = 0; i < cl.worldmodel->brushhdr->numnodes; i++)
	{
		mnode_t *node = &cl.worldmodel->brushhdr->nodes[i];

		// node culling
		if (node->contents == CONTENTS_SOLID) continue;
		if (!node->seen) continue;
		if (R_CullBox (node->minmaxs, node->minmaxs + 3)) continue;

		// find which side of the node we are on
		switch (node->plane->type)
		{
		case PLANE_X:
			node->dot = d3d_RenderDef.worldentity.modelorg[0] - node->plane->dist;
			break;
		case PLANE_Y:
			node->dot = d3d_RenderDef.worldentity.modelorg[1] - node->plane->dist;
			break;
		case PLANE_Z:
			node->dot = d3d_RenderDef.worldentity.modelorg[2] - node->plane->dist;
			break;
		default:
			node->dot = DotProduct (d3d_RenderDef.worldentity.modelorg, node->plane->normal) - node->plane->dist;
			break;
		}

		// check max dot
		if (node->dot > r_clipdist) r_clipdist = node->dot;
		if (-node->dot > r_clipdist) r_clipdist = -node->dot;

		// find which side we're on
		node->side = (node->dot >= 0 ? 0 : 1);

		int sidebit = (node->dot >= 0 ? 0 : SURF_PLANEBACK);

		for (int j = 0; j < node->numsurfaces; j++)
		{
			msurface_t *surf = cl.worldmodel->brushhdr->surfaces + node->firstsurface + j;

			// surf culling
			if (surf->visframe != d3d_RenderDef.framecount) continue;
			if (surf->flags & SURF_DRAWSKY) continue;
			if (surf->mins[2] > (r_refdef.vieworg[2] + r_automap_nearclip.integer + r_automap_z)) continue;
			if ((surf->flags & SURF_PLANEBACK) != sidebit) continue;

			// world surfs never have explicit alpha
			// (they may pick it up from the value of r_wateralpha)
			surf->alphaval = 255;

			// check for lightmap modifications
			if (surf->d3d_Lightmap) surf->d3d_Lightmap->CheckSurfaceForModification (surf);

			texture_t *tex = R_TextureAnimation (&d3d_RenderDef.worldentity, surf->texinfo->texture);

			// this only ever comes from the world so matrix is always NULL
			D3D_AllocModelSurf (surf, tex, NULL);
		}
	}
}


void R_BeginSurfaces (model_t *mod)
{
	// clear surface chains
	for (int i = 0; i < d3d_NumRegisteredTextures; i++)
		d3d_RegisteredTextures[i]->surfchain = NULL;

	// clear texture chains
	for (int i = 0; i < mod->brushhdr->numtextures; i++)
	{
		texture_t *tex = mod->brushhdr->textures[i];
		if (!tex) continue;

		// these are only used for lightmap building???
		tex->texturechain = NULL;
	}

	// no modelsurfs yet
	d3d_NumModelSurfs = 0;
}


void D3D_DrawAlphaBrushModel (entity_t *ent)
{
	// model was culled
	if (ent->visframe != d3d_RenderDef.framecount) return;

	// sanity check
	if (!ent->model) return;
	if (!ent->model->brushhdr) return;
	if (ent->model->type != mod_brush) return;

	model_t *mod = ent->model;
	msurface_t *surf = mod->brushhdr->surfaces + mod->brushhdr->firstmodelsurface;
	D3DMATRIX *m = &ent->matrix;

	// begin a new batch of surfaces
	R_BeginSurfaces (mod);

	// don't bother with ordering these for now; to be honest i'm quite sick of this code at the moment
	// and not yet ready to give it the restructuring it needs.  i'm going to be maintaining it going forward
	// however so i do need to get back to it some time
	for (int s = 0; s < mod->brushhdr->nummodelsurfaces; s++, surf++)
	{
		// sky and water are dealt with separately
		if (surf->flags & SURF_DRAWSKY) continue;
		if (surf->flags & SURF_DRAWTURB) continue;

		// add to the new modelsurfs list
		D3D_AllocModelSurf (surf, R_TextureAnimation (ent, surf->texinfo->texture), ent);
	}

	// add the new modelsurfs list to the render list
	D3D_AddSurfacesToRender (ent->alphaval);
}


cvar_t r_zfightinghack ("r_zfightinghack", "0.5", CVAR_ARCHIVE);

void D3D_SetupBrushModel (entity_t *ent)
{
	vec3_t mins, maxs;
	model_t *mod = ent->model;

	if (ent->rotated)
	{
		for (int i = 0; i < 3; i++)
		{
			mins[i] = ent->origin[i] - mod->radius;
			maxs[i] = ent->origin[i] + mod->radius;
		}
	}
	else
	{
		VectorAdd (ent->origin, mod->mins, mins);
		VectorAdd (ent->origin, mod->maxs, maxs);
	}

	// static entities already have the leafs they are in bbox culled
	if (R_CullBox (mins, maxs))
	{
		// mark as not visible
		ent->visframe = -1;
		return;
	}

	// visible this frame now
	ent->visframe = d3d_RenderDef.framecount;

	// flag the model as having been previously seen
	ent->model->wasseen = true;

	// wait until after the bbox check before flagging this otherwise visframe will never be set
	if (ent->occluded) return;

	// store transform for model - we need to run this in software as we are potentially submitting
	// multiple brush models in a single batch, all of which will be merged with the world render.
	D3D_LoadIdentity (&ent->matrix);
	ent->angles[0] = -ent->angles[0];	// stupid quake bug
	D3D_RotateForEntity (ent, &ent->matrix);
	ent->angles[0] = -ent->angles[0];	// stupid quake bug

	// anti z-fighting hack
	// this value gets rid of most artefacts without producing too much visual discontinuity
	ent->matrix._43 -= (r_zfightinghack.value / 10.0f);

	// cache data for visible entities for the automap
	if (ent->model->cacheent && ent->model->name[0] == '*')
	{
		// if we're caching models just store out it's last status
		// so that the automap can at least draw it at the position/orientation/etc it was
		// the last time that the player saw it.  this is only done for inline brush models
		entity_t *e2 = ent->model->cacheent;

		// we don't actually need the full contents of the entity_t struct here
		VectorCopy (ent->angles, e2->angles);
		VectorCopy (ent->origin, e2->origin);

		e2->alphaval = ent->alphaval;
		e2->model = ent->model;
		e2->frame = ent->frame;
		e2->nocullbox = ent->nocullbox;

		Q_MemCpy (&e2->matrix, &ent->matrix, sizeof (D3DMATRIX));

		e2->visframe = d3d_RenderDef.framecount;

		// set the cached ent back to itself (ensure)
		e2->model->cacheent = e2;
	}

	// get origin vector relative to viewer
	// this is now stored in the entity so we can read it back any time we want
	VectorSubtract (r_refdef.vieworg, ent->origin, ent->modelorg);

	// adjust for rotation
	if (ent->rotated)
	{
		vec3_t temp;
		vec3_t forward, right, up;

		// i fixed this bug for matrix transforms but of course it came back here... sigh...
		VectorCopy (ent->modelorg, temp);
		AngleVectors (ent->angles, forward, right, up);

		ent->modelorg[0] = DotProduct (temp, forward);
		ent->modelorg[1] = -DotProduct (temp, right);
		ent->modelorg[2] = DotProduct (temp, up);
	}

	if (mod->name[0] == '*')
	{
		// calculate dynamic lighting for the inline bmodel
		R_PushDlights (mod->brushhdr->nodes + mod->brushhdr->hulls[0].firstclipnode);
	}

	// if the ent has alpha we need to add it to the alpha list
	// even so we still run through the surfs to check for water and sky which are handled separately
	// (sky never has alpha, water - including on alpha ents - is also handled separately and will get the
	// correct alpha ent water if the ent has alpha during the water rendering pass)
	msurface_t *surf = mod->brushhdr->surfaces + mod->brushhdr->firstmodelsurface;
	int numalphasurfs = 0;

	// don't bother with ordering these for now; we'll sort them by texture later
	for (int s = 0; s < mod->brushhdr->nummodelsurfaces; s++, surf++)
	{
		float dot = DotProduct (ent->modelorg, surf->plane->normal) - surf->plane->dist;

		if (((surf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) ||
			(!(surf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON)) ||
			(ent->alphaval < 255 && !(surf->flags & SURF_DRAWSKY)))
		{
			// the surface needs to inherit an alpha and the rotation member from the entity so that we know how to draw it
			// (should we make this a SURF_ROTATED flag?)
			surf->alphaval = ent->alphaval;
			surf->rotated = ent->rotated;

			// sky surfaces need to be added immediately
			// other surface types either go on the main list or the alpha list
			if (surf->flags & SURF_DRAWSKY)
				D3D_AllocModelSurf (surf, surf->texinfo->texture, ent);
			else if (surf->flags & SURF_DRAWTURB)
			{
				surf->alphaval = ent->alphaval;
				D3D_AllocModelSurf (surf, surf->texinfo->texture, ent);
			}
			else
			{
				if (mod->name[0] == '*' && surf->d3d_Lightmap)
					surf->d3d_Lightmap->CheckSurfaceForModification (surf);

				if (ent->alphaval < 255)
				{
					// track the number of alpha surfs in this bmodel
					numalphasurfs++;
				}
				else
				{
					// add to the modelsurfs list
					D3D_AllocModelSurf (surf, R_TextureAnimation (ent, surf->texinfo->texture), ent);
				}
			}
		}
	}

	if (numalphasurfs)
	{
		// ensure
		ent->visframe = d3d_RenderDef.framecount;
		D3D_AddToAlphaList (ent);
	}
}


int lmuploads = 0;

void D3D_BuildWorld (void)
{
	// moved up because the node array builder uses it too and that gets called from r_markleaves
	VectorCopy (r_refdef.vieworg, d3d_RenderDef.worldentity.modelorg);

	// mark visible leafs
	R_MarkLeaves ();

	R_BeginSurfaces (cl.worldmodel);

	// calculate dynamic lighting for the world
	R_PushDlights (cl.worldmodel->brushhdr->nodes);

	// never go < 4096 as that is the clip dist in regular quake and maps/mods may expect it
	r_clipdist = 4096;

	// assume that the world is always visible ;)
	// (although it might not be depending on the scene...)
	d3d_RenderDef.worldentity.visframe = d3d_RenderDef.framecount;

	numrrwn = 0;

	// the automap has a different viewpoint so R_RecursiveWorldNode is not valid for it
	if (d3d_RenderDef.automap)
		R_AutomapSurfaces ();
	else R_RecursiveWorldNode (cl.worldmodel->brushhdr->nodes);

	// models are done after the world so that the double-reverse will put them after it too during the render
	if (r_drawentities.integer)
	{
		// add brushmodels to the render
		for (int i = 0; i < d3d_RenderDef.numvisedicts; i++)
		{
			entity_t *ent = d3d_RenderDef.visedicts[i];

			if (!ent->model) continue;
			if (ent->model->type != mod_brush) continue;

			// brush models have different surface types some of which we can't
			// draw during the alpha pass.  we also need to include it in the lightmap
			// uploads pass, so we need to add the full model at this stage.
			D3D_SetupBrushModel (ent);
		}
	}

	// upload any lightmaps that were modified
	// done as early as possible for best parallelism
	d3d_Lightmaps->UploadLightmap ();

	//if (lmuploads) Con_Printf ("uploaded %i lightmaps\n", lmuploads);
	lmuploads = 0;

	// finish solid surfaces by adding any such to the solid buffer
	D3D_AddSurfacesToRender (255);
}


/*
=============================================================

	VISIBILITY

=============================================================
*/

void R_LeafVisibility (byte *vis)
{
	// leaf 0 is the generic solid leaf; the last leaf overlaps with the first node
	mleaf_t *leaf = &cl.worldmodel->brushhdr->leafs[1];

	// mark leafs and surfaces as visible
	for (int i = 0; i < cl.worldmodel->brushhdr->numleafs; i++, leaf++)
	{
		if (!vis || (vis[i >> 3] & (1 << (i & 7))))
		{
			// note - nodes and leafs need to be in consecutive memory for this to work so
			// that the last leaf will resolve to the first node here; this is set up in Mod_LoadVisLeafsNodes
			// first node always needs to be in the pvs for R_RecursiveWorldNode
			mnode_t *node = (mnode_t *) leaf;

			do
			{
				// already added
				if (node->visframe == d3d_RenderDef.visframecount) break;

				// add it
				node->visframe = d3d_RenderDef.visframecount;
				node->seen = true;
				node = node->parent;
			} while (node);
		}
	}
}


extern cvar_t r_lavaalpha;
extern cvar_t r_telealpha;
extern cvar_t r_slimealpha;
extern cvar_t r_lockalpha;

bool R_NearWaterTest (void)
{
	msurface_t **mark = d3d_RenderDef.viewleaf->firstmarksurface;
	int c = d3d_RenderDef.viewleaf->nummarksurfaces;

	if (c)
	{
		do
		{
			if (!r_lockalpha.integer)
			{
				if ((mark[0]->flags & SURF_DRAWLAVA) && (r_lavaalpha.value > 0.99f)) return true;
				if ((mark[0]->flags & SURF_DRAWSLIME) && (r_slimealpha.value > 0.99f)) return true;
				if ((mark[0]->flags & SURF_DRAWTELE) && (r_telealpha.value > 0.99f)) return true;
				if ((mark[0]->flags & SURF_DRAWWATER) && (r_wateralpha.value > 0.99f)) return true;
			}
			else
			{
				// any type and use (r_wateralpha globally
				if ((mark[0]->flags & SURF_DRAWTURB) && (r_wateralpha.value > 0.99f)) return true;
			}

			mark++;
		} while (--c);
	}

	return false;
}


void R_MarkLeaves (void)
{
	byte	*vis;
	mnode_t	*node;
	int		i;
	extern	byte *mod_novis;
	static int old_novis = -666;

	// viewleaf hasn't changed, r_novis hasn't changed or we're drawing with a locked PVS
	if (((d3d_RenderDef.oldviewleaf == d3d_RenderDef.viewleaf) && (r_novis.integer == old_novis)) || r_lockpvs.value) return;

	// go to a new visframe
	d3d_RenderDef.visframecount++;
	old_novis = r_novis.integer;

	// add in visible leafs - we always add the fat PVS to ensure that client visibility
	// is the same as that which was used by the server; R_CullBox will take care of unwanted leafs
	if (r_novis.integer)
		R_LeafVisibility (NULL);
	else if (!R_NearWaterTest ())
		R_LeafVisibility (Mod_LeafPVS (d3d_RenderDef.viewleaf, cl.worldmodel));
	else R_LeafVisibility (Mod_FatPVS (r_origin));

	// no old viewleaf so can't make a transition check
	if (!d3d_RenderDef.oldviewleaf) return;

	// check for a contents transition
	if (d3d_RenderDef.oldviewleaf->contents == d3d_RenderDef.viewleaf->contents)
	{
		// if we're still in the same contents we still have the same contents colour
		d3d_RenderDef.viewleaf->contentscolor = d3d_RenderDef.oldviewleaf->contentscolor;
	}
}


// just in case these get fucked...
void R_FixupBModelBBoxes (void)
{
	// note - the player is model 0 in the precache list;
	// the world is model 1
	for (int j = 1; j < MAX_MODELS; j++)
	{
		model_t *mod;

		// note - we set the end of the precache list to NULL in cl_parse to ensure this test is valid
		if (!(mod = cl.model_precache[j])) break;

		if (mod->type != mod_brush) continue;

		brushhdr_t *hdr = mod->brushhdr;
		msurface_t *surf = hdr->surfaces + hdr->firstmodelsurface;

		float mins[3] = {99999999, 99999999, 99999999};
		float maxs[3] = {-99999999, -99999999, -99999999};

		for (int s = 0; s < hdr->nummodelsurfaces; s++, surf++)
		{
			for (int i = 0; i < 3; i++)
			{
				if (surf->mins[i] < mins[i]) mins[i] = surf->mins[i];
				if (surf->maxs[i] > maxs[i]) maxs[i] = surf->maxs[i];
			}
		}

		j = j;

		for (int i = 0; i < 3; i++)
		{
			if (mins[i] < mod->mins[i]) mod->mins[i] = mins[i];
			if (maxs[i] > mod->maxs[i]) mod->maxs[i] = maxs[i];
		}
	}
}


void R_FindHipnoticWindows (void)
{
}

