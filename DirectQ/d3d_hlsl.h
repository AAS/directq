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


class CD3DEffect
{
public:
	// setup and control
	CD3DEffect (void);
	void LoadEffect (char *name, int resourceid, bool iswarpeffect = false);

	void GetWPMatrixHandle (char *HandleName);
	void SetWPMatrix (D3DXMATRIX *matrix);

	void GetEntMatrixHandle (char *HandleName);
	void SetEntMatrix (D3DXMATRIX *matrix);

	void GetColor4fHandle (char *HandleName);
	void SetColor4f (float *color);

	void GetTimeHandle (char *HandleName);
	void SetTime (float time);

	void GetAlphaHandle (char *HandleName);
	void SetAlpha (float alpha);

	void GetScaleHandle (char *HandleName);
	void SetScale (float scale);

	void GetTextureHandle (int hnum, char *HandleName);
	void SetTexture (int hnum, LPDIRECT3DTEXTURE9 texture);
	void GetTextureHandle (char *HandleName);
	void SetTexture (LPDIRECT3DTEXTURE9 texture);

	void Release (void);

	// rendering
	void BeginRender (void);
	void EndRender (void);
	void SwitchToPass (int passnum);
	void Draw (D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount, CONST void* pVertexStreamZeroData, UINT VertexStreamZeroStride);
	void Draw (D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount);
	void Draw (D3DPRIMITIVETYPE Type, INT BaseVertexIndex, UINT MinIndex, UINT NumVertices, UINT StartIndex, UINT PrimitiveCount);


private:
	// internal control
	D3DXHANDLE GetParamHandle (char *handlename);

	// basic attributes
	LPD3DXEFFECT TheEffect;
	bool IsWarpEffect;
	char Name[256];
	bool ValidFX;

	// handles for all the various param types we want to use
	D3DXHANDLE MainTechnique;
	D3DXHANDLE WPMatrixHandle;
	D3DXHANDLE EntMatrixHandle;
	D3DXHANDLE ColourHandle;
	D3DXHANDLE TimeHandle;
	D3DXHANDLE AlphaHandle;
	D3DXHANDLE ScaleHandle;
	D3DXHANDLE TextureHandle[4];

	// rendering and state control
	int NumPasses;
	int CurrentPass;
	bool RenderActive;
	LPDIRECT3DTEXTURE9 CurrentTexture[4];
	bool CommitPending;
};


extern LPDIRECT3DVERTEXDECLARATION9 d3d_AliasVertexDeclaration;
extern LPDIRECT3DVERTEXDECLARATION9 d3d_ParticleVertexDeclaration;
extern LPDIRECT3DVERTEXDECLARATION9 d3d_V3ST2Declaration;
extern LPDIRECT3DVERTEXDECLARATION9 d3d_V3ST4Declaration;
extern LPDIRECT3DVERTEXDECLARATION9 d3d_V3NoSTDeclaration;

extern CD3DEffect d3d_InstancedBrushFX;
extern CD3DEffect d3d_BrushFX;
extern CD3DEffect d3d_LiquidFX;
extern CD3DEffect d3d_AliasFX;
extern CD3DEffect d3d_Flat2DFX;
extern CD3DEffect d3d_SkyFX;
extern CD3DEffect d3d_SpriteFX;
extern CD3DEffect d3d_ParticleFX;
extern CD3DEffect d3d_UnderwaterFX;

void D3D_InitHLSL (void);
void D3D_ShutdownHLSL (void);


