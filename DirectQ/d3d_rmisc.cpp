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
// r_misc.c

#include "quakedef.h"
#include "d3d_quake.h"
#include "resource.h"

void R_InitParticles (void);
void R_ClearParticles (void);
void GL_BuildLightmaps (void);
void R_LoadSkyBox (char *basename, bool feedback);

LPDIRECT3DTEXTURE9 r_notexture = NULL;
extern LPDIRECT3DTEXTURE9 crosshairtexture;
extern LPDIRECT3DTEXTURE9 playertextures[];

/*
====================
D3D_SetFVFStateManaged

sets the FVF
====================
*/
void D3D_SetFVFStateManaged (DWORD NewFVF)
{
	static DWORD CurrentFVF = 0;

	// unchanged
	if (NewFVF == CurrentFVF) return;

	// set
	d3d_Device->SetFVF (NewFVF);
	CurrentFVF = NewFVF;
}


/*
==================
R_InitTextures
==================
*/
void R_InitTextures (void)
{
	int		x,y, m;
	byte	*dest;

	// create a simple checkerboard texture for the default
	r_notexture_mip = (texture_t *) Heap_TagAlloc (TAG_STARTUP, sizeof (texture_t) + 16*16+8*8+4*4+2*2);

	r_notexture_mip->width = r_notexture_mip->height = 16;
	r_notexture_mip->offsets[0] = sizeof(texture_t);
	r_notexture_mip->offsets[1] = r_notexture_mip->offsets[0] + 16*16;
	r_notexture_mip->offsets[2] = r_notexture_mip->offsets[1] + 8*8;
	r_notexture_mip->offsets[3] = r_notexture_mip->offsets[2] + 4*4;
	
	for (m=0 ; m<4 ; m++)
	{
		dest = (byte *)r_notexture_mip + r_notexture_mip->offsets[m];
		for (y=0 ; y< (16>>m) ; y++)
			for (x=0 ; x< (16>>m) ; x++)
			{
				if (  (y< (8>>m) ) ^ (x< (8>>m) ) )
					*dest++ = 0;
				else
					*dest++ = 0xff;
			}
	}	
}


// textures to load from resources
extern LPDIRECT3DTEXTURE9 particledottexture;
extern LPDIRECT3DTEXTURE9 particlesmoketexture;
extern LPDIRECT3DTEXTURE9 particletracertexture;
extern LPDIRECT3DTEXTURE9 R_PaletteTexture;
extern LPDIRECT3DTEXTURE9 particleblood[];
LPDIRECT3DTEXTURE9 r_blacktexture = NULL;
LPDIRECT3DTEXTURE9 r_greytexture = NULL;

void R_ReleaseResourceTextures (void)
{
	for (int i = 0; i < 8; i++)
		SAFE_RELEASE (particleblood[i]);

	SAFE_RELEASE (particledottexture);
	SAFE_RELEASE (particlesmoketexture);
	SAFE_RELEASE (crosshairtexture);
	SAFE_RELEASE (particletracertexture);
	SAFE_RELEASE (r_blacktexture);
	SAFE_RELEASE (r_greytexture);
}


void R_InitResourceTextures (void)
{
	// load any textures contained in exe resources
	D3D_LoadResourceTexture (&particledottexture, IDR_PARTICLEDOT, IMAGE_MIPMAP);
	D3D_LoadResourceTexture (&particlesmoketexture, IDR_PARTICLESMOKE, IMAGE_MIPMAP);
	D3D_LoadResourceTexture (&particletracertexture, IDR_PARTICLETRACER, IMAGE_MIPMAP);
	D3D_LoadResourceTexture (&particleblood[0], IDR_PARTICLEBLOOD1, IMAGE_MIPMAP);
	D3D_LoadResourceTexture (&particleblood[1], IDR_PARTICLEBLOOD2, IMAGE_MIPMAP);
	D3D_LoadResourceTexture (&particleblood[2], IDR_PARTICLEBLOOD3, IMAGE_MIPMAP);
	D3D_LoadResourceTexture (&particleblood[3], IDR_PARTICLEBLOOD4, IMAGE_MIPMAP);
	D3D_LoadResourceTexture (&particleblood[4], IDR_PARTICLEBLOOD5, IMAGE_MIPMAP);
	D3D_LoadResourceTexture (&particleblood[5], IDR_PARTICLEBLOOD6, IMAGE_MIPMAP);
	D3D_LoadResourceTexture (&particleblood[6], IDR_PARTICLEBLOOD7, IMAGE_MIPMAP);
	D3D_LoadResourceTexture (&particleblood[7], IDR_PARTICLEBLOOD8, IMAGE_MIPMAP);
	D3D_LoadResourceTexture (&crosshairtexture, IDR_CROSSHAIR, 0);

	// create a texture for the palette
	// this is to save on state changes when a flat colour is needed; rather than
	// switching off texturing and other messing, we just draw this one.
	byte *paldata = (byte *) Heap_QMalloc (128 * 128);

	for (int i = 0; i < 256; i++)
	{
		int row = (i >> 4) * 8;
		int col = (i & 15) * 8;

		for (int x = col; x < col + 8; x++)
		{
			for (int y = row; y < row + 8; y++)
			{
				int p = y * 128 + x;

				paldata[p] = i;
			}
		}
	}

	D3D_LoadTexture (&R_PaletteTexture, 128, 128, paldata, d_8to24table, false, false);

	// clear to black
	memset (paldata, 0, 128 * 128);

	// load the black texture - we must mipmap this and also load it as 32 bit
	// (in case palette index 0 isn't black).  also load it really really small...
	D3D_LoadTexture (&r_blacktexture, 4, 4, paldata, NULL, true, false);

	// clear to grey
	memset (paldata, 128, 128 * 128);

	// load the black texture - we must mipmap this and also load it as 32 bit
	// (in case palette index 0 isn't black).  also load it really really small...
	D3D_LoadTexture (&r_greytexture, 4, 4, paldata, NULL, true, false);

	Heap_QFree (paldata);

	// load the notexture properly
	D3D_LoadTexture (&r_notexture, r_notexture_mip->width, r_notexture_mip->height, (byte *) (r_notexture_mip + 1), d_8to24table, true, false);
}


/*
===============
R_Init
===============
*/
cvar_t r_lerporient ("r_lerporient", "1", CVAR_ARCHIVE);
cvar_t r_lerpframe ("r_lerpframe", "1", CVAR_ARCHIVE);

extern cvar_t r_lerplightstyle;
extern cvar_t r_lightupdatefrequency;
void D3D_InitTextures (void);

cmd_t R_TimeRefresh_f_Cmd ("timerefresh", R_TimeRefresh_f);
cmd_t R_ReadPointFile_f_Cmd ("pointfile", R_ReadPointFile_f);

void R_Init (void)
{	
	extern cvar_t gl_finish;

	D3D_InitTextures ();

	R_InitParticles ();
	R_InitResourceTextures ();

	for (int i = 0; i < 16; i++)
		SAFE_RELEASE (playertextures[i]);
}


/*
===============
R_TranslatePlayerSkin

Translates a skin texture by the per-player color lookup
===============
*/
void R_TranslatePlayerSkin (int playernum)
{
	int		top, bottom;
	byte	translate[256];
	unsigned	translate32[256];
	int		i, j, s;
	model_t	*model;
	aliashdr_t *paliashdr;
	byte	*original;
	unsigned	pixels[512*256], *out;
	unsigned	scaled_width, scaled_height;
	int			inwidth, inheight;
	byte		*inrow;
	unsigned	frac, fracstep;

	top = cl.scores[playernum].colors & 0xf0;
	bottom = (cl.scores[playernum].colors &15)<<4;

	for (i=0 ; i<256 ; i++)
		translate[i] = i;

	for (i=0 ; i<16 ; i++)
	{
		if (top < 128)	// the artists made some backwards ranges.  sigh.
			translate[TOP_RANGE+i] = top+i;
		else
			translate[TOP_RANGE+i] = top+15-i;
				
		if (bottom < 128)
			translate[BOTTOM_RANGE+i] = bottom+i;
		else
			translate[BOTTOM_RANGE+i] = bottom+15-i;
	}

	//
	// locate the original skin pixels
	//
	currententity = cl_entities[1 + playernum];
	model = currententity->model;
	if (!model)
		return;		// player doesn't have a model yet
	if (model->type != mod_alias)
		return; // only translate skins on alias models

	paliashdr = model->ah;

	s = paliashdr->skinwidth * paliashdr->skinheight;

	if (currententity->skinnum < 0 || currententity->skinnum >= paliashdr->numskins)
	{
		Con_Printf("(%d): Invalid player skin #%d\n", playernum, currententity->skinnum);
		original = (byte *)paliashdr + paliashdr->texels[0];
	}
	else original = (byte *)paliashdr + paliashdr->texels[currententity->skinnum];

	if (s & 3) Sys_Error ("R_TranslateSkin: s&3");

	inwidth = paliashdr->skinwidth;
	inheight = paliashdr->skinheight;

	// recreate the texture
	SAFE_RELEASE (playertextures[playernum]);

	byte *translated = (byte *) Heap_QMalloc (paliashdr->skinwidth * paliashdr->skinheight);

	for (i=0 ; i<s ; i+=4)
	{
		translated[i] = translate[original[i]];
		translated[i+1] = translate[original[i+1]];
		translated[i+2] = translate[original[i+2]];
		translated[i+3] = translate[original[i+3]];
	}

	// do mipmap these because it only happens when colour changes
	D3D_LoadTexture (&playertextures[playernum], paliashdr->skinwidth, paliashdr->skinheight, translated, (unsigned int *) d_8to24table, true, false);

	Heap_QFree (translated);
}


/*
===============
R_NewMap
===============
*/
void S_InitAmbients (void);

void R_ParseWorldSpawn (void)
{
	// get a pointer to the entities lump
	char *data = cl.worldbrush->entities;
	char key[40];

	// can never happen, otherwise we wouldn't have gotten this far
	if (!data) return;

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
			strncpy (key, &com_token[1], 39);
		else strncpy (key, com_token, 39);

		// remove trailing spaces
		while (key[strlen (key) - 1] == ' ') key[strlen (key) - 1] = 0;

		// parse the value
		data = COM_Parse (data);

		// likewise should never happen (has already been successfully parsed server-side and any errors that
		// were going to happen would have happened then; we only check to guard against pointer badness)
		if (!data) return;

		// check the key for a sky - notice the lack of standardisation in full swing again here!
		if (!stricmp (key, "sky") || !stricmp (key, "skyname") || !stricmp (key, "q1sky") || !stricmp (key, "skybox"))
		{
			// attempt to load it (silently fail)
			// direct from com_token - is this safe?  should be...
			R_LoadSkyBox (com_token, false);
		}

		// can add anything else we want to parse out of the worldspawn here too...
	}
}


void R_CheckTransWater (void);

void R_NewMap (void)
{
	int		i;

	// normal light value (consistency with 'm' * 22)
	for (i = 0; i < 256; i++) d_lightstylevalue[i] = 264;

	memset (&r_worldentity, 0, sizeof (r_worldentity));
	r_worldentity.model = cl.worldmodel;

	r_viewleaf = NULL;
	R_ClearParticles ();

	GL_BuildLightmaps ();

	// flush unused textures from the cache
	D3D_FlushTextures ();

	// parse worldspawn from the entities lump to determine any map-specific fields in there
	R_ParseWorldSpawn ();

	// check for translucent water in the map
	R_CheckTransWater ();

	// as sounds are now cleared between maps these sounds also need to be
	// reloaded otherwise the known_sfx will go out of sequence for them
	CL_InitTEnts ();
	S_InitAmbients ();
}


/*
====================
R_TimeRefresh_f

For program optimization
====================
*/
void R_TimeRefresh_f (void)
{
	int			i;
	float		start, stop, time;

	if (cls.state != ca_connected)
	{
		Con_Printf ("You cannot timerefresh when not connected.\n");
		return;
	}

	start = Sys_FloatTime ();
	float oldvang = r_refdef.viewangles[1];

	d3d_Device->EndScene ();
	d3d_Device->Present (NULL, NULL, NULL, NULL);

	for (i = 0; i < 128; i++)
	{
		r_refdef.viewangles[1] = i / 128.0 * 360.0;

		d3d_Device->BeginScene ();
		R_RenderView ();
		d3d_Device->EndScene ();
		d3d_Device->Present (NULL, NULL, NULL, NULL);
	}

	r_refdef.viewangles[1] = oldvang;
	stop = Sys_FloatTime ();
	time = stop - start;

	Con_Printf ("%f seconds (%f fps)\n", time, 128 / time);
}

void D_FlushCaches (void)
{
}


