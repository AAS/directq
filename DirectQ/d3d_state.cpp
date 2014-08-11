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


// state block management
#include "quakedef.h"
#include "d3d_quake.h"

IDirect3DStateBlock9 *d3d_SetAliasState = NULL;
IDirect3DStateBlock9 *d3d_RevertAliasState = NULL;
IDirect3DStateBlock9 *d3d_EnableAlphaTest = NULL;
IDirect3DStateBlock9 *d3d_DisableAlphaTest = NULL;
IDirect3DStateBlock9 *d3d_EnableAlphaBlend = NULL;
IDirect3DStateBlock9 *d3d_DisableAlphaBlend = NULL;


void D3D_ReleaseStateBlocks (void)
{
	SAFE_RELEASE (d3d_EnableAlphaBlend);
	SAFE_RELEASE (d3d_DisableAlphaBlend);
	SAFE_RELEASE (d3d_EnableAlphaTest);
	SAFE_RELEASE (d3d_DisableAlphaTest);
	SAFE_RELEASE (d3d_SetAliasState);
	SAFE_RELEASE (d3d_RevertAliasState);
}


void D3D_CreateStateBlocks (void)
{
	D3DVIEWPORT9 vp;

	// enable alpha blend
	d3d_Device->BeginStateBlock ();
	d3d_Device->SetRenderState (D3DRS_ALPHABLENDENABLE, TRUE);
	d3d_Device->SetRenderState (D3DRS_BLENDOP, D3DBLENDOP_ADD);
	d3d_Device->SetRenderState (D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	d3d_Device->SetRenderState (D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	d3d_Device->EndStateBlock (&d3d_EnableAlphaBlend);

	// disable alpha blend
	d3d_Device->BeginStateBlock ();
	d3d_Device->SetRenderState (D3DRS_ALPHABLENDENABLE, FALSE);
	d3d_Device->EndStateBlock (&d3d_DisableAlphaBlend);

	// enable alpha test
	d3d_Device->BeginStateBlock ();
	d3d_Device->SetRenderState (D3DRS_ALPHATESTENABLE, TRUE);
	d3d_Device->SetRenderState (D3DRS_ALPHAREF, 128);
	d3d_Device->SetRenderState (D3DRS_ALPHAFUNC, D3DCMP_GREATER);
	d3d_Device->EndStateBlock (&d3d_EnableAlphaTest);

	// disable alpha test
	d3d_Device->BeginStateBlock ();
	d3d_Device->SetRenderState (D3DRS_ALPHATESTENABLE, FALSE);
	d3d_Device->EndStateBlock (&d3d_DisableAlphaTest);
}


// other states
void D3D_SetDefaultStates (void)
{
	// create a matrix stack for the world
	SAFE_RELEASE (d3d_WorldMatrixStack);
	D3DXCreateMatrixStack (0, &d3d_WorldMatrixStack);
	d3d_WorldMatrixStack->LoadIdentity ();

	// disable lighting
	d3d_Device->SetRenderState (D3DRS_LIGHTING, FALSE);
}


