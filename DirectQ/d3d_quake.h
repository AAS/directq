


#include <d3d9.h>
#include <d3dx9.h>

void D3D_SetFVFStateManaged (DWORD NewFVF);

extern LPDIRECT3DVERTEXBUFFER9 d3d_BrushModelVerts;
extern int d3d_VertexBufferVerts;

// video
void D3D_InitDirect3D (D3DDISPLAYMODE *mode);
void D3D_ShutdownDirect3D (void);
void D3D_BeginRendering (int *x, int *y, int *width, int *height);
void D3D_EndRendering (void);


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

#define D3D_RELEASE_ALL		true
#define D3D_RELEASE_UNUSED	false

void D3D_BindTexture (LPDIRECT3DTEXTURE9 tex);
void D3D_BindTexture (int stage, LPDIRECT3DTEXTURE9 tex);
LPDIRECT3DTEXTURE9 D3D_LoadTexture (char *identifier, int width, int height, byte *data, bool mipmap, bool alpha);
LPDIRECT3DTEXTURE9 D3D_LoadTexture (image_t *image);
void D3D_LoadTexture (LPDIRECT3DTEXTURE9 *tex, image_t *image);
void D3D_LoadTexture (LPDIRECT3DTEXTURE9 *tex, int width, int height, byte *data, unsigned int *palette, bool mipmap, bool alpha);
LPDIRECT3DTEXTURE9 D3D_LoadTexture (int width, int height, byte *data, int flags);
void D3D_ReleaseTextures (bool fullrelease);

// lightmaps
void D3D_CreateSurfaceLightmap (msurface_t *surf);
void D3D_UploadLightmaps (void);
void D3D_ReleaseLightmaps (void);
void D3D_BindLightmap (int stage, void *lm);
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
extern D3DXMATRIX *d3d_WorldMatrix;
extern D3DXMATRIX d3d_ProjectionMatrix;

// scaling factors for x and y coords in the 2D view;
#define SCALE_2D_X(x) (((x) * glwidth) / 640)
#define SCALE_2D_Y(y) (((y) * glheight) / 480)

// state changes
void D3D_Set2D (void);
void D3D_SetDefaultStates (void);
void D3D_ReleaseStateBlocks (void);
void D3D_CreateStateBlocks (void);

extern IDirect3DStateBlock9 *d3d_SetAliasState;
extern IDirect3DStateBlock9 *d3d_RevertAliasState;
extern IDirect3DStateBlock9 *d3d_DefaultViewport;
extern IDirect3DStateBlock9 *d3d_GunViewport;
extern IDirect3DStateBlock9 *d3d_2DViewport;
extern IDirect3DStateBlock9 *d3d_EnableAlphaTest;
extern IDirect3DStateBlock9 *d3d_DisableAlphaTest;
extern IDirect3DStateBlock9 *d3d_EnableAlphaBlend;
extern IDirect3DStateBlock9 *d3d_DisableAlphaBlend;

// object release
#define SAFE_RELEASE(d3d_Generic) {if ((d3d_Generic)) {(d3d_Generic)->Release (); (d3d_Generic) = NULL;}}

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
} d3d_global_caps_t;

extern d3d_global_caps_t d3d_GlobalCaps;

