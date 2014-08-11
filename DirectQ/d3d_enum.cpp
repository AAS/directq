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
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <conio.h>
#include <string.h>


/*
================================================================================================

	Returns a string representation for a given D3D enum.
	-----------------------------------------------------

Bugs in the SDK documentation (August 2009):

* D3DDECLMETHOD does not include a FORCE_DWORD
* D3DDECLTYPE does not include a FORCE_DWORD
* D3DDECLUSAGE does not include a FORCE_DWORD
* D3DDISPLAYROTATION does not include a FORCE_DWORD
* D3DERR is not an enum and should be moved to Constants.
* D3DQUERYTYPE_ResourceManager should be all upper-case
* D3DQUERYTYPE does not include a FORCE_DWORD
* D3DRTYPE_CubeTexture should be all upper-case
* D3DSTT_1D is not a member of D3DSAMPLER_TEXTURE_TYPE
* D3DSAMPLER_TEXTURE_TYPE is not documented as having a FORCE_DWORD value of 0x7fffffff
* D3DSCANLINEORDERING does not include a FORCE_DWORD
* D3DVERTEXBLENDFLAGS does not include a FORCE_DWORD

================================================================================================
*/

char *D3DTypeToString (D3DFORMAT enumval)
{
	static char *pstr = NULL;

	switch (enumval)
	{
		case D3DFMT_UNKNOWN: pstr = "D3DFMT_UNKNOWN"; break;
		case D3DFMT_R8G8B8: pstr = "D3DFMT_R8G8B8"; break;
		case D3DFMT_A8R8G8B8: pstr = "D3DFMT_A8R8G8B8"; break;
		case D3DFMT_X8R8G8B8: pstr = "D3DFMT_X8R8G8B8"; break;
		case D3DFMT_R5G6B5: pstr = "D3DFMT_R5G6B5"; break;
		case D3DFMT_X1R5G5B5: pstr = "D3DFMT_X1R5G5B5"; break;
		case D3DFMT_A1R5G5B5: pstr = "D3DFMT_A1R5G5B5"; break;
		case D3DFMT_A4R4G4B4: pstr = "D3DFMT_A4R4G4B4"; break;
		case D3DFMT_R3G3B2: pstr = "D3DFMT_R3G3B2"; break;
		case D3DFMT_A8: pstr = "D3DFMT_A8"; break;
		case D3DFMT_A8R3G3B2: pstr = "D3DFMT_A8R3G3B2"; break;
		case D3DFMT_X4R4G4B4: pstr = "D3DFMT_X4R4G4B4"; break;
		case D3DFMT_A2B10G10R10: pstr = "D3DFMT_A2B10G10R10"; break;
		case D3DFMT_A8B8G8R8: pstr = "D3DFMT_A8B8G8R8"; break;
		case D3DFMT_X8B8G8R8: pstr = "D3DFMT_X8B8G8R8"; break;
		case D3DFMT_G16R16: pstr = "D3DFMT_G16R16"; break;
		case D3DFMT_A2R10G10B10: pstr = "D3DFMT_A2R10G10B10"; break;
		case D3DFMT_A16B16G16R16: pstr = "D3DFMT_A16B16G16R16"; break;
		case D3DFMT_A8P8: pstr = "D3DFMT_A8P8"; break;
		case D3DFMT_P8: pstr = "D3DFMT_P8"; break;
		case D3DFMT_L8: pstr = "D3DFMT_L8"; break;
		case D3DFMT_A8L8: pstr = "D3DFMT_A8L8"; break;
		case D3DFMT_A4L4: pstr = "D3DFMT_A4L4"; break;
		case D3DFMT_V8U8: pstr = "D3DFMT_V8U8"; break;
		case D3DFMT_L6V5U5: pstr = "D3DFMT_L6V5U5"; break;
		case D3DFMT_X8L8V8U8: pstr = "D3DFMT_X8L8V8U8"; break;
		case D3DFMT_Q8W8V8U8: pstr = "D3DFMT_Q8W8V8U8"; break;
		case D3DFMT_V16U16: pstr = "D3DFMT_V16U16"; break;
		case D3DFMT_A2W10V10U10: pstr = "D3DFMT_A2W10V10U10"; break;
		case D3DFMT_UYVY: pstr = "D3DFMT_UYVY"; break;
		case D3DFMT_YUY2: pstr = "D3DFMT_YUY2"; break;
		case D3DFMT_DXT1: pstr = "D3DFMT_DXT1"; break;
		case D3DFMT_DXT2: pstr = "D3DFMT_DXT2"; break;
		case D3DFMT_DXT3: pstr = "D3DFMT_DXT3"; break;
		case D3DFMT_DXT4: pstr = "D3DFMT_DXT4"; break;
		case D3DFMT_DXT5: pstr = "D3DFMT_DXT5"; break;
		case D3DFMT_D16_LOCKABLE: pstr = "D3DFMT_D16_LOCKABLE"; break;
		case D3DFMT_D32: pstr = "D3DFMT_D32"; break;
		case D3DFMT_D15S1: pstr = "D3DFMT_D15S1"; break;
		case D3DFMT_D24S8: pstr = "D3DFMT_D24S8"; break;
		case D3DFMT_D24X8: pstr = "D3DFMT_D24X8"; break;
		case D3DFMT_D24X4S4: pstr = "D3DFMT_D24X4S4"; break;
		case D3DFMT_D16: pstr = "D3DFMT_D16"; break;
		case D3DFMT_L16: pstr = "D3DFMT_L16"; break;
		case D3DFMT_VERTEXDATA: pstr = "D3DFMT_VERTEXDATA"; break;
		case D3DFMT_INDEX16: pstr = "D3DFMT_INDEX16"; break;
		case D3DFMT_INDEX32: pstr = "D3DFMT_INDEX32"; break;
		case D3DFMT_Q16W16V16U16: pstr = "D3DFMT_Q16W16V16U16"; break;
		case D3DFMT_MULTI2_ARGB8: pstr = "D3DFMT_MULTI2_ARGB8"; break;
		case D3DFMT_R16F: pstr = "D3DFMT_R16F"; break;
		case D3DFMT_G16R16F: pstr = "D3DFMT_G16R16F"; break;
		case D3DFMT_A16B16G16R16F: pstr = "D3DFMT_A16B16G16R16F"; break;
		case D3DFMT_R32F: pstr = "D3DFMT_R32F"; break;
		case D3DFMT_G32R32F: pstr = "D3DFMT_G32R32F"; break;
		case D3DFMT_A32B32G32R32F: pstr = "D3DFMT_A32B32G32R32F"; break;
		case D3DFMT_CxV8U8: pstr = "D3DFMT_CxV8U8"; break;
		default: pstr = "Unknown format"; break;
	}

	return pstr;
}


char *D3DTypeToString (D3DMULTISAMPLE_TYPE enumval)
{
	static char *pstr = NULL;

	switch (enumval)
	{
		case D3DMULTISAMPLE_NONE: pstr = "D3DMULTISAMPLE_NONE"; break;
		case D3DMULTISAMPLE_NONMASKABLE: pstr = "D3DMULTISAMPLE_NONMASKABLE"; break;
		case D3DMULTISAMPLE_2_SAMPLES: pstr = "D3DMULTISAMPLE_2_SAMPLES"; break;
		case D3DMULTISAMPLE_3_SAMPLES: pstr = "D3DMULTISAMPLE_3_SAMPLES"; break;
		case D3DMULTISAMPLE_4_SAMPLES: pstr = "D3DMULTISAMPLE_4_SAMPLES"; break;
		case D3DMULTISAMPLE_5_SAMPLES : pstr = "D3DMULTISAMPLE_5_SAMPLES "; break;
		case D3DMULTISAMPLE_6_SAMPLES: pstr = "D3DMULTISAMPLE_6_SAMPLES"; break;
		case D3DMULTISAMPLE_7_SAMPLES: pstr = "D3DMULTISAMPLE_7_SAMPLES"; break;
		case D3DMULTISAMPLE_8_SAMPLES: pstr = "D3DMULTISAMPLE_8_SAMPLES"; break;
		case D3DMULTISAMPLE_9_SAMPLES: pstr = "D3DMULTISAMPLE_9_SAMPLES"; break;
		case D3DMULTISAMPLE_10_SAMPLES: pstr = "D3DMULTISAMPLE_10_SAMPLES"; break;
		case D3DMULTISAMPLE_11_SAMPLES: pstr = "D3DMULTISAMPLE_11_SAMPLES"; break;
		case D3DMULTISAMPLE_12_SAMPLES: pstr = "D3DMULTISAMPLE_12_SAMPLES"; break;
		case D3DMULTISAMPLE_13_SAMPLES: pstr = "D3DMULTISAMPLE_13_SAMPLES"; break;
		case D3DMULTISAMPLE_14_SAMPLES: pstr = "D3DMULTISAMPLE_14_SAMPLES"; break;
		case D3DMULTISAMPLE_15_SAMPLES: pstr = "D3DMULTISAMPLE_15_SAMPLES"; break;
		case D3DMULTISAMPLE_16_SAMPLES: pstr = "D3DMULTISAMPLE_16_SAMPLES"; break;
		default: pstr = "Unknown"; break;
	}

	return pstr;
}



char *D3DTypeToString (D3DBACKBUFFER_TYPE enumval)
{
	static char *pstr = NULL;

	switch (enumval)
	{
		case D3DBACKBUFFER_TYPE_MONO: pstr = "D3DBACKBUFFER_TYPE_MONO"; break;
		case D3DBACKBUFFER_TYPE_LEFT: pstr = "D3DBACKBUFFER_TYPE_LEFT"; break;
		case D3DBACKBUFFER_TYPE_RIGHT: pstr = "D3DBACKBUFFER_TYPE_RIGHT"; break;
		default: pstr = "Unknown"; break;
	}

	return pstr;
}


char *D3DTypeToString (D3DBASISTYPE enumval)
{
	static char *pstr = NULL;

	switch (enumval)
	{
		case D3DBASIS_BEZIER: pstr = "D3DBASIS_BEZIER"; break;
		case D3DBASIS_BSPLINE: pstr = "D3DBASIS_BSPLINE"; break;
		case D3DBASIS_CATMULL_ROM: pstr = "D3DBASIS_CATMULL_ROM"; break;
		default: pstr = "Unknown"; break;
	}

	return pstr;
}


char *D3DTypeToString (D3DBLEND enumval)
{
	static char *pstr = NULL;

	switch (enumval)
	{
		case D3DBLEND_ZERO: pstr = "D3DBLEND_ZERO"; break;
		case D3DBLEND_ONE: pstr = "D3DBLEND_ONE"; break;
		case D3DBLEND_SRCCOLOR: pstr = "D3DBLEND_SRCCOLOR"; break;
		case D3DBLEND_INVSRCCOLOR: pstr = "D3DBLEND_INVSRCCOLOR"; break;
		case D3DBLEND_SRCALPHA: pstr = "D3DBLEND_SRCALPHA"; break;
		case D3DBLEND_INVSRCALPHA: pstr = "D3DBLEND_INVSRCALPHA"; break;
		case D3DBLEND_DESTALPHA: pstr = "D3DBLEND_DESTALPHA"; break;
		case D3DBLEND_INVDESTALPHA: pstr = "D3DBLEND_INVDESTALPHA"; break;
		case D3DBLEND_DESTCOLOR: pstr = "D3DBLEND_DESTCOLOR"; break;
		case D3DBLEND_INVDESTCOLOR: pstr = "D3DBLEND_INVDESTCOLOR"; break;
		case D3DBLEND_SRCALPHASAT: pstr = "D3DBLEND_SRCALPHASAT"; break;
		case D3DBLEND_BOTHSRCALPHA: pstr = "D3DBLEND_BOTHSRCALPHA"; break;
		case D3DBLEND_BOTHINVSRCALPHA: pstr = "D3DBLEND_BOTHINVSRCALPHA"; break;
		case D3DBLEND_BLENDFACTOR: pstr = "D3DBLEND_BLENDFACTOR"; break;
		case D3DBLEND_INVBLENDFACTOR: pstr = "D3DBLEND_INVBLENDFACTOR"; break;
		default: pstr = "Unknown"; break;
	}

	return pstr;
}


char *D3DTypeToString (D3DBLENDOP enumval)
{
	static char *pstr = NULL;

	switch (enumval)
	{
		case D3DBLENDOP_ADD: pstr = "D3DBLENDOP_ADD"; break;
		case D3DBLENDOP_SUBTRACT: pstr = "D3DBLENDOP_SUBTRACT"; break;
		case D3DBLENDOP_REVSUBTRACT: pstr = "D3DBLENDOP_REVSUBTRACT"; break;
		case D3DBLENDOP_MIN: pstr = "D3DBLENDOP_MIN"; break;
		case D3DBLENDOP_MAX: pstr = "D3DBLENDOP_MAX"; break;
		default: pstr = "Unknown"; break;
	}

	return pstr;
}


char *D3DTypeToString (D3DCMPFUNC enumval)
{
	static char *pstr = NULL;

	switch (enumval)
	{
		case D3DCMP_NEVER: pstr = "D3DCMP_NEVER"; break;
		case D3DCMP_LESS: pstr = "D3DCMP_LESS"; break;
		case D3DCMP_EQUAL: pstr = "D3DCMP_EQUAL"; break;
		case D3DCMP_LESSEQUAL: pstr = "D3DCMP_LESSEQUAL"; break;
		case D3DCMP_GREATER: pstr = "D3DCMP_GREATER"; break;
		case D3DCMP_NOTEQUAL: pstr = "D3DCMP_NOTEQUAL"; break;
		case D3DCMP_GREATEREQUAL: pstr = "D3DCMP_GREATEREQUAL"; break;
		case D3DCMP_ALWAYS: pstr = "D3DCMP_ALWAYS"; break;
		default: pstr = "Unknown"; break;
	}

	return pstr;
}


char *D3DTypeToString (D3DCUBEMAP_FACES enumval)
{
	static char *pstr = NULL;

	switch (enumval)
	{
		case D3DCUBEMAP_FACE_POSITIVE_X: pstr = "D3DCUBEMAP_FACE_POSITIVE_X"; break;
		case D3DCUBEMAP_FACE_NEGATIVE_X: pstr = "D3DCUBEMAP_FACE_NEGATIVE_X"; break;
		case D3DCUBEMAP_FACE_POSITIVE_Y: pstr = "D3DCUBEMAP_FACE_POSITIVE_Y"; break;
		case D3DCUBEMAP_FACE_NEGATIVE_Y: pstr = "D3DCUBEMAP_FACE_NEGATIVE_Y"; break;
		default: pstr = "Unknown"; break;
	}

	return pstr;
}


char *D3DTypeToString (D3DCULL enumval)
{
	static char *pstr = NULL;

	switch (enumval)
	{
		case D3DCULL_NONE: pstr = "D3DCULL_NONE"; break;
		case D3DCULL_CW: pstr = "D3DCULL_CW"; break;
		case D3DCULL_CCW: pstr = "D3DCULL_CCW"; break;
		default: pstr = "Unknown"; break;
	}

	return pstr;
}


char *D3DTypeToString (D3DDEBUGMONITORTOKENS enumval)
{
	static char *pstr = NULL;

	switch (enumval)
	{
		case D3DDMT_ENABLE: pstr = "D3DDMT_ENABLE"; break;
		case D3DDMT_DISABLE: pstr = "D3DDMT_DISABLE"; break;
		default: pstr = "Unknown"; break;
	}

	return pstr;
}


char *D3DTypeToString (D3DDECLMETHOD enumval)
{
	static char *pstr = NULL;

	switch (enumval)
	{
		case D3DDECLMETHOD_DEFAULT: pstr = "D3DDECLMETHOD_DEFAULT"; break;
		case D3DDECLMETHOD_PARTIALU: pstr = "D3DDECLMETHOD_PARTIALU"; break;
		case D3DDECLMETHOD_PARTIALV: pstr = "D3DDECLMETHOD_PARTIALV"; break;
		case D3DDECLMETHOD_CROSSUV: pstr = "D3DDECLMETHOD_CROSSUV"; break;
		case D3DDECLMETHOD_UV: pstr = "D3DDECLMETHOD_UV"; break;
		case D3DDECLMETHOD_LOOKUP: pstr = "D3DDECLMETHOD_LOOKUP"; break;
		case D3DDECLMETHOD_LOOKUPPRESAMPLED: pstr = "D3DDECLMETHOD_LOOKUPPRESAMPLED"; break;
		default: pstr = "Unknown"; break;
	}

	return pstr;
}


char *D3DTypeToString (D3DDECLTYPE enumval)
{
	static char *pstr = NULL;

	switch (enumval)
	{
		case D3DDECLTYPE_FLOAT1: pstr = "D3DDECLTYPE_FLOAT1"; break;
		case D3DDECLTYPE_FLOAT2: pstr = "D3DDECLTYPE_FLOAT2"; break;
		case D3DDECLTYPE_FLOAT3: pstr = "D3DDECLTYPE_FLOAT3"; break;
		case D3DDECLTYPE_FLOAT4: pstr = "D3DDECLTYPE_FLOAT4"; break;
		case D3DDECLTYPE_D3DCOLOR: pstr = "D3DDECLTYPE_D3DCOLOR"; break;
		case D3DDECLTYPE_UBYTE4: pstr = "D3DDECLTYPE_UBYTE4"; break;
		case D3DDECLTYPE_SHORT2: pstr = "D3DDECLTYPE_SHORT2"; break;
		case D3DDECLTYPE_SHORT4: pstr = "D3DDECLTYPE_SHORT4"; break;
		case D3DDECLTYPE_UBYTE4N: pstr = "D3DDECLTYPE_UBYTE4N"; break;
		case D3DDECLTYPE_SHORT2N: pstr = "D3DDECLTYPE_SHORT2N"; break;
		case D3DDECLTYPE_SHORT4N: pstr = "D3DDECLTYPE_SHORT4N"; break;
		case D3DDECLTYPE_USHORT2N: pstr = "D3DDECLTYPE_USHORT2N"; break;
		case D3DDECLTYPE_USHORT4N: pstr = "D3DDECLTYPE_USHORT4N"; break;
		case D3DDECLTYPE_UDEC3: pstr = "D3DDECLTYPE_UDEC3"; break;
		case D3DDECLTYPE_DEC3N: pstr = "D3DDECLTYPE_DEC3N"; break;
		case D3DDECLTYPE_FLOAT16_2: pstr = "D3DDECLTYPE_FLOAT16_2"; break;
		case D3DDECLTYPE_FLOAT16_4: pstr = "D3DDECLTYPE_FLOAT16_4"; break;
		case D3DDECLTYPE_UNUSED: pstr = "D3DDECLTYPE_UNUSED"; break;
		default: pstr = "Unknown"; break;
	}

	return pstr;
}


char *D3DTypeToString (D3DDECLUSAGE enumval)
{
	static char *pstr = NULL;

	switch (enumval)
	{
		case D3DDECLUSAGE_POSITION: pstr = "D3DDECLUSAGE_POSITION"; break;
		case D3DDECLUSAGE_BLENDWEIGHT: pstr = "D3DDECLUSAGE_BLENDWEIGHT"; break;
		case D3DDECLUSAGE_BLENDINDICES: pstr = "D3DDECLUSAGE_BLENDINDICES"; break;
		case D3DDECLUSAGE_NORMAL: pstr = "D3DDECLUSAGE_NORMAL"; break;
		case D3DDECLUSAGE_PSIZE: pstr = "D3DDECLUSAGE_PSIZE"; break;
		case D3DDECLUSAGE_TEXCOORD: pstr = "D3DDECLUSAGE_TEXCOORD"; break;
		case D3DDECLUSAGE_TANGENT: pstr = "D3DDECLUSAGE_TANGENT"; break;
		case D3DDECLUSAGE_BINORMAL: pstr = "D3DDECLUSAGE_BINORMAL"; break;
		case D3DDECLUSAGE_TESSFACTOR: pstr = "D3DDECLUSAGE_TESSFACTOR"; break;
		case D3DDECLUSAGE_POSITIONT: pstr = "D3DDECLUSAGE_POSITIONT"; break;
		case D3DDECLUSAGE_COLOR: pstr = "D3DDECLUSAGE_COLOR"; break;
		case D3DDECLUSAGE_FOG: pstr = "D3DDECLUSAGE_FOG"; break;
		case D3DDECLUSAGE_DEPTH: pstr = "D3DDECLUSAGE_DEPTH"; break;
		case D3DDECLUSAGE_SAMPLE: pstr = "D3DDECLUSAGE_SAMPLE"; break;
		default: pstr = "Unknown"; break;
	}

	return pstr;
}


char *D3DTypeToString (D3DDEGREETYPE enumval)
{
	static char *pstr = NULL;

	switch (enumval)
	{
		case D3DDEGREE_LINEAR: pstr = "D3DDEGREE_LINEAR"; break;
		case D3DDEGREE_QUADRATIC: pstr = "D3DDEGREE_QUADRATIC"; break;
		case D3DDEGREE_CUBIC: pstr = "D3DDEGREE_CUBIC"; break;
		case D3DDEGREE_QUINTIC: pstr = "D3DDEGREE_QUINTIC"; break;
		default: pstr = "Unknown"; break;
	}

	return pstr;
}


char *D3DTypeToString (D3DDEVTYPE enumval)
{
	static char *pstr = NULL;

	switch (enumval)
	{
		case D3DDEVTYPE_HAL: pstr = "D3DDEVTYPE_HAL"; break;
		case D3DDEVTYPE_NULLREF: pstr = "D3DDEVTYPE_NULLREF"; break;
		case D3DDEVTYPE_REF: pstr = "D3DDEVTYPE_REF"; break;
		case D3DDEVTYPE_SW: pstr = "D3DDEVTYPE_SW"; break;
		default: pstr = "Unknown"; break;
	}

	return pstr;
}


char *D3DTypeToString (D3DFILLMODE enumval)
{
	static char *pstr = NULL;

	switch (enumval)
	{
		case D3DFILL_POINT: pstr = "D3DFILL_POINT"; break;
		case D3DFILL_WIREFRAME: pstr = "D3DFILL_WIREFRAME"; break;
		case D3DFILL_SOLID: pstr = "D3DFILL_SOLID"; break;
		default: pstr = "Unknown"; break;
	}

	return pstr;
}


char *D3DTypeToString (D3DFOGMODE enumval)
{
	static char *pstr = NULL;

	switch (enumval)
	{
		case D3DFOG_NONE: pstr = "D3DFOG_NONE"; break;
		case D3DFOG_EXP: pstr = "D3DFOG_EXP"; break;
		case D3DFOG_EXP2: pstr = "D3DFOG_EXP2"; break;
		case D3DFOG_LINEAR: pstr = "D3DFOG_LINEAR"; break;
		default: pstr = "Unknown"; break;
	}

	return pstr;
}


char *D3DTypeToString (D3DLIGHTTYPE enumval)
{
	static char *pstr = NULL;

	switch (enumval)
	{
		case D3DLIGHT_POINT: pstr = "D3DLIGHT_POINT"; break;
		case D3DLIGHT_SPOT: pstr = "D3DLIGHT_SPOT"; break;
		case D3DLIGHT_DIRECTIONAL: pstr = "D3DLIGHT_DIRECTIONAL"; break;
		default: pstr = "Unknown"; break;
	}

	return pstr;
}


char *D3DTypeToString (D3DMATERIALCOLORSOURCE enumval)
{
	static char *pstr = NULL;

	switch (enumval)
	{
		case D3DMCS_MATERIAL: pstr = "D3DMCS_MATERIAL"; break;
		case D3DMCS_COLOR1: pstr = "D3DMCS_COLOR1"; break;
		case D3DMCS_COLOR2: pstr = "D3DMCS_COLOR2"; break;
		default: pstr = "Unknown"; break;
	}

	return pstr;
}


char *D3DTypeToString (D3DPATCHEDGESTYLE enumval)
{
	static char *pstr = NULL;

	switch (enumval)
	{
		case D3DPATCHEDGE_DISCRETE: pstr = "D3DPATCHEDGE_DISCRETE"; break;
		case D3DPATCHEDGE_CONTINUOUS: pstr = "D3DPATCHEDGE_CONTINUOUS"; break;
		default: pstr = "Unknown"; break;
	}

	return pstr;
}


char *D3DTypeToString (D3DPOOL enumval)
{
	static char *pstr = NULL;

	switch (enumval)
	{
		case D3DPOOL_DEFAULT: pstr = "D3DPOOL_DEFAULT"; break;
		case D3DPOOL_MANAGED: pstr = "D3DPOOL_MANAGED"; break;
		case D3DPOOL_SYSTEMMEM: pstr = "D3DPOOL_SYSTEMMEM"; break;
		case D3DPOOL_SCRATCH: pstr = "D3DPOOL_SCRATCH"; break;
		default: pstr = "Unknown"; break;
	}

	return pstr;
}


char *D3DTypeToString (D3DPRIMITIVETYPE enumval)
{
	static char *pstr = NULL;

	switch (enumval)
	{
		case D3DPT_POINTLIST: pstr = "D3DPT_POINTLIST"; break;
		case D3DPT_LINELIST: pstr = "D3DPT_LINELIST"; break;
		case D3DPT_LINESTRIP: pstr = "D3DPT_LINESTRIP"; break;
		case D3DPT_TRIANGLELIST: pstr = "D3DPT_TRIANGLELIST"; break;
		case D3DPT_TRIANGLESTRIP: pstr = "D3DPT_TRIANGLESTRIP"; break;
		case D3DPT_TRIANGLEFAN: pstr = "D3DPT_TRIANGLEFAN"; break;
		default: pstr = "Unknown"; break;
	}

	return pstr;
}


char *D3DTypeToString (D3DQUERYTYPE enumval)
{
	static char *pstr = NULL;

	switch (enumval)
	{
		case D3DQUERYTYPE_VCACHE: pstr = "D3DQUERYTYPE_VCACHE"; break;
		case D3DQUERYTYPE_RESOURCEMANAGER: pstr = "D3DQUERYTYPE_RESOURCEMANAGER"; break;
		case D3DQUERYTYPE_VERTEXSTATS: pstr = "D3DQUERYTYPE_VERTEXSTATS"; break;
		case D3DQUERYTYPE_EVENT: pstr = "D3DQUERYTYPE_EVENT"; break;
		case D3DQUERYTYPE_OCCLUSION: pstr = "D3DQUERYTYPE_OCCLUSION"; break;
		case D3DQUERYTYPE_TIMESTAMP: pstr = "D3DQUERYTYPE_TIMESTAMP"; break;
		case D3DQUERYTYPE_TIMESTAMPDISJOINT: pstr = "D3DQUERYTYPE_TIMESTAMPDISJOINT"; break;
		case D3DQUERYTYPE_TIMESTAMPFREQ: pstr = "D3DQUERYTYPE_TIMESTAMPFREQ"; break;
		case D3DQUERYTYPE_PIPELINETIMINGS: pstr = "D3DQUERYTYPE_PIPELINETIMINGS"; break;
		case D3DQUERYTYPE_INTERFACETIMINGS: pstr = "D3DQUERYTYPE_INTERFACETIMINGS"; break;
		case D3DQUERYTYPE_VERTEXTIMINGS: pstr = "D3DQUERYTYPE_VERTEXTIMINGS"; break;
		case D3DQUERYTYPE_PIXELTIMINGS: pstr = "D3DQUERYTYPE_PIXELTIMINGS"; break;
		case D3DQUERYTYPE_BANDWIDTHTIMINGS: pstr = "D3DQUERYTYPE_BANDWIDTHTIMINGS"; break;
		case D3DQUERYTYPE_CACHEUTILIZATION: pstr = "D3DQUERYTYPE_CACHEUTILIZATION"; break;
		default: pstr = "Unknown"; break;
	}

	return pstr;
}


char *D3DTypeToString (D3DRENDERSTATETYPE enumval)
{
	static char *pstr = NULL;

	switch (enumval)
	{
		case D3DRS_ZENABLE: pstr = "D3DRS_ZENABLE"; break;
		case D3DRS_FILLMODE: pstr = "D3DRS_FILLMODE"; break;
		case D3DRS_SHADEMODE: pstr = "D3DRS_SHADEMODE"; break;
		case D3DRS_ZWRITEENABLE: pstr = "D3DRS_ZWRITEENABLE"; break;
		case D3DRS_ALPHATESTENABLE: pstr = "D3DRS_ALPHATESTENABLE"; break;
		case D3DRS_LASTPIXEL: pstr = "D3DRS_LASTPIXEL"; break;
		case D3DRS_SRCBLEND: pstr = "D3DRS_SRCBLEND"; break;
		case D3DRS_DESTBLEND: pstr = "D3DRS_DESTBLEND"; break;
		case D3DRS_CULLMODE: pstr = "D3DRS_CULLMODE"; break;
		case D3DRS_ZFUNC: pstr = "D3DRS_ZFUNC"; break;
		case D3DRS_ALPHAREF: pstr = "D3DRS_ALPHAREF"; break;
		case D3DRS_ALPHAFUNC: pstr = "D3DRS_ALPHAFUNC"; break;
		case D3DRS_DITHERENABLE: pstr = "D3DRS_DITHERENABLE"; break;
		case D3DRS_ALPHABLENDENABLE: pstr = "D3DRS_ALPHABLENDENABLE"; break;
		case D3DRS_FOGENABLE: pstr = "D3DRS_FOGENABLE"; break;
		case D3DRS_SPECULARENABLE: pstr = "D3DRS_SPECULARENABLE"; break;
		case D3DRS_FOGCOLOR: pstr = "D3DRS_FOGCOLOR"; break;
		case D3DRS_FOGTABLEMODE: pstr = "D3DRS_FOGTABLEMODE"; break;
		case D3DRS_FOGSTART: pstr = "D3DRS_FOGSTART"; break;
		case D3DRS_FOGEND: pstr = "D3DRS_FOGEND"; break;
		case D3DRS_FOGDENSITY: pstr = "D3DRS_FOGDENSITY"; break;
		case D3DRS_RANGEFOGENABLE: pstr = "D3DRS_RANGEFOGENABLE"; break;
		case D3DRS_STENCILENABLE: pstr = "D3DRS_STENCILENABLE"; break;
		case D3DRS_STENCILFAIL: pstr = "D3DRS_STENCILFAIL"; break;
		case D3DRS_STENCILZFAIL: pstr = "D3DRS_STENCILZFAIL"; break;
		case D3DRS_STENCILPASS: pstr = "D3DRS_STENCILPASS"; break;
		case D3DRS_STENCILFUNC: pstr = "D3DRS_STENCILFUNC"; break;
		case D3DRS_STENCILREF: pstr = "D3DRS_STENCILREF"; break;
		case D3DRS_STENCILMASK: pstr = "D3DRS_STENCILMASK"; break;
		case D3DRS_STENCILWRITEMASK: pstr = "D3DRS_STENCILWRITEMASK"; break;
		case D3DRS_TEXTUREFACTOR: pstr = "D3DRS_TEXTUREFACTOR"; break;
		case D3DRS_WRAP0: pstr = "D3DRS_WRAP0"; break;
		case D3DRS_WRAP1: pstr = "D3DRS_WRAP1"; break;
		case D3DRS_WRAP2: pstr = "D3DRS_WRAP2"; break;
		case D3DRS_WRAP3: pstr = "D3DRS_WRAP3"; break;
		case D3DRS_WRAP4: pstr = "D3DRS_WRAP4"; break;
		case D3DRS_WRAP5: pstr = "D3DRS_WRAP5"; break;
		case D3DRS_WRAP6: pstr = "D3DRS_WRAP6"; break;
		case D3DRS_WRAP7: pstr = "D3DRS_WRAP7"; break;
		case D3DRS_CLIPPING: pstr = "D3DRS_CLIPPING"; break;
		case D3DRS_LIGHTING: pstr = "D3DRS_LIGHTING"; break;
		case D3DRS_AMBIENT: pstr = "D3DRS_AMBIENT"; break;
		case D3DRS_FOGVERTEXMODE: pstr = "D3DRS_FOGVERTEXMODE"; break;
		case D3DRS_COLORVERTEX: pstr = "D3DRS_COLORVERTEX"; break;
		case D3DRS_LOCALVIEWER: pstr = "D3DRS_LOCALVIEWER"; break;
		case D3DRS_NORMALIZENORMALS: pstr = "D3DRS_NORMALIZENORMALS"; break;
		case D3DRS_DIFFUSEMATERIALSOURCE: pstr = "D3DRS_DIFFUSEMATERIALSOURCE"; break;
		case D3DRS_SPECULARMATERIALSOURCE: pstr = "D3DRS_SPECULARMATERIALSOURCE"; break;
		case D3DRS_AMBIENTMATERIALSOURCE: pstr = "D3DRS_AMBIENTMATERIALSOURCE"; break;
		case D3DRS_EMISSIVEMATERIALSOURCE: pstr = "D3DRS_EMISSIVEMATERIALSOURCE"; break;
		case D3DRS_VERTEXBLEND: pstr = "D3DRS_VERTEXBLEND"; break;
		case D3DRS_CLIPPLANEENABLE: pstr = "D3DRS_CLIPPLANEENABLE"; break;
		case D3DRS_POINTSIZE: pstr = "D3DRS_POINTSIZE"; break;
		case D3DRS_POINTSIZE_MIN: pstr = "D3DRS_POINTSIZE_MIN"; break;
		case D3DRS_POINTSPRITEENABLE: pstr = "D3DRS_POINTSPRITEENABLE"; break;
		case D3DRS_POINTSCALEENABLE: pstr = "D3DRS_POINTSCALEENABLE"; break;
		case D3DRS_POINTSCALE_A: pstr = "D3DRS_POINTSCALE_A"; break;
		case D3DRS_POINTSCALE_B: pstr = "D3DRS_POINTSCALE_B"; break;
		case D3DRS_POINTSCALE_C: pstr = "D3DRS_POINTSCALE_C"; break;
		case D3DRS_MULTISAMPLEANTIALIAS: pstr = "D3DRS_MULTISAMPLEANTIALIAS"; break;
		case D3DRS_MULTISAMPLEMASK: pstr = "D3DRS_MULTISAMPLEMASK"; break;
		case D3DRS_PATCHEDGESTYLE: pstr = "D3DRS_PATCHEDGESTYLE"; break;
		case D3DRS_DEBUGMONITORTOKEN: pstr = "D3DRS_DEBUGMONITORTOKEN"; break;
		case D3DRS_POINTSIZE_MAX: pstr = "D3DRS_POINTSIZE_MAX"; break;
		case D3DRS_INDEXEDVERTEXBLENDENABLE: pstr = "D3DRS_INDEXEDVERTEXBLENDENABLE"; break;
		case D3DRS_COLORWRITEENABLE: pstr = "D3DRS_COLORWRITEENABLE"; break;
		case D3DRS_TWEENFACTOR: pstr = "D3DRS_TWEENFACTOR"; break;
		case D3DRS_BLENDOP: pstr = "D3DRS_BLENDOP"; break;
		case D3DRS_POSITIONDEGREE: pstr = "D3DRS_POSITIONDEGREE"; break;
		case D3DRS_NORMALDEGREE: pstr = "D3DRS_NORMALDEGREE"; break;
		case D3DRS_SCISSORTESTENABLE: pstr = "D3DRS_SCISSORTESTENABLE"; break;
		case D3DRS_SLOPESCALEDEPTHBIAS: pstr = "D3DRS_SLOPESCALEDEPTHBIAS"; break;
		case D3DRS_ANTIALIASEDLINEENABLE: pstr = "D3DRS_ANTIALIASEDLINEENABLE"; break;
		case D3DRS_MINTESSELLATIONLEVEL: pstr = "D3DRS_MINTESSELLATIONLEVEL"; break;
		case D3DRS_MAXTESSELLATIONLEVEL: pstr = "D3DRS_MAXTESSELLATIONLEVEL"; break;
		case D3DRS_ADAPTIVETESS_X: pstr = "D3DRS_ADAPTIVETESS_X"; break;
		case D3DRS_ADAPTIVETESS_Y: pstr = "D3DRS_ADAPTIVETESS_Y"; break;
		case D3DRS_ADAPTIVETESS_Z: pstr = "D3DRS_ADAPTIVETESS_Z"; break;
		case D3DRS_ADAPTIVETESS_W: pstr = "D3DRS_ADAPTIVETESS_W"; break;
		case D3DRS_ENABLEADAPTIVETESSELLATION: pstr = "D3DRS_ENABLEADAPTIVETESSELLATION"; break;
		case D3DRS_TWOSIDEDSTENCILMODE: pstr = "D3DRS_TWOSIDEDSTENCILMODE"; break;
		case D3DRS_CCW_STENCILFAIL: pstr = "D3DRS_CCW_STENCILFAIL"; break;
		case D3DRS_CCW_STENCILZFAIL: pstr = "D3DRS_CCW_STENCILZFAIL"; break;
		case D3DRS_CCW_STENCILPASS: pstr = "D3DRS_CCW_STENCILPASS"; break;
		case D3DRS_CCW_STENCILFUNC: pstr = "D3DRS_CCW_STENCILFUNC"; break;
		case D3DRS_COLORWRITEENABLE1: pstr = "D3DRS_COLORWRITEENABLE1"; break;
		case D3DRS_COLORWRITEENABLE2: pstr = "D3DRS_COLORWRITEENABLE2"; break;
		case D3DRS_COLORWRITEENABLE3: pstr = "D3DRS_COLORWRITEENABLE3"; break;
		case D3DRS_BLENDFACTOR: pstr = "D3DRS_BLENDFACTOR"; break;
		case D3DRS_SRGBWRITEENABLE: pstr = "D3DRS_SRGBWRITEENABLE"; break;
		case D3DRS_DEPTHBIAS: pstr = "D3DRS_DEPTHBIAS"; break;
		case D3DRS_WRAP8: pstr = "D3DRS_WRAP8"; break;
		case D3DRS_WRAP9: pstr = "D3DRS_WRAP9"; break;
		case D3DRS_WRAP10: pstr = "D3DRS_WRAP10"; break;
		case D3DRS_WRAP11: pstr = "D3DRS_WRAP11"; break;
		case D3DRS_WRAP12: pstr = "D3DRS_WRAP12"; break;
		case D3DRS_WRAP13: pstr = "D3DRS_WRAP13"; break;
		case D3DRS_WRAP14: pstr = "D3DRS_WRAP14"; break;
		case D3DRS_WRAP15: pstr = "D3DRS_WRAP15"; break;
		case D3DRS_SEPARATEALPHABLENDENABLE: pstr = "D3DRS_SEPARATEALPHABLENDENABLE"; break;
		case D3DRS_SRCBLENDALPHA: pstr = "D3DRS_SRCBLENDALPHA"; break;
		case D3DRS_DESTBLENDALPHA: pstr = "D3DRS_DESTBLENDALPHA"; break;
		case D3DRS_BLENDOPALPHA: pstr = "D3DRS_BLENDOPALPHA"; break;
		default: pstr = "Unknown"; break;
	}

	return pstr;
}


char *D3DTypeToString (D3DRESOURCETYPE enumval)
{
	static char *pstr = NULL;

	switch (enumval)
	{
		case D3DRTYPE_SURFACE: pstr = "D3DRTYPE_SURFACE"; break;
		case D3DRTYPE_VOLUME: pstr = "D3DRTYPE_VOLUME"; break;
		case D3DRTYPE_TEXTURE: pstr = "D3DRTYPE_TEXTURE"; break;
		case D3DRTYPE_VOLUMETEXTURE: pstr = "D3DRTYPE_VOLUMETEXTURE"; break;
		case D3DRTYPE_CUBETEXTURE: pstr = "D3DRTYPE_CUBETEXTURE"; break;
		case D3DRTYPE_VERTEXBUFFER: pstr = "D3DRTYPE_VERTEXBUFFER"; break;
		case D3DRTYPE_INDEXBUFFER: pstr = "D3DRTYPE_INDEXBUFFER"; break;
		default: pstr = "Unknown"; break;
	}

	return pstr;
}


char *D3DTypeToString (D3DSAMPLER_TEXTURE_TYPE enumval)
{
	static char *pstr = NULL;

	switch (enumval)
	{
		case D3DSTT_UNKNOWN: pstr = "D3DSTT_UNKNOWN"; break;
		case D3DSTT_2D: pstr = "D3DSTT_2D"; break;
		case D3DSTT_CUBE: pstr = "D3DSTT_CUBE"; break;
		case D3DSTT_VOLUME: pstr = "D3DSTT_VOLUME"; break;
		default: pstr = "Unknown"; break;
	}

	return pstr;
}


char *D3DTypeToString (D3DSAMPLERSTATETYPE enumval)
{
	static char *pstr = NULL;

	switch (enumval)
	{
		case D3DSAMP_ADDRESSU: pstr = "D3DSAMP_ADDRESSU"; break;
		case D3DSAMP_ADDRESSV: pstr = "D3DSAMP_ADDRESSV"; break;
		case D3DSAMP_ADDRESSW: pstr = "D3DSAMP_ADDRESSW"; break;
		case D3DSAMP_BORDERCOLOR: pstr = "D3DSAMP_BORDERCOLOR"; break;
		case D3DSAMP_MAGFILTER: pstr = "D3DSAMP_MAGFILTER"; break;
		case D3DSAMP_MINFILTER: pstr = "D3DSAMP_MINFILTER"; break;
		case D3DSAMP_MIPFILTER: pstr = "D3DSAMP_MIPFILTER"; break;
		case D3DSAMP_MIPMAPLODBIAS: pstr = "D3DSAMP_MIPMAPLODBIAS"; break;
		case D3DSAMP_MAXMIPLEVEL: pstr = "D3DSAMP_MAXMIPLEVEL"; break;
		case D3DSAMP_MAXANISOTROPY: pstr = "D3DSAMP_MAXANISOTROPY"; break;
		case D3DSAMP_SRGBTEXTURE: pstr = "D3DSAMP_SRGBTEXTURE"; break;
		case D3DSAMP_ELEMENTINDEX: pstr = "D3DSAMP_ELEMENTINDEX"; break;
		case D3DSAMP_DMAPOFFSET: pstr = "D3DSAMP_DMAPOFFSET"; break;
		default: pstr = "Unknown"; break;
	}

	return pstr;
}


char *D3DTypeToString (D3DSHADEMODE enumval)
{
	static char *pstr = NULL;

	switch (enumval)
	{
		case D3DSHADE_FLAT: pstr = "D3DSHADE_FLAT"; break;
		case D3DSHADE_GOURAUD: pstr = "D3DSHADE_GOURAUD"; break;
		case D3DSHADE_PHONG: pstr = "D3DSHADE_PHONG"; break;
		default: pstr = "Unknown"; break;
	}

	return pstr;
}


char *D3DTypeToString (D3DSTATEBLOCKTYPE enumval)
{
	static char *pstr = NULL;

	switch (enumval)
	{
		case D3DSBT_ALL: pstr = "D3DSBT_ALL"; break;
		case D3DSBT_PIXELSTATE: pstr = "D3DSBT_PIXELSTATE"; break;
		case D3DSBT_VERTEXSTATE: pstr = "D3DSBT_VERTEXSTATE"; break;
		default: pstr = "Unknown"; break;
	}

	return pstr;
}


char *D3DTypeToString (D3DSTENCILOP enumval)
{
	static char *pstr = NULL;

	switch (enumval)
	{
		case D3DSTENCILOP_KEEP: pstr = "D3DSTENCILOP_KEEP"; break;
		case D3DSTENCILOP_ZERO: pstr = "D3DSTENCILOP_ZERO"; break;
		case D3DSTENCILOP_REPLACE: pstr = "D3DSTENCILOP_REPLACE"; break;
		case D3DSTENCILOP_INCRSAT: pstr = "D3DSTENCILOP_INCRSAT"; break;
		case D3DSTENCILOP_DECRSAT: pstr = "D3DSTENCILOP_DECRSAT"; break;
		case D3DSTENCILOP_INVERT: pstr = "D3DSTENCILOP_INVERT"; break;
		case D3DSTENCILOP_INCR: pstr = "D3DSTENCILOP_INCR"; break;
		case D3DSTENCILOP_DECR: pstr = "D3DSTENCILOP_DECR"; break;
		default: pstr = "Unknown"; break;
	}

	return pstr;
}


char *D3DTypeToString (D3DSWAPEFFECT enumval)
{
	static char *pstr = NULL;

	switch (enumval)
	{
		case D3DSWAPEFFECT_DISCARD: pstr = "D3DSWAPEFFECT_DISCARD"; break;
		case D3DSWAPEFFECT_FLIP: pstr = "D3DSWAPEFFECT_FLIP"; break;
		case D3DSWAPEFFECT_COPY: pstr = "D3DSWAPEFFECT_COPY"; break;
		default: pstr = "Unknown"; break;
	}

	return pstr;
}


char *D3DTypeToString (D3DTEXTUREADDRESS enumval)
{
	static char *pstr = NULL;

	switch (enumval)
	{
		case D3DTADDRESS_WRAP: pstr = "D3DTADDRESS_WRAP"; break;
		case D3DTADDRESS_MIRROR: pstr = "D3DTADDRESS_MIRROR"; break;
		case D3DTADDRESS_CLAMP: pstr = "D3DTADDRESS_CLAMP"; break;
		case D3DTADDRESS_BORDER: pstr = "D3DTADDRESS_BORDER"; break;
		case D3DTADDRESS_MIRRORONCE: pstr = "D3DTADDRESS_MIRRORONCE"; break;
		default: pstr = "Unknown"; break;
	}

	return pstr;
}


char *D3DTypeToString (D3DTEXTUREFILTERTYPE enumval)
{
	static char *pstr = NULL;

	switch (enumval)
	{
		case D3DTEXF_NONE: pstr = "D3DTEXF_NONE"; break;
		case D3DTEXF_POINT: pstr = "D3DTEXF_POINT"; break;
		case D3DTEXF_LINEAR: pstr = "D3DTEXF_LINEAR"; break;
		case D3DTEXF_ANISOTROPIC: pstr = "D3DTEXF_ANISOTROPIC"; break;
		case D3DTEXF_PYRAMIDALQUAD: pstr = "D3DTEXF_PYRAMIDALQUAD"; break;
		case D3DTEXF_GAUSSIANQUAD: pstr = "D3DTEXF_GAUSSIANQUAD"; break;
		default: pstr = "Unknown"; break;
	}

	return pstr;
}


char *D3DTypeToString (D3DTEXTUREOP enumval)
{
	static char *pstr = NULL;

	switch (enumval)
	{
		case D3DTOP_DISABLE: pstr = "D3DTOP_DISABLE"; break;
		case D3DTOP_SELECTARG1: pstr = "D3DTOP_SELECTARG1"; break;
		case D3DTOP_SELECTARG2: pstr = "D3DTOP_SELECTARG2"; break;
		case D3DTOP_MODULATE: pstr = "D3DTOP_MODULATE"; break;
		case D3DTOP_MODULATE2X: pstr = "D3DTOP_MODULATE2X"; break;
		case D3DTOP_MODULATE4X: pstr = "D3DTOP_MODULATE4X"; break;
		case D3DTOP_ADD: pstr = "D3DTOP_ADD"; break;
		case D3DTOP_ADDSIGNED: pstr = "D3DTOP_ADDSIGNED"; break;
		case D3DTOP_ADDSIGNED2X: pstr = "D3DTOP_ADDSIGNED2X"; break;
		case D3DTOP_SUBTRACT: pstr = "D3DTOP_SUBTRACT"; break;
		case D3DTOP_ADDSMOOTH: pstr = "D3DTOP_ADDSMOOTH"; break;
		case D3DTOP_BLENDDIFFUSEALPHA: pstr = "D3DTOP_BLENDDIFFUSEALPHA"; break;
		case D3DTOP_BLENDTEXTUREALPHA: pstr = "D3DTOP_BLENDTEXTUREALPHA"; break;
		case D3DTOP_BLENDFACTORALPHA: pstr = "D3DTOP_BLENDFACTORALPHA"; break;
		case D3DTOP_BLENDTEXTUREALPHAPM: pstr = "D3DTOP_BLENDTEXTUREALPHAPM"; break;
		case D3DTOP_BLENDCURRENTALPHA: pstr = "D3DTOP_BLENDCURRENTALPHA"; break;
		case D3DTOP_PREMODULATE: pstr = "D3DTOP_PREMODULATE"; break;
		case D3DTOP_MODULATEALPHA_ADDCOLOR: pstr = "D3DTOP_MODULATEALPHA_ADDCOLOR"; break;
		case D3DTOP_MODULATECOLOR_ADDALPHA: pstr = "D3DTOP_MODULATECOLOR_ADDALPHA"; break;
		case D3DTOP_MODULATEINVALPHA_ADDCOLOR: pstr = "D3DTOP_MODULATEINVALPHA_ADDCOLOR"; break;
		case D3DTOP_MODULATEINVCOLOR_ADDALPHA: pstr = "D3DTOP_MODULATEINVCOLOR_ADDALPHA"; break;
		case D3DTOP_BUMPENVMAP: pstr = "D3DTOP_BUMPENVMAP"; break;
		case D3DTOP_BUMPENVMAPLUMINANCE: pstr = "D3DTOP_BUMPENVMAPLUMINANCE"; break;
		case D3DTOP_DOTPRODUCT3: pstr = "D3DTOP_DOTPRODUCT3"; break;
		case D3DTOP_MULTIPLYADD: pstr = "D3DTOP_MULTIPLYADD"; break;
		case D3DTOP_LERP: pstr = "D3DTOP_LERP"; break;
		default: pstr = "Unknown"; break;
	}

	return pstr;
}


char *D3DTypeToString (D3DTEXTURESTAGESTATETYPE enumval)
{
	static char *pstr = NULL;

	switch (enumval)
	{
		case D3DTSS_COLOROP: pstr = "D3DTSS_COLOROP"; break;
		case D3DTSS_COLORARG1: pstr = "D3DTSS_COLORARG1"; break;
		case D3DTSS_COLORARG2: pstr = "D3DTSS_COLORARG2"; break;
		case D3DTSS_ALPHAOP: pstr = "D3DTSS_ALPHAOP"; break;
		case D3DTSS_ALPHAARG1: pstr = "D3DTSS_ALPHAARG1"; break;
		case D3DTSS_ALPHAARG2: pstr = "D3DTSS_ALPHAARG2"; break;
		case D3DTSS_BUMPENVMAT00: pstr = "D3DTSS_BUMPENVMAT00"; break;
		case D3DTSS_BUMPENVMAT01: pstr = "D3DTSS_BUMPENVMAT01"; break;
		case D3DTSS_BUMPENVMAT10: pstr = "D3DTSS_BUMPENVMAT10"; break;
		case D3DTSS_BUMPENVMAT11: pstr = "D3DTSS_BUMPENVMAT11"; break;
		case D3DTSS_TEXCOORDINDEX: pstr = "D3DTSS_TEXCOORDINDEX"; break;
		case D3DTSS_BUMPENVLSCALE: pstr = "D3DTSS_BUMPENVLSCALE"; break;
		case D3DTSS_BUMPENVLOFFSET: pstr = "D3DTSS_BUMPENVLOFFSET"; break;
		case D3DTSS_TEXTURETRANSFORMFLAGS: pstr = "D3DTSS_TEXTURETRANSFORMFLAGS"; break;
		case D3DTSS_COLORARG0: pstr = "D3DTSS_COLORARG0"; break;
		case D3DTSS_ALPHAARG0: pstr = "D3DTSS_ALPHAARG0"; break;
		case D3DTSS_RESULTARG: pstr = "D3DTSS_RESULTARG"; break;
		case D3DTSS_CONSTANT: pstr = "D3DTSS_CONSTANT"; break;
		default: pstr = "Unknown"; break;
	}

	return pstr;
}


char *D3DTypeToString (D3DTEXTURETRANSFORMFLAGS enumval)
{
	static char *pstr = NULL;

	switch (enumval)
	{
		case D3DTTFF_DISABLE: pstr = "D3DTTFF_DISABLE"; break;
		case D3DTTFF_COUNT1: pstr = "D3DTTFF_COUNT1"; break;
		case D3DTTFF_COUNT2: pstr = "D3DTTFF_COUNT2"; break;
		case D3DTTFF_COUNT3: pstr = "D3DTTFF_COUNT3"; break;
		case D3DTTFF_COUNT4: pstr = "D3DTTFF_COUNT4"; break;
		case D3DTTFF_PROJECTED: pstr = "D3DTTFF_PROJECTED"; break;
		default: pstr = "Unknown"; break;
	}

	return pstr;
}


char *D3DTypeToString (D3DTRANSFORMSTATETYPE enumval)
{
	static char *pstr = NULL;

	switch (enumval)
	{
		case D3DTS_VIEW: pstr = "D3DTS_VIEW"; break;
		case D3DTS_PROJECTION: pstr = "D3DTS_PROJECTION"; break;
		case D3DTS_TEXTURE0: pstr = "D3DTS_TEXTURE0"; break;
		case D3DTS_TEXTURE1: pstr = "D3DTS_TEXTURE1"; break;
		case D3DTS_TEXTURE2: pstr = "D3DTS_TEXTURE2"; break;
		case D3DTS_TEXTURE3: pstr = "D3DTS_TEXTURE3"; break;
		case D3DTS_TEXTURE4: pstr = "D3DTS_TEXTURE4"; break;
		case D3DTS_TEXTURE5: pstr = "D3DTS_TEXTURE5"; break;
		case D3DTS_TEXTURE6: pstr = "D3DTS_TEXTURE6"; break;
		case D3DTS_TEXTURE7: pstr = "D3DTS_TEXTURE7"; break;
		default: pstr = "Unknown"; break;
	}

	return pstr;
}


char *D3DTypeToString (D3DVERTEXBLENDFLAGS enumval)
{
	static char *pstr = NULL;

	switch (enumval)
	{
		case D3DVBF_DISABLE: pstr = "D3DVBF_DISABLE"; break;
		case D3DVBF_1WEIGHTS: pstr = "D3DVBF_1WEIGHTS"; break;
		case D3DVBF_2WEIGHTS: pstr = "D3DVBF_2WEIGHTS"; break;
		case D3DVBF_3WEIGHTS: pstr = "D3DVBF_3WEIGHTS"; break;
		case D3DVBF_TWEENING: pstr = "D3DVBF_TWEENING"; break;
		case D3DVBF_0WEIGHTS: pstr = "D3DVBF_0WEIGHTS"; break;
		default: pstr = "Unknown"; break;
	}

	return pstr;
}


char *D3DTypeToString (D3DZBUFFERTYPE enumval)
{
	static char *pstr = NULL;

	switch (enumval)
	{
		case D3DZB_FALSE: pstr = "D3DZB_FALSE"; break;
		case D3DZB_TRUE: pstr = "D3DZB_TRUE"; break;
		case D3DZB_USEW: pstr = "D3DZB_USEW"; break;
		default: pstr = "Unknown"; break;
	}

	return pstr;
}

