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
	bool LoadEffect (char *name, int resourceid);

	void SetMatrix (D3DXHANDLE hHandle, D3DXMATRIX *matrix);
	void SetFloatArray (D3DXHANDLE hHandle, float *f, int len);
	void SetFloat (D3DXHANDLE hHandle, float f);
	void SetTexture (D3DXHANDLE hHandle, LPDIRECT3DTEXTURE9 texture);

	void Release (void);

	// rendering
	void BeginRender (void);
	void EndRender (void);
	void SwitchToPass (int passnum);
	void Draw (D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount, CONST void* pVertexStreamZeroData, UINT VertexStreamZeroStride);
	void Draw (D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount);
	void Draw (D3DPRIMITIVETYPE Type, INT BaseVertexIndex, UINT MinIndex, UINT NumVertices, UINT StartIndex, UINT PrimitiveCount);
	void Draw (D3DPRIMITIVETYPE Type, UINT MinVertexIndex, UINT NumVertices, UINT PrimitiveCount, const void *pIndexData, D3DFORMAT IndexDataFormat, CONST void* pVertexStreamZeroData, UINT VertexStreamZeroStride);


private:
	// basic attributes
	LPD3DXEFFECT TheEffect;
	char Name[256];
	bool ValidFX;

	// rendering and state control
	int NumPasses;
	int CurrentPass;
	int PreviousPass;
	bool RenderActive;
	bool CommitPending;
	D3DXHANDLE MainTechnique;

	// state updates
	void BeforeDraw (void);

	// current active effect
	static CD3DEffect *CurrentEffect;
};


extern LPDIRECT3DVERTEXDECLARATION9 d3d_AliasDeclaration;
extern LPDIRECT3DVERTEXDECLARATION9 d3d_LiquidDeclaration;
extern LPDIRECT3DVERTEXDECLARATION9 d3d_SkyDeclaration;

extern CD3DEffect d3d_AliasFX;
extern CD3DEffect d3d_LiquidFX;
extern CD3DEffect d3d_SkyFX;

void D3D_InitHLSL (void);
void D3D_ShutdownHLSL (void);


