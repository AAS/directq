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



#include <d3d9.h>
#include <d3dx9.h>

void D3D_SetFVFStateManaged (DWORD NewFVF);

extern LPDIRECT3DVERTEXBUFFER9 d3d_BrushModelVerts;
extern LPDIRECT3DINDEXBUFFER9 d3d_BrushModelIndexes;
extern int d3d_VertexBufferVerts;
extern DWORD d3d_VertexBufferUsage;
extern D3DFORMAT d3d_BrushIndexFormat;

// video
void D3D_InitDirect3D (D3DDISPLAYMODE *mode);
void D3D_ShutdownDirect3D (void);
void D3D_BeginRendering (int *x, int *y, int *width, int *height);
void D3D_EndRendering (void);

extern D3DDISPLAYMODE d3d_CurrentMode;

// this is stored out so that it can be used subsequently
extern D3DVIEWPORT9 d3d_3DViewport;

// we'll store this out too in case we ever want to do anything with it
extern D3DVIEWPORT9 d3d_2DViewport;


// textures
#define D3D_TEXTURE0	0
#define D3D_TEXTURE1	1
#define D3D_TEXTURE2	2
#define D3D_TEXTURE3	3
#define D3D_TEXTURE4	4
#define D3D_TEXTURE5	5
#define D3D_TEXTURE6	6
#define D3D_TEXTURE7	7

#define IMAGE_MIPMAP	1
#define IMAGE_ALPHA		2
#define IMAGE_32BIT		4
#define IMAGE_PRESERVE	8
#define IMAGE_LIQUID	16
#define IMAGE_BSP		32
#define IMAGE_ALIAS		64
#define IMAGE_SPRITE	128
#define IMAGE_LUMA		256

typedef struct image_s
{
	char identifier[64];
	int width;
	int height;
	unsigned int *palette;
	byte *data;
	byte hash[16];
	int flags;
} image_t;

typedef struct d3d_texture_s
{
	LPDIRECT3DTEXTURE9 d3d_Texture;
	image_t TexImage;
	int LastUsage;

	struct d3d_texture_s *next;
} d3d_texture_t;


LPDIRECT3DTEXTURE9 D3D_LoadTexture (char *identifier, int width, int height, byte *data, bool mipmap, bool alpha);
LPDIRECT3DTEXTURE9 D3D_LoadTexture (image_t *image);
void D3D_LoadTexture (LPDIRECT3DTEXTURE9 *tex, image_t *image);
void D3D_LoadTexture (LPDIRECT3DTEXTURE9 *tex, int width, int height, byte *data, unsigned int *palette, bool mipmap, bool alpha);
LPDIRECT3DTEXTURE9 D3D_LoadTexture (int width, int height, byte *data, int flags);
bool D3D_LoadExternalTexture (LPDIRECT3DTEXTURE9 *tex, char *filename, int flags);
void D3D_LoadResourceTexture (LPDIRECT3DTEXTURE9 *tex, int ResourceID, int flags);
LPDIRECT3DTEXTURE9 D3D_LoadTexture (char *identifier, int width, int height, byte *data, int flags);
LPDIRECT3DTEXTURE9 D3D_LoadTexture (miptex_t *mt, int flags);
void D3D_ReleaseTextures (void);
void D3D_FlushTextures (void);

extern LPDIRECT3DTEXTURE9 r_notexture;

// lightmaps
void D3D_CreateSurfaceLightmap (msurface_t *surf);
void D3D_UploadLightmaps (void);
void D3D_ReleaseLightmaps (void);
LPDIRECT3DTEXTURE9 D3D_GetLightmap (void *lm);
void D3D_CheckLightmapModification (msurface_t *surf);
void D3D_UnlockLightmaps (void);

// global stuff
extern LPDIRECT3D9 d3d_Object;
extern LPDIRECT3DDEVICE9 d3d_Device;
extern D3DADAPTER_IDENTIFIER9 d3d_Adapter;
extern D3DCAPS9 d3d_DeviceCaps;

// matrixes
extern LPD3DXMATRIXSTACK d3d_WorldMatrixStack;
extern D3DXMATRIX d3d_ViewMatrix;
extern D3DXMATRIX d3d_WorldMatrix;
extern D3DXMATRIX d3d_PerspectiveMatrix;
extern D3DXMATRIX d3d_OrthoMatrix;

// scaling factors for x and y coords in the 2D view;
#define SCALE_2D_X(x) (((x) * glwidth) / 640)
#define SCALE_2D_Y(y) (((y) * glheight) / 480)

// state changes
void D3D_Set2D (void);

// x/y/z vector shortcuts
extern D3DXVECTOR3 XVECTOR;
extern D3DXVECTOR3 YVECTOR;
extern D3DXVECTOR3 ZVECTOR;

// global caps - used for any specific settings that we choose rather than that are
// enforced on us through device caps
typedef struct d3d_global_caps_s
{
	bool AllowA16B16G16R16;
	D3DFORMAT DepthStencilFormat;
	bool isNvidia;
	bool supportSRGB;
} d3d_global_caps_t;

extern d3d_global_caps_t d3d_GlobalCaps;

// using byte colours and having the macros wrap may have seemed like a good idea to someone somewhere sometime
// say in 1996, when every single byte or cpu cycle was precious...
#define BYTE_CLAMP(i) (int) ((((i) > 255) ? 255 : (((i) < 0) ? 0 : (i))))

// useful in various places
extern float r_frametime;

// renderflags for global updates
#define R_RENDERABOVEWATER			(1 << 0)
#define R_RENDERUNDERWATER			(1 << 1)
#define R_RENDERWATERSURFACE		(1 << 2)
#define R_RENDERALIAS				(1 << 3)
#define R_RENDERSPRITE				(1 << 4)
#define R_RENDERINSTANCEDBRUSH		(1 << 5)
#define R_RENDERINLINEBRUSH			(1 << 6)
#define R_RENDERLUMA				(1 << 7)
#define R_RENDERNOLUMA				(1 << 8)

extern int r_renderflags;

// generic world surface with verts and two sets of texcoords
typedef struct worldvert_s
{
	float xyz[3];
	float st[2];
	float lm[2];

	// force the vertex buffer to compile to a 32-byte size multiple
	DWORD dummy;
} worldvert_t;

extern cvar_t r_64bitlightmaps;

void D3D_BackfaceCull (DWORD D3D_CULLTYPE);

// colour space transform
extern float *r_activetransform;
float D3D_TransformColourSpace (float in);
byte D3D_TransformColourSpaceByte (byte in);
extern cvar_t r_sRGBgamma;