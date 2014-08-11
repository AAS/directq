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

void D3D_RotateForEntity (entity_t *e);
void D3D_RotateForEntity (entity_t *e, D3DMATRIX *m);
void D3D_RotateForEntity (entity_t *e, D3DMATRIX *m, float *origin, float *angles);

void D3DMain_CreateVertexBuffer (UINT length, DWORD usage, LPDIRECT3DVERTEXBUFFER9 *buf);
void D3DMain_CreateIndexBuffer (UINT numindexes, DWORD usage, LPDIRECT3DINDEXBUFFER9 *buf);

void D3D_PrelockVertexBuffer (LPDIRECT3DVERTEXBUFFER9 vb);
void D3D_PrelockIndexBuffer (LPDIRECT3DINDEXBUFFER9 ib);

void D3DRTT_BeginScene (void);
void D3DRTT_EndScene (void);

// palette hackery
typedef struct palettedef_s
{
	PALETTEENTRY luma[256];
	PALETTEENTRY noluma[256];
	PALETTEENTRY standard[256];

	D3DCOLOR standard32[256];
	float colorfloat[256][4];
	int darkindex;
} palettedef_t;

extern palettedef_t d3d_QuakePalette;

#define MAX_CHAR_TEXTURES	64

// VBO interface
void D3D_VBOReleaseBuffers (void);
void D3D_SubmitVertexes (int numverts, int numindexes, int polysize);

void D3D_GetIndexBufferSpace (void **data, int locksize = 0);
void D3D_GetVertexBufferSpace (void **data, int locksize = 0);

BOOL D3D_AreBuffersFull (int numverts, int numindexes);


// this is our matrix interface now
extern D3DMATRIX d3d_ViewMatrix;
extern D3DMATRIX d3d_WorldMatrix;
extern D3DMATRIX d3d_ModelViewProjMatrix;
extern D3DMATRIX d3d_ProjMatrix;

void D3DMatrix_Translate (D3DMATRIX *matrix, float x, float y, float z);
void D3DMatrix_Scale (D3DMATRIX *matrix, float x, float y, float z);
void D3DMatrix_Translate (D3DMATRIX *matrix, float *xyz);
void D3DMatrix_Scale (D3DMATRIX *matrix, float *xyz);
void D3DMatrix_TransformPoint (D3DMATRIX *matrix, float *in, float *out);
void D3DMatrix_Rotate (D3DMATRIX *matrix, float x, float y, float z, float angle);
D3DMATRIX *D3DMatrix_Identity (D3DMATRIX *matrix);
D3DXMATRIX *D3DMatrix_Identity (D3DXMATRIX *matrix);
void D3DMatrix_Multiply (D3DMATRIX *matrix1, D3DMATRIX *matrix2);
void D3DMatrix_Multiply (D3DMATRIX *matrixout, D3DMATRIX *matrix1, D3DMATRIX *matrix2);
D3DMATRIX *D3DMatrix_OrthoOffCenterRH (D3DMATRIX *matrix, FLOAT l, FLOAT r, FLOAT b, FLOAT t, FLOAT zn, FLOAT zf);
D3DMATRIX *D3DMatrix_PerspectiveFovRH (D3DMATRIX *matrix, FLOAT fovy, FLOAT aspect, FLOAT zn, FLOAT zf);
D3DXMATRIX *D3DMatrix_ToD3DXMatrix (D3DMATRIX *matrix);

extern HRESULT hr;

extern cvar_t r_overbright;

// hlsl
void D3DHLSL_SetPass (int passnum);
void D3DHLSL_SetAlpha (float alphaval);
void D3DHLSL_SetTexture (UINT stage, LPDIRECT3DBASETEXTURE9 tex);
void D3DHLSL_CheckCommit (void);
void D3DHLSL_SetMatrix (D3DXHANDLE h, D3DMATRIX *matrix);
void D3DHLSL_SetWorldMatrix (D3DMATRIX *worldmatrix);
void D3DHLSL_SetEntMatrix (D3DMATRIX *entmatrix);
void D3DHLSL_SetFogMatrix (D3DMATRIX *fogmatrix);
void D3DHLSL_InvalidateState (void);
void D3DHLSL_BeginFrame (void);
void D3DHLSL_EndFrame (void);
void D3DHLSL_SetFloat (D3DXHANDLE handle, float fl);
void D3DHLSL_SetFloatArray (D3DXHANDLE handle, float *fl, int len);
void D3DHLSL_SetInt (D3DXHANDLE handle, int n);
void D3DHLSL_SetLerp (float curr, float last);
void D3DHLSL_SetVectorArray (D3DXHANDLE handle, D3DXVECTOR4 *vecs, int len);

#define FX_PASS_NOTBEGUN					-1
#define FX_PASS_ALIAS_NOLUMA				0
#define FX_PASS_ALIAS_LUMA					1
#define FX_PASS_LIQUID						2
#define FX_PASS_SHADOW						3
#define FX_PASS_WORLD_NOLUMA				4
#define FX_PASS_WORLD_LUMA					5
#define FX_PASS_SKYWARP						6
#define FX_PASS_DRAWTEXTURED				7
#define FX_PASS_DRAWCOLORED					8
#define FX_PASS_SKYBOX						9
#define FX_PASS_PARTICLES					10
#define FX_PASS_WORLD_NOLUMA_ALPHA			11
#define FX_PASS_WORLD_LUMA_ALPHA			12
#define FX_PASS_SPRITE						13
#define FX_PASS_UNDERWATER					14
#define FX_PASS_ALIAS_LUMA_NOLUMA			15
#define FX_PASS_WORLD_LUMA_NOLUMA			16
#define FX_PASS_WORLD_LUMA_NOLUMA_ALPHA		17
#define FX_PASS_ALIAS_INSTANCED_NOLUMA		18
#define FX_PASS_ALIAS_INSTANCED_LUMA		19
#define FX_PASS_ALIAS_INSTANCED_LUMA_NOLUMA	20
#define FX_PASS_ALIAS_VIEWMODEL_NOLUMA		21
#define FX_PASS_ALIAS_VIEWMODEL_LUMA		22
#define FX_PASS_ALIAS_VIEWMODEL_LUMA_NOLUMA	23
#define FX_PASS_CORONA						24
#define FX_PASS_BBOXES						25
#define FX_PASS_LIQUID_RIPPLE				26
#define FX_PASS_PARTICLE_SQUARE				27
#define FX_PASS_ALIAS_KUROK					28


void D3DHLSL_Init (void);
void D3DHLSL_Shutdown (void);


extern cvar_t gl_fullbrights;

// this one wraps DIP so that I can check for commit and anything else i need to do before drawing
void D3D_DrawIndexedPrimitive (int FirstVertex, int NumVertexes, int FirstIndex, int NumPrimitives);
void D3D_DrawPrimitive (D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount);


// crap from the old glquake.h
#define ALIAS_BASE_SIZE_RATIO (1.0 / 11.0)
#define BACKFACE_EPSILON	0.01

struct texture_s *R_TextureAnimation (entity_t *ent, texture_t *base);
void R_ReadPointFile_f (void);

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

extern	cvar_t	gl_cull;
extern	cvar_t	gl_smoothmodels;
extern	cvar_t	gl_affinemodels;
extern	cvar_t	gl_polyblend;
extern	cvar_t	gl_nocolors;
extern	cvar_t	gl_doubleeyes;


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
void D3DVid_BeginRendering (void);
void D3DVid_EndRendering (void);
void D3DVid_Finish (void);

extern D3DDISPLAYMODE d3d_CurrentMode;
void D3D_SetViewport (DWORD x, DWORD y, DWORD w, DWORD h, float zn, float zf);


// textures
#define IMAGE_MIPMAP		(1 << 1)
#define IMAGE_ALPHA			(1 << 2)
#define IMAGE_32BIT			(1 << 3)
#define IMAGE_PRESERVE		(1 << 4)
#define IMAGE_LIQUID		(1 << 5)
#define IMAGE_BSP			(1 << 6)
#define IMAGE_ALIAS			(1 << 7)
#define IMAGE_SPRITE		(1 << 8)
#define IMAGE_LUMA			(1 << 9)
#define IMAGE_EXTERNAL		(1 << 10)
#define IMAGE_NOEXTERN		(1 << 11)
#define IMAGE_HALFLIFE		(1 << 12)
#define IMAGE_KEEPPATH		(1 << 13)
#define IMAGE_PADDABLE		(1 << 14)
#define IMAGE_PADDED		(1 << 15)
#define IMAGE_SYSMEM		(1 << 16)
#define IMAGE_SCRAP			(1 << 17)
#define IMAGE_FENCE			(1 << 18)
#define IMAGE_SCALE2X		(1 << 19)
#define IMAGE_SKYBOX		(1 << 20)

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
	int RegistrationSequence;
	int ChainNumber;
	LPDIRECT3DTEXTURE9 d3d_Texture;
} image_t;


bool D3D_LoadExternalTexture (LPDIRECT3DTEXTURE9 *tex, char *filename, int flags);
void D3D_LoadResourceTexture (char *name, LPDIRECT3DTEXTURE9 *tex, int ResourceID, int flags);
image_t *D3D_LoadTexture (char *identifier, int width, int height, byte *data, int flags = 0);
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
void D3DDraw_Begin2D (void);

// global caps - used for any specific settings that we choose rather than that are
// enforced on us through device caps
typedef struct d3d_global_caps_s
{
	D3DFORMAT DepthStencilFormat;
	bool supportHardwareTandL;
	bool supportNonPow2;
	bool supportInstancing;
	DWORD deviceCreateFlags;
	DWORD DiscardLock;
	DWORD NoOverwriteLock;
	int MaxExtents;
	int NumTMUs;
} d3d_global_caps_t;

extern d3d_global_caps_t d3d_GlobalCaps;

typedef struct d3d_renderdef_s
{
	int framecount;
	int visframecount;
	int entframecount;

	// r_speeds counts
	int	brush_polys;
	int alias_polys;
	int numsss;
	int numdrawprim;
	int numlock;
	int numnode;
	int numleaf;

	mleaf_t *viewleaf;
	mleaf_t *oldviewleaf;
	int *lastgoodcontents;

	// normal opaque entities
	entity_t **visedicts;
	int numvisedicts;
	int relinkframe;

	bool RTT;
	bool WorldModelLoaded;

	entity_t worldentity;

	// models who's RegistrationSequence is == this have been touched on this map load
	int RegistrationSequence;

	// actual fov used for rendering
	float fov_x;
	float fov_y;
} d3d_renderdef_t;

extern d3d_renderdef_t d3d_RenderDef;

void D3DAlpha_AddToList (entity_t *ent);
void D3DAlpha_AddToList (struct d3d_modelsurf_s *modelsurf);
void D3DAlpha_AddToList (struct particle_type_s *particle);
void D3DAlpha_RenderList (void);

void D3D_BackfaceCull (DWORD D3D_CULLTYPE);

extern D3DTEXTUREFILTERTYPE d3d_TexFilter;
extern D3DTEXTUREFILTERTYPE d3d_MipFilter;

// state management functions
// these are wrappers around the real call that check the previous value for a change before issuing the API call
void D3D_SetRenderState (D3DRENDERSTATETYPE State, DWORD Value);
void D3D_SetRenderStatef (D3DRENDERSTATETYPE State, float Value);
void D3D_SetSamplerState (UINT sampler, D3DSAMPLERSTATETYPE type, DWORD state);
void D3D_SetVertexDeclaration (LPDIRECT3DVERTEXDECLARATION9 vd);
void D3D_SetStreamSource (DWORD stream, LPDIRECT3DVERTEXBUFFER9 vb, DWORD offset, DWORD stride, UINT freq = 1);
void D3D_SetIndices (LPDIRECT3DINDEXBUFFER9 ib);
void D3D_UnbindStreams (void);

void D3D_SetTextureMipmap (DWORD stage, D3DTEXTUREFILTERTYPE texfilter, D3DTEXTUREFILTERTYPE mipfilter = D3DTEXF_NONE);
void D3D_SetTextureAddress (DWORD stage, DWORD mode);

void D3D_AlignCubeMapFaceTexels (LPDIRECT3DSURFACE9 surf, D3DCUBEMAP_FACES face);

bool R_CullBox (vec3_t mins, vec3_t maxs, mplane_t *frustumplanes);
extern mplane_t	frustum[];

float Lerp (float l1, float l2, float lerpfactor);

bool D3D_CheckTextureFormat (D3DFORMAT textureformat, BOOL mandatory);

void SCR_WriteSurfaceToTGA (char *filename, LPDIRECT3DSURFACE9 rts, D3DFORMAT fmt);
void SCR_WriteTextureToTGA (char *filename, LPDIRECT3DTEXTURE9 rts, D3DFORMAT fmt);

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

class CD3DDeviceLossHandler
{
public:
	CD3DDeviceLossHandler (xcommand_t onloss, xcommand_t onrecover);
	xcommand_t OnLoseDevice;
	xcommand_t OnRecoverDevice;
};


extern cvar_t d3d_usinginstancing;

#define MAX_LIGHTMAPS		256
