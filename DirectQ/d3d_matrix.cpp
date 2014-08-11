
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

#include "quakedef.h"
#include "d3d_model.h"
#include "d3d_quake.h"


/*
============================================================================================================

		MATRIX OPS

	These happen in place on the matrix and update it's current values.  Wherever possible OpenGL-like
	functionality is replicated.

	Why the fuck these ain't in the D3DXMATRIX class I'll never know...

============================================================================================================
*/

QMATRIX::QMATRIX (float _11, float _12, float _13, float _14,
				  float _21, float _22, float _23, float _24,
				  float _31, float _32, float _33, float _34,
				  float _41, float _42, float _43, float _44)
{
	this->_11 = _11; this->_12 = _12; this->_13 = _13; this->_14 = _14;
	this->_21 = _21; this->_22 = _22; this->_23 = _23; this->_24 = _24;
	this->_31 = _31; this->_32 = _32; this->_33 = _33; this->_34 = _34;
	this->_41 = _41; this->_42 = _42; this->_43 = _43; this->_44 = _44;
}


void QMATRIX::LoadIdentity (void)
{
	D3DXMatrixIdentity (this);
}


void QMATRIX::Translate (float x, float y, float z)
{
	D3DXMATRIX m;

	D3DXMatrixTranslation (&m, x, y, z);
	D3DXMatrixMultiply (this, &m, this);
}


void QMATRIX::Translate (float *xyz)
{
	D3DXMATRIX m;

	D3DXMatrixTranslation (&m, xyz[0], xyz[1], xyz[2]);
	D3DXMatrixMultiply (this, &m, this);
}


void QMATRIX::SetFromYawPitchRoll (float y, float p, float r)
{
	D3DXMatrixRotationYawPitchRoll (this, D3DXToRadian (y), D3DXToRadian (p), D3DXToRadian (r));
}


void QMATRIX::YawPitchRoll (float y, float p, float r)
{
	D3DXMATRIX m;

	D3DXMatrixRotationYawPitchRoll (&m, D3DXToRadian (y), D3DXToRadian (p), D3DXToRadian (r));
	D3DXMatrixMultiply (this, &m, this);
}


void QMATRIX::Rotate (float x, float y, float z, float angle)
{
	D3DXMATRIX m;

	D3DXMatrixRotationAxis (&m, &D3DXVECTOR3 (x, y, z), D3DXToRadian (angle));
	D3DXMatrixMultiply (this, &m, this);
}


void QMATRIX::Scale (float x, float y, float z)
{
	D3DXMATRIX m;

	D3DXMatrixScaling (&m, x, y, z);
	D3DXMatrixMultiply (this, &m, this);
}


void QMATRIX::Scale (float *xyz)
{
	D3DXMATRIX m;

	D3DXMatrixScaling (&m, xyz[0], xyz[1], xyz[2]);
	D3DXMatrixMultiply (this, &m, this);
}


void QMATRIX::OrthoOffCenterRH (float l, float r, float b, float t, float zn, float zf)
{
	D3DXMATRIX m;

	D3DXMatrixOrthoOffCenterRH (&m, l, r, b, t, zn, zf);
	D3DXMatrixMultiply (this, &m, this);
}


void QMATRIX::Projection (float fovx, float fovy, float zn, float zf)
{
	D3DXMATRIX m;

	float Q = zf / (zf - zn);

	m.m[0][0] = 1.0f / tan ((fovx * D3DX_PI) / 360.0f);	// equivalent to D3DXToRadian (fovx) / 2
	m.m[0][1] = m.m[0][2] = m.m[0][3] = 0;

	m.m[1][1] = 1.0f / tan ((fovy * D3DX_PI) / 360.0f);	// equivalent to D3DXToRadian (fovy) / 2
	m.m[1][0] = m.m[1][2] = m.m[1][3] = 0;

	m.m[2][0] = m.m[2][1] = 0;
	m.m[2][2] = -Q;	// flip to RH
	m.m[2][3] = -1;	// flip to RH

	m.m[3][0] = m.m[3][1] = m.m[3][3] = 0;
	m.m[3][2] = -(Q * zn);

	D3DXMatrixMultiply (this, &m, this);
}


void QMATRIX::PerspectiveFovRH (float fovy, float Aspect, float zn, float zf)
{
	D3DXMATRIX m;

	D3DXMatrixPerspectiveFovRH (&m, D3DXToRadian (fovy), Aspect, zn, zf);
	D3DXMatrixMultiply (this, &m, this);
}


void QMATRIX::MultMatrix (D3DXMATRIX *in)
{
	D3DXMatrixMultiply (this, in, this);
}


void QMATRIX::LoadMatrix (D3DXMATRIX *in)
{
	this->m[0][0] = in->m[0][0]; this->m[0][1] = in->m[0][1]; this->m[0][2] = in->m[0][2]; this->m[0][3] = in->m[0][3];
	this->m[1][0] = in->m[1][0]; this->m[1][1] = in->m[1][1]; this->m[1][2] = in->m[1][2]; this->m[1][3] = in->m[1][3];
	this->m[2][0] = in->m[2][0]; this->m[2][1] = in->m[2][1]; this->m[2][2] = in->m[2][2]; this->m[2][3] = in->m[2][3];
	this->m[3][0] = in->m[3][0]; this->m[3][1] = in->m[3][1]; this->m[3][2] = in->m[3][2]; this->m[3][3] = in->m[3][3];
}


void QMATRIX::TransformPoint (float *in, float *out)
{
	out[0] = in[0] * this->_11 + in[1] * this->_21 + in[2] * this->_31 + this->_41;
	out[1] = in[0] * this->_12 + in[1] * this->_22 + in[2] * this->_32 + this->_42;
	out[2] = in[0] * this->_13 + in[1] * this->_23 + in[2] * this->_33 + this->_43;
}


void QMATRIX::UpdateMVP (QMATRIX *mvp, QMATRIX *m, QMATRIX *v, QMATRIX *p)
{
	mvp->LoadMatrix (p);
	mvp->MultMatrix (v);
	mvp->MultMatrix (m);
}


void QMATRIX::ToVectors (float *forward, float *up, float *right)
{
	forward[0] = this->_11;
	forward[1] = this->_21;
	forward[2] = this->_31;

	right[0] = -this->_12;	// stupid Quake bug
	right[1] = -this->_22;	// stupid Quake bug
	right[2] = -this->_32;	// stupid Quake bug

	up[0] = this->_13;
	up[1] = this->_23;
	up[2] = this->_33;
}


void QMATRIX::Rotate (float a0, float a2, float a1)
{
	QMATRIX m;

	m.LoadIdentity ();
	m.YawPitchRoll (a0, a2, a1);
	m.Transpose ();
	m.FixupRotation ();

	D3DXMatrixMultiply (this, &m, this);
}


void QMATRIX::Rotate (float *angles)
{
	QMATRIX m;

	m.LoadIdentity ();
	m.YawPitchRoll (angles[0], -angles[2], angles[1]);
	m.Transpose ();
	m.FixupRotation ();

	D3DXMatrixMultiply (this, &m, this);
}


void QMATRIX::FixupRotation (void)
{
	this->_12 = -this->_12;
	this->_21 = -this->_21;
	this->_32 = -this->_32;
}


void QMATRIX::Transpose (void)
{
	D3DXMatrixTranspose (this, this);
}


void QMATRIX::ToVectors (struct r_viewvecs_s *vecs)
{
	this->ToVectors (vecs->forward, vecs->up, vecs->right);
}


void QMATRIX::ExtractFrustum (struct mplane_s *f)
{
	f[0].normal[0] = this->_14 - this->_11; f[0].normal[1] = this->_24 - this->_21; f[0].normal[2] = this->_34 - this->_31;
	f[1].normal[0] = this->_14 + this->_11; f[1].normal[1] = this->_24 + this->_21; f[1].normal[2] = this->_34 + this->_31;
	f[2].normal[0] = this->_14 + this->_12; f[2].normal[1] = this->_24 + this->_22; f[2].normal[2] = this->_34 + this->_32;
	f[3].normal[0] = this->_14 - this->_12; f[3].normal[1] = this->_24 - this->_22; f[3].normal[2] = this->_34 - this->_32;

	VectorNormalize (f[0].normal);
	VectorNormalize (f[1].normal);
	VectorNormalize (f[2].normal);
	VectorNormalize (f[3].normal);
}

