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

// up to 16 color translated skins
image_t d3d_PlayerSkins[256];

void D3D_RotateForEntity (entity_t *e);
void D3D_RotateForEntity (entity_t *e, D3DMATRIX *m);
void D3DLight_LightPoint (entity_t *e, float *c);

cvar_t r_aliaslightscale ("r_aliaslightscale", "1", CVAR_ARCHIVE);
cvar_t r_optimizealiasmodels ("r_optimizealiasmodels", "1", CVAR_ARCHIVE | CVAR_RESTART);
cvar_t r_cachealiasmodels ("r_cachealiasmodels", "0", CVAR_ARCHIVE | CVAR_RESTART);

/*
=================================================================

ALIAS MODEL DISPLAY LIST GENERATION

=================================================================
*/

#define AM_NOSHADOW			1
#define AM_FULLBRIGHT		2
#define AM_EYES				4
#define AM_DRAWSHADOW		8
#define AM_VIEWMODEL		16


typedef struct aliasbuffers_s
{
	LPDIRECT3DVERTEXBUFFER9 *VertexStreams;
	int *StreamOffsets;
	int NumVertexStreams;

	// this one is optional and is only used for the view model
	// it will be created on demand when you switch to such a model
	LPDIRECT3DVERTEXBUFFER9 BlendStream;

	LPDIRECT3DVERTEXBUFFER9 TexCoordStream;
	LPDIRECT3DINDEXBUFFER9 Indexes;

	int RegistrationSequence;
	int BufferSize;

	aliashdr_t *aliashdr;
} aliasbuffers_t;


bool d3d_AliasRegenBuffers = false;


void D3DAlias_ClearCache (void)
{
	// if the cache needs to be flushed we need special handling of subsequent buffer clearing and creation
	// this is needed because a buffer struct now holds a reference to the MDL that it was created for, and
	// that reference will now be an invalid pointer.  yes, this is ugly.
	d3d_AliasRegenBuffers = true;
}


void D3D_CacheAliasMesh (char *name, byte *hash, aliashdr_t *hdr)
{
	char meshname[MAX_PATH] = {0};

	Sys_mkdir ("mesh");
	COM_StripExtension (name, meshname);
	FILE *f = fopen (va ("%s/mesh/%s.ms3", com_gamedir, &meshname[6]), "wb");

	if (f)
	{
		// write out a checksum for the model to ensure that the saved .ms3 is valid
		fwrite (hash, 16, 1, f);

		// write out the sizes
		fwrite (&hdr->numindexes, sizeof (int), 1, f);
		fwrite (&hdr->nummesh, sizeof (int), 1, f);
		fwrite (&r_optimizealiasmodels.integer, sizeof (int), 1, f);

		// write out the data
		fwrite (hdr->indexes, sizeof (unsigned short), hdr->numindexes, f);
		fwrite (hdr->meshverts, sizeof (aliasmesh_t), hdr->nummesh, f);

		fclose (f);
	}
}


/*
================
D3DAlias_MakeAliasMesh

================
*/
void D3DAlias_MakeAliasMesh (char *name, byte *hash, aliashdr_t *hdr, stvert_t *stverts, dtriangle_t *triangles)
{
	if (r_cachealiasmodels.integer)
	{
		// look for a cached version
		HANDLE h = INVALID_HANDLE_VALUE;
		char meshname[MAX_PATH] = {0};
		byte ms3hash[16];
		int optimized = 0;

		COM_StripExtension (name, meshname);
		COM_FOpenFile (va ("mesh/%s.ms3", &meshname[6]), &h);

		if (h == INVALID_HANDLE_VALUE) goto bad_mesh_1;

		// read and validate all of the data
		if (COM_FReadFile (h, ms3hash, 16) != 16) goto bad_mesh_2;
		if (!COM_CheckHash (ms3hash, hash)) goto bad_mesh_2;
		if (COM_FReadFile (h, &hdr->numindexes, sizeof (int)) != sizeof (int)) goto bad_mesh_2;
		if (COM_FReadFile (h, &hdr->nummesh, sizeof (int)) != sizeof (int)) goto bad_mesh_2;
		if (COM_FReadFile (h, &optimized, sizeof (int)) != sizeof (int)) goto bad_mesh_2;

		// if the cached version is different from the current optimization level then recache it too
		// (exception: if the cached version is optimized we prefer to use it)
		if (optimized != r_optimizealiasmodels.integer && !optimized) goto bad_mesh_2;

		// the scratchbuf is big enough for 65536 verts so set up temp storage for reading to
		unsigned short *cacheindexes = (unsigned short *) scratchbuf;
		aliasmesh_t *cachemesh = (aliasmesh_t *) (cacheindexes + hdr->numindexes);

		// these will be cached even if invalid so they need to stay in temp storage until caching is complete
		// (although by this stage it's unlikely, but still...)
		if (COM_FReadFile (h, cacheindexes, sizeof (unsigned short) * hdr->numindexes) != sizeof (unsigned short) * hdr->numindexes) goto bad_mesh_2;
		if (COM_FReadFile (h, cachemesh, sizeof (aliasmesh_t) * hdr->nummesh) != sizeof (aliasmesh_t) * hdr->nummesh) goto bad_mesh_2;

		// cached OK
		COM_FCloseFile (&h);

		// and cache the mesh for real
		hdr->indexes = (unsigned short *) MainCache->Alloc (cacheindexes, hdr->numindexes * sizeof (unsigned short));
		hdr->meshverts = (aliasmesh_t *) MainCache->Alloc (cachemesh, hdr->nummesh * sizeof (aliasmesh_t));

		// we probably need to create buffers for this model
		hdr->buffernum = -1;

		return;

bad_mesh_2:;
		// we get here if the mesh file was successfully opened but was not valid for this MDL or had a read error
		COM_FCloseFile (&h);
bad_mesh_1:;
		// we get here if the mesh file could not be opened
		Con_Printf ("meshing %s...\n", name);
	}

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

	if (r_optimizealiasmodels.integer)
	{
		// optimize the mesh - we can pull temp storage off the hunk here because we're gonna free it anyway
		// most of the following code was inspired by the example at http://psp.jim.sh/svn/comp.php?repname=ps3ware&compare[]=/@136&compare[]=/@137
		// i reworked it to support my data types and naming and removed all of the STL gross-out crap.
		// this was the *only* *practical* example of these functions usage i could find, everything else just says
		// "use them" but doesn't actually say *how* (indicating that a lot of folks don't really know what they're talking about...)
		int numtris = hdr->numindexes / 3;
		unsigned short *newindexes = (unsigned short *) MainHunk->Alloc (hdr->numindexes * sizeof (unsigned short), FALSE);
		DWORD *optresult = (DWORD *) MainHunk->Alloc ((hdr->nummesh > numtris ? hdr->nummesh : numtris) * sizeof (DWORD), FALSE);
		aliasmesh_t *newvertexes = (aliasmesh_t *) MainHunk->Alloc (hdr->nummesh * sizeof (aliasmesh_t), FALSE);
		DWORD *remap = (DWORD *) MainHunk->Alloc (hdr->nummesh * sizeof (DWORD), FALSE);

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

	// (optionally) save the data out to disk
	if (r_cachealiasmodels.integer)
		D3D_CacheAliasMesh (name, hash, hdr);

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
			hdr->drawflags |= (AM_NOSHADOW | AM_EYES);
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

	// explicitly no buffer the first time it's cached
	hdr->buffernum = -1;
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
	DWORD normal;
} aliasvertexstream_t;

typedef struct aliastexcoordstream_s
{
	float st[2];
} aliastexcoordstream_t;


aliasbuffers_t d3d_AliasBuffers[MAX_MOD_KNOWN];

LPDIRECT3DVERTEXDECLARATION9 d3d_AliasDecl = NULL;
LPDIRECT3DVERTEXDECLARATION9 d3d_ViewModelDecl = NULL;
LPDIRECT3DVERTEXDECLARATION9 d3d_ShadowDecl = NULL;
LPDIRECT3DVERTEXDECLARATION9 d3d_InstanceDecl = NULL;

#define MAX_ALIAS_INSTANCES		200

typedef struct aliasinstance_s
{
	D3DMATRIX matrix;
	float lerps[4];
	float svec[3];
	float rgba[4];
} aliasinstance_t;


// here we rotate the instance buffer which may be a performance optimization on some hardware
// this only comes out as ~168k so memory-saving weenies needn't have a fit over it.
#define MAX_INSTANCE_BUFFERS	8
LPDIRECT3DVERTEXBUFFER9 d3d_AliasInstances[MAX_INSTANCE_BUFFERS] = {NULL};
int d3d_InstBuffer = 0;

void D3DAlias_CreateVertexStreamBuffer (aliashdr_t *hdr, aliasbuffers_t *buf)
{
	if (d3d_GlobalCaps.supportStreamOffset)
	{
		buf->VertexStreams = (LPDIRECT3DVERTEXBUFFER9 *) MainZone->Alloc (sizeof (LPDIRECT3DVERTEXBUFFER9));
		buf->StreamOffsets = (int *) MainZone->Alloc (hdr->nummeshframes * sizeof (int));
		buf->NumVertexStreams = 1;

		aliasvertexstream_t *xyz = NULL;
		int ofs = 0;

		D3DMain_CreateVertexBuffer (hdr->nummesh * hdr->nummeshframes * sizeof (aliasvertexstream_t), D3DUSAGE_WRITEONLY, &buf->VertexStreams[0]);
		hr = buf->VertexStreams[0]->Lock (0, 0, (void **) &xyz, 0);
		if (FAILED (hr)) Sys_Error ("D3DAlias_CreateVertexStreamBuffer : failed to lock vertex buffer");

		for (int i = 0; i < hdr->nummeshframes; i++)
		{
			aliasmesh_t *src = hdr->meshverts;
			drawvertx_t *verts = hdr->vertexes[i];

			for (int v = 0; v < hdr->nummesh; v++, src++, xyz++)
			{
				// positions and normals are sent as bytes to save on bandwidth and storage
				xyz->xyz = verts[src->vertindex].xyz;
				xyz->normal = verts[src->vertindex].normal1dw;
			}

			buf->StreamOffsets[i] = ofs;
			ofs += hdr->nummesh * sizeof (aliasvertexstream_t);
		}

		hr = buf->VertexStreams[0]->Unlock ();
		if (FAILED (hr)) Sys_Error ("D3DAlias_CreateVertexStreamBuffer : failed to unlock vertex buffer");
	}
	else
	{
		// multiple VBOs case
		buf->VertexStreams = (LPDIRECT3DVERTEXBUFFER9 *) MainZone->Alloc (hdr->nummeshframes * sizeof (LPDIRECT3DVERTEXBUFFER9));
		buf->NumVertexStreams = hdr->nummeshframes;

		// we can't rely on stream offset being universally available so instead we need to create lots of
		// small vertex buffers - YUCK!  probably need to add a cap for when stream offset *is* available.
		// in general however if offset is *not* available we're in software T&L land...
		for (int i = 0; i < buf->NumVertexStreams; i++)
		{
			D3DMain_CreateVertexBuffer (hdr->nummesh * sizeof (aliasvertexstream_t),
				D3DUSAGE_WRITEONLY, &buf->VertexStreams[i]);

			aliasvertexstream_t *xyz = NULL;
			aliasmesh_t *src = hdr->meshverts;
			drawvertx_t *verts = hdr->vertexes[i];

			hr = buf->VertexStreams[i]->Lock (0, 0, (void **) &xyz, 0);
			if (FAILED (hr)) Sys_Error ("D3DAlias_CreateVertexStreamBuffer : failed to lock vertex buffer");

			for (int v = 0; v < hdr->nummesh; v++, src++, xyz++)
			{
				// positions and normals are sent as bytes to save on bandwidth and storage
				xyz->xyz = verts[src->vertindex].xyz;
				xyz->normal = verts[src->vertindex].normal1dw;
			}

			hr = buf->VertexStreams[i]->Unlock ();
			if (FAILED (hr)) Sys_Error ("D3DAlias_CreateVertexStreamBuffer : failed to unlock vertex buffer");
		}
	}
}


void D3DAlias_CreateTexCoordStreamBuffer (aliashdr_t *hdr, aliasbuffers_t *buf)
{
	D3DMain_CreateVertexBuffer (hdr->nummesh * sizeof (aliastexcoordstream_t), D3DUSAGE_WRITEONLY, &buf->TexCoordStream);

	aliastexcoordstream_t *st = NULL;
	aliasmesh_t *src = hdr->meshverts;

	hr = buf->TexCoordStream->Lock (0, 0, (void **) &st, 0);
	if (FAILED (hr)) Sys_Error ("D3DAlias_CreateTexCoordStreamBuffer: failed to lock vertex buffer");

	for (int i = 0; i < hdr->nummesh; i++, src++, st++)
	{
		// convert back to floating point and store out
		st->st[0] = (float) src->st[0] / (float) hdr->skinwidth;
		st->st[1] = (float) src->st[1] / (float) hdr->skinheight;
	}

	hr = buf->TexCoordStream->Unlock ();
	if (FAILED (hr)) Sys_Error ("D3DAlias_CreateTexCoordStreamBuffer: failed to unlock vertex buffer");
}


void D3DAlias_CreateIndexBuffer (aliashdr_t *hdr, aliasbuffers_t *buf)
{
	D3DMain_CreateIndexBuffer (hdr->numindexes, D3DUSAGE_WRITEONLY, &buf->Indexes);

	// now we fill in the index buffer; this is a non-dynamic index buffer and it only needs to be set once
	unsigned short *ndx = NULL;

	hr = buf->Indexes->Lock (0, 0, (void **) &ndx, 0);
	if (FAILED (hr)) Sys_Error ("D3DAlias_CreateIndexBuffer: failed to lock index buffer");

	memcpy (ndx, hdr->indexes, hdr->numindexes * sizeof (unsigned short));

	hr = buf->Indexes->Unlock ();
	if (FAILED (hr)) Sys_Error ("D3DAlias_CreateIndexBuffer: failed to unlock index buffer");
}


void D3DAlias_ReleaseBuffers (aliasbuffers_t *buf)
{
	if (buf->VertexStreams)
	{
		for (int i = 0; i < buf->NumVertexStreams; i++)
			SAFE_RELEASE (buf->VertexStreams[i]);

		MainZone->Free (buf->VertexStreams);
		buf->VertexStreams = NULL;
	}

	if (buf->StreamOffsets)
	{
		MainZone->Free (buf->StreamOffsets);
		buf->StreamOffsets = NULL;
	}

	buf->NumVertexStreams = 0;

	SAFE_RELEASE (buf->BlendStream);
	SAFE_RELEASE (buf->TexCoordStream);
	SAFE_RELEASE (buf->Indexes);

	// explicitly unregistered
	buf->RegistrationSequence = ~d3d_RenderDef.RegistrationSequence;

	// NULL the header if we're regenerating buffers (the creation code will look after everything else we need here)
	if (d3d_AliasRegenBuffers) buf->aliashdr = NULL;

	if (buf->aliashdr)
	{
		buf->aliashdr->buffernum = -1;
		buf->aliashdr = NULL;
	}
}


void D3DAlias_ReleaseBuffers (int buffernum)
{
	D3DAlias_ReleaseBuffers (&d3d_AliasBuffers[buffernum]);
}


void D3DAlias_CreateBuffers (aliashdr_t *hdr)
{
	// so that we can check for failure
	hdr->buffernum = -1;

	// the model needs a new buffer so find a free one
	for (int j = 0; j < MAX_MOD_KNOWN; j++)
	{
		if (!d3d_AliasBuffers[j].Indexes)
		{
			hdr->buffernum = j;
			break;
		}
	}

	// this should never happen as MAX_MOD_KNOWN should hopefully crap out first
	if (hdr->buffernum < 0) Sys_Error ("D3DAlias_CreateBuffers : failed to find free buffers");

	// this one is just to keep the memory weenies from having fits
	d3d_AliasBuffers[hdr->buffernum].BufferSize = hdr->nummesh * hdr->nummeshframes * sizeof (aliasvertexstream_t) +
		hdr->nummesh * sizeof (aliastexcoordstream_t) +
		hdr->numindexes * sizeof (unsigned short);

	D3DAlias_CreateIndexBuffer (hdr, &d3d_AliasBuffers[hdr->buffernum]);
	D3DAlias_CreateVertexStreamBuffer (hdr, &d3d_AliasBuffers[hdr->buffernum]);
	D3DAlias_CreateTexCoordStreamBuffer (hdr, &d3d_AliasBuffers[hdr->buffernum]);

	// store the alias header that was used with this buffer so that we can clear it properly
	// (fixme - this needs to be fixed if we;re clearing the cache)
	d3d_AliasBuffers[hdr->buffernum].aliashdr = hdr;
}


void D3DAlias_CreateBuffers (void)
{
	model_t *mod = NULL;
	int createbuffers = 0;
	int clearbuffers = 0;
	int activebuffers = 0;
	int BufferSize = 0;

	// nothing allocated yet
	if (!mod_known) return;

	// go through all the models
	for (int i = 0; i < MAX_MOD_KNOWN; i++)
	{
		if (!(mod = mod_known[i])) continue;
		if (mod->type != mod_alias) continue;

		aliashdr_t *hdr = mod->aliashdr;

		// a cache flush needs to regenerate buffers for all MDLs as the buffer now holds
		// a reference to the MDL header
		if (mod->aliashdr->buffernum < 0 || d3d_AliasRegenBuffers)
		{
			D3DAlias_CreateBuffers (mod->aliashdr);
			createbuffers++;
		}

		// track total allocations
		BufferSize += d3d_AliasBuffers[hdr->buffernum].BufferSize;

		// mark the buffer as used in this registration sequence
		d3d_AliasBuffers[hdr->buffernum].RegistrationSequence = d3d_RenderDef.RegistrationSequence;
		activebuffers++;
	}

	// clear down any buffers which are unused in this registration sequence
	for (int i = 0; i < MAX_MOD_KNOWN; i++)
	{
		// this buffer is used
		if (d3d_AliasBuffers[i].RegistrationSequence == d3d_RenderDef.RegistrationSequence) continue;

		// all models must have indexes so if it has indexes count it to the cleared total
		if (d3d_AliasBuffers[i].Indexes)
		{
			clearbuffers++;
			D3DAlias_ReleaseBuffers (&d3d_AliasBuffers[i]);
		}
	}

	// no need to force a regeneration any more
	d3d_AliasRegenBuffers = false;

	// Con_Printf ("Created %i buffers\n", createbuffers);
	// Con_Printf ("Released %i buffers\n", clearbuffers);
	// Con_Printf ("Using %i buffers\n", activebuffers);

	// this just exists so that I can track how much memory I'm using for MDLs and keep the memory weenies from panicking
	// Con_Printf ("%i k in MDL vertex buffers\n", (BufferSize + 1023) / 1024);

	// create everything else we need
	if (!d3d_AliasDecl)
	{
		D3DVERTEXELEMENT9 d3d_aliaslayout[] =
		{
			// positions are sent as bytes to save on bandwidth and storage
			// position 0 can't be d3dcolor as it fucks with fog (why???)
			{0, 0, D3DDECLTYPE_UBYTE4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
			{0, 4, D3DDECLTYPE_UBYTE4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},	// this is really a normal
			{1, 0, D3DDECLTYPE_UBYTE4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 1},
			{1, 4, D3DDECLTYPE_UBYTE4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1},	// and so is this
			{2, 0, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 2},
			D3DDECL_END ()
		};

		hr = d3d_Device->CreateVertexDeclaration (d3d_aliaslayout, &d3d_AliasDecl);
		if (FAILED (hr)) Sys_Error ("D3DAlias_CreateBuffers: d3d_Device->CreateVertexDeclaration failed");
	}

	if (!d3d_ViewModelDecl)
	{
		D3DVERTEXELEMENT9 d3d_viewmodellayout[] =
		{
			// positions are sent as bytes to save on bandwidth and storage
			// position 0 can't be d3dcolor as it fucks with fog (why???)
			{0, 0, D3DDECLTYPE_UBYTE4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
			{0, 4, D3DDECLTYPE_UBYTE4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},	// this is really a normal
			{1, 0, D3DDECLTYPE_UBYTE4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 1},
			{1, 4, D3DDECLTYPE_UBYTE4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1},	// and so is this
			{2, 0, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 2},
			{3, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 3},	// lerp factors for the view model
			D3DDECL_END ()
		};

		hr = d3d_Device->CreateVertexDeclaration (d3d_viewmodellayout, &d3d_ViewModelDecl);
		if (FAILED (hr)) Sys_Error ("D3DAlias_CreateBuffers: d3d_Device->CreateVertexDeclaration failed");
	}

	if (d3d_GlobalCaps.supportInstancing)
	{
		// here we rotate the instance buffer which may be a performance optimization on some hardware
		// this only comes out as ~168k so memory-saving weenies needn't have a fit over it.
		for (int i = 0; i < MAX_INSTANCE_BUFFERS; i++)
		{
			if (!d3d_AliasInstances[i])
			{
				D3DMain_CreateVertexBuffer (MAX_ALIAS_INSTANCES * sizeof (aliasinstance_t), D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, &d3d_AliasInstances[i]);
				D3D_PrelockVertexBuffer (d3d_AliasInstances[i]);
			}
		}
	}

	if (!d3d_InstanceDecl && d3d_GlobalCaps.supportInstancing)
	{
		D3DVERTEXELEMENT9 d3d_instancedlayout[] =
		{
			// positions are sent as bytes to save on bandwidth and storage
			// position 0 can't be d3dcolor as it fucks with fog (why???)
			{0,  0, D3DDECLTYPE_UBYTE4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
			{0,  4, D3DDECLTYPE_UBYTE4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},	// this is really a normal
			{1,  0, D3DDECLTYPE_UBYTE4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 1},
			{1,  4, D3DDECLTYPE_UBYTE4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1},	// and so is this
			{2,  0, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 2},
			{3,  0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 3},	// matrix row 1
			{3, 16, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 4},	// matrix row 2
			{3, 32, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 5},	// matrix row 3
			{3, 48, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 6},	// matrix row 4
			{3, 64, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 7},	// blend[LERP_CURR] and blend[LERP_LAST] combined
			{3, 80, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 8},	// shadevector
			{3, 92, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 9},	// shadelight and alpha combined
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
			{0, 0, D3DDECLTYPE_UBYTE4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
			{0, 4, D3DDECLTYPE_UBYTE4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},	// this is really a normal
			{1, 0, D3DDECLTYPE_UBYTE4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 1},
			{1, 4, D3DDECLTYPE_UBYTE4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1},	// and so is this
			D3DDECL_END ()
		};

		hr = d3d_Device->CreateVertexDeclaration (d3d_shadowlayout, &d3d_ShadowDecl);
		if (FAILED (hr)) Sys_Error ("D3DAlias_CreateBuffers: d3d_Device->CreateVertexDeclaration failed");
	}
}


void D3DAlias_ReleaseBuffers (void)
{
	// if we're releasing here on a vid_restart, game change or lost device we must fully regenerate
	d3d_AliasRegenBuffers = true;

	for (int i = 0; i < MAX_MOD_KNOWN; i++)
		D3DAlias_ReleaseBuffers (&d3d_AliasBuffers[i]);

	for (int i = 0; i < MAX_INSTANCE_BUFFERS; i++)
		SAFE_RELEASE (d3d_AliasInstances[i]);

	SAFE_RELEASE (d3d_ViewModelDecl);
	SAFE_RELEASE (d3d_InstanceDecl);
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


cvar_t cl_itembobheight ("cl_itembobheight", 0.0f);
cvar_t cl_itembobspeed ("cl_itembobspeed", 0.5f);

void D3DAlias_SetupLighting (entity_t *ent, float *shadevector, float *shadelight)
{
	// set up other stuff for lighting
	float lightvec[3] = {-1, 0, 0};
	avectors_t av;

	ent->angles[0] = -ent->angles[0];	// stupid quake bug
	AngleVectors (ent->angles, &av);
	ent->angles[0] = -ent->angles[0];	// stupid quake bug

	// rotate the lighting vector into the model's frame of reference
	shadevector[0] = DotProduct (lightvec, av.forward);
	shadevector[1] = -DotProduct (lightvec, av.right);
	shadevector[2] = DotProduct (lightvec, av.up);

	// copy these out because we need to keep the originals in the entity for frame averaging
	VectorCopy2 (shadelight, ent->shadelight);

	// nehahra assumes that fullbrights are not available in the engine
	if ((nehahra || !gl_fullbrights.integer) && (ent->model->aliashdr->drawflags & AM_FULLBRIGHT))
	{
		shadelight[0] = (256 >> r_overbright.integer);
		shadelight[1] = (256 >> r_overbright.integer);
		shadelight[2] = (256 >> r_overbright.integer);
	}

	VectorScale (shadelight, (r_aliaslightscale.value / 255.0f), shadelight);
}


void D3DAlias_TransformFinal (entity_t *ent, aliashdr_t *hdr)
{
	if ((hdr->drawflags & AM_EYES) && gl_doubleeyes.integer)
	{
		// the scaling needs to be included at this time
		D3DMatrix_Translate (&ent->matrix, hdr->scale_origin[0] - (22 + 8), hdr->scale_origin[1] - (22 + 8), hdr->scale_origin[2] - (22 + 8));
		D3DMatrix_Scale (&ent->matrix, hdr->scale[0] * 2.0f, hdr->scale[1] * 2.0f, hdr->scale[2] * 2.0f);
	}
	else
	{
		// the scaling needs to be included at this time
		D3DMatrix_Translate (&ent->matrix, hdr->scale_origin);
		D3DMatrix_Scale (&ent->matrix, hdr->scale);
	}
}


void D3DAlias_TransformStandard (entity_t *ent, aliashdr_t *hdr)
{
	// initialize entity matrix to identity
	D3DMatrix_Identity (&ent->matrix);

	// build the transformation for this entity.  the full model gets a full rotation
	// allow bouncing items for those who like them
	if (cl_itembobheight.value > 0.0f && (ent->model->flags & EF_ROTATE))
	{
		// store out the original origin so that we can put it back the way it was for shadows
		float org2 = ent->origin[2];

		// come back to my place, bouncy bouncy!
		ent->origin[2] += (cos ((cl.time + ent->entnum) * cl_itembobspeed.value * (2.0f * D3DX_PI)) + 1.0f) * 0.5f * cl_itembobheight.value;
		D3D_RotateForEntity (ent, &ent->matrix);

		// restore the origin for any later use
		ent->origin[2] = org2;
	}
	else D3D_RotateForEntity (ent, &ent->matrix);

	// run the final transform for scaling
	D3DAlias_TransformFinal (ent, hdr);
}


void D3DAlias_TransformShadowed (entity_t *ent, aliashdr_t *hdr)
{
	// initialize entity matrix to identity
	D3DMatrix_Identity (&ent->matrix);

	// build the transformation for this entity.  shadows only rotate around Z
	D3DMatrix_Translate (&ent->matrix, ent->origin);
	D3DMatrix_Rotate (&ent->matrix, 0, 0, 1, ent->angles[1]);

	// run the final transform for scaling
	D3DAlias_TransformFinal (ent, hdr);
}


void D3DAlias_DrawModel (entity_t *ent, aliashdr_t *hdr, int flags = 0)
{
	// this is used for shadows as well as regular drawing otherwise shadows might not cache normals
	aliasstate_t *state = &ent->aliasstate;
	aliasbuffers_t *buf = &d3d_AliasBuffers[hdr->buffernum];
	LPDIRECT3DVERTEXBUFFER9 Streams[2];
	int Offsets[2];

	// vertex streams
	// streams 0, 1 and indexes are common whether or not we're doing shadows
	if (d3d_GlobalCaps.supportStreamOffset)
	{
		Streams[LERP_CURR] = Streams[LERP_LAST] = buf->VertexStreams[0];
		Offsets[LERP_CURR] = buf->StreamOffsets[ent->lerppose[LERP_CURR]];
		Offsets[LERP_LAST] = buf->StreamOffsets[ent->lerppose[LERP_LAST]];
	}
	else
	{
		Streams[LERP_CURR] = buf->VertexStreams[ent->lerppose[LERP_CURR]];
		Streams[LERP_LAST] = buf->VertexStreams[ent->lerppose[LERP_LAST]];
		Offsets[LERP_CURR] = Offsets[LERP_LAST] = 0;
	}

	D3D_SetStreamSource (0, Streams[LERP_CURR], Offsets[LERP_CURR], sizeof (aliasvertexstream_t));
	D3D_SetStreamSource (1, Streams[LERP_LAST], Offsets[LERP_LAST], sizeof (aliasvertexstream_t));
	D3D_SetStreamSource (2, buf->TexCoordStream, 0, sizeof (aliastexcoordstream_t));

	D3D_SetIndices (buf->Indexes);

	// initialize entity matrix to identity; what happens with it next depends on whether it is a shadow or not
	D3DMatrix_Identity (&ent->matrix);

	if (flags & AM_DRAWSHADOW)
	{
		// build the transforms for the entity
		D3DAlias_TransformShadowed (ent, hdr);

		// shadows only need the position stream for drawing
		D3D_SetStreamSource (2, NULL, 0, 0);

		// shadows just reuse the ShadeVector uniform for the lightspot as a convenience measure
		D3DHLSL_SetFloatArray ("ShadeVector", state->lightspot, 3);
	}
	else
	{
		// build the transforms for the entity
		D3DAlias_TransformStandard (ent, hdr);

		// the full model needs the texcoord stream too
		D3D_SetStreamSource (2, buf->TexCoordStream, 0, sizeof (aliastexcoordstream_t));

		D3DHLSL_SetAlpha ((float) ent->alphaval / 255.0f);

		float shadevector[3];
		float shadelight[3];

		D3DAlias_SetupLighting (ent, shadevector, shadelight);

		D3DHLSL_SetFloatArray ("ShadeVector", shadevector, 3);
		D3DHLSL_SetFloatArray ("ShadeLight", shadelight, 3);
	}

	if (flags & AM_VIEWMODEL)
	{
		// create the vertex buffer for the blend stream if it doesn't already exist
		// (this might be slow in timedemos but it's OK in regular gameplay -- i hope!!!
		if (!buf->BlendStream)
		{
			// create the buffer and force an immediate update the first time it's seen
			// (this might be momentarily slow but we have no 100% reliable way of identifying a view model aside from when the model is drawn)
			D3DMain_CreateVertexBuffer (hdr->nummesh * sizeof (float) * 4, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, &buf->BlendStream);
			hdr->lastblends[LERP_CURR] = hdr->lastblends[LERP_LAST] = 999999;
			hdr->mfdelerp = false;
		}

		if (!hdr->mfdelerp)
		{
			// set up additional info about the viewmodel the first time it's seen
			// again, the only reliable way to identify a viewmodel is when it's drawn, so it has to wait until we draw it before we can do this
			DelerpMuzzleFlashes (hdr);
			hdr->mfdelerp = true;
		}

		// if the blends change then we need to rebuild the blending buffer.
		// we could in theory look for an optimization by swapping the blends but in practice they're never swappable
		if (state->blend[LERP_CURR] != hdr->lastblends[LERP_CURR] || state->blend[LERP_LAST] != hdr->lastblends[LERP_LAST])
		{
			float *blends = NULL;
			aliasmesh_t *src = hdr->meshverts;
			drawvertx_t *verts = hdr->vertexes[ent->lerppose[LERP_CURR]];

			hr = buf->BlendStream->Lock (0, 0, (void **) &blends, D3DLOCK_DISCARD);
			if (FAILED (hr)) Sys_Error ("D3DAlias_DrawModel : failed to lock vertex buffer");

			for (int i = 0; i < hdr->nummesh; i++, src++, blends += 4)
			{
				if (verts[src->vertindex].lerpvert)
				{
					// use standard blends if this vertex should interpolate
					blends[0] = state->blend[LERP_CURR];
					blends[1] = state->blend[LERP_CURR] * 0.005f;
					blends[2] = state->blend[LERP_LAST];
					blends[3] = state->blend[LERP_LAST] * 0.005f;
				}
				else
				{
					// override the blend if this vertex should not interpolate
					blends[0] = 1.0f;
					blends[1] = 0.005f;
					blends[2] = 0.0f;
					blends[3] = 0.0f;
				}
			}

			hr = buf->BlendStream->Unlock ();
			if (FAILED (hr)) Sys_Error ("D3DAlias_DrawModel : failed to unlock vertex buffer");
			d3d_RenderDef.numlock++;

			// cache them back so that we only need to update if they change
			hdr->lastblends[LERP_CURR] = state->blend[LERP_CURR];
			hdr->lastblends[LERP_LAST] = state->blend[LERP_LAST];
		}

		// for the viewmodel the blendstream is separate because it can vary for muzzleflash delerping
		D3D_SetStreamSource (3, buf->BlendStream, 0, sizeof (float) * 4);
	}
	else
	{
		// stream 3 is never used outside of MDLs and needs nulling here in case the previous batch was instanced
		D3D_SetStreamSource (3, NULL, 0, 0);

		D3DHLSL_SetCurrLerp (state->blend[LERP_CURR]);
		D3DHLSL_SetLastLerp (state->blend[LERP_LAST]);
	}

	D3DHLSL_SetEntMatrix (&ent->matrix);

	D3D_DrawIndexedPrimitive (0, hdr->nummesh, 0, hdr->numindexes / 3);
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
		aliasstate_t *state = &ent->aliasstate;
		aliashdr_t *hdr = ent->model->aliashdr;

		// don't crash
		if (!state->lightplane) continue;

		// these entities don't have shadows
		if (hdr->drawflags & AM_NOSHADOW) continue;

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
		D3DAlias_DrawModel (ent, hdr, AM_DRAWSHADOW);
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

void D3DAlias_SetupAliasFrame (entity_t *ent, aliashdr_t *hdr)
{
	// blend is on a coarser scale (0-10 integer instead of 0-1 float) so that we get better cache hits
	// and can do better comparison/etc for cache checking; this still gives 10 x smoothing so it's OK
	int pose, numposes;
	float interval;
	bool lerpmdl = true;
	float lerpblend;
	float frame_interval;
	aliasstate_t *state = &ent->aliasstate;

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
		pose += (int) ((cl.time + ent->posebase) / interval) % numposes;

		// not really needed for non-interpolated mdls, but does no harm
		frame_interval = interval;
	}
	else if (ent->lerpflags & LERP_FINISH)
		frame_interval = ent->lerpinterval;
	else frame_interval = 0.1;

	// conditions for turning lerping off (the SNG bug is no longer an issue)
	if (hdr->nummeshframes == 1) lerpmdl = false;			// only one pose
	if (ent->lerppose[LERP_LAST] == ent->lerppose[LERP_CURR]) lerpmdl = false;		// both frames are identical
	if (!r_lerpframe.value) lerpmdl = false;

	// interpolation
	if (ent->lerppose[LERP_CURR] == -1 || ent->lerppose[LERP_LAST] == -1)
	{
		// new entity so reset to no interpolation initially
		ent->framestarttime = cl.time;
		ent->lerppose[LERP_LAST] = ent->lerppose[LERP_CURR] = pose;
		lerpblend = 0;
	}
	else if (ent->lerppose[LERP_LAST] == ent->lerppose[LERP_CURR] && ent->lerppose[LERP_CURR] == 0 && ent != &cl.viewent)
	{
		// "dying throes" interpolation bug - begin a new sequence with both poses the same
		// this happens when an entity is spawned client-side
		ent->framestarttime = cl.time;
		ent->lerppose[LERP_LAST] = ent->lerppose[LERP_CURR] = pose;
		lerpblend = 0;
	}
	else if (pose == 0 && ent == &cl.viewent)
	{
		// don't interpolate from previous pose on frame 0 of the viewent
		ent->framestarttime = cl.time;
		ent->lerppose[LERP_LAST] = ent->lerppose[LERP_CURR] = pose;
		lerpblend = 0;
	}
	else if (ent->lerppose[LERP_CURR] != pose || !lerpmdl)
	{
		// begin a new interpolation sequence
		ent->framestarttime = cl.time;
		ent->lerppose[LERP_LAST] = ent->lerppose[LERP_CURR];
		ent->lerppose[LERP_CURR] = pose;
		lerpblend = 0;
	}
	else
	{
		// standard interpolation between two frames which already exist
		// (should this use cl.time????)
		lerpblend = (cl.time - ent->framestarttime) / frame_interval;
	}

	// if a viewmodel is switched and the previous had a current frame number higher than the number of frames
	// in the new one, DirectQ will crash so we need to fix that.  this is also a general case sanity check.
	if (ent->lerppose[LERP_LAST] >= hdr->nummeshframes)
		ent->lerppose[LERP_LAST] = ent->lerppose[LERP_CURR] = 0;
	else if (ent->lerppose[LERP_LAST] < 0)
		ent->lerppose[LERP_LAST] = ent->lerppose[LERP_CURR] = hdr->nummeshframes - 1;

	// more fixing here
	if (ent->lerppose[LERP_CURR] >= hdr->nummeshframes)
		ent->lerppose[LERP_LAST] = ent->lerppose[LERP_CURR] = 0;
	else if (ent->lerppose[LERP_CURR] < 0)
		ent->lerppose[LERP_LAST] = ent->lerppose[LERP_CURR] = hdr->nummeshframes - 1;

	// don't let blend pass 1
	if (cl.paused) lerpblend = 0;
	if (lerpblend > 1) lerpblend = 1;

	// coarsen the scale a little for the viewmodel if we're in a flash frame to prevent excessive buffer updates
	// this still interpolates at 50 FPS (or 5 x the default) so it should be more than ample anyway
	if (ent == &cl.viewent && (ent->lerppose[LERP_CURR] > 0 || ent->lerppose[LERP_LAST] > 0))
	{
		lerpblend = (int) (lerpblend * 5.0f);
		lerpblend *= 0.2f;
	}

	// use cubic interpolation
	state->blend[LERP_LAST] = 1.0f - lerpblend;
	state->blend[LERP_CURR] = lerpblend;
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
		IMAGE_MIPMAP | IMAGE_NOEXTERN
	);

	// Con_Printf ("Translated skin to %i\n", ent->playerskin);
	// success
	return true;
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
		if (R_CullBox (ent->mins, ent->maxs)) return;
	}

	// the model has not been culled now
	ent->visframe = d3d_RenderDef.framecount;
	state->lightplane = NULL;

	if (ent == cl_entities[cl.viewentity] && chase_active.value)
	{
		// adjust angles (done here instead of chase_update as we only want the angle for rendering to be adjusted)
		ent->angles[0] *= 0.3;
	}

	// get lighting information
	vec3_t shadelight;
	D3DLight_LightPoint (ent, shadelight);

	// average light between frames
	// we can't rescale shadelight to the final range here because it will mess with the averaging so instead we do it in the shader
	for (int i = 0; i < 3; i++)
		ent->shadelight[i] = (shadelight[i] + ent->shadelight[i]) / 2;

	// store out for shadows
	VectorCopy (lightspot, state->lightspot);
	state->lightplane = lightplane;

	// get texturing info
	// software quake randomises the base animation and so should we
	int anim = (int) ((cl.time + ent->skinbase) * 10) & 3;

	// switch the entity to a skin texture at &d3d_PlayerSkins[ent->playerskin]
	// move all skin translation to here (only if translation succeeds)
	if (D3DAlias_TranslateAliasSkin (ent))
	{
		state->teximage = &d3d_PlayerSkins[ent->playerskin];
		state->lumaimage = hdr->skins[ent->skinnum].lumaimage[anim];
		d3d_PlayerSkins[ent->playerskin].LastUsage = 0;
	}
	else
	{
		state->teximage = hdr->skins[ent->skinnum].teximage[anim];
		state->lumaimage = hdr->skins[ent->skinnum].lumaimage[anim];
	}

	// build the sort order (which may be inverted for more optimal reuse)
	// keeping the lowest first ensures that it's more likely to be reused, which works out well for quake anims
	if (ent->lerppose[LERP_CURR] < ent->lerppose[LERP_LAST])
	{
		ent->sortpose[LERP_CURR] = ent->lerppose[LERP_LAST];
		ent->sortpose[LERP_LAST] = ent->lerppose[LERP_CURR];
	}
	else
	{
		ent->sortpose[LERP_CURR] = ent->lerppose[LERP_CURR];
		ent->sortpose[LERP_LAST] = ent->lerppose[LERP_LAST];
	}
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
		aliasstate_t *state = &ent->aliasstate;

		// prydon gets this
		if (!state->teximage) continue;
		if (!state->teximage->d3d_Texture) continue;

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
		if (ent->lerppose[LERP_CURR] < 0) ent->lerppose[LERP_CURR] = 0;
		if (ent->lerppose[LERP_LAST] < 0) ent->lerppose[LERP_LAST] = 0;

		// update textures if necessary
		if ((state->teximage != d3d_AliasState.lasttexture) || (state->lumaimage != d3d_AliasState.lastluma))
		{
			if (state->lumaimage)
			{
				if (gl_fullbrights.integer)
					D3DHLSL_SetPass (FX_PASS_ALIAS_LUMA);
				else D3DHLSL_SetPass (FX_PASS_ALIAS_LUMA_NOLUMA);

				D3DHLSL_SetTexture (0, state->teximage->d3d_Texture);
				D3DHLSL_SetTexture (1, state->lumaimage->d3d_Texture);
			}
			else
			{
				D3DHLSL_SetPass (FX_PASS_ALIAS_NOLUMA);
				D3DHLSL_SetTexture (0, state->teximage->d3d_Texture);
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
	// Con_Printf ("D3DAlias_DrawInstances : %3i %3i\n", firstent, numinstances);

	if (d3d_GlobalCaps.supportInstancing && numinstances > 1 && d3d_usinginstancing.value)
	{
		// here we rotate the instance buffer which may be a performance optimization on some hardware
		// this only comes out as ~168k so memory-saving weenies needn't have a fit over it.
		LPDIRECT3DVERTEXBUFFER9 d3d_CurrentInstBuffer = d3d_AliasInstances[(++d3d_InstBuffer) & (MAX_INSTANCE_BUFFERS - 1)];
		aliasinstance_t *instances = NULL;

		// set up the per-instance data
		hr = d3d_CurrentInstBuffer->Lock (0, 0, (void **) &instances, D3DLOCK_DISCARD);
		if (FAILED (hr)) Sys_Error ("D3DAlias_DrawInstances : failed to lock vertex buffer");

		for (int i = 0; i < numinstances; i++, instances++)
		{
			entity_t *ent = ents[firstent + i];

			D3DAlias_TransformStandard (ent, ent->model->aliashdr);
			memcpy (&instances->matrix, &ent->matrix, sizeof (D3DMATRIX));

			instances->lerps[0] = ent->aliasstate.blend[LERP_CURR];
			instances->lerps[1] = ent->aliasstate.blend[LERP_CURR] * 0.005f;
			instances->lerps[2] = ent->aliasstate.blend[LERP_LAST];
			instances->lerps[3] = ent->aliasstate.blend[LERP_LAST] * 0.005f;

			D3DAlias_SetupLighting (ent, instances->svec, instances->rgba);
			instances->rgba[3] = (float) ent->alphaval / 255.0f;

			// this count is even more irrelevant now...
			d3d_RenderDef.alias_polys += ent->model->aliashdr->numtris;
		}

		hr = d3d_CurrentInstBuffer->Unlock ();
		if (FAILED (hr)) Sys_Error ("D3DAlias_DrawInstances : failed to unlock vertex buffer");
		d3d_RenderDef.numlock++;

		// we just need one valid entity here so take the first one; the model
		// data will be identical for the rest of them... (we've already ensured that in the sort func)
		aliashdr_t *hdr = ents[firstent]->model->aliashdr;
		aliasstate_t *state = &ents[firstent]->aliasstate;

		// set texture and choose the correct shader
		if (state->lumaimage)
		{
			if (gl_fullbrights.integer)
				D3DHLSL_SetPass (FX_PASS_ALIAS_INSTANCED_LUMA);
			else D3DHLSL_SetPass (FX_PASS_ALIAS_INSTANCED_LUMA_NOLUMA);

			D3DHLSL_SetTexture (0, state->teximage->d3d_Texture);
			D3DHLSL_SetTexture (1, state->lumaimage->d3d_Texture);
		}
		else
		{
			D3DHLSL_SetPass (FX_PASS_ALIAS_INSTANCED_NOLUMA);
			D3DHLSL_SetTexture (0, state->teximage->d3d_Texture);
		}

		aliasbuffers_t *buf = &d3d_AliasBuffers[hdr->buffernum];
		LPDIRECT3DVERTEXBUFFER9 Streams[2];
		int Offsets[2];

		// vertex streams
		// streams zero and one are the model and their frequency is the number of models to draw
		// stream two is the per-instance data and advances to the next value after each instance is drawn
		if (d3d_GlobalCaps.supportStreamOffset)
		{
			Streams[LERP_CURR] = Streams[LERP_LAST] = buf->VertexStreams[0];
			Offsets[LERP_CURR] = buf->StreamOffsets[ents[firstent]->lerppose[LERP_CURR]];
			Offsets[LERP_LAST] = buf->StreamOffsets[ents[firstent]->lerppose[LERP_LAST]];
		}
		else
		{
			Streams[LERP_CURR] = buf->VertexStreams[ents[firstent]->lerppose[LERP_CURR]];
			Streams[LERP_LAST] = buf->VertexStreams[ents[firstent]->lerppose[LERP_LAST]];
			Offsets[LERP_CURR] = Offsets[LERP_LAST] = 0;
		}

		D3D_SetStreamSource (0, Streams[LERP_CURR], Offsets[LERP_CURR], sizeof (aliasvertexstream_t), D3DSTREAMSOURCE_INDEXEDDATA | numinstances);
		D3D_SetStreamSource (1, Streams[LERP_LAST], Offsets[LERP_LAST], sizeof (aliasvertexstream_t), D3DSTREAMSOURCE_INDEXEDDATA | numinstances);
		D3D_SetStreamSource (2, buf->TexCoordStream, 0, sizeof (aliastexcoordstream_t), D3DSTREAMSOURCE_INDEXEDDATA | numinstances);
		D3D_SetStreamSource (3, d3d_CurrentInstBuffer, 0, sizeof (aliasinstance_t), D3DSTREAMSOURCE_INSTANCEDATA | 1);

		D3D_SetIndices (buf->Indexes);

		// get the vertex decl right using our new instanced decl
		D3D_SetVertexDeclaration (d3d_InstanceDecl);

		// draw (as if there were only one model)
		D3D_DrawIndexedPrimitive (0, hdr->nummesh, 0, hdr->numindexes / 3);

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
		// (this actually never gets called; it was just here for testing)
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
			// check basic stuff; note that lerppose[LERP_CURR] and lerppose[LERP_LAST] could be the same but swapped around
			// so we optimize for this when setting up the model for drawing... (done)
			if (ent->model != lastent->model) breakbatch = true;
			if (ent->lerppose[LERP_CURR] != lastent->lerppose[LERP_CURR]) breakbatch = true;
			if (ent->lerppose[LERP_LAST] != lastent->lerppose[LERP_LAST]) breakbatch = true;

			// check textures instead of skinnum as the texture can change for the player model
			if (ent->aliasstate.lumaimage != lastent->aliasstate.lumaimage) breakbatch = true;
			if (ent->aliasstate.teximage != lastent->aliasstate.teximage) breakbatch = true;
		}

		if (numinstances >= MAX_ALIAS_INSTANCES) breakbatch = true;

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
	if (!r_drawentities.integer) return;
	if (!d3d_AliasEdicts) d3d_AliasEdicts = (entity_t **) Zone_Alloc (sizeof (entity_t *) * MAX_VISEDICTS);

	d3d_NumAliasEdicts = 0;
	int NumOccluded = 0;

	for (int i = 0; i < d3d_RenderDef.numvisedicts; i++)
	{
		entity_t *ent = d3d_RenderDef.visedicts[i];

		if (!ent->model) continue;
		if (ent->model->type != mod_alias) continue;

		// initial setup
		D3DAlias_SetupAliasModel (ent);

		if (ent->visframe != d3d_RenderDef.framecount) continue;
		if (ent->Occluded) {NumOccluded++; continue;}

		if (ent->alphaval < 255)
			D3DAlpha_AddToList (ent);
		else d3d_AliasEdicts[d3d_NumAliasEdicts++] = ent;
	}

	// if (NumOccluded) Con_Printf ("occluded %i\n", NumOccluded);
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
	if (d3d_GlobalCaps.supportInstancing && d3d_usinginstancing.value)
		D3DAlias_DrawInstanced (d3d_AliasEdicts, d3d_NumAliasEdicts);
	else D3DAlias_DrawAliasBatch (d3d_AliasEdicts, d3d_NumAliasEdicts);

	// don't bother instancing shadows for now; we might later on if it ever becomes a problem
	D3DAlias_DrawAliasShadows (d3d_AliasEdicts, d3d_NumAliasEdicts);
}


void D3D_SetupProjection (float fovx, float fovy, float zn, float zf);
float SCR_CalcFovX (float fov_y, float width, float height);
float SCR_CalcFovY (float fov_x, float width, float height);

void D3DAlias_DrawViewModel (void)
{
	// conditions for switching off view model
	if (chase_active.value) return;
	if (cl.stats[STAT_HEALTH] <= 0) return;
	if (!cl.viewent.model) return;
	if (!r_drawentities.value) return;
	if (d3d_RenderDef.automap) return;

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
		D3D_SetRenderState (D3DRS_ALPHABLENDENABLE, TRUE);
		D3D_SetRenderState (D3DRS_BLENDOP, D3DBLENDOP_ADD);
		D3D_SetRenderState (D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
		D3D_SetRenderState (D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

		// leave this because it looks ugly if we don't (compare the view model...)
		// D3D_SetRenderState (D3DRS_ZWRITEENABLE, FALSE);
	}

	extern cvar_t scr_fov;
	extern cvar_t scr_fovcompat;
	float aspect = (float) r_refdef.vrect.width / (float) r_refdef.vrect.height;

	if ((scr_fov.value > 90 && !scr_fovcompat.integer) || aspect < (4.0f / 3.0f))
	{
		// recalculate the correct fov for displaying the gun model as if fov was 90
		float fov_y = SCR_CalcFovY (90, 640, 432);
		float fov_x = SCR_CalcFovX (fov_y, r_refdef.vrect.width, r_refdef.vrect.height);

		// adjust projection to a constant y fov for wide-angle views
		// don't need to worry over much about the far clipping plane here
		D3D_SetupProjection (fov_x, fov_y, 4, 4096);

		// calculate concatenated final matrix for use by shaders
		// because it's only needed once per frame instead of once per vertex we can save some vs instructions
		D3DMatrix_Multiply (&d3d_ModelViewProjMatrix, &d3d_ViewMatrix, &d3d_WorldMatrix);
		D3DMatrix_Multiply (&d3d_ModelViewProjMatrix, &d3d_ProjMatrix);

		// we don't need to extract the frustum as the gun is never frustum-culled
		D3DHLSL_SetWorldMatrix (&d3d_ModelViewProjMatrix);
	}

	// setup the frame for drawing and store the interpolation blend
	D3DAlias_SetupAliasFrame (ent, hdr);

	// add it to the list and draw it (there will only ever be one of these do don't even bother with instancing)
	D3DAlias_SetupAliasModel (ent);

	// draw directly as we may wish to change some stuff here...
	D3D_SetTextureMipmap (0, d3d_TexFilter, d3d_MipFilter);
	D3D_SetTextureMipmap (1, d3d_TexFilter, d3d_MipFilter);

	D3D_SetTextureAddress (0, D3DTADDRESS_CLAMP);
	D3D_SetTextureAddress (1, D3DTADDRESS_CLAMP);

	D3D_SetVertexDeclaration (d3d_ViewModelDecl);

	// interpolation clearing gets this
	if (ent->lerppose[LERP_CURR] < 0) ent->lerppose[LERP_CURR] = 0;
	if (ent->lerppose[LERP_LAST] < 0) ent->lerppose[LERP_LAST] = 0;

	// assume that the textures will always need to change
	if (ent->aliasstate.lumaimage)
	{
		if (gl_fullbrights.integer)
			D3DHLSL_SetPass (FX_PASS_ALIAS_VIEWMODEL_LUMA);
		else D3DHLSL_SetPass (FX_PASS_ALIAS_VIEWMODEL_LUMA_NOLUMA);

		D3DHLSL_SetTexture (0, ent->aliasstate.teximage->d3d_Texture);
		D3DHLSL_SetTexture (1, ent->aliasstate.lumaimage->d3d_Texture);
	}
	else
	{
		D3DHLSL_SetPass (FX_PASS_ALIAS_VIEWMODEL_NOLUMA);
		D3DHLSL_SetTexture (0, ent->aliasstate.teximage->d3d_Texture);
	}

	// and now we can send it through the standard draw func
	D3DAlias_DrawModel (ent, hdr, AM_VIEWMODEL);

	// restore alpha
	ent->alphaval = 255;

	// restoring the original projection is unnecessary as the gun is the last thing drawn in the 3D view
	if ((cl.items & IT_INVISIBILITY) || r_drawviewmodel.value < 0.99f)
	{
		// D3D_SetRenderState (D3DRS_ZWRITEENABLE, TRUE);
		D3D_SetRenderState (D3DRS_ALPHABLENDENABLE, FALSE);
	}
}


