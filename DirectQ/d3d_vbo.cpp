
/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
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

#define D3D_MAX_VBO_SIZE 65534


#define D3D_VBO_VERT_SIZE	32
#define D3D_MAX_VBO_SHADERS	128

typedef struct d3d_vbo_shader_texture_s
{
	LPDIRECT3DTEXTURE9 Tex;

	DWORD ColorOp;
	DWORD ColorArg1;
	DWORD ColorArg2;

	DWORD AlphaOp;
	DWORD AlphaArg1;
	DWORD AlphaArg2;
} d3d_vbo_shader_texture_t;

typedef struct d3d_vbo_effect_s
{
	xcommand_t SetupCallback;
	xcommand_t StateChangeCallback;
	xcommand_t ShutdownCallback;
} d3d_vbo_effect_t;

#define SHADER_TYPE_FIXED	1
#define SHADER_TYPE_HLSL	2

typedef struct d3d_vbo_shader_s
{
	union
	{
		d3d_vbo_effect_t Effect;
		d3d_vbo_shader_texture_t Stages[3];
	};

	int FirstVert;
	int FirstIndex;
	int NumVerts;
	int NumIndexes;
	int Type;
} d3d_vbo_shader_t;


typedef struct d3d_vbo_state_s
{
	LPDIRECT3DVERTEXBUFFER9 VBO;
	LPDIRECT3DINDEXBUFFER9 IBO;

	unsigned char *VBOData;
	unsigned short *IBOData;

	bool VBOLocked;
	bool IBOLocked;

	int VBOLockSize;
	int IBOLockSize;

	int NumVerts;
	int NumIndexes;

	int NumShaders;
	d3d_vbo_shader_t *Shaders;
	d3d_vbo_shader_t *CurrShader;

	D3DPRIMITIVETYPE PrimitiveType;
	int Stride;
} d3d_vbo_state_t;

d3d_vbo_state_t d3d_VBOState;


void D3D_VBOReleaseBuffers (void)
{
	SAFE_RELEASE (d3d_VBOState.VBO);
	SAFE_RELEASE (d3d_VBOState.IBO);
}


void D3D_VBOSetVBOStream (LPDIRECT3DVERTEXBUFFER9 vbo, LPDIRECT3DINDEXBUFFER9 ibo, int stride)
{
	// these are to force an update the first time
	static int oldvbo = -1;
	static int oldibo = -1;
	static int oldstride = -1;

	if ((int) vbo != oldvbo || stride != oldstride)
	{
		if (vbo)
		{
			d3d_Device->SetStreamSource (0, vbo, 0, stride);
			d3d_RenderDef.numsss++;
		}

		oldvbo = (int) vbo;
		oldstride = stride;
	}

	if ((int) ibo != oldibo)
	{
		if (ibo)
			d3d_Device->SetIndices (ibo);
		else d3d_Device->SetIndices (NULL);

		oldibo = (int) ibo;
	}
}


void D3D_DrawUserPrimitive (D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount, CONST void *pVertexStreamZeroData, UINT VertexStreamZeroStride)
{
	D3D_VBOSetVBOStream (NULL);
	d3d_Device->DrawPrimitiveUP (PrimitiveType, PrimitiveCount, pVertexStreamZeroData, VertexStreamZeroStride);
}


void D3D_DrawUserPrimitive (D3DPRIMITIVETYPE PrimitiveType, UINT NumVertices, UINT PrimitiveCount, CONST void *pIndexData, CONST void *pVertexStreamZeroData, UINT VertexStreamZeroStride)
{
	D3D_VBOSetVBOStream (NULL);
	d3d_Device->DrawIndexedPrimitiveUP (PrimitiveType, 0, NumVertices, PrimitiveCount, pIndexData, D3DFMT_INDEX16, pVertexStreamZeroData, VertexStreamZeroStride);
}


void D3D_VBOBegin (D3DPRIMITIVETYPE PrimitiveType, int Stride)
{
	if (!d3d_VBOState.Shaders)
		d3d_VBOState.Shaders = (d3d_vbo_shader_t *) Zone_Alloc (D3D_MAX_VBO_SHADERS * sizeof (d3d_vbo_shader_t));

	do
	{
		if (!d3d_VBOState.VBO)
		{
			hr = d3d_Device->CreateVertexBuffer
			(
				D3D_VBO_VERT_SIZE * D3D_MAX_VBO_SIZE,
				D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY,
				0,
				D3DPOOL_DEFAULT,
				&d3d_VBOState.VBO,
				NULL
			);

			if (FAILED (hr))
			{
				Sys_Error ("D3D_VBOBegin: failed to create a vertex buffer");
				return;
			}
		}

		hr = d3d_VBOState.VBO->Lock (0, 0, (void **) &d3d_VBOState.VBOData, D3DLOCK_DISCARD);
		d3d_VBOState.VBOLockSize = 0;

		if (FAILED (hr))
		{
			SAFE_RELEASE (d3d_VBOState.VBO);
			continue;
		}
	} while (false);

	do
	{
		if (!d3d_VBOState.IBO)
		{
			hr = d3d_Device->CreateIndexBuffer
			(
				sizeof (unsigned short) * D3D_MAX_VBO_SIZE,
				D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY,
				D3DFMT_INDEX16,
				D3DPOOL_DEFAULT,
				&d3d_VBOState.IBO,
				NULL
			);

			if (FAILED (hr))
			{
				Sys_Error ("D3D_VBOCreateOnDemand: failed to create an index buffer");
				return;
			}
		}

		hr = d3d_VBOState.IBO->Lock (0, 0, (void **) &d3d_VBOState.IBOData, D3DLOCK_DISCARD);
		d3d_VBOState.IBOLockSize = 0;

		if (FAILED (hr))
		{
			SAFE_RELEASE (d3d_VBOState.IBO);
			continue;
		}
	} while (false);

	d3d_VBOState.VBOLocked = true;
	d3d_VBOState.IBOLocked = true;

	d3d_VBOState.NumVerts = 0;
	d3d_VBOState.NumIndexes = 0;
	d3d_VBOState.NumShaders = 0;

	d3d_VBOState.PrimitiveType = PrimitiveType;
	d3d_VBOState.Stride = Stride;
	d3d_VBOState.CurrShader = NULL;
}


static void D3D_VBOUnlockBuffers (void)
{
	HRESULT hrVBO = S_OK;
	HRESULT hrIBO = S_OK;

	if (d3d_VBOState.VBOLocked)
	{
		hrVBO = d3d_VBOState.VBO->Unlock ();
		d3d_VBOState.VBOLocked = false;
	}

	if (d3d_VBOState.IBOLocked)
	{
		hrIBO = d3d_VBOState.IBO->Unlock ();
		d3d_VBOState.IBOLocked = false;
	}

	// explicitly NULL to catch invalid pointer references
	d3d_VBOState.VBOData = NULL;
	d3d_VBOState.IBOData = NULL;

	if (FAILED (hrVBO) || FAILED (hrIBO))
	{
		// hack to prevent rendering
		d3d_VBOState.NumVerts = 0;
	}
}


void D3D_GetVertexBufferSpace (void **data, int locksize)
{
	// create on demand
	do
	{
		if (!d3d_VBOState.VBO)
		{
			hr = d3d_Device->CreateVertexBuffer
			(
				D3D_VBO_VERT_SIZE * D3D_MAX_VBO_SIZE,
				D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY,
				0,
				D3DPOOL_DEFAULT,
				&d3d_VBOState.VBO,
				NULL
			);

			if (FAILED (hr))
			{
				Sys_Error ("D3D_VBOBegin: failed to create a vertex buffer");
				return;
			}
		}

		hr = d3d_VBOState.VBO->Lock (0, locksize, data, D3DLOCK_DISCARD);
		d3d_VBOState.VBOLockSize = locksize;

		if (FAILED (hr))
		{
			SAFE_RELEASE (d3d_VBOState.VBO);
			continue;
		}
	} while (false);

	d3d_VBOState.VBOLocked = true;
}


void D3D_GetIndexBufferSpace (void **data, int locksize)
{
	// create on demand
	do
	{
		if (!d3d_VBOState.IBO)
		{
			hr = d3d_Device->CreateIndexBuffer
			(
				sizeof (unsigned short) * D3D_MAX_VBO_SIZE,
				D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY,
				D3DFMT_INDEX16,
				D3DPOOL_DEFAULT,
				&d3d_VBOState.IBO,
				NULL
			);

			if (FAILED (hr))
			{
				Sys_Error ("D3D_VBOCreateOnDemand: failed to create an index buffer");
				return;
			}
		}

		hr = d3d_VBOState.IBO->Lock (0, locksize, data, D3DLOCK_DISCARD);
		d3d_VBOState.IBOLockSize = locksize;

		if (FAILED (hr))
		{
			SAFE_RELEASE (d3d_VBOState.IBO);
			continue;
		}
	} while (false);

	d3d_VBOState.IBOLocked = true;
}


void D3D_SubmitVertexes (int numverts, int numindexes, int polysize)
{
	// always unlock!
	D3D_VBOUnlockBuffers ();

	if (numverts && numindexes)
	{
		D3D_VBOSetVBOStream (d3d_VBOState.VBO, d3d_VBOState.IBO, polysize);
		d3d_Device->DrawIndexedPrimitive (D3DPT_TRIANGLELIST, 0, 0, numverts, 0, (numindexes / 3));
	}
	else if (numverts)
	{
		D3D_VBOSetVBOStream (d3d_VBOState.VBO, NULL, polysize);
		d3d_Device->DrawPrimitive (D3DPT_TRIANGLELIST, 0, numverts / 3);
	}

	d3d_VBOState.NumVerts = 0;
	d3d_VBOState.NumIndexes = 0;
	d3d_VBOState.NumShaders = 0;
	d3d_VBOState.CurrShader = NULL;
}


BOOL D3D_AreBuffersFull (int numverts, int numindexes)
{
	if (numverts >= D3D_MAX_VBO_SIZE) return TRUE;
	if (numindexes >= D3D_MAX_VBO_SIZE) return TRUE;

	return FALSE;
}


void D3D_VBOAddShader (xcommand_t SetupCallback, xcommand_t StateChangeCallback, xcommand_t ShutdownCallback)
{
	assert (SetupCallback);
	assert (StateChangeCallback);
	assert (ShutdownCallback);

	// always added
	d3d_VBOState.CurrShader = &d3d_VBOState.Shaders[d3d_VBOState.NumShaders];
	d3d_VBOState.NumShaders++;

	d3d_VBOState.CurrShader->Type = SHADER_TYPE_HLSL;
	d3d_VBOState.CurrShader->FirstIndex = d3d_VBOState.NumIndexes;
	d3d_VBOState.CurrShader->FirstVert = d3d_VBOState.NumVerts;
	d3d_VBOState.CurrShader->NumIndexes = 0;
	d3d_VBOState.CurrShader->NumVerts = 0;

	d3d_VBOState.CurrShader->Effect.SetupCallback = SetupCallback;
	d3d_VBOState.CurrShader->Effect.StateChangeCallback = StateChangeCallback;
	d3d_VBOState.CurrShader->Effect.ShutdownCallback = ShutdownCallback;
}


void D3D_VBOAddShader (d3d_shader_t *Shader, LPDIRECT3DTEXTURE9 Stage0Tex, LPDIRECT3DTEXTURE9 Stage1Tex, LPDIRECT3DTEXTURE9 Stage2Tex)
{
	// if there's no current shader we need to add one anyway
	if (d3d_VBOState.CurrShader)
	{
		// only add the new shader if the textures change
		if (d3d_VBOState.CurrShader->Type != SHADER_TYPE_FIXED) goto NewShader;
		if (Stage0Tex != d3d_VBOState.CurrShader->Stages[0].Tex) goto NewShader;
		if (Stage1Tex != d3d_VBOState.CurrShader->Stages[1].Tex) goto NewShader;
		if (Stage2Tex != d3d_VBOState.CurrShader->Stages[2].Tex) goto NewShader;

		// not needed
		return;
	}

NewShader:;
	d3d_VBOState.CurrShader = &d3d_VBOState.Shaders[d3d_VBOState.NumShaders];
	d3d_VBOState.NumShaders++;

	d3d_VBOState.CurrShader->Type = SHADER_TYPE_FIXED;
	d3d_VBOState.CurrShader->FirstIndex = d3d_VBOState.NumIndexes;
	d3d_VBOState.CurrShader->FirstVert = d3d_VBOState.NumVerts;
	d3d_VBOState.CurrShader->NumIndexes = 0;
	d3d_VBOState.CurrShader->NumVerts = 0;

	for (int i = 0; i < 3; i++)
	{
		d3d_VBOState.CurrShader->Stages[i].ColorOp = Shader->ColorDef[i].Op;
		d3d_VBOState.CurrShader->Stages[i].ColorArg1 = Shader->ColorDef[i].Arg1;
		d3d_VBOState.CurrShader->Stages[i].ColorArg2 = Shader->ColorDef[i].Arg2;

		d3d_VBOState.CurrShader->Stages[i].AlphaOp = Shader->AlphaDef[i].Op;
		d3d_VBOState.CurrShader->Stages[i].AlphaArg1 = Shader->AlphaDef[i].Arg1;
		d3d_VBOState.CurrShader->Stages[i].AlphaArg2 = Shader->AlphaDef[i].Arg2;
	}

	d3d_VBOState.CurrShader->Stages[0].Tex = Stage0Tex;
	d3d_VBOState.CurrShader->Stages[1].Tex = Stage1Tex;
	d3d_VBOState.CurrShader->Stages[2].Tex = Stage2Tex;
}


#define D3D_EmitSingleSurfaceFullVert(d, s) \
	D3D_EmitSingleSurfaceVert (src[s].basevert, dst[d].xyz, m); \
	D3D_EmitSingleSurfaceTexCoord (src[s].st, dst[d].st); \
	D3D_EmitSingleSurfaceTexCoord (src[s].lm, dst[d].lm);

void D3D_VBOEmitIndexes (unsigned short *src, unsigned short *dst, int num, int base)
{
	if (num == 3)
	{
		dst[0] = src[0] + base;
		dst[1] = src[1] + base;
		dst[2] = src[2] + base;
	}
	else if (num == 4)
	{
		dst[0] = src[0] + base;
		dst[1] = src[1] + base;
		dst[2] = src[2] + base;
		dst[3] = src[3] + base;
	}
	else if (!(num & 7))
	{
		for (int i = 0; i < num; i += 8, src += 8, dst += 8)
		{
			dst[0] = src[0] + base;
			dst[1] = src[1] + base;
			dst[2] = src[2] + base;
			dst[3] = src[3] + base;

			dst[4] = src[4] + base;
			dst[5] = src[5] + base;
			dst[6] = src[6] + base;
			dst[7] = src[7] + base;
		}
	}
	else if (!(num & 3))
	{
		for (int i = 0; i < num; i += 4, src += 4, dst += 4)
		{
			dst[0] = src[0] + base;
			dst[1] = src[1] + base;
			dst[2] = src[2] + base;
			dst[3] = src[3] + base;

			dst[4] = src[4] + base;
			dst[5] = src[5] + base;
			dst[6] = src[6] + base;
			dst[7] = src[7] + base;
		}
	}
	else
	{
		for (int i = 0; i < num; i++)
			dst[i] = src[i] + base;
	}
}


void D3D_VBOAddSurfaceVerts (msurface_t *surf)
{
	if (!d3d_VBOState.CurrShader) return;
	if (!surf->numverts) return;

	polyvert_t *src = surf->verts;
	brushpolyvert_t *dst = (brushpolyvert_t *) (d3d_VBOState.VBOData + d3d_VBOState.NumVerts * sizeof (brushpolyvert_t));
	D3DMATRIX *m = surf->matrix;

	if (surf->numverts == 3)
	{
		D3D_EmitSingleSurfaceFullVert (0, 0);
		D3D_EmitSingleSurfaceFullVert (1, 1);
		D3D_EmitSingleSurfaceFullVert (2, 2);
	}
	else if (surf->numverts == 4)
	{
		D3D_EmitSingleSurfaceFullVert (0, 0);
		D3D_EmitSingleSurfaceFullVert (1, 1);
		D3D_EmitSingleSurfaceFullVert (2, 2);
		D3D_EmitSingleSurfaceFullVert (3, 3);
	}
	else if (!(surf->numverts & 7))
	{
		for (int v = 0; v < surf->numverts; v += 8, src += 8, dst += 8)
		{
			D3D_EmitSingleSurfaceFullVert (0, 0);
			D3D_EmitSingleSurfaceFullVert (1, 1);
			D3D_EmitSingleSurfaceFullVert (2, 2);
			D3D_EmitSingleSurfaceFullVert (3, 3);
			D3D_EmitSingleSurfaceFullVert (4, 4);
			D3D_EmitSingleSurfaceFullVert (5, 5);
			D3D_EmitSingleSurfaceFullVert (6, 6);
			D3D_EmitSingleSurfaceFullVert (7, 7);
		}
	}
	else if (!(surf->numverts & 3))
	{
		for (int v = 0; v < surf->numverts; v += 4, src += 4, dst += 4)
		{
			D3D_EmitSingleSurfaceFullVert (0, 0);
			D3D_EmitSingleSurfaceFullVert (1, 1);
			D3D_EmitSingleSurfaceFullVert (2, 2);
			D3D_EmitSingleSurfaceFullVert (3, 3);
		}
	}
	else
	{
		for (int v = 0; v < surf->numverts; v++, src++, dst++)
		{
			D3D_EmitSingleSurfaceVert (src->basevert, dst->xyz, m);
			D3D_EmitSingleSurfaceTexCoord (src->st, dst->st);
			D3D_EmitSingleSurfaceTexCoord (src->lm, dst->lm);
		}
	}

	D3D_VBOEmitIndexes (surf->indexes, &d3d_VBOState.IBOData[d3d_VBOState.NumIndexes], surf->numindexes, d3d_VBOState.NumVerts);

	d3d_VBOState.NumVerts += surf->numverts;
	d3d_VBOState.NumIndexes += surf->numindexes;
	d3d_VBOState.CurrShader->NumVerts += surf->numverts;
	d3d_VBOState.CurrShader->NumIndexes += surf->numindexes;
}


__inline void D3D_LerpVert (aliaspolyvert_t *dest, aliasmesh_t *av, aliasstate_t *aliasstate, drawvertx_t *lastverts, drawvertx_t *currverts, D3DMATRIX *m = NULL)
{
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
		dest->xyz[0] = vert[0] * m->_11 + vert[1] * m->_21 + vert[2] * m->_31 + m->_41;
		dest->xyz[1] = vert[0] * m->_12 + vert[1] * m->_22 + vert[2] * m->_32 + m->_42;
		dest->xyz[2] = vert[0] * m->_13 + vert[1] * m->_23 + vert[2] * m->_33 + m->_43;
	}
	else
	{
		dest->xyz[0] = vert[0];
		dest->xyz[1] = vert[1];
		dest->xyz[2] = vert[2];
	}

	// do texcoords here too
	dest->st[0] = av->s;
	dest->st[1] = av->t;
}


__inline void D3D_LerpLight (aliaspolyvert_t *dest, entity_t *e, aliasmesh_t *av, aliasstate_t *aliasstate, drawvertx_t *lastverts, drawvertx_t *currverts)
{
	float l;

	lastverts += av->vertindex;
	currverts += av->vertindex;

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
}


#define EMIT_ALIAS_VERT(vd, vs) \
	{D3D_LerpLight (vd, ent, vs, aliasstate, hdr->vertexes[ent->lastpose], hdr->vertexes[ent->currpose]); \
	D3D_LerpVert (vd, vs, aliasstate, hdr->vertexes[ent->lastpose], hdr->vertexes[ent->currpose], &ent->matrix);}

void D3D_VBOAddAliasVerts (entity_t *ent, aliashdr_t *hdr, aliaspart_t *part, aliasstate_t *aliasstate)
{
	if (!d3d_VBOState.CurrShader) return;

	aliasmesh_t *src = part->meshverts;
	aliaspolyvert_t *dst = (aliaspolyvert_t *) (d3d_VBOState.VBOData + d3d_VBOState.NumVerts * sizeof (aliaspolyvert_t));

	// submit this entity to the renderer
	if (!(part->nummesh & 7))
	{
		for (int i = 0; i < part->nummesh; i += 8, src += 8, dst += 8)
		{
			EMIT_ALIAS_VERT (&dst[0], &src[0]);
			EMIT_ALIAS_VERT (&dst[1], &src[1]);
			EMIT_ALIAS_VERT (&dst[2], &src[2]);
			EMIT_ALIAS_VERT (&dst[3], &src[3]);
			EMIT_ALIAS_VERT (&dst[4], &src[4]);
			EMIT_ALIAS_VERT (&dst[5], &src[5]);
			EMIT_ALIAS_VERT (&dst[6], &src[6]);
			EMIT_ALIAS_VERT (&dst[7], &src[7]);
		}
	}
	else if (!(part->nummesh & 3))
	{
		for (int i = 0; i < part->nummesh; i += 4, src += 4, dst += 4)
		{
			EMIT_ALIAS_VERT (&dst[0], &src[0]);
			EMIT_ALIAS_VERT (&dst[1], &src[1]);
			EMIT_ALIAS_VERT (&dst[2], &src[2]);
			EMIT_ALIAS_VERT (&dst[3], &src[3]);
		}
	}
	else
	{
		for (int i = 0; i < part->nummesh; i++, src++, dst++)
			EMIT_ALIAS_VERT (dst, src);
	}

	D3D_VBOEmitIndexes (part->indexes, &d3d_VBOState.IBOData[d3d_VBOState.NumIndexes], part->numindexes, d3d_VBOState.NumVerts);

	d3d_VBOState.NumVerts += part->nummesh;
	d3d_VBOState.NumIndexes += part->numindexes;
	d3d_VBOState.CurrShader->NumVerts += part->nummesh;
	d3d_VBOState.CurrShader->NumIndexes += part->numindexes;
}


#define EMIT_SHADOW_VERT(vd, vs) \
	{D3D_LerpVert (vd, vs, aliasstate, hdr->vertexes[ent->lastpose], hdr->vertexes[ent->currpose], &ent->matrix); \
	(vd)->xyz[2] = aliasstate->lightspot[2] + 0.1f; \
	(vd)->color = shadecolor;}

void D3D_VBOAddShadowVerts (entity_t *ent, aliashdr_t *hdr, aliaspart_t *part, aliasstate_t *aliasstate, DWORD shadecolor)
{
	if (!d3d_VBOState.CurrShader) return;

	aliasmesh_t *src = part->meshverts;
	aliaspolyvert_t *dst = (aliaspolyvert_t *) (d3d_VBOState.VBOData + d3d_VBOState.NumVerts * sizeof (aliaspolyvert_t));

	// submit this entity to the renderer
	// submit this entity to the renderer
	if (!(part->nummesh & 7))
	{
		for (int i = 0; i < part->nummesh; i += 8, src += 8, dst += 8)
		{
			EMIT_SHADOW_VERT (&dst[0], &src[0]);
			EMIT_SHADOW_VERT (&dst[1], &src[1]);
			EMIT_SHADOW_VERT (&dst[2], &src[2]);
			EMIT_SHADOW_VERT (&dst[3], &src[3]);
			EMIT_SHADOW_VERT (&dst[4], &src[4]);
			EMIT_SHADOW_VERT (&dst[5], &src[5]);
			EMIT_SHADOW_VERT (&dst[6], &src[6]);
			EMIT_SHADOW_VERT (&dst[7], &src[7]);
		}
	}
	else if (!(part->nummesh & 3))
	{
		for (int i = 0; i < part->nummesh; i += 4, src += 4, dst += 4)
		{
			EMIT_SHADOW_VERT (&dst[0], &src[0]);
			EMIT_SHADOW_VERT (&dst[1], &src[1]);
			EMIT_SHADOW_VERT (&dst[2], &src[2]);
			EMIT_SHADOW_VERT (&dst[3], &src[3]);
		}
	}
	else
	{
		for (int i = 0; i < part->nummesh; i++, src++, dst++)
			EMIT_SHADOW_VERT (dst, src);
	}

	D3D_VBOEmitIndexes (part->indexes, &d3d_VBOState.IBOData[d3d_VBOState.NumIndexes], part->numindexes, d3d_VBOState.NumVerts);

	d3d_VBOState.NumVerts += part->nummesh;
	d3d_VBOState.NumIndexes += part->numindexes;
	d3d_VBOState.CurrShader->NumVerts += part->nummesh;
	d3d_VBOState.CurrShader->NumIndexes += part->numindexes;
}


void D3D_VBORender (void)
{
	// done first because they could be locked even if rendering nothing
	D3D_VBOUnlockBuffers ();

	// nothing to render
	if (!d3d_VBOState.NumVerts) return;
	if (!d3d_VBOState.NumShaders) return;

	D3D_VBOSetVBOStream (d3d_VBOState.VBO, d3d_VBOState.IBO, d3d_VBOState.Stride);
	d3d_vbo_shader_t *CurrShader = d3d_VBOState.Shaders;
	d3d_vbo_shader_t *PrevShader = NULL;

	for (int i = 0; i < d3d_VBOState.NumShaders; i++, CurrShader++)
	{
		if (!CurrShader->NumVerts) continue;

		if (CurrShader->Type == SHADER_TYPE_FIXED)
		{
			// shut down a previous HLSL shader if moving to fixed
			if (PrevShader && PrevShader->Type == SHADER_TYPE_HLSL)
				PrevShader->Effect.ShutdownCallback ();

			for (int s = 0; s < 3; s++)
			{
				D3D_SetTexture (s, CurrShader->Stages[s].Tex);

				D3D_SetTextureState (s, D3DTSS_COLOROP, CurrShader->Stages[s].ColorOp);
				D3D_SetTextureState (s, D3DTSS_COLORARG1, CurrShader->Stages[s].ColorArg1);
				D3D_SetTextureState (s, D3DTSS_COLORARG2, CurrShader->Stages[s].ColorArg2);

				D3D_SetTextureState (s, D3DTSS_ALPHAOP, CurrShader->Stages[s].AlphaOp);
				D3D_SetTextureState (s, D3DTSS_ALPHAARG1, CurrShader->Stages[s].AlphaArg1);
				D3D_SetTextureState (s, D3DTSS_ALPHAARG2, CurrShader->Stages[s].AlphaArg2);
			}
		}
		else if (CurrShader->Type == SHADER_TYPE_HLSL)
		{
			// startup this shader if moving from fixed
			if (PrevShader && PrevShader->Type != SHADER_TYPE_HLSL)
				CurrShader->Effect.SetupCallback;

			// change state for this shader
			CurrShader->Effect.StateChangeCallback ();
		}
		else continue;

		// to do - add vbo state here too
		if (CurrShader->NumIndexes && CurrShader->NumVerts)
		{
			d3d_Device->DrawIndexedPrimitive
			(
				d3d_VBOState.PrimitiveType,
				0,
				CurrShader->FirstVert,
				CurrShader->NumVerts,
				CurrShader->FirstIndex,
				CurrShader->NumIndexes / 3
			);
		}

		// track the previous shader so that we can switch it off
		PrevShader = CurrShader;
	}

	// shut down the last shader if appropriate
	if (PrevShader && PrevShader->Type == SHADER_TYPE_HLSL)
		PrevShader->Effect.ShutdownCallback ();

	d3d_VBOState.NumVerts = 0;
	d3d_VBOState.NumIndexes = 0;
	d3d_VBOState.NumShaders = 0;
	d3d_VBOState.CurrShader = NULL;
}


void D3D_VBOCheckOverflow (int numverts, int numindexes)
{
	if (d3d_VBOState.NumVerts + numverts >= D3D_MAX_VBO_SIZE) goto OverflowVBO;
	if (d3d_VBOState.NumIndexes + numindexes >= D3D_MAX_VBO_SIZE) goto OverflowVBO;
	if (d3d_VBOState.NumShaders >= D3D_MAX_VBO_SHADERS) goto OverflowVBO;

	// not overflowed
	return;

OverflowVBO:;
	// render what we've currently got
	D3D_VBORender ();

	// begin again (fixme - do we need to add the most recent shader....)
	D3D_VBOBegin (d3d_VBOState.PrimitiveType, d3d_VBOState.Stride);
}

