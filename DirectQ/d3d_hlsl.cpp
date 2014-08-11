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

cvar_t r_hlsl ("r_hlsl", "1", CVAR_ARCHIVE);

// filtering of redundant shader state changes
// let's give ourselves a bit more control over what's happening in our effects
// this is also needed to keep the states in sync
class CFXManager : ID3DXEffectStateManager
{
private:
	int RefCount;

public:
	CFXManager (void)
	{
		// need a refcount of 1 as it's instantiated as a non-pointer instance.
		this->RefCount = 1;
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
		return d3d_Device->LightEnable (Index, Enable);
	}

	HRESULT CALLBACK SetFVF (DWORD FVF)
	{
		D3D_SetFVF (FVF);
		return S_OK;
	}

	HRESULT CALLBACK SetLight (DWORD Index, CONST D3DLIGHT9 *pLight)
	{
		return d3d_Device->SetLight (Index, pLight);
	}

	HRESULT CALLBACK SetMaterial (CONST D3DMATERIAL9 *pMaterial)
	{
		return d3d_Device->SetMaterial (pMaterial);
	}

	HRESULT CALLBACK SetNPatchMode (FLOAT nSegments)
	{
		return d3d_Device->SetNPatchMode (nSegments);
	}

	HRESULT CALLBACK SetPixelShader (LPDIRECT3DPIXELSHADER9 pShader)
	{
		return d3d_Device->SetPixelShader (pShader);
	}

	HRESULT CALLBACK SetPixelShaderConstantB (UINT StartRegister, CONST BOOL *pConstantData, UINT RegisterCount)
	{
		return d3d_Device->SetPixelShaderConstantB (StartRegister, pConstantData, RegisterCount);
	}

	HRESULT CALLBACK SetPixelShaderConstantF (UINT StartRegister, CONST FLOAT *pConstantData, UINT RegisterCount)
	{
		return d3d_Device->SetPixelShaderConstantF (StartRegister, pConstantData, RegisterCount);
	}

	HRESULT CALLBACK SetPixelShaderConstantI (UINT StartRegister, CONST INT *pConstantData, UINT RegisterCount)
	{
		return d3d_Device->SetPixelShaderConstantI (StartRegister, pConstantData, RegisterCount);
	}

	HRESULT CALLBACK SetRenderState (D3DRENDERSTATETYPE State, DWORD Value)
	{
		D3D_SetRenderState (State, Value);
		return S_OK;
	}

	HRESULT CALLBACK SetSamplerState (DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD Value)
	{
		D3D_SetSamplerState (Sampler, Type, Value);
		return S_OK;
	}

	HRESULT CALLBACK SetTexture (DWORD Stage, LPDIRECT3DBASETEXTURE9 pTexture)
	{
		D3D_SetTexture (Stage, (LPDIRECT3DTEXTURE9) pTexture);
		return S_OK;
	}

	HRESULT CALLBACK SetTextureStageState (DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value)
	{
		D3D_SetTextureStageState (Stage, Type, Value);
		return S_OK;
	}

	HRESULT CALLBACK SetTransform (D3DTRANSFORMSTATETYPE State, CONST D3DMATRIX *pMatrix)
	{
		return d3d_Device->SetTransform (State, pMatrix);
	}

	HRESULT CALLBACK SetVertexShader (LPDIRECT3DVERTEXSHADER9 pShader)
	{
		return d3d_Device->SetVertexShader (pShader);
	}

	HRESULT CALLBACK SetVertexShaderConstantB (UINT StartRegister, CONST BOOL *pConstantData, UINT RegisterCount)
	{
		return d3d_Device->SetVertexShaderConstantB (StartRegister, pConstantData, RegisterCount);
	}

	HRESULT CALLBACK SetVertexShaderConstantF (UINT StartRegister, CONST FLOAT *pConstantData, UINT RegisterCount)
	{
		return d3d_Device->SetVertexShaderConstantF (StartRegister, pConstantData, RegisterCount);
	}

	HRESULT CALLBACK SetVertexShaderConstantI (UINT StartRegister, CONST INT *pConstantData, UINT RegisterCount)
	{
		return d3d_Device->SetVertexShaderConstantI (StartRegister, pConstantData, RegisterCount);
	}
};


CFXManager FXManager;

bool SilentLoad = false;

// keep these global as we'll want to use them in a few places
char *vs_version;
char *ps_version;

// effects
CD3DEffect d3d_LiquidFX;

// vertex declarations
LPDIRECT3DVERTEXDECLARATION9 d3d_LiquidDeclaration = NULL;

void D3D_LoadEffect (char *name, int resourceid, LPD3DXEFFECT *eff)
{
	// because there are so many places this can go wrong we'll use exception handling on it rather
	// than creating a mess of error-checking.
	try
	{
		int len = 0;
		char *EffectString = NULL;
		LPD3DXBUFFER errbuf = NULL;

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
			D3DXFX_NOT_CLONEABLE | D3DXSHADER_OPTIMIZATION_LEVEL3 | D3DXSHADER_SKIPVALIDATION | D3DXFX_DONOTSAVESTATE,
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
			Con_Printf ("D3D_LoadEffect: Error compiling %s\n%s", name, errstr);
			d3d_GlobalCaps.supportPixelShaders = false;
			errbuf->Release ();
			return;
		}

		SAFE_RELEASE (errbuf);
		if (!SilentLoad) Con_Printf ("Created effect \"%s\" OK\n", name);
	}
	catch (...)
	{
		// oh well...
		Con_Printf ("D3D_LoadEffect: General error\n");
		d3d_GlobalCaps.supportPixelShaders = false;
	}
}


void D3D_InitHLSL (void)
{
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
		d3d_GlobalCaps.supportPixelShaders = true;
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
		d3d_GlobalCaps.supportPixelShaders = false;
		Con_Printf ("Vertex or Pixel Shaders version is too low.\n");
		return;
	}

	// load effects - if we get this far we know that pixel shaders are available
	d3d_LiquidFX.LoadEffect ("Liquid Shader", IDR_LIQUID);

	// only display output on the first load
	SilentLoad = true;

	// if we failed to load we don't do anything more here
	if (!d3d_GlobalCaps.supportPixelShaders) return;

	// set up vertex declarations
	// this needs to map to the layout of glwarpvert_t in gl_warp.cpp
	D3DVERTEXELEMENT9 vd[] =
	{
		{0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
		{0, 12, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
		{0, 20, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 1},
		{0, 32, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1},
		{0xff, 0, D3DDECLTYPE_UNUSED, 0, 0, 0}
	};

	d3d_Device->CreateVertexDeclaration (vd, &d3d_LiquidDeclaration);
}


void D3D_ShutdownHLSL (void)
{
	// effects
	d3d_LiquidFX.Release ();

	// declarations
	SAFE_RELEASE (d3d_LiquidDeclaration);
}


CD3DEffect::CD3DEffect (void)
{
	this->TheEffect = NULL;
	this->ValidFX = false;
}


void CD3DEffect::LoadEffect (char *name, int resourceid)
{
	this->Release ();
	D3D_LoadEffect (name, resourceid, &this->TheEffect);
	this->ValidFX = true;
	strncpy (this->Name, name, 254);

	if (!this->TheEffect)
	{
		this->ValidFX = false;
		d3d_GlobalCaps.supportPixelShaders = false;
		return;
	}

	// the effect state manager mostly just calls into our own state management functions in vidnt.
	// this lets us use the same state management routines for both fixed and programmable paths
	this->TheEffect->SetStateManager ((LPD3DXEFFECTSTATEMANAGER) &FXManager);

	// main technique
	if (!(this->MainTechnique = this->TheEffect->GetTechnique (0)))
	{
		Con_Printf ("Shader error in %s: Main Technique 0 not found!\n", name);
		this->ValidFX = false;
	}
}


void CD3DEffect::SetMatrix (D3DXHANDLE hHandle, D3DXMATRIX *matrix)
{
	if (!this->ValidFX) return;

	this->TheEffect->SetMatrix (hHandle, matrix);
	this->CommitPending = true;
}


void CD3DEffect::SetFloatArray (D3DXHANDLE hHandle, float *f)
{
	if (!this->ValidFX) return;

	this->TheEffect->SetFloatArray (hHandle, f, 4);
	this->CommitPending = true;
}


void CD3DEffect::SetFloat (D3DXHANDLE hHandle, float f)
{
	if (!this->ValidFX) return;

	this->TheEffect->SetFloat (hHandle, f);
	this->CommitPending = true;
}


void CD3DEffect::SetTexture (D3DXHANDLE hHandle, LPDIRECT3DTEXTURE9 texture)
{
	if (!this->ValidFX) return;

	this->TheEffect->SetTexture (hHandle, texture);
	this->CommitPending = true;
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
	this->PreviousPass = -1;
	this->CurrentPass = -1;
	this->RenderActive = true;
	this->CommitPending = false;
}


void CD3DEffect::BeforeDraw (void)
{
	if (this->CurrentPass != this->PreviousPass)
	{
		// if there was no valid previous pass we don't need to end it
		if (this->PreviousPass != -1) this->TheEffect->EndPass ();

		// begin the new pass
		this->TheEffect->BeginPass (this->CurrentPass);

		// store back
		this->PreviousPass = this->CurrentPass;

		// cancel any commits that may be pending as a new pass doesn't require them
		this->CommitPending = false;
	}
	else if (this->CommitPending)
	{
		// something else has changed that we need to commit
		this->TheEffect->CommitChanges ();
		this->CommitPending = false;
	}
}


void CD3DEffect::Draw (D3DPRIMITIVETYPE Type, UINT MinVertexIndex, UINT NumVertices, UINT PrimitiveCount, const void *pIndexData, D3DFORMAT IndexDataFormat, CONST void* pVertexStreamZeroData, UINT VertexStreamZeroStride)
{
	this->BeforeDraw ();
	d3d_Device->DrawIndexedPrimitiveUP (Type, MinVertexIndex, NumVertices, PrimitiveCount, pIndexData, IndexDataFormat, pVertexStreamZeroData, VertexStreamZeroStride);
}


void CD3DEffect::Draw (D3DPRIMITIVETYPE Type, INT BaseVertexIndex, UINT MinIndex, UINT NumVertices, UINT StartIndex, UINT PrimitiveCount)
{
	this->BeforeDraw ();
	d3d_Device->DrawIndexedPrimitive (Type, BaseVertexIndex, MinIndex, NumVertices, StartIndex, PrimitiveCount);
}


void CD3DEffect::Draw (D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount, CONST void *pVertexStreamZeroData, UINT VertexStreamZeroStride)
{
	this->BeforeDraw ();
	d3d_Device->DrawPrimitiveUP (PrimitiveType, PrimitiveCount, pVertexStreamZeroData, VertexStreamZeroStride);
}


void CD3DEffect::Draw (D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount)
{
	this->BeforeDraw ();
	d3d_Device->DrawPrimitive (PrimitiveType, StartVertex, PrimitiveCount);
}


void CD3DEffect::SwitchToPass (int passnum)
{
	if (!this->ValidFX) return;

	if (!this->RenderActive)
		this->BeginRender ();

	// just store out the pass number, the actual switch doesn't happen until we come to draw something
	this->CurrentPass = passnum;
}


void D3D_SetAllStates (void);

void CD3DEffect::EndRender (void)
{
	if (!this->ValidFX) return;

	if (this->RenderActive)
	{
		if (this->CurrentPass != -1)
			this->TheEffect->EndPass ();

		this->TheEffect->End ();
		this->RenderActive = false;

		// take down shaders
		d3d_Device->SetPixelShader (NULL);
		d3d_Device->SetVertexShader (NULL);
	}
}
