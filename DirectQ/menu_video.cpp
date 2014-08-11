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

#include "winquake.h"
#include "resource.h"
#include <commctrl.h>


#include "menu_common.h"

extern bool WinDWM;

char **menu_videomodes = NULL;
int menu_videomodenum = 0;

char **menu_anisotropicmodes = NULL;
int menu_anisonum = 0;

extern cvar_t d3d_mode;
extern cvar_t v_gamma;
extern cvar_t r_gamma;
extern cvar_t g_gamma;
extern cvar_t b_gamma;
extern cvar_t r_anisotropicfilter;
extern cvar_t vid_vsync;

char *filtermodes[] = {"None", "Point", "Linear", NULL};

int texfiltermode = 0;
int mipfiltermode = 0;

// dummy cvars for temp stuff
int dummy_vsync;

bool D3D_ModeIsCurrent (d3d_ModeDesc_t *mode);

extern d3d_ModeDesc_t *d3d_ModeList;
extern int d3d_NumModes;
extern int d3d_NumWindowedModes;

#define TAG_VIDMODEAPPLY	1
#define TAG_HLSL			32
#define TAG_WINDOWED_ENABLE		256
#define TAG_FULLSCREEN_ENABLE	512
#define TAG_WINDOWED_HIDE	1024
#define TAG_FULLSCREEN_HIDE	2048

extern int d3d_AllowedRefreshRates[];
extern int d3d_NumRefreshRates;

// if these are changed they need to be changed in menu_other.cpp as well
// these need to be kept high enough so that they don't interfere with other options
#ifndef MENU_TAG_SIMPLE
#define MENU_TAG_SIMPLE			(1 << 30)
#endif

#ifndef MENU_TAG_FULL
#define MENU_TAG_FULL			(1 << 31)
#endif

void VID_ApplyModeChange (void)
{
	// run a screen update after each to make sure they only occur one at a time
	Cvar_Set (&d3d_mode, menu_videomodenum);
	SCR_UpdateScreen ();

	Cvar_Set (&vid_vsync, dummy_vsync);
	SCR_UpdateScreen ();
	HUD_Changed ();

	// position the selection back up one as the "apply" option is no longer valid
	menu_Video.Key (K_UPARROW);
}


bool Menu_VideoCheckNeedApply (void)
{
	// if any of the options that need a device reset have changed from their defaults
	// we signal to show the apply option
	if (menu_videomodenum != d3d_mode.integer) return true;
	if (vid_vsync.integer != dummy_vsync) return true;

	// no apply needed
	return false;
}


void Menu_VideoDecodeTextureFilter (void)
{
	switch (d3d_TexFilter)
	{
	case D3DTEXF_POINT:
		texfiltermode = 0;
		break;

	default:
		// linear or anisotropic
		texfiltermode = 1;
		break;
	}

	switch (d3d_MipFilter)
	{
	case D3DTEXF_NONE:
		mipfiltermode = 0;
		break;

	case D3DTEXF_POINT:
		mipfiltermode = 1;
		break;

	default:
		mipfiltermode = 2;
		break;
	}
}


void Menu_VideoEncodeTextureFilter (void)
{
	switch (texfiltermode)
	{
	case 0:
		d3d_TexFilter = D3DTEXF_POINT;
		break;

	default:
		d3d_TexFilter = D3DTEXF_LINEAR;

		break;
	}

	switch (mipfiltermode)
	{
	case 0:
		d3d_MipFilter = D3DTEXF_NONE;
		break;

	case 1:
		d3d_MipFilter = D3DTEXF_POINT;
		break;

	default:
		d3d_MipFilter = D3DTEXF_LINEAR;
		break;
	}
}

char **menu_windowedres = NULL;
char **menu_fullscrnres = NULL;

int menu_windnum = 0;
int menu_fullnum = 0;

char *bpplist[] = {"16 bpp", "32 bpp", NULL};
char *modelist[] = {"Windowed", "Fullscreen", NULL};

int bppnum = 0;
int modetypenum = 0;

extern D3DDISPLAYMODE d3d_DesktopMode;

void Menu_VideoDecodeVideoModes (d3d_ModeDesc_t *modes, int totalnummodes, int numwindowed)
{
	char tempmode[64];

	// windowed modes
	if (numwindowed)
	{
		// add one extra for NULL termination
		menu_windowedres = (char **) MainZone->Alloc ((numwindowed + 1) * sizeof (char *));

		for (int i = 0; i < numwindowed; i++)
		{
			d3d_ModeDesc_t *mode = modes + i;

			// copy to a temp buffer, alloc in memory, copy back to memory and ensure NULL termination
			sprintf (tempmode, "%i x %i", mode->d3d_Mode.Width, mode->d3d_Mode.Height);
			menu_windowedres[i] = (char *) MainZone->Alloc (strlen (tempmode) + 1);
			strcpy (menu_windowedres[i], tempmode);
			menu_windowedres[i + 1] = NULL;
		}
	}

	// fullscreen modes
	if (totalnummodes > numwindowed)
	{
		int numfullscreen = totalnummodes - numwindowed;

		// add one extra for NULL termination
		menu_fullscrnres = (char **) MainZone->Alloc ((numfullscreen + 1) * sizeof (char *));

		for (int i = 0, realfs = 0; i < numfullscreen; i++)
		{
			d3d_ModeDesc_t *mode = modes + numwindowed + i;

			switch (mode->d3d_Mode.Format)
			{
			case D3DFMT_R5G6B5:
			case D3DFMT_X1R5G5B5:
			case D3DFMT_A1R5G5B5:
				// these are the 16-bit modes and they don't get added to the list
				break;

			default:
				// add 32 bpp modes
				// copy to a temp buffer, alloc in memory, copy back to memory and ensure NULL termination
				sprintf (tempmode, "%i x %i", mode->d3d_Mode.Width, mode->d3d_Mode.Height);
				menu_fullscrnres[realfs] = (char *) MainZone->Alloc (strlen (tempmode) + 1);
				strcpy (menu_fullscrnres[realfs], tempmode);
				menu_fullscrnres[realfs + 1] = NULL;
				realfs++;
				break;
			}
		}
	}
}


void Menu_VideoDecodeVideoMode (void)
{
	if (menu_windowedres || menu_fullscrnres)
	{
		D3DFORMAT findformat = D3DFMT_UNKNOWN;
		char **findres = NULL;

		// set the correct type
		if (d3d_CurrentMode.Format == D3DFMT_UNKNOWN || d3d_CurrentMode.RefreshRate == 0)
		{
			modetypenum = 0;
			findformat = d3d_DesktopMode.Format;
			findres = menu_windowedres;
		}
		else
		{
			modetypenum = 1;
			findformat = d3d_CurrentMode.Format;
			findres = menu_fullscrnres;
		}

		switch (findformat)
		{
		case D3DFMT_R5G6B5:		bppnum = 0; break;
		case D3DFMT_X1R5G5B5:	bppnum = 0; break;
		case D3DFMT_A1R5G5B5:	bppnum = 0; break;
		case D3DFMT_X8R8G8B8:	bppnum = 1; break;
		case D3DFMT_UNKNOWN:	bppnum = 1; break;
		default:				bppnum = 1; break;
		}

		char resbuf[32];
		int findresnum = 0;

		sprintf (resbuf, "%i x %i", d3d_CurrentMode.Width, d3d_CurrentMode.Height);

		for (int i = 0;; i++)
		{
			if (!findres[i]) break;

			if (!strcmp (resbuf, findres[i]))
			{
				findresnum = i;
				break;
			}
		}

		if (modetypenum == 0)
		{
			menu_windnum = findresnum;
			menu_fullnum = 0;
		}
		else
		{
			menu_windnum = 0;
			menu_fullnum = findresnum;
		}

		return;
	}

	// note: on any sane and reasonable driver these will all eval to true
	// on the kind of crap that *certain* people use they might not...
	bool allowwindowed = false;
	bool allowfullscreen = false;
	bool allow16bpp = false;
	bool allow32bpp = false;

	char *fullresolutions = (char *) scratchbuf;
	int numfullresolutions = 0;

	char *windresolutions = (char *) (scratchbuf + SCRATCHBUF_SIZE / 2);
	int numwindresolutions = 0;

	for (int i = 0; i < d3d_NumModes; i++)
	{
		d3d_ModeDesc_t *mode = d3d_ModeList + i;

		if (mode->AllowWindowed)
		{
			sprintf (windresolutions, "%i x %i", mode->d3d_Mode.Width, mode->d3d_Mode.Height);
			windresolutions += 32;
			numwindresolutions++;
			allowwindowed = true;
		}
		else
		{
			if (mode->BPP == 32)
			{
				sprintf (fullresolutions, "%i x %i", mode->d3d_Mode.Width, mode->d3d_Mode.Height);
				fullresolutions += 32;
				numfullresolutions++;
				allow32bpp = true;
			}
			else allow16bpp = true;

			allowfullscreen = true;
		}
	}

	// reset to the start of the buffers
	fullresolutions = (char *) scratchbuf;
	windresolutions = (char *) (scratchbuf + SCRATCHBUF_SIZE / 2);

	// add 1 for NULL termination
	menu_windowedres = (char **) MainZone->Alloc ((numwindresolutions + 1) * sizeof (char *));
	menu_fullscrnres = (char **) MainZone->Alloc ((numfullresolutions + 1) * sizeof (char *));

	// fill in windowed resolutions
	for (int i = 0; i < numwindresolutions; i++)
	{
		menu_windowedres[i] = (char *) MainZone->Alloc (strlen (windresolutions) + 1);
		strcpy (menu_windowedres[i], windresolutions);
		windresolutions += 32;
	}

	// fill in fullscreen resolutions
	for (int i = 0; i < numfullresolutions; i++)
	{
		menu_fullscrnres[i] = (char *) MainZone->Alloc (strlen (fullresolutions) + 1);
		strcpy (menu_fullscrnres[i], fullresolutions);
		fullresolutions += 32;
	}

	// call recursively to set the selected options
	Menu_VideoDecodeVideoMode ();
}


void Menu_VideoEncodeVideoMode (void)
{
	// pick a safe mode in case we don't find it
	menu_videomodenum = 0;

	int width = 0;
	int height = 0;

	// scan the dimensions
	if (modetypenum == 0)
		sscanf (menu_windowedres[menu_windnum], "%i x %i", &width, &height);
	else sscanf (menu_fullscrnres[menu_fullnum], "%i x %i", &width, &height);

	// search through the modes for it
	for (int i = 0; i < d3d_NumModes; i++)
	{
		d3d_ModeDesc_t *mode = d3d_ModeList + i;

		// ensure matching dimensions
		if (mode->d3d_Mode.Width != width) continue;
		if (mode->d3d_Mode.Height != height) continue;

		// match windowed/fullscreen
		if (!mode->AllowWindowed && modetypenum == 0) continue;
		if (mode->AllowWindowed && modetypenum == 1) continue;

		// match bit depth if fullscreen
		if (modetypenum == 1 && bppnum == 0 && mode->BPP != 16) continue;
		if (modetypenum == 1 && bppnum == 1 && mode->BPP != 32) continue;

		// this is the mode
		menu_videomodenum = mode->ModeNum;
		break;
	}
}


int Menu_VideoCustomDraw (int y)
{
	// encode the selected mode options into a video mode number that we can compare with the current mode
	Menu_VideoEncodeVideoMode ();

	if (modetypenum == 0)
	{
		menu_Video.DisableMenuOptions (TAG_FULLSCREEN_ENABLE);
		menu_Video.HideMenuOptions (TAG_FULLSCREEN_HIDE);
		menu_Video.EnableMenuOptions (TAG_WINDOWED_ENABLE);
		menu_Video.ShowMenuOptions (TAG_WINDOWED_HIDE);
	}
	else
	{
		menu_Video.EnableMenuOptions (TAG_FULLSCREEN_ENABLE);
		menu_Video.ShowMenuOptions (TAG_FULLSCREEN_HIDE);
		menu_Video.DisableMenuOptions (TAG_WINDOWED_ENABLE);
		menu_Video.HideMenuOptions (TAG_WINDOWED_HIDE);
	}

	// check for "Apply" on d3d_mode change
	if (!Menu_VideoCheckNeedApply ())
		menu_Video.DisableMenuOptions (TAG_VIDMODEAPPLY);
	else menu_Video.EnableMenuOptions (TAG_VIDMODEAPPLY);

	Menu_VideoEncodeTextureFilter ();

	// no anisotropic filtering available
	if (d3d_DeviceCaps.MaxAnisotropy < 2) return y;

	// store the selected anisotropic filter into the r_aniso cvar
	for (int af = 1, i = 0;; i++, af <<= 1)
	{
		if (i == menu_anisonum)
		{
			Con_DPrintf ("Setting r_anisotropicfilter to %i\n", af);
			Cvar_Set (&r_anisotropicfilter, af);
			break;
		}
	}

	return y;
}


void Menu_VideoCustomEnter (void)
{
	// decode the video mode and set currently selected stuff
	Menu_VideoDecodeVideoMode ();

	// take it from the d3d_mode cvar
	menu_videomodenum = (int) d3d_mode.value;

	Menu_VideoDecodeTextureFilter ();

	// store out vsync
	dummy_vsync = vid_vsync.integer;

	int real_aniso;

	// no anisotropic filtering available
	if (d3d_DeviceCaps.MaxAnisotropy < 2) return;

	// get the real value from the cvar - users may enter any old crap manually!!!
	for (real_aniso = 1; real_aniso < r_anisotropicfilter.value; real_aniso <<= 1);

	// clamp it
	if (real_aniso < 1) real_aniso = 1;
	if (real_aniso > d3d_DeviceCaps.MaxAnisotropy) real_aniso = d3d_DeviceCaps.MaxAnisotropy;

	// store it back
	Cvar_Set (&r_anisotropicfilter, real_aniso);

	// now derive the menu entry from it
	for (int i = 0, af = 1;; i++, af <<= 1)
	{
		if (af == real_aniso)
		{
			menu_anisonum = i;
			break;
		}
	}
}


extern cvar_t r_optimizealiasmodels;
extern cvar_t r_cachealiasmodels;

void Menu_VideoBuild (void)
{
	menu_Video.AddOption (new CQMenuCustomDraw (Menu_VideoCustomDraw));
	menu_Video.AddOption (new CQMenuSpinControl ("Video Mode Type", &modetypenum, modelist));
	menu_Video.AddOption (TAG_WINDOWED_HIDE, new CQMenuSpinControl ("Resolution", &menu_windnum, &menu_windowedres));
	menu_Video.AddOption (TAG_FULLSCREEN_HIDE, new CQMenuSpinControl ("Resolution", &menu_fullnum, &menu_fullscrnres));
	menu_Video.AddOption (TAG_FULLSCREEN_ENABLE, new CQMenuSpinControl ("Color Depth", &bppnum, bpplist));
	menu_Video.AddOption (new CQMenuIntegerToggle ("Vertical Sync", &dummy_vsync, 0, 1));
	menu_Video.AddOption (new CQMenuSpacer (DIVIDER_LINE));
	menu_Video.AddOption (TAG_VIDMODEAPPLY, new CQMenuCommand ("Apply Video Mode Change", VID_ApplyModeChange));

	// add the rest of the options to ensure that they;re kept in order
	menu_Video.AddOption (new CQMenuSpacer ());
	menu_Video.AddOption (new CQMenuTitle ("Configure Video Options"));

	menu_Video.AddOption (new CQMenuSpinControl ("Texture Filter", &texfiltermode, &filtermodes[1]));
	menu_Video.AddOption (new CQMenuSpinControl ("Mipmap Filter", &mipfiltermode, filtermodes));

	if (d3d_DeviceCaps.MaxAnisotropy > 1)
	{
		// count the number of modes available
		for (int i = 1, mode = 1;; i++, mode *= 2)
		{
			if (mode == d3d_DeviceCaps.MaxAnisotropy)
			{
				menu_anisotropicmodes = (char **) Zone_Alloc ((i + 1) * sizeof (char *));

				for (int m = 0, f = 1; m < i; m++, f *= 2)
				{
					menu_anisotropicmodes[m] = (char *) Zone_Alloc (32);

					if (f == 1)
						Q_strncpy (menu_anisotropicmodes[m], "Off", 32);
					else _snprintf (menu_anisotropicmodes[m], 32, "%i x Filtering", f);
				}

				menu_anisotropicmodes[i] = NULL;
				break;
			}
		}

		menu_Video.AddOption (new CQMenuSpinControl ("Anisotropic Filter", &menu_anisonum, menu_anisotropicmodes));
	}

	menu_Video.AddOption (new CQMenuSpacer (DIVIDER_LINE));
	menu_Video.AddOption (new CQMenuCvarToggle ("Optimize MDL Files", &r_optimizealiasmodels, 0, 1));
	menu_Video.AddOption (new CQMenuCvarToggle ("Cache Mesh to Disk", &r_cachealiasmodels, 0, 1));
}


