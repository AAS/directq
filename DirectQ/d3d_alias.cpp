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

// gl_mesh.c: triangle model functions

/*
instancing idea
per-instance gets xyz and colour and also needs a matrix
baseline model has texcoords and indexes

stream 0
baseline model (texcoords and indexes)
d3d_Device->SetStreamSourceFreq (0, D3DSTREAMSOURCE_INDEXEDDATA | NumEntsUsingThisModel);

stream 1
per-instance models (great big VBO)
d3d_Device->SetStreamSourceFreq (1, D3DSTREAMSOURCE_INSTANCEDATA | 1);
this data might need to move to stream 0... so we take the extra hit of adding texcoords in exchange for less draw calls and not having to add indexes???

stream 2
matrix transforms, one per entity using the model
advances to the next value after each instance is drawn so it's (D3DSTREAMSOURCE_INSTANCEDATA | 1) in theory...
d3d_Device->SetStreamSourceFreq (2, ?????????????????????);
4 rows of float4 texcoords as the matrix????
also contains shadelight and possibly alphaval????
*/

#include "quakedef.h"
#include "d3d_model.h"
#include "d3d_quake.h"
#include "resource.h"

// texture for light lookups to avoid branching in the shader
LPDIRECT3DTEXTURE9 d3d_ShadedotsTexture = NULL;

// up to 16 color translated skins
image_t d3d_PlayerSkins[256];

void D3D_RotateForEntity (entity_t *e);
void D3D_RotateForEntity (entity_t *e, D3DMATRIX *m);
void D3DLight_LightPoint (entity_t *e, float *c);

cvar_t r_aliaslightscale ("r_aliaslightscale", "1", CVAR_ARCHIVE);
cvar_t r_aliasambientcutoff ("r_aliasambientcutoff", "128", CVAR_ARCHIVE);
cvar_t r_aliaslightingcutoff ("r_aliaslightingcutoff", "192", CVAR_ARCHIVE);

/*
=================================================================

ALIAS MODEL DISPLAY LIST GENERATION

=================================================================
*/

/*
================
D3DAlias_MakeAliasMesh

================
*/
void D3DAlias_MakeAliasMesh (char *name, aliashdr_t *hdr, stvert_t *stverts, dtriangle_t *triangles)
{
	// also reserve space for the index remap tables
	int max_mesh = (SCRATCHBUF_SIZE / sizeof (aliasmesh_t));

	// clamp further to max supported by hardware
	if (max_mesh > d3d_DeviceCaps.MaxVertexIndex) max_mesh = d3d_DeviceCaps.MaxVertexIndex;
	if (max_mesh > d3d_DeviceCaps.MaxPrimitiveCount) max_mesh = d3d_DeviceCaps.MaxPrimitiveCount;

	// create a pool of indexes for use by the model
	unsigned short *indexes = (unsigned short *) MainCache->Alloc (3 * sizeof (unsigned short) * hdr->numtris);
	hdr->indexes = indexes;

	int hunkmark = MainHunk->GetLowMark ();

	// take an area of memory for the verts; if it won't fit in our fast temp buffer we'll pull it off the hunk
	if (hdr->numtris * 3 > max_mesh)
		hdr->meshverts = (aliasmesh_t *) MainHunk->Alloc (hdr->numtris * 3 * sizeof (aliasmesh_t));
	else hdr->meshverts = (aliasmesh_t *) scratchbuf;

	// set up the initial params
	hdr->nummesh = 0;
	hdr->numindexes = 0;

	for (int i = 0, v = 0; i < hdr->numtris; i++)
	{
		for (int j = 0; j < 3; j++, indexes++, hdr->numindexes++)
		{
			// this is nothing to do with an index buffer, it's an index into hdr->vertexes
			int vertindex = triangles[i].vertindex[j];

			// basic s/t coords
			int s = stverts[vertindex].s;
			int t = stverts[vertindex].t;

			// check for back side and adjust texcoord s
			if (!triangles[i].facesfront && stverts[vertindex].onseam) s += hdr->skinwidth / 2;

			// see does this vert already exist
			for (v = 0; v < hdr->nummesh; v++)
			{
				// it could use the same xyz but have different s and t
				// store as int for better comparison
				if (hdr->meshverts[v].vertindex == vertindex && hdr->meshverts[v].st[0] == s && (int) hdr->meshverts[v].st[1] == t)
				{
					// exists; emit an index for it
					hdr->indexes[hdr->numindexes] = v;
					break;
				}
			}

			if (v == hdr->nummesh)
			{
				// doesn't exist; emit a new vert and index
				hdr->indexes[hdr->numindexes] = hdr->nummesh;

				hdr->meshverts[hdr->nummesh].vertindex = vertindex;
				hdr->meshverts[hdr->nummesh].st[0] = s;
				hdr->meshverts[hdr->nummesh++].st[1] = t;
			}
		}
	}

	// check hardware limits; it sucks if this happens but there's nothing much we can do
	if (hdr->nummesh > d3d_DeviceCaps.MaxVertexIndex) Sys_Error ("D3DAlias_MakeAliasMesh: MDL %s too big", name);
	if (hdr->nummesh > d3d_DeviceCaps.MaxPrimitiveCount) Sys_Error ("D3DAlias_MakeAliasMesh: MDL %s too big", name);

	// alloc a final for-real buffer for the mesh verts
	hdr->meshverts = (aliasmesh_t *) MainCache->Alloc (hdr->meshverts, hdr->nummesh * sizeof (aliasmesh_t));
	MainHunk->FreeToLowMark (hunkmark);

	// not delerped yet (view models only)
	hdr->mfdelerp = false;
}


typedef struct aliasbuffers_s
{
	LPDIRECT3DVERTEXBUFFER9 Stream0;
	LPDIRECT3DVERTEXBUFFER9 Stream1;
	LPDIRECT3DINDEXBUFFER9 Indexes;
} aliasbuffers_t;


typedef struct aliasstream0_s
{
	// positions are sent as bytes to save on bandwidth and storage
	// also keep the names consistent with drawvertx_t
	DWORD lastxyz;
	DWORD currxyz;
	DWORD lastnormal;
	DWORD currnormal;
} aliasstream0_t;

typedef struct aliasstream1_s
{
	float st[2];
} aliasstream1_t;

aliasbuffers_t d3d_AliasBuffers[MAX_MODELS];

LPDIRECT3DVERTEXDECLARATION9 d3d_AliasDecl = NULL;
LPDIRECT3DVERTEXDECLARATION9 d3d_ShadowDecl = NULL;


void D3DAlias_CreateStream0Buffer (aliashdr_t *hdr, aliasbuffers_t *buf)
{
	// filled in at runtime
	hr = d3d_Device->CreateVertexBuffer
	(
		hdr->nummesh * sizeof (aliasstream0_t),
		D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY,
		0,
		D3DPOOL_DEFAULT,
		&buf->Stream0,
		NULL
	);

	if (FAILED (hr)) Sys_Error ("D3DAlias_CreateStream0Buffer: d3d_Device->CreateVertexBuffer failed");

	D3D_PrelockVertexBuffer (buf->Stream0);
}


void D3DAlias_CreateStream1Buffer (aliashdr_t *hdr, aliasbuffers_t *buf)
{
	// filled in at runtime
	hr = d3d_Device->CreateVertexBuffer
	(
		hdr->nummesh * sizeof (aliasstream1_t),
		D3DUSAGE_WRITEONLY,
		0,
		D3DPOOL_DEFAULT,
		&buf->Stream1,
		NULL
	);

	if (FAILED (hr)) Sys_Error ("D3DAlias_CreateStream1Buffer: d3d_Device->CreateVertexBuffer failed");

	aliasstream1_t *st = NULL;
	aliasmesh_t *src = hdr->meshverts;

	hr = buf->Stream1->Lock (0, 0, (void **) &st, 0);
	if (FAILED (hr)) Sys_Error ("D3DAlias_CreateStream1Buffer: failed to lock vertex buffer");

	for (int i = 0; i < hdr->nummesh; i++, src++, st++)
	{
		// convert back to floating point and store out
		st->st[0] = (float) src->st[0] / (float) hdr->skinwidth;
		st->st[1] = (float) src->st[1] / (float) hdr->skinheight;
	}

	hr = buf->Stream1->Unlock ();
	if (FAILED (hr)) Sys_Error ("D3DAlias_CreateStream1Buffer: failed to unlock vertex buffer");
}


void D3DAlias_CreateIndexBuffer (aliashdr_t *hdr, aliasbuffers_t *buf)
{
	hr = d3d_Device->CreateIndexBuffer
	(
		hdr->numindexes * sizeof (unsigned short),
		D3DUSAGE_WRITEONLY,
		D3DFMT_INDEX16,
		D3DPOOL_DEFAULT,
		&buf->Indexes,
		NULL
	);

	if (FAILED (hr)) Sys_Error ("D3DAlias_CreateIndexBuffer: d3d_Device->CreateIndexBuffer failed");

	// now we fill in the index buffer; this is a non-dynamic index buffer and it only needs to be set once
	unsigned short *ndx = NULL;

	hr = buf->Indexes->Lock (0, 0, (void **) &ndx, 0);
	if (FAILED (hr)) Sys_Error ("D3DAlias_CreateIndexBuffer: failed to lock index buffer");

	memcpy (ndx, hdr->indexes, hdr->numindexes * sizeof (unsigned short));

	hr = buf->Indexes->Unlock ();
	if (FAILED (hr)) Sys_Error ("D3DAlias_CreateIndexBuffer: failed to unlock index buffer");
}


void D3DAlias_CreateBuffers (void)
{
	model_t *mod;

	// go through all the models
	for (int i = 0; i < MAX_MODELS; i++)
	{
		aliasbuffers_t *buf = &d3d_AliasBuffers[i];

		// clear down any buffers previously associated with this model
		SAFE_RELEASE (buf->Stream0);
		SAFE_RELEASE (buf->Stream1);
		SAFE_RELEASE (buf->Indexes);

		// note - we set the end of the precache list to NULL in cl_parse to ensure this test is valid
		if (!cl.model_precache) continue;	// this can happen on a game change but we still run the main loop to release what was there
		if (!(mod = cl.model_precache[i])) continue;
		if (mod->type != mod_alias) continue;

		aliashdr_t *hdr = mod->aliashdr;
		hdr->buffernum = i;
		hdr->cacheposes = 0xffffffff;

		// create the other buffers used by this mdl
		D3DAlias_CreateIndexBuffer (hdr, buf);
		D3DAlias_CreateStream0Buffer (hdr, buf);
		D3DAlias_CreateStream1Buffer (hdr, buf);
	}

	// create everything else we need
	if (!d3d_AliasDecl)
	{
		D3DVERTEXELEMENT9 d3d_aliaslayout[] =
		{
			// positions are sent as bytes to save on bandwidth and storage
			// position 0 can't be d3dcolor as it fucks with fog (why???)
			{0, 0, D3DDECLTYPE_UBYTE4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
			{0, 4, D3DDECLTYPE_UBYTE4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 1},
			{0, 8, D3DDECLTYPE_UBYTE4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},	// this is really a normal
			{0, 12, D3DDECLTYPE_UBYTE4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1},	// and so is this
			{1, 0, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 2},
			D3DDECL_END ()
		};

		hr = d3d_Device->CreateVertexDeclaration (d3d_aliaslayout, &d3d_AliasDecl);
		if (FAILED (hr)) Sys_Error ("D3DAlias_CreateBuffers: d3d_Device->CreateVertexDeclaration failed");
	}

	if (!d3d_ShadowDecl)
	{
		D3DVERTEXELEMENT9 d3d_shadowlayout[] =
		{
			// positions are sent as bytes to save on bandwidth and storage
			// position 0 can't be d3dcolor as it fucks with fog (why???)
			{0, 0, D3DDECLTYPE_UBYTE4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
			{0, 4, D3DDECLTYPE_UBYTE4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 1},
			D3DDECL_END ()
		};

		hr = d3d_Device->CreateVertexDeclaration (d3d_shadowlayout, &d3d_ShadowDecl);
		if (FAILED (hr)) Sys_Error ("D3DAlias_CreateBuffers: d3d_Device->CreateVertexDeclaration failed");
	}

	if (!d3d_ShadedotsTexture)
	{
		hr = d3d_Device->CreateTexture (256, 1, 1, 0, D3DFMT_L8, D3DPOOL_MANAGED, &d3d_ShadedotsTexture, NULL);
		if (FAILED (hr)) Sys_Error ("D3DAlias_CreateBuffers: d3d_Device->CreateTexture failed");

		D3DLOCKED_RECT lockrect;
		byte *dots;

		hr = d3d_ShadedotsTexture->LockRect (0, &lockrect, NULL, 0);
		if (FAILED (hr)) Sys_Error ("D3DAlias_CreateBuffers: failed to lock texture rectangle");

		dots = (byte *) lockrect.pBits;

		for (int i = 0; i < 256; i++)
			dots[i] = i;

		hr = d3d_ShadedotsTexture->UnlockRect (0);
		if (FAILED (hr)) Sys_Error ("D3DAlias_CreateBuffers: failed to unlock texture rectangle");
	}
}


void D3DAlias_ReleaseBuffers (void)
{
	for (int i = 0; i < MAX_MODELS; i++)
	{
		SAFE_RELEASE (d3d_AliasBuffers[i].Stream0);
		SAFE_RELEASE (d3d_AliasBuffers[i].Stream1);
		SAFE_RELEASE (d3d_AliasBuffers[i].Indexes);
	}

	SAFE_RELEASE (d3d_AliasDecl);
	SAFE_RELEASE (d3d_ShadowDecl);
}


CD3DDeviceLossHandler d3d_AliasBuffersHandler (D3DAlias_ReleaseBuffers, D3DAlias_CreateBuffers);


typedef struct d3d_aliasstate_s
{
	image_t *lasttexture;
	image_t *lastluma;
	aliashdr_t *lastmodel;
} d3d_aliasstate_t;

d3d_aliasstate_t d3d_AliasState;


void D3DAlias_TextureChange (aliasstate_t *aliasstate)
{
	// update textures if necessary
	if ((aliasstate->teximage != d3d_AliasState.lasttexture) || (aliasstate->lumaimage != d3d_AliasState.lastluma))
	{
		if (aliasstate->lumaimage)
		{
			D3DHLSL_SetPass (FX_PASS_ALIAS_LUMA);
			D3DHLSL_SetTexture (0, aliasstate->teximage->d3d_Texture);
			D3DHLSL_SetTexture (1, aliasstate->lumaimage->d3d_Texture);
		}
		else
		{
			D3DHLSL_SetPass (FX_PASS_ALIAS_NOLUMA);
			D3DHLSL_SetTexture (0, aliasstate->teximage->d3d_Texture);
		}

		d3d_AliasState.lasttexture = aliasstate->teximage;
		d3d_AliasState.lastluma = aliasstate->lumaimage;
	}

	// always set the shadedots texture (it will be filtered if set more than once)
	D3DHLSL_SetTexture (2, d3d_ShadedotsTexture);
}


#define CACHE_ENCODE_POSES(e) (((e)->currpose << 16) + (e)->lastpose)

void D3DAlias_InterpolateStreams (entity_t *ent, aliashdr_t *hdr, aliasstate_t *aliasstate)
{
	// if this changes the full model needs to be refreshed
	if (hdr->cacheposes == CACHE_ENCODE_POSES (ent))
	{
		// unchanged so use the cached version
		// Con_Printf ("cached %s\n", ent->model->name);
		return;
	}

	// recache or cache for the first time
	hdr->cacheposes = CACHE_ENCODE_POSES (ent);
	// Con_Printf ("uncached %s\n", ent->model->name);

	aliasstream0_t *xyz = NULL;
	aliasmesh_t *src = hdr->meshverts;
	aliasbuffers_t *buf = &d3d_AliasBuffers[hdr->buffernum];

	hr = buf->Stream0->Lock (0, 0, (void **) &xyz, D3DLOCK_DISCARD);
	if (FAILED (hr)) Sys_Error ("D3DAlias_DrawModel: failed to lock vertex buffer");

	for (int i = 0; i < hdr->nummesh; i++, src++, xyz++)
	{
		drawvertx_t *lastverts = hdr->vertexes[ent->lastpose] + src->vertindex;
		drawvertx_t *currverts = hdr->vertexes[ent->currpose] + src->vertindex;

		// if we're not lerping away from the previous set of verts we use the current set for everything
		if (!lastverts->lerpvert) lastverts = currverts;

		// positions and normals are sent as bytes to save on bandwidth and storage
		xyz->lastxyz = lastverts->xyz;
		xyz->currxyz = currverts->xyz;

		xyz->lastnormal = lastverts->normal1dw;
		xyz->currnormal = currverts->normal1dw;
	}

	hr = buf->Stream0->Unlock ();
	d3d_RenderDef.numlock++;
	if (FAILED (hr)) Sys_Error ("D3DAlias_DrawModel: failed to unlock vertex buffer");
}


void D3DAlias_ClampLighting (float *ambientlight, float *shadelight)
{
	// correct rgb to intensity scaling factors
	float ntsc[] = {0.3f, 0.59f, 0.11f};

	// convert light values to intensity values so that we can preserve the final correct colour balance
	float ambientintensity = DotProduct (ambientlight, ntsc);
	float shadeintensity = DotProduct (shadelight, ntsc);

	// prevent division by zero (clamping is not needed at these levels either)
	if (ambientintensity < 1) return;
	if (shadeintensity < 1) return;

	// scale down now before we modify the intensities
	VectorScale (ambientlight, (1.0f / ambientintensity), ambientlight);
	VectorScale (shadelight, (1.0f / shadeintensity), shadelight);

	// clamp lighting so that it doesn't overbright as much
	if (ambientintensity > r_aliasambientcutoff.value) ambientintensity = r_aliasambientcutoff.value;

	if (ambientintensity + shadeintensity > r_aliaslightingcutoff.value)
		shadeintensity = r_aliaslightingcutoff.value - ambientintensity;

	// now scale them back up to the new values preserving colour balance
	VectorScale (ambientlight, ambientintensity, ambientlight);
	VectorScale (shadelight, shadeintensity, shadelight);
}


void D3DAlias_DrawModel (entity_t *ent, aliashdr_t *hdr, bool shadowed)
{
	// this is used for shadows as well as regular drawing otherwise shadows might not cache colours
	aliasstate_t *aliasstate = &ent->aliasstate;

	// the interpolation is optional and depends on whether or not anything cached is valid
	D3DAlias_InterpolateStreams (ent, hdr, aliasstate);

	// stream 0 and indexes are common whether or not we're doing shadows
	D3D_SetStreamSource (0, d3d_AliasBuffers[hdr->buffernum].Stream0, 0, sizeof (aliasstream0_t));
	D3D_SetIndices (d3d_AliasBuffers[hdr->buffernum].Indexes);

	// initialize entity matrix to identity; what happens with it next depends on whether it is a shadow or not
	D3DMatrix_Identity (&ent->matrix);

	if (shadowed)
	{
		// build the transformation for this entity.  shadows only rotate around Z
		D3DMatrix_Translate (&ent->matrix, ent->origin[0], ent->origin[1], ent->origin[2]);
		D3DMatrix_Rotate (&ent->matrix, 0, 0, 1, ent->angles[1]);

		// shadows only need the position stream for drawing
		D3D_SetStreamSource (1, NULL, 0, 0);
		D3D_SetStreamSource (2, NULL, 0, 0);

		// shadows just reuse the ShadeVector uniform for the lightspot as a convenience measure
		D3DHLSL_SetFloatArray ("ShadeVector", aliasstate->lightspot, 3);
	}
	else
	{
		// build the transformation for this entity.  the full model gets a full rotation
		D3D_RotateForEntity (ent, &ent->matrix);

		// the full model needs the texcoord stream too
		D3D_SetStreamSource (1, d3d_AliasBuffers[hdr->buffernum].Stream1, 0, sizeof (aliasstream1_t));
		D3D_SetStreamSource (2, NULL, 0, 0);

		D3DHLSL_SetAlpha ((float) ent->alphaval / 255.0f);

		// set up other stuff for lighting
		// here we replicate software quake lighting because we're just cool like that ;)
		float shadevector[3], lightvec[3] = {-1, 0, 0};
		float alias_forward[3], alias_right[3], alias_up[3];

		ent->angles[0] = -ent->angles[0];	// stupid quake bug
		AngleVectors (ent->angles, alias_forward, alias_right, alias_up);
		ent->angles[0] = -ent->angles[0];	// stupid quake bug

		// rotate the lighting vector into the model's frame of reference
		shadevector[0] = DotProduct (lightvec, alias_forward);
		shadevector[1] = -DotProduct (lightvec, alias_right);
		shadevector[2] = DotProduct (lightvec, alias_up);

		// copy these out because we need to keep the originals in the entity for frame averaging
		vec3_t shadelight = {ent->shadelight[0], ent->shadelight[1], ent->shadelight[2]};
		vec3_t ambientlight = {ent->ambientlight[0], ent->ambientlight[1], ent->ambientlight[2]};

		// clamp lighting so that it doesn't overbright so much
		// (regular overbright scaling is done in D3DLight_LightPoint
		D3DAlias_ClampLighting (ambientlight, shadelight);

		// nehahra assumes that fullbrights are not available in the engine
		if (nehahra)
		{
			if (!strcmp (ent->model->name, "progs/flame2.mdl") || !strcmp (ent->model->name, "progs/flame.mdl"))
			{
				ambientlight[0] = shadelight[0] = 256;
				ambientlight[1] = shadelight[1] = 256;
				ambientlight[2] = shadelight[2] = 256;
			}
		}

		// and now scale them by the correct factor; the default scale assumes no overbrighting
		// we use different scaling on the view ent as it comes out really dark with this light model
		if (ent == &cl.viewent)
		{
			// the software quake lighting model subtracts dot rather than adds it so multiply shadelight by -1 to reflect this
			// (the shader will still add)
			VectorScale (shadelight, -(r_aliaslightscale.value / 128.0f), shadelight);
			VectorScale (ambientlight, (r_aliaslightscale.value / 128.0f), ambientlight);
		}
		else
		{
			// the software quake lighting model subtracts dot rather than adds it so multiply shadelight by -1 to reflect this
			// (the shader will still add)
			VectorScale (shadelight, -(r_aliaslightscale.value / 200.0f), shadelight);
			VectorScale (ambientlight, (r_aliaslightscale.value / 200.0f), ambientlight);
		}

		D3DHLSL_SetFloatArray ("ShadeVector", shadevector, 3);
		D3DHLSL_SetFloatArray ("AmbientLight", ambientlight, 3);
		D3DHLSL_SetFloatArray ("ShadeLight", shadelight, 3);
	}

	// the scaling needs to be included at this time
	D3DMatrix_Translate (&ent->matrix, hdr->scale_origin[0], hdr->scale_origin[1], hdr->scale_origin[2]);
	D3DMatrix_Scale (&ent->matrix, hdr->scale[0], hdr->scale[1], hdr->scale[2]);

	D3DHLSL_SetEntMatrix (&ent->matrix);

	D3DHLSL_SetCurrLerp (aliasstate->currlerp);
	D3DHLSL_SetLastLerp (aliasstate->lastlerp);

	D3D_DrawIndexedPrimitive (0, hdr->nummesh, 0, hdr->numindexes / 3);
	d3d_RenderDef.alias_polys += hdr->numtris;
}


/*
=============================================================

  ALIAS MODELS

=============================================================
*/

float D3DAlias_MeshScaleVert (aliashdr_t *hdr, drawvertx_t *invert, int index)
{
	float outvert = invert->v[index];

	outvert *= hdr->scale[index];
	outvert += hdr->scale_origin[index];

	return outvert;
}


/*
===================
D3DAlias_DelerpMuzzleFlashes

Done at runtime (once only per model) because there is no guarantee that a viewmodel
will follow the naming convention used by ID.  As a result, the only way we have to
be certain that a model is a viewmodel is when we come to draw the viewmodel.
===================
*/
void D3DAlias_DelerpMuzzleFlashes (aliashdr_t *hdr)
{
	// already delerped
	if (hdr->mfdelerp) return;

	// shrak crashes as it has viewmodels with only one frame
	// who woulda ever thought!!!
	if (hdr->numframes < 2) return;

	// done now
	hdr->mfdelerp = true;

	// get pointers to the verts
	drawvertx_t *vertsf0 = hdr->vertexes[0];
	drawvertx_t *vertsf1 = hdr->vertexes[1];
	drawvertx_t *vertsfi;

	// now go through them and compare.  we expect that (a) the animation is sensible and there's no major
	// difference between the 2 frames to be expected, and (b) any verts that do exhibit a major difference
	// can be assumed to belong to the muzzleflash
	for (int j = 0; j < hdr->vertsperframe; j++, vertsf0++, vertsf1++)
	{
		// get difference in front to back movement
		float vdiff = D3DAlias_MeshScaleVert (hdr, vertsf1, 0) - D3DAlias_MeshScaleVert (hdr, vertsf0, 0);

		// if it's above a certain treshold, assume a muzzleflash and mark for no lerp
		// 10 is the approx lowest range of visible front to back in a view model, so that seems reasonable to work with
		if (vdiff > 10)
			vertsf0->lerpvert = false;
		else vertsf0->lerpvert = true;
	}

	// now we mark every other vert in the model as per the instructions in the first frame
	for (int i = 1; i < hdr->numframes; i++)
	{
		// get pointers to the verts
		vertsf0 = hdr->vertexes[0];
		vertsfi = hdr->vertexes[i];

		for (int j = 0; j < hdr->vertsperframe; j++, vertsf0++, vertsfi++)
		{
			// just copy it across
			vertsfi->lerpvert = vertsf0->lerpvert;
		}
	}
}



extern vec3_t lightspot;
extern mplane_t *lightplane;

void D3DAlias_DrawAliasShadows (entity_t **ents, int numents)
{
	// this is a hack to get around a non-zero r_shadows being triggered by a combination of r_shadows 0 and
	// low precision FP rounding errors, thereby causing unnecesary slowdowns.
	byte shadealpha = BYTE_CLAMPF (r_shadows.value);

	if (shadealpha < 1) return;

	// only allow intermediate steps if we have a stencil buffer
	if (d3d_GlobalCaps.DepthStencilFormat != D3DFMT_D24S8) shadealpha = 255;

	bool stateset = false;
	DWORD shadecolor = D3DCOLOR_ARGB (shadealpha, 0, 0, 0);

	for (int i = 0; i < numents; i++)
	{
		entity_t *ent = ents[i];

		if (ent->alphaval < 255) continue;
		if (ent->visframe != d3d_RenderDef.framecount) continue;

		// easier access
		aliasstate_t *aliasstate = &ent->aliasstate;
		aliashdr_t *hdr = ent->model->aliashdr;

		// don't crash
		if (!aliasstate->lightplane) continue;

		if (!stateset)
		{
			// state for shadows
			D3D_SetRenderState (D3DRS_ZWRITEENABLE, FALSE);
			D3D_SetRenderState (D3DRS_ALPHABLENDENABLE, TRUE);
			D3D_SetRenderState (D3DRS_BLENDOP, D3DBLENDOP_ADD);
			D3D_SetRenderState (D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
			D3D_SetRenderState (D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

			if (d3d_GlobalCaps.DepthStencilFormat == D3DFMT_D24S8)
			{
				// of course, we all know that Direct3D lacks Stencil Buffer and Polygon Offset support,
				// so what you're looking at here doesn't really exist.  Those of you who froth at the mouth
				// and like to think it's still the 1990s had probably better look away now.
				D3D_SetRenderState (D3DRS_STENCILENABLE, TRUE);
				D3D_SetRenderState (D3DRS_STENCILFUNC, D3DCMP_EQUAL);
				D3D_SetRenderState (D3DRS_STENCILREF, 0x00000001);
				D3D_SetRenderState (D3DRS_STENCILMASK, 0x00000002);
				D3D_SetRenderState (D3DRS_STENCILWRITEMASK, 0xFFFFFFFF);
				D3D_SetRenderState (D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP);
				D3D_SetRenderState (D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP);
				D3D_SetRenderState (D3DRS_STENCILPASS, D3DSTENCILOP_INCRSAT);
			}

			D3DHLSL_SetPass (FX_PASS_SHADOW);
			D3DHLSL_SetAlpha (r_shadows.value);

			D3D_SetVertexDeclaration (d3d_ShadowDecl);

			stateset = true;
		}

		// a commit is always required so don't bother with the pending flag and just send it
		// (but reset the pending flag to false so that it's correct)
		D3DAlias_DrawModel (ent, hdr, true);
	}

	if (stateset)
	{
		// back to normal
		if (d3d_GlobalCaps.DepthStencilFormat == D3DFMT_D24S8)
			D3D_SetRenderState (D3DRS_STENCILENABLE, FALSE);

		D3D_SetRenderState (D3DRS_ALPHABLENDENABLE, FALSE);
		D3D_SetRenderState (D3DRS_ZWRITEENABLE, TRUE);
	}
}


/*
=================
D3DAlias_SetupAliasFrame

=================
*/
extern cvar_t r_lerpframe;

float D3DAlias_SetupAliasFrame (entity_t *ent, aliashdr_t *hdr)
{
	// blend is on a coarser scale (0-10 integer instead of 0-1 float) so that we get better cache hits
	// and can do better comparison/etc for cache checking; this still gives 10 x smoothing so it's OK
	int pose, numposes;
	float interval;
	bool lerpmdl = true;
	int lerpblend;
	float frame_interval;

	// silently revert to frame 0
	if ((ent->frame >= hdr->numframes) || (ent->frame < 0)) ent->frame = 0;

	pose = hdr->frames[ent->frame].firstpose;
	numposes = hdr->frames[ent->frame].numposes;

	if (numposes > 1)
	{
		// client-side animations
		interval = hdr->frames[ent->frame].interval;

		// software quake adds syncbase here so that animations using the same model aren't in lockstep
		// trouble is that syncbase is always 0 for them so we provide new fields for it instead...
		pose += (int) ((d3d_RenderDef.time + ent->posebase) / interval) % numposes;

		// not really needed for non-interpolated mdls, but does no harm
		frame_interval = interval;
	}
	else if (ent->lerpflags & LERP_FINISH)
		frame_interval = ent->lerpinterval;
	else frame_interval = 0.1;

	// conditions for turning lerping off (the SNG bug is no longer an issue)
	if (hdr->nummeshframes == 1) lerpmdl = false;			// only one pose
	if (ent->lastpose == ent->currpose) lerpmdl = false;		// both frames are identical
	if (!r_lerpframe.value) lerpmdl = false;

	// interpolation
	if (ent->currpose == -1 || ent->lastpose == -1)
	{
		// new entity so reset to no interpolation initially
		ent->framestarttime = d3d_RenderDef.time;
		ent->lastpose = ent->currpose = pose;
		lerpblend = 0;
	}
	else if (ent->lastpose == ent->currpose && ent->currpose == 0 && ent != &cl.viewent)
	{
		// "dying throes" interpolation bug - begin a new sequence with both poses the same
		// this happens when an entity is spawned client-side
		ent->framestarttime = d3d_RenderDef.time;
		ent->lastpose = ent->currpose = pose;
		lerpblend = 0;
	}
	else if (pose == 0 && ent == &cl.viewent)
	{
		// don't interpolate from previous pose on frame 0 of the viewent
		ent->framestarttime = d3d_RenderDef.time;
		ent->lastpose = ent->currpose = pose;
		lerpblend = 0;
	}
	else if (ent->currpose != pose || !lerpmdl)
	{
		// begin a new interpolation sequence
		ent->framestarttime = d3d_RenderDef.time;
		ent->lastpose = ent->currpose;
		ent->currpose = pose;
		lerpblend = 0;
	}
	else lerpblend = (int) (((d3d_RenderDef.time - ent->framestarttime) / frame_interval) * 10.0f);

	// if a viewmodel is switched and the previous had a current frame number higher than the number of frames
	// in the new one, DirectQ will crash so we need to fix that.  this is also a general case sanity check.
	if (ent->lastpose >= hdr->nummeshframes) ent->lastpose = ent->currpose = 0; else if (ent->lastpose < 0) ent->lastpose = ent->currpose = hdr->nummeshframes - 1;
	if (ent->currpose >= hdr->nummeshframes) ent->lastpose = ent->currpose = 0; else if (ent->currpose < 0) ent->lastpose = ent->currpose = hdr->nummeshframes - 1;

	// don't let blend pass 1
	if (cl.paused || lerpblend > 10) lerpblend = 10;

	return (((float) lerpblend) / 10.0f);
}


// to do -
// a more robust general method is needed.  use the middle line of the colormap, always store out texels
// detect if the colormap changes and work on the actual proper texture instead of a copy in the playerskins array
// no, because we're not creating a colormap for the entity any more.  in fact storing the colormap in the ent
// and the translation in cl.scores is now largely redundant...
// also no because working on the skin directly will break with instanced models
bool D3DAlias_TranslateAliasSkin (entity_t *ent)
{
	// no translation
	if (ent->playerskin < 0) return false;
	if (!ent->model) return false;
	if (ent->model->type != mod_alias) return false;
	if (!(ent->model->flags & EF_PLAYER)) return false;

	// sanity
	ent->playerskin &= 255;

	// already built a skin for this colour
	if (d3d_PlayerSkins[ent->playerskin].d3d_Texture) return true;

	byte	translate[256];
	byte	*original;
	static int skinsize = -1;
	static byte *translated = NULL;

	aliashdr_t *paliashdr = ent->model->aliashdr;
	int s = paliashdr->skinwidth * paliashdr->skinheight;

	if (ent->skinnum < 0 || ent->skinnum >= paliashdr->numskins)
	{
		Con_Printf ("(%d): Invalid player skin #%d\n", ent->playerskin, ent->skinnum);
		original = paliashdr->skins[0].texels;
	}
	else original = paliashdr->skins[ent->skinnum].texels;

	// no texels were saved
	if (!original) return false;

	if (s & 3)
	{
		Con_Printf ("D3DAlias_TranslateAliasSkin: s & 3\n");
		return false;
	}

	int top = ent->playerskin & 0xf0;
	int bottom = (ent->playerskin & 15) << 4;

	// baseline has no palette translation
	for (int i = 0; i < 256; i++) translate[i] = i;

	for (int i = 0; i < 16; i++)
	{
		if (top < 128)	// the artists made some backwards ranges.  sigh.
			translate[TOP_RANGE + i] = top + i;
		else translate[TOP_RANGE + i] = top + 15 - i;

		if (bottom < 128)
			translate[BOTTOM_RANGE + i] = bottom + i;
		else translate[BOTTOM_RANGE + i] = bottom + 15 - i;
	}

	// recreate the texture
	SAFE_RELEASE (d3d_PlayerSkins[ent->playerskin].d3d_Texture);

	// check for size change
	if (skinsize != s)
	{
		// cache the size
		skinsize = s;

		// free the current buffer
		if (translated) Zone_Free (translated);

		translated = NULL;
	}

	// create a new buffer only if required (more optimization)
	if (!translated) translated = (byte *) Zone_Alloc (s);

	for (int i = 0; i < s; i += 4)
	{
		translated[i] = translate[original[i]];
		translated[i + 1] = translate[original[i + 1]];
		translated[i + 2] = translate[original[i + 2]];
		translated[i + 3] = translate[original[i + 3]];
	}

	// don't compress these because it takes too long
	D3D_UploadTexture
	(
		&d3d_PlayerSkins[ent->playerskin].d3d_Texture,
		translated,
		paliashdr->skinwidth,
		paliashdr->skinheight,
		IMAGE_MIPMAP | IMAGE_NOEXTERN | IMAGE_NOCOMPRESS
	);

	// Con_Printf ("Translated skin to %i\n", ent->playerskin);
	// success
	return true;
}


void D3DAlias_SetupAliasModel (entity_t *ent)
{
	vec3_t mins, maxs;

	// take pointers for easier access
	aliashdr_t *hdr = ent->model->aliashdr;
	aliasstate_t *aliasstate = &ent->aliasstate;

	// assume that the model has been culled
	ent->visframe = -1;

	// revert to full model culling here
	VectorAdd (ent->origin, ent->model->mins, mins);
	VectorAdd (ent->origin, ent->model->maxs, maxs);

	// the gun or the chase model are never culled away
	if (ent == cl_entities[cl.viewentity] && chase_active.value)
		;	// no bbox culling on certain entities
	else if (ent->nocullbox)
		;	// no bbox culling on certain entities
	else if (R_CullBox (mins, maxs))
	{
		// no further setup needed
		return;
	}

	// the model has not been culled now
	ent->visframe = d3d_RenderDef.framecount;
	aliasstate->lightplane = NULL;

	if (ent == cl_entities[cl.viewentity] && chase_active.value)
	{
		// adjust angles (done here instead of chase_update as we only want the angle for rendering to be adjusted)
		ent->angles[0] *= 0.3;
	}

	// setup the frame for drawing and store the interpolation blend
	// this can't be moved back to the client because static entities and the viewmodel need to go through it
	float blend = D3DAlias_SetupAliasFrame (ent, hdr);

	// use cubic interpolation
	aliasstate->lastlerp = 1.0f - blend;
	aliasstate->currlerp = blend;

	// get lighting information
	vec3_t shadelight;
	D3DLight_LightPoint (ent, shadelight);

	// average light between frames
	for (int i = 0; i < 3; i++)
	{
		// we can't rescale shadelight to the range we need here because it will mess with the averaging so instead we do it in the shader
		shadelight[i] = (shadelight[i] + ent->shadelight[i]) / 2;
		ent->shadelight[i] = ent->ambientlight[i] = shadelight[i];
	}

	// store out for shadows
	VectorCopy (lightspot, aliasstate->lightspot);
	aliasstate->lightplane = lightplane;

	// get texturing info
	// software quake randomises the base animation and so should we
	int anim = (int) ((d3d_RenderDef.time + ent->skinbase) * 10) & 3;

	// switch the entity to a skin texture at &d3d_PlayerSkins[ent->playerskin]
	// move all skin translation to here (only if translation succeeds)
	if (D3DAlias_TranslateAliasSkin (ent))
	{
		aliasstate->teximage = &d3d_PlayerSkins[ent->playerskin];
		aliasstate->lumaimage = hdr->skins[ent->skinnum].lumaimage[anim];
		d3d_PlayerSkins[ent->playerskin].LastUsage = 0;
	}
	else
	{
		aliasstate->teximage = hdr->skins[ent->skinnum].teximage[anim];
		aliasstate->lumaimage = hdr->skins[ent->skinnum].lumaimage[anim];
	}

	// cache the poses in the ent so that we can access them there too
	ent->cacheposes = CACHE_ENCODE_POSES (ent);
}


void D3DAlias_DrawAliasBatch (entity_t **ents, int numents)
{
	bool stateset = false;

	d3d_AliasState.lasttexture = NULL;
	d3d_AliasState.lastluma = NULL;

	for (int i = 0; i < numents; i++)
	{
		entity_t *ent = ents[i];
		aliashdr_t *hdr = ent->model->aliashdr;

		// skip conditions
		if (ent->visframe != d3d_RenderDef.framecount) continue;

		// take pointers for easier access
		aliasstate_t *aliasstate = &ent->aliasstate;

		// prydon gets this
		if (!aliasstate->teximage) continue;
		if (!aliasstate->teximage->d3d_Texture) continue;

		if (!stateset)
		{
			D3D_SetTextureMipmap (0, d3d_TexFilter, d3d_MipFilter);
			D3D_SetTextureMipmap (1, d3d_TexFilter, d3d_MipFilter);
			D3D_SetTextureMipmap (2, D3DTEXF_LINEAR, D3DTEXF_NONE);
			D3D_SetTextureAddressMode (D3DTADDRESS_CLAMP, D3DTADDRESS_CLAMP, D3DTADDRESS_CLAMP);

			D3D_SetVertexDeclaration (d3d_AliasDecl);

			stateset = true;
		}

		// interpolation clearing gets this
		if (ent->currpose < 0) ent->currpose = 0;
		if (ent->lastpose < 0) ent->lastpose = 0;

		D3DAlias_TextureChange (aliasstate);
		D3DAlias_DrawModel (ent, hdr, false);
	}
}


entity_t **d3d_AliasEdicts = NULL;
int d3d_NumAliasEdicts = 0;


int D3DAlias_ModelSortFunc (entity_t **e1, entity_t **e2)
{
	// sort so that the same pose is likely to be used more often
	if (e1[0]->model == e2[0]->model)
		return (e1[0]->model->aliashdr->cacheposes - e2[0]->model->aliashdr->cacheposes);
	return (int) (e1[0]->model - e2[0]->model);
}


void D3DAlias_RenderAliasModels (void)
{
	if (!r_drawentities.integer) return;
	if (!d3d_AliasEdicts) d3d_AliasEdicts = (entity_t **) Zone_Alloc (sizeof (entity_t *) * MAX_VISEDICTS);

	d3d_NumAliasEdicts = 0;

	for (int i = 0; i < d3d_RenderDef.numvisedicts; i++)
	{
		entity_t *ent = d3d_RenderDef.visedicts[i];

		if (!ent->model) continue;
		if (ent->model->type != mod_alias) continue;

		// initial setup
		D3DAlias_SetupAliasModel (ent);

		if (ent->visframe != d3d_RenderDef.framecount) continue;

		if (ent->alphaval < 255)
			D3DAlpha_AddToList (ent);
		else d3d_AliasEdicts[d3d_NumAliasEdicts++] = ent;
	}

	if (!d3d_NumAliasEdicts) return;

	// sort the alias edicts by model
	// (to do - chain these in a list instead to save memory, remove limits and run faster...)
	qsort
	(
		d3d_AliasEdicts,
		d3d_NumAliasEdicts,
		sizeof (entity_t *),
		(int (*) (const void *, const void *)) D3DAlias_ModelSortFunc
	);

	// draw in two passes to prevent excessive shader switching
	D3DAlias_DrawAliasBatch (d3d_AliasEdicts, d3d_NumAliasEdicts);
	D3DAlias_DrawAliasShadows (d3d_AliasEdicts, d3d_NumAliasEdicts);
}


void D3D_SetupProjection (float fovx, float fovy);
float SCR_CalcFovX (float fov_y, float width, float height);
float SCR_CalcFovY (float fov_x, float width, float height);

void D3DAlias_DrawViewModel (void)
{
	// conditions for switching off view model
	if (r_drawviewmodel.value < 0.01f) return;
	if (chase_active.value) return;
	if (cl.stats[STAT_HEALTH] <= 0) return;
	if (!cl.viewent.model) return;
	if (!r_drawentities.value) return;
	if (d3d_RenderDef.automap) return;

	// select view ent
	entity_t *ent = &cl.viewent;

	// the viewmodel should always be an alias model
	if (ent->model->type != mod_alias) return;

	// never check for bbox culling on the viewmodel
	ent->nocullbox = true;

	// the viewmodel is always fully transformed
	ent->rotated = ent->translated = true;

	// delerp muzzleflashes here
	D3DAlias_DelerpMuzzleFlashes (ent->model->aliashdr);

	if ((cl.items & IT_INVISIBILITY) || r_drawviewmodel.value < 0.99f)
	{
		// initial alpha
		ent->alphaval = (int) (r_drawviewmodel.value * 255.0f);

		// adjust for invisibility
		if (cl.items & IT_INVISIBILITY) ent->alphaval >>= 1;

		// final range
		ent->alphaval = BYTE_CLAMP (ent->alphaval);

		// enable blending
		D3D_SetRenderState (D3DRS_ALPHABLENDENABLE, TRUE);
		D3D_SetRenderState (D3DRS_BLENDOP, D3DBLENDOP_ADD);
		D3D_SetRenderState (D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
		D3D_SetRenderState (D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

		// leave this because it looks ugly if we don't (compare the view model...)
		// D3D_SetRenderState (D3DRS_ZWRITEENABLE, FALSE);
	}

	extern cvar_t scr_fov;
	extern cvar_t scr_fovcompat;

	if (scr_fov.value > 90 && !scr_fovcompat.integer)
	{
		// recalculate the correct fov for displaying the gun model as if fov was 90
		float fov_y = SCR_CalcFovY (90, 640, 432);
		float fov_x = SCR_CalcFovX (fov_y, r_refdef.vrect.width, r_refdef.vrect.height);

		// adjust projection to a constant y fov for wide-angle views
		D3D_SetupProjection (fov_x, fov_y);

		// calculate concatenated final matrix for use by shaders
		// because it's only needed once per frame instead of once per vertex we can save some vs instructions
		D3DMatrix_Multiply (&d3d_ModelViewProjMatrix, &d3d_ViewMatrix, &d3d_WorldMatrix);
		D3DMatrix_Multiply (&d3d_ModelViewProjMatrix, &d3d_ProjMatrix);

		// we don't need to extract the frustum as the gun is never frustum-culled
		D3DHLSL_SetWorldMatrix (&d3d_ModelViewProjMatrix);
	}

	// hack the depth range to prevent the view model from poking into walls
	// this prevents having to adjust the depth range at runtime thus allowing Intel to run better
	// we have better precision in the near range of the depth buffer so this can be smaller than GLQuake's 0.3f
	D3DHLSL_SetDepthBias (0.15f);

	// add it to the list
	D3DAlias_SetupAliasModel (ent);
	D3DAlias_DrawAliasBatch (&ent, 1);

	// restore the hacked depth range
	D3DHLSL_SetDepthBias (1.0f);

	// restore alpha
	ent->alphaval = 255;

	// restoring the original projection is unnecessary as the gun is the last thing drawn in the 3D view
	if ((cl.items & IT_INVISIBILITY) || r_drawviewmodel.value < 0.99f)
	{
		D3D_SetRenderState (D3DRS_ZWRITEENABLE, TRUE);
		D3D_SetRenderState (D3DRS_ALPHABLENDENABLE, FALSE);
	}
}


