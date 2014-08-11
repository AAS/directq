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
// gl_warp.c -- sky and water polygons

#include "quakedef.h"
#include "d3d_model.h"
#include "d3d_quake.h"
#include "d3d_vbo.h"

LPDIRECT3DTEXTURE9 solidskytexture = NULL;
LPDIRECT3DTEXTURE9 alphaskytexture = NULL;
LPDIRECT3DCUBETEXTURE9 skyboxcubemap = NULL;
LPDIRECT3DTEXTURE9 skyboxtextures[6] = {NULL};

// the menu needs to know the name of the loaded skybox
char CachedSkyBoxName[MAX_PATH] = {0};
bool SkyboxValid = false;
int NumComponents = 0;

// the main sky render doesn't take texcoords in any of it's modes
typedef struct skyverts_s
{
	float v[3];
} skyverts_t;

/*
==============================================================================================================================

		SKY WARP RENDERING

==============================================================================================================================
*/

// let's distinguish properly between preproccessor defines and variables
// (LH apparently doesn't believe in this, but I do)
#define SKYGRID_SIZE 16
#define SKYGRID_SIZE_PLUS_1 (SKYGRID_SIZE + 1)
#define SKYGRID_RECIP (1.0f / (SKYGRID_SIZE))
#define SKYSPHERE_NUMVERTS (SKYGRID_SIZE_PLUS_1 * SKYGRID_SIZE_PLUS_1)
#define SKYSPHERE_NUMTRIS (SKYGRID_SIZE * SKYGRID_SIZE * 2)
#define SKYSPHERE_NUMINDEXES (SKYGRID_SIZE * SKYGRID_SIZE * 6)


warpverts_t *d3d_DPSkyVerts = NULL;
unsigned short *d3d_DPSkyIndexes = NULL;

cvar_t r_skybackscroll ("r_skybackscroll", 8, CVAR_ARCHIVE);
cvar_t r_skyfrontscroll ("r_skyfrontscroll", 16, CVAR_ARCHIVE);
cvar_t r_skyalpha ("r_skyalpha", 1, CVAR_ARCHIVE);

// dynamic far clip plane for sky
extern float r_clipdist;
unsigned *skyalphatexels = NULL;
void D3D_UpdateSkyAlpha (void);

void D3D_InitSkySphere (void);

void D3D_SkyBeginCallback (void *blah)
{
	/*
	D3D_SetTextureMipmap (0, d3d_TexFilter, D3DTEXF_NONE);
	D3D_SetTextureMipmap (1, d3d_TexFilter, D3DTEXF_NONE);
	D3D_SetTexCoordIndexes (0, 1);
	D3D_SetTextureAddressMode (D3DTADDRESS_WRAP, D3DTADDRESS_WRAP);
	*/

	if (SkyboxValid && d3d_GlobalCaps.usingPixelShaders)
	{
		if (d3d_FXPass != FX_PASS_NOTBEGUN)
			D3D_EndShaderPass ();

		D3D_SetVertexDeclaration (d3d_VDXyz);
		d3d_MasterFX->SetTexture ("tmu0Texture", skyboxcubemap);

		D3D_SetTextureMipmap (0, d3d_TexFilter, D3DTEXF_NONE);
		D3D_SetTextureAddressMode (D3DTADDRESS_CLAMP);

		D3D_BeginShaderPass (FX_PASS_SKYBOX);
	}
	else if (SkyboxValid)
	{
		// cube mapped sky - note: this could be massively simplified by just doing a cubemap lookup but
		// then we would need the extra verts.  On balance it's probably six of one.
		D3D_SetTextureMipmap (0, d3d_TexFilter, D3DTEXF_NONE);
		D3D_SetTextureAddressMode (D3DTADDRESS_CLAMP);

		D3D_SetVertexDeclaration (d3d_VDXyz);
		D3D_SetTexture (0, skyboxcubemap);
		D3D_SetTextureState (0, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
		D3D_SetTextureMatrixOp (D3DTTFF_COUNT3);

		D3DMATRIX d3d_ViewRotate, d3d_ViewInverse;

		// rotate backwards by the viewangles to get correct orientation
		D3D_LoadIdentity (&d3d_ViewRotate);
		D3D_LoadIdentity (&d3d_ViewInverse);

		// put z going up
		D3D_RotateMatrix (&d3d_ViewRotate, 1, 0, 0, -90);
		D3D_RotateMatrix (&d3d_ViewRotate, 0, 0, 1, 90);

		// rotate camera by angles
		D3D_RotateMatrix (&d3d_ViewRotate, 1, 0, 0, -r_refdef.viewangles[2]);
		D3D_RotateMatrix (&d3d_ViewRotate, 0, 1, 0, -r_refdef.viewangles[0]);
		D3D_RotateMatrix (&d3d_ViewRotate, 0, 0, 1, -r_refdef.viewangles[1]);

		// invert resulting matrix for final correct undoing of camera position.
		// note that we don't apply the translation because we want the skybox to track the view position
		QD3DXMatrixInverse (D3D_MakeD3DXMatrix (&d3d_ViewInverse), NULL, D3D_MakeD3DXMatrix (&d3d_ViewRotate));

		d3d_Device->SetTransform (D3DTS_TEXTURE0, &d3d_ViewInverse);
	}
	else if (!d3d_GlobalCaps.usingPixelShaders)
	{
		// emit a clipping area
		D3D_SetVertexDeclaration (d3d_VDXyz);
		D3D_SetTextureColorMode (0, D3DTOP_DISABLE);
		D3D_SetRenderState (D3DRS_COLORWRITEENABLE, 0);
	}
	else
	{
		// hlsl sky warp
		float speedscale[] = {cl.time * r_skybackscroll.value, cl.time * r_skyfrontscroll.value, 1};

		speedscale[0] -= (int) speedscale[0] & ~127;
		speedscale[1] -= (int) speedscale[1] & ~127;

		D3D_SetVertexDeclaration (d3d_VDXyz);

		if (d3d_FXPass != FX_PASS_NOTBEGUN)
			D3D_EndShaderPass ();

		d3d_MasterFX->SetTexture ("tmu0Texture", solidskytexture);
		d3d_MasterFX->SetTexture ("tmu1Texture", alphaskytexture);

		d3d_MasterFX->SetFloat ("AlphaVal", r_skyalpha.value);
		d3d_MasterFX->SetFloatArray ("Scale", speedscale, 3);

		D3D_SetTextureMipmap (0, d3d_TexFilter, D3DTEXF_NONE);
		D3D_SetTextureMipmap (1, d3d_TexFilter, D3DTEXF_NONE);
		D3D_SetTextureAddressMode (D3DTADDRESS_WRAP, D3DTADDRESS_WRAP);

		D3D_BeginShaderPass (FX_PASS_SKYWARP);
	}
}


void D3D_InitializeSky (void)
{
	if (d3d_GlobalCaps.usingPixelShaders || SkyboxValid)
	{
		// throw away the verts and indexes
		if (d3d_DPSkyVerts) {MainZone->Free (d3d_DPSkyVerts); d3d_DPSkyVerts = NULL;}
		if (d3d_DPSkyIndexes) {MainZone->Free (d3d_DPSkyIndexes); d3d_DPSkyIndexes = NULL;}
	}
	else
	{
		// recreate them if needed
		// (D3D_InitSkySphere will return if they already exist)
		D3D_InitSkySphere ();
	}

	// bound alpha
	if (r_skyalpha.value < 0.0f) Cvar_Set (&r_skyalpha, 0.0f);
	if (r_skyalpha.value > 1.0f) Cvar_Set (&r_skyalpha, 1.0f);

	// this is always done even if the relevant modes are not selected so that things will be correct
	D3D_UpdateSkyAlpha ();
	VBO_AddCallback (D3D_SkyBeginCallback);
}


void D3D_UpdateSkyAlpha (void)
{
	// i can't figure the correct mode for skyalpha so i'll just do it this way :p
	static int oldalpha = -666;
	bool copytexels = false;

	int activealpha = (r_skyalpha.value * 255);

	// always use full opaque with PS as we'll handle the alpha in the shader itself
	if (d3d_GlobalCaps.usingPixelShaders) activealpha = 255;

	if (oldalpha == activealpha && skyalphatexels) return;

	oldalpha = activealpha;

	D3DSURFACE_DESC Level0Desc;
	D3DLOCKED_RECT Level0Rect;
	LPDIRECT3DSURFACE9 skysurf;

	// the alpha texture was created uncompressed so we can lock it directly
	alphaskytexture->GetLevelDesc (0, &Level0Desc);
	alphaskytexture->GetSurfaceLevel (0, &skysurf);

	skysurf->LockRect (&Level0Rect, NULL, D3DLOCK_NO_DIRTY_UPDATE);

	// alloc texels
	if (!skyalphatexels)
	{
		skyalphatexels = (unsigned *) MainHunk->Alloc (Level0Desc.Width * Level0Desc.Height * sizeof (unsigned));
		copytexels = true;
	}

	for (int i = 0; i < Level0Desc.Width * Level0Desc.Height; i++)
	{
		// copy out the first time
		if (copytexels) skyalphatexels[i] = ((unsigned *) Level0Rect.pBits)[i];

		// copy back first and subsequent times
		((unsigned *) Level0Rect.pBits)[i] = skyalphatexels[i];

		// despite being created as D3DFMT_A8R8G8B8 this is actually bgra.  WTF Microsoft???
		byte *bgra = (byte *) &(((unsigned *) Level0Rect.pBits)[i]);

		float alpha = bgra[3];
		alpha *= r_skyalpha.value;
		bgra[3] = BYTE_CLAMP (alpha);
	}

	skysurf->UnlockRect ();
	skysurf->Release ();
	alphaskytexture->AddDirtyRect (NULL);
}


void D3D_ScrollSky (D3DTRANSFORMSTATETYPE unit, float scroll)
{
	// use cl.time so that it pauses properly when the console is down (same as everything else)
	float speedscale = cl.time * scroll;
	speedscale -= (int) speedscale & ~127;
	speedscale /= 128.0f;

	D3DMATRIX scrollmatrix;
	D3D_LoadIdentity (&scrollmatrix);

	// aaaaarrrrggghhhh - direct3D doesn't use standard matrix positions for texture transforms!!!
	// and where *exactly* was that documented again...?
	scrollmatrix._31 = speedscale;
	scrollmatrix._32 = speedscale;
	d3d_Device->SetTransform (unit, &scrollmatrix);
}


void D3DSky_SphereCallback1 (void *data)
{
	/*
	why is this not in a shader?
	because it's the fallback mode for when shaders aren't available!
	*/
	float scale = ((float *) data)[0];

	D3D_SetRenderState (D3DRS_COLORWRITEENABLE, 0x0000000F);
	D3D_SetTextureColorMode (0, D3DTOP_SELECTARG1);
	D3D_SetTextureState (0, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU);

	// invert the depth func and add the skybox
	// also disable depth writing so that it retains the z values from the clipping brushes
	D3D_SetRenderState (D3DRS_ZFUNC, D3DCMP_GREATEREQUAL);
	D3D_SetRenderState (D3DRS_ZWRITEENABLE, FALSE);

	// darkplaces warp
	D3D_SetVertexDeclaration (d3d_VDXyzTex1);

	D3D_SetTextureColorMode (0, D3DTOP_SELECTARG1, D3DTA_TEXTURE, D3DTA_DIFFUSE);
	D3D_SetTextureAlphaMode (0, D3DTOP_DISABLE);

	D3D_SetTextureColorMode (1, D3DTOP_BLENDTEXTUREALPHA, D3DTA_TEXTURE, D3DTA_CURRENT);
	D3D_SetTextureAlphaMode (1, D3DTOP_DISABLE);

	D3D_SetTextureColorMode (2, D3DTOP_DISABLE);
	D3D_SetTextureAlphaMode (2, D3DTOP_DISABLE);

	D3D_SetTextureMipmap (0, d3d_TexFilter, D3DTEXF_NONE);
	D3D_SetTextureMipmap (1, d3d_TexFilter, D3DTEXF_NONE);

	D3D_SetTexCoordIndexes (0, 0);
	D3D_SetTextureMatrixOp (D3DTTFF_COUNT2, D3DTTFF_COUNT2);
	D3D_SetTextureAddressMode (D3DTADDRESS_WRAP, D3DTADDRESS_WRAP);

	D3DMATRIX skymatrix;
	Q_MemCpy (&skymatrix, &d3d_WorldMatrix, sizeof (D3DMATRIX));
	D3D_ScaleMatrix (&skymatrix, scale, scale, scale);
	d3d_Device->SetTransform (D3DTS_WORLD, &skymatrix);

	D3D_ScrollSky (D3DTS_TEXTURE0, r_skybackscroll.value);
	D3D_ScrollSky (D3DTS_TEXTURE1, r_skyfrontscroll.value);

	D3D_SetTexture (0, solidskytexture);
	D3D_SetTexture (1, alphaskytexture);
}


void D3DSky_SphereCallback2 (void *blah)
{
	/*
	why is this not in a shader?
	because it's the fallback mode for when shaders aren't available!
	*/
	// take down specific stuff
	D3D_SetTextureMatrixOp (D3DTTFF_DISABLE, D3DTTFF_DISABLE);

	// restore transform
	d3d_Device->SetTransform (D3DTS_WORLD, &d3d_WorldMatrix);

	// restore the old depth func
	D3D_SetRenderState (D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	D3D_SetRenderState (D3DRS_ZWRITEENABLE, TRUE);
}


void D3D_AddSkySphereToRender (float scale)
{
	/*
	why is this not in a shader?
	because it's the fallback mode for when shaders aren't available!
	*/
	VBO_AddCallback (D3DSky_SphereCallback1, &scale, sizeof (float));

	if (d3d_DPSkyVerts && d3d_DPSkyIndexes)
	{
		VBO_AddSkySphere
		(
			d3d_DPSkyVerts,
			SKYSPHERE_NUMVERTS,
			d3d_DPSkyIndexes,
			SKYSPHERE_NUMTRIS * 3
		);
	}

	VBO_AddCallback (D3DSky_SphereCallback2);
}


void D3D_AddSkySurfaceToRender (msurface_t *surf, entity_t *ent)
{
	d3d_RenderDef.brush_polys++;

	if (d3d_RenderDef.skyframe != d3d_RenderDef.framecount)
	{
		// initialize sky for the frame
		D3D_InitializeSky ();

		// flag sky as having been initialized this frame
		d3d_RenderDef.skyframe = d3d_RenderDef.framecount;
	}

	VBO_AddSky (surf, ent);
}


void D3DSky_NoSkyCallback (void *blah)
{
	D3D_SetTextureState (0, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU);
}


void D3DSky_SkyboxCallback (void *blah)
{
	if (d3d_GlobalCaps.usingPixelShaders)
	{
		D3D_EndShaderPass ();
	}
	else
	{
		// revert states
		D3D_SetTextureState (0, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU);
		D3D_SetTextureMatrixOp (D3DTTFF_DISABLE);
	}
}


void D3DSky_PSCallback (void *blah)
{
	// done
	D3D_EndShaderPass ();
}


void D3D_FinalizeSky (void)
{
	if (d3d_RenderDef.skyframe != d3d_RenderDef.framecount) return;

	if (SkyboxValid)
		VBO_AddCallback (D3DSky_SkyboxCallback);
	else if (!d3d_GlobalCaps.usingPixelShaders)
		D3D_AddSkySphereToRender (r_clipdist / 8);
	else VBO_AddCallback (D3DSky_PSCallback);

	d3d_RenderDef.skyframe = -1;
}


/*
==============================================================================================================================

		SKY INITIALIZATION

==============================================================================================================================
*/


void D3D_InitSkySphere (void)
{
	// protect against multiple allocations
	// (this may be called every frame now)
	if (d3d_DPSkyVerts && d3d_DPSkyIndexes) return;

	// throw away any that might exist
	if (d3d_DPSkyVerts) MainZone->Free (d3d_DPSkyVerts);
	if (d3d_DPSkyIndexes) MainZone->Free (d3d_DPSkyIndexes);

	d3d_DPSkyVerts = NULL;
	d3d_DPSkyIndexes = NULL;

	int i, j;
	float a, b, x, ax, ay, v[3], length;
	float dx, dy, dz;

	dx = 16;
	dy = 16;
	dz = 16 / 3;

	d3d_DPSkyVerts = (warpverts_t *) MainZone->Alloc (SKYSPHERE_NUMVERTS * sizeof (warpverts_t));

	warpverts_t *ssv = d3d_DPSkyVerts;
	warpverts_t *ssv2 = ssv;

	i = SKYSPHERE_NUMVERTS;

	for (j = 0; j <= SKYGRID_SIZE; j++)
	{
		a = j * SKYGRID_RECIP;
		ax = cos (a * D3DX_PI * 2);
		ay = -sin (a * D3DX_PI * 2);

		for (i = 0; i <= SKYGRID_SIZE; i++)
		{
			b = i * SKYGRID_RECIP;
			x = cos ((b + 0.5) * D3DX_PI);

			v[0] = ax * x * dx;
			v[1] = ay * x * dy;
			v[2] = -sin ((b + 0.5) * D3DX_PI) * dz;

			// same calculation as classic Q1 sky but projected onto an actual physical sphere
			// (rather than on flat surfs) and calced as if from an origin of [0,0,0] to prevent
			// the heaving and buckling effect
			length = 3.0f / sqrt (v[0] * v[0] + v[1] * v[1] + (v[2] * v[2] * 9));

			ssv2->st[0] = v[0] * length;
			ssv2->st[1] = v[1] * length;
			ssv2->v[0] = v[0];
			ssv2->v[1] = v[1];
			ssv2->v[2] = v[2];

			ssv2++;
		}
	}

	d3d_DPSkyIndexes = (unsigned short *) MainZone->Alloc ((SKYGRID_SIZE * SKYGRID_SIZE * 6) * sizeof (unsigned short));

	unsigned short *skyindexes = d3d_DPSkyIndexes;
	unsigned short *e = skyindexes;

	for (j = 0; j < SKYGRID_SIZE; j++)
	{
		for (i = 0; i < SKYGRID_SIZE; i++)
		{
			*e++ =  j * SKYGRID_SIZE_PLUS_1 + i;
			*e++ =  j * SKYGRID_SIZE_PLUS_1 + i + 1;
			*e++ = (j + 1) * SKYGRID_SIZE_PLUS_1 + i;

			*e++ =  j * SKYGRID_SIZE_PLUS_1 + i + 1;
			*e++ = (j + 1) * SKYGRID_SIZE_PLUS_1 + i + 1;
			*e++ = (j + 1) * SKYGRID_SIZE_PLUS_1 + i;
		}
	}
}


/*
=============
R_InitSky

A sky texture is 256*128, with the right side being a masked overlay
==============
*/
void R_InitSky (miptex_t *mt)
{
	// sanity check
	if ((mt->width % 4) || (mt->width < 4) || (mt->height % 2) || (mt->height < 2))
	{
		Host_Error ("R_InitSky: invalid sky dimensions (%i x %i)\n", mt->width, mt->height);
		return;
	}

	// because you never know when a mapper might use a non-standard size...
	unsigned *trans = (unsigned *) Zone_Alloc (mt->width * mt->height * sizeof (unsigned) / 2);

	// copy out
	int transwidth = mt->width / 2;
	int transheight = mt->height;

	// destroy any current textures we might have
	SAFE_RELEASE (solidskytexture);
	SAFE_RELEASE (alphaskytexture);

	byte *src = (byte *) mt + mt->offsets[0];
	unsigned int transpix, r = 0, g = 0, b = 0;

	// make an average value for the back to avoid a fringe on the top level
	for (int i = 0; i < transheight; i++)
	{
		for (int j = 0; j < transwidth; j++)
		{
			// solid sky can go up as 8 bit
			int p = src[i * mt->width + j + transwidth];
			((byte * ) trans)[(i * transwidth) + j] = p;

			r += d3d_QuakePalette.standard[p].peRed;
			g += d3d_QuakePalette.standard[p].peGreen;
			b += d3d_QuakePalette.standard[p].peBlue;
		}
	}

	// bgr
	((byte *) &transpix)[2] = BYTE_CLAMP (r / (transwidth * transheight));
	((byte *) &transpix)[1] = BYTE_CLAMP (g / (transwidth * transheight));
	((byte *) &transpix)[0] = BYTE_CLAMP (b / (transwidth * transheight));
	((byte *) &transpix)[3] = 0;

	// upload it - solid sky can go up as 8 bit
	if (!D3D_LoadExternalTexture (&solidskytexture, va ("%s_solid", mt->name), 0))
		D3D_UploadTexture (&solidskytexture, trans, transwidth, transheight, IMAGE_NOCOMPRESS);

	// bottom layer
	for (int i = 0; i < transheight; i++)
	{
		for (int j = 0; j < transwidth; j++)
		{
			int p = src[i * mt->width + j];

			if (p == 0)
				trans[(i * transwidth) + j] = transpix;
			else trans[(i * transwidth) + j] = d3d_QuakePalette.standard32[p];
		}
	}

	// upload it - alpha sky needs to go up as 32 bit owing to averaging
	// don't compress it so that we can lock it for alpha updating
	if (!D3D_LoadExternalTexture (&alphaskytexture, va ("%s_alpha", mt->name), IMAGE_ALPHA))
		D3D_UploadTexture (&alphaskytexture, trans, transwidth, transheight, IMAGE_32BIT | IMAGE_ALPHA | IMAGE_NOCOMPRESS);

	// no texels yet
	skyalphatexels = NULL;

	// prevent it happening first time during game play
	D3D_UpdateSkyAlpha ();
	Zone_Free (trans);
}


char *suf[6] = {"rt", "bk", "lf", "ft", "up", "dn"};

void R_UnloadSkybox (void)
{
	// release any skybox textures we might already have
	SAFE_RELEASE (skyboxtextures[0]);
	SAFE_RELEASE (skyboxtextures[1]);
	SAFE_RELEASE (skyboxtextures[2]);
	SAFE_RELEASE (skyboxtextures[3]);
	SAFE_RELEASE (skyboxtextures[4]);
	SAFE_RELEASE (skyboxtextures[5]);
	SAFE_RELEASE (skyboxcubemap);

	// the skybox is invalid now so revert to regular warps
	SkyboxValid = false;
	CachedSkyBoxName[0] = 0;
	NumComponents = 0;
}

char *sbdir[] = {"gfx", "env", "gfx/env", NULL};


void R_MakeSkyboxCubeMap (void)
{
	if (!(d3d_DeviceCaps.TextureCaps & D3DPTEXTURECAPS_CUBEMAP))
	{
		// we don't support cubemaps
		R_UnloadSkybox ();
		SkyboxValid = false;
		return;
	}

	// take the largest of the dimensions for the cubemap texture
	int maxsize = 0;

	for (int i = 0; i < 6; i++)
	{
		if (!skyboxtextures[i]) continue;

		D3DSURFACE_DESC level0desc;

		hr = skyboxtextures[i]->GetLevelDesc (0, &level0desc);

		if (FAILED (hr)) continue;

		if (level0desc.Width > maxsize) maxsize = level0desc.Width;
		if (level0desc.Height > maxsize) maxsize = level0desc.Height;
	}

	// those wacky modders!!! ensure it's a power of 2...
	maxsize = D3D_PowerOf2Size (maxsize);

	// clamp to max supported size
	if (maxsize > d3d_DeviceCaps.MaxTextureWidth) maxsize = d3d_DeviceCaps.MaxTextureWidth;
	if (maxsize > d3d_DeviceCaps.MaxTextureHeight) maxsize = d3d_DeviceCaps.MaxTextureWidth;

	// prevent those wacky modders from choking our video ram and performance
	if (maxsize > 512) maxsize = 512;

	// now we can attempt to create the cubemap
	for (;;)
	{
		// too small!!!
		if (maxsize < 1) break;

		// attempt to create it
		// use DXT compression as some modders like to provide humungous sized skyboxes which annihilates your video RAM
		if (d3d_GlobalCaps.supportDXT1)
			hr = d3d_Device->CreateCubeTexture (maxsize, 1, 0, D3DFMT_DXT1, D3DPOOL_MANAGED, &skyboxcubemap, NULL);
		else hr = d3d_Device->CreateCubeTexture (maxsize, 1, 0, D3DFMT_X8R8G8B8, D3DPOOL_MANAGED, &skyboxcubemap, NULL);

		if (FAILED (hr))
		{
			// if we couldn't create it, it might be too big so reduce it in size
			maxsize >>= 1;
			continue;
		}

		// succeeded
		break;
	}

	if (!skyboxcubemap)
	{
		// failed
		R_UnloadSkybox ();
		SkyboxValid = false;
		return;
	}

	// map from d3d cubemap face to quake skybox face
	D3DCUBEMAP_FACES faces[] =
	{
		D3DCUBEMAP_FACE_POSITIVE_X, // right
		D3DCUBEMAP_FACE_POSITIVE_Y,	// back
		D3DCUBEMAP_FACE_NEGATIVE_X, // left
		D3DCUBEMAP_FACE_NEGATIVE_Y,	// front
		D3DCUBEMAP_FACE_POSITIVE_Z, // top
		D3DCUBEMAP_FACE_NEGATIVE_Z  // bottom
	};

	LPDIRECT3DSURFACE9 copysurf = NULL;

	// now fill it
	for (int i = 0; i < 6; i++)
	{
		if (!skyboxtextures[i]) continue;

		LPDIRECT3DSURFACE9 cubesurf = NULL;
		LPDIRECT3DSURFACE9 texsurf = NULL;
		D3DSURFACE_DESC level0desc;
		bool noscale = false;

		if (FAILED (hr = skyboxtextures[i]->GetLevelDesc (0, &level0desc))) continue;
		if (level0desc.Width == maxsize && level0desc.Height == maxsize) noscale = true;

		if (FAILED (hr = skyboxtextures[i]->GetSurfaceLevel (0, &texsurf))) texsurf = NULL;
		if (FAILED (hr = skyboxcubemap->GetCubeMapSurface (faces[i], 0, &cubesurf))) cubesurf = NULL;

		if (noscale && texsurf && cubesurf)
		{
			// if the source is the same size as the dest we can just align and copy directly
			D3D_AlignCubeMapFaceTexels (texsurf, faces[i]);
			QD3DXLoadSurfaceFromSurface (cubesurf, NULL, NULL, texsurf, NULL, NULL, D3DX_DEFAULT, 0);
		}
		else
		{
			if (!copysurf)
			{
				// create a new system memory surf to copy it to (this can be reused for subsequent faces that need it)
				hr = d3d_Device->CreateOffscreenPlainSurface (maxsize, maxsize, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &copysurf, NULL);
				if (FAILED (hr)) copysurf = NULL;
			}

			if (texsurf && cubesurf && copysurf)
			{
				// copy it from the original texture to a system memory surface at the correct resolution
				QD3DXLoadSurfaceFromSurface (copysurf, NULL, NULL, texsurf, NULL, NULL, D3DX_DEFAULT, 0);

				// realign the face texels to map from quake skybox to d3d cubemap
				D3D_AlignCubeMapFaceTexels (copysurf, faces[i]);

				// copy it back from system memory to the cubemap
				QD3DXLoadSurfaceFromSurface (cubesurf, NULL, NULL, copysurf, NULL, NULL, D3DX_DEFAULT, 0);
			}
		}

		// release all surfaces used here
		SAFE_RELEASE (texsurf);
		SAFE_RELEASE (cubesurf);

		// don't need the skybox texture any more either
		SAFE_RELEASE (skyboxtextures[i]);
	}

	SAFE_RELEASE (copysurf);

	// we have our cubemap now
	Con_DPrintf ("created cubemap at %i\n", maxsize);
}


void R_LoadSkyBox (char *basename, bool feedback)
{
	// force an unload of the current skybox
	R_UnloadSkybox ();

	int numloaded = 0;

	for (int sb = 0; sb < 6; sb++)
	{
		// attempt to load it (sometimes an underscore is expected)
		// don't compress these because we're going to copy them later on
		if (!D3D_LoadExternalTexture (&skyboxtextures[sb], va ("%s%s", basename, suf[sb]), IMAGE_32BIT | IMAGE_NOCOMPRESS | IMAGE_SYSMEM))
			if (!D3D_LoadExternalTexture (&skyboxtextures[sb], va ("%s_%s", basename, suf[sb]), IMAGE_32BIT | IMAGE_NOCOMPRESS | IMAGE_SYSMEM))
				continue;

		// loaded OK
		numloaded++;
	}

	if (numloaded)
	{
		// as FQ is the behaviour modders expect let's allow partial skyboxes (much as it galls me)
		if (feedback) Con_Printf ("Loaded %i skybox components\n", numloaded);

		// the skybox is valid now, no need to search any more
		SkyboxValid = true;
		NumComponents = numloaded;
		strcpy (CachedSkyBoxName, basename);
		R_MakeSkyboxCubeMap ();
		return;
	}

	if (feedback) Con_Printf ("Failed to load skybox\n");
}


void R_Loadsky_f (void)
{
	if (Cmd_Argc () != 2)
	{
		Con_Printf ("loadsky <skybox> : loads a skybox\n");
		return;
	}

	// send through the common loader
	R_LoadSkyBox (Cmd_Argv (1), true);
}


char *D3D_FindExternalTexture (char *);

void R_RevalidateSkybox (void)
{
	// look for the textures we used
	if (CachedSkyBoxName[0])
	{
		// revalidates the currently loaded skybox
		// need to copy out cached name as we're going to change it
		char basename[260];
		int numloaded = 0;

		for (int sb = 0; sb < 6; sb++)
		{
			// attempt to find it (sometimes an underscore is expected)
			if (!D3D_FindExternalTexture (va ("%s%s", CachedSkyBoxName, suf[sb])))
				if (!D3D_FindExternalTexture (va ("%s_%s", CachedSkyBoxName, suf[sb])))
					continue;

			// loaded OK
			numloaded++;
		}

		// ensure the same number of components
		if (numloaded == NumComponents) return;

		// OK, something is different so reload it
		strcpy (basename, CachedSkyBoxName);
		R_LoadSkyBox (basename, false);
	}
}


cmd_t Loadsky1_Cmd ("loadsky", R_Loadsky_f);
cmd_t Loadsky2_Cmd ("skybox", R_Loadsky_f);
cmd_t Loadsky3_Cmd ("sky", R_Loadsky_f);

