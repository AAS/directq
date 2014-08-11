/*
Copyright (C) 1997-2001 Id Software, Inc.

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


/*
=======================================================================================================================

replicates the D3DXMatrixStack class but DOESN'T require creation and releasing of a fucking COM object...!

also mimics OpenGL more closely in the operation of some functions (particularly rotation) and includes a few extra
helper members for convenience

=======================================================================================================================
*/

#include "quakedef.h"
#include "d3d_quake.h"


CD3D_MatrixStack::CD3D_MatrixStack (D3DTRANSFORMSTATETYPE trans)
{
	// set all to identity
	for (int i = 0; i < MAX_MATRIX_STACK_DEPTH; i++)
		D3DXMatrixIdentity (&this->theStack[i]);

	this->currdepth = 0;
	this->usage = trans;
	this->pushed = false;

	// force to dirty on creation so that we can get an immediate SetTransform from the device if it's used
	// without any transforms set on the matrix.  we can't do a SetTransform here as the device might not exist yet! 
	this->dirty = true;

	// no left to right conversion
	this->RHtoLH = false;
}


void CD3D_MatrixStack::Push (void)
{
	this->currdepth++;

	if (this->currdepth == MAX_MATRIX_STACK_DEPTH)
	{
		Con_Printf ("CD3D_MatrixStack::Push - stack overflow\n");
		this->currdepth--;
	}

	this->pushed = true;

	// copy the previous matrix up
	memcpy (&this->theStack[this->currdepth], &this->theStack[this->currdepth - 1], sizeof (D3DXMATRIX));

	// dirty the matrix
	this->dirty = true;
}


void CD3D_MatrixStack::Pop (void)
{
	this->pushed = false;

	if (this->currdepth > 0)
		this->currdepth--;
	else Con_Printf ("CD3D_MatrixStack::Pop - stack underflow\n");

	// dirty the matrix
	this->dirty = true;
}


void CD3D_MatrixStack::Reset (void)
{
	this->currdepth = 0;

	// dirty the matrix
	this->dirty = true;
}


void CD3D_MatrixStack::ResetIdentity (void)
{
	D3DXMatrixIdentity (&this->theStack[0]);
	this->currdepth = 0;

	// dirty the matrix
	this->dirty = true;
}


void CD3D_MatrixStack::LoadIdentity (void)
{
	D3DXMatrixIdentity (&this->theStack[this->currdepth]);

	// dirty the matrix
	this->dirty = true;
}


void CD3D_MatrixStack::Rotate (float x, float y, float z, float angle)
{
	// replicates the OpenGL glRotatef with 3 components and angle in degrees
	D3DXVECTOR3 vec (x, y, z);

	D3DXMATRIX tmp;
	D3DXMatrixRotationAxis (&tmp, &vec, D3DXToRadian (angle));
	this->theStack[this->currdepth] = tmp * this->theStack[this->currdepth];

	// dirty the matrix
	this->dirty = true;
}


void CD3D_MatrixStack::Translate (float x, float y, float z)
{
	D3DXMATRIX tmp;
	D3DXMatrixTranslation (&tmp, x, y, z);
	this->theStack[this->currdepth] = tmp * this->theStack[this->currdepth];

	// dirty the matrix
	this->dirty = true;
}


void CD3D_MatrixStack::Translatev (float *v)
{
	this->Translate (v[0], v[1], v[2]);
}


void CD3D_MatrixStack::Scale (float x, float y, float z)
{
	D3DXMATRIX tmp;
	D3DXMatrixScaling (&tmp, x, y, z);
	this->theStack[this->currdepth] = tmp * this->theStack[this->currdepth];

	// dirty the matrix
	this->dirty = true;
}


void CD3D_MatrixStack::Scalev (float *v)
{
	this->Scale (v[0], v[1], v[2]);
}


D3DXMATRIX *CD3D_MatrixStack::GetTop (void)
{
	return &this->theStack[this->currdepth];
}


void CD3D_MatrixStack::GetMatrix (D3DXMATRIX *m)
{
	memcpy (m, &this->theStack[this->currdepth], sizeof (D3DXMATRIX));
}


void CD3D_MatrixStack::SetMatrix (D3DXMATRIX *m)
{
	memcpy (&this->theStack[this->currdepth], m, sizeof (D3DXMATRIX));

	// dirty the matrix
	this->dirty = true;
}


void CD3D_MatrixStack::Ortho2D (float left, float right, float bottom, float top, float znear, float zfar)
{
	D3DXMATRIX tmp;
	D3DXMatrixOrthoOffCenterRH (&tmp, left, right, bottom, top, znear, zfar);
	this->theStack[this->currdepth] = tmp * this->theStack[this->currdepth];

	// dirty the matrix
	this->dirty = true;
}


void CD3D_MatrixStack::Perspective3D (float fovy, float screenaspect, float znear, float zfar)
{
	D3DXMATRIX tmp;
	D3DXMatrixPerspectiveFovRH (&tmp, D3DXToRadian (fovy), screenaspect, znear, zfar);
	this->theStack[this->currdepth] = tmp * this->theStack[this->currdepth];

	// dirty the matrix
	this->dirty = true;
}


void CD3D_MatrixStack::Frustum3D (float fovx, float fovy, float znear, float zfar)
{
	float xmax = znear * tan ((fovx * M_PI) / 360.0);
	float ymax = znear * tan ((fovy * M_PI) / 360.0);

	D3DXMATRIX tmp;
	D3DXMatrixPerspectiveOffCenterRH (&tmp, -xmax, xmax, -ymax, ymax, znear, zfar);
	this->theStack[this->currdepth] = tmp * this->theStack[this->currdepth];

	// dirty the matrix
	this->dirty = true;
}


void CD3D_MatrixStack::CheckDirtyState (void)
{
	if (this->dirty)
	{
		// this might happen during a restart or device lost case so just silently ignore the call
		if (!d3d_Device) return;

		// set the transform
		d3d_Device->SetTransform (this->usage, &this->theStack[this->currdepth]);

		// flag as no longer dirty
		this->dirty = false;
	}
}
