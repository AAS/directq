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
// r_misc.c

#include "quakedef.h"
#include "d3d_model.h"
#include "d3d_quake.h"
#include "resource.h"

extern HWND d3d_Window;


/*
============================================================================================================

		VBOs

============================================================================================================
*/

#define MAX_BUFFER_SIZE		65534

static LPDIRECT3DVERTEXBUFFER9 d3d_VBO = NULL;
static LPDIRECT3DINDEXBUFFER9 d3d_IBO = NULL;

void D3D_VBOCreateOnDemand (void)
{
	if (!d3d_VBO)
	{
		// we don't anticipate ever having more than 64 bytes per vert, but we could increase it if we ever do
		hr = d3d_Device->CreateVertexBuffer
		(
			64 * MAX_BUFFER_SIZE,
			D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY,
			0,
			D3DPOOL_DEFAULT,
			&d3d_VBO,
			NULL
		);

		if (FAILED (hr))
		{
			Sys_Error ("D3D_VBOCreateOnDemand: failed to create a vertex buffer");
			return;
		}
	}

	if (!d3d_IBO)
	{
		hr = d3d_Device->CreateIndexBuffer
		(
			sizeof (unsigned short) * MAX_BUFFER_SIZE,
			D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY,
			D3DFMT_INDEX16,
			D3DPOOL_DEFAULT,
			&d3d_IBO,
			NULL
		);

		if (FAILED (hr))
		{
			Sys_Error ("D3D_VBOCreateOnDemand: failed to create an index buffer");
			return;
		}
	}
}


bool bufferslocked = false;

typedef struct d3d_vbotexturedef_s
{
	DWORD colorop;
	DWORD colorarg1;
	DWORD colorarg2;

	DWORD alphaop;
	DWORD alphaarg1;
	DWORD alphaarg2;

	LPDIRECT3DTEXTURE9 tex;
} d3d_vbotexturedef_t;

typedef struct d3d_vbomark_s
{
	d3d_vbotexturedef_t TextureDefs[3];

	int firstvertex;
	int numvertexes;

	int firstindex;
	int numindexes;
} d3d_vbomark_t;

// marcher hits ~100
#define MAX_VBO_MARKS	256

d3d_vbomark_t d3d_VBOMarks[MAX_VBO_MARKS];
d3d_vbomark_t *d3d_CurrentVBOMark = NULL;

int d3d_NumVBOMarks;
int d3d_VBOPolySize;
int d3d_VBOTotalVertexes;
int d3d_VBOTotalIndexes;


void D3D_BeginVertexes (void **vertexes, void **indexes, int polysize)
{
	d3d_VBOPolySize = polysize;

	D3D_VBOCreateOnDemand ();
	d3d_VBO->Lock (0, 0, vertexes, D3DLOCK_DISCARD);
	d3d_IBO->Lock (0, 0, indexes, D3DLOCK_DISCARD);

	bufferslocked = true;

	d3d_CurrentVBOMark = &d3d_VBOMarks[0];
	memset (d3d_CurrentVBOMark, 0, sizeof (d3d_vbomark_t));

	d3d_CurrentVBOMark->numindexes = d3d_CurrentVBOMark->numvertexes = 0;
	d3d_CurrentVBOMark->firstindex = d3d_CurrentVBOMark->firstvertex = 0;

	d3d_VBOTotalIndexes = d3d_VBOTotalVertexes = 0;

	d3d_NumVBOMarks = 1;
}


void D3D_EndVertexes (void)
{
	if (bufferslocked)
	{
		d3d_VBO->Unlock ();
		d3d_IBO->Unlock ();

		bufferslocked = false;
	}

	if (d3d_NumVBOMarks)
	{
		d3d_RenderDef.numsss++;
		d3d_Device->SetStreamSource (0, d3d_VBO, 0, d3d_VBOPolySize);
		d3d_Device->SetIndices (d3d_IBO);

		for (int i = 0; i < d3d_NumVBOMarks; i++)
		{
			d3d_CurrentVBOMark = &d3d_VBOMarks[i];

			// the current mark contains no verts or indexes...!
			if (!d3d_CurrentVBOMark->numindexes || !d3d_CurrentVBOMark->numvertexes) continue;

			d3d_vbotexturedef_t *TextureDef = d3d_CurrentVBOMark->TextureDefs;

			for (int t = 0; t < 3; t++, TextureDef++)
			{
				D3D_SetTextureColorMode (t, TextureDef->colorop, TextureDef->colorarg1, TextureDef->colorarg2);
				D3D_SetTextureAlphaMode (t, TextureDef->alphaop, TextureDef->alphaarg1, TextureDef->alphaarg2);
				D3D_SetTexture (t, TextureDef->tex);
			}

			d3d_Device->DrawIndexedPrimitive (D3DPT_TRIANGLELIST, 0, d3d_CurrentVBOMark->firstvertex, d3d_CurrentVBOMark->numvertexes, d3d_CurrentVBOMark->firstindex, d3d_CurrentVBOMark->numindexes / 3);
		}
	}
}


void D3D_SetVBOColorMode (DWORD stage, DWORD mode, DWORD arg1, DWORD arg2)
{
	d3d_CurrentVBOMark->TextureDefs[stage].colorop = mode;
	d3d_CurrentVBOMark->TextureDefs[stage].colorarg1 = arg1;
	d3d_CurrentVBOMark->TextureDefs[stage].colorarg2 = arg2;
}


void D3D_SetVBOAlphaMode (DWORD stage, DWORD mode, DWORD arg1, DWORD arg2)
{
	d3d_CurrentVBOMark->TextureDefs[stage].alphaop = mode;
	d3d_CurrentVBOMark->TextureDefs[stage].alphaarg1 = arg1;
	d3d_CurrentVBOMark->TextureDefs[stage].alphaarg2 = arg2;
}


void D3D_SetVBOTexture (DWORD stage, LPDIRECT3DTEXTURE9 texture)
{
	d3d_CurrentVBOMark->TextureDefs[stage].tex = texture;
}


bool D3D_CheckVBO (int numverts, int numindexes)
{
	if (d3d_VBOTotalVertexes + numverts >= 65000 || d3d_VBOTotalIndexes + numindexes >= 65000 || d3d_NumVBOMarks >= MAX_VBO_MARKS)
		return false;
	else return true;
}


void D3D_GotoNewVBOMark (void)
{
	d3d_vbomark_t *d3d_PreviousVBOMark = d3d_CurrentVBOMark;

	d3d_CurrentVBOMark = &d3d_VBOMarks[d3d_NumVBOMarks];
	memset (d3d_CurrentVBOMark, 0, sizeof (d3d_vbomark_t));

	d3d_CurrentVBOMark->numindexes = 0;
	d3d_CurrentVBOMark->numvertexes = 0;

	d3d_CurrentVBOMark->firstindex = d3d_PreviousVBOMark->firstindex + d3d_PreviousVBOMark->numindexes;
	d3d_CurrentVBOMark->firstvertex = d3d_PreviousVBOMark->firstvertex + d3d_PreviousVBOMark->numvertexes;

	d3d_NumVBOMarks++;
}


void D3D_UpdateVBOMark (int numverts, int numindexes)
{
	d3d_CurrentVBOMark->numvertexes += numverts;
	d3d_CurrentVBOMark->numindexes += numindexes;

	d3d_VBOTotalVertexes += numverts;
	d3d_VBOTotalIndexes += numindexes;
}


void D3D_GetVertexBufferSpace (void **data)
{
	// create on demand
	D3D_VBOCreateOnDemand ();
	d3d_VBO->Lock (0, 0, data, D3DLOCK_DISCARD);
}


void D3D_GetIndexBufferSpace (void **data)
{
	// create on demand
	D3D_VBOCreateOnDemand ();
	d3d_IBO->Lock (0, 0, data, D3DLOCK_DISCARD);
}


void D3D_SubmitVertexes (int numverts, int numindexes, int polysize)
{
	// always unlock!
	d3d_VBO->Unlock ();
	d3d_IBO->Unlock ();

	if (numverts || numindexes)
	{
		d3d_RenderDef.numsss++;
		d3d_Device->SetStreamSource (0, d3d_VBO, 0, polysize);
		d3d_Device->SetIndices (d3d_IBO);

		d3d_Device->DrawIndexedPrimitive (D3DPT_TRIANGLELIST, 0, 0, numverts, 0, (numindexes / 3));
	}
}


BOOL D3D_AreBuffersFull (int numverts, int numindexes)
{
	if (numverts >= 65534) return TRUE;
	if (numindexes >= 65536) return TRUE;

	return FALSE;
}


void D3D_ReleaseBuffers (void)
{
	SAFE_RELEASE (d3d_VBO);
	SAFE_RELEASE (d3d_IBO);
}


/*
============================================================================================================

		MATRIX OPS

	These happen in pace on the matrix and update it's current values

============================================================================================================
*/

void D3D_TranslateMatrix (D3DMATRIX *matrix, float x, float y, float z)
{
	D3DXMATRIX *m = D3D_MakeD3DXMatrix (matrix);
	D3DXMATRIX tmp;

	D3DXMatrixTranslation (&tmp, x, y, z);
	D3DXMatrixMultiply (m, &tmp, m);
}


void D3D_ScaleMatrix (D3DMATRIX *matrix, float x, float y, float z)
{
	D3DXMATRIX *m = D3D_MakeD3DXMatrix (matrix);
	D3DXMATRIX tmp;

	D3DXMatrixScaling (&tmp, x, y, z);
	D3DXMatrixMultiply (m, &tmp, m);
}


void D3D_RotateMatrix (D3DMATRIX *matrix, float x, float y, float z, float angle)
{
	D3DXMATRIX *m = D3D_MakeD3DXMatrix (matrix);
	D3DXMATRIX tmp;

	if (x)
	{
		D3DXMatrixRotationX (&tmp, D3DXToRadian (angle) * x);
		D3DXMatrixMultiply (m, &tmp, m);
	}

	if (y)
	{
		D3DXMatrixRotationY (&tmp, D3DXToRadian (angle) * y);
		D3DXMatrixMultiply (m, &tmp, m);
	}

	if (z)
	{
		D3DXMatrixRotationZ (&tmp, D3DXToRadian (angle) * z);
		D3DXMatrixMultiply (m, &tmp, m);
	}
}


void D3D_LoadIdentity (D3DMATRIX *matrix)
{
	D3DXMATRIX *m = D3D_MakeD3DXMatrix (matrix);
	D3DXMatrixIdentity (m);
}


void D3D_MultMatrix (D3DMATRIX *matrix1, D3DMATRIX *matrix2)
{
	D3DXMATRIX *m1 = D3D_MakeD3DXMatrix (matrix1);
	D3DXMATRIX *m2 = D3D_MakeD3DXMatrix (matrix1);

	D3DXMatrixMultiply (m1, m1, m2);
}


D3DXMATRIX *D3D_MakeD3DXMatrix (D3DMATRIX *matrix)
{
	union SortItOutBill
	{
		D3DMATRIX *d3dmatrix;
		D3DXMATRIX *d3dxmatrix;
	} m;

	m.d3dmatrix = matrix;
	return m.d3dxmatrix;
}


/*
============================================================================================================

		HLSL STUFF

============================================================================================================
*/

cvar_t r_hlsl ("r_hlsl", "1", CVAR_ARCHIVE);

bool SilentLoad = false;

// keep these global as we'll want to use them in a few places
char *vs_version;
char *ps_version;

// effects
LPD3DXEFFECT d3d_LiquidFX;
LPD3DXEFFECT d3d_SkyFX;

// vertex declarations
LPDIRECT3DVERTEXDECLARATION9 d3d_LiquidDeclaration = NULL;
LPDIRECT3DVERTEXDECLARATION9 d3d_SkyDeclaration = NULL;

bool D3D_LoadEffect (char *name, int resourceid, LPD3DXEFFECT *eff, int vsver, int psver)
{
	char *EffectString = NULL;
	LPD3DXBUFFER errbuf = NULL;

	// load the resource - note - we don't use D3DXCreateEffectFromResource as importing an RCDATA resource
	// from a text file in Visual Studio doesn't NULL terminate the file, causing it to blow up.
	int len = Sys_LoadResourceData (resourceid, (void **) &EffectString);

	if (vsver >= 3 && psver >= 3)
	{
		for (int i = 0; ; i++)
		{
			if (!EffectString[i]) break;

			if (!strnicmp (&EffectString[i], " vs_2_0 ", 8))
			{
				EffectString[i + 4] = '3';
				continue;
			}

			if (!strnicmp (&EffectString[i], " ps_2_0 ", 8))
			{
				EffectString[i + 4] = '3';
				continue;
			}
		}
	}

	hr = D3DXCreateEffect
	(
		d3d_Device,
		EffectString,
		len,
		NULL,
		NULL,
		D3DXSHADER_SKIPVALIDATION | D3DXFX_DONOTSAVESTATE,
		NULL,
		eff,
		&errbuf
	);

	if (FAILED (hr))
	{
		char *errstr = (char *) errbuf->GetBufferPointer ();
#ifdef _DEBUG
		Con_Printf ("D3D_LoadEffect: Error compiling %s\n%s", name, errstr);
#endif
		errbuf->Release ();
		return false;
	}

	SAFE_RELEASE (errbuf);
	return true;
}


void D3D_InitHLSL (void)
{
	D3DVERTEXELEMENT9 vdliquid[] =
	{
		{0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
		{0, 12, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
		{0xff, 0, D3DDECLTYPE_UNUSED, 0, 0, 0}
	};

	d3d_Device->CreateVertexDeclaration (vdliquid, &d3d_LiquidDeclaration);

	D3DVERTEXELEMENT9 vdsky[] =
	{
		{0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
		{0xff, 0, D3DDECLTYPE_UNUSED, 0, 0, 0}
	};

	d3d_Device->CreateVertexDeclaration (vdsky, &d3d_SkyDeclaration);

	// now set up effects
	vs_version = (char *) D3DXGetVertexShaderProfile (d3d_Device);
	ps_version = (char *) D3DXGetPixelShaderProfile (d3d_Device);
	d3d_GlobalCaps.supportPixelShaders = false;

	if (!SilentLoad) Con_Printf ("\n");

	if (vs_version)
	{
		if (!SilentLoad) Con_Printf ("Vertex Shader Version: %s\n", vs_version);
	}
	else
	{
		// note - we expect this to never happen as D3D will create vertex shaders in software
		// for devices that don't support them in hardware.
		Con_Printf ("Vertex Shaders Not Available\n");
		return;
	}

	if (ps_version)
	{
		if (!SilentLoad) Con_Printf ("Pixel Shader Version: %s\n", ps_version);
	}
	else
	{
		// note - don't sys_error here as we will implement a software fallback
		if (!SilentLoad) Con_Printf ("Pixel Shaders Not Available\n");
		return;
	}

	// D3DSHADER_VERSION_MAJOR is UNDOCUMENTED in the SDK!!!  all that it says about these caps is that
	// they are "two numbers" but there is NOTHING about decoding them.
	int vsvermaj = D3DSHADER_VERSION_MAJOR (d3d_DeviceCaps.VertexShaderVersion);
	int psvermaj = D3DSHADER_VERSION_MAJOR (d3d_DeviceCaps.PixelShaderVersion);

	if (vsvermaj < 2 || psvermaj < 2)
	{
		Con_Printf ("Vertex or Pixel Shaders version is too low.\n");
		return;
	}

	// load effects - if we get this far we know that pixel shaders are available
	if (!D3D_LoadEffect ("Liquid Shader", IDR_LIQUID, &d3d_LiquidFX, vsvermaj, psvermaj)) return;
	if (!D3D_LoadEffect ("Sky Shader", IDR_SKY, &d3d_SkyFX, vsvermaj, psvermaj)) return;

	if (!SilentLoad) Con_Printf ("Created Shaders OK\n");

	// only display output on the first load
	SilentLoad = true;
	d3d_GlobalCaps.supportPixelShaders = true;
}


void D3D_ShutdownHLSL (void)
{
	// effects
	SAFE_RELEASE (d3d_LiquidFX);
	SAFE_RELEASE (d3d_SkyFX);

	// declarations
	SAFE_RELEASE (d3d_LiquidDeclaration);
	SAFE_RELEASE (d3d_SkyDeclaration);
}


/*
============================================================================================================

		BRUSHMODEL TEXTURE REGISTRATION

============================================================================================================
*/

d3d_registeredtexture_t *d3d_RegisteredTextures = NULL;
int d3d_NumRegisteredTextures = 0;

void D3D_RegisterTexture (texture_t *tex)
{
	// see does it already exist (the D3D texture is good for use as a unique signifier here)
	for (int i = 0; i < d3d_NumRegisteredTextures; i++)
	{
		if ((int) d3d_RegisteredTextures[i].texture->teximage == (int) tex->teximage)
		{
			// ensure that we catch the same texture used in different bmodels
			tex->registration = &d3d_RegisteredTextures[i];
			return;
		}
	}

	// create the registered textures buffer if it doesn't currently exist, otherwise
	// we append to the previous buffer in a consecutive memory pool
	if (!d3d_RegisteredTextures)
		d3d_RegisteredTextures = (d3d_registeredtexture_t *) Pool_Map->Alloc (sizeof (d3d_registeredtexture_t));
	else Pool_Map->Alloc (sizeof (d3d_registeredtexture_t));

	// register this texture
	d3d_RegisteredTextures[d3d_NumRegisteredTextures].texture = tex;
	d3d_RegisteredTextures[d3d_NumRegisteredTextures].surfchain = NULL;

	// store the registration back to the texture
	tex->registration = &d3d_RegisteredTextures[d3d_NumRegisteredTextures];

	// go to the next registration
	d3d_NumRegisteredTextures++;
}


void D3D_RegisterTextures (void)
{
	d3d_RegisteredTextures = NULL;
	d3d_NumRegisteredTextures = 0;

	model_t *mod;

	for (int i = 1; i < MAX_MODELS; i++)
	{
		// no more models
		if (!(mod = cl.model_precache[i])) break;

		// brushes only
		if (mod->type != mod_brush) continue;

		// make sure we get 'em all; including inline here
		texture_t *tex;
		brushhdr_t *hdr = mod->brushhdr;

		// find all textures in the model
		for (int t = 0; t < hdr->numtextures; t++)
		{
			// no texture
			if (!(tex = hdr->textures[t])) continue;

			// already registered
			if (tex->registration) continue;

			D3D_RegisterTexture (tex);
		}

		// sanity check
		// (remove this before release)
		for (int t = 0; t < hdr->numtextures; t++)
		{
			// no texture
			if (!(tex = hdr->textures[t])) continue;

			if (!tex->registration) Sys_Error ("Failed to register texture %s", tex->name);
		}
	}
}


/*
============================================================================================================

		ALPHA SORTING

============================================================================================================
*/

// list of alpha items
typedef struct d3d_alphalist_s
{
	int Type;
	float Dist;

	union
	{
		entity_t *Entity;
		particle_type_t *Particle;
		struct d3d_modelsurf_s *ModelSurf;
	};
} d3d_alphalist_t;


#define D3D_ALPHATYPE_ENTITY		1
#define D3D_ALPHATYPE_PARTICLE		2
#define D3D_ALPHATYPE_WATERWARP		3

d3d_alphalist_t *d3d_AlphaList = NULL;
CSpaceBuffer *Pool_Alpha = NULL;
int d3d_NumAlphaList = 0;


void D3D_GetAlphaPoolSpace (void)
{
	if (!Pool_Alpha)
	{
		Pool_Alpha = new CSpaceBuffer ("Alpha Polys", 2, POOL_MAP);
		d3d_AlphaList = (d3d_alphalist_t *) Pool_Alpha->Alloc (1);
		Pool_Alpha->Rewind ();
		d3d_NumAlphaList = 0;
	}

	// ensure space
	Pool_Alpha->Alloc (sizeof (d3d_alphalist_t));
}


float D3D_GetDist (float *origin)
{
	// no need to sqrt these as all we're concerned about is relative distances
	// (if x < y then sqrt (x) is also < sqrt (y))
	return
	(
		(origin[0] - r_origin[0]) * (origin[0] - r_origin[0]) +
		(origin[1] - r_origin[1]) * (origin[1] - r_origin[1]) +
		(origin[2] - r_origin[2]) * (origin[2] - r_origin[2])
	);
}


void D3D_AddToAlphaList (entity_t *ent)
{
	D3D_GetAlphaPoolSpace ();

	d3d_AlphaList[d3d_NumAlphaList].Type = D3D_ALPHATYPE_ENTITY;
	d3d_AlphaList[d3d_NumAlphaList].Entity = ent;
	d3d_AlphaList[d3d_NumAlphaList].Dist = D3D_GetDist (ent->origin);

	d3d_NumAlphaList++;
}


void D3D_AddToAlphaList (d3d_modelsurf_t *modelsurf)
{
	D3D_GetAlphaPoolSpace ();

	// we only support turb surfaces for now
	if (modelsurf->surf->flags & SURF_DRAWTURB)
	{
		d3d_AlphaList[d3d_NumAlphaList].Type = D3D_ALPHATYPE_WATERWARP;
		d3d_AlphaList[d3d_NumAlphaList].ModelSurf = modelsurf;
		d3d_AlphaList[d3d_NumAlphaList].Dist = D3D_GetDist (modelsurf->surf->midpoint);

		d3d_NumAlphaList++;
	}
}


void D3D_AddToAlphaList (particle_type_t *particle)
{
	D3D_GetAlphaPoolSpace ();

	d3d_AlphaList[d3d_NumAlphaList].Type = D3D_ALPHATYPE_PARTICLE;
	d3d_AlphaList[d3d_NumAlphaList].Particle = particle;
	d3d_AlphaList[d3d_NumAlphaList].Dist = D3D_GetDist (particle->spawnorg);

	d3d_NumAlphaList++;
}


int D3D_AlphaSortFunc (d3d_alphalist_t *a, d3d_alphalist_t *b)
{
	// back to front ordering
	return (int) (b->Dist - a->Dist);
}


void D3D_SetupAliasModel (entity_t *e);
void D3D_DrawAliasBatch (entity_t **ents, int numents);
void D3D_DrawAlphaBrushModel (entity_t *ent);
void D3D_SetupSpriteModel (entity_t *ent);
void R_AddParticleTypeToRender (particle_type_t *pt);

void D3D_SetupTurbState (void);
void D3D_TakeDownTurbState (void);
void D3D_EmitWarpSurface (d3d_modelsurf_t *modelsurf);

void D3D_AlphaListStageChange (int oldtype, int newtype)
{
	switch (oldtype)
	{
	case D3D_ALPHATYPE_WATERWARP:
		D3D_TakeDownTurbState ();
		D3D_BackfaceCull (D3DCULL_CCW);

		break;

	default:
		break;
	}

	switch (newtype)
	{
	case D3D_ALPHATYPE_WATERWARP:
		D3D_SetupTurbState ();
		D3D_BackfaceCull (D3DCULL_NONE);

		break;

	default:
		break;
	}
}


void D3D_SortAlphaList (void)
{
	// nothing to add
	if (!d3d_NumAlphaList) return;

	// sort the alpha list
	if (d3d_NumAlphaList == 1)
		; // no need to sort
	else if (d3d_NumAlphaList == 2)
	{
		// exchange if necessary
		if (d3d_AlphaList[2].Dist > d3d_AlphaList[1].Dist)
		{
			// exchange
			d3d_alphalist_t Temp;

			memcpy (&Temp, &d3d_AlphaList[1], sizeof (d3d_alphalist_t));
			memcpy (&d3d_AlphaList[1], &d3d_AlphaList[2], sizeof (d3d_alphalist_t));
			memcpy (&d3d_AlphaList[1], &Temp, sizeof (d3d_alphalist_t));
		}
	}
	else
	{
		// sort fully
		qsort
		(
			d3d_AlphaList,
			d3d_NumAlphaList,
			sizeof (d3d_alphalist_t),
			(int (*) (const void *, const void *)) D3D_AlphaSortFunc
		);
	}

	// enable blending
	D3D_SetRenderState (D3DRS_ALPHABLENDENABLE, TRUE);
	D3D_SetRenderState (D3DRS_BLENDOP, D3DBLENDOP_ADD);
	D3D_SetRenderState (D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	D3D_SetRenderState (D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	D3D_SetRenderState (D3DRS_ZWRITEENABLE, FALSE);

	int previous = 0;

	// now add all the items in it to the alpha buffer
	for (int i = 0; i < d3d_NumAlphaList; i++)
	{
		// check for state change
		if (d3d_AlphaList[i].Type != previous)
		{
			D3D_AlphaListStageChange (previous, d3d_AlphaList[i].Type);
			previous = d3d_AlphaList[i].Type;
		}

		switch (d3d_AlphaList[i].Type)
		{
		case D3D_ALPHATYPE_ENTITY:
			if (d3d_AlphaList[i].Entity->model->type == mod_alias)
			{
				// the viewent needs to write to Z
				if (d3d_AlphaList[i].Entity == cl_entities[cl.viewentity] && chase_active.value) D3D_SetRenderState (D3DRS_ZWRITEENABLE, TRUE);
				D3D_DrawAliasBatch (&d3d_AlphaList[i].Entity, 1);
				if (d3d_AlphaList[i].Entity == cl_entities[cl.viewentity] && chase_active.value) D3D_SetRenderState (D3DRS_ZWRITEENABLE, FALSE);
			}
			else if (d3d_AlphaList[i].Entity->model->type == mod_brush)
				D3D_DrawAlphaBrushModel (d3d_AlphaList[i].Entity);
			else if (d3d_AlphaList[i].Entity->model->type == mod_sprite)
				D3D_SetupSpriteModel (d3d_AlphaList[i].Entity);

			break;

		case D3D_ALPHATYPE_PARTICLE:
			R_AddParticleTypeToRender (d3d_AlphaList[i].Particle);
			break;

		case D3D_ALPHATYPE_WATERWARP:
			D3D_EmitWarpSurface (d3d_AlphaList[i].ModelSurf);

			break;

		default:
			// nothing to add
			break;
		}
	}

	// take down the final state used (in case it was a HLSL state)
	D3D_AlphaListStageChange (d3d_AlphaList[d3d_NumAlphaList - 1].Type, 0);

	// disable blending (done)
	// the cull type may have been modified going through here so put it back the way it was
	D3D_BackfaceCull (D3DCULL_CCW);
	D3D_SetRenderState (D3DRS_ZWRITEENABLE, TRUE);
	D3D_SetRenderState (D3DRS_ALPHABLENDENABLE, FALSE);

	// reset alpha list
	d3d_NumAlphaList = 0;
	Pool_Alpha->Rewind ();
}


/*
============================================================================================================

		SETUP

============================================================================================================
*/

float Lerp (float l1, float l2, float lerpfactor)
{
	return (l1 * lerpfactor) + (l2 * (1.0f - lerpfactor));
}


void R_InitParticles (void);
void R_ClearParticles (void);
void D3D_BuildLightmaps (void);
void R_LoadSkyBox (char *basename, bool feedback);

LPDIRECT3DTEXTURE9 r_notexture = NULL;
extern LPDIRECT3DTEXTURE9 crosshairtexture;
extern LPDIRECT3DTEXTURE9 yahtexture;


/*
==================
R_InitTextures
==================
*/
void R_InitTextures (void)
{
	// create a simple checkerboard texture for the default
	r_notexture_mip = (texture_t *) Pool_Permanent->Alloc (sizeof (texture_t) + 4 * 4);

	r_notexture_mip->width = r_notexture_mip->height = 4;
	byte *dest = (byte *) (r_notexture_mip + 1);

	for (int y = 0; y < 4; y++)
	{
		for (int x = 0; x < 4; x++)
		{
			if ((y < 2) ^ (x < 2))
				*dest++ = 0;
			else *dest++ = 0xf;
		}
	}
}


// textures to load from resources
extern LPDIRECT3DTEXTURE9 particledottexture;
extern LPDIRECT3DTEXTURE9 qmbparticleblood;
extern LPDIRECT3DTEXTURE9 qmbparticlebubble;
extern LPDIRECT3DTEXTURE9 qmbparticlelightning;
extern LPDIRECT3DTEXTURE9 qmbparticlelightningold;
extern LPDIRECT3DTEXTURE9 particlesmoketexture;
extern LPDIRECT3DTEXTURE9 qmbparticlespark;
extern LPDIRECT3DTEXTURE9 qmbparticletrail;

extern LPDIRECT3DTEXTURE9 R_PaletteTexture;

LPDIRECT3DTEXTURE9 r_blacktexture = NULL;
LPDIRECT3DTEXTURE9 r_greytexture = NULL;

void R_ReleaseResourceTextures (void)
{
	SAFE_RELEASE (particledottexture);
	SAFE_RELEASE (qmbparticleblood);
	SAFE_RELEASE (qmbparticlebubble);
	SAFE_RELEASE (qmbparticlelightning);
	SAFE_RELEASE (qmbparticlelightningold);
	SAFE_RELEASE (particlesmoketexture);
	SAFE_RELEASE (qmbparticlespark);
	SAFE_RELEASE (qmbparticletrail);

	SAFE_RELEASE (crosshairtexture);
	SAFE_RELEASE (r_blacktexture);
	SAFE_RELEASE (r_greytexture);
}


void R_InitResourceTextures (void)
{
	// load any textures contained in exe resources
	D3D_LoadResourceTexture (&particledottexture, IDR_PARTICLEDOT, IMAGE_MIPMAP);

	// QMB particles
	D3D_LoadResourceTexture (&qmbparticleblood, IDR_QMBBLOOD, IMAGE_MIPMAP);
	D3D_LoadResourceTexture (&qmbparticlebubble, IDR_QMBBUBBLE, IMAGE_MIPMAP);
	D3D_LoadResourceTexture (&qmbparticlelightning, IDR_QMBLIGHTNING, IMAGE_MIPMAP);
	D3D_LoadResourceTexture (&qmbparticlelightningold, IDR_QMBLIGHTNING_OLD, IMAGE_MIPMAP);
	D3D_LoadResourceTexture (&particlesmoketexture, IDR_QMBSMOKE, IMAGE_MIPMAP);
	D3D_LoadResourceTexture (&qmbparticlespark, IDR_QMBSPARK, IMAGE_MIPMAP);
	D3D_LoadResourceTexture (&qmbparticletrail, IDR_QMBTRAIL, IMAGE_MIPMAP);

	D3D_LoadResourceTexture (&crosshairtexture, IDR_CROSSHAIR, 0);
	D3D_LoadResourceTexture (&yahtexture, IDR_YOUAREHERE, 0);

	// create a texture for the palette
	// this is to save on state changes when a flat colour is needed; rather than
	// switching off texturing and other messing, we just draw this one.
	byte *paldata = (byte *) Pool_Temp->Alloc (128 * 128);

	for (int i = 0; i < 256; i++)
	{
		int row = (i >> 4) * 8;
		int col = (i & 15) * 8;

		for (int x = col; x < col + 8; x++)
		{
			for (int y = row; y < row + 8; y++)
			{
				int p = y * 128 + x;

				paldata[p] = i;
			}
		}
	}

	D3D_UploadTexture (&R_PaletteTexture, paldata, 128, 128, 0);

	// clear to black
	memset (paldata, 0, 128 * 128);

	// load the black texture - we must mipmap this and also load it as 32 bit
	// (in case palette index 0 isn't black).  also load it really really small...
	D3D_UploadTexture (&r_blacktexture, paldata, 4, 4, IMAGE_MIPMAP);

	// clear to grey
	memset (paldata, 128, 128 * 128);

	// load the black texture - we must mipmap this and also load it as 32 bit
	// (in case palette index 0 isn't black).  also load it really really small...
	D3D_UploadTexture (&r_greytexture, paldata, 4, 4, IMAGE_MIPMAP);

	// load the notexture properly
	D3D_UploadTexture (&r_notexture, (byte *) (r_notexture_mip + 1), r_notexture_mip->width, r_notexture_mip->height, IMAGE_MIPMAP);
}


/*
===============
R_Init
===============
*/
cvar_t r_lerporient ("r_lerporient", "1", CVAR_ARCHIVE);
cvar_t r_lerpframe ("r_lerpframe", "1", CVAR_ARCHIVE);

// these cvars do nothing for now; they only exist to soak up abuse from nehahra maps which expect them to be there
cvar_t r_oldsky ("r_oldsky", "1", CVAR_NEHAHRA);

cmd_t R_ReadPointFile_f_Cmd ("pointfile", R_ReadPointFile_f);

extern image_t d3d_PlayerSkins[];

void R_Init (void)
{
	R_InitParticles ();
	R_InitResourceTextures ();

	for (int i = 0; i < 256; i++)
	{
		SAFE_RELEASE (d3d_PlayerSkins[i].d3d_Texture);
	}
}


/*
============================================================================================================

		SKIN TRANSLATION

============================================================================================================
*/

/*
===============
D3D_TranslatePlayerSkin

Translates a skin texture by the per-player color lookup
===============
*/
void D3D_TranslatePlayerSkin (int playernum)
{
	byte	translate[256];
	int		i, j, s;
	model_t	*model;
	aliashdr_t *paliashdr;
	byte	*original;
	static int skinsize = -1;
	static byte *translated = NULL;

	// sanity
	cl.scores[playernum].colors &= 255;

	// already built a skin for this colour
	if (d3d_PlayerSkins[cl.scores[playernum].colors].d3d_Texture) return;

	int top = cl.scores[playernum].colors & 0xf0;
	int bottom = (cl.scores[playernum].colors & 15) << 4;

	// baseline has no palette translation
	for (i = 0; i < 256; i++) translate[i] = i;

	// is this just repeating what's done ample times elsewhere?
	for (i = 0; i < 16; i++)
	{
		if (top < 128)	// the artists made some backwards ranges.  sigh.
			translate[TOP_RANGE + i] = top + i;
		else
			translate[TOP_RANGE + i] = top + 15 - i;
				
		if (bottom < 128)
			translate[BOTTOM_RANGE + i] = bottom + i;
		else translate[BOTTOM_RANGE + i] = bottom + 15 - i;
	}

	// locate the original skin pixels
	entity_t *e = cl_entities[1 + playernum];
	model = e->model;

	if (!model) return;		// player doesn't have a model yet
	if (model->type != mod_alias) return; // only translate skins on alias models

	paliashdr = model->aliashdr;

	s = paliashdr->skinwidth * paliashdr->skinheight;

	if (e->skinnum < 0 || e->skinnum >= paliashdr->numskins)
	{
		Con_Printf ("(%d): Invalid player skin #%d\n", playernum, e->skinnum);
		original = paliashdr->skins[0].texels;
	}
	else original = paliashdr->skins[e->skinnum].texels;

	// no texels were saved
	if (!original) return;

	if (s & 3) Sys_Error ("R_TranslateSkin: s&3");

	// recreate the texture
	SAFE_RELEASE (d3d_PlayerSkins[cl.scores[playernum].colors].d3d_Texture);

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

	for (i = 0; i < s; i += 4)
	{
		translated[i] = translate[original[i]];
		translated[i+1] = translate[original[i+1]];
		translated[i+2] = translate[original[i+2]];
		translated[i+3] = translate[original[i+3]];
	}

	// don't compress these because it takes too long
	// we can't lock the texture and modify the texels directly because it may be a non-power-of-2
	D3D_UploadTexture
	(
		&d3d_PlayerSkins[cl.scores[playernum].colors].d3d_Texture,
		translated,
		paliashdr->skinwidth,
		paliashdr->skinheight,
		IMAGE_MIPMAP | IMAGE_NOCOMPRESS | IMAGE_NOEXTERN
	);
}


/*
===============
D3D_DeleteTranslation

Keeps vram usage down by deleting a skin texture when colour changes and if the old colour is unused
by any other player.
===============
*/
void D3D_DeleteTranslation (int playernum)
{
	for (int i = 0; i < 16; i++)
	{
		// current player
		if (i == playernum) continue;

		// in use
		if (cl.scores[playernum].colors == cl.scores[i].colors) return;
	}

	// release it
	SAFE_RELEASE (d3d_PlayerSkins[cl.scores[playernum].colors].d3d_Texture);
}


/*
============================================================================================================

		NEW MAP

============================================================================================================
*/

/*
===============
R_NewMap
===============
*/
void S_InitAmbients (void);

extern cvar_t gl_fogenable;
extern cvar_t gl_fogred;
extern cvar_t gl_foggreen;
extern cvar_t gl_fogblue;
extern cvar_t gl_fogdensity;

void R_ParseWorldSpawn (void)
{
	// get a pointer to the entities lump
	char *data = cl.worldmodel->brushhdr->entities;
	char key[40];

	// can never happen, otherwise we wouldn't have gotten this far
	if (!data) return;

	// parse the opening brace
	data = COM_Parse (data);

	// likewise can never happen
	if (!data) return;
	if (com_token[0] != '{') return;

	while (1)
	{
		// parse the key
		data = COM_Parse (data);

		// there is no key (end of worldspawn)
		if (!data) break;
		if (com_token[0] == '}') break;

		// allow keys with a leading _
		if (com_token[0] == '_')
			Q_strncpy (key, &com_token[1], 39);
		else Q_strncpy (key, com_token, 39);

		// remove trailing spaces
		while (key[strlen (key) - 1] == ' ') key[strlen (key) - 1] = 0;

		// parse the value
		data = COM_Parse (data);

		// likewise should never happen (has already been successfully parsed server-side and any errors that
		// were going to happen would have happened then; we only check to guard against pointer badness)
		if (!data) return;

		// check the key for a sky - notice the lack of standardisation in full swing again here!
		if (!stricmp (key, "sky") || !stricmp (key, "skyname") || !stricmp (key, "q1sky") || !stricmp (key, "skybox"))
		{
			// attempt to load it (silently fail)
			// direct from com_token - is this safe?  should be...
			R_LoadSkyBox (com_token, false);
			continue;
		}

		// parse fog from worldspawn
		if (!stricmp (key, "fog"))
		{
			sscanf (com_token, "%f %f %f %f", &gl_fogdensity.value, &gl_fogred.value, &gl_foggreen.value, &gl_fogblue.value);

			// update cvars correctly
			Cvar_Set (&gl_fogred, gl_fogred.value);
			Cvar_Set (&gl_foggreen, gl_foggreen.value);
			Cvar_Set (&gl_fogblue, gl_fogblue.value);
			Cvar_Set (&gl_fogdensity, gl_fogdensity.value / 50);

			// set to per-pixel linear fog
			Cvar_Set (&gl_fogenable, 1);

			continue;
		}

		// can add anything else we want to parse out of the worldspawn here too...
	}
}


bool R_RecursiveLeafContents (mnode_t *node)
{
	int			side;
	mplane_t	*plane;
	msurface_t	*surf;
	float		dot;

	if (node->contents == CONTENTS_SOLID) return true;
	if (node->visframe != d3d_RenderDef.visframecount) return true;

	// if a leaf node, draw stuff
	if (node->contents < 0)
	{
		// update contents colour
		if (((mleaf_t *) node)->contents == d3d_RenderDef.viewleaf->contents)
			((mleaf_t *) node)->contentscolor = d3d_RenderDef.viewleaf->contentscolor;
		else
		{
			// don't cross contents boundaries
			return false;
		}

		// leaf visframes are never marked?
		((mleaf_t *) node)->visframe = d3d_RenderDef.visframecount;
		return true;
	}

	// go down both sides
	if (!R_RecursiveLeafContents (node->children[0])) return false;
	return R_RecursiveLeafContents (node->children[1]);
}


void R_LeafVisibility (byte *vis);

void R_SetLeafContents (void)
{
	d3d_RenderDef.visframecount = -1;

	for (int i = 0; i < cl.worldmodel->brushhdr->numleafs; i++)
	{
		mleaf_t *leaf = &cl.worldmodel->brushhdr->leafs[i];

		// explicit NULLs
		if (leaf->contents == CONTENTS_EMPTY)
			leaf->contentscolor = NULL;
		else if (leaf->contents == CONTENTS_SOLID)
			leaf->contentscolor = NULL;
		else if (leaf->contents == CONTENTS_SKY)
			leaf->contentscolor = NULL;

		// no contents
		if (!leaf->contentscolor) continue;

		// go to a new visframe, reverse order so that we don't get mixed up with the main render
		d3d_RenderDef.visframecount--;
		d3d_RenderDef.viewleaf = leaf;

		// get pvs for this leaf
		byte *vis = Mod_LeafPVS (leaf, cl.worldmodel);

		// eval visibility
		R_LeafVisibility (vis);

		// update leaf contents
		R_RecursiveLeafContents (cl.worldmodel->brushhdr->nodes);
	}

	// mark as unseen to keep the automap valid
	for (int i = 0; i < cl.worldmodel->brushhdr->numleafs; i++) cl.worldmodel->brushhdr->leafs[i].seen = false;
	for (int i = 0; i < cl.worldmodel->brushhdr->numnodes; i++) cl.worldmodel->brushhdr->nodes[i].seen = false;

	d3d_RenderDef.visframecount = 0;
}


void LOC_LoadLocations (void);

extern byte *fatpvs;
extern float r_clipdist;

void Con_RemoveConsole (void);
void Menu_RemoveMenu (void);
void IN_FlushDInput (void);
void IN_ActivateMouse (void);
void R_RevalidateSkybox (void);

void R_NewMap (void)
{
	// init frame counters
	d3d_RenderDef.skyframe = -1;
	d3d_RenderDef.framecount = 1;
	d3d_RenderDef.visframecount = 0;

	// normal light value
	for (int i = 0; i < 256; i++) d_lightstylevalue[i] = 256;

	// world entity baseline
	memset (&d3d_RenderDef.worldentity, 0, sizeof (entity_t));
	d3d_RenderDef.worldentity.model = cl.worldmodel;
	cl.worldmodel->cacheent = &d3d_RenderDef.worldentity;
	d3d_RenderDef.worldentity.alphaval = 255;

	// fix up the worldmodel surfaces so it's consistent and we can reuse code
	cl.worldmodel->brushhdr->firstmodelsurface = 0;
	cl.worldmodel->brushhdr->nummodelsurfaces = cl.worldmodel->brushhdr->numsurfaces;

	// init edict pools
	if (!d3d_RenderDef.visedicts) d3d_RenderDef.visedicts = (entity_t **) Pool_Permanent->Alloc (MAX_VISEDICTS * sizeof (entity_t *));
	d3d_RenderDef.numvisedicts = 0;

	// no viewpoint
	d3d_RenderDef.viewleaf = NULL;
	d3d_RenderDef.oldviewleaf = NULL;

	// setup stuff
	R_ClearParticles ();

	// texture registration needs to come first
	D3D_RegisterTextures ();
	D3D_BuildLightmaps ();
	D3D_FlushTextures ();
	R_SetLeafContents ();
	R_ParseWorldSpawn ();

	// release cached skins to save memory
	for (int i = 0; i < 256; i++) SAFE_RELEASE (d3d_PlayerSkins[i].d3d_Texture);

	// as sounds are now cleared between maps these sounds also need to be
	// reloaded otherwise the known_sfx will go out of sequence for them
	// (this isn't the case any more but it does no harm)
	CL_InitTEnts ();
	S_InitAmbients ();
	LOC_LoadLocations ();

	// decommit any temp allocs which were made during loading
	FreeSpaceBuffers (POOL_FILELOAD | POOL_TEMP);

	// also need it here as demos don't spawn a server!!!
	// this was nasty as it meant that a random memory location was being overwritten by PVS data in demos!
	fatpvs = NULL;

	// see do we need to switch off the menus or console
	if (key_dest != key_game && (cls.demoplayback || cls.demorecording || cls.timedemo))
	{
		Con_RemoveConsole ();
		Menu_RemoveMenu ();

		// switch to game
		key_dest = key_game;
	}

	// activate the mouse and flush the directinput buffers
	IN_ActivateMouse ();
	IN_FlushDInput ();

	// revalidate the skybox in case the last one was cleared
	R_RevalidateSkybox ();
}


/*
============================================================================================================

		NEHAHRA SHOWLMP STUFF

============================================================================================================
*/

// nehahra showlmp stuff; this was UGLY, EVIL and DISGUSTING code.
#define SHOWLMP_MAXLABELS 256

typedef struct showlmp_s
{
	bool		isactive;
	float		x;
	float		y;
	char		label[32];
	char		pic[128];
} showlmp_t;

showlmp_t *showlmp = NULL;
extern bool nehahra;

void SHOWLMP_decodehide (void)
{
	if (!showlmp) showlmp = (showlmp_t *) Pool_Game->Alloc (sizeof (showlmp_t) * SHOWLMP_MAXLABELS);

	char *lmplabel = MSG_ReadString ();

	for (int i = 0; i < SHOWLMP_MAXLABELS; i++)
	{
		if (showlmp[i].isactive && strcmp (showlmp[i].label, lmplabel) == 0)
		{
			showlmp[i].isactive = false;
			return;
		}
	}
}


void SHOWLMP_decodeshow (void)
{
	if (!showlmp) showlmp = (showlmp_t *) Pool_Game->Alloc (sizeof (showlmp_t) * SHOWLMP_MAXLABELS);

	char lmplabel[256], picname[256];

	Q_strncpy (lmplabel, MSG_ReadString (), 255);
	Q_strncpy (picname, MSG_ReadString (), 255);

	float x = MSG_ReadByte ();
	float y = MSG_ReadByte ();

	int k = -1;

	for (int i = 0; i < SHOWLMP_MAXLABELS; i++)
	{
		if (showlmp[i].isactive)
		{
			if (strcmp (showlmp[i].label, lmplabel) == 0)
			{
				// drop out to replace it
				k = i;
				break;
			}
		}
		else if (k < 0)
		{
			// find first empty one to replace
			k = i;
		}
	}

	// none found to replace
	if (k < 0) return;

	// change existing one
	showlmp[k].isactive = true;
	Q_strncpy (showlmp[k].label, lmplabel, 255);
	Q_strncpy (showlmp[k].pic, picname, 255);
	showlmp[k].x = x;
	showlmp[k].y = y;
}


void SHOWLMP_drawall (void)
{
	if (!nehahra) return;
	if (!showlmp) showlmp = (showlmp_t *) Pool_Game->Alloc (sizeof (showlmp_t) * SHOWLMP_MAXLABELS);

	for (int i = 0; i < SHOWLMP_MAXLABELS; i++)
	{
		if (showlmp[i].isactive)
		{
			Draw_Pic (showlmp[i].x, showlmp[i].y, Draw_CachePic (showlmp[i].pic));
		}
	}
}


void SHOWLMP_clear (void)
{
	if (!nehahra) return;
	if (!showlmp) showlmp = (showlmp_t *) Pool_Game->Alloc (sizeof (showlmp_t) * SHOWLMP_MAXLABELS);

	for (int i = 0; i < SHOWLMP_MAXLABELS; i++) showlmp[i].isactive = false;
}


void SHOWLMP_newgame (void)
{
	showlmp = NULL;
}

