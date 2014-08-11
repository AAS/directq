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

// new vbo interface
#define VBO_SHADER_TYPE_FIXED		1
#define VBO_SHADER_TYPE_HLSL		2

typedef struct d3d_fixed_shader_texture_s
{
	DWORD Op;
	DWORD Arg1;
	DWORD Arg2;
} d3d_fixed_shader_texture_t;

typedef struct d3d_shaderdef_s
{
	int NumStages;
	int Type;
} d3d_shaderdef_t;

typedef struct d3d_shader_s
{
	d3d_shaderdef_t ShaderDef;
	d3d_fixed_shader_texture_t ColorDef[3];
	d3d_fixed_shader_texture_t AlphaDef[3];
} d3d_shader_t;


void D3D_VBOBegin (D3DPRIMITIVETYPE PrimitiveType, int Stride);
void D3D_VBOAddShader (d3d_shader_t *Shader, LPDIRECT3DTEXTURE9 Stage0Tex, LPDIRECT3DTEXTURE9 Stage1Tex = NULL, LPDIRECT3DTEXTURE9 Stage2Tex = NULL);
void D3D_VBOAddSurfaceVerts (msurface_t *surf);
void D3D_VBORender (void);
void D3D_VBOCheckOverflow (int numverts, int numindexes);
void D3D_VBOAddAliasVerts (entity_t *ent, aliashdr_t *hdr, aliasstate_t *aliasstate);
void D3D_VBOAddShadowVerts (entity_t *ent, aliashdr_t *hdr, aliasstate_t *aliasstate, DWORD shadecolor);
void D3D_VBOSetVBOStream (LPDIRECT3DVERTEXBUFFER9 vbo, LPDIRECT3DINDEXBUFFER9 ibo = NULL, int stride = 0);
void D3D_DrawUserPrimitive (D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount, CONST void *pVertexStreamZeroData, UINT VertexStreamZeroStride);
void D3D_DrawUserPrimitive (D3DPRIMITIVETYPE PrimitiveType, UINT NumVertices, UINT PrimitiveCount, CONST void *pIndexData, CONST void *pVertexStreamZeroData, UINT VertexStreamZeroStride);


// dynamic linking
typedef IDirect3D9 *(WINAPI *DIRECT3DCREATE9PROC) (UINT);
typedef D3DXMATRIX *(WINAPI *D3DXMATRIXPERSPECTIVEFOVRHPROC) (D3DXMATRIX *, FLOAT, FLOAT, FLOAT, FLOAT);
typedef HRESULT (WINAPI *D3DXCREATEEFFECTPROC) (LPDIRECT3DDEVICE9, LPCVOID, UINT, CONST D3DXMACRO *, LPD3DXINCLUDE, DWORD, LPD3DXEFFECTPOOL, LPD3DXEFFECT *, LPD3DXBUFFER *);
typedef HRESULT (WINAPI *D3DXLOADSURFACEFROMSURFACEPROC) (LPDIRECT3DSURFACE9, CONST PALETTEENTRY *, CONST RECT *, LPDIRECT3DSURFACE9, CONST PALETTEENTRY *, CONST RECT *, DWORD, D3DCOLOR);
typedef D3DXMATRIX *(WINAPI *D3DXMATRIXPERSPECTIVEOFFCENTERRHPROC) (D3DXMATRIX *, FLOAT, FLOAT, FLOAT, FLOAT, FLOAT, FLOAT);
typedef D3DXMATRIX *(WINAPI *D3DXMATRIXORTHOOFFCENTERPROC) (D3DXMATRIX *, FLOAT, FLOAT, FLOAT, FLOAT, FLOAT, FLOAT);
typedef D3DXMATRIX *(WINAPI *D3DXMATRIXMULTIPLYPROC) (D3DXMATRIX *, CONST D3DXMATRIX *, CONST D3DXMATRIX *);
typedef D3DXMATRIX *(WINAPI *D3DXMATRIXSCALINGPROC) (D3DXMATRIX *, FLOAT, FLOAT, FLOAT);
typedef D3DXMATRIX *(WINAPI *D3DXMATRIXTRANSLATIONPROC) (D3DXMATRIX *, FLOAT, FLOAT, FLOAT);
typedef D3DXMATRIX *(WINAPI *D3DXMATRIXROTATIONXPROC) (D3DXMATRIX *, FLOAT);
typedef D3DXMATRIX *(WINAPI *D3DXMATRIXROTATIONYPROC) (D3DXMATRIX *, FLOAT);
typedef D3DXMATRIX *(WINAPI *D3DXMATRIXROTATIONZPROC) (D3DXMATRIX *, FLOAT);
typedef HRESULT (WINAPI *D3DXLOADSURFACEFROMMEMORYPROC) (LPDIRECT3DSURFACE9, CONST PALETTEENTRY *, CONST RECT *, LPCVOID, D3DFORMAT, UINT, CONST PALETTEENTRY *, CONST RECT *, DWORD, D3DCOLOR);
typedef HRESULT (WINAPI *D3DXFILTERTEXTUREPROC) (LPDIRECT3DBASETEXTURE9, CONST PALETTEENTRY *, UINT, DWORD);
typedef LPCSTR (WINAPI *D3DXGETPIXELSHADERPROFILEPROC) (LPDIRECT3DDEVICE9);
typedef LPCSTR (WINAPI *D3DXGETVERTEXSHADERPROFILEPROC) (LPDIRECT3DDEVICE9);
typedef HRESULT (WINAPI *D3DXSAVESURFACETOFILEPROC) (LPCSTR, D3DXIMAGE_FILEFORMAT, LPDIRECT3DSURFACE9, CONST PALETTEENTRY *, CONST RECT *);
typedef HRESULT (WINAPI *D3DXCREATETEXTUREFROMFILEINMEMORYEXPROC) (LPDIRECT3DDEVICE9, LPCVOID, UINT, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, DWORD, DWORD, D3DCOLOR, D3DXIMAGE_INFO *, PALETTEENTRY *, LPDIRECT3DTEXTURE9 *);
typedef HRESULT (WINAPI *D3DXCREATETEXTUREFROMRESOURCEEXAPROC) (LPDIRECT3DDEVICE9, HMODULE, LPCSTR, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, DWORD, DWORD, D3DCOLOR, D3DXIMAGE_INFO *, PALETTEENTRY *, LPDIRECT3DTEXTURE9 *);
typedef HRESULT (WINAPI *D3DXCREATERENDERTOSURFACEPROC) (LPDIRECT3DDEVICE9, UINT, UINT, D3DFORMAT, BOOL, D3DFORMAT, LPD3DXRENDERTOSURFACE *);

extern HINSTANCE hInstD3D9;
extern HINSTANCE hInstD3DX;

extern DIRECT3DCREATE9PROC QDirect3DCreate9;
extern D3DXMATRIXPERSPECTIVEFOVRHPROC QD3DXMatrixPerspectiveFovRH;
extern D3DXCREATEEFFECTPROC QD3DXCreateEffect;
extern D3DXLOADSURFACEFROMSURFACEPROC QD3DXLoadSurfaceFromSurface;
extern D3DXMATRIXPERSPECTIVEOFFCENTERRHPROC QD3DXMatrixPerspectiveOffCenterRH;
extern D3DXMATRIXORTHOOFFCENTERPROC QD3DXMatrixOrthoOffCenterRH;
extern D3DXMATRIXMULTIPLYPROC QD3DXMatrixMultiply;
extern D3DXMATRIXSCALINGPROC QD3DXMatrixScaling;
extern D3DXMATRIXTRANSLATIONPROC QD3DXMatrixTranslation;
extern D3DXMATRIXROTATIONXPROC QD3DXMatrixRotationX;
extern D3DXMATRIXROTATIONYPROC QD3DXMatrixRotationY;
extern D3DXMATRIXROTATIONZPROC QD3DXMatrixRotationZ;
extern D3DXLOADSURFACEFROMMEMORYPROC QD3DXLoadSurfaceFromMemory;
extern D3DXFILTERTEXTUREPROC QD3DXFilterTexture;
extern D3DXGETPIXELSHADERPROFILEPROC QD3DXGetPixelShaderProfile;
extern D3DXGETVERTEXSHADERPROFILEPROC QD3DXGetVertexShaderProfile;
extern D3DXSAVESURFACETOFILEPROC QD3DXSaveSurfaceToFileA;
extern D3DXCREATETEXTUREFROMFILEINMEMORYEXPROC QD3DXCreateTextureFromFileInMemoryEx;
extern D3DXCREATETEXTUREFROMRESOURCEEXAPROC QD3DXCreateTextureFromResourceExA;
extern D3DXCREATERENDERTOSURFACEPROC QD3DXCreateRenderToSurface;

// for render to texture
extern bool d3d_SceneBegun;

// VBO interface
void D3D_VBOReleaseBuffers (void);
void D3D_SubmitVertexes (int numverts, int numindexes, int polysize);

void D3D_GetIndexBufferSpace (void **data);
void D3D_GetVertexBufferSpace (void **data);

BOOL D3D_AreBuffersFull (int numverts, int numindexes);


// this is our matrix interface now
extern D3DMATRIX d3d_WorldMatrix;
extern D3DMATRIX d3d_ProjMatrix;

void D3D_TranslateMatrix (D3DMATRIX *matrix, float x, float y, float z);
void D3D_ScaleMatrix (D3DMATRIX *matrix, float x, float y, float z);
void D3D_RotateMatrix (D3DMATRIX *matrix, float x, float y, float z, float angle);
void D3D_LoadIdentity (D3DMATRIX *matrix);
void D3D_MultMatrix (D3DMATRIX *matrix1, D3DMATRIX *matrix2);
D3DXMATRIX *D3D_MakeD3DXMatrix (D3DMATRIX *matrix);

extern HRESULT hr;

// hlsl
extern LPDIRECT3DVERTEXDECLARATION9 d3d_LiquidDeclaration;
extern LPDIRECT3DVERTEXDECLARATION9 d3d_SkyDeclaration;
extern LPDIRECT3DVERTEXDECLARATION9 d3d_UnderwaterDeclaration;

extern LPD3DXEFFECT d3d_LiquidFX;
extern LPD3DXEFFECT d3d_SkyFX;
extern LPD3DXEFFECT d3d_UnderwaterFX;

void D3D_InitHLSL (void);
void D3D_ShutdownHLSL (void);


// crap from the old glquake.h
#define ALIAS_BASE_SIZE_RATIO (1.0 / 11.0)
#define BACKFACE_EPSILON	0.01

texture_t *R_TextureAnimation (entity_t *ent, texture_t *base);
void R_ReadPointFile_f (void);

// view origin
extern	vec3_t	vup;
extern	vec3_t	vpn;
extern	vec3_t	vright;
extern	vec3_t	r_origin;

// screen size info
extern	refdef_t	r_refdef;
extern	texture_t	*r_notexture_mip;
extern	int		d_lightstylevalue[256];	// 8.8 fraction of base light value

extern	cvar_t	r_norefresh;
extern	cvar_t	r_drawentities;
extern	cvar_t	r_drawworld;
extern	cvar_t	r_drawviewmodel;
extern	cvar_t	r_speeds;
extern	cvar_t	r_waterwarp;
extern	cvar_t	r_fullbright;
extern	cvar_t	r_shadows;
extern	cvar_t	r_wateralpha;
extern	cvar_t	r_dynamic;
extern	cvar_t	r_novis;

extern	cvar_t	gl_clear;
extern	cvar_t	gl_cull;
extern	cvar_t	gl_smoothmodels;
extern	cvar_t	gl_affinemodels;
extern	cvar_t	gl_polyblend;
extern	cvar_t	gl_nocolors;
extern	cvar_t	gl_doubleeyes;

void D3D_TranslatePlayerSkin (int playernum);


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
	void UploadLightmap (void);
	void CalcLightmapTexCoords (msurface_t *surf);
	void CheckSurfaceForModification (msurface_t *surf);
	bool AllocBlock (msurface_t *surf);
	void LoseDefaultTexture (void);
	void RecoverDefaultTexture (void);
	void EnsureSurfaceTexture (msurface_t *surf);

private:
	bool modified;
	int width;
	int height;
	int *allocated;
	D3DLOCKED_RECT d3d_LockedRect;
	LPDIRECT3DTEXTURE9 d3d_MainTexture;
	LPDIRECT3DTEXTURE9 d3d_BackupTexture;
	bool lost;
	RECT DirtyRect;

	// next lightmap in the chain
	CD3DLightmap *next;
};


extern CD3DLightmap *d3d_Lightmaps;


// generic world surface with verts and two sets of texcoords
typedef struct worldvert_s
{
	float xyz[3];
	float st[2];
	float lm[2];

	// force the vertex buffer to compile to a 32-byte size multiple
	DWORD dummy;
} worldvert_t;


// video
void D3D_InitDirect3D (D3DDISPLAYMODE *mode);
void D3D_ShutdownDirect3D (void);
void D3D_BeginRendering (void);
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
#define IMAGE_NOEXTERN		2048
#define IMAGE_HALFLIFE		4096
#define IMAGE_MAPSHOT		8192
#define IMAGE_PADDABLE		16384
#define IMAGE_PADDED		32768

int D3D_PowerOf2Size (int size);

typedef struct image_s
{
	char identifier[64];
	int width;
	int height;
	unsigned int *palette;
	byte *data;
	byte hash[16];
	int flags;
	int LastUsage;
	LPDIRECT3DTEXTURE9 d3d_Texture;
} image_t;


bool D3D_LoadExternalTexture (LPDIRECT3DTEXTURE9 *tex, char *filename, int flags);
void D3D_LoadResourceTexture (LPDIRECT3DTEXTURE9 *tex, int ResourceID, int flags);
image_t *D3D_LoadTexture (char *identifier, int width, int height, byte *data, int flags);
void D3D_UploadTexture (LPDIRECT3DTEXTURE9 *texture, void *data, int width, int height, int flags);


void D3D_ReleaseTextures (void);
void D3D_FlushTextures (void);

extern LPDIRECT3DTEXTURE9 r_notexture;

// global stuff
extern LPDIRECT3D9 d3d_Object;
extern LPDIRECT3DDEVICE9 d3d_Device;
extern D3DADAPTER_IDENTIFIER9 d3d_Adapter;
extern D3DCAPS9 d3d_DeviceCaps;

// state changes
void D3D_Set2D (void);

// global caps - used for any specific settings that we choose rather than that are
// enforced on us through device caps
typedef struct d3d_global_caps_s
{
	D3DFORMAT DepthStencilFormat;
	bool supportXRGB;
	bool supportARGB;
	bool supportL8;
	bool supportA8L8;
	bool supportDXT1;
	bool supportDXT3;
	bool supportDXT5;
	bool supportPixelShaders;
	bool usingPixelShaders;
	bool supportTripleBuffer;
	bool supportHardwareTandL;
	bool supportOcclusion;
	DWORD deviceCreateFlags;
	int videoRAMMB;
	int NumTMUs;
} d3d_global_caps_t;

extern d3d_global_caps_t d3d_GlobalCaps;

extern cvar_t r_hlsl;

typedef struct d3d_renderdef_s
{
	int skyframe;
	int framecount;
	int visframecount;

	// r_speeds counts
	int	brush_polys;
	int last_alias_polys;
	int alias_polys;
	int numsss;

	mleaf_t *viewleaf;
	mleaf_t *oldviewleaf;
	bool automap;

	// normal opaque entities
	entity_t **visedicts;
	int numvisedicts;

	entity_t worldentity;
	float frametime;

	// actual fov used for rendering
	float fov_x;
	float fov_y;
} d3d_renderdef_t;

extern d3d_renderdef_t d3d_RenderDef;

// this is needed outside of r_part now...
typedef struct particle_type_s
{
	struct particle_s *particles;
	vec3_t spawnorg;
	struct particle_type_s *next;
} particle_type_t;

void D3D_AddToAlphaList (entity_t *ent);
void D3D_AddToAlphaList (struct d3d_modelsurf_s *modelsurf);
void D3D_AddToAlphaList (particle_type_t *particle);
void D3D_RenderAlphaList (void);

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
void D3D_SetTextureState (DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value);
void D3D_SetTextureStatef (DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, float Value);
void D3D_SetTexture (DWORD Sampler, LPDIRECT3DBASETEXTURE9 pTexture);
void D3D_SetFVF (DWORD FVF);

void D3D_SetColorMode (DWORD stage, DWORD mode, DWORD arg1 = D3DTA_TEXTURE, DWORD arg2 = D3DTA_DIFFUSE);
void D3D_SetAlphaMode (DWORD stage, DWORD mode, DWORD arg1 = D3DTA_TEXTURE, DWORD arg2 = D3DTA_CURRENT);

// batched up states
void D3D_EnableAlphaBlend (DWORD blendop, DWORD srcfactor, DWORD dstfactor);
void D3D_DisableAlphaBlend (void);
void D3D_SetTexCoordIndexes (DWORD tmu0index, DWORD tmu1index = 1, DWORD tmu2index = 2);
void D3D_SetTextureAddressMode (DWORD tmu0mode, DWORD tmu1mode = D3DTADDRESS_WRAP, DWORD tmu2mode = D3DTADDRESS_WRAP);
void D3D_SetTextureMipmap (DWORD stage, D3DTEXTUREFILTERTYPE magfilter, D3DTEXTUREFILTERTYPE minfilter, D3DTEXTUREFILTERTYPE mipfilter = D3DTEXF_NONE);
void D3D_SetTextureMatrixOp (DWORD tmu0op, DWORD tmu1op = D3DTTFF_DISABLE, DWORD tmu2op = D3DTTFF_DISABLE);
void D3D_SetTextureColorMode (DWORD stage, DWORD mode, DWORD arg1 = D3DTA_TEXTURE, DWORD arg2 = D3DTA_DIFFUSE);
void D3D_SetTextureAlphaMode (DWORD stage, DWORD mode, DWORD arg1 = D3DTA_TEXTURE, DWORD arg2 = D3DTA_CURRENT);

bool R_CullBox (mnode_t *node);
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

// modulation factors for overbrights; the first is for fixed, the second for HLSL
extern DWORD D3D_OVERBRIGHT_MODULATE;
extern float d3d_OverbrightModulate;

// enumeration to string conversion
char *D3DTypeToString (D3DFORMAT enumval);
char *D3DTypeToString (D3DMULTISAMPLE_TYPE enumval);
char *D3DTypeToString (D3DBACKBUFFER_TYPE enumval);
char *D3DTypeToString (D3DBASISTYPE enumval);
char *D3DTypeToString (D3DBLEND enumval);
char *D3DTypeToString (D3DBLENDOP enumval);
char *D3DTypeToString (D3DCMPFUNC enumval);
char *D3DTypeToString (D3DCUBEMAP_FACES enumval);
char *D3DTypeToString (D3DCULL enumval);
char *D3DTypeToString (D3DDEBUGMONITORTOKENS enumval);
char *D3DTypeToString (D3DDECLMETHOD enumval);
char *D3DTypeToString (D3DDECLTYPE enumval);
char *D3DTypeToString (D3DDECLUSAGE enumval);
char *D3DTypeToString (D3DDEGREETYPE enumval);
char *D3DTypeToString (D3DDEVTYPE enumval);
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
