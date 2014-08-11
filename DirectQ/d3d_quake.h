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

extern HRESULT hr;

/*
==============================================================================================================================

				LIGHTMAP CLASS

	This is a linked list that is only accessible through it's head pointer; all list walking is done internally.

==============================================================================================================================
*/

class CD3DLightmap
{
public:
	CD3DLightmap (msurface_t *surf);
	~CD3DLightmap (void);
	void Upload (void);
	void UploadModified (void);
	void CalcLightmapTexCoords (msurface_t *surf, float *v, float *st);
	void CheckSurfaceForModification (msurface_t *surf);
	bool AllocBlock (msurface_t *surf);
	void BindLightmap (int stage);

private:
	bool modified;
	int size;
	int *allocated;
	RECT DirtyRect;
	D3DLOCKED_RECT LockedRect;
	LPDIRECT3DTEXTURE9 d3d_Texture;

	// next lightmap in the chain
	CD3DLightmap *next;
};


extern CD3DLightmap *d3d_Lightmaps;


/*
=======================================================================================================================

replicates the D3DXMatrixStack class but DOESN'T require creation and releasing of a fucking COM object...!

also mimics OpenGL more closely in the operation of some functions (particularly rotation) and includes a few extra
helper members for convenience

=======================================================================================================================
*/


// 32 is standard for OpenGL so copy that
#define MAX_MATRIX_STACK_DEPTH		32


class CD3D_MatrixStack
{
public:
	CD3D_MatrixStack (D3DTRANSFORMSTATETYPE trans);
	void Push (void);
	void Pop (void);
	void Reset (void);
	void ResetIdentity (void);
	void LoadIdentity (void);
	void Rotate (float x, float y, float z, float angle);
	void Translate (float x, float y, float z);
	void Translatev (float *v);
	void Scale (float x, float y, float z);
	void Scalev (float *v);
	D3DXMATRIX *GetTop (void);
	void GetMatrix (D3DXMATRIX *m);
	void SetMatrix (D3DXMATRIX *m);
	void Ortho2D (float left, float right, float bottom, float top, float znear, float zfar);
	void Perspective3D (float fovy, float screenaspect, float znear, float zfar);
	void Frustum3D (float fovx, float fovy, float znear, float zfar);
	void CheckDirtyState (void);

private:
	D3DXMATRIX theStack[MAX_MATRIX_STACK_DEPTH];
	D3DTRANSFORMSTATETYPE usage;
	int currdepth;
	bool pushed;
	bool dirty;
	bool RHtoLH;
};


// generic world surface with verts and two sets of texcoords
typedef struct worldvert_s
{
	float xyz[3];
	float st[2];
	float lm[2];

	// force the vertex buffer to compile to a 32-byte size multiple
	DWORD dummy;
} worldvert_t;

extern DWORD d3d_VertexBufferUsage;

// video
void D3D_InitDirect3D (D3DDISPLAYMODE *mode);
void D3D_ShutdownDirect3D (void);
void D3D_BeginRendering (int *x, int *y, int *width, int *height);
void D3D_EndRendering (void);

extern D3DDISPLAYMODE d3d_CurrentMode;


// textures
#define IMAGE_MIPMAP		1
#define IMAGE_ALPHA			2
#define IMAGE_32BIT			4
#define IMAGE_PRESERVE		8
#define IMAGE_LIQUID		16
#define IMAGE_BSP			32
#define IMAGE_ALIAS			64
#define IMAGE_SPRITE		128
#define IMAGE_LUMA			256
#define IMAGE_NOCOMPRESS	512
#define IMAGE_RMQRAIN		1024
#define IMAGE_NOEXTERN		2048


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

// global stuff
extern LPDIRECT3D9 d3d_Object;
extern LPDIRECT3DDEVICE9 d3d_Device;
extern D3DADAPTER_IDENTIFIER9 d3d_Adapter;
extern D3DCAPS9 d3d_DeviceCaps;

// matrixes
extern CD3D_MatrixStack *d3d_WorldMatrixStack;
extern CD3D_MatrixStack *d3d_ViewMatrixStack;
extern CD3D_MatrixStack *d3d_ProjMatrixStack;

// state changes
void D3D_Set2D (void);

// global caps - used for any specific settings that we choose rather than that are
// enforced on us through device caps
typedef struct d3d_global_caps_s
{
	D3DFORMAT DepthStencilFormat;
	bool supportDXT1;
	bool supportDXT3;
	bool supportDXT5;
} d3d_global_caps_t;

extern d3d_global_caps_t d3d_GlobalCaps;

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
#define R_RENDERALPHAWATER			(1 << 9)
#define R_RENDEROPAQUEWATER			(1 << 10)
#define R_RENDERALPHASURFACE		(1 << 11)
#define R_RENDEROPAQUEBRUSH			(1 << 12)
#define R_RENDERALPHABRUSH			(1 << 13)
#define R_RENDERFULLBRIGHTALIAS		(1 << 16)
#define R_RENDERTEXTUREDALIAS		(1 << 17)
#define R_RENDERWATERPARTICLE		(1 << 18)
#define R_RENDEREMPTYPARTICLE		(1 << 19)

typedef struct d3d_renderdef_s
{
	int framecount;
	int visframecount;

	// r_speeds counts
	int	brush_polys;
	int alias_polys;

	mleaf_t *viewleaf;
	mleaf_t *oldviewleaf;
	entity_t *currententity;
	bool automap;
	int renderflags;

	// render flags for entity types
	int brushrenderflags;

	// normal texture chains render from the texture_t * object;
	// these ones are just stored separately for special handling
	msurface_t *skychain;

	// normal opaque entities
	entity_t **visedicts;
	int numvisedicts;

	// translucent entities
	entity_t **transedicts;
	int numtransedicts;

	entity_t worldentity;
	float frametime;

	// actual fov used for rendering
	float fov_x;
	float fov_y;
} d3d_renderdef_t;


extern d3d_renderdef_t d3d_RenderDef;

void D3D_BackfaceCull (DWORD D3D_CULLTYPE);

extern D3DTEXTUREFILTERTYPE d3d_3DFilterMin;
extern D3DTEXTUREFILTERTYPE d3d_3DFilterMag;
extern D3DTEXTUREFILTERTYPE d3d_3DFilterMip;

// state management functions
// these are wrappers around the real call that check the previous value for a change before issuing the API call
void D3D_SetRenderState (D3DRENDERSTATETYPE State, DWORD Value);
void D3D_SetRenderStatef (D3DRENDERSTATETYPE State, float Value);
void D3D_SetSamplerState (DWORD Stage, D3DSAMPLERSTATETYPE Type, DWORD Value);
void D3D_SetSamplerStatef (DWORD Stage, D3DSAMPLERSTATETYPE Type, float Value);
void D3D_SetTextureStageState (DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value);
void D3D_SetTextureStageStatef (DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, float Value);
void D3D_SetTexture (DWORD Sampler, LPDIRECT3DTEXTURE9 pTexture);
void D3D_SetFVF (DWORD FVF);
void D3D_CheckDirtyMatrixes (void);
void D3D_DrawPrimitive (D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount, CONST void *pVertexStreamZeroData, UINT VertexStreamZeroStride);
void D3D_DrawPrimitive (D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount);
void D3D_DrawPrimitive (D3DPRIMITIVETYPE PrimitiveType, INT BaseVertexIndex, UINT MinIndex, UINT NumVertices, UINT StartIndex, UINT PrimitiveCount);
void D3D_DrawPrimitive (D3DPRIMITIVETYPE PrimitiveType, UINT MinVertexIndex, UINT NumVertices, UINT PrimitiveCount, const void *pIndexData, D3DFORMAT IndexDataFormat, CONST void* pVertexStreamZeroData, UINT VertexStreamZeroStride);

// batched up states
void D3D_EnableAlphaBlend (DWORD blendop, DWORD srcfactor, DWORD dstfactor, bool disablezwrite = true);
void D3D_DisableAlphaBlend (bool enablezwrite = true);
void D3D_SetTexCoordIndexes (DWORD tmu0index, DWORD tmu1index = 1, DWORD tmu2index = 2);
void D3D_SetTextureAddressMode (DWORD tmu0mode, DWORD tmu1mode = D3DTADDRESS_WRAP, DWORD tmu2mode = D3DTADDRESS_WRAP);
void D3D_SetTextureMipmap (DWORD stage, D3DTEXTUREFILTERTYPE magfilter, D3DTEXTUREFILTERTYPE minfilter, D3DTEXTUREFILTERTYPE mipfilter = D3DTEXF_NONE);
void D3D_SetTextureMatrixOp (DWORD tmu0op, DWORD tmu1op = D3DTTFF_DISABLE, DWORD tmu2op = D3DTTFF_DISABLE);
void D3D_SetTextureColorMode (DWORD stage, DWORD mode, DWORD arg1 = D3DTA_TEXTURE, DWORD arg2 = D3DTA_DIFFUSE);
void D3D_SetTextureAlphaMode (DWORD stage, DWORD mode, DWORD arg1 = D3DTA_TEXTURE, DWORD arg2 = D3DTA_CURRENT);

typedef bool (*entityfunc_t) (entity_t *);
void R_DrawEntitiesOnList (entity_t **list, int count, int type, entityfunc_t drawfunc);
void R_PrepareEntitiesOnList (entity_t **list, int count, int type, entityfunc_t prepfunc);

void D3D_DrawTexturedPic (int x, int y, int w, int h, LPDIRECT3DTEXTURE9 texpic);
bool R_CullBox (vec3_t mins, vec3_t maxs);

float Lerp (float l1, float l2, float lerpfactor);

typedef struct d3d_ModeDesc_s
{
	D3DDISPLAYMODE d3d_Mode;
	bool AllowWindowed;
	int BPP;
	int ModeNum;

	char ModeDesc[64];

	struct d3d_ModeDesc_s *Next;
} d3d_ModeDesc_t;

bool D3D_CheckTextureFormat (D3DFORMAT textureformat, BOOL mandatory);

void SCR_WriteSurfaceToTGA (char *filename, LPDIRECT3DSURFACE9 rts);
void SCR_WriteTextureToTGA (char *filename, LPDIRECT3DTEXTURE9 rts);

// TRUE if IDirect3DDevice9::BeginScene has been called
extern BOOL d3d_SceneBegun;

// enumeration to string conversion
char *D3DTypeToString (D3DFORMAT enumval);
char *D3DTypeToString (D3DMULTISAMPLE_TYPE enumval);
char *D3DTypeToString (D3DBACKBUFFER_TYPE enumval);
char *D3DTypeToString (D3DBASISTYPE enumval);
char *D3DTypeToString (D3DBLEND enumval);
char *D3DTypeToString (D3DBLENDOP enumval);
char *D3DTypeToString (D3DCMPFUNC enumval);
char *D3DTypeToString (D3DCOMPOSERECTSOP enumval);
char *D3DTypeToString (D3DCUBEMAP_FACES enumval);
char *D3DTypeToString (D3DCULL enumval);
char *D3DTypeToString (D3DDEBUGMONITORTOKENS enumval);
char *D3DTypeToString (D3DDECLMETHOD enumval);
char *D3DTypeToString (D3DDECLTYPE enumval);
char *D3DTypeToString (D3DDECLUSAGE enumval);
char *D3DTypeToString (D3DDEGREETYPE enumval);
char *D3DTypeToString (D3DDEVTYPE enumval);
char *D3DTypeToString (D3DDISPLAYROTATION enumval);
char *D3DTypeToString (D3DFILLMODE enumval);
char *D3DTypeToString (D3DFOGMODE enumval);
char *D3DTypeToString (D3DLIGHTTYPE enumval);
char *D3DTypeToString (D3DMATERIALCOLORSOURCE enumval);
char *D3DTypeToString (D3DPATCHEDGESTYLE enumval);
char *D3DTypeToString (D3DPOOL enumval);
char *D3DTypeToString (D3DPRIMITIVETYPE enumval);
char *D3DTypeToString (D3DQUERYTYPE enumval);
char *D3DTypeToString (D3DRENDERSTATETYPE enumval);
char *D3DTypeToString (D3DRESOURCETYPE enumval);
char *D3DTypeToString (D3DSAMPLER_TEXTURE_TYPE enumval);
char *D3DTypeToString (D3DSAMPLERSTATETYPE enumval);
char *D3DTypeToString (D3DSCANLINEORDERING enumval);
char *D3DTypeToString (D3DSHADEMODE enumval);
char *D3DTypeToString (D3DSTATEBLOCKTYPE enumval);
char *D3DTypeToString (D3DSTENCILOP enumval);
char *D3DTypeToString (D3DSWAPEFFECT enumval);
char *D3DTypeToString (D3DTEXTUREADDRESS enumval);
char *D3DTypeToString (D3DTEXTUREFILTERTYPE enumval);
char *D3DTypeToString (D3DTEXTUREOP enumval);
char *D3DTypeToString (D3DTEXTURESTAGESTATETYPE enumval);
char *D3DTypeToString (D3DTEXTURETRANSFORMFLAGS enumval);
char *D3DTypeToString (D3DTRANSFORMSTATETYPE enumval);
char *D3DTypeToString (D3DVERTEXBLENDFLAGS enumval);
char *D3DTypeToString (D3DZBUFFERTYPE enumval);
