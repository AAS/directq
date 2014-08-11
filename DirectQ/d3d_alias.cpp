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

#include "quakedef.h"
#include "d3d_model.h"
#include "d3d_quake.h"
#include "resource.h"

extern LPDIRECT3DTEXTURE9 d3d_PaletteRowTextures[];
LPDIRECT3DTEXTURE9 shadetexture;

void D3DLight_LightPoint (entity_t *e);

cvar_t r_aliaslightscale ("r_aliaslightscale", "1", CVAR_ARCHIVE);

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

	// only optimize with hardware T&L as there have been reports of it being slower with software (no idea why - must check...)
	if (d3d_GlobalCaps.supportHardwareTandL)
	{
		// optimize the mesh - we can pull temp storage off the hunk here because we're gonna free it anyway
		// most of the following code was inspired by the example at http://psp.jim.sh/svn/comp.php?repname=ps3ware&compare[]=/@136&compare[]=/@137
		// i reworked it to support my data types and naming and removed all of the STL gross-out crap.
		// this was the *only* *practical* example of these functions usage i could find, everything else just says
		// "use them" but doesn't actually say *how* (indicating that a lot of folks don't really know what they're talking about...)
		int numtris = hdr->numindexes / 3;
		unsigned short *newindexes = (unsigned short *) MainHunk->Alloc (hdr->numindexes * sizeof (unsigned short));
		DWORD *optresult = (DWORD *) MainHunk->Alloc ((hdr->nummesh > numtris ? hdr->nummesh : numtris) * sizeof (DWORD));
		aliasmesh_t *newvertexes = (aliasmesh_t *) MainHunk->Alloc (hdr->nummesh * sizeof (aliasmesh_t));
		DWORD *remap = (DWORD *) MainHunk->Alloc (hdr->nummesh * sizeof (DWORD));

		D3DXOptimizeFaces (hdr->indexes, numtris, hdr->nummesh, FALSE, optresult);

		for (int i = 0; i < numtris; i++)
		{
			int src = optresult[i] * 3;
			int dst = i * 3;

			newindexes[dst + 0] = hdr->indexes[src + 0];
			newindexes[dst + 1] = hdr->indexes[src + 1];
			newindexes[dst + 2] = hdr->indexes[src + 2];
		}

		D3DXOptimizeVertices (newindexes, numtris, hdr->nummesh, FALSE, optresult);

		for (int i = 0; i < hdr->nummesh; i++)
		{
			memcpy (&newvertexes[i], &hdr->meshverts[optresult[i]], sizeof (aliasmesh_t));
			remap[optresult[i]] = i;
		}

		for (int i = 0; i < hdr->numindexes; i++)
			hdr->indexes[i] = remap[newindexes[i]];

		// cache alloc from the optimized vertexes
		hdr->meshverts = (aliasmesh_t *) MainCache->Alloc (newvertexes, hdr->nummesh * sizeof (aliasmesh_t));
	}
	else
	{
		// alloc a final for-real buffer for the mesh verts
		hdr->meshverts = (aliasmesh_t *) MainCache->Alloc (hdr->meshverts, hdr->nummesh * sizeof (aliasmesh_t));
	}

	// free our hunk memory
	MainHunk->FreeToLowMark (hunkmark);

	// alloc space for stream offsets - deferred to here because the vertex buffer size and layout will change from map to map
	hdr->streamoffsets = (int *) MainCache->Alloc (hdr->nummeshframes * sizeof (int));

	// calculate drawflags
	hdr->drawflags = 0;
	char *Name = strrchr (name, '/');

	if (Name)
	{
		Name++;

		// this list was more or less lifted straight from Bengt Jardrup's engine.  Personally I think that hard-coding
		// behaviours like this into the engine is evil, but there are mods that depend on it so oh well.
		// At least I guess it's *standardized* evil...
		if (!strcmp (name, "progs/flame.mdl") || !strcmp (name, "progs/flame2.mdl"))
			hdr->drawflags |= (AM_NOSHADOW | AM_FULLBRIGHT);
		else if (!strcmp (name, "progs/eyes.mdl"))
			hdr->drawflags |= AM_EYES;
		else if (!strcmp (name, "progs/bolt.mdl"))
			hdr->drawflags |= AM_NOSHADOW;
		else if (!strncmp (Name, "flame", 5) || !strncmp (Name, "torch", 5) || !strcmp (name, "progs/missile.mdl") ||
			!strcmp (Name, "newfire.mdl") || !strcmp (Name, "longtrch.mdl") || !strcmp (Name, "bm_reap.mdl"))
			hdr->drawflags |= (AM_NOSHADOW | AM_FULLBRIGHT);
		else if (!strncmp (Name, "lantern", 7) ||
			 !strcmp (Name, "brazshrt.mdl") ||  // For Chapters ...
			 !strcmp (Name, "braztall.mdl"))
			hdr->drawflags |= (AM_NOSHADOW | AM_FULLBRIGHT);
		else if (!strncmp (Name, "bolt", 4) ||	    // Bolts ...
			 !strcmp (Name, "s_light.mdl"))
			hdr->drawflags |= (AM_NOSHADOW | AM_FULLBRIGHT);
		else if (!strncmp (Name, "candle", 6))
			hdr->drawflags |= (AM_NOSHADOW | AM_FULLBRIGHT);
		else if ((!strcmp (Name, "necro.mdl") ||
			 !strcmp (Name, "wizard.mdl") ||
			 !strcmp (Name, "wraith.mdl")) && nehahra)	    // Nehahra
			hdr->drawflags |= AM_NOSHADOW;
		else if (!strcmp (Name, "beam.mdl") ||	    // Rogue
			 !strcmp (Name, "dragon.mdl") ||    // Rogue
			 !strcmp (Name, "eel2.mdl") ||	    // Rogue
			 !strcmp (Name, "fish.mdl") ||
			 !strcmp (Name, "flak.mdl") ||	    // Marcher
			 (Name[0] != 'v' && !strcmp (&Name[1], "_spike.mdl")) ||
			 !strcmp (Name, "imp.mdl") ||
			 !strcmp (Name, "laser.mdl") ||
			 !strcmp (Name, "lasrspik.mdl") ||  // Hipnotic
			 !strcmp (Name, "lspike.mdl") ||    // Rogue
			 !strncmp (Name, "plasma", 6) ||    // Rogue
			 !strcmp (Name, "spike.mdl") ||
			 !strncmp (Name, "tree", 4) ||
			 !strcmp (Name, "wr_spike.mdl"))    // Nehahra
			hdr->drawflags |= AM_NOSHADOW;
	}
}


/*
===================
DelerpMuzzleFlashes

Done at runtime (once only per model) because there is no guarantee that a viewmodel
will follow the naming convention used by ID.  As a result, the only way we have to
be certain that a model is a viewmodel is when we come to draw the viewmodel.
===================
*/
float Mesh_ScaleVert (aliashdr_t *hdr, drawvertx_t *invert, int index)
{
	float outvert = invert->v[index];

	outvert *= hdr->scale[index];
	outvert += hdr->scale_origin[index];

	return outvert;
}


void DelerpMuzzleFlashes (aliashdr_t *hdr)
{
	// shrak crashes as it has viewmodels with only one frame
	// who woulda ever thought!!!
	if (hdr->numframes < 2) return;

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
		float vdiff = Mesh_ScaleVert (hdr, vertsf1, 0) - Mesh_ScaleVert (hdr, vertsf0, 0);

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


typedef struct aliasvertexstream_s
{
	// positions are sent as bytes to save on bandwidth and storage
	// also keep the names consistent with drawvertx_t
	DWORD xyz;
	float normal[3];
} aliasvertexstream_t;

typedef struct aliastexcoordstream_s
{
	float st[2];
	float blendindex;
} aliastexcoordstream_t;


LPDIRECT3DVERTEXDECLARATION9 d3d_AliasDecl = NULL;
LPDIRECT3DVERTEXDECLARATION9 d3d_ShadowDecl = NULL;
LPDIRECT3DVERTEXDECLARATION9 d3d_InstanceDecl = NULL;

typedef struct aliasinstance_s
{
	float matrix[16];
	float lerps[2];
	float svec[3];
	float rgba[4];
} aliasinstance_t;


LPDIRECT3DINDEXBUFFER9 d3d_AliasIndexes;
LPDIRECT3DVERTEXBUFFER9 d3d_AliasVertexes;
LPDIRECT3DVERTEXBUFFER9 d3d_AliasTexCoords;

#define MAX_ALIAS_INSTANCES		16384

LPDIRECT3DVERTEXBUFFER9 d3d_AliasInstances;
int d3d_NumAliasInstances = 0;

extern float r_avertexnormals[162][3];

int d3d_LastTotalIndexes = 0;
int d3d_LastTotalMesh = 0;
int d3d_LastTotalVerts = 0;

void D3DAlias_CreateBuffers (void)
{
	model_t *mod = NULL;
	aliashdr_t *hdr = NULL;
	int TotalIndexes = 0;
	int TotalMesh = 0;
	int TotalVerts = 0;

	// go through all the models
	for (int i = 0; i < MAX_MOD_KNOWN; i++)
	{
		// nothing allocated yet
		if (!mod_known) continue;
		if (!(mod = mod_known[i])) continue;
		if (mod->type != mod_alias) continue;

		hdr = mod->aliashdr;

		// we don't yet know if this mdl is going to be a viewmodel so we take no chances and just do it
		DelerpMuzzleFlashes (hdr);

		// fill in stream offsets for this model
		for (int m = 0; m < hdr->nummeshframes; m++)
		{
			hdr->streamoffsets[m] = TotalVerts * sizeof (aliasvertexstream_t);
			TotalVerts += hdr->nummesh;
		}

		hdr->firstindex = TotalIndexes;
		TotalIndexes += hdr->numindexes;

		hdr->meshoffset = TotalMesh * sizeof (aliastexcoordstream_t);
		TotalMesh += hdr->nummesh;
	}

	// release any buffers which were previously created
	// (reusing the buffer if the previous one was bigger)
	if (TotalIndexes > d3d_LastTotalIndexes)
	{
		SAFE_RELEASE (d3d_AliasIndexes);
		d3d_LastTotalIndexes = TotalIndexes;
	}

	if (TotalMesh > d3d_LastTotalMesh)
	{
		SAFE_RELEASE (d3d_AliasTexCoords);
		d3d_LastTotalMesh = TotalMesh;
	}

	if (TotalVerts > d3d_LastTotalVerts)
	{
		SAFE_RELEASE (d3d_AliasVertexes);
		d3d_LastTotalVerts = TotalVerts;
	}

	if (TotalVerts && TotalIndexes && TotalMesh)
	{
		if (!d3d_AliasIndexes) D3DMain_CreateIndexBuffer16 (TotalIndexes, D3DUSAGE_WRITEONLY, &d3d_AliasIndexes);
		if (!d3d_AliasVertexes) D3DMain_CreateVertexBuffer (TotalVerts * sizeof (aliasvertexstream_t), D3DUSAGE_WRITEONLY, &d3d_AliasVertexes);
		if (!d3d_AliasTexCoords) D3DMain_CreateVertexBuffer (TotalMesh * sizeof (aliastexcoordstream_t), D3DUSAGE_WRITEONLY, &d3d_AliasTexCoords);

		aliasvertexstream_t *verts = NULL;
		unsigned short *ndx = NULL;
		aliastexcoordstream_t *stverts = NULL;

		hr = d3d_AliasIndexes->Lock (0, 0, (void **) &ndx, d3d_GlobalCaps.DefaultLock);
		if (FAILED (hr)) Sys_Error ("D3DAlias_CreateBuffers: failed to lock index buffer");

		hr = d3d_AliasVertexes->Lock (0, 0, (void **) &verts, d3d_GlobalCaps.DefaultLock);
		if (FAILED (hr)) Sys_Error ("D3DAlias_CreateBuffers: failed to lock vertex buffer");

		hr = d3d_AliasTexCoords->Lock (0, 0, (void **) &stverts, d3d_GlobalCaps.DefaultLock);
		if (FAILED (hr)) Sys_Error ("D3DAlias_CreateBuffers: failed to lock vertex buffer");

		for (int i = 0; i < MAX_MOD_KNOWN; i++)
		{
			// nothing allocated yet
			if (!mod_known) continue;
			if (!(mod = mod_known[i])) continue;
			if (mod->type != mod_alias) continue;

			hdr = mod->aliashdr;

			for (int m = 0; m < hdr->nummeshframes; m++)
			{
				aliasmesh_t *src = hdr->meshverts;
				drawvertx_t *dv = hdr->vertexes[m];

				for (int mm = 0; mm < hdr->nummesh; mm++)
				{
					// positions are sent as bytes to save on bandwidth and storage
					verts[mm].xyz = dv[src[mm].vertindex].xyz;
					verts[mm].normal[0] = r_avertexnormals[dv[src[mm].vertindex].lightnormalindex][0];
					verts[mm].normal[1] = r_avertexnormals[dv[src[mm].vertindex].lightnormalindex][1];
					verts[mm].normal[2] = r_avertexnormals[dv[src[mm].vertindex].lightnormalindex][2];
				}

				verts += hdr->nummesh;
			}

			memcpy (ndx, hdr->indexes, hdr->numindexes * sizeof (unsigned short));
			ndx += hdr->numindexes;

			for (int m = 0; m < hdr->nummesh; m++)
			{
				// convert back to floating point and store out
				stverts[m].st[0] = (float) hdr->meshverts[m].st[0] / (float) hdr->skinwidth;
				stverts[m].st[1] = (float) hdr->meshverts[m].st[1] / (float) hdr->skinheight;

				// blendindexes for the viewmodel piggyback on the texcoord stream
				// we might find something to use them for with other models sometime...
				if (hdr->vertexes[0][hdr->meshverts[m].vertindex].lerpvert)
					stverts[m].blendindex = 0;
				else stverts[m].blendindex = 1;
			}

			stverts += hdr->nummesh;
		}

		hr = d3d_AliasTexCoords->Unlock ();
		d3d_RenderDef.numlock++;
		if (FAILED (hr)) Sys_Error ("D3DAlias_CreateBuffers: failed to unlock vertex buffer");

		hr = d3d_AliasIndexes->Unlock ();
		d3d_RenderDef.numlock++;
		if (FAILED (hr)) Sys_Error ("D3DAlias_CreateBuffers: failed to unlock index buffer");

		hr = d3d_AliasVertexes->Unlock ();
		d3d_RenderDef.numlock++;
		if (FAILED (hr)) Sys_Error ("D3DAlias_CreateBuffers: failed to unlock vertex buffer");
	}

	// create everything else we need
	if (!d3d_AliasDecl)
	{
		D3DVERTEXELEMENT9 d3d_aliaslayout[] =
		{
			// positions are sent as bytes to save on bandwidth and storage
			// position 0 can't be d3dcolor as it fucks with fog (why???)
			VDECL (0, 0, D3DDECLTYPE_UBYTE4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0),
			VDECL (0, 4, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0),	// this is really a normal
			VDECL (1, 0, D3DDECLTYPE_UBYTE4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 1),
			VDECL (1, 4, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1),	// and so is this
			VDECL (2, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 2),
			D3DDECL_END ()
		};

		hr = d3d_Device->CreateVertexDeclaration (d3d_aliaslayout, &d3d_AliasDecl);
		if (FAILED (hr)) Sys_Error ("D3DAlias_CreateBuffers: d3d_Device->CreateVertexDeclaration failed");
	}

	if (d3d_GlobalCaps.supportInstancing)
	{
		if (!d3d_AliasInstances)
			D3DMain_CreateVertexBuffer (MAX_ALIAS_INSTANCES * sizeof (aliasinstance_t), D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, &d3d_AliasInstances);

		// do a discard lock on this so that we're good to go on the new map
		D3D_PrelockVertexBuffer (d3d_AliasInstances);
		d3d_NumAliasInstances = 0;
	}

	if (!d3d_InstanceDecl && d3d_GlobalCaps.supportInstancing)
	{
		D3DVERTEXELEMENT9 d3d_instancedlayout[] =
		{
			// positions are sent as bytes to save on bandwidth and storage
			// position 0 can't be d3dcolor as it fucks with fog (why???)
			VDECL (0,  0, D3DDECLTYPE_UBYTE4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0),
			VDECL (0,  4, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0),	// this is really a normal
			VDECL (1,  0, D3DDECLTYPE_UBYTE4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 1),
			VDECL (1,  4, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1),	// and so is this
			VDECL (2,  0, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 2),
			VDECL (3,  0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 3),	// matrix row 1
			VDECL (3, 16, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 4),	// matrix row 2
			VDECL (3, 32, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 5),	// matrix row 3
			VDECL (3, 48, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 6),	// matrix row 4
			VDECL (3, 64, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 7),	// blends combined
			VDECL (3, 72, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 8),	// shadevector
			VDECL (3, 84, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 9),	// shadelight and alpha combined
			D3DDECL_END ()
		};

		hr = d3d_Device->CreateVertexDeclaration (d3d_instancedlayout, &d3d_InstanceDecl);
		if (FAILED (hr)) Sys_Error ("D3DAlias_CreateBuffers: d3d_Device->CreateVertexDeclaration failed");
	}

	if (!d3d_ShadowDecl)
	{
		D3DVERTEXELEMENT9 d3d_shadowlayout[] =
		{
			// positions are sent as bytes to save on bandwidth and storage
			// position 0 can't be d3dcolor as it fucks with fog (why???)
			VDECL (0, 0, D3DDECLTYPE_UBYTE4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0),
			VDECL (0, 4, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0),	// this is really a normal
			VDECL (1, 0, D3DDECLTYPE_UBYTE4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 1),
			VDECL (1, 4, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1),	// and so is this
			D3DDECL_END ()
		};

		hr = d3d_Device->CreateVertexDeclaration (d3d_shadowlayout, &d3d_ShadowDecl);
		if (FAILED (hr)) Sys_Error ("D3DAlias_CreateBuffers: d3d_Device->CreateVertexDeclaration failed");
	}
}


void D3DAlias_ReleaseBuffers (void)
{
	SAFE_RELEASE (d3d_AliasInstances);
	SAFE_RELEASE (d3d_AliasIndexes);
	SAFE_RELEASE (d3d_AliasTexCoords);
	SAFE_RELEASE (d3d_AliasVertexes);
	SAFE_RELEASE (d3d_InstanceDecl);
	SAFE_RELEASE (d3d_AliasDecl);
	SAFE_RELEASE (d3d_ShadowDecl);

	d3d_LastTotalIndexes = 0;
	d3d_LastTotalMesh = 0;
	d3d_LastTotalVerts = 0;
}


CD3DDeviceLossHandler d3d_AliasBuffersHandler (D3DAlias_ReleaseBuffers, D3DAlias_CreateBuffers);


typedef struct d3d_aliasstate_s
{
	LPDIRECT3DTEXTURE9 lastcmap;
	LPDIRECT3DTEXTURE9 lastshirt;
	LPDIRECT3DTEXTURE9 lastpants;
	LPDIRECT3DTEXTURE9 lasttexture;
	LPDIRECT3DTEXTURE9 lastluma;
	aliashdr_t *lastmodel;
} d3d_aliasstate_t;

d3d_aliasstate_t d3d_AliasState;


void D3DAlias_SetupLighting (entity_t *ent, aliasinstance_t *instance)
{
	if (instance)
	{
		// copy in - NOTE - this is a vertex buffer destination and should NOT be read from!!!
		instance->svec[0] = ent->shadevector[0];
		instance->svec[1] = ent->shadevector[1];
		instance->svec[2] = ent->shadevector[2];

		instance->rgba[0] = ent->shadelight[0];
		instance->rgba[1] = ent->shadelight[1];
		instance->rgba[2] = ent->shadelight[2];

		if (ent->alphaval > 0 && ent->alphaval < 255)
			instance->rgba[3] = (float) ent->alphaval / 255.0f;
		else instance->rgba[3] = 1.0f;
	}
	else
	{
		D3DHLSL_SetFloatArray ("ShadeVector", ent->shadevector, 3);
		D3DHLSL_SetFloatArray ("ShadeLight", ent->shadelight, 3);

		if (ent->alphaval > 0 && ent->alphaval < 255)
			D3DHLSL_SetAlpha ((float) ent->alphaval / 255.0f);
		else D3DHLSL_SetAlpha (1.0f);
	}
}


// fix terminal spellcheck failure in the original cvar
cvar_alias_t gl_doubleeyes_spellcheck ("gl_doubleeyes", &gl_doubleeyes);

void D3DAlias_TransformFinal (entity_t *ent, aliashdr_t *hdr)
{
	if ((hdr->drawflags & AM_EYES) && gl_doubleeyes.value > 0.0f)
	{
		// the scaling needs to be included at this time
		float sc = gl_doubleeyes.value + 1.0f;

		// position the eyes consistently
		ent->matrix.Translate
		(
			hdr->scale_origin[0] * sc,
			hdr->scale_origin[1] * sc,
			hdr->scale_origin[2] * sc - (gl_doubleeyes.value * 25.0f)
		);

		ent->matrix.Scale (hdr->scale[0] * sc, hdr->scale[1] * sc, hdr->scale[2] * sc);
	}
	else
	{
		// the scaling needs to be included at this time
		ent->matrix.Translate (hdr->scale_origin);
		ent->matrix.Scale (hdr->scale);
	}
}


void D3DAlias_TransformStandard (entity_t *ent)
{
	ent->matrix.LoadMatrix (&d3d_ModelViewProjMatrix);
	ent->matrix.Translate (ent->origin);

	if (ent->angles[1]) ent->matrix.Rotate (0, 0, 1, ent->angles[1]);
	if (ent->angles[0]) ent->matrix.Rotate (0, 1, 0, -ent->angles[0]);
	if (ent->angles[2]) ent->matrix.Rotate (1, 0, 0, ent->angles[2]);
}


#define SHADOW_SKEW_X	-0.7	// skew along x axis. -0.7 to mimic glquake shadows
#define SHADOW_SKEW_Y	0		// skew along y axis. 0 to mimic glquake shadows
#define SHADOW_VSCALE	0		// 0 = completely flat
#define SHADOW_HEIGHT	0.1		// how far above the floor to render the shadow

QMATRIX r_shadowmatrix (
	1,				0,				0,				0,
	0,				1,				0,				0,
	SHADOW_SKEW_X,	SHADOW_SKEW_Y,	SHADOW_VSCALE,	0,
	0,				0,				SHADOW_HEIGHT,	1
);


void D3DAlias_TransformShadowed (entity_t *ent)
{
	ent->matrix.LoadMatrix (&d3d_ModelViewProjMatrix);
	ent->matrix.Translate (ent->origin);

	ent->matrix.Translate (0, 0, -(ent->origin[2] - ent->lightspot[2]));
	ent->matrix.MultMatrix (&r_shadowmatrix);
	ent->matrix.Translate (0, 0, (ent->origin[2] - ent->lightspot[2]));

	ent->matrix.Rotate (0, 0, 1, ent->angles[1]);
}


void D3DAlias_SetVertexBuffers (entity_t *ent, aliashdr_t *hdr, int numinstances = 1)
{
	if (numinstances > 1)
	{
		UINT freq = D3DSTREAMSOURCE_INDEXEDDATA | numinstances;

		D3D_SetStreamSource (0, d3d_AliasVertexes, hdr->streamoffsets[ent->currpose], sizeof (aliasvertexstream_t), freq);
		D3D_SetStreamSource (1, d3d_AliasVertexes, hdr->streamoffsets[ent->lastpose], sizeof (aliasvertexstream_t), freq);
		D3D_SetStreamSource (2, d3d_AliasTexCoords, hdr->meshoffset, sizeof (aliastexcoordstream_t), freq);

		D3D_SetStreamSource (3, d3d_AliasInstances, d3d_NumAliasInstances * sizeof (aliasinstance_t), sizeof (aliasinstance_t), D3DSTREAMSOURCE_INSTANCEDATA | 1);
		d3d_NumAliasInstances += numinstances;
	}
	else
	{
		D3D_SetStreamSource (0, d3d_AliasVertexes, hdr->streamoffsets[ent->currpose], sizeof (aliasvertexstream_t));
		D3D_SetStreamSource (1, d3d_AliasVertexes, hdr->streamoffsets[ent->lastpose], sizeof (aliasvertexstream_t));
		D3D_SetStreamSource (2, d3d_AliasTexCoords, hdr->meshoffset, sizeof (aliastexcoordstream_t));

		// stream 3 is never used outside of MDLs and needs nulling here in case the previous batch was instanced
		D3D_SetStreamSource (3, NULL, 0, 0);
	}

	D3D_SetIndices (d3d_AliasIndexes);
}


void D3DAlias_DrawModel (entity_t *ent, aliashdr_t *hdr, int flags = 0)
{
	// this is used for shadows as well as regular drawing otherwise shadows might not cache normals
	aliasstate_t *state = &ent->aliasstate;

	// set up vertx buffers for this entity
	D3DAlias_SetVertexBuffers (ent, hdr);

	if (flags & AM_DRAWSHADOW)
	{
		// build the transforms for the entity
		D3DAlias_TransformShadowed (ent);
		D3DAlias_TransformFinal (ent, hdr);
	}
	else
	{
		// build the transforms for the entity
		D3DAlias_TransformStandard (ent);
		D3DAlias_TransformFinal (ent, hdr);
		D3DAlias_SetupLighting (ent, NULL);
	}

	// stream 3 is never used outside of MDLs and needs nulling here in case the previous batch was instanced
	//D3D_SetStreamSource (3, NULL, 0, 0);

	if (flags & AM_VIEWMODEL)
	{
		// set up the blend factors; vmblends[0] is indexed if a vertex should interpolate, vmblends[1] is indexed if it should not
		D3DXVECTOR4 vmblends[2];

		vmblends[0].x = ent->poseblend;
		vmblends[0].y = (1.0f - ent->poseblend);

		vmblends[1].x = 1;
		vmblends[1].y = 0;

		D3DHLSL_SetVectorArray ("vmblends", vmblends, 2);
	}
	else
	{
		D3DHLSL_SetLerp (ent->poseblend, (1.0f - ent->poseblend));
	}

	D3DHLSL_SetEntMatrix (&ent->matrix);

	D3D_DrawIndexedPrimitive (0, hdr->nummesh, hdr->firstindex, hdr->numindexes / 3);
	d3d_RenderDef.alias_polys += hdr->numtris;

	// the viewmodel uses stream 3 so we need to explicitly kill it here
	if (flags & AM_VIEWMODEL) D3D_SetStreamSource (3, NULL, 0, 0);
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


void D3DAlias_DrawAliasShadows (entity_t **ents, int numents)
{
	if (!(r_shadows.value > 0)) return;

	bool stateset = false;

	for (int i = 0; i < numents; i++)
	{
		entity_t *ent = ents[i];

		if (ent->alphaval > 0 && ent->alphaval < 255) continue;
		if (ent->visframe != d3d_RenderDef.framecount) continue;

		// easier access
		aliasstate_t *state = &ent->aliasstate;
		aliashdr_t *hdr = ent->model->aliashdr;

		// don't crash
		if (!ent->lightplane) continue;

		// these entities don't have shadows
		if (hdr->drawflags & AM_NOSHADOW) continue;

		if (!stateset)
		{
			// state for shadows
			D3DState_EnableShadows (true);
			D3DHLSL_SetPass (FX_PASS_SHADOW);
			D3D_SetVertexDeclaration (d3d_ShadowDecl);

			stateset = true;
		}

		// a commit is always required so don't bother with the pending flag and just send it
		// (but reset the pending flag to false so that it's correct)
		D3DAlias_DrawModel (ent, hdr, AM_DRAWSHADOW);
	}

	if (stateset)
	{
		// back to normal
		D3DState_EnableShadows (false);
	}
}


/*
=================
D3DAlias_SetupFrame

=================
*/
extern cvar_t r_lerpframe;

// split out for use by IQMs too - should this move to a different module? (misc?  model?  main?)
// pose needs to be separated from ent owing to alias framegroups
void D3DAlias_LerpToFrame (entity_t *ent, int pose, float interval)
{
	if (!r_lerpframe.value) ent->lerpflags |= LERP_RESETANIM;

	// buncha hacks to fix up buggy Quake models
	if (pose == 0 && ent == &cl.viewent) ent->lerpflags |= LERP_RESETANIM;
	if (ent->currpose == ent->lastpose && ent->currpose == 0 && ent != &cl.viewent) ent->lerpflags |= LERP_RESETANIM;

	if (ent->lerpflags & LERP_RESETANIM)
	{
		// kill any lerp in progress
		ent->lastpose = ent->currpose = pose;
		ent->framestarttime = cl.time;
		ent->poseblend = 1;
		ent->lerpflags &= ~LERP_RESETANIM;
	}
	else if (pose != ent->currpose)
	{
		// begin a lerp to a new frame
		ent->lastpose = ent->currpose;
		ent->currpose = pose;
		ent->framestarttime = cl.time;
		ent->poseblend = 0;
	}
	else ent->poseblend = (cl.time - ent->framestarttime) / interval;

	// don't let blend pass 1
	if (cl.paused) ent->poseblend = 0;
	if (ent->poseblend > 1) ent->poseblend = 1;
	if (ent->poseblend < 0) ent->poseblend = 0;
}


void D3DAlias_SetupFrame (entity_t *ent, aliashdr_t *hdr)
{
	float frame_interval;

	// silently revert to frame 0
	if ((ent->frame >= hdr->numframes) || (ent->frame < 0)) ent->frame = 0;

	int pose = hdr->frames[ent->frame].firstpose;
	int numposes = hdr->frames[ent->frame].numposes;

	if (numposes > 1)
	{
		// let's do this right
		float fullinterval = hdr->frames[ent->frame].intervals[numposes - 1];

		if (fullinterval > 0)
		{
			float time = cl.time + ent->syncbase;
			float targettime = time - ((int) (time / fullinterval)) * fullinterval;

			if (hdr->frames[ent->frame].intervals[0] > targettime)
				frame_interval = hdr->frames[ent->frame].intervals[0];
			else
			{
				for (int i = 1; i < numposes; i++)
				{
					if (hdr->frames[ent->frame].intervals[i] > targettime)
					{
						pose += i;
						frame_interval = hdr->frames[ent->frame].intervals[i] - hdr->frames[ent->frame].intervals[i - 1];
						break;
					}
				}
			}
		}
		else frame_interval = 0.1f;
	}
	else if (ent->lerpflags & LERP_FINISH)
		frame_interval = ent->lerpinterval;
	else frame_interval = 0.1;

	D3DAlias_LerpToFrame (ent, pose, frame_interval);
}


void D3DAlias_SetupAliasModel (entity_t *ent)
{
	// take pointers for easier access
	aliashdr_t *hdr = ent->model->aliashdr;
	aliasstate_t *state = &ent->aliasstate;

	// assume that the model has been culled
	ent->visframe = -1;

	// the gun or the chase model are never culled away
	if (ent == cl_entities[cl.viewentity] && chase_active.value)
		;	// no bbox culling on certain entities
	else if (ent->nocullbox)
		;	// no bbox culling on certain entities
	else
	{
		// and now check it for culling
		if (R_CullBox (ent->mins, ent->maxs, frustum))
		{
			CL_ClearInterpolation (ent, CLEAR_ALLLERP);
			return;
		}
	}

	// the model has not been culled now
	ent->visframe = d3d_RenderDef.framecount;
	ent->lightplane = NULL;

	if (ent == cl_entities[cl.viewentity] && chase_active.value)
	{
		// adjust angles (done here instead of chase_update as we only want the angle for rendering to be adjusted)
		ent->angles[0] *= 0.3;
	}

	// get lighting information
	D3DLight_LightPoint (ent);

	// get texturing info
	// software quake randomises the base animation and so should we
	int anim = (int) ((cl.time + ent->syncbase) * 10) & 3;

	// select the proper skins (this can now work with any model, not just the player)
	// (although in it's current incarnation the supporting infrastructure just works with the player)
	state->teximage = hdr->skins[ent->skinnum].teximage[anim];
	state->lumaimage = hdr->skins[ent->skinnum].lumaimage[anim];

	// nehahra uses player.mdl for non-players :(
	if (ent->entnum >= 1 && ent->entnum <= cl.maxclients && (ent->model->flags & EF_PLAYER))
		state->cmapimage = hdr->skins[ent->skinnum].cmapimage[anim];
	else state->cmapimage = NULL;

	// build the sort order (which may be inverted for more optimal reuse)
	// keeping the lowest first ensures that it's more likely to be reused, which works out well for quake anims
	ent->sortpose[0] = ent->currpose < ent->lastpose ? ent->lastpose : ent->currpose;
	ent->sortpose[1] = ent->currpose < ent->lastpose ? ent->currpose : ent->lastpose;
}


void D3DAlias_DrawAliasBatch (entity_t **ents, int numents)
{
	bool stateset = false;

	d3d_AliasState.lasttexture = NULL;
	d3d_AliasState.lastluma = NULL;
	d3d_AliasState.lastcmap = NULL;
	d3d_AliasState.lastshirt = NULL;
	d3d_AliasState.lastpants = NULL;

	for (int i = 0; i < numents; i++)
	{
		entity_t *ent = ents[i];
		aliashdr_t *hdr = ent->model->aliashdr;

		// skip conditions
		if (ent->visframe != d3d_RenderDef.framecount) continue;

		// take pointers for easier access
		aliasstate_t *state = &ent->aliasstate;

		// prydon gets this
		if (!state->teximage) continue;
		if (!state->teximage) continue;

		if (!stateset)
		{
			D3D_SetTextureMipmap (0, d3d_TexFilter, d3d_MipFilter);
			D3D_SetTextureMipmap (1, d3d_TexFilter, d3d_MipFilter);

			D3D_SetTextureAddress (0, D3DTADDRESS_CLAMP);
			D3D_SetTextureAddress (1, D3DTADDRESS_CLAMP);

			D3D_SetVertexDeclaration (d3d_AliasDecl);

			stateset = true;
		}

		// interpolation clearing gets this
		if (ent->currpose < 0) ent->currpose = 0;
		if (ent->lastpose < 0) ent->lastpose = 0;

		if ((ent->model->flags & EF_PLAYER) && state->cmapimage && !gl_nocolors.value)
		{
			// convert player skin colours back to 0..15 range each
			// the new palette texture will automatically handle backward ranges
			int shirt = (ent->playerskin & 0xf0) >> 4;
			int pants = ent->playerskin & 15;

			if (state->cmapimage != d3d_AliasState.lastcmap)
			{
				// the colormap must always go in tmu2
				D3DHLSL_SetTexture (2, state->cmapimage);
				d3d_AliasState.lastcmap = state->cmapimage;
			}

			if (d3d_PaletteRowTextures[shirt] != d3d_AliasState.lastshirt)
			{
				// each row of the palette is a separate texture to prevent bilerp bleeding
				D3DHLSL_SetTexture (3, d3d_PaletteRowTextures[shirt]);		// and the palette into tmu3
				d3d_AliasState.lastshirt = d3d_PaletteRowTextures[shirt];
			}

			if (d3d_PaletteRowTextures[pants] != d3d_AliasState.lastpants)
			{
				// each row of the palette is a separate texture to prevent bilerp bleeding
				D3DHLSL_SetTexture (4, d3d_PaletteRowTextures[pants]);		// and the palette into tmu4
				d3d_AliasState.lastpants = d3d_PaletteRowTextures[pants];
			}

			D3D_SetTextureMipmap (2, d3d_TexFilter, d3d_MipFilter);
			D3D_SetTextureMipmap (3, d3d_TexFilter, D3DTEXF_NONE);
			D3D_SetTextureMipmap (4, d3d_TexFilter, D3DTEXF_NONE);

			D3D_SetTextureAddress (2, D3DTADDRESS_CLAMP);
			D3D_SetTextureAddress (3, D3DTADDRESS_CLAMP);
			D3D_SetTextureAddress (4, D3DTADDRESS_CLAMP);
		}

		// update textures if necessary
		if ((state->teximage != d3d_AliasState.lasttexture) || (state->lumaimage != d3d_AliasState.lastluma))
		{
			if (state->lumaimage && gl_fullbrights.integer)
			{
				if ((ent->model->flags & EF_PLAYER) && state->cmapimage && !gl_nocolors.value)
					D3DHLSL_SetPass (FX_PASS_ALIAS_PLAYER_LUMA);
				else D3DHLSL_SetPass (FX_PASS_ALIAS_LUMA);

				D3DHLSL_SetTexture (0, state->teximage);
				D3DHLSL_SetTexture (1, state->lumaimage);
			}
			else
			{
				if ((ent->model->flags & EF_PLAYER) && state->cmapimage && !gl_nocolors.value)
					D3DHLSL_SetPass (FX_PASS_ALIAS_PLAYER_NOLUMA);
				else D3DHLSL_SetPass (FX_PASS_ALIAS_NOLUMA);

				D3DHLSL_SetTexture (0, state->teximage);
			}

			d3d_AliasState.lasttexture = state->teximage;
			d3d_AliasState.lastluma = state->lumaimage;
		}

		D3DAlias_DrawModel (ent, hdr);
	}
}


entity_t **d3d_AliasEdicts = NULL;
int d3d_NumAliasEdicts = 0;


int D3DAlias_ModelSortFunc (entity_t **e1, entity_t **e2)
{
	// sort so that the same pose is likely to be used more often
	// (should this use current poses instead of the cached poses???
	if (e1[0]->model == e2[0]->model)
		return (e1[0]->sortorder - e2[0]->sortorder);
	return (int) (e1[0]->model - e2[0]->model);
}


void D3DAlias_DrawInstances (entity_t **ents, int numents, int firstent, int numinstances)
{
	if (d3d_GlobalCaps.supportInstancing && numinstances > d3d_usinginstancing.integer)
	{
		// Con_Printf ("D3DAlias_DrawInstances : %3i %3i\n", firstent, numinstances);

		aliasinstance_t *instances = NULL;

		if (d3d_NumAliasInstances + numinstances >= MAX_ALIAS_INSTANCES)
		{
			d3d_AliasInstances->Lock (0, 0, (void **) &instances, d3d_GlobalCaps.DiscardLock);
			d3d_NumAliasInstances = 0;
		}
		else
		{
			d3d_AliasInstances->Lock (d3d_NumAliasInstances * sizeof (aliasinstance_t),
				numinstances * sizeof (aliasinstance_t),
				(void **) &instances,
				d3d_GlobalCaps.NoOverwriteLock);
		}

		for (int i = 0; i < numinstances; i++, instances++)
		{
			entity_t *ent = ents[firstent + i];

			D3DAlias_TransformStandard (ent);
			D3DAlias_TransformFinal (ent, ent->model->aliashdr);

			memcpy (instances->matrix, ent->matrix.m, sizeof (float) * 16);

			instances->lerps[0] = ent->poseblend;
			instances->lerps[1] = (1.0f - ent->poseblend);

			D3DAlias_SetupLighting (ent, instances);

			// this count is even more irrelevant now...
			d3d_RenderDef.alias_polys += ent->model->aliashdr->numtris;
		}

		hr = d3d_AliasInstances->Unlock ();
		if (FAILED (hr)) Sys_Error ("D3DAlias_DrawInstances : failed to unlock vertex buffer");
		d3d_RenderDef.numlock++;

		// we just need one valid entity here so take the first one; the model
		// data will be identical for the rest of them... (we've already ensured that in the sort func)
		aliashdr_t *hdr = ents[firstent]->model->aliashdr;
		aliasstate_t *state = &ents[firstent]->aliasstate;

		// set texture and choose the correct shader
		if (state->lumaimage && gl_fullbrights.integer)
		{
			D3DHLSL_SetPass (FX_PASS_ALIAS_INSTANCED_LUMA);
			D3DHLSL_SetTexture (0, state->teximage);
			D3DHLSL_SetTexture (1, state->lumaimage);
		}
		else
		{
			D3DHLSL_SetPass (FX_PASS_ALIAS_INSTANCED_NOLUMA);
			D3DHLSL_SetTexture (0, state->teximage);
		}

		// vertex buffers
		D3DAlias_SetVertexBuffers (ents[firstent], hdr, numinstances);

		// get the vertex decl right using our new instanced decl
		D3D_SetVertexDeclaration (d3d_InstanceDecl);

		// draw (as if there were only one model)
		D3D_DrawIndexedPrimitive (0, hdr->nummesh, hdr->firstindex, hdr->numindexes / 3);

		// testing
		// Con_Printf ("Drew %i instances\n", numinstances);
	}
	else if (numinstances == 1)
	{
		// just the one
		D3DAlias_DrawAliasBatch (&ents[firstent], 1);
	}
	else if (numinstances)
	{
		// non-instanced version just draws everything individually
		for (int i = 0; i < numinstances; i++)
		{
			entity_t *ent = ents[firstent + i];
			D3DAlias_DrawAliasBatch (&ent, 1);
		}
	}
}


void D3DAlias_DrawInstanced (entity_t **ents, int numents)
{
	entity_t *lastent = NULL;
	bool breakbatch = false;
	int numinstances = 0;
	int firstent = 0;

	for (int i = 0; i < numents; i++)
	{
		entity_t *ent = ents[i];

		// this is not necessary as it's already been checked in the setup function
		// if (ent->visframe != d3d_RenderDef.framecount) continue;

		if (lastent)
		{
			// check basic stuff; note that currpose and lastpose could be the same but swapped around
			// so we optimize for this when setting up the model for drawing... (done)
			if (ent->model != lastent->model) breakbatch = true;
			if (ent->currpose != lastent->currpose) breakbatch = true;
			if (ent->lastpose != lastent->lastpose) breakbatch = true;

			// check textures instead of skinnum as the texture can change for the player model
			if (ent->aliasstate.lumaimage != lastent->aliasstate.lumaimage) breakbatch = true;
			if (ent->aliasstate.teximage != lastent->aliasstate.teximage) breakbatch = true;
		}

		if (numinstances >= MAX_ALIAS_INSTANCES) breakbatch = true;

		// players must be drawn individually for colormapping
		if (ent->model->flags & EF_PLAYER) breakbatch = true;

		if (breakbatch)
		{
			// draw everything accumulated so far
			D3DAlias_DrawInstances (ents, numents, firstent, numinstances);

			breakbatch = false;
			numinstances = 0;
			firstent = i;
		}

		numinstances++;
		lastent = ent;
	}

	// draw anything left over
	if (numinstances) D3DAlias_DrawInstances (ents, numents, firstent, numinstances);

	// stream 3 is never used outside of MDLs and needs nulling here in case the previous batch was instanced
	D3D_SetStreamSource (3, NULL, 0, 0);
}


void D3DAlias_RenderAliasModels (void)
{
	// if (NumOccluded) Con_Printf ("occluded %i\n", NumOccluded);
	if (!d3d_NumAliasEdicts) return;

	// sort the alias edicts by model and poses
	// (to do - chain these in a list instead to save memory, remove limits and run faster...)
	qsort (d3d_AliasEdicts, d3d_NumAliasEdicts, sizeof (entity_t *), (sortfunc_t) D3DAlias_ModelSortFunc);

	// draw in two passes to prevent excessive shader switching
	if (d3d_GlobalCaps.supportInstancing && d3d_usinginstancing.value)
		D3DAlias_DrawInstanced (d3d_AliasEdicts, d3d_NumAliasEdicts);
	else D3DAlias_DrawAliasBatch (d3d_AliasEdicts, d3d_NumAliasEdicts);

	// don't bother instancing shadows for now; we might later on if it ever becomes a problem
	D3DAlias_DrawAliasShadows (d3d_AliasEdicts, d3d_NumAliasEdicts);

	// clear for next frame
	d3d_NumAliasEdicts = 0;
}


void D3DAlias_AddModelToList (entity_t *ent)
{
	D3DAlias_SetupAliasModel (ent);

	if (ent->visframe != d3d_RenderDef.framecount) return;

	if (ent->alphaval > 0 && ent->alphaval < 255)
		D3DAlpha_AddToList (ent);
	else d3d_AliasEdicts[d3d_NumAliasEdicts++] = ent;
}


void D3DAlias_InitList (void)
{
	if (!d3d_AliasEdicts) d3d_AliasEdicts = (entity_t **) Zone_Alloc (sizeof (entity_t *) * MAX_VISEDICTS);
	d3d_NumAliasEdicts = 0;
}


void D3D_SetupProjection (float fovx, float fovy, float zn, float zf);
float SCR_CalcFovX (float fov_y, float width, float height);
float SCR_CalcFovY (float fov_x, float width, float height);
void SCR_SetFOV (float *fovx, float *fovy, float fovvar, int width, int height, bool guncalc);
void D3DIQM_DrawIQM (entity_t *ent);
void D3DIQM_SetCommonState (void);

// fixme - shouldn't this be the first thing drawn in the frame so that a lot of geometry can get early-z???
void D3DAlias_DrawViewModel (void)
{
	// conditions for switching off view model
	if (chase_active.value) return;
	if (cl.stats[STAT_HEALTH] <= 0) return;
	if (!cl.viewent.model) return;
	if (!r_drawentities.value) return;
	if (r_drawviewmodel.value <= 0) return;

	// select view ent
	entity_t *ent = &cl.viewent;
	aliashdr_t *hdr = ent->model->aliashdr;

	// the viewmodel should always be an alias model
	if (ent->model->type != mod_alias) return;

	// never check for bbox culling on the viewmodel
	ent->nocullbox = true;

	// the viewmodel is always fully transformed
	ent->rotated = true;

	if ((cl.items & IT_INVISIBILITY) || r_drawviewmodel.value < 1.0f)
	{
		// initial alpha
		ent->alphaval = (int) (r_drawviewmodel.value * 255.0f);

		// adjust for invisibility
		if (cl.items & IT_INVISIBILITY) ent->alphaval >>= 1;

		// final range; if the ent is fully transparent then don't bother drawing it
		if ((ent->alphaval = BYTE_CLAMP (ent->alphaval)) < 1) return;

		// enable blending
		D3DState_SetAlphaBlend (TRUE);
	}

	// recalculate the FOV here because the gun model might need different handling
	extern cvar_t scr_fov;

	float fov_x = 0;
	float fov_y = 0;

	if (scr_fov.value > 90)
		SCR_SetFOV (&fov_x, &fov_y, 90.0f, vid.ref3dsize.width, vid.ref3dsize.height, true);
	else SCR_SetFOV (&fov_x, &fov_y, scr_fov.value, vid.ref3dsize.width, vid.ref3dsize.height, true);

	// only update if it actually changes...!
	if (fov_x != r_refdef.fov_x || fov_y != r_refdef.fov_y)
	{
		// adjust projection to a constant y fov for wide-angle views
		// don't need to worry over much about the far clipping plane here
		D3D_SetupProjection (fov_x, fov_y, 4, 4096);

		// calculate concatenated final matrix for use by shaders
		// because it's only needed once per frame instead of once per vertex we can save some vs instructions
		QMATRIX::UpdateMVP (&d3d_ModelViewProjMatrix, &d3d_WorldMatrix, &d3d_ViewMatrix, &d3d_ProjMatrix);

		// we don't need to extract the frustum as the gun is never frustum-culled
		D3DHLSL_SetWorldMatrix (&d3d_ModelViewProjMatrix);
	}

	if (ent->model->type == mod_iqm)
	{
		D3DIQM_SetCommonState ();
		D3DAlias_LerpToFrame (ent, ent->frame, 0.1f);
		D3DIQM_DrawIQM (ent);
	}
	else
	{
		// setup the frame for drawing and store the interpolation blend
		D3DAlias_SetupFrame (ent, hdr);

		// add it to the list and draw it (there will only ever be one of these do don't even bother with instancing)
		D3DAlias_SetupAliasModel (ent);

		// draw directly as we may wish to change some stuff here...
		D3D_SetTextureMipmap (0, d3d_TexFilter, d3d_MipFilter);
		D3D_SetTextureMipmap (1, d3d_TexFilter, d3d_MipFilter);

		D3D_SetTextureAddress (0, D3DTADDRESS_CLAMP);
		D3D_SetTextureAddress (1, D3DTADDRESS_CLAMP);

		D3D_SetVertexDeclaration (d3d_AliasDecl);

		// assume that the textures will always need to change
		if (ent->aliasstate.lumaimage && gl_fullbrights.integer)
		{
			D3DHLSL_SetPass (FX_PASS_ALIAS_VIEWMODEL_LUMA);
			D3DHLSL_SetTexture (0, ent->aliasstate.teximage);
			D3DHLSL_SetTexture (1, ent->aliasstate.lumaimage);
		}
		else
		{
			D3DHLSL_SetPass (FX_PASS_ALIAS_VIEWMODEL_NOLUMA);
			D3DHLSL_SetTexture (0, ent->aliasstate.teximage);
		}

		// and now we can send it through the standard draw func
		D3DAlias_DrawModel (ent, hdr, AM_VIEWMODEL);
	}

	// restore alpha
	ent->alphaval = 0;

	// restoring the original projection is unnecessary as the gun is the last thing drawn in the 3D view
	if ((cl.items & IT_INVISIBILITY) || r_drawviewmodel.value < 0.99f)
	{
		D3DState_SetAlphaBlend (FALSE);
	}
}


