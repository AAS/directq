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

LPDIRECT3DTEXTURE9 solidskytexture = NULL;
LPDIRECT3DTEXTURE9 alphaskytexture = NULL;
LPDIRECT3DCUBETEXTURE9 skyboxcubemap = NULL;
LPDIRECT3DTEXTURE9 skyboxtextures[6] = {NULL};
LPDIRECT3DTEXTURE9 skyspheretexture = NULL;
LPDIRECT3DTEXTURE9 skyatan2texture = NULL;

extern float AngleBuf[256];

void D3DSky_ReleaseTextures (void)
{
	SAFE_RELEASE (skyboxtextures[0]);
	SAFE_RELEASE (skyboxtextures[1]);
	SAFE_RELEASE (skyboxtextures[2]);
	SAFE_RELEASE (skyboxtextures[3]);
	SAFE_RELEASE (skyboxtextures[4]);
	SAFE_RELEASE (skyboxtextures[5]);

	SAFE_RELEASE (skyboxcubemap);
	SAFE_RELEASE (skyspheretexture);
	SAFE_RELEASE (skyatan2texture);

	SAFE_RELEASE (solidskytexture);
	SAFE_RELEASE (alphaskytexture);
}


/*
==============================================================================================================================

		SKY WARP RENDERING

==============================================================================================================================
*/

// the menu needs to know the name of the loaded skybox
char CachedSkyBoxName[MAX_PATH] = {0};
bool SkyboxValid = false;
bool SkySphereValid = false;
int NumSkyboxComponents = 0;

float spherescale_y = 1.0f;

cvar_t r_skybackscroll ("r_skybackscroll", 8, CVAR_ARCHIVE);
cvar_t r_skyfrontscroll ("r_skyfrontscroll", 16, CVAR_ARCHIVE);
cvar_t r_skyalpha ("r_skyalpha", 1, CVAR_ARCHIVE);
cvar_t r_skyscale ("r_skyscale", 3, CVAR_ARCHIVE);

// intended to be used by mods
cvar_t r_skyrotate_x ("r_skyrotate_x", 0.0f);
cvar_t r_skyrotate_y ("r_skyrotate_y", 0.0f);
cvar_t r_skyrotate_z ("r_skyrotate_z", 0.0f);
cvar_t r_skyrotate_speed ("r_skyrotate_speed", 0.0f);

cvar_t r_skyspherescale ("r_skyspherescale", 1, CVAR_ARCHIVE);

void D3DSky_DrawSkySurfaces (msurface_t *chain)
{
	// instead of doing all this in the VS by hand we'll use a matrix instead; this lets us
	// use the same VS for all 3 sky types
	QMATRIX skymatrix;
	int ShaderPass = 0;

	skymatrix.LoadIdentity ();

	if (SkyboxValid || SkySphereValid)
	{
		if ((r_skyrotate_x.value || r_skyrotate_y.value || r_skyrotate_z.value) && r_skyrotate_speed.value)
		{
			// rotate the skybox
			float rot[3] =
			{
				r_skyrotate_x.value,
				r_skyrotate_y.value,
				r_skyrotate_z.value
			};

			VectorNormalize (rot);
			skymatrix.Rotate (rot[0], rot[1], rot[2], cl.time * r_skyrotate_speed.value);
		}

		D3D_SetTextureMipmap (0, d3d_TexFilter, D3DTEXF_NONE);

		if (SkyboxValid)
		{
			D3DHLSL_SetTexture (0, skyboxcubemap);
			D3D_SetTextureAddress (0, D3DTADDRESS_CLAMP);
			ShaderPass = FX_PASS_SKYBOX;
			D3DHLSL_SetPass (FX_PASS_SKYBOX);
		}
		else
		{
			D3D_SetSamplerState (0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
			D3D_SetSamplerState (0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

			D3DHLSL_SetTexture (0, skyspheretexture);
			D3DHLSL_SetTexture (1, skyatan2texture);

			D3DHLSL_SetFloat ("skyspherescaley", spherescale_y * r_skyspherescale.value);

			D3D_SetTextureMipmap (1, d3d_TexFilter, D3DTEXF_NONE);
			D3D_SetTextureAddress (1, D3DTADDRESS_CLAMP);

			ShaderPass = FX_PASS_SKYSPHERE;
			D3DHLSL_SetPass (FX_PASS_SKYSPHERE);
		}
	}
	else
	{
		// flatten the sphere
		skymatrix.Scale (1.0f, 1.0f, 3.0f);

		// hlsl sky warp
		float speedscale[] =
		{
			cl.time * r_skybackscroll.value,
			cl.time * r_skyfrontscroll.value,
			r_skyscale.value
		};

		// wrap in 0 to 127 range and bring back to 0 to 1 range
		speedscale[0] = (speedscale[0] - ((int) speedscale[0] & ~127)) * 0.0078125f;
		speedscale[1] = (speedscale[1] - ((int) speedscale[1] & ~127)) * 0.0078125f;

		D3DHLSL_SetTexture (0, solidskytexture);
		D3DHLSL_SetTexture (1, alphaskytexture);

		D3DHLSL_SetAlpha (r_skyalpha.value);
		D3DHLSL_SetFloatArray ("Scale", speedscale, 3);

		D3D_SetTextureMipmap (0, d3d_TexFilter, D3DTEXF_NONE);
		D3D_SetTextureMipmap (1, d3d_TexFilter, D3DTEXF_NONE);

		D3D_SetTextureAddress (0, D3DTADDRESS_WRAP);
		D3D_SetTextureAddress (1, D3DTADDRESS_WRAP);

		ShaderPass = FX_PASS_SKYWARP;
		D3DHLSL_SetPass (FX_PASS_SKYWARP);
	}

	// initialize sky for the frame
	if (r_skyalpha.value < 0.0f) Cvar_Set (&r_skyalpha, 0.0f);
	if (r_skyalpha.value > 1.0f) Cvar_Set (&r_skyalpha, 1.0f);

	// move relative to view position only
	skymatrix.Translate (-r_refdef.vieworigin[0], -r_refdef.vieworigin[1], -r_refdef.vieworigin[2]);

	D3DHLSL_SetMatrix ("EntMatrix", &skymatrix);

	for (; chain; chain = chain->texturechain)
	{
		D3DBrush_PrecheckSurface (chain, NULL);
		D3DBrush_BatchSurface (chain);
	}

	D3DBrush_FlushSurfaces ();
}


/*
==============================================================================================================================

		SKY INITIALIZATION

==============================================================================================================================
*/

void FractalNoise (unsigned char *noise, int size, int startgrid);
void FractalNoise32 (unsigned int *noise, int size, int startgrid);


/*
=============
D3DSky_InitTextures

A sky texture is 256*128, with the right side being a masked overlay
==============
*/
void D3DSky_InitTextures (miptex_t *mt, char **paths)
{
	// sanity check
	if ((mt->width % 4) || (mt->width < 4) || (mt->height % 2) || (mt->height < 2))
	{
		Host_Error ("D3DSky_InitTextures: invalid sky dimensions (%i x %i)\n", mt->width, mt->height);
		return;
	}

	// because you never know when a mapper might use a non-standard size...
	int hunkmark = MainHunk->GetLowMark ();
	unsigned *trans = (unsigned *) MainHunk->Alloc (mt->width * mt->height * sizeof (unsigned) / 2);

	// copy out
	int transwidth = mt->width / 2;
	int transheight = mt->height;

	// we don't really need to do this (we could just reuse the old textures) but that's kinda broke right now
	// (the attempt to load an external will NULL them anyway........)
	SAFE_RELEASE (solidskytexture);
	SAFE_RELEASE (alphaskytexture);

	unsigned int transpix, r = 0, g = 0, b = 0;

	// make an average value for the back to avoid a fringe on the top level
	for (int i = 0; i < transheight; i++)
	{
		for (int j = 0; j < transwidth; j++)
		{
			// solid sky can go up as 8 bit
			int p = mt->texels[i * mt->width + j + transwidth];
			((byte *) trans)[(i * transwidth) + j] = p;

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
	if ((solidskytexture = D3DTexture_LoadExternal (va ("%s_solid", mt->name), paths, 0)) == NULL)
		solidskytexture = D3DTexture_Upload (trans, transwidth, transheight, 0);

	// bottom layer
	for (int i = 0; i < transheight; i++)
	{
		for (int j = 0; j < transwidth; j++)
		{
			int p = mt->texels[i * mt->width + j];

			if (p == 0)
				trans[(i * transwidth) + j] = transpix;
			else trans[(i * transwidth) + j] = d3d_QuakePalette.standard32[p];
		}
	}

	// upload it - alpha sky needs to go up as 32 bit owing to averaging
	if ((alphaskytexture = D3DTexture_LoadExternal (va ("%s_alpha", mt->name), paths, IMAGE_ALPHA)) == NULL)
		alphaskytexture = D3DTexture_Upload (trans, transwidth, transheight, IMAGE_32BIT | IMAGE_ALPHA);

	// prevent it happening first time during game play
	MainHunk->FreeToLowMark (hunkmark);
}


char *suf[6] = {"rt", "bk", "lf", "ft", "up", "dn"};

void D3DSky_UnloadSkybox (void)
{
	// release any skybox textures we might already have
	SAFE_RELEASE (skyboxtextures[0]);
	SAFE_RELEASE (skyboxtextures[1]);
	SAFE_RELEASE (skyboxtextures[2]);
	SAFE_RELEASE (skyboxtextures[3]);
	SAFE_RELEASE (skyboxtextures[4]);
	SAFE_RELEASE (skyboxtextures[5]);

	SAFE_RELEASE (skyboxcubemap);
	SAFE_RELEASE (skyspheretexture);
	SAFE_RELEASE (skyatan2texture);

	// the skybox is invalid now so revert to regular warps
	SkyboxValid = false;
	SkySphereValid = false;

	CachedSkyBoxName[0] = 0;
	NumSkyboxComponents = 0;
}

char *sbdir[] = {"gfx", "env", "gfx/env", NULL};


void D3DSky_MakeSkyboxCubeMap (void)
{
	if (!(d3d_DeviceCaps.TextureCaps & D3DPTEXTURECAPS_CUBEMAP))
	{
		// we don't support cubemaps
		D3DSky_UnloadSkybox ();
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
	maxsize = D3DTexture_PowerOf2Size (maxsize);

	// clamp to max supported size
	if (maxsize > d3d_DeviceCaps.MaxTextureWidth) maxsize = d3d_DeviceCaps.MaxTextureWidth;
	if (maxsize > d3d_DeviceCaps.MaxTextureHeight) maxsize = d3d_DeviceCaps.MaxTextureWidth;

	// now we can attempt to create the cubemap
	for (;;)
	{
		// too small!!!
		if (maxsize < 1) break;

		// attempt to create it
		hr = d3d_Device->CreateCubeTexture (maxsize, 1, 0, D3DFMT_X8R8G8B8, D3DPOOL_MANAGED, &skyboxcubemap, NULL);

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
		D3DSky_UnloadSkybox ();
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
			D3DImage_AlignCubeMapFaceTexels (texsurf, faces[i]);
			D3DXLoadSurfaceFromSurface (cubesurf, NULL, NULL, texsurf, NULL, NULL, D3DX_DEFAULT, 0);
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
				D3DXLoadSurfaceFromSurface (copysurf, NULL, NULL, texsurf, NULL, NULL, D3DX_DEFAULT, 0);

				// realign the face texels to map from quake skybox to d3d cubemap
				D3DImage_AlignCubeMapFaceTexels (copysurf, faces[i]);

				// copy it back from system memory to the cubemap
				D3DXLoadSurfaceFromSurface (cubesurf, NULL, NULL, copysurf, NULL, NULL, D3DX_DEFAULT, 0);
			}
		}

		// release all surfaces used here
		SAFE_RELEASE (texsurf);
		SAFE_RELEASE (cubesurf);

		// don't need the skybox texture any more either
		SAFE_RELEASE (skyboxtextures[i]);
	}

	SAFE_RELEASE (copysurf);
	skyboxcubemap->PreLoad ();

	// we have our cubemap now
	Con_DPrintf ("created cubemap at %i\n", maxsize);
}


void D3DSky_LoadSkyBox (char *basename, bool feedback)
{
	// force an unload of the current skybox
	D3DSky_UnloadSkybox ();

	int numloaded = 0;
	int sbflags = IMAGE_32BIT | IMAGE_SYSMEM | IMAGE_SKYBOX;
	char *sbpaths[] = {"gfx/env/", "env/gfx/", "env/", NULL};

	for (int sb = 0; sb < 6; sb++)
	{
		// attempt to load it (sometimes an underscore is expected)
		if ((skyboxtextures[sb] = D3DTexture_LoadExternal (va ("%s%s", basename, suf[sb]), sbpaths, sbflags)) == NULL)
			if ((skyboxtextures[sb] = D3DTexture_LoadExternal (va ("%s_%s", basename, suf[sb]), sbpaths, sbflags)) == NULL)
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
		NumSkyboxComponents = numloaded;
		strcpy (CachedSkyBoxName, basename);
		D3DSky_MakeSkyboxCubeMap ();
		return;
	}

	// not a skybox so try load a sphere texture
	if ((skyspheretexture = D3DTexture_LoadExternal (basename, sbpaths, IMAGE_32BIT)) != NULL)
	{
		D3DSURFACE_DESC surfdesc;

		skyspheretexture->GetLevelDesc (0, &surfdesc);
		spherescale_y = ((float) surfdesc.Width / (float) surfdesc.Height) / 2.0f;

		SAFE_RELEASE (skyatan2texture);
		D3DLOCKED_RECT lockrect;

		d3d_Device->CreateTexture (256, 256, 1, 0, D3DFMT_L16, D3DPOOL_MANAGED, &skyatan2texture, NULL);
		skyatan2texture->LockRect (0, &lockrect, NULL, 0);

		unsigned short *data = (unsigned short *) lockrect.pBits;

		// skyatan2texture
		for (int y = 0, i = 0; y < 256; y++)
		{
			for (int x = 0; x < 256; x++, i++)
			{
				if (x == 0 && y == 0)
					data[i] = 0;
				else
				{
					float fx = (float) x / 255.0f;
					float fy = (float) y / 255.0f;

					fx = (fx * 2.0f) - 1.0f;
					fy = (fy * 2.0f) - 1.0f;

					float at2 = atan2 (fy, fx);

					if (at2 == at2)
					{
						if ((at2 = (at2 + D3DX_PI) / (2 * D3DX_PI)) > 1)
							data[i] = 65535;
						else if (at2 < 0)
							data[i] = 0;
						else data[i] = (unsigned short) ((at2 * 65535.0f) + 0.5f);
					}
					else data[i] = 0;
				}
			}
		}

		skyatan2texture->UnlockRect (0);
		SkySphereValid = true;

		// SCR_WriteTextureToTGA ("at2.tga", skyatan2texture, D3DFMT_X8R8G8B8);
		return;
	}

	if (feedback) Con_Printf ("Failed to load skybox\n");
}


void D3DSky_Loadsky_f (void)
{
	if (Cmd_Argc () != 2)
	{
		Con_Printf ("loadsky <skybox> : loads a skybox\n");
		return;
	}

	// send through the common loader
	D3DSky_LoadSkyBox (Cmd_Argv (1), true);
}


char *D3D_FindExternalTexture (char *);

void D3DSky_RevalidateSkybox (void)
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
		if (numloaded == NumSkyboxComponents) return;

		// OK, something is different so reload it
		strcpy (basename, CachedSkyBoxName);
		D3DSky_LoadSkyBox (basename, false);
	}
}


void D3DSky_ParseWorldSpawn (void)
{
	// get a pointer to the entities lump
	char *data = cl.worldmodel->brushhdr->entities;
	char key[40];
	extern char lastworldmodel[];

	// can never happen, otherwise we wouldn't have gotten this far
	if (!data) return;

	// if we're on the same map as before we keep the old settings, otherwise we wipe them
	if (!strcmp (lastworldmodel, cl.worldmodel->name))
		return;

	// parse the opening brace
	data = COM_Parse (data);

	// likewise can never happen
	if (!data) return;
	if (com_token[0] != '{') return;

	while (1)
	{
		// parse the key
		data = COM_Parse (data);

		// there is no key (end of worldspawn)
		if (!data) break;

		if (com_token[0] == '}') break;

		// allow keys with a leading _
		if (com_token[0] == '_')
			Q_strncpy (key, &com_token[1], 39);
		else Q_strncpy (key, com_token, 39);

		// remove trailing spaces
		while (key[strlen (key) - 1] == ' ') key[strlen (key) - 1] = 0;

		// parse the value
		data = COM_Parse (data);

		// likewise should never happen (has already been successfully parsed server-side and any errors that
		// were going to happen would have happened then; we only check to guard against pointer badness)
		if (!data) return;

		// check the key for a sky - notice the lack of standardisation in full swing again here!
		if (!_stricmp (key, "sky") || !_stricmp (key, "skyname") || !_stricmp (key, "q1sky") || !_stricmp (key, "skybox"))
		{
			// attempt to load it (silently fail)
			// direct from com_token - is this safe?  should be...
			D3DSky_LoadSkyBox (com_token, false);
			continue;
		}
	}
}


cmd_t Loadsky1_Cmd ("loadsky", D3DSky_Loadsky_f);
cmd_t Loadsky2_Cmd ("skybox", D3DSky_Loadsky_f);
cmd_t Loadsky3_Cmd ("sky", D3DSky_Loadsky_f);

