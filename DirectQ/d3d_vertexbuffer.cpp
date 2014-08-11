
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

// changed these because they are hardware variable
int D3D_MAX_VB_SIZE = 65534;
int D3D_MAX_IB_SIZE = 262144;

#define D3D_COMMONVERT_SIZE		28

typedef struct vbo_statedef_s
{
	vbostatefunc_t callback;
	byte data[64];

	int firstvert;
	int numverts;

	int firstindex;
	int numindexes;
} vbo_statedef_t;

typedef struct vbo_state_s
{
	byte *vbodata;
	unsigned short *ibodata;

	byte *vbodataup;
	unsigned short *ibodataup;

	int numverts;
	int numindexes;

	LPDIRECT3DVERTEXBUFFER9 vbo;
	LPDIRECT3DINDEXBUFFER9 ibo;

	int stride;
	bool locked;

	int numstatedefs;
	vbo_statedef_t *currstatedef;
} vbo_state_t;

#define MAX_STATEDEF 1024

vbo_state_t vboState;
vbo_statedef_t *vboStateDef = NULL;


void VBO_DestroyBuffers (void)
{
	SAFE_RELEASE (vboState.vbo);
	SAFE_RELEASE (vboState.ibo);
}


void VBO_InitFrameParams (void)
{
	if (!vboStateDef)
		vboStateDef = (vbo_statedef_t *) MainZone->Alloc (sizeof (vbo_statedef_t) * MAX_STATEDEF);

	// initial params
	vboState.ibodata = NULL;
	vboState.locked = false;
	vboState.numindexes = 0;
	vboState.numverts = 0;
	vboState.stride = 0;
	vboState.vbodata = NULL;

	vboState.numstatedefs = 0;
	vboState.currstatedef = NULL;
}


void VBO_BeginFrame (void)
{
	// MaxPrimitiveCount is also the max number of unique verts; since every vert in a draw call
	// might potentially be unique we clamp to it for the ib size
	if (D3D_MAX_IB_SIZE >= d3d_DeviceCaps.MaxPrimitiveCount)
		D3D_MAX_IB_SIZE = d3d_DeviceCaps.MaxPrimitiveCount;

	if (D3D_MAX_VB_SIZE >= d3d_DeviceCaps.MaxVertexIndex)
		D3D_MAX_VB_SIZE = d3d_DeviceCaps.MaxVertexIndex;

	if (!d3d_GlobalCaps.supportVertexBuffers)
	{
		// destroy any buffers that were created
		VBO_DestroyBuffers ();

		// create the UP buffers
		if (!vboState.vbodataup) vboState.vbodataup = (byte *) MainZone->Alloc (D3D_MAX_VB_SIZE * D3D_COMMONVERT_SIZE);
		if (!vboState.ibodataup) vboState.ibodataup = (unsigned short *) MainZone->Alloc (D3D_MAX_IB_SIZE * sizeof (unsigned short));

		if (!vboState.vbodataup || !vboState.ibodataup)
			Sys_Error ("Failed to create backup memory buffers");

		VBO_InitFrameParams ();
		return;
	}

	if (!vboState.vbo)
	{
		hr = d3d_Device->CreateVertexBuffer
		(
			D3D_MAX_VB_SIZE * D3D_COMMONVERT_SIZE,
			D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY,
			0,
			D3DPOOL_DEFAULT,
			&vboState.vbo,
			NULL
		);

		if (FAILED (hr)) d3d_GlobalCaps.supportVertexBuffers = false;
	}

	if (!vboState.ibo)
	{
		hr = d3d_Device->CreateIndexBuffer
		(
			D3D_MAX_IB_SIZE * sizeof (unsigned short),
			D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY,
			D3DFMT_INDEX16,
			D3DPOOL_DEFAULT,
			&vboState.ibo,
			NULL
		);

		if (FAILED (hr)) d3d_GlobalCaps.supportVertexBuffers = false;
	}

	VBO_InitFrameParams ();

	// if we failed to create any buffers we call recursive to go into UP mode
	if (!d3d_GlobalCaps.supportVertexBuffers) VBO_BeginFrame ();
}


void VBO_EndFrame (void)
{
	if (vboState.locked)
	{
		// there's no unlocking or pointer resets with the up buffers
		if (d3d_GlobalCaps.supportVertexBuffers)
		{
			vboState.vbo->Unlock ();
			vboState.ibo->Unlock ();
		}

		vboState.locked = false;

		vboState.vbodata = NULL;
		vboState.ibodata = NULL;
	}

	vbo_statedef_t *sd = vboStateDef;

	for (int i = 0; i < vboState.numstatedefs; i++, sd++)
	{
		if (sd->callback) sd->callback (sd->data);

		if (sd->numverts && sd->numindexes)
		{
			if (d3d_FXCommitPending)
			{
				// commit if required
				d3d_MasterFX->CommitChanges ();
				d3d_FXCommitPending = false;
			}

			if (d3d_GlobalCaps.supportVertexBuffers)
			{
				d3d_Device->DrawIndexedPrimitive
					(D3DPT_TRIANGLELIST,
					0,
					sd->firstvert,
					sd->numverts,
					sd->firstindex,
					sd->numindexes / 3);
			}
			else
			{
				d3d_Device->DrawIndexedPrimitiveUP
					(D3DPT_TRIANGLELIST,
					sd->firstvert,
					sd->numverts,
					sd->numindexes / 3,
					&vboState.ibodataup[sd->firstindex],
					D3DFMT_INDEX16,
					vboState.vbodataup,
					vboState.stride);
			}

			d3d_RenderDef.numdrawprim++;
		}
	}

	vboState.numverts = 0;
	vboState.numindexes = 0;

	vboState.currstatedef = &vboStateDef[0];
	vboState.numstatedefs = 1;

	// add a first callback with initial properties
	vboState.currstatedef->callback = NULL;
	vboState.currstatedef->firstindex = 0;
	vboState.currstatedef->firstvert = 0;
	vboState.currstatedef->numindexes = 0;
	vboState.currstatedef->numverts = 0;
}


void VBO_AddCallback (vbostatefunc_t callback, void *data, int len)
{
	vboState.currstatedef = &vboStateDef[vboState.numstatedefs];
	vboState.numstatedefs++;

	vboState.currstatedef->callback = callback;

	if (data) Q_MemCpy (vboState.currstatedef->data, data, len);

	vboState.currstatedef->firstindex = vboState.numindexes;
	vboState.currstatedef->firstvert = vboState.numverts;
	vboState.currstatedef->numindexes = 0;
	vboState.currstatedef->numverts = 0;
}


void VBO_BeginBatchSet (int numverts, int numindexes, int vertexstride)
{
	if (vertexstride != vboState.stride)
	{
		// render everything pending
		VBO_EndFrame ();

		// reset the buffer (only exists with vbos)
		if (d3d_GlobalCaps.supportVertexBuffers)
		{
			d3d_Device->SetStreamSource (0, vboState.vbo, 0, vertexstride);
			d3d_Device->SetIndices (vboState.ibo);
			d3d_RenderDef.numsss++;
		}

		// update correct stride
		vboState.stride = vertexstride;
	}

	if (vboState.numverts + numverts >= D3D_MAX_VB_SIZE || vboState.numindexes + numindexes >= D3D_MAX_IB_SIZE || vboState.numstatedefs >= MAX_STATEDEF)
		VBO_EndFrame ();

	if (!vboState.locked)
	{
		if (d3d_GlobalCaps.supportVertexBuffers)
		{
			// locking the entire buffer each time even if we're not using all of it is faster than only locking portions with D3DLOCK_NOOVERWRITE
			vboState.vbo->Lock (0, 0, (void **) &vboState.vbodata, D3DLOCK_DISCARD);
			vboState.ibo->Lock (0, 0, (void **) &vboState.ibodata, D3DLOCK_DISCARD);
		}
		else
		{
			vboState.vbodata = vboState.vbodataup;
			vboState.ibodata = vboState.ibodataup;
		}

		vboState.locked = true;

		vboState.numverts = 0;
		vboState.numindexes = 0;

		d3d_RenderDef.numlocks++;
	}
}


__inline void VBO_EndBatchSet (unsigned short *indexes, int numindexes, int numverts)
{
	// add these first because we're going to change the value of numindexes below
	vboState.numindexes += numindexes;
	vboState.currstatedef->numindexes += numindexes;

	if (indexes)
	{
		// fast-transfer the indexes
		while (numindexes > 15)
		{
			vboState.ibodata[0] = indexes[0] + vboState.numverts;
			vboState.ibodata[1] = indexes[1] + vboState.numverts;
			vboState.ibodata[2] = indexes[2] + vboState.numverts;
			vboState.ibodata[3] = indexes[3] + vboState.numverts;
			vboState.ibodata[4] = indexes[4] + vboState.numverts;
			vboState.ibodata[5] = indexes[5] + vboState.numverts;
			vboState.ibodata[6] = indexes[6] + vboState.numverts;
			vboState.ibodata[7] = indexes[7] + vboState.numverts;
			vboState.ibodata[8] = indexes[8] + vboState.numverts;
			vboState.ibodata[9] = indexes[9] + vboState.numverts;
			vboState.ibodata[10] = indexes[10] + vboState.numverts;
			vboState.ibodata[11] = indexes[11] + vboState.numverts;
			vboState.ibodata[12] = indexes[12] + vboState.numverts;
			vboState.ibodata[13] = indexes[13] + vboState.numverts;
			vboState.ibodata[14] = indexes[14] + vboState.numverts;
			vboState.ibodata[15] = indexes[15] + vboState.numverts;

			vboState.ibodata += 16;
			indexes += 16;
			numindexes -= 16;
		}

		while (numindexes > 7)
		{
			vboState.ibodata[0] = indexes[0] + vboState.numverts;
			vboState.ibodata[1] = indexes[1] + vboState.numverts;
			vboState.ibodata[2] = indexes[2] + vboState.numverts;
			vboState.ibodata[3] = indexes[3] + vboState.numverts;
			vboState.ibodata[4] = indexes[4] + vboState.numverts;
			vboState.ibodata[5] = indexes[5] + vboState.numverts;
			vboState.ibodata[6] = indexes[6] + vboState.numverts;
			vboState.ibodata[7] = indexes[7] + vboState.numverts;

			vboState.ibodata += 8;
			indexes += 8;
			numindexes -= 8;
		}

		while (numindexes > 3)
		{
			vboState.ibodata[0] = indexes[0] + vboState.numverts;
			vboState.ibodata[1] = indexes[1] + vboState.numverts;
			vboState.ibodata[2] = indexes[2] + vboState.numverts;
			vboState.ibodata[3] = indexes[3] + vboState.numverts;

			vboState.ibodata += 4;
			indexes += 4;
			numindexes -= 4;
		}

		for (int i = 0; i < numindexes; i++)
			vboState.ibodata[i] = indexes[i] + vboState.numverts;

		vboState.ibodata += numindexes;
	}

	// add these last because index offsets depend on the value of vboState.numverts
	vboState.numverts += numverts;
	vboState.currstatedef->numverts += numverts;
}


void VBO_AddSolidSurf (msurface_t *surf, entity_t *ent)
{
	VBO_BeginBatchSet (surf->numverts, surf->numindexes, sizeof (brushpolyvert_t));

	brushpolyvert_t *src = surf->verts;
	brushpolyvert_t *dst = (brushpolyvert_t *) vboState.vbodata;

	if (ent)
	{
		// rebuild the verts
		D3DMATRIX *m = &ent->matrix;
		brushhdr_t *hdr = ent->model->brushhdr;

		if (surf->rotated)
		{
			for (int i = 0; i < surf->numverts; i++)
			{
				float *vec;
				int lindex = hdr->surfedges[surf->firstedge + i];

				if (lindex > 0)
					vec = hdr->vertexes[hdr->edges[lindex].v[0]].position;
				else vec = hdr->vertexes[hdr->edges[-lindex].v[1]].position;

				surf->verts[i].xyz[0] = vec[0] * m->_11 + vec[1] * m->_21 + vec[2] * m->_31 + m->_41;
				surf->verts[i].xyz[1] = vec[0] * m->_12 + vec[1] * m->_22 + vec[2] * m->_32 + m->_42;
				surf->verts[i].xyz[2] = vec[0] * m->_13 + vec[1] * m->_23 + vec[2] * m->_33 + m->_43;
			}
		}
		else
		{
			for (int i = 0; i < surf->numverts; i++)
			{
				float *vec;
				int lindex = hdr->surfedges[surf->firstedge + i];

				if (lindex > 0)
					vec = hdr->vertexes[hdr->edges[lindex].v[0]].position;
				else vec = hdr->vertexes[hdr->edges[-lindex].v[1]].position;

				surf->verts[i].xyz[0] = vec[0] + m->_41;
				surf->verts[i].xyz[1] = vec[1] + m->_42;
				surf->verts[i].xyz[2] = vec[2] + m->_43;
			}
		}
	}

	// fast-transfer the surface verts
	Q_MemCpy (dst, src, surf->numverts * sizeof (brushpolyvert_t));
	dst += surf->numverts;

	vboState.vbodata = (byte *) dst;

	VBO_EndBatchSet (surf->indexes, surf->numindexes, surf->numverts);
}


void VBO_Add2DQuad (quaddef_textured_t *q, bool rotate)
{
	VBO_BeginBatchSet (4, 6, sizeof (aliaspolyvert_t));

	aliaspolyvert_t *dst = (aliaspolyvert_t *) vboState.vbodata;

	// correctly map texels to pixels - http://msdn.microsoft.com/en-us/library/bb219690%28VS.85%29.aspx
	dst[0].xyz[0] = q->x - 0.5f;
	dst[0].xyz[1] = q->y - 0.5f;
	dst[0].xyz[2] = 0;

	dst[0].color = q->c;

	if (rotate)
	{
		dst[0].st[0] = q->l;
		dst[0].st[1] = q->b;
	}
	else
	{
		dst[0].st[0] = q->l;
		dst[0].st[1] = q->t;
	}

	dst[1].xyz[0] = q->x + q->w - 0.5f;
	dst[1].xyz[1] = q->y - 0.5f;
	dst[1].xyz[2] = 0;

	dst[1].color = q->c;

	if (rotate)
	{
		dst[1].st[0] = q->l;
		dst[1].st[1] = q->t;
	}
	else
	{
		dst[1].st[0] = q->r;
		dst[1].st[1] = q->t;
	}

	dst[2].xyz[0] = q->x + q->w - 0.5f;
	dst[2].xyz[1] = q->y + q->h - 0.5f;
	dst[2].xyz[2] = 0;

	dst[2].color = q->c;

	if (rotate)
	{
		dst[2].st[0] = q->r;
		dst[2].st[1] = q->t;
	}
	else
	{
		dst[2].st[0] = q->r;
		dst[2].st[1] = q->b;
	}

	dst[3].xyz[0] = q->x - 0.5f;
	dst[3].xyz[1] = q->y + q->h - 0.5f;
	dst[3].xyz[2] = 0;

	dst[3].color = q->c;

	if (rotate)
	{
		dst[3].st[0] = q->r;
		dst[3].st[1] = q->b;
	}
	else
	{
		dst[3].st[0] = q->l;
		dst[3].st[1] = q->b;
	}

	vboState.vbodata = (unsigned char *) (dst + 4);

	unsigned short indexes[] = {0, 1, 2, 0, 2, 3};

	VBO_EndBatchSet (indexes, 6, 4);
}


void VBO_Add2DQuad (quaddef_coloured_t *q)
{
	VBO_BeginBatchSet (4, 6, sizeof (colouredvert_t));

	colouredvert_t *dst = (colouredvert_t *) vboState.vbodata;

	// no texels here but apply the same offset as both types can be on-screen together and will need to line up right
	dst[0].xyz[0] = q->x - 0.5f;
	dst[0].xyz[1] = q->y - 0.5f;
	dst[0].xyz[2] = 0;

	dst[0].color = q->c;

	dst[1].xyz[0] = q->x + q->w - 0.5f;
	dst[1].xyz[1] = q->y - 0.5f;
	dst[1].xyz[2] = 0;

	dst[1].color = q->c;

	dst[2].xyz[0] = q->x + q->w - 0.5f;
	dst[2].xyz[1] = q->y + q->h - 0.5f;
	dst[2].xyz[2] = 0;

	dst[2].color = q->c;

	dst[3].xyz[0] = q->x - 0.5f;
	dst[3].xyz[1] = q->y + q->h - 0.5f;
	dst[3].xyz[2] = 0;

	dst[3].color = q->c;

	vboState.vbodata = (unsigned char *) (dst + 4);

	unsigned short indexes[] = {0, 1, 2, 0, 2, 3};

	VBO_EndBatchSet (indexes, 6, 4);
}


void VBO_AddSky (msurface_t *surf, entity_t *ent)
{
	VBO_BeginBatchSet (surf->numverts, (surf->numverts - 2) * 3, sizeof (float) * 3);

	brushpolyvert_t *src = surf->verts;
	float *dst = (float *) vboState.vbodata;

	if (ent)
	{
		D3DMATRIX *m = &ent->matrix;

		if (surf->rotated)
		{
			for (int i = 0; i < surf->numverts; i++, src++, dst += 3)
			{
				dst[0] = src->xyz[0] * m->_11 + src->xyz[1] * m->_21 + src->xyz[2] * m->_31 + m->_41;
				dst[1] = src->xyz[0] * m->_12 + src->xyz[1] * m->_22 + src->xyz[2] * m->_32 + m->_42;
				dst[2] = src->xyz[0] * m->_13 + src->xyz[1] * m->_23 + src->xyz[2] * m->_33 + m->_43;
			}
		}
		else
		{
			for (int i = 0; i < surf->numverts; i++, src++, dst += 3)
			{
				dst[0] = src->xyz[0] + m->_41;
				dst[1] = src->xyz[1] + m->_42;
				dst[2] = src->xyz[2] + m->_43;
			}
		}
	}
	else
	{
		for (int i = 0; i < surf->numverts; i++, src++, dst += 3)
		{
			dst[0] = src->xyz[0];
			dst[1] = src->xyz[1];
			dst[2] = src->xyz[2];
		}
	}

	vboState.vbodata = (byte *) dst;

	VBO_EndBatchSet (surf->indexes, surf->numindexes, surf->numverts);
}


void VBO_AddWarpSurf (msurface_t *surf, entity_t *ent)
{
	if (!surf->numverts) return;

	VBO_BeginBatchSet (surf->numverts, surf->numindexes, sizeof (warpverts_t));

	brushpolyvert_t *src = surf->verts;
	warpverts_t *dst = (warpverts_t *) vboState.vbodata;

	for (int i = 0; i < surf->numverts; i++, src++, dst++)
	{
		if (ent)
		{
			D3DMATRIX *m = &ent->matrix;

			if (surf->rotated)
			{
				dst->v[0] = src->xyz[0] * m->_11 + src->xyz[1] * m->_21 + src->xyz[2] * m->_31 + m->_41;
				dst->v[1] = src->xyz[0] * m->_12 + src->xyz[1] * m->_22 + src->xyz[2] * m->_32 + m->_42;
				dst->v[2] = src->xyz[0] * m->_13 + src->xyz[1] * m->_23 + src->xyz[2] * m->_33 + m->_43;
			}
			else
			{
				dst->v[0] = src->xyz[0] + m->_41;
				dst->v[1] = src->xyz[1] + m->_42;
				dst->v[2] = src->xyz[2] + m->_43;
			}
		}
		else
		{
			dst->v[0] = src->xyz[0];
			dst->v[1] = src->xyz[1];
			dst->v[2] = src->xyz[2];
		}

		dst->st[0] = src->st[0];
		dst->st[1] = src->st[1];
	}

	vboState.vbodata = (byte *) dst;

	VBO_EndBatchSet (surf->indexes, surf->numindexes, surf->numverts);
}


__inline void D3DAlias_LerpVert (aliaspolyvert_t *dest, aliasmesh_t *av, entity_t *e, drawvertx_t *lastverts, drawvertx_t *currverts, D3DMATRIX *m = NULL)
{
	aliasstate_t *aliasstate = &e->aliasstate;

	lastverts += av->vertindex;
	currverts += av->vertindex;

	float vert[3];

	if (lastverts->lerpvert)
	{
		vert[0] = (float) lastverts->v[0] * aliasstate->lastlerp + (float) currverts->v[0] * aliasstate->currlerp;
		vert[1] = (float) lastverts->v[1] * aliasstate->lastlerp + (float) currverts->v[1] * aliasstate->currlerp;
		vert[2] = (float) lastverts->v[2] * aliasstate->lastlerp + (float) currverts->v[2] * aliasstate->currlerp;
	}
	else
	{
		vert[0] = currverts->v[0];
		vert[1] = currverts->v[1];
		vert[2] = currverts->v[2];
	}

	if (m)
	{
		if (e->rotated)
		{
			dest->xyz[0] = vert[0] * m->_11 + vert[1] * m->_21 + vert[2] * m->_31 + m->_41;
			dest->xyz[1] = vert[0] * m->_12 + vert[1] * m->_22 + vert[2] * m->_32 + m->_42;
			dest->xyz[2] = vert[0] * m->_13 + vert[1] * m->_23 + vert[2] * m->_33 + m->_43;
		}
		else
		{
			// alias models also need to be scaled
			dest->xyz[0] = vert[0] * m->_11 + m->_41;
			dest->xyz[1] = vert[1] * m->_22 + m->_42;
			dest->xyz[2] = vert[2] * m->_33 + m->_43;
		}
	}
	else
	{
		dest->xyz[0] = vert[0];
		dest->xyz[1] = vert[1];
		dest->xyz[2] = vert[2];
	}

	float l;

	// pre-interpolated shadedots
	if (lastverts->lerpvert)
		l = (aliasstate->shadedots[lastverts->lightnormalindex] * aliasstate->lastlerp + aliasstate->shadedots[currverts->lightnormalindex] * aliasstate->currlerp);
	else l = aliasstate->shadedots[currverts->lightnormalindex];

	// set light color
	dest->color = D3DCOLOR_ARGB
	(
		BYTE_CLAMP (e->alphaval),
		vid.lightmap[BYTE_CLAMP (l * e->shadelight[0])],
		vid.lightmap[BYTE_CLAMP (l * e->shadelight[1])],
		vid.lightmap[BYTE_CLAMP (l * e->shadelight[2])]
	);

	// do texcoords here too
	dest->st[0] = av->s;
	dest->st[1] = av->t;
}


void VBO_AddAliasPart (entity_t *ent, aliaspart_t *part)
{
	aliashdr_t *hdr = ent->model->aliashdr;
	aliasstate_t *aliasstate = &ent->aliasstate;

	VBO_BeginBatchSet (part->nummesh, part->numindexes, sizeof (aliaspolyvert_t));

	aliasmesh_t *src = part->meshverts;
	aliaspolyvert_t *dst = (aliaspolyvert_t *) vboState.vbodata;

	for (int i = 0; i < part->nummesh; i++, src++, dst++)
		D3DAlias_LerpVert (dst, src, ent, hdr->vertexes[ent->lastpose], hdr->vertexes[ent->currpose], &ent->matrix);

	vboState.vbodata = (byte *) dst;

	VBO_EndBatchSet (part->indexes, part->numindexes, part->nummesh);
}


void VBO_AddAliasShadow (entity_t *ent, aliashdr_t *hdr, aliaspart_t *part, aliasstate_t *aliasstate, DWORD shadecolor)
{
	VBO_BeginBatchSet (part->nummesh, part->numindexes, sizeof (aliaspolyvert_t));

	aliasmesh_t *src = part->meshverts;
	aliaspolyvert_t *dst = (aliaspolyvert_t *) vboState.vbodata;

	for (int i = 0; i < part->nummesh; i++, src++, dst++)
	{
		D3DAlias_LerpVert (dst, src, ent, hdr->vertexes[ent->lastpose], hdr->vertexes[ent->currpose], &ent->matrix);
		dst->xyz[2] = aliasstate->lightspot[2] + 0.1f;
		dst->color = shadecolor;
	}

	vboState.vbodata = (byte *) dst;

	VBO_EndBatchSet (part->indexes, part->numindexes, part->nummesh);
}


void VBO_AddBBox (float *origin, float *mins, float *maxs, float expand)
{
	VBO_BeginBatchSet (8, 36, sizeof (vec3_t));

	vec3_t *bboxverts = (vec3_t *) vboState.vbodata;
	vboState.vbodata += 8 * sizeof (vec3_t);

	unsigned short bboxindexes[36] =
	{
		0, 2, 6, 0, 6, 4,
		1, 3, 7, 1, 7, 5,
		0, 1, 3, 0, 3, 2,
		4, 5, 7, 4, 7, 6,
		0, 1, 5, 0, 5, 4,
		2, 3, 7, 2, 7, 6
	};

	// expand the bbox so that entities don't occlude against themselves
	// this also help with situations where an entity is just coming round a corner
	bboxverts[0][0] = origin[0] + mins[0] - expand;
	bboxverts[0][1] = origin[1] + mins[1] - expand;
	bboxverts[0][2] = origin[2] + mins[2] - expand;

	bboxverts[1][0] = origin[0] + mins[0] - expand;
	bboxverts[1][1] = origin[1] + mins[1] - expand;
	bboxverts[1][2] = origin[2] + maxs[2] + expand;

	bboxverts[2][0] = origin[0] + mins[0] - expand;
	bboxverts[2][1] = origin[1] + maxs[1] + expand;
	bboxverts[2][2] = origin[2] + mins[2] - expand;

	bboxverts[3][0] = origin[0] + mins[0] - expand;
	bboxverts[3][1] = origin[1] + maxs[1] + expand;
	bboxverts[3][2] = origin[2] + maxs[2] + expand;

	bboxverts[4][0] = origin[0] + maxs[0] + expand;
	bboxverts[4][1] = origin[1] + mins[1] - expand;
	bboxverts[4][2] = origin[2] + mins[2] - expand;

	bboxverts[5][0] = origin[0] + maxs[0] + expand;
	bboxverts[5][1] = origin[1] + mins[1] - expand;
	bboxverts[5][2] = origin[2] + maxs[2] + expand;

	bboxverts[6][0] = origin[0] + maxs[0] + expand;
	bboxverts[6][1] = origin[1] + maxs[1] + expand;
	bboxverts[6][2] = origin[2] + mins[2] - expand;

	bboxverts[7][0] = origin[0] + maxs[0] + expand;
	bboxverts[7][1] = origin[1] + maxs[1] + expand;
	bboxverts[7][2] = origin[2] + maxs[2] + expand;

	d3d_RenderDef.alias_polys++;

	VBO_EndBatchSet (bboxindexes, 36, 8);
}


void VBO_AddParticle (float *origin, float scale, float *up, float *right, DWORD color)
{
	VBO_BeginBatchSet (4, 6, sizeof (aliaspolyvert_t));

	// fixme - rescale all particle scales for this
	scale *= 0.5f;

	// take a copy out so that we can avoid having to read back from the buffer
	vec3_t origin2;
	VectorMA (origin, scale, up, origin2);

	aliaspolyvert_t dst[4];

	dst[0].xyz[0] = origin[0];
	dst[0].xyz[1] = origin[1];
	dst[0].xyz[2] = origin[2];
	dst[0].color = color;
	dst[0].st[0] = 0;
	dst[0].st[1] = 0;

	VectorCopy (origin2, dst[1].xyz);
	dst[1].color = color;
	dst[1].st[0] = 1;
	dst[1].st[1] = 0;

	VectorMA (origin2, scale, right, dst[2].xyz);
	dst[2].color = color;
	dst[2].st[0] = 1;
	dst[2].st[1] = 1;

	VectorMA (origin, scale, right, dst[3].xyz);
	dst[3].color = color;
	dst[3].st[0] = 0;
	dst[3].st[1] = 1;

	Q_MemCpy (vboState.vbodata, dst, sizeof (aliaspolyvert_t) * 4);
	vboState.vbodata += (sizeof (aliaspolyvert_t) * 4);

	unsigned short indexes[] = {0, 1, 2, 0, 2, 3};

	VBO_EndBatchSet (indexes, 6, 4);
}


void VBO_AddSprite (mspriteframe_t *frame, float *origin, float *up, float *right, int alpha)
{
	float point[3];

	VBO_BeginBatchSet (4, 6, sizeof (aliaspolyvert_t));

	aliaspolyvert_t *dst = (aliaspolyvert_t *) vboState.vbodata;

	VectorMA (origin, frame->down, up, point);
	VectorMA (point, frame->left, right, point);

	dst[0].xyz[0] = point[0];
	dst[0].xyz[1] = point[1];
	dst[0].xyz[2] = point[2];

	dst[0].color = D3DCOLOR_ARGB (BYTE_CLAMP (alpha), 255, 255, 255);

	dst[0].st[0] = 0;
	dst[0].st[1] = frame->t;

	VectorMA (origin, frame->up, up, point);
	VectorMA (point, frame->left, right, point);

	dst[1].xyz[0] = point[0];
	dst[1].xyz[1] = point[1];
	dst[1].xyz[2] = point[2];

	dst[1].color = D3DCOLOR_ARGB (BYTE_CLAMP (alpha), 255, 255, 255);

	dst[1].st[0] = 0;
	dst[1].st[1] = 0;

	VectorMA (origin, frame->up, up, point);
	VectorMA (point, frame->right, right, point);

	dst[2].xyz[0] = point[0];
	dst[2].xyz[1] = point[1];
	dst[2].xyz[2] = point[2];

	dst[2].color = D3DCOLOR_ARGB (BYTE_CLAMP (alpha), 255, 255, 255);

	dst[2].st[0] = frame->s;
	dst[2].st[1] = 0;

	VectorMA (origin, frame->down, up, point);
	VectorMA (point, frame->right, right, point);

	dst[3].xyz[0] = point[0];
	dst[3].xyz[1] = point[1];
	dst[3].xyz[2] = point[2];

	dst[3].color = D3DCOLOR_ARGB (BYTE_CLAMP (alpha), 255, 255, 255);

	dst[3].st[0] = frame->s;
	dst[3].st[1] = frame->t;

	vboState.vbodata = (unsigned char *) (dst + 4);

	unsigned short indexes[] = {0, 1, 2, 0, 2, 3};

	VBO_EndBatchSet (indexes, 6, 4);
}


void VBO_AddSkySphere (warpverts_t *verts, int numverts, unsigned short *indexes, int numindexes)
{
	VBO_BeginBatchSet (numverts, numindexes, sizeof (warpverts_t));

	warpverts_t *dst = (warpverts_t *) vboState.vbodata;

	for (int i = 0; i < numverts; i++, verts++, dst++)
	{
		dst->v[0] = verts->v[0];
		dst->v[1] = verts->v[1];
		dst->v[2] = verts->v[2];

		dst->st[0] = verts->st[0];
		dst->st[1] = verts->st[1];
	}

	vboState.vbodata = (byte *) dst;

	VBO_EndBatchSet (indexes, numindexes, numverts);
}


void VBO_RTTBlendScreen (D3DCOLOR blend)
{
	VBO_BeginBatchSet (4, 6, sizeof (aliaspolyvert_t));

	// bonus flash/pickup/damage screen
	aliaspolyvert_t *verts = (aliaspolyvert_t *) vboState.vbodata;
	vboState.vbodata = (byte *) (verts + 4);

	unsigned short indexes[] = {0, 1, 2, 0, 2, 3};
	float hscale = (float) (vid.height - sb_lines) / (float) vid.height;

	verts[0].xyz[0] = 0.0f;
	verts[0].xyz[1] = 0.0f;
	verts[0].xyz[2] = 0;

	verts[0].color = blend;

	verts[0].st[0] = 0;
	verts[0].st[1] = 0;

	verts[1].xyz[0] = (float) d3d_CurrentMode.Width;
	verts[1].xyz[1] = 0.0f;
	verts[1].xyz[2] = 0;

	verts[1].color = blend;

	verts[1].st[0] = 1;
	verts[1].st[1] = 0;

	verts[2].xyz[0] = (float) d3d_CurrentMode.Width;
	verts[2].xyz[1] = ((float) d3d_CurrentMode.Height * hscale);
	verts[2].xyz[2] = 0;

	verts[2].color = blend;

	verts[2].st[0] = 1;
	verts[2].st[1] = hscale;

	verts[3].xyz[0] = 0.0f;
	verts[3].xyz[1] = ((float) d3d_CurrentMode.Height * hscale);
	verts[3].xyz[2] = 0;

	verts[3].color = blend;

	verts[3].st[0] = 0;
	verts[3].st[1] = hscale;

	VBO_EndBatchSet (indexes, 6, 4);
}


void VBO_RTTWarpScreen (int tess, float rdt, D3DCOLOR blend)
{
	/*
	why is this not in a shader?  surely it would be higher quality (and possibly run faster) if it was?
	true, but I also wouldn't be able to *not* warp the edge vertexes which would cause distortion at
	the edges and look ugly.  sometimes a shader ain't the answer to everything, baby!
	*/
	int numverts = (tess + 1) * (tess + 1);
	int numindexes = tess * tess * 6;
	extern cvar_t r_waterwarpscale;

	VBO_BeginBatchSet (numverts, numindexes, sizeof (aliaspolyvert_t));

	aliaspolyvert_t *dst = (aliaspolyvert_t *) vboState.vbodata;
	float hscale = (float) (vid.height - sb_lines) / (float) vid.height;

	float textessw = (float) tess;
	float textessh = (float) tess * hscale;
	float invtess = 1.0f / tess;
	float scalefactor = r_waterwarpscale.value * ((float) tess) / 32.0f;

	// because the mode can change we run the tesselation each frame
	// the texcoords and colour also need to change anyway so overhead is minimal
	// the tesselation factor is now user-controllable too. ;)
	for (int y = 0; y <= tess; y++)
	{
		for (int x = 0; x <= tess; x++, dst++)
		{
			dst->xyz[0] = (float) (x * d3d_CurrentMode.Width / tess);
			dst->xyz[1] = (float) ((y * d3d_CurrentMode.Height / tess) * hscale);
			dst->xyz[2] = 0;

			dst->color = blend;

			float s = invtess * x * textessw;
			float t = (invtess * y) * textessh;

			if (x == 0 || x == tess)
				dst->st[0] = (float) dst->xyz[0] / (float) d3d_CurrentMode.Width;
			else dst->st[0] = (s + sin (t + rdt) * scalefactor) * invtess;

			if (y == 0 || y == tess)
				dst->st[1] = (float) dst->xyz[1] / (float) d3d_CurrentMode.Height;
			else dst->st[1] = (t + sin (s + rdt) * scalefactor) * invtess;
		}
	}

	vboState.vbodata = (unsigned char *) dst;

	// need to set up the indexes directly as they don't come from a pre-existing buffer
	// but we also need a valid index pointer for accumulating into the buffer, so we just cheat.
	unsigned short *indexes = vboState.ibodata;

	// triangle strip weenies can go to hell
	for (int y = 0, ndx = 0, i = 0; y < tess; y++, i++)
	{
		for (int x = 0; x < tess; x++, i++, ndx += 6)
		{
			indexes[ndx + 0] = i;
			indexes[ndx + 1] = i + 1;
			indexes[ndx + 2] = i + tess + 2;
			indexes[ndx + 3] = i;
			indexes[ndx + 4] = i + tess + 2;
			indexes[ndx + 5] = i + tess + 1;
		}
	}

	VBO_EndBatchSet (indexes, numindexes, numverts);
}

