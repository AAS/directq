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
#include "iqm.h"

// beware of row-major/column-major differences.  i use column-major thoughout this code with the exception of
// D3DXMatrixTransformation which generates a row-major matrix and needs to be transposed (i've wrapped it to
// do this and get rid of some unused params too

float RadiusFromBounds (vec3_t mins, vec3_t maxs);

void QMatrixTransformation (D3DXMATRIX *m, D3DXVECTOR3 *s, D3DXQUATERNION *r, D3DXVECTOR3 *t)
{
	D3DXMatrixTransformation (m, NULL, NULL, s, NULL, r, t);
	D3DXMatrixTranspose (m, m);
}


void IQM_LoadVertexes (iqmheader_t *iqm, iqmdata_t *iqmdata)
{
	float *vposition = NULL, *vtexcoord = NULL, *vnormal = NULL;
	unsigned char *vblendindexes = NULL, *vblendweights = NULL;
	byte *buf = (byte *) iqm;
	iqmvertexarray_t *va = (iqmvertexarray_t *) (buf + iqm->ofs_vertexarrays);

	for (int i = 0; i < iqm->num_vertexarrays; i++)
	{
		switch (va[i].type)
		{
		case IQM_POSITION:
			if (va[i].format == IQM_FLOAT && va[i].size == 3)
				vposition = (float *) (buf + va[i].offset);
			break;

		case IQM_TEXCOORD:
			if (va[i].format == IQM_FLOAT && va[i].size == 2)
				vtexcoord = (float *) (buf + va[i].offset);
			break;

		case IQM_NORMAL:
			if (va[i].format == IQM_FLOAT && va[i].size == 3)
				vnormal = (float *) (buf + va[i].offset);
			break;

		case IQM_BLENDINDEXES:
			if (va[i].format == IQM_UBYTE && va[i].size == 4)
				vblendindexes = (unsigned char *) (buf + va[i].offset);
			break;

		case IQM_BLENDWEIGHTS:
			if (va[i].format == IQM_UBYTE && va[i].size == 4)
				vblendweights = (unsigned char *) (buf + va[i].offset);
			break;
		}
	}

	if (!vposition || !vtexcoord || !vnormal || !vblendindexes || !vblendweights || iqmdata->num_joints > d3d_GlobalCaps.MaxIQMJoints)
	{
		Sys_Error ("IQM_LoadVertexes : incomplete model or limits exceeded");
		return;
	}

	// load vertex data
	iqmdata->verts = (iqmvertex_t *) ModelZone->Alloc (iqmdata->numvertexes * sizeof (iqmvertex_t));

	for (int i = 0; i < iqmdata->numvertexes; i++, vposition += 3, vnormal += 3, vtexcoord += 2, vblendindexes += 4, vblendweights += 4)
	{
		for (int j = 0; j < 3; j++)
		{
			iqmdata->verts[i].position[j] = vposition[j];
			iqmdata->verts[i].normal[j] = vnormal[j];
		}

		iqmdata->verts[i].texcoord[0] = vtexcoord[0];
		iqmdata->verts[i].texcoord[1] = vtexcoord[1];

		for (int j = 0; j < 4; j++)
		{
			iqmdata->verts[i].blendindexes[j] = vblendindexes[j];
			iqmdata->verts[i].blendweights[j] = vblendweights[j];
		}
	}
}


void IQM_LoadV1Joints (iqmheader_t *iqm, iqmdata_t *iqmdata, D3DXMATRIX *baseframe, D3DXMATRIX *inversebaseframe)
{
	byte *buf = (byte *) iqm;

	// load the joints
	iqmdata->jointsv1 = (iqmjointv1_t *) ModelZone->Alloc (iqm->num_joints * sizeof (iqmjointv1_t));
	memcpy (iqmdata->jointsv1, (buf + iqm->ofs_joints), iqm->num_joints * sizeof (iqmjointv1_t));

	for (int i = 0; i < (int) iqm->num_joints; i++)
	{
		iqmjointv1_t *j = &iqmdata->jointsv1[i];

		// first need to make a vec4 quat from our rotation vec
		D3DXVECTOR3 rot (j->rotate[0], j->rotate[1], j->rotate[2]);
		D3DXQUATERNION q_rot (j->rotate[0], j->rotate[1], j->rotate[2], -sqrt (max (1.0 - pow (D3DXVec3Length (&rot), 2), 0.0)));

		QMatrixTransformation (&baseframe[i], &D3DXVECTOR3 (j->scale), &q_rot, &D3DXVECTOR3 (j->translate));
		D3DXMatrixInverse (&inversebaseframe[i], NULL, &baseframe[i]);

		if (j->parent >= 0)
		{
			baseframe[i] = baseframe[j->parent] * baseframe[i];
			inversebaseframe[i] = inversebaseframe[i] * inversebaseframe[j->parent];
		}
	}
}


void IQM_LoadV2Joints (iqmheader_t *iqm, iqmdata_t *iqmdata, D3DXMATRIX *baseframe, D3DXMATRIX *inversebaseframe)
{
	byte *buf = (byte *) iqm;

	// load the joints
	iqmdata->jointsv2 = (iqmjointv2_t *) ModelZone->Alloc (iqm->num_joints * sizeof (iqmjointv2_t));
	memcpy (iqmdata->jointsv2, (buf + iqm->ofs_joints), iqm->num_joints * sizeof (iqmjointv2_t));

	for (int i = 0; i < (int) iqm->num_joints; i++)
	{
		iqmjointv2_t *j = &iqmdata->jointsv2[i];

		QMatrixTransformation (&baseframe[i], &D3DXVECTOR3 (j->scale), &D3DXQUATERNION (j->rotate), &D3DXVECTOR3 (j->translate));
		D3DXMatrixInverse (&inversebaseframe[i], NULL, &baseframe[i]);

		if (j->parent >= 0)
		{
			baseframe[i] = baseframe[j->parent] * baseframe[i];
			inversebaseframe[i] = inversebaseframe[i] * inversebaseframe[j->parent];
		}
	}
}


void IQM_LoadV1Poses (iqmheader_t *iqm, iqmdata_t *iqmdata, D3DXMATRIX *baseframe, D3DXMATRIX *inversebaseframe)
{
	byte *buf = (byte *) iqm;
	iqmposev1_t *posesv1 = (iqmposev1_t *) (buf + iqm->ofs_poses);
	unsigned short *framedata = (unsigned short *) (buf + iqm->ofs_frames);
	float posevecs[9];
	D3DXMATRIX m;

	for (int i = 0; i < iqm->num_frames; i++)
	{
		for (int j = 0; j < iqm->num_poses; j++)
		{
			iqmposev1_t p = posesv1[j];

			for (int trs = 0; trs < 9; trs++)
			{
				posevecs[trs] = p.channeloffset[trs];
				if (p.mask & (1 << trs)) posevecs[trs] += *framedata++ * p.channelscale[trs];
			}

			QMatrixTransformation (&m, &D3DXVECTOR3 (&posevecs[6]),
				&D3DXQUATERNION (posevecs[3], posevecs[4], posevecs[5], -sqrt (max (1.0 - pow (D3DXVec3Length (&D3DXVECTOR3 (&posevecs[3])), 2), 0.0))),
				&D3DXVECTOR3 (posevecs));

			if (p.parent >= 0)
				iqmdata->frames[i * iqm->num_poses + j] = (baseframe[p.parent] * m) * inversebaseframe[j];
			else iqmdata->frames[i * iqm->num_poses + j] = m * inversebaseframe[j];
		}
	}
}


void IQM_LoadV2Poses (iqmheader_t *iqm, iqmdata_t *iqmdata, D3DXMATRIX *baseframe, D3DXMATRIX *inversebaseframe)
{
	byte *buf = (byte *) iqm;
	iqmposev2_t *posesv2 = (iqmposev2_t *) (buf + iqm->ofs_poses);
	unsigned short *framedata = (unsigned short *) (buf + iqm->ofs_frames);
	float posevecs[10];
	D3DXMATRIX m;

	for (int i = 0; i < iqm->num_frames; i++)
	{
		for (int j = 0; j < iqm->num_poses; j++)
		{
			iqmposev2_t p = posesv2[j];

			for (int trs = 0; trs < 10; trs++)
			{
				posevecs[trs] = p.channeloffset[trs];
				if (p.mask & (1 << trs)) posevecs[trs] += *framedata++ * p.channelscale[trs];
			}

			QMatrixTransformation (&m, &D3DXVECTOR3 (&posevecs[7]), &D3DXQUATERNION (&posevecs[3]), &D3DXVECTOR3 (posevecs));

			if (p.parent >= 0)
				iqmdata->frames[i * iqm->num_poses + j] = (baseframe[p.parent] * m) * inversebaseframe[j];
			else iqmdata->frames[i * iqm->num_poses + j] = m * inversebaseframe[j];
		}
	}
}


void IQM_LoadJoints (iqmheader_t *iqm, iqmdata_t *iqmdata)
{
	byte *buf = (byte *) iqm;
	int hunkmark = MainHunk->GetLowMark ();

	// these don't need to be a part of mod
	D3DXMATRIX *baseframe = (D3DXMATRIX *) MainHunk->Alloc (iqm->num_joints * sizeof (D3DXMATRIX));
	D3DXMATRIX *inversebaseframe = (D3DXMATRIX *) MainHunk->Alloc (iqm->num_joints * sizeof (D3DXMATRIX));

	if (iqm->version == IQM_VERSION1)
		IQM_LoadV1Joints (iqm, iqmdata, baseframe, inversebaseframe);
	else IQM_LoadV2Joints (iqm, iqmdata, baseframe, inversebaseframe);

	if (!iqm->num_poses || !iqm->num_frames)
		iqmdata->frames = NULL;
	else
	{
		iqmdata->frames = (D3DXMATRIX *) ModelZone->Alloc (iqm->num_frames * iqm->num_poses * sizeof (D3DXMATRIX));

		if (iqm->version == IQM_VERSION1)
			IQM_LoadV1Poses (iqm, iqmdata, baseframe, inversebaseframe);
		else IQM_LoadV2Poses (iqm, iqmdata, baseframe, inversebaseframe);
	}

	MainHunk->FreeToLowMark (hunkmark);
}


void IQM_LoadBounds (iqmheader_t *iqm, iqmdata_t *iqmdata, model_t *mod)
{
	byte *buf = (byte *) iqm;

	// load bounding box data
	if (iqm->ofs_bounds)
	{
		float xyradius = 0, radius = 0;
		iqmbounds_t *bounds = (iqmbounds_t *) (buf + iqm->ofs_bounds);

		Mod_ClearBoundingBox (iqmdata->mins, iqmdata->maxs);

		// we're only using one frame so we only use one bounding box for the entire model
		// we'll still check 'em all just to be precise though
		for (int i = 0; i < (int) iqm->num_frames; i++)
		{
			Mod_AccumulateBox (iqmdata->mins, iqmdata->maxs, bounds[i].bbmin, bounds[i].bbmax);

			bounds[i].xyradius = bounds[i].xyradius;
			bounds[i].radius = bounds[i].radius;

			if (bounds[i].xyradius > xyradius) xyradius = bounds[i].xyradius;
			if (bounds[i].radius > radius) radius = bounds[i].radius;
		}
	}
	else
	{
		// no bounds so just take it from the vertexes
		Mod_ClearBoundingBox (iqmdata->mins, iqmdata->maxs);

		for (int i = 0; i < iqmdata->numvertexes; i++)
			Mod_AccumulateBox (iqmdata->mins, iqmdata->maxs, iqmdata->verts[i].position);
	}

	for (int i = 0; i < 3; i++)
	{
		// this bbox is used server-side for collisions so it should be clamped
		if (iqmdata->mins[i] > -16) mod->mins[i] = -16; else mod->mins[i] = iqmdata->mins[i];
		if (iqmdata->maxs[i] < 16) mod->maxs[i] = 16; else mod->maxs[i] = iqmdata->maxs[i];
	}
}


void IQM_LoadTriangles (iqmheader_t *iqm, iqmdata_t *iqmdata)
{
	byte *buf = (byte *) iqm;
	int *inelements;

	// load triangle data - here we compress down to unsigned short because OpenGL weenies are unaware of how hardware actually works
	inelements = (int *) (buf + iqm->ofs_triangles);
	iqmdata->tris = (iqmtriangle_t *) ModelZone->Alloc (iqm->num_triangles * sizeof (iqmtriangle_t));

	for (int i = 0; i < (int) iqm->num_triangles; i++)
	{
		iqmdata->tris[i].vertex[0] = inelements[0];
		iqmdata->tris[i].vertex[1] = inelements[1];
		iqmdata->tris[i].vertex[2] = inelements[2];
		inelements += 3;
	}
}


void IQM_LoadMesh (iqmheader_t *iqm, iqmdata_t *iqmdata, char *path)
{
	byte *buf = (byte *) iqm;
	char *str = (char *) &buf[iqm->ofs_text];
	iqmmesh_t *mesh = (iqmmesh_t *) (buf + iqm->ofs_meshes);

	// lead meshes
	iqmdata->num_mesh = iqm->num_meshes;
	iqmdata->numindexes = 0;
	iqmdata->mesh = (iqmmesh_t *) ModelZone->Alloc (iqmdata->num_mesh * sizeof (iqmmesh_t));
	iqmdata->skins = (LPDIRECT3DTEXTURE9 *) ModelZone->Alloc (iqmdata->num_mesh * sizeof (LPDIRECT3DTEXTURE9));
	iqmdata->fullbrights = (LPDIRECT3DTEXTURE9 *) ModelZone->Alloc (iqmdata->num_mesh * sizeof (LPDIRECT3DTEXTURE9));

	for (int i = 0; i < iqmdata->num_mesh; i++)
	{
		char texturepath[256];
		int hunkmark = MainHunk->GetLowMark ();

		// no more endian weenieness here
		memcpy (&iqmdata->mesh[i], &mesh[i], sizeof (iqmmesh_t));
		iqmdata->numindexes += mesh[i].num_triangles * 3;

		// build the base texture path from the model directory and the material defined in the IQM
		strcpy (texturepath, path);

		for (int j = strlen (texturepath); j; j--)
		{
			if (texturepath[j] == '/' || texturepath[j] == '\\')
			{
				strcpy (&texturepath[j + 1], &str[iqmdata->mesh[i].material]);
				break;
			}
		}

		COM_StripExtension (texturepath, texturepath);

		iqmdata->skins[i] = D3DTexture_Load (texturepath, 0, 0, NULL, IMAGE_32BIT | IMAGE_MIPMAP);
		iqmdata->fullbrights[i] = D3DTexture_Load (texturepath, 0, 0, NULL, IMAGE_32BIT | IMAGE_MIPMAP | IMAGE_LUMA);

		Con_DPrintf ("texture %i %s\n", i, texturepath);

		MainHunk->FreeToLowMark (hunkmark);
	}
}


void Mod_LoadIQMModel (model_t *mod, void *buffer, char *path)
{
	iqmheader_t *hdr = (iqmheader_t *) buffer;
	int hunkmark = MainHunk->GetLowMark ();

	if (strcmp (hdr->magic, IQM_MAGIC)) goto IQM_LoadError;
	if (hdr->version != IQM_VERSION1 && hdr->version != IQM_VERSION2) goto IQM_LoadError;

	// just needs to be something that's not mod_iqm so that we can test for success
	mod->type = mod_alias;

	iqmdata_t *iqmdata = (iqmdata_t *) ModelZone->Alloc (sizeof (iqmdata_t));

	// set these up-front in case anything in the loaders need them
	iqmdata->num_frames = hdr->num_anims;
	iqmdata->num_joints = hdr->num_joints;
	iqmdata->num_poses = hdr->num_frames;
	iqmdata->numvertexes = hdr->num_vertexes;
	iqmdata->num_triangles = hdr->num_triangles;

	IQM_LoadVertexes (hdr, iqmdata);
	IQM_LoadJoints (hdr, iqmdata);
	IQM_LoadBounds (hdr, iqmdata, mod);
	IQM_LoadTriangles (hdr, iqmdata);
	IQM_LoadMesh (hdr, iqmdata, path);

	// store the version so that we know how to animate it
	iqmdata->version = hdr->version;

	// loaded OK
	mod->iqmheader = iqmdata;
	mod->flags = hdr->flags;

	// this signifies success
	mod->type = mod_iqm;

	MainHunk->FreeToLowMark (hunkmark);

	if (mod->type == mod_iqm) return;

IQM_LoadError:;
	Sys_Error ("Mod_LoadIQMModel : failed to load %s\n", path);
}


bool Mod_FindIQMModel (model_t *mod)
{
	HANDLE fh = INVALID_HANDLE_VALUE;
	char iqmname[MAX_PATH];
	iqmheader_t *hdr = NULL;
	int hunkmark = MainHunk->GetLowMark ();

	// attempt q2 naming convention
	COM_StripExtension (mod->name, iqmname);
	strcat (iqmname, "/tris.iqm");

	COM_FOpenFile (iqmname, &fh);

	if (fh != INVALID_HANDLE_VALUE && com_filesize > 0)
	{
		hdr = (iqmheader_t *) MainHunk->Alloc (com_filesize);
		COM_FReadFile (fh, hdr, com_filesize);
		COM_FCloseFile (&fh);
		Mod_LoadIQMModel (mod, hdr, iqmname);
		MainHunk->FreeToLowMark (hunkmark);
		return true;
	}

	// fallback on direct replace
	COM_StripExtension (mod->name, iqmname);
	COM_DefaultExtension (iqmname, ".iqm");

	COM_FOpenFile (iqmname, &fh);

	if (fh != INVALID_HANDLE_VALUE && com_filesize > 0)
	{
		hdr = (iqmheader_t *) MainHunk->Alloc (com_filesize);
		COM_FReadFile (fh, hdr, com_filesize);
		COM_FCloseFile (&fh);
		Mod_LoadIQMModel (mod, hdr, iqmname);
		MainHunk->FreeToLowMark (hunkmark);
		return true;
	}

	return false;
}


typedef struct iqmbuffer_s
{
	LPDIRECT3DVERTEXBUFFER9 Vertexes;
	LPDIRECT3DINDEXBUFFER9 Indexes;
} iqmbuffer_t;


iqmbuffer_t d3d_IQMBuffers[MAX_MOD_KNOWN];


LPDIRECT3DVERTEXDECLARATION9 d3d_IQMDecl = NULL;

void D3DIQM_CreateBuffers (void)
{
	model_t *mod = NULL;

	for (int i = 0; i < MAX_MOD_KNOWN; i++)
	{
		SAFE_RELEASE (d3d_IQMBuffers[i].Vertexes);
		SAFE_RELEASE (d3d_IQMBuffers[i].Indexes);

		// nothing allocated yet
		if (!mod_known) continue;
		if (!(mod = mod_known[i])) continue;
		if (mod->type != mod_iqm) continue;

		iqmdata_t *hdr = mod->iqmheader;
		unsigned short *ndx = NULL;
		iqmvertex_t *verts = NULL;

		D3DMain_CreateVertexBuffer (hdr->numvertexes * sizeof (iqmvertex_t), D3DUSAGE_WRITEONLY, &d3d_IQMBuffers[i].Vertexes);
		D3DMain_CreateIndexBuffer16 (hdr->numindexes, D3DUSAGE_WRITEONLY, &d3d_IQMBuffers[i].Indexes);

		if (FAILED (d3d_IQMBuffers[i].Indexes->Lock (0, 0, (void **) &ndx, d3d_GlobalCaps.DefaultLock)))
			Sys_Error ("D3DIQM_CreateBuffers: failed to lock index buffer");

		for (int m = 0; m < hdr->num_mesh; m++)
		{
			memcpy (ndx, &hdr->tris[hdr->mesh[m].first_triangle], 3 * hdr->mesh[m].num_triangles * sizeof (unsigned short));
			ndx += hdr->mesh[m].num_triangles * 3;
		}

		if (FAILED (d3d_IQMBuffers[i].Indexes->Unlock ()))
			Sys_Error ("D3DIQM_CreateBuffers: failed to unlock index buffer");

		if (FAILED (d3d_IQMBuffers[i].Vertexes->Lock (0, 0, (void **) &verts, d3d_GlobalCaps.DefaultLock)))
			Sys_Error ("D3DIQM_CreateBuffers: failed to lock vertex buffer");

		memcpy (verts, hdr->verts, hdr->numvertexes * sizeof (iqmvertex_t));

		if (FAILED (d3d_IQMBuffers[i].Vertexes->Unlock ()))
			Sys_Error ("D3DIQM_CreateBuffers: failed to unlock vertex buffer");

		d3d_RenderDef.numlock += 2;

		// and this is the buffer set that this model will use
		hdr->buffernum = i;
	}

	if (!d3d_IQMDecl)
	{
		// second stream will be added later...
		D3DVERTEXELEMENT9 d3d_iqmlayout[] =
		{
			VDECL (0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0),
			VDECL (0, 12, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0),	// this is really a normal
			VDECL (0, 24, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1),
			VDECL (0, 32, D3DDECLTYPE_UBYTE4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 2),	// blend indexes
			VDECL (0, 36, D3DDECLTYPE_UBYTE4N, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 3),	// blend weights
			D3DDECL_END ()
		};

		if (FAILED (d3d_Device->CreateVertexDeclaration (d3d_iqmlayout, &d3d_IQMDecl)))
			Sys_Error ("D3DIQM_CreateBuffers: d3d_Device->CreateVertexDeclaration failed");
	}
}


void D3DIQM_ReleaseBuffers (void)
{
	for (int i = 0; i < MAX_MOD_KNOWN; i++)
	{
		SAFE_RELEASE (d3d_IQMBuffers[i].Vertexes);
		SAFE_RELEASE (d3d_IQMBuffers[i].Indexes);
	}

	SAFE_RELEASE (d3d_IQMDecl);
}


CD3DDeviceLossHandler d3d_IQMBuffersHandler (D3DIQM_ReleaseBuffers, D3DIQM_CreateBuffers);

// shared code with alias model renderer
void D3DAlias_SetupLighting (entity_t *ent, struct aliasinstance_s *instance);
void D3DLight_LightPoint (entity_t *e);
void D3DAlias_TransformStandard (entity_t *ent);
void D3DAlias_TransformShadowed (entity_t *ent);

void D3DIQM_AnimateFrame (iqmdata_t *hdr, int currframe, int lastframe, float lerp)
{
	// temp memory for building the animation
	D3DXMATRIX *outframe = (D3DXMATRIX *) scratchbuf;
	D3DXVECTOR4 *outvecs1 = (D3DXVECTOR4 *) (outframe + hdr->num_joints);
	D3DXVECTOR4 *outvecs2 = (D3DXVECTOR4 *) (outvecs1 + hdr->num_joints);
	D3DXVECTOR4 *outvecs3 = (D3DXVECTOR4 *) (outvecs2 + hdr->num_joints);

	// this memory should never be referenced if there are no frames or poses
	D3DXMATRIX *mat1 = (!hdr->num_poses || !hdr->num_frames) ? NULL : &hdr->frames[(lastframe % hdr->num_poses) * hdr->num_joints];
	D3DXMATRIX *mat2 = (!hdr->num_poses || !hdr->num_frames) ? NULL : &hdr->frames[(currframe % hdr->num_poses) * hdr->num_joints];

	// and build the animation
	for (int i = 0; i < hdr->num_joints; i++)
	{
		// the joints were unioned and parent is in the same memory location so this is valid to do
		if (!hdr->num_poses || !hdr->num_frames)
			D3DXMatrixIdentity (&outframe[i]);
		else if (hdr->jointsv2[i].parent >= 0)
			outframe[i] = outframe[hdr->jointsv2[i].parent] * (mat1[i] + ((mat2[i] - mat1[i]) * lerp));
		else outframe[i] = mat1[i] + ((mat2[i] - mat1[i]) * lerp);

		// copy out to 3xvec4 for more space in the constants (otherwise we'd just use a matrix array) (in d3d11 we'll use 3 cbuffers, one for each row)
		outvecs1[i] = D3DXVECTOR4 (outframe[i].m[0]);
		outvecs2[i] = D3DXVECTOR4 (outframe[i].m[1]);
		outvecs3[i] = D3DXVECTOR4 (outframe[i].m[2]);
	}

	// set this up for the layout i'll use when i move to d3d11 - it's faster here too...
	D3DHLSL_SetVectorArray ("IQMJoints1", outvecs1, hdr->num_joints);
	D3DHLSL_SetVectorArray ("IQMJoints2", outvecs2, hdr->num_joints);
	D3DHLSL_SetVectorArray ("IQMJoints3", outvecs3, hdr->num_joints);
}


void D3DIQM_DrawIQM (entity_t *ent)
{
	model_t *mod = ent->model;
	iqmdata_t *hdr = mod->iqmheader;

	D3D_SetStreamSource (0, d3d_IQMBuffers[hdr->buffernum].Vertexes, 0, sizeof (iqmvertex_t));
	D3D_SetIndices (d3d_IQMBuffers[hdr->buffernum].Indexes);

	D3DAlias_TransformStandard (ent);

	D3DIQM_AnimateFrame (hdr, ent->currpose, ent->lastpose, ent->poseblend);
	D3DHLSL_SetEntMatrix (&ent->matrix);

	D3DLight_LightPoint (ent);
	D3DAlias_SetupLighting (ent, NULL);

	for (int i = 0; i < hdr->num_mesh; i++)
	{
		if (hdr->fullbrights[i])
		{
			D3DHLSL_SetPass (FX_PASS_IQM_LUMA);
			D3DHLSL_SetTexture (1, hdr->fullbrights[i]);
		}
		else D3DHLSL_SetPass (FX_PASS_IQM_NOLUMA);

		D3DHLSL_SetTexture (0, hdr->skins[i]);

		D3D_DrawIndexedPrimitive (D3DPT_TRIANGLELIST, hdr->mesh[i].first_vertex, hdr->mesh[i].num_vertexes, hdr->mesh[i].first_triangle * 3, hdr->mesh[i].num_triangles);
		d3d_RenderDef.alias_polys += hdr->mesh[i].num_triangles;
	}

	// no shadows on alpha IQMs
	if (ent->alphaval > 0 && ent->alphaval < 255) return;

	// just draw shadows in-place while we have joint transforms up and loaded
	// yeah it's state changes, but the alternative is to rebuild and reload the joints
	if (r_shadows.value > 0 && ent != &cl.viewent)
	{
		D3DState_EnableShadows (true);

		// different transform for shadowing
		D3DAlias_TransformShadowed (ent);

		D3DHLSL_SetPass (FX_PASS_IQM_SHADOW);
		D3DHLSL_SetEntMatrix (&ent->matrix);

		// set pass and draw
		for (int i = 0; i < hdr->num_mesh; i++)
		{
			D3D_DrawIndexedPrimitive (D3DPT_TRIANGLELIST, hdr->mesh[i].first_vertex, hdr->mesh[i].num_vertexes, hdr->mesh[i].first_triangle * 3, hdr->mesh[i].num_triangles);
			d3d_RenderDef.alias_polys += hdr->mesh[i].num_triangles;
		}

		// back to normal
		D3DState_EnableShadows (false);
	}
}


void D3DIQM_SetCommonState (void)
{
	D3D_SetTextureMipmap (0, d3d_TexFilter, d3d_MipFilter);
	D3D_SetTextureMipmap (1, d3d_TexFilter, d3d_MipFilter);

	D3D_SetTextureAddress (0, D3DTADDRESS_CLAMP);
	D3D_SetTextureAddress (1, D3DTADDRESS_CLAMP);

	D3D_SetVertexDeclaration (d3d_IQMDecl);

	D3D_SetStreamSource (1, NULL, 0, 0);
	D3D_SetStreamSource (2, NULL, 0, 0);
	D3D_SetStreamSource (3, NULL, 0, 0);
}


void D3DIQM_DrawIQMs (void)
{
	bool stateset = false;

	for (int i = 0; i < d3d_RenderDef.numvisedicts; i++)
	{
		entity_t *ent = d3d_RenderDef.visedicts[i];

		if (!ent->model) continue;
		if (ent->model->type != mod_iqm) continue;

		// we must always have at least one valid skin
		if (!ent->model->iqmheader->skins[0]) continue;

		if (R_CullBox (ent->mins, ent->maxs, frustum))
		{
			CL_ClearInterpolation (ent, CLEAR_ALLLERP);
			continue;
		}

		// mark as visible (primarily for bbox drawing)
		ent->visframe = d3d_RenderDef.framecount;

		if (ent->alphaval > 0 && ent->alphaval < 255)
		{
			D3DAlpha_AddToList (ent);
			continue;
		}

		if (!stateset)
		{
			D3DIQM_SetCommonState ();
			stateset = true;
		}

		D3DIQM_DrawIQM (ent);
	}
}

