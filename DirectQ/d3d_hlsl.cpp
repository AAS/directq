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
#include "d3d_quake.h"
#include "d3d_hlsl.h"
#include "winquake.h"
#include "resource.h"

// filtering of redundant shader state changes
// let's give ourselves a bit more control over what's happening in our effects
class CFXManager : ID3DXEffectStateManager
{
private:
	LPDIRECT3DBASETEXTURE9 CurrentTexture[4];
	LPDIRECT3DVERTEXSHADER9 CurrentVertexShader;
	LPDIRECT3DPIXELSHADER9 CurrentPixelShader;
	DWORD CurrentAddressU[4];
	DWORD CurrentAddressV[4];
	DWORD CurrentFVF;
	int RefCount;

public:
	CFXManager (void)
	{
		// need a refcount of 1 as it's instantiated as a non-pointer instance.
		this->RefCount = 1;

		// initial states
		this->CurrentTexture[0] = this->CurrentTexture[1] = this->CurrentTexture[2] = this->CurrentTexture[3] = NULL;
		this->CurrentPixelShader = NULL;
		this->CurrentVertexShader = NULL;
		this->CurrentAddressU[0] = this->CurrentAddressU[1] = this->CurrentAddressU[2] = this->CurrentAddressU[3] = (DWORD) -1;
		this->CurrentAddressV[0] = this->CurrentAddressV[1] = this->CurrentAddressV[2] = this->CurrentAddressV[3] = (DWORD) -1;
		this->CurrentFVF = (DWORD) -1;
	}

	HRESULT CALLBACK QueryInterface (REFIID riid, LPVOID *ppvObj)
	{
        if (riid == IID_IUnknown || riid == IID_ID3DXEffectStateManager)
		{
			this->AddRef ();
			*(ID3DXEffectStateManager **) ppvObj = this;
			return S_OK;
		}

		*ppvObj = NULL;
		return E_NOINTERFACE;
	}

	ULONG CALLBACK AddRef (void)
	{
		return (++(this->RefCount));
	}

	ULONG CALLBACK Release (void)
	{
		this->RefCount--;

		if (this->RefCount == 0)
		{
			delete this;
			return 0;
		}

		return this->RefCount;
	}

	HRESULT CALLBACK LightEnable (DWORD Index, BOOL Enable)
	{
		d3d_Device->LightEnable (Index, Enable);
		return S_OK;
	}

	HRESULT CALLBACK SetFVF (DWORD FVF)
	{
		if (this->CurrentFVF == FVF)
			return S_OK;

		d3d_Device->SetFVF (FVF);
		this->CurrentFVF = FVF;
		return S_OK;
	}

	HRESULT CALLBACK SetLight (DWORD Index, CONST D3DLIGHT9 *pLight)
	{
		d3d_Device->SetLight (Index, pLight);
		return S_OK;
	}

	HRESULT CALLBACK SetMaterial (CONST D3DMATERIAL9 *pMaterial)
	{
		d3d_Device->SetMaterial (pMaterial);
		return S_OK;
	}

	HRESULT CALLBACK SetNPatchMode (FLOAT nSegments)
	{
		d3d_Device->SetNPatchMode (nSegments);
		return S_OK;
	}

	HRESULT CALLBACK SetPixelShader (LPDIRECT3DPIXELSHADER9 pShader)
	{
		if (pShader == this->CurrentPixelShader) return S_OK;

		d3d_Device->SetPixelShader (pShader);
		this->CurrentPixelShader = pShader;
		return S_OK;
	}

	HRESULT CALLBACK SetPixelShaderConstantB (UINT StartRegister, CONST BOOL *pConstantData, UINT RegisterCount)
	{
		d3d_Device->SetPixelShaderConstantB (StartRegister, pConstantData, RegisterCount);
		return S_OK;
	}

	HRESULT CALLBACK SetPixelShaderConstantF (UINT StartRegister, CONST FLOAT *pConstantData, UINT RegisterCount)
	{
		d3d_Device->SetPixelShaderConstantF (StartRegister, pConstantData, RegisterCount);
		return S_OK;
	}

	HRESULT CALLBACK SetPixelShaderConstantI (UINT StartRegister, CONST INT *pConstantData, UINT RegisterCount)
	{
		d3d_Device->SetPixelShaderConstantI (StartRegister, pConstantData, RegisterCount);
		return S_OK;
	}

	HRESULT CALLBACK SetRenderState (D3DRENDERSTATETYPE State, DWORD Value)
	{
		d3d_Device->SetRenderState (State, Value);
		return S_OK;
	}

	HRESULT CALLBACK SetSamplerState (DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD Value)
	{
		// these are the only sampler states i use in my shaders;
		// this can be added to if i ever decide to use any more...
		if (Type == D3DSAMP_ADDRESSU)
		{
			if (this->CurrentAddressU[Sampler] == Value)
				return S_OK;

			this->CurrentAddressU[Sampler] = Value;
		}
		else if (Type == D3DSAMP_ADDRESSV)
		{
			if (this->CurrentAddressV[Sampler] == Value)
				return S_OK;

			this->CurrentAddressV[Sampler] = Value;
		}

		d3d_Device->SetSamplerState (Sampler, Type, Value);
		return S_OK;
	}

	HRESULT CALLBACK SetTexture (DWORD Stage, LPDIRECT3DBASETEXTURE9 pTexture)
	{
		if (Stage > 3) return S_OK;

		// in *theory* this should never happen as the effect classes manage texture switching too,
		// but in practice it does.
		if (this->CurrentTexture[Stage] == pTexture)
			return S_OK;

		d3d_Device->SetTexture (Stage, pTexture);
		this->CurrentTexture[Stage] = pTexture;
		return S_OK;
	}

	HRESULT CALLBACK SetTextureStageState (DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value)
	{
		d3d_Device->SetTextureStageState (Stage, Type, Value);
		return S_OK;
	}

	HRESULT CALLBACK SetTransform (D3DTRANSFORMSTATETYPE State, CONST D3DMATRIX *pMatrix)
	{
		d3d_Device->SetTransform (State, pMatrix);
		return S_OK;
	}

	HRESULT CALLBACK SetVertexShader (LPDIRECT3DVERTEXSHADER9 pShader)
	{
		if (pShader == this->CurrentVertexShader)
			return S_OK;

		d3d_Device->SetVertexShader (pShader);
		this->CurrentVertexShader = pShader;
		return S_OK;
	}

	HRESULT CALLBACK SetVertexShaderConstantB (UINT StartRegister, CONST BOOL *pConstantData, UINT RegisterCount)
	{
		d3d_Device->SetVertexShaderConstantB (StartRegister, pConstantData, RegisterCount);
		return S_OK;
	}

	HRESULT CALLBACK SetVertexShaderConstantF (UINT StartRegister, CONST FLOAT *pConstantData, UINT RegisterCount)
	{
		d3d_Device->SetVertexShaderConstantF (StartRegister, pConstantData, RegisterCount);
		return S_OK;
	}

	HRESULT CALLBACK SetVertexShaderConstantI (UINT StartRegister, CONST INT *pConstantData, UINT RegisterCount)
	{
		d3d_Device->SetVertexShaderConstantI (StartRegister, pConstantData, RegisterCount);
		return S_OK;
	}
};


CFXManager FXManager;

bool SilentLoad = false;

// keep these global as we'll want to use them in a few places
char *vs_version;
char *ps_version;

cvar_t r_defaultshaderprecision ("r_defaultshaderprecision", "0", CVAR_ARCHIVE);
cvar_t r_warpshaderprecision ("r_warpshaderprecision", "0", CVAR_ARCHIVE);

void D3D_MakeVertexDeclaration (int stream, int numverts, int numcolor, int numst, LPDIRECT3DVERTEXDECLARATION9 *d3d_VD)
{
	// set up the vertex element struct array
	D3DVERTEXELEMENT9 *ve = (D3DVERTEXELEMENT9 *) Heap_QMalloc (sizeof (D3DVERTEXELEMENT9) * (numverts + numcolor + numst + 1));

	// current item
	D3DVERTEXELEMENT9 *currentve = ve;
	int ofs = 0;

	for (int i = 0; i < numverts; i++, currentve++)
	{
		// fill 'er in
		currentve->Stream = stream;
		currentve->Offset = ofs;
		currentve->Type = D3DDECLTYPE_FLOAT3;
		currentve->Method = D3DDECLMETHOD_DEFAULT;
		currentve->Usage = D3DDECLUSAGE_POSITION;
		currentve->UsageIndex = i;

		// next offset
		ofs += 12;
	}

	for (int i = 0; i < numcolor; i++, currentve++)
	{
		// fill 'er in
		currentve->Stream = stream;
		currentve->Offset = ofs;
		currentve->Type = D3DDECLTYPE_D3DCOLOR;
		currentve->Method = D3DDECLMETHOD_DEFAULT;
		currentve->Usage = D3DDECLUSAGE_COLOR;
		currentve->UsageIndex = i;

		// next offset
		ofs += 4;
	}

	for (int i = 0; i < numst; i++, currentve++)
	{
		// fill 'er in
		currentve->Stream = stream;
		currentve->Offset = ofs;
		currentve->Type = D3DDECLTYPE_FLOAT2;
		currentve->Method = D3DDECLMETHOD_DEFAULT;
		currentve->Usage = D3DDECLUSAGE_TEXCOORD;
		currentve->UsageIndex = i;

		// next offset
		ofs += 8;
	}

	// terminate it (D3DDECL_END)
	currentve->Stream = 0xff;
	currentve->Offset = 0;
	currentve->Type = D3DDECLTYPE_UNUSED;
	currentve->Method = 0;
	currentve->Usage = 0;
	currentve->UsageIndex = 0;

	// create the declaration
	d3d_Device->CreateVertexDeclaration (ve, d3d_VD);

	// done
	Heap_QFree (ve);
}


// effects
CD3DEffect d3d_InstancedBrushFX;
CD3DEffect d3d_BrushFX;
CD3DEffect d3d_LiquidFX;
CD3DEffect d3d_AliasFX;
CD3DEffect d3d_Flat2DFX;
CD3DEffect d3d_SkyFX;
CD3DEffect d3d_SpriteFX;
CD3DEffect d3d_ParticleFX;
CD3DEffect d3d_UnderwaterFX;

// vertex declarations
LPDIRECT3DVERTEXDECLARATION9 d3d_AliasVertexDeclaration = NULL;
LPDIRECT3DVERTEXDECLARATION9 d3d_ParticleVertexDeclaration = NULL;
LPDIRECT3DVERTEXDECLARATION9 d3d_V3NoSTDeclaration = NULL;
LPDIRECT3DVERTEXDECLARATION9 d3d_V3ST2Declaration = NULL;
LPDIRECT3DVERTEXDECLARATION9 d3d_V3ST4Declaration = NULL;

void D3D_LoadEffect (char *name, int resourceid, LPD3DXEFFECT *eff, bool iswarpeffect = false)
{
	int shaderprecisionbit = 0;
	int cvarinteger = 0;

	if (iswarpeffect)
		cvarinteger = r_warpshaderprecision.integer;
	else cvarinteger = r_defaultshaderprecision.integer;

	if (cvarinteger == 1)
	{
		// force partial precision
		shaderprecisionbit = D3DXSHADER_PARTIALPRECISION;
	}
	else if (cvarinteger < 1)
	{
		// auto detect
		// nvidia uses 16 bits with partial precision, which is not enough for DirectQ warp surfs
		if (d3d_GlobalCaps.isNvidia && iswarpeffect)
			shaderprecisionbit = 0;
		else shaderprecisionbit = D3DXSHADER_PARTIALPRECISION;
	}
	else
	{
		// force full precision
		shaderprecisionbit = 0;
	}

	// because there are so many places this can go wrong we'll use exception handling on it rather
	// than creating a mess of error-checking.
	try
	{
		if (!name)
		{
			Sys_Error ("D3D_LoadEffect: File name alternate for shader creation not specified!");
			return;
		}

		// first off we try to create the shader from a file, so that we'll be able to let
		// users play with their own shaders if they like.  if that fails we'll create it from
		// a resource instead to use a pre-canned shader.
		FILE *sf = NULL;
		int len = 0;
		char *EffectString = NULL;
		HRESULT hr;
		LPD3DXBUFFER errbuf = NULL;

		len = COM_FOpenFile (name, &sf);

		if (sf)
		{
			// alloc and read in the effect string
			EffectString = (char *) Heap_QMalloc (len + 1);
			fread (EffectString, len, 1, sf);

			hr = D3DXCreateEffect
			(
				d3d_Device,
				EffectString,
				len,
				NULL,
				NULL,
				D3DXFX_NOT_CLONEABLE | D3DXSHADER_OPTIMIZATION_LEVEL3 | D3DXSHADER_SKIPVALIDATION | shaderprecisionbit,
				NULL,
				eff,
				&errbuf
			);

			// clean up
			fclose (sf);
			Heap_QFree (EffectString);
			EffectString = NULL;

			if (SUCCEEDED (hr))
			{
				if (!SilentLoad) Con_Printf ("Loaded effect \"%s\" from disk OK\n", name);
				SAFE_RELEASE (errbuf);
				return;
			}
			else if (!SilentLoad)
			{
				char *errstr = (char *) errbuf->GetBufferPointer ();
				Con_Printf ("D3D_LoadEffect: Error compiling %s\n%s", name, errstr);
				SAFE_RELEASE (errbuf);
			}
		}

		// load the resource - note - we don't use D3DXCreateEffectFromResource as importing an RCDATA resource
		// from a text file in Visual Studio doesn't NULL terminate the file, causing it to blow up.
		HRSRC hResInfo = FindResource (NULL, MAKEINTRESOURCE (resourceid), RT_RCDATA);
		HGLOBAL hResData = LoadResource (NULL, hResInfo);
		EffectString = (char *) LockResource (hResData);
		len = SizeofResource (NULL, hResInfo);

		hr = D3DXCreateEffect
		(
			d3d_Device,
			EffectString,
			len,
			NULL,
			NULL,
			D3DXFX_NOT_CLONEABLE | D3DXSHADER_OPTIMIZATION_LEVEL3 | D3DXSHADER_SKIPVALIDATION | shaderprecisionbit,
			NULL,
			eff,
			&errbuf
		);

		// clean up (we're probably leaking memory here somewhere, but unless we do REALLY LOADS of resolution
		// changes in-game, it's not so much that anyone would notice...)
		UnlockResource (hResData);

		if (FAILED (hr))
		{
			char *errstr = (char *) errbuf->GetBufferPointer ();
			Sys_Error ("D3D_LoadEffect: Error compiling %s\n%s", name, errstr);
			errbuf->Release ();
			return;
		}

		SAFE_RELEASE (errbuf);
		if (!SilentLoad) Con_Printf ("Created built-in effect \"%s\" OK\n", name);
	}
	catch (...)
	{
		// oh well...
		Sys_Error ("D3D_LoadEffect: General error");
	}
}


void D3D_InitHLSL (void)
{
	vs_version = (char *) D3DXGetVertexShaderProfile (d3d_Device);
	ps_version = (char *) D3DXGetPixelShaderProfile (d3d_Device);

	if (!SilentLoad) Con_Printf ("\n");

	if (vs_version)
	{
		if (!SilentLoad) Con_Printf ("Vertex Shader Version: %s\n", vs_version);
	}
	else
	{
		// note - we expect this to never happen as D3D will create vertex shaders in software
		// for devices that don't support them in hardware.
		Sys_Error ("Vertex Shaders Not Available\n");
		return;
	}

	if (ps_version)
	{
		if (!SilentLoad) Con_Printf ("Pixel Shader Version: %s\n", ps_version);
	}
	else
	{
		// note - we expect this to never happen as D3D will create vertex shaders in software
		// for devices that don't support them in hardware.
		Sys_Error ("Pixel Shaders Not Available\n");
		return;
	}

	if (!SilentLoad) Con_Printf ("\n");

	// load effects
	d3d_AliasFX.LoadEffect ("shaders/AliasModel.fx", IDR_ALIAS);
	d3d_BrushFX.LoadEffect ("shaders/Brush.fx", IDR_BRUSH);
	d3d_Flat2DFX.LoadEffect ("shaders/Flat2D.fx", IDR_FLAT2D);
	d3d_InstancedBrushFX.LoadEffect ("shaders/InstancedBrush.fx", IDR_INSTANCEDBRUSH);
	d3d_LiquidFX.LoadEffect ("shaders/Liquid.fx", IDR_LIQUID, true);
	d3d_ParticleFX.LoadEffect ("shaders/Particle.fx", IDR_PARTICLE);
	d3d_SkyFX.LoadEffect ("shaders/Sky.fx", IDR_SKY);
	d3d_SpriteFX.LoadEffect ("shaders/Sprite.fx", IDR_SPRITE);
	d3d_UnderwaterFX.LoadEffect ("shaders/Underwater.fx", IDR_UNDERWATER);

	// only display output on the first load
	SilentLoad = true;

	// set up vertex declarations
	D3D_MakeVertexDeclaration (0, 2, 1, 2, &d3d_AliasVertexDeclaration);
	D3D_MakeVertexDeclaration (0, 1, 1, 1, &d3d_ParticleVertexDeclaration);
	D3D_MakeVertexDeclaration (0, 1, 0, 1, &d3d_V3ST2Declaration);
	D3D_MakeVertexDeclaration (0, 1, 0, 2, &d3d_V3ST4Declaration);
	D3D_MakeVertexDeclaration (0, 3, 0, 0, &d3d_V3NoSTDeclaration);

	// get all handles for all parameters for all effects
	d3d_Flat2DFX.GetWPMatrixHandle ("WorldProjMatrix");
	d3d_Flat2DFX.GetColor4fHandle ("BaseColor");
	d3d_Flat2DFX.GetTextureHandle ("baseTexture");

	d3d_LiquidFX.GetWPMatrixHandle ("WorldProjMatrix");
	d3d_LiquidFX.GetEntMatrixHandle ("ModelTransform");
	d3d_LiquidFX.GetTimeHandle ("warptime");
	d3d_LiquidFX.GetAlphaHandle ("Alpha");
	d3d_LiquidFX.GetTextureHandle ("baseTexture");

	d3d_BrushFX.GetWPMatrixHandle ("WorldProjMatrix");
	d3d_BrushFX.GetEntMatrixHandle ("ModelTransform");
	d3d_BrushFX.GetScaleHandle ("LightScale");
	d3d_BrushFX.GetTextureHandle (0, "baseTexture");
	d3d_BrushFX.GetTextureHandle (1, "lightTexture");
	d3d_BrushFX.GetTextureHandle (2, "lumaTexture");

	d3d_InstancedBrushFX.GetWPMatrixHandle ("WorldProjMatrix");
	d3d_InstancedBrushFX.GetEntMatrixHandle ("ModelTransform");
	d3d_InstancedBrushFX.GetColor4fHandle ("Light");
	d3d_InstancedBrushFX.GetTimeHandle ("warptime");
	d3d_InstancedBrushFX.GetTextureHandle (0, "baseTexture");
	d3d_InstancedBrushFX.GetTextureHandle (1, "lumaTexture");
	d3d_InstancedBrushFX.GetScaleHandle ("colourscale");

	d3d_UnderwaterFX.GetWPMatrixHandle ("WorldProjMatrix");
	d3d_UnderwaterFX.GetTimeHandle ("WarpTime");
	d3d_UnderwaterFX.GetScaleHandle ("WarpScale");
	d3d_UnderwaterFX.GetTextureHandle ("baseTexture");

	// hack - rather than create 2 new handles with getters and setters
	// for them, we instead reuse some of what's already there
	d3d_SkyFX.GetWPMatrixHandle ("WorldProjMatrix");
	d3d_SkyFX.GetScaleHandle ("backscroll");
	d3d_SkyFX.GetTimeHandle ("frontscroll");
	d3d_SkyFX.GetTextureHandle (0, "backTexture");
	d3d_SkyFX.GetTextureHandle (1, "frontTexture");
	d3d_SkyFX.GetAlphaHandle ("skyalpha");

	d3d_AliasFX.GetWPMatrixHandle ("WorldProjMatrix");
	d3d_AliasFX.GetTextureHandle (0, "baseTexture");
	d3d_AliasFX.GetTextureHandle (1, "lumaTexture");
	d3d_AliasFX.GetScaleHandle ("colourscale");

	d3d_ParticleFX.GetWPMatrixHandle ("WorldProjMatrix");
	d3d_ParticleFX.GetTextureHandle ("particleTexture");

	d3d_SpriteFX.GetWPMatrixHandle ("WorldProjMatrix");
	d3d_SpriteFX.GetTextureHandle ("baseTexture");
}


void D3D_ShutdownHLSL (void)
{
	// effects
	d3d_InstancedBrushFX.Release ();
	d3d_BrushFX.Release ();
	d3d_LiquidFX.Release ();
	d3d_AliasFX.Release ();
	d3d_Flat2DFX.Release ();
	d3d_SkyFX.Release ();
	d3d_SpriteFX.Release ();
	d3d_ParticleFX.Release ();
	d3d_UnderwaterFX.Release ();

	// declarations
	SAFE_RELEASE (d3d_AliasVertexDeclaration);
	SAFE_RELEASE (d3d_ParticleVertexDeclaration);
	SAFE_RELEASE (d3d_V3ST2Declaration);
	SAFE_RELEASE (d3d_V3ST4Declaration);
	SAFE_RELEASE (d3d_V3NoSTDeclaration);
}


CD3DEffect::CD3DEffect (void)
{
	this->TheEffect = NULL;
	this->IsWarpEffect = false;
	this->ValidFX = false;
}


void CD3DEffect::LoadEffect (char *name, int resourceid, bool iswarpeffect)
{
	this->Release ();
	this->IsWarpEffect = iswarpeffect;
	D3D_LoadEffect (name, resourceid, &this->TheEffect, iswarpeffect);
	this->ValidFX = true;
	strncpy (this->Name, name, 254);

	this->TheEffect->SetStateManager ((LPD3DXEFFECTSTATEMANAGER) &FXManager);

	// main technique
	if (!(this->MainTechnique = this->TheEffect->GetTechnique (0)))
	{
		Con_Printf ("Shader error in %s: Main Technique 0 not found!\n", name);
		this->ValidFX = false;
	}
}


D3DXHANDLE CD3DEffect::GetParamHandle (char *handlename)
{
	D3DXHANDLE TheHandle = this->TheEffect->GetParameterByName (NULL, handlename);

	if (!TheHandle)
	{
		Con_Printf ("Shader error in %s: parameter %s not found!\n", this->Name, handlename);
		this->ValidFX = false;
	}

	return TheHandle;
}


void CD3DEffect::GetWPMatrixHandle (char *HandleName)
{
	if (!this->ValidFX) return;

	this->WPMatrixHandle = this->GetParamHandle (HandleName);
}


void CD3DEffect::SetWPMatrix (D3DXMATRIX *matrix)
{
	if (!this->ValidFX) return;

	this->TheEffect->SetMatrix (this->WPMatrixHandle, matrix);
	this->CommitPending = true;
}


void CD3DEffect::GetEntMatrixHandle (char *HandleName)
{
	if (!this->ValidFX) return;

	this->EntMatrixHandle = this->GetParamHandle (HandleName);
}


void CD3DEffect::SetEntMatrix (D3DXMATRIX *matrix)
{
	if (!this->ValidFX) return;

	this->TheEffect->SetMatrix (this->EntMatrixHandle, matrix);
	this->CommitPending = true;
}


void CD3DEffect::GetColor4fHandle (char *HandleName)
{
	if (!this->ValidFX) return;

	this->ColourHandle = this->GetParamHandle (HandleName);
}


void CD3DEffect::SetColor4f (float *color)
{
	if (!this->ValidFX) return;

	this->TheEffect->SetFloatArray (this->ColourHandle, color, 4);
	this->CommitPending = true;
}


void CD3DEffect::GetTimeHandle (char *HandleName)
{
	if (!this->ValidFX) return;

	this->TimeHandle = this->GetParamHandle (HandleName);
}


void CD3DEffect::SetTime (float time)
{
	if (!this->ValidFX) return;

	this->TheEffect->SetFloat (this->TimeHandle, time);
	this->CommitPending = true;
}


void CD3DEffect::GetAlphaHandle (char *HandleName)
{
	if (!this->ValidFX) return;

	this->AlphaHandle = this->GetParamHandle (HandleName);
}


void CD3DEffect::SetAlpha (float alpha)
{
	if (!this->ValidFX) return;

	this->TheEffect->SetFloat (this->AlphaHandle, alpha);
	this->CommitPending = true;
}


void CD3DEffect::GetScaleHandle (char *HandleName)
{
	if (!this->ValidFX) return;

	this->ScaleHandle = this->GetParamHandle (HandleName);
}


void CD3DEffect::SetScale (float scale)
{
	if (!this->ValidFX) return;

	this->TheEffect->SetFloat (this->ScaleHandle, scale);
	this->CommitPending = true;
}


void CD3DEffect::GetTextureHandle (int hnum, char *HandleName)
{
	if (!this->ValidFX) return;

	this->TextureHandle[hnum] = this->GetParamHandle (HandleName);
}


void CD3DEffect::SetTexture (int hnum, LPDIRECT3DTEXTURE9 texture)
{
	if (!this->ValidFX) return;

	if (this->CurrentTexture[hnum] != texture)
	{
		this->TheEffect->SetTexture (this->TextureHandle[hnum], texture);
		this->CurrentTexture[hnum] = texture;
		this->CommitPending = true;
	}
}


void CD3DEffect::GetTextureHandle (char *HandleName)
{
	this->GetTextureHandle (0, HandleName);
}


void CD3DEffect::SetTexture (LPDIRECT3DTEXTURE9 texture)
{
	this->SetTexture (0, texture);
}


void CD3DEffect::Release (void)
{
	if (this->TheEffect)
		this->TheEffect->SetStateManager (NULL);

	SAFE_RELEASE (this->TheEffect);
}


void CD3DEffect::BeginRender (void)
{
	if (!this->ValidFX) return;

	this->TheEffect->SetTechnique (this->MainTechnique);
	this->TheEffect->Begin ((UINT *) &this->NumPasses, D3DXFX_DONOTSAVESTATE);
	this->CurrentPass = -1;
	this->RenderActive = true;
	this->CurrentTexture[0] = this->CurrentTexture[1] = this->CurrentTexture[2] = this->CurrentTexture[3] = NULL;
	this->CommitPending = false;
}


void CD3DEffect::Draw (D3DPRIMITIVETYPE Type, INT BaseVertexIndex, UINT MinIndex, UINT NumVertices, UINT StartIndex, UINT PrimitiveCount)
{
	if (this->CommitPending)
	{
		this->TheEffect->CommitChanges ();
		this->CommitPending = false;
	}

	d3d_Device->DrawIndexedPrimitive (Type, BaseVertexIndex, MinIndex, NumVertices, StartIndex, PrimitiveCount);
}


void CD3DEffect::Draw (D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount, CONST void *pVertexStreamZeroData, UINT VertexStreamZeroStride)
{
	if (this->CommitPending)
	{
		this->TheEffect->CommitChanges ();
		this->CommitPending = false;
	}

	d3d_Device->DrawPrimitiveUP (PrimitiveType, PrimitiveCount, pVertexStreamZeroData, VertexStreamZeroStride);
}


void CD3DEffect::Draw (D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount)
{
	if (this->CommitPending)
	{
		this->TheEffect->CommitChanges ();
		this->CommitPending = false;
	}

	d3d_Device->DrawPrimitive (PrimitiveType, StartVertex, PrimitiveCount);
}


void CD3DEffect::SwitchToPass (int passnum)
{
	if (!this->ValidFX) return;

	if (!this->RenderActive)
		this->BeginRender ();

	if (passnum < 0 || passnum >= this->NumPasses)
	{
		if (this->CurrentPass != -1)
			this->TheEffect->EndPass ();

		this->CurrentPass = -1;
	}
	else if (this->CurrentPass == -1)
	{
		this->TheEffect->BeginPass (passnum);
		this->CurrentPass = passnum;
		this->CommitPending = false;
	}
	else if (this->CurrentPass != passnum)
	{
		this->TheEffect->EndPass ();
		this->CurrentPass = passnum;

		this->TheEffect->BeginPass (passnum);
		this->CommitPending = false;
	}
}


void CD3DEffect::EndRender (void)
{
	if (!this->ValidFX) return;

	if (this->RenderActive)
	{
		if (this->CurrentPass != -1)
			this->TheEffect->EndPass ();

		this->TheEffect->End ();
		this->RenderActive = false;
	}
}
