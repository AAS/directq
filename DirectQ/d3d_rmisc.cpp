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

		MATRIX OPS

	These happen in pace on the matrix and update it's current values

============================================================================================================
*/

void D3D_TranslateMatrix (D3DMATRIX *matrix, float x, float y, float z)
{
	D3DXMATRIX *m = D3D_MakeD3DXMatrix (matrix);
	D3DXMATRIX tmp;

	QD3DXMatrixTranslation (&tmp, x, y, z);
	QD3DXMatrixMultiply (m, &tmp, m);
}


void D3D_ScaleMatrix (D3DMATRIX *matrix, float x, float y, float z)
{
	D3DXMATRIX *m = D3D_MakeD3DXMatrix (matrix);
	D3DXMATRIX tmp;

	QD3DXMatrixScaling (&tmp, x, y, z);
	QD3DXMatrixMultiply (m, &tmp, m);
}


void D3D_RotateMatrix (D3DMATRIX *matrix, float x, float y, float z, float angle)
{
	D3DXMATRIX *m = D3D_MakeD3DXMatrix (matrix);
	D3DXMATRIX tmp;

	if (x)
	{
		QD3DXMatrixRotationX (&tmp, D3DXToRadian (angle) * x);
		QD3DXMatrixMultiply (m, &tmp, m);
	}

	if (y)
	{
		QD3DXMatrixRotationY (&tmp, D3DXToRadian (angle) * y);
		QD3DXMatrixMultiply (m, &tmp, m);
	}

	if (z)
	{
		QD3DXMatrixRotationZ (&tmp, D3DXToRadian (angle) * z);
		QD3DXMatrixMultiply (m, &tmp, m);
	}
}


D3DMATRIX *D3D_LoadIdentity (D3DMATRIX *matrix)
{
    matrix->m[0][1] = matrix->m[0][2] = matrix->m[0][3] =
    matrix->m[1][0] = matrix->m[1][2] = matrix->m[1][3] =
    matrix->m[2][0] = matrix->m[2][1] = matrix->m[2][3] =
    matrix->m[3][0] = matrix->m[3][1] = matrix->m[3][2] = 0.0f;

    matrix->m[0][0] = matrix->m[1][1] = matrix->m[2][2] = matrix->m[3][3] = 1.0f;

	return matrix;
}


D3DXMATRIX *D3D_LoadIdentity (D3DXMATRIX *matrix)
{
    matrix->m[0][1] = matrix->m[0][2] = matrix->m[0][3] =
    matrix->m[1][0] = matrix->m[1][2] = matrix->m[1][3] =
    matrix->m[2][0] = matrix->m[2][1] = matrix->m[2][3] =
    matrix->m[3][0] = matrix->m[3][1] = matrix->m[3][2] = 0.0f;

    matrix->m[0][0] = matrix->m[1][1] = matrix->m[2][2] = matrix->m[3][3] = 1.0f;

	return matrix;
}


void D3D_MultMatrix (D3DMATRIX *matrix1, D3DMATRIX *matrix2)
{
	D3DXMATRIX *m1 = D3D_MakeD3DXMatrix (matrix1);
	D3DXMATRIX *m2 = D3D_MakeD3DXMatrix (matrix1);

	QD3DXMatrixMultiply (m1, m1, m2);
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
LPD3DXEFFECT d3d_LiquidFX = NULL;
LPD3DXEFFECT d3d_SkyFX = NULL;
LPD3DXEFFECT d3d_ScreenFX = NULL;

// vertex declarations
LPDIRECT3DVERTEXDECLARATION9 d3d_VDXyzTex1 = NULL;
LPDIRECT3DVERTEXDECLARATION9 d3d_VDXyzTex2 = NULL;
LPDIRECT3DVERTEXDECLARATION9 d3d_VDXyz = NULL;
LPDIRECT3DVERTEXDECLARATION9 d3d_VDXyzDiffuseTex1 = NULL;
LPDIRECT3DVERTEXDECLARATION9 d3d_VDXyzDiffuse = NULL;

void D3D_UpgradeShader (char *EffectString, char *old)
{
	for (int i = 0;; i++)
	{
		if (!EffectString[i]) return;

		if (!strnicmp (&EffectString[i], old, 8))
		{
			EffectString[i + 4] = '3';
			return;
		}
	}

	// never reached
}


bool D3D_LoadEffect (char *name, int resourceid, LPD3DXEFFECT *eff, int vsver, int psver)
{
	char *EffectString = NULL;
	LPD3DXBUFFER errbuf = NULL;

	// load the resource - note - we don't use D3DXCreateEffectFromResource as importing an RCDATA resource
	// from a text file in Visual Studio doesn't NULL terminate the file, causing it to blow up.
	int len = Sys_LoadResourceData (resourceid, (void **) &EffectString);

	// allow a higher vs level in case we have software emulated vs
	if (vsver >= 3) D3D_UpgradeShader (EffectString, " vs_2_0 ");
	if (psver >= 3) D3D_UpgradeShader (EffectString, " ps_2_0 ");

	// basline flags
	DWORD ShaderFlags = D3DXSHADER_SKIPVALIDATION | D3DXFX_DONOTSAVESTATE;

	// add extra flags if they're available
#ifdef D3DXFX_NOT_CLONEABLE
	ShaderFlags |= D3DXFX_NOT_CLONEABLE;
#endif

#ifdef D3DXSHADER_OPTIMIZATION_LEVEL3
	ShaderFlags |= D3DXSHADER_OPTIMIZATION_LEVEL3;
#endif

	hr = QD3DXCreateEffect
	(
		d3d_Device,
		EffectString,
		len,
		NULL,
		NULL,
		ShaderFlags,
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


void D3D_CreateVertDeclFromFVFCode (DWORD fvf, LPDIRECT3DVERTEXDECLARATION9 *vd)
{
	D3DVERTEXELEMENT9 *vdlayout = (D3DVERTEXELEMENT9 *) scratchbuf;
	D3DVERTEXELEMENT9 enddecl[] = {D3DDECL_END ()};

	// word is that some versions of D3DXDeclaratorFromFVF don't append a D3DDECL_END
	// so here we fill our layout with them (because we don't know how long the decl is gonna be)
	for (int i = 0; i < MAX_FVF_DECL_SIZE; i++)
		Q_MemCpy (&vdlayout[i], enddecl, sizeof (D3DVERTEXELEMENT9));

	if (FAILED (QD3DXDeclaratorFromFVF (fvf, vdlayout)))
		Sys_Error ("D3D_CreateVertDeclFromFVFCode: failed to create a vertex declaration");

	if (FAILED (d3d_Device->CreateVertexDeclaration (vdlayout, vd)))
		Sys_Error ("D3D_CreateVertDeclFromFVFCode: failed to create a vertex declaration");
}


void D3D_InitHLSL (void)
{
	// done first so that even if hlsl is unavailable these will be
	D3D_CreateVertDeclFromFVFCode (D3DFVF_XYZ, &d3d_VDXyz);
	D3D_CreateVertDeclFromFVFCode (D3DFVF_XYZ | D3DFVF_TEX1, &d3d_VDXyzTex1);
	D3D_CreateVertDeclFromFVFCode (D3DFVF_XYZ | D3DFVF_TEX2, &d3d_VDXyzTex2);
	D3D_CreateVertDeclFromFVFCode (D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1, &d3d_VDXyzDiffuseTex1);
	D3D_CreateVertDeclFromFVFCode (D3DFVF_XYZ | D3DFVF_DIFFUSE, &d3d_VDXyzDiffuse);

	// now set up effects
	vs_version = (char *) QD3DXGetVertexShaderProfile (d3d_Device);
	ps_version = (char *) QD3DXGetPixelShaderProfile (d3d_Device);
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

	if (vsvermaj < 2 || psvermaj < 2 || vsvermaj < psvermaj)
	{
		Con_Printf ("Vertex or Pixel Shaders version is too low.\n");
		return;
	}

	// load effects - if we get this far we know that pixel shaders are available
	if (!D3D_LoadEffect ("Liquid Shader", IDR_LIQUID, &d3d_LiquidFX, vsvermaj, psvermaj)) return;
	if (!D3D_LoadEffect ("Sky Shader", IDR_SKY, &d3d_SkyFX, vsvermaj, psvermaj)) return;
	if (!D3D_LoadEffect ("Screen Shader", IDR_SCREEN, &d3d_ScreenFX, vsvermaj, psvermaj)) return;

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
	SAFE_RELEASE (d3d_ScreenFX);

	// declarations
	SAFE_RELEASE (d3d_VDXyzTex1);
	SAFE_RELEASE (d3d_VDXyzTex2);
	SAFE_RELEASE (d3d_VDXyz);
	SAFE_RELEASE (d3d_VDXyzDiffuseTex1);
	SAFE_RELEASE (d3d_VDXyzDiffuse);
}


/*
============================================================================================================

		BRUSHMODEL TEXTURE REGISTRATION

============================================================================================================
*/

d3d_registeredtexture_t **d3d_RegisteredTextures = NULL;
int d3d_NumRegisteredTextures = 0;
int d3d_MaxRegisteredTextures = 0;

void D3D_RegisterTexture (texture_t *tex)
{
	// see does it already exist (the D3D texture is good for use as a unique signifier here)
	for (int i = 0; i < d3d_NumRegisteredTextures; i++)
	{
		if ((int) d3d_RegisteredTextures[i]->texture->teximage == (int) tex->teximage)
		{
			// ensure that we catch the same texture used in different bmodels
			tex->registration = d3d_RegisteredTextures[i];
			return;
		}
	}

	// if we have more than 65536 textures we're really in bad luck
	if (d3d_NumRegisteredTextures >= d3d_MaxRegisteredTextures) return;

	// register this texture
	d3d_RegisteredTextures[d3d_NumRegisteredTextures] = (d3d_registeredtexture_t *) MainHunk->Alloc (sizeof (d3d_registeredtexture_t));
	d3d_RegisteredTextures[d3d_NumRegisteredTextures]->texture = tex;
	d3d_RegisteredTextures[d3d_NumRegisteredTextures]->surfchain = NULL;

	// store the registration back to the texture
	tex->registration = d3d_RegisteredTextures[d3d_NumRegisteredTextures];

	// go to the next registration
	d3d_NumRegisteredTextures++;
}


void D3D_RegisterTextures (void)
{
	d3d_RegisteredTextures = (d3d_registeredtexture_t **) scratchbuf;
	d3d_NumRegisteredTextures = 0;
	d3d_MaxRegisteredTextures = SCRATCHBUF_SIZE / sizeof (d3d_registeredtexture_t *);

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

	// set them up for real
	d3d_RegisteredTextures = (d3d_registeredtexture_t **) MainHunk->Alloc (d3d_NumRegisteredTextures * sizeof (d3d_registeredtexture_t *));
	Q_MemCpy (d3d_RegisteredTextures, scratchbuf, d3d_NumRegisteredTextures * sizeof (d3d_registeredtexture_t *));
}


/*
============================================================================================================

		ALPHA SORTING

============================================================================================================
*/

#define MAX_ALPHA_ITEMS		65536

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
		void *data;
	};
} d3d_alphalist_t;


#define D3D_ALPHATYPE_ENTITY		1
#define D3D_ALPHATYPE_PARTICLE		2
#define D3D_ALPHATYPE_WATERWARP		3

d3d_alphalist_t **d3d_AlphaList = NULL;
int d3d_NumAlphaList = 0;


void D3D_AlphaListNewMap (void)
{
	d3d_AlphaList = (d3d_alphalist_t **) MainHunk->Alloc (MAX_ALPHA_ITEMS * sizeof (d3d_alphalist_t *));
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


void D3D_AddToAlphaList (int type, void *data, float dist)
{
	if (d3d_NumAlphaList == MAX_ALPHA_ITEMS) return;
	if (!d3d_AlphaList[d3d_NumAlphaList]) d3d_AlphaList[d3d_NumAlphaList] = (d3d_alphalist_t *) MainHunk->Alloc (sizeof (d3d_alphalist_t));

	d3d_AlphaList[d3d_NumAlphaList]->Type = type;
	d3d_AlphaList[d3d_NumAlphaList]->data = data;
	d3d_AlphaList[d3d_NumAlphaList]->Dist = dist;

	d3d_NumAlphaList++;
}


void D3D_AddToAlphaList (entity_t *ent)
{
	D3D_AddToAlphaList (D3D_ALPHATYPE_ENTITY, ent, D3D_GetDist (ent->origin));
}


void D3D_AddToAlphaList (d3d_modelsurf_t *modelsurf)
{
	// we only support turb surfaces for now
	if (modelsurf->surf->flags & SURF_DRAWTURB) D3D_AddToAlphaList (D3D_ALPHATYPE_WATERWARP, modelsurf, D3D_GetDist (modelsurf->surf->midpoint));
}


void D3D_AddToAlphaList (particle_type_t *particle)
{
	D3D_AddToAlphaList (D3D_ALPHATYPE_PARTICLE, particle, D3D_GetDist (particle->spawnorg));
}


int D3D_AlphaSortFunc (const void *a, const void *b)
{
	d3d_alphalist_t *al1 = *(d3d_alphalist_t **) a;
	d3d_alphalist_t *al2 = *(d3d_alphalist_t **) b;

	// back to front ordering
	return (int) (al2->Dist - al1->Dist);
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


void D3D_RenderAlphaList (void)
{
	// nothing to add
	if (!d3d_NumAlphaList) return;

	// sort the alpha list
	if (d3d_NumAlphaList == 1)
		; // no need to sort
	else if (d3d_NumAlphaList == 2)
	{
		// exchange if necessary
		if (d3d_AlphaList[1]->Dist > d3d_AlphaList[0]->Dist)
		{
			// exchange - this was [1] and [2] - how come it never crashed?
			d3d_alphalist_t *Temp = d3d_AlphaList[0];
			d3d_AlphaList[0] = d3d_AlphaList[1];
			d3d_AlphaList[1] = Temp;
		}
	}
	else
	{
		// sort fully
		qsort
		(
			d3d_AlphaList,
			d3d_NumAlphaList,
			sizeof (d3d_alphalist_t *),
			D3D_AlphaSortFunc
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
		if (d3d_AlphaList[i]->Type != previous)
		{
			D3D_AlphaListStageChange (previous, d3d_AlphaList[i]->Type);
			previous = d3d_AlphaList[i]->Type;
		}

		switch (d3d_AlphaList[i]->Type)
		{
		case D3D_ALPHATYPE_ENTITY:
			if (d3d_AlphaList[i]->Entity->model->type == mod_alias)
			{
				// the viewent needs to write to Z
				if (d3d_AlphaList[i]->Entity == cl_entities[cl.viewentity] && chase_active.value) D3D_SetRenderState (D3DRS_ZWRITEENABLE, TRUE);
				D3D_DrawAliasBatch (&d3d_AlphaList[i]->Entity, 1);
				if (d3d_AlphaList[i]->Entity == cl_entities[cl.viewentity] && chase_active.value) D3D_SetRenderState (D3DRS_ZWRITEENABLE, FALSE);
			}
			else if (d3d_AlphaList[i]->Entity->model->type == mod_brush)
				D3D_DrawAlphaBrushModel (d3d_AlphaList[i]->Entity);
			else if (d3d_AlphaList[i]->Entity->model->type == mod_sprite)
				D3D_SetupSpriteModel (d3d_AlphaList[i]->Entity);

			break;

		case D3D_ALPHATYPE_PARTICLE:
			R_AddParticleTypeToRender (d3d_AlphaList[i]->Particle);
			break;

		case D3D_ALPHATYPE_WATERWARP:
			D3D_EmitWarpSurface (d3d_AlphaList[i]->ModelSurf);

			break;

		default:
			// nothing to add
			break;
		}
	}

	// take down the final state used (in case it was a HLSL state)
	D3D_AlphaListStageChange (d3d_AlphaList[d3d_NumAlphaList - 1]->Type, 0);

	// disable blending (done)
	// the cull type may have been modified going through here so put it back the way it was
	D3D_BackfaceCull (D3DCULL_CCW);
	D3D_SetRenderState (D3DRS_ZWRITEENABLE, TRUE);
	D3D_SetRenderState (D3DRS_ALPHABLENDENABLE, FALSE);

	// reset alpha list
	d3d_NumAlphaList = 0;
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
	r_notexture_mip = (texture_t *) Zone_Alloc (sizeof (texture_t) + 4 * 4);

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

extern LPDIRECT3DTEXTURE9 R_PaletteTexture;

LPDIRECT3DTEXTURE9 r_blacktexture = NULL;
LPDIRECT3DTEXTURE9 r_greytexture = NULL;

void Draw_FreeCrosshairs (void);

void R_ReleaseResourceTextures (void)
{
	SAFE_RELEASE (particledottexture);

	SAFE_RELEASE (crosshairtexture);
	SAFE_RELEASE (r_blacktexture);
	SAFE_RELEASE (r_greytexture);

	// and replacement crosshairs too
	Draw_FreeCrosshairs ();
}


void R_InitResourceTextures (void)
{
	// load any textures contained in exe resources
	D3D_LoadResourceTexture (&particledottexture, IDR_PARTICLEDOT, IMAGE_MIPMAP);

	D3D_LoadResourceTexture (&crosshairtexture, IDR_CROSSHAIR, 0);
	D3D_LoadResourceTexture (&yahtexture, IDR_YOUAREHERE, 0);

	// create a texture for the palette
	// this is to save on state changes when a flat colour is needed; rather than
	// switching off texturing and other messing, we just draw this one.
	byte *paldata = (byte *) scratchbuf;

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
	Q_MemSet (paldata, 0, 128 * 128);

	// load the black texture - we must mipmap this and also load it as 32 bit
	// (in case palette index 0 isn't black).  also load it really really small...
	D3D_UploadTexture (&r_blacktexture, paldata, 4, 4, IMAGE_MIPMAP);

	// clear to grey
	Q_MemSet (paldata, 128, 128 * 128);

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

// allow the old QER names as aliases
cvar_alias_t r_interpolate_model_animation ("r_interpolate_model_animation", &r_lerpframe);
cvar_alias_t r_interpolate_model_transform ("r_interpolate_model_transform", &r_lerporient);

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
		d3d_PlayerSkins[i].LastUsage = 0;
	}
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
void D3D_InitSubdivision (void);
void D3D_ModelSurfsBeginMap (void);

void R_NewMap (void)
{
	// we're going to be creating some default pool resources here, so
	// evict the managed resources to get the default stuff into best locations
	d3d_Device->EvictManagedResources ();

	// init frame counters
	d3d_RenderDef.skyframe = -1;
	d3d_RenderDef.framecount = 1;
	d3d_RenderDef.visframecount = 0;

	// normal light value
	for (int i = 0; i < 256; i++) d_lightstylevalue[i] = 256;

	// clear out efrags (one short???)
	for (int i = 0; i < cl.worldmodel->brushhdr->numleafs; i++)
		cl.worldmodel->brushhdr->leafs[i].efrags = NULL;

	// world entity baseline
	Q_MemSet (&d3d_RenderDef.worldentity, 0, sizeof (entity_t));
	d3d_RenderDef.worldentity.model = cl.worldmodel;
	cl.worldmodel->cacheent = &d3d_RenderDef.worldentity;
	d3d_RenderDef.worldentity.alphaval = 255;

	// fix up the worldmodel surfaces so it's consistent and we can reuse code
	cl.worldmodel->brushhdr->firstmodelsurface = 0;
	cl.worldmodel->brushhdr->nummodelsurfaces = cl.worldmodel->brushhdr->numsurfaces;

	// init edict pools
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
	D3D_InitSubdivision ();
	D3D_ModelSurfsBeginMap ();
	D3D_AlphaListNewMap ();

	// release cached skins to save memory
	for (int i = 0; i < 256; i++) SAFE_RELEASE (d3d_PlayerSkins[i].d3d_Texture);

	// as sounds are now cleared between maps these sounds also need to be
	// reloaded otherwise the known_sfx will go out of sequence for them
	// (this isn't the case any more but it does no harm)
	CL_InitTEnts ();
	S_InitAmbients ();
	LOC_LoadLocations ();

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
	if (!showlmp) showlmp = (showlmp_t *) GameZone->Alloc (sizeof (showlmp_t) * SHOWLMP_MAXLABELS);

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
	if (!showlmp) showlmp = (showlmp_t *) GameZone->Alloc (sizeof (showlmp_t) * SHOWLMP_MAXLABELS);

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
	if (!showlmp) showlmp = (showlmp_t *) GameZone->Alloc (sizeof (showlmp_t) * SHOWLMP_MAXLABELS);

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
	if (!showlmp) showlmp = (showlmp_t *) GameZone->Alloc (sizeof (showlmp_t) * SHOWLMP_MAXLABELS);

	for (int i = 0; i < SHOWLMP_MAXLABELS; i++) showlmp[i].isactive = false;
}


void SHOWLMP_newgame (void)
{
	showlmp = NULL;
}

