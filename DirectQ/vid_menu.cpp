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

#include <vector>

#include "menu_common.h"

typedef struct modematch_s
{
	int width;
	int height;
	int firstrealmode;
} modematch_t;


std::vector<modematch_t> d3d_ModeMatcher;

extern std::vector<D3DDISPLAYMODE> d3d_DisplayModes;

char *sshot_formats[] = {"TGA", "BMP", "PNG", "DDS", "JPG", "PCX", NULL};
int sshot_format_number = 0;
extern cvar_t scr_screenshotformat;
extern cvar_t scr_screenshot_jpeg;
extern cvar_t scr_screenshot_png;

char **menu_anisotropicmodes = NULL;
int menu_anisonum = 0;

extern cvar_t vid_mode;
extern cvar_t vid_fullscreen;
extern cvar_t r_anisotropicfilter;
extern cvar_t vid_vsync;
extern cvar_t vid_multisample;
extern cvar_t vid_windowborders;

extern cvar_t v_gamma;
extern cvar_t r_gamma;
extern cvar_t g_gamma;
extern cvar_t b_gamma;

char *filtermodes[] = {"None", "Point", "Linear", NULL};

int texfiltermode = 0;
int mipfiltermode = 0;

// dummy cvars for temp stuff
int dummy_vsync;
int dummy_fullscreen;
int dummy_windowborders;

int menu_multisamplenum = 0;
char **menu_multisamplelist = NULL;

int menu_modenum = 0;
int menu_refnum = 0;
char **menu_modelist = NULL;
char **menu_reflist = NULL;
char **menu_realreflist = NULL;

#define TAG_VIDMODEAPPLY	1
#define TAG_WINDOWED_ENABLE		256
#define TAG_FULLSCREEN_ENABLE	512
#define TAG_WINDOWED_HIDE	1024
#define TAG_FULLSCREEN_HIDE	2048

void D3DVid_DescribeMode (D3DDISPLAYMODE *mode);

void VID_ApplyModeChange (void)
{
	// these just signal a change is going at happen at the next screen update so it's safe to set them together
	Cvar_Set (&vid_mode, d3d_ModeMatcher[menu_modenum].firstrealmode + menu_refnum);
	Cvar_Set (&vid_vsync, dummy_vsync);
	Cvar_Set (&vid_fullscreen, dummy_fullscreen);
	Cvar_Set (&vid_windowborders, dummy_windowborders);

	// and run the actual change
	// we need to run a few screen refreshes to ensure that the mode and it's params sync properly (fixme - this is ugly)
	SCR_UpdateScreen (0);
	SCR_UpdateScreen (0);
	SCR_UpdateScreen (0);

	Con_Printf ("Changed to mode : %i : ", vid_mode.integer);
	D3DVid_DescribeMode (&d3d_CurrentMode);

	// position the selection back up one as the "apply" option is no longer valid
	menu_Video.Key (K_UPARROW);
}


bool Menu_VideoCheckNeedApply (void)
{
	// if any of the options that need a device reset have changed from their defaults
	// we signal to show the apply option
	if (d3d_ModeMatcher[menu_modenum].firstrealmode + menu_refnum != vid_mode.integer) return true;
	if (vid_vsync.integer != dummy_vsync) return true;
	if (vid_fullscreen.integer != dummy_fullscreen) return true;
	if (vid_windowborders.integer != dummy_windowborders) return true;

	// no apply needed
	return false;
}


void Menu_VideoDecodeTextureFilter (void)
{
	switch (d3d_TexFilter)
	{
	case D3DTEXF_POINT: texfiltermode = 0; break;
	default: texfiltermode = 1; break;
	}

	switch (d3d_MipFilter)
	{
	case D3DTEXF_NONE: mipfiltermode = 0; break;
	case D3DTEXF_POINT: mipfiltermode = 1; break;
	default: mipfiltermode = 2; break;
	}
}


void Menu_VideoEncodeTextureFilter (void)
{
	switch (texfiltermode)
	{
	case 0: d3d_TexFilter = D3DTEXF_POINT; break;
	default: d3d_TexFilter = D3DTEXF_LINEAR; break;
	}

	switch (mipfiltermode)
	{
	case 0: d3d_MipFilter = D3DTEXF_NONE; break;
	case 1: d3d_MipFilter = D3DTEXF_POINT; break;
	default: d3d_MipFilter = D3DTEXF_LINEAR; break;
	}
}


extern D3DDISPLAYMODE d3d_DesktopMode;


void Menu_VideoDecodeVideoMode (void)
{
	menu_modenum = 0;
	menu_refnum = 0;

	dummy_fullscreen = !!vid_fullscreen.integer;
	dummy_vsync = !!vid_vsync.integer;
	dummy_windowborders = !!vid_windowborders.integer;

	// decode the mode
	for (int i = 0; i < d3d_ModeMatcher.size (); i++)
	{
		if (d3d_ModeMatcher[i].width != d3d_CurrentMode.Width) continue;
		if (d3d_ModeMatcher[i].height != d3d_CurrentMode.Height) continue;

		menu_modenum = i;
		break;
	}

	// decode the refresh rate
	if (d3d_CurrentMode.RefreshRate)
	{
		for (int i = d3d_ModeMatcher[menu_modenum].firstrealmode, j = 0; i < d3d_DisplayModes.size (); i++, j++)
		{
			if (d3d_DisplayModes[i].Width != d3d_CurrentMode.Width) break;
			if (d3d_DisplayModes[i].Height != d3d_CurrentMode.Height) break;
			if (d3d_DisplayModes[i].RefreshRate != d3d_CurrentMode.RefreshRate) continue;

			// this is the one
			menu_refnum = j;
			break;
		}
	}

	// sync the vid_mode cvar (this should never trigger a change)
	Cvar_Set (&vid_mode, d3d_ModeMatcher[menu_modenum].firstrealmode + menu_refnum);
}


void Menu_VideoEncodeVideoMode (void)
{
}


int Menu_VideoCustomDraw (int y)
{
	// encode the selected mode options into a video mode number that we can compare with the current mode
	Menu_VideoEncodeVideoMode ();

	if (dummy_fullscreen)
	{
		menu_Video.ShowMenuOptions (TAG_WINDOWED_HIDE);
		menu_Video.HideMenuOptions (TAG_FULLSCREEN_HIDE);
	}
	else
	{
		menu_Video.HideMenuOptions (TAG_WINDOWED_HIDE);
		menu_Video.ShowMenuOptions (TAG_FULLSCREEN_HIDE);
	}

	// check for "Apply" on vid_mode change
	if (!Menu_VideoCheckNeedApply ())
		menu_Video.DisableMenuOptions (TAG_VIDMODEAPPLY);
	else menu_Video.EnableMenuOptions (TAG_VIDMODEAPPLY);

	Menu_VideoEncodeTextureFilter ();

	for (int i = 0; ; i++)
	{
		if (!sshot_formats[i])
		{
			// invalid format so default back to tga
			sshot_format_number = 0;
			Cvar_Set (&scr_screenshotformat, "tga");
			break;
		}

		if (i == sshot_format_number)
		{
			if (!_stricmp (sshot_formats[i], "jpg"))
			{
				Cvar_Set (&scr_screenshot_jpeg, 1.0f);
				Cvar_Set (&scr_screenshot_png, 0.0f);
			}
			else if (!_stricmp (sshot_formats[i], "png"))
			{
				Cvar_Set (&scr_screenshot_jpeg, 0.0f);
				Cvar_Set (&scr_screenshot_png, 1.0f);
			}
			else
			{
				Cvar_Set (&scr_screenshot_jpeg, 0.0f);
				Cvar_Set (&scr_screenshot_png, 0.0f);
			}

			Cvar_Set (&scr_screenshotformat, sshot_formats[i]);
			break;
		}
	}

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
	if (scr_screenshot_jpeg.value) Cvar_Set (&scr_screenshotformat, "jpg");
	if (scr_screenshot_png.value) Cvar_Set (&scr_screenshotformat, "png");

	for (int i = 0; ; i++)
	{
		if (!sshot_formats[i])
		{
			// invalid format so default back to tga
			sshot_format_number = 0;
			Cvar_Set (&scr_screenshotformat, "tga");
			break;
		}

		if (!_stricmp (scr_screenshotformat.string, sshot_formats[i]))
		{
			// found a format
			sshot_format_number = i;
			break;
		}
	}

	// decode the video mode and set currently selected stuff
	Menu_VideoDecodeVideoMode ();
	Menu_VideoDecodeTextureFilter ();

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


int DrawTestMode (int y)
{
	Menu_PrintCenter (y + 30, va ("Height: %i", d3d_DisplayModes[d3d_ModeMatcher[menu_modenum].firstrealmode].Height));
	Menu_PrintCenter (y + 40, va ("Rate: %i", d3d_DisplayModes[d3d_ModeMatcher[menu_modenum].firstrealmode].RefreshRate));
	return y;
}


int Menu_VideoCheckModeChange (int y)
{
	static int lastmodenum = -1;

	if (menu_modenum != lastmodenum)
	{
		D3DDISPLAYMODE *lastmode = NULL;

		// build the refresh rate list
		for (int i = d3d_ModeMatcher[menu_modenum].firstrealmode, j = 0; i < d3d_DisplayModes.size (); i++)
		{
			if (lastmode)
			{
				if (d3d_DisplayModes[i].Width != lastmode->Width) break;
				if (d3d_DisplayModes[i].Height != lastmode->Height) break;
			}

			// this is a valid entry to add
			menu_reflist[j] = menu_realreflist[i];
			menu_reflist[++j] = NULL;

			lastmode = &d3d_DisplayModes[i];
		}

		// reset the refresh rate option in case it has changed
		menu_refnum = 0;

		// cache so we don't need to rebuild the list
		lastmodenum = menu_modenum;
	}

	return y;
}


extern cvar_t idgamma_gamma;
extern cvar_t idgamma_intensity;
extern cvar_t idgamma_saturation;
extern cvar_t idgamma_modifyfullbrights;

void Menu_VideoBuild (void)
{
	// add the enumerated display modes to the menu
	menu_modelist = (char **) MainZone->Alloc ((d3d_DisplayModes.size () + 1) * sizeof (char *));
	menu_reflist = (char **) MainZone->Alloc ((d3d_DisplayModes.size () + 1) * sizeof (char *));
	menu_realreflist = (char **) MainZone->Alloc ((d3d_DisplayModes.size () + 1) * sizeof (char *));

	D3DDISPLAYMODE *lastmode = NULL;

	for (int i = 0, j = 0; i < d3d_DisplayModes.size (); i++)
	{
		if (!lastmode || (lastmode && (lastmode->Width != d3d_DisplayModes[i].Width || lastmode->Height != d3d_DisplayModes[i].Height)))
		{
			menu_modelist[j] = (char *) MainZone->Alloc (32);
			sprintf (menu_modelist[j], "%i x %i", d3d_DisplayModes[i].Width, d3d_DisplayModes[i].Height);
			menu_modelist[++j] = NULL;

			// this is a helper for translating the compressed mode nums to real mode nums
			modematch_t match = {d3d_DisplayModes[i].Width, d3d_DisplayModes[i].Height, i};
			d3d_ModeMatcher.push_back (match);
		}

		menu_realreflist[i] = (char *) MainZone->Alloc (16);
		sprintf (menu_realreflist[i], "%i hz", d3d_DisplayModes[i].RefreshRate);
		menu_realreflist[i + 1] = NULL;

		menu_reflist[i] = NULL;
		lastmode = &d3d_DisplayModes[i];
	}
//	for (int i = 0; i < d3d_DisplayModes.size (); i++)
	{
		/*
		menu_modelist[i] = (char *) MainZone->Alloc (32);
		sprintf (menu_modelist[i], "%i x %i", d3d_DisplayModes[i].Width, d3d_DisplayModes[i].Height);
		menu_modelist[i + 1] = NULL;

		menu_reflist[i] = (char *) MainZone->Alloc (16);
		sprintf (menu_reflist[i], "%i hz", d3d_DisplayModes[i].RefreshRate);
		menu_reflist[i + 1] = NULL;
		*/
	}

	menu_Video.AddOption (new CQMenuCustomDraw (Menu_VideoCheckModeChange));
	menu_Video.AddOption (new CQMenuCustomDraw (Menu_VideoCustomDraw));
	menu_Video.AddOption (new CQMenuSpinControl ("Video Mode", &menu_modenum, &menu_modelist));
	menu_Video.AddOption (TAG_WINDOWED_HIDE, new CQMenuSpinControl ("Refresh Rate", &menu_refnum, &menu_reflist));
	menu_Video.AddOption (TAG_FULLSCREEN_HIDE, new CQMenuIntegerToggle ("Borders", &dummy_windowborders, 0, 1));
	menu_Video.AddOption (new CQMenuIntegerToggle ("Fullscreen", &dummy_fullscreen, 0, 1));
	menu_Video.AddOption (new CQMenuIntegerToggle ("Vertical Sync", &dummy_vsync, 0, 1));

	menu_Video.AddOption (new CQMenuSpacer (DIVIDER_LINE));
	menu_Video.AddOption (TAG_VIDMODEAPPLY, new CQMenuCommand ("Apply Video Mode Change", VID_ApplyModeChange));

	// add the rest of the options to ensure that they;re kept in order
	menu_Video.AddOption (new CQMenuSpacer ());
	menu_Video.AddOption (new CQMenuTitle ("Configure Video Options"));

	menu_Video.AddOption (new CQMenuSpinControl ("Screenshot Format", &sshot_format_number, sshot_formats));
	menu_Video.AddOption (new CQMenuSpinControl ("Texture Filter", &texfiltermode, &filtermodes[1]));
	menu_Video.AddOption (new CQMenuSpinControl ("Mipmap Filter", &mipfiltermode, filtermodes));

	if (d3d_DeviceCaps.MaxAnisotropy > 1)
	{
		// count the number of modes available
		for (int i = 1, mode = 1;; i++, mode *= 2)
		{
			if (mode == d3d_DeviceCaps.MaxAnisotropy)
			{
				menu_anisotropicmodes = (char **) MainZone->Alloc ((i + 1) * sizeof (char *));

				for (int m = 0, f = 1; m < i; m++, f *= 2)
				{
					menu_anisotropicmodes[m] = (char *) MainZone->Alloc (32);

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

	/*
	menu_modelist = (char **) MainZone->Alloc (d3d_DisplayModes.size () * sizeof (char *));
	menu_reflist = (char **) MainZone->Alloc (d3d_DisplayModes.size () * sizeof (char *));

	for (int i = 0, j = 0; i < d3d_DisplayModes.size (); i++)
	{
		if (!lastmode || (lastmode && (lastmode->Width != d3d_DisplayModes[i].Width || lastmode->Height != d3d_DisplayModes[i].Height)))
		{
			menu_modelist[j] = (char *) MainZone->Alloc (32);
			sprintf (menu_modelist[j], "%i x %i", d3d_DisplayModes[i].Width, d3d_DisplayModes[i].Height);
			menu_modelist[++j] = NULL;

			// this is a helper for translating the compressed mode nums to real mode nums
			modematch_t match = {d3d_DisplayModes[i].Width, d3d_DisplayModes[i].Height, i};
			d3d_ModeMatcher.push_back (match);
		}

		menu_realreflist[i] = (char *) MainZone->Alloc (16);
		sprintf (menu_realreflist[i], "%i hz", d3d_DisplayModes[i].RefreshRate);
		menu_realreflist[i + 1] = NULL;

		menu_reflist[i] = NULL;
		lastmode = &d3d_DisplayModes[i];
	}
	*/

	// these were cutesy to have in here, but, y'know, really...
	/*
	menu_Video.AddOption (new CQMenuSpacer ());
	menu_Video.AddOption (new CQMenuTitle ("IDGamma Settings"));
	menu_Video.AddOption (new CQMenuCvarSlider ("Intensity", &idgamma_intensity, 0.0f, 2.0f, 0.1f));	// 0.1 .. 10.0
	menu_Video.AddOption (new CQMenuCvarSlider ("Gamma", &idgamma_gamma, 0.0f, 1.0f, 0.05f));	// 0.1 .. 1.0
	menu_Video.AddOption (new CQMenuCvarSlider ("Saturation", &idgamma_saturation, 1, 3, 1));	// none/little/medium/full
	menu_Video.AddOption (new CQMenuCvarToggle ("Modify Fullbrights", &idgamma_modifyfullbrights, 0, 1));
	*/
}


