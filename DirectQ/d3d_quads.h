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

// quad batcher
// we could use ID3DXSprite but:
// - it addrefs a texture which is slow
// - it locks each 4 verts vhich is slow
// - it's not flexible enough for our needs
// so we don't

typedef struct quadvert_s
{
	float xyz[3];
	DWORD color;
	float st[2];
} quadvert_t;


typedef struct d3d_quadstate_s
{
	int FirstVertex;
	int NumVertexes;
	int FirstIndex;
	int NumIndexes;
	int TotalVertexes;
	int TotalIndexes;
	int LockOffset;
	quadvert_t *QuadVerts;
} d3d_quadstate_t;

extern d3d_quadstate_t d3d_QuadState;


void D3DQuad_Flush (void);
void D3DQuad_Begin (LPDIRECT3DVERTEXDECLARATION9 decloverride = NULL);
void D3DQuad_CheckLock (int numvertexes);
void D3DQuad_Advance (void);
