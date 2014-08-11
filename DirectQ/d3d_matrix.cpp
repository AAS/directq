
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
	functionality is replicated, and the D3DX versions of the functions are replaced with versions that
	operate on D3DMATRIX structs (code adapted from the WINE source code).

============================================================================================================
*/

D3DMATRIX *D3DMatrix_PerspectiveFovRH (D3DMATRIX *matrix, FLOAT fovy, FLOAT aspect, FLOAT zn, FLOAT zf)
{
	D3DMATRIX tmp;
	D3DMatrix_Identity (&tmp);
	fovy = D3DXToRadian (fovy);

	tmp.m[0][0] = 1.0f / (aspect * tan (fovy / 2.0f));
	tmp.m[1][1] = 1.0f / tan (fovy / 2.0f);
	tmp.m[2][2] = zf / (zn - zf);
	tmp.m[2][3] = -1.0f;
	tmp.m[3][2] = (zf * zn) / (zn - zf);
	tmp.m[3][3] = 0.0f;

	D3DMatrix_Multiply (matrix, &tmp, matrix);
	return matrix;
}


D3DMATRIX *D3DMatrix_OrthoOffCenterRH (D3DMATRIX *matrix, FLOAT l, FLOAT r, FLOAT b, FLOAT t, FLOAT zn, FLOAT zf)
{
	D3DMATRIX tmp;
	D3DMatrix_Identity (&tmp);

	tmp.m[0][0] = 2.0f / (r - l);
	tmp.m[1][1] = 2.0f / (t - b);
	tmp.m[2][2] = 1.0f / (zn - zf);
	tmp.m[3][0] = -1.0f - 2.0f * l / (r - l);
	tmp.m[3][1] = 1.0f + 2.0f * t / (b - t);
	tmp.m[3][2] = zn / (zn - zf);

	D3DMatrix_Multiply (matrix, &tmp, matrix);
	return matrix;
}


void D3DMatrix_Translate (D3DMATRIX *matrix, float x, float y, float z)
{
	D3DMATRIX tmp;
	D3DMatrix_Identity (&tmp);

	tmp.m[3][0] = x;
	tmp.m[3][1] = y;
	tmp.m[3][2] = z;

	D3DMatrix_Multiply (matrix, &tmp, matrix);
}


void D3DMatrix_Scale (D3DMATRIX *matrix, float x, float y, float z)
{
	D3DMATRIX tmp;
	D3DMatrix_Identity (&tmp);

	tmp.m[0][0] = x;
	tmp.m[1][1] = y;
	tmp.m[2][2] = z;

	D3DMatrix_Multiply (matrix, &tmp, matrix);
}


void D3DMatrix_Rotate (D3DMATRIX *matrix, float x, float y, float z, float angle)
{
	D3DMATRIX tmp;
	float xyz[3] = {x, y, z};

	VectorNormalize (xyz);
	angle = D3DXToRadian (angle);

	xyz[0] = angle * xyz[0];
	xyz[1] = angle * xyz[1];
	xyz[2] = angle * xyz[2];

	if (xyz[0])
	{
		D3DMatrix_Identity (&tmp);

		tmp.m[1][1] = cos (xyz[0]);
		tmp.m[2][2] = cos (xyz[0]);
		tmp.m[1][2] = sin (xyz[0]);
		tmp.m[2][1] = -sin (xyz[0]);

		D3DMatrix_Multiply (matrix, &tmp, matrix);
	}

	if (xyz[1])
	{
		D3DMatrix_Identity (&tmp);

		tmp.m[0][0] = cos (xyz[1]);
		tmp.m[2][2] = cos (xyz[1]);
		tmp.m[0][2] = -sin (xyz[1]);
		tmp.m[2][0] = sin (xyz[1]);

		D3DMatrix_Multiply (matrix, &tmp, matrix);
	}

	if (xyz[2])
	{
		D3DMatrix_Identity (&tmp);

		tmp.m[0][0] = cos (xyz[2]);
		tmp.m[1][1] = cos (xyz[2]);
		tmp.m[0][1] = sin (xyz[2]);
		tmp.m[1][0] = -sin (xyz[2]);

		D3DMatrix_Multiply (matrix, &tmp, matrix);
	}
}


D3DMATRIX *D3DMatrix_Identity (D3DMATRIX *matrix)
{
	matrix->m[0][1] = matrix->m[0][2] = matrix->m[0][3] =
		matrix->m[1][0] = matrix->m[1][2] = matrix->m[1][3] =
		matrix->m[2][0] = matrix->m[2][1] = matrix->m[2][3] =
		matrix->m[3][0] = matrix->m[3][1] = matrix->m[3][2] = 0.0f;

	matrix->m[0][0] = matrix->m[1][1] = matrix->m[2][2] = matrix->m[3][3] = 1.0f;

	return matrix;
}


D3DXMATRIX *D3DMatrix_Identity (D3DXMATRIX *matrix)
{
	matrix->m[0][1] = matrix->m[0][2] = matrix->m[0][3] =
		matrix->m[1][0] = matrix->m[1][2] = matrix->m[1][3] =
		matrix->m[2][0] = matrix->m[2][1] = matrix->m[2][3] =
		matrix->m[3][0] = matrix->m[3][1] = matrix->m[3][2] = 0.0f;

	matrix->m[0][0] = matrix->m[1][1] = matrix->m[2][2] = matrix->m[3][3] = 1.0f;

	return matrix;
}


void D3DMatrix_Multiply (D3DMATRIX *matrix1, D3DMATRIX *matrix2)
{
	D3DMATRIX matrixtmp;

	for (int i = 0; i < 4; i++)
	{
		for (int j = 0; j < 4; j++)
		{
			matrixtmp.m[i][j] = matrix1->m[i][0] * matrix2->m[0][j] +
								matrix1->m[i][1] * matrix2->m[1][j] +
								matrix1->m[i][2] * matrix2->m[2][j] +
								matrix1->m[i][3] * matrix2->m[3][j];
		}
	}

	memcpy (matrix1, &matrixtmp, sizeof (D3DMATRIX));
}


void D3DMatrix_Multiply (D3DMATRIX *matrixout, D3DMATRIX *matrix1, D3DMATRIX *matrix2)
{
	// because one of the input matrixes is allowed in d3dx to be the same as the output
	// we initially multiply into a temp copy
	D3DMATRIX matrixtmp;

	for (int i = 0; i < 4; i++)
	{
		for (int j = 0; j < 4; j++)
		{
			matrixtmp.m[i][j] = matrix1->m[i][0] * matrix2->m[0][j] +
								matrix1->m[i][1] * matrix2->m[1][j] +
								matrix1->m[i][2] * matrix2->m[2][j] +
								matrix1->m[i][3] * matrix2->m[3][j];
		}
	}

	memcpy (matrixout, &matrixtmp, sizeof (D3DMATRIX));
}


D3DXMATRIX *D3DMatrix_ToD3DXMatrix (D3DMATRIX *matrix)
{
	static D3DXMATRIX mx;
	memcpy (mx.m, matrix->m, sizeof (matrix->m));
	return &mx;
}


