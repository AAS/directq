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
#include "menu_common.h"
#include "winquake.h"
#include "d3d_model.h"
#include "d3d_quake.h"

extern qpic_t *gfx_p_option_lmp;

// cvars used
extern cvar_t scr_screenshotformat;
extern cvar_t r_lerporient;
extern cvar_t r_lerpframe;
extern cvar_t r_lerplightstyle;
extern cvar_t r_coronas;
extern cvar_t gl_flashblend;
extern cvar_t freelook;
extern cvar_t chase_back;
extern cvar_t chase_up;
extern cvar_t chase_right;
extern cvar_t chase_active;
extern cvar_t chase_scale;
extern cvar_t sound_nominal_clip_dist;
extern cvar_t ambient_level;
extern cvar_t ambient_fade;
extern cvar_t r_coloredlight;
extern cvar_t r_extradlight;
extern cvar_t r_warpspeed;
extern cvar_t r_lavaalpha;
extern cvar_t r_telealpha;
extern cvar_t r_slimealpha;
extern cvar_t r_lockalpha;
extern cvar_t r_automapshot;
extern cvar_t com_hipnotic;
extern cvar_t com_rogue;
extern cvar_t com_quoth;
extern cvar_t com_nehahra;
extern cvar_t scr_centerlog;
extern cvar_t host_savenamebase;
extern cvar_t scr_shotnamebase;
extern cvar_t r_skybackscroll;
extern cvar_t r_skyfrontscroll;
extern cvar_t r_waterwarptime;
extern cvar_t menu_fillcolor;
extern cvar_t r_skyalpha;
extern cvar_t v_gamma;
extern cvar_t lm_gamma;
extern cvar_t vid_contrast;
extern cvar_t r_waterwarp;
extern cvar_t r_wateralpha;
extern cvar_t loadas8bit;
extern cvar_t s_khz;
extern cvar_t scr_sbaralpha;
extern cvar_t scr_centersbar;
extern cvar_t r_aliaslightscale;
extern cvar_t r_particlesize;
extern cvar_t r_particlestyle;
extern cvar_t gl_fullbrights;
extern cvar_t r_truecontentscolour;

CQMenu menu_Main (m_main);
CQMenu menu_Singleplayer (m_other);
CQMenu menu_Save (m_save);
CQMenu menu_Load (m_load);
CQMenu menu_Multiplayer (m_multiplayer);
CQMenu menu_TCPIPNewGame (m_lanconfig_newgame);
CQMenu menu_TCPIPJoinGame (m_lanconfig_joingame);
CQMenu menu_GameConfig (m_gameoptions);
CQMenu menu_Setup (m_setup);
CQMenu menu_Options (m_options);
CQMenu menu_Video (m_video);
CQMenu menu_Sound (m_other);
CQMenu menu_Help (m_help);
CQMenu menu_Input (m_other);
CQMenu menu_Keybindings (m_keys);
CQMenu menu_Effects (m_other);
CQMenu menu_EffectsSimple (m_other);
CQMenu menu_UI (m_other);
CQMenu menu_WarpSurf (m_other);
CQMenu menu_Fog (m_other);
CQMenu menu_ContentDir (m_other);
CQMenu menu_Chase (m_other);
CQMenu menu_Game (m_other);
CQMenu menu_Controller (m_other);
CQMenu menu_Maps (m_other);
CQMenu menu_Demo (m_other);

// for use in various menus
char *SkillNames[] =
{
	"Easy",
	"Normal",
	"Hard",
	"NIGHTMARE",
	NULL
};

void Menu_FixupSpin (int *spinnum, char **spintext);

// for enabling simple menus
cvar_t menu_advanced ("menu_advanced", "0", CVAR_ARCHIVE);

/*
========================================================================================================================

					MAIN MENU

========================================================================================================================
*/

int m_save_demonum;

void Menu_ExitMenus (void)
{
	key_dest = key_game;
	m_state = m_none;
	cls.demonum = m_save_demonum;
	menu_Current = NULL;

	if (cls.demonum != -1 && !cls.demoplayback && cls.state != ca_connected) CL_NextDemo ();
}


void Menu_ToggleSimpleMenus (void)
{
	if (!menu_advanced.integer)
	{
		menu_Main.ShowMenuOptions (MENU_TAG_SIMPLE);
		menu_Main.HideMenuOptions (MENU_TAG_FULL);

		menu_Singleplayer.ShowMenuOptions (MENU_TAG_SIMPLE);
		menu_Singleplayer.HideMenuOptions (MENU_TAG_FULL);

		menu_Multiplayer.ShowMenuOptions (MENU_TAG_SIMPLE);
		menu_Multiplayer.HideMenuOptions (MENU_TAG_FULL);

		menu_Options.ShowMenuOptions (MENU_TAG_SIMPLE);
		menu_Options.HideMenuOptions (MENU_TAG_FULL);

		menu_Video.ShowMenuOptions (MENU_TAG_SIMPLE);
		menu_Video.HideMenuOptions (MENU_TAG_FULL);
	}
	else
	{
		menu_Main.ShowMenuOptions (MENU_TAG_FULL);
		menu_Main.HideMenuOptions (MENU_TAG_SIMPLE);

		menu_Singleplayer.ShowMenuOptions (MENU_TAG_FULL);
		menu_Singleplayer.HideMenuOptions (MENU_TAG_SIMPLE);

		menu_Multiplayer.ShowMenuOptions (MENU_TAG_FULL);
		menu_Multiplayer.HideMenuOptions (MENU_TAG_SIMPLE);

		menu_Options.ShowMenuOptions (MENU_TAG_FULL);
		menu_Options.HideMenuOptions (MENU_TAG_SIMPLE);

		menu_Video.ShowMenuOptions (MENU_TAG_FULL);
		menu_Video.HideMenuOptions (MENU_TAG_SIMPLE);
	}
}


int Menu_MainCustomDraw (int y)
{
	Menu_ToggleSimpleMenus ();
	return y;
}


void Menu_MainCustomEnter (void)
{
	m_save_demonum = cls.demonum;
	cls.demonum = -1;
}


void Menu_MainExitQuake (void)
{
	if (!SCR_ModalMessage ("Are you sure you want to\nExit Quake?\n", "Confirm Exit", MB_YESNO))
		return;

	key_dest = key_console;
	m_state = m_none;
	menu_Current = NULL;
	Host_Quit_f ();
}


void Menu_InitMainMenu (void)
{
	extern qpic_t *gfx_mainmenu_lmp;
	extern qpic_t *gfx_ttl_main_lmp;

	menu_Main.AddOption (new CQMenuCustomDraw (Menu_MainCustomDraw));
	menu_Main.AddOption (new CQMenuBanner (&gfx_ttl_main_lmp));
	menu_Main.AddOption (new CQMenuSpacer (DIVIDER_LINE));
	menu_Main.AddOption (new CQMenuCursorSubMenu (&menu_Singleplayer));
	menu_Main.AddOption (new CQMenuCursorSubMenu (&menu_Multiplayer));
	menu_Main.AddOption (new CQMenuCursorSubMenu (&menu_Options));
	menu_Main.AddOption (new CQMenuCursorSubMenu (&menu_Help));
	menu_Main.AddOption (new CQMenuCursorSubMenu (Menu_MainExitQuake));
	menu_Main.AddOption (new CQMenuChunkyPic (&gfx_mainmenu_lmp));

	menu_Main.AddOption (new CQMenuCustomEnter (Menu_MainCustomEnter));
	menu_Main.AddOption (new CQMenuCustomKey (K_ESCAPE, Menu_ExitMenus));
}


/*
========================================================================================================================

					OPTIONS MENU

========================================================================================================================
*/


// commands
void Host_WriteConfiguration (void);
void Cvar_ResetAll (void);

void Menu_OptionsResetToDefaults (void)
{
	// this option should also put cvars back to their defaults
	Cvar_ResetAll ();

	Cbuf_AddText ("exec default.cfg\n");
}


void Menu_OptionsGoToConsole (void)
{
	m_state = m_none;
	Con_ToggleConsole_f ();
}


char *key_bindnames[][2] =
{
	{"+attack", 		"Attack"},
	{"impulse 10", 		"Next Weapon"},
	{"impulse 12", 		"Previous Weapon"},
	{"+jump", 			"Jump/Swim up"},
	{"+forward", 		"Walk Forward"},
	{"+back", 			"Backpedal"},
	{"+left", 			"Turn Left"},
	{"+right", 			"Turn Right"},
	{"+speed", 			"Run"},
	{"+moveleft", 		"Step Left"},
	{"+moveright", 		"Step Right"},
	{"+strafe", 		"Sidestep"},
	{"centerview", 		"Center View"},
	{"+mlook", 			"Mouse Look"},
	{"+quickshot",		"Quick Shot"},
	{"+quickgrenade",	"Quick Grenade"},
	{"+quickrocket",	"Quick Rocket"},
	{"+quickshaft",		"Quick Shaft"},
	{"bestsafe",		"Best Safe Weapon"},
	{"lastweapon",		"Last Weapon"},
};


bool keybind_grab = false;

#define	NUMBINDINGS		(sizeof (key_bindnames) / sizeof (key_bindnames[0]))

int bind_cursor = 0;


void Menu_FindKeybindingsForAction (char *action, int *twokeys)
{
	char	*b;

	twokeys[0] = twokeys[1] = -1;
	int l = strlen (action);
	int count = 0;

	for (int j = 0; j < 256; j++)
	{
		if (!(b = keybindings[j])) continue;

		if (!strncmp (b, action, l))
		{
			twokeys[count++] = j;

			if (count == 2) break;
		}
	}
}


int Menu_KeybindingsCustomDraw (int y)
{
	int keys[2];

	if (keybind_grab)
		Menu_PrintCenterWhite (y, "Press a Key or Button for this Action");
	else Menu_PrintCenterWhite (y, "Press ENTER to Change or BACKSPACE to Clear");

	Menu_PrintCenter (y + 15, DIVIDER_LINE);

	y += 30;

	for (int i = 0; i < NUMBINDINGS; i++)
	{
		// draw the current option highlight wider so that we have room for everything
		if (i == bind_cursor) Menu_HighlightBar (-200, y, 400);

		// action
		Menu_Print (124 - strlen (key_bindnames[i][1]) * 8, y, key_bindnames[i][1]);

		Menu_FindKeybindingsForAction (key_bindnames[i][0], keys);

		if (keys[0] == -1)
			Menu_Print (148, y, "(unbound)");
		else
		{
			char *name = Key_KeynumToString (keys[0]);
			Menu_PrintWhite (148, y, name);

			int x = strlen (name) * 8;

			if (keys[1] != -1)
			{
				Menu_Print (148 + x + 8, y, "or");
				Menu_PrintWhite (148 + x + 32, y, Key_KeynumToString (keys[1]));
			}
		}

		// next bind name
		y += 15;
	}

	return y;
}


void Menu_UnbindAction (char *action)
{
	char	*b;

	int l = strlen (action);

	for (int j = 0; j < 256; j++)
	{
		if (!(b = keybindings[j])) continue;
		if (!strncmp (b, action, l)) Key_SetBinding (j, "");
	}
}


// we need to manage the keys for this menu ourselves on account of bindings
void Menu_KeybindingsCustomKey (int key)
{
	if (keybind_grab)
	{
		menu_soundlevel = m_sound_enter;

		// ESC and the console key can never be bound
		if (key == K_ESCAPE)
		{
			// don't change the binding
			keybind_grab = false;
			return;
		}
		else if (key != '`')
		{
			// unbind the key from anything
			Key_SetBinding (key, "");

			// set the new binding
			char cmd[256];
			_snprintf (cmd, 256, "bind \"%s\" \"%s\"\n", Key_KeynumToString (key), key_bindnames[bind_cursor][0]);
			Cbuf_InsertText (cmd);
		}

		// exit binding mode
		keybind_grab = false;
		return;
	}

	// get keys for currently selected action
	int keys[2];
	Menu_FindKeybindingsForAction (key_bindnames[bind_cursor][0], keys);

	switch (key)
	{
	case K_ESCAPE:
		Menu_StackPop ();
		break;

	case K_UPARROW:
		menu_soundlevel = m_sound_nav;

		if ((--bind_cursor) < 0) bind_cursor = NUMBINDINGS - 1;

		break;

	case K_DOWNARROW:
		menu_soundlevel = m_sound_nav;

		if ((++bind_cursor) >= NUMBINDINGS) bind_cursor = 0;

		break;

	case K_ENTER:
		// go into bind mode
		menu_soundlevel = m_sound_query;
		keybind_grab = true;

		// if two keys are bound we delete the first binding
		if (keys[1] != -1) Menu_UnbindAction (key_bindnames[bind_cursor][0]);

		break;

	case K_BACKSPACE:
	case K_DEL:
		menu_soundlevel = m_sound_enter;

		if (keys[0] == -1)
		{
			// nothing to unbind
			menu_soundlevel = m_sound_deny;
		}
		else if (keys[1] == -1)
		{
			// unbind first key
			Key_SetBinding (keys[0], "");
		}
		else
		{
			// unbind second key
			Key_SetBinding (keys[1], "");
		}

		break;
	}
}

void Menu_VideoCustomEnter (void);

bool CheckKnownContent (char *mask)
{
	WIN32_FIND_DATA FindFileData;
	HANDLE hFind = INVALID_HANDLE_VALUE;

	hFind = FindFirstFile (mask, &FindFileData);

	if (hFind == INVALID_HANDLE_VALUE)
	{
		// found no files
		FindClose (hFind);
		return false;
	}

	// found something
	FindClose (hFind);
	return true;
}


bool IsGameDir (char *path)
{
	char *basedir = host_parms.basedir;

	// check for known files that indicate a gamedir
	if (CheckKnownContent (va ("%s/pak0.pak", path))) return true;
	if (CheckKnownContent (va ("%s/config.cfg", path))) return true;
	if (CheckKnownContent (va ("%s/autoexec.cfg", path))) return true;
	if (CheckKnownContent (va ("%s/progs.dat", path))) return true;
	if (CheckKnownContent (va ("%s/gfx.wad", path))) return true;

	// some gamedirs just have maps or models, or may have weirdly named paks
	if (CheckKnownContent (va ("%s/%s/maps/*.bsp", basedir, path))) return true;
	if (CheckKnownContent (va ("%s/%s/progs/*.mdl", basedir, path))) return true;
	if (CheckKnownContent (va ("%s/%s/*.pak", basedir, path))) return true;
	if (CheckKnownContent (va ("%s/%s/*.pk3", basedir, path))) return true;
	if (CheckKnownContent (va ("%s/%s/*.zip", basedir, path))) return true;

	// some gamedirs are just used for keeping stuff separate
	if (CheckKnownContent (va ("%s/%s/*.sav", basedir, path))) return true;
	if (CheckKnownContent (va ("%s/%s/*.dem", basedir, path))) return true;

	// nope
	return false;
}


void AddGameDirOption (char *dirname, cvar_t *togglecvar)
{
	if (!IsGameDir (dirname)) return;

	if (togglecvar)
		menu_Game.AddOption (new CQMenuCvarToggle (dirname, togglecvar, 0, 1));
	else menu_Game.AddOption (new CQMenuSpacer (dirname));
}

#define MAX_GAME_DIRS	2048
char *gamedirs[MAX_GAME_DIRS] = {NULL};
int gamedirnum = 0;


void Menu_GameCustomEnter (void)
{
	char *gamedir = com_gamedir;

	for (int i = strlen (com_gamedir); i; i--)
	{
		if (com_gamedir[i] == '/' || com_gamedir[i] == '\\')
		{
			gamedir = &com_gamedir[i + 1];
			break;
		}
	}

	// find the current game
	for (gamedirnum = 0;; gamedirnum++)
	{
		// no more gamedirs
		if (!gamedirs[gamedirnum])
		{
			// break out and reset to 0
			gamedirnum = 0;
			break;
		}

		// found it
		if (!stricmp (gamedir, gamedirs[gamedirnum])) break;
	}
}


int Menu_GameCustomDraw (int y)
{
	if (com_quoth.integer)
	{
		Menu_PrintCenterWhite (y + 20, "Warning");
		Menu_PrintCenter (y + 35, DIVIDER_LINE);
		Menu_PrintCenter (y + 50, "Enabling Quoth with a mod that is not");
		Menu_PrintCenter (y + 60, "designed for it can cause bugs and crashes.");
		Menu_PrintCenter (y + 80, "Only enable Quoth if you are certain");
		Menu_PrintCenter (y + 90, "that a mod requires it.");
	}

	return y;
}


extern bool WasInGameMenu;

void Menu_GameLoadGames (void)
{
	// drop out of the menus
	key_dest = key_game;
	WasInGameMenu = true;

	// fire the game change command
	Cbuf_InsertText (va ("game %s\n", gamedirs[gamedirnum]));
	Cbuf_Execute ();
}


void EnumGameDirs (void)
{
	int numgamedirs = 0;

	WIN32_FIND_DATA FindFileData;
	HANDLE hFind = INVALID_HANDLE_VALUE;

	// note - something BAD has happened if this fails....
	hFind = FindFirstFile ("*", &FindFileData);

	if (hFind == INVALID_HANDLE_VALUE)
	{
		// found no files
		FindClose (hFind);
		Sys_Error ("EnumGameDirs: Empty Quake Folder!");
		return;
	}

	do
	{
		// too many games
		if (numgamedirs == (MAX_GAME_DIRS - 1)) break;

		// not interested
		if (!(FindFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
		if (FindFileData.dwFileAttributes & FILE_ATTRIBUTE_COMPRESSED) continue;
		if (FindFileData.dwFileAttributes & FILE_ATTRIBUTE_ENCRYPTED) continue;
		if (FindFileData.dwFileAttributes & FILE_ATTRIBUTE_OFFLINE) continue;
		if (FindFileData.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) continue;

		// don't do these
		if (!strcmp (FindFileData.cFileName, ".")) continue;
		if (!strcmp (FindFileData.cFileName, "..")) continue;

		// ensure that it's a game dir
		if (!IsGameDir (FindFileData.cFileName)) continue;

		// store it out
		gamedirs[numgamedirs] = (char *) Zone_Alloc (strlen (FindFileData.cFileName) + 1);
		strcpy (gamedirs[numgamedirs], FindFileData.cFileName);

		numgamedirs++;
	} while (FindNextFile (hFind, &FindFileData));

	// close the finder
	FindClose (hFind);

	// terminate the list
	gamedirs[numgamedirs] = NULL;

	if (numgamedirs)
	{
		// spacer
		menu_Game.AddOption (new CQMenuSpacer ());

		// add them to the list
		menu_Game.AddOption (new CQMenuSpinControl ("Game Directory", &gamedirnum, gamedirs));
	}

	if (IsGameDir ("Hipnotic") || IsGameDir ("Rogue") || IsGameDir ("Quoth") || IsGameDir ("Nehahra") || numgamedirs)
	{
		menu_Game.AddOption (new CQMenuSpacer (DIVIDER_LINE));
		menu_Game.AddOption (new CQMenuCommand ("Load Selected Games and Add-Ons", Menu_GameLoadGames));
	}
	else
	{
		// nothing to load...
		// (this should never happen as id1 will always be there...)
		menu_Game.AddOption (new CQMenuSpacer ("No Quake Games Found!"));
		menu_Game.AddOption (new CQMenuSpacer ("Press ESC to Return to the Previous Menu"));
	}
}


int Menu_EffectsCustomDraw (int y)
{
	// sanity check frame interpolation cvar
	if (r_lerpframe.integer < 0) Cvar_Set (&r_lerpframe, (float) 0);

	if (r_lerpframe.integer > 1) Cvar_Set (&r_lerpframe, 1);

	// keep y
	return y;
}


char *ShotTypes[] = {"TGA", "BMP", "PNG", "DDS", "JPG", "PCX", NULL};
int shottype_number = 0;
int old_shottype_number = 0;

void Menu_ContentCustomEnter (void)
{
	// select the correct shot type
	for (int i = 0;; i++)
	{
		if (!ShotTypes[i]) break;

		if (!stricmp (scr_screenshotformat.string, ShotTypes[i]))
		{
			Cvar_Set (&scr_screenshotformat, ShotTypes[i]);
			old_shottype_number = shottype_number = i;
			return;
		}
	}

	// default to TGA if unsupported type
	Cvar_Set (&scr_screenshotformat, "TGA");
	old_shottype_number = shottype_number = 0;
}


int Menu_ContentCustomDraw (int y)
{
	if (old_shottype_number != shottype_number)
	{
		// store selected shot type back
		Cvar_Set (&scr_screenshotformat, ShotTypes[shottype_number]);
		old_shottype_number = shottype_number;
	}

	return y;
}


char *skywarpstyles[] = {"Simple", "Classic", NULL};

// keep these consistent with the loader
extern char *TextureExtensions[];
extern char *sbdir[];
extern char *suf[];
char **skybox_menulist = NULL;
int skybox_menunumber = 0;
int old_skybox_menunumber = 0;

#define TAG_SKYBOXAPPLY		1
#define TAG_WATERALPHA		4

void Menu_LoadAvailableSkyboxes (void)
{
	skybox_menulist = NULL;
	int listlen = 0;
	char **SkyboxList = NULL;

	for (int i = 0;; i++)
	{
		if (!sbdir[i]) break;

		for (int j = 0;; j++)
		{
			if (!TextureExtensions[j]) break;

			// because we allow skyboxes components to be missing (crazy modders!) we need to check all 6 suffixes
			for (int s = 0; s < 6; s++)
			{
				listlen = COM_BuildContentList (&SkyboxList, va ("%s/", sbdir[i]), va ("%s.%s", suf[s], TextureExtensions[j]));
			}
		}
	}

	// because we added each skybox component (to cover for crazy modders who leave one out) we now go
	// through the list and remove duplicates, copying it into a second list as we go.  this is what we're reduced to. :(
	// add 1 for NULL termination
	char **NewSkyboxList = (char **) Zone_Alloc ((listlen + 1) * sizeof (char *));
	NewSkyboxList[0] = NULL;
	int newboxes = 0;

	for (int i = 0; i < listlen; i++)
	{
		for (int j = strlen (SkyboxList[i]); j; j--)
		{
			if (SkyboxList[i][j] == '/') break;
			if (SkyboxList[i][j] == '\\') break;

			if (SkyboxList[i][j] == '.' && j > 2)
			{
				SkyboxList[i][j - 2] = 0;
				break;
			}
		}

		bool present = false;

		for (int j = 0;; j++)
		{
			if (!NewSkyboxList[j]) break;

			if (!stricmp (NewSkyboxList[j], SkyboxList[i]))
			{
				present = true;
				break;
			}
		}

		if (!present)
		{
			NewSkyboxList[newboxes] = (char *) Zone_Alloc (strlen (SkyboxList[i]) + 1);
			strcpy (NewSkyboxList[newboxes], SkyboxList[i]);
			newboxes++;
			NewSkyboxList[newboxes] = NULL;
		}

		Zone_Free (SkyboxList[i]);
	}

	listlen = newboxes;

	// alloc a buffer for the menu list (add 1 to null term the list)
	// and a second for the first item, which will be "no skybox"
	skybox_menulist = (char **) GameZone->Alloc ((listlen + 2) * sizeof (char *));

	// this is needed for cases where there are no skyboxes present
	skybox_menulist[1] = NULL;

	for (int i = 0; i < listlen; i++)
	{
		skybox_menulist[i + 1] = (char *) GameZone->Alloc (strlen (NewSkyboxList[i]) + 1);
		strcpy (skybox_menulist[i + 1], NewSkyboxList[i]);

		// it's the callers responsibility to free up the list generated by COM_BuildContentList
		Zone_Free (NewSkyboxList[i]);

		// always null term the next item to ensure that the menu list is null termed itself
		skybox_menulist[i + 2] = NULL;
	}

	Zone_Free (NewSkyboxList);

	// set up item 0 (ensure that it's name can't conflict with a valid name)
	skybox_menulist[0] = (char *) GameZone->Alloc (20);
	strcpy (skybox_menulist[0], "none/no skybox");

	// new menu
	skybox_menunumber = 0;
	old_skybox_menunumber = 0;
}


char *hudstylelist[] = {"Classic Status Bar", "Overlay Status Bar", "QuakeWorld HUD", "Quake 64 HUD", NULL};
int hudstyleselection = 0;

#define TAG_CONSCALE		64
#define TAG_SMOOTHCHAR		128
#define TAG_HUDALIGN		8192
#define TAG_HUDALPHA		16384

extern cvar_t r_smoothcharacters;
extern cvar_t gl_conscale;

char *hud_invshow[] = {"On", "Off", NULL};
int hud_invshownum = 0;

char *overbright_options[] = {"Off", "2 x Overbright", "4 x Overbright", NULL};
int overbright_num = 1;
extern cvar_t r_overbright;

char *waterwarp_options[] = {"Off", "Classic", "Perspective", NULL};
int waterwarp_num = 1;

char *particle_options[] = {"Dots", "Squares", NULL};
int particle_num = 0;

// the wording has been chosen to hint at the player that there is something more here...
char *flashblend_options[] = {"Lightmaps Only", "Coronas Only", "Lightmaps/Coronas", NULL};
int flashblend_num = 0;


int Menu_WarpCustomDraw (int y)
{
	if (skybox_menunumber != old_skybox_menunumber)
	{
		menu_WarpSurf.EnableMenuOptions (TAG_SKYBOXAPPLY);
		// this broke the enable/disable switch - it's now done at load time instead
		//old_skybox_menunumber = skybox_menunumber;
	}
	else menu_WarpSurf.DisableMenuOptions (TAG_SKYBOXAPPLY);

	// this is a little cleaner now...
	if (r_lockalpha.value)
	{
		menu_EffectsSimple.DisableMenuOptions (TAG_WATERALPHA);
		menu_WarpSurf.DisableMenuOptions (TAG_WATERALPHA);
	}
	else
	{
		menu_EffectsSimple.EnableMenuOptions (TAG_WATERALPHA);
		menu_WarpSurf.EnableMenuOptions (TAG_WATERALPHA);
	}

	Cvar_Set (&r_overbright, overbright_num);
	Cvar_Set (&r_waterwarp, waterwarp_num);
	Cvar_Set (&r_particlestyle, particle_num);

	if (flashblend_num == 0)
	{
		// lightmaps only
		Cvar_Set (&r_coronas, 0.0f);
		Cvar_Set (&gl_flashblend, 0.0f);
	}
	else if (flashblend_num == 1)
	{
		// coronas only
		Cvar_Set (&r_coronas, 0.0f);
		Cvar_Set (&gl_flashblend, 1.0f);
	}
	else
	{
		// lightmaps + coronas
		Cvar_Set (&r_coronas, 1.0f);
		Cvar_Set (&gl_flashblend, 0.0f);
	}

	// hud style
	Cvar_Get (hudstyle, "cl_sbar");
	Cvar_Set (hudstyle, hudstyleselection);

	switch (hudstyle->integer)
	{
	case 0:
		menu_UI.DisableMenuOptions (TAG_HUDALPHA);
		menu_UI.EnableMenuOptions (TAG_HUDALIGN);
		break;

	case 1:
		menu_UI.EnableMenuOptions (TAG_HUDALPHA);
		menu_UI.EnableMenuOptions (TAG_HUDALIGN);
		break;

	default:
		menu_UI.DisableMenuOptions (TAG_HUDALPHA);
		menu_UI.DisableMenuOptions (TAG_HUDALIGN);
		break;
	}

	if (hud_invshownum)
		Cvar_Set (&scr_viewsize, 110.0f);
	else Cvar_Set (&scr_viewsize, 100.0f);

	if (d3d_CurrentMode.Width > 640 && d3d_CurrentMode.Height > 480)
	{
		menu_UI.EnableMenuOptions (TAG_CONSCALE);

		if (gl_conscale.value < 1)
			menu_UI.EnableMenuOptions (TAG_SMOOTHCHAR);
		else menu_UI.DisableMenuOptions (TAG_SMOOTHCHAR);
	}
	else
	{
		menu_UI.DisableMenuOptions (TAG_SMOOTHCHAR);
		menu_UI.DisableMenuOptions (TAG_CONSCALE);
	}

	// keep y
	return y;
}


#define GETCVAROPTION(var, num, minnum, maxnum) \
	(num) = (var); \
	if ((num) < (minnum)) (num) = (minnum); \
	if ((num) > (maxnum)) (num) = (maxnum); \

void Menu_WarpCustomEnter (void)
{
	if (scr_viewsize.integer > 100)
		hud_invshownum = 1;
	else hud_invshownum = 0;

	Cvar_Get (hudstyle, "cl_sbar");

	skybox_menunumber = 0;
	old_skybox_menunumber = 0;

	GETCVAROPTION (hudstyle->integer, hudstyleselection, 0, 3);
	GETCVAROPTION (r_overbright.integer, overbright_num, 0, 2);
	GETCVAROPTION (r_waterwarp.integer, waterwarp_num, 0, 2);
	GETCVAROPTION (r_particlestyle.integer, particle_num, 0, 2);

	// set correct flashblend mode
	if (gl_flashblend.integer)
		flashblend_num = 1;
	else if (r_coronas.value)
		flashblend_num = 2;
	else flashblend_num = 0;

	extern char CachedSkyBoxName[];

	// find the current skybox
	for (int i = 0;; i++)
	{
		if (!skybox_menulist[i]) break;

		if (!stricmp (CachedSkyBoxName, skybox_menulist[i]))
		{
			// set up the menu to display it
			skybox_menunumber = i;
			old_skybox_menunumber = i;
			break;
		}
	}
}


void D3DSky_LoadSkyBox (char *basename, bool feedback);

void Menu_WarpSkyBoxApply (void)
{
	if (skybox_menunumber)
		D3DSky_LoadSkyBox (skybox_menulist[skybox_menunumber], true);
	else
	{
		// item 0 is "no skybox"
		D3DSky_LoadSkyBox (skybox_menulist[skybox_menunumber], false);
		Con_Printf ("Skybox Unloaded\n");
	}

	old_skybox_menunumber = skybox_menunumber;
}


int Menu_OptionsCustomDraw (int y)
{
	// save out menu highlight colour
	Cvar_Set (&menu_fillcolor, menu_fillcolor.integer);

	// toggle simplemenus option
	Menu_ToggleSimpleMenus ();

	return y;
}


char *fogenablelist[] = {"Off", "On", NULL};
char *fogqualitylist[] = {"Per-Vertex", "Per-Pixel", NULL};
char *fogmodelist[] = {"Linear", "Exp", "Exp2", NULL};
int fogenable = 0;
int fogquality = 0;
int fogmode = 0;
#define TAG_FOGDISABLED	1
#define TAG_LINEARONLY	2
#define TAG_EXPONLY		4

void Menu_FogCustomEnter (void)
{
}


int Menu_FogCustomDraw (int y)
{
	return y;
}


int Menu_FogColourBox (int y)
{
	return y;
}


char *soundspeedlist[] = {"11025", "22050", "44100", "48000", NULL};
int soundspeednum = 0;

#define TAG_SOUNDDISABLED 1024

void Menu_SoundCustomEnter (void)
{
	menu_Sound.DisableMenuOptions (TAG_SOUNDDISABLED);

	/*
	switch (s_khz.integer)
	{
	case 48:
	case 48000:
		soundspeednum = 3;
		break;

	case 44:
	case 44100:
		soundspeednum = 2;
		break;

	case 22:
	case 22100:
		soundspeednum = 1;
		break;

	default:
		soundspeednum = 0;
		break;
	}
	*/
}


int Menu_SoundCustomDraw (int y)
{
	/*
	switch (soundspeednum)
	{
	case 3:
		Cvar_Set (&s_khz, 48);
		break;

	case 2:
		Cvar_Set (&s_khz, 44);
		break;

	case 1:
		Cvar_Set (&s_khz, 22);
		break;

	default:
		Cvar_Set (&s_khz, 11);
		break;
	}
	*/

	return y;
}


cvar_t dummy_speed;
int dummy_speed_backup = 0;

void Menu_SpeedEnterCheck (void)
{
	if (cl_forwardspeed.value > 200)
		Cvar_Set (&dummy_speed, 1);
	else Cvar_Set (&dummy_speed, 0.0f);

	dummy_speed_backup = dummy_speed.integer;
}


int Menu_SpeedDrawCheck (int y)
{
	// only change the cvars if it actually changes
	if (dummy_speed_backup != dummy_speed.integer)
	{
		if (dummy_speed.integer)
		{
			// only reset these if they are less than the base speed, otherwise keep the higher value
			if (cl_forwardspeed.value < 400) Cvar_Set (&cl_forwardspeed, 400);
			if (cl_backspeed.value < 400) Cvar_Set (&cl_backspeed, 400);
		}
		else
		{
			// only reset these if they are higher than the base speed, otherwise keep the lower value
			if (cl_forwardspeed.value > 200) Cvar_Set (&cl_forwardspeed, 200);
			if (cl_backspeed.value > 200) Cvar_Set (&cl_backspeed, 200);
		}

		dummy_speed_backup = dummy_speed.integer;
	}

	return y;
}


extern cvar_t host_maxfps;
extern cvar_t scr_fov;
extern cvar_t scr_fovcompat;

void Menu_InitOptionsMenu (void)
{
	extern qpic_t *gfx_ttl_cstm_lmp;
	extern qpic_t *gfx_vidmodes_lmp;

	// options
	menu_Options.AddOption (new CQMenuCustomDraw (Menu_OptionsCustomDraw));
	menu_Options.AddOption (new CQMenuCustomEnter (Menu_SpeedEnterCheck));
	menu_Options.AddOption (new CQMenuCustomDraw (Menu_SpeedDrawCheck));
	menu_Options.AddOption (new CQMenuBanner (&gfx_p_option_lmp));
	menu_Options.AddOption (new CQMenuTitle ("Configure Game Options"));
	menu_Options.AddOption (new CQMenuSubMenu ("Customize Controls", &menu_Keybindings));
	// if you wanted to go to the console, you'd go to the console, not the menu...
	// (unless you messed up your keybindings, that is...)
	menu_Options.AddOption (new CQMenuCommand ("Go to Console", Menu_OptionsGoToConsole));
	menu_Options.AddOption (new CQMenuCommand ("Reset to Defaults", Menu_OptionsResetToDefaults));
	menu_Options.AddOption (new CQMenuCommand ("Save Current Configuration", Host_WriteConfiguration));
	menu_Options.AddOption (new CQMenuSpacer (DIVIDER_LINE));
	menu_Options.AddOption (new CQMenuSubMenu ("Change Game Directory", &menu_Game));
	menu_Options.AddOption (new CQMenuSubMenu ("Load a Map", &menu_Maps));
	menu_Options.AddOption (new CQMenuSubMenu ("Run or Record a Demo", &menu_Demo));
	menu_Options.AddOption (new CQMenuSpacer (DIVIDER_LINE));
	menu_Options.AddOption (new CQMenuCvarSlider ("Brightness", &v_gamma, 1.75, 0.25, 0.05));
	menu_Options.AddOption (new CQMenuCvarSlider ("Contrast", &vid_contrast, 0.25f, 1.75f, 0.05f));
	menu_Options.AddOption (new CQMenuCvarSlider ("Music Volume", &bgmvolume, 0, 1, 0.05));
	menu_Options.AddOption (new CQMenuCvarSlider ("Sound Volume", &volume, 0, 1, 0.05));
	menu_Options.AddOption (new CQMenuSpacer (DIVIDER_LINE));
	menu_Options.AddOption (new CQMenuCvarSlider ("Mouse Speed", &sensitivity, 1, 21, 1));
	menu_Options.AddOption (new CQMenuCvarToggle ("Mouse Look", &freelook, 0, 1));
	menu_Options.AddOption (new CQMenuCvarToggle ("Invert Mouse", &m_pitch, 0.022, -0.022));
	menu_Options.AddOption (new CQMenuCvarToggle ("Always Run", &dummy_speed, 0, 1));
	menu_Options.AddOption (new CQMenuSpacer (DIVIDER_LINE));
	menu_Options.AddOption (new CQMenuSubMenu ("User Interface Options", &menu_UI));
	menu_Options.AddOption (new CQMenuSubMenu ("Video Options", &menu_Video));
#ifdef _DEBUG
	menu_Options.AddOption (new CQMenuSubMenu ("Sound Options", &menu_Sound));
#endif
	menu_Options.AddOption (new CQMenuSubMenu ("Effects and Other Options", &menu_EffectsSimple));

	menu_UI.AddOption (new CQMenuCustomDraw (Menu_OptionsCustomDraw));
	menu_UI.AddOption (new CQMenuCustomEnter (Menu_WarpCustomEnter));
	menu_UI.AddOption (new CQMenuCustomDraw (Menu_WarpCustomDraw));
	menu_UI.AddOption (new CQMenuBanner (&gfx_p_option_lmp));
	menu_UI.AddOption (new CQMenuTitle ("User Interface Options"));
	menu_UI.AddOption (new CQMenuColourBar ("Menu Highlight", &menu_fillcolor.integer));
	menu_UI.AddOption (TAG_CONSCALE, new CQMenuCvarSlider ("Console Size", &gl_conscale, 1, 0, 0.1));
	menu_UI.AddOption (TAG_SMOOTHCHAR, new CQMenuCvarToggle ("Smooth Characters", &r_smoothcharacters, 0, 1));
	menu_UI.AddOption (new CQMenuSpacer (DIVIDER_LINE));
	menu_UI.AddOption (new CQMenuCvarSlider ("Field of View", &scr_fov, 10, 170, 5));
	menu_UI.AddOption (new CQMenuCvarToggle ("Compatible FOV", &scr_fovcompat, 0, 1));
	menu_UI.AddOption (new CQMenuTitle ("Heads-Up Display"));
	menu_UI.AddOption (new CQMenuSpinControl ("HUD Style", &hudstyleselection, hudstylelist));
	menu_UI.AddOption (new CQMenuSpinControl ("Show Inventory", &hud_invshownum, hud_invshow));
	menu_UI.AddOption (TAG_HUDALIGN, new CQMenuCvarToggle ("Center-align HUD", &scr_centersbar));
	menu_UI.AddOption (TAG_HUDALPHA, new CQMenuCvarSlider ("HUD Alpha", &scr_sbaralpha, 0, 1, 0.1));

	// sound
	menu_Sound.AddOption (new CQMenuCustomEnter (Menu_SoundCustomEnter));
	menu_Sound.AddOption (new CQMenuCustomDraw (Menu_SoundCustomDraw));
	menu_Sound.AddOption (new CQMenuBanner (&gfx_p_option_lmp));
	menu_Sound.AddOption (new CQMenuTitle ("Sound Options"));
	menu_Sound.AddOption (new CQMenuCvarSlider ("Music Volume", &bgmvolume, 0, 1, 0.05));
	menu_Sound.AddOption (new CQMenuCvarSlider ("Sound Volume", &volume, 0, 1, 0.05));
	menu_Sound.AddOption (new CQMenuSpacer (DIVIDER_LINE));
	menu_Sound.AddOption (new CQMenuCvarSlider ("Clip Distance", &sound_nominal_clip_dist, 500, 2000, 100));
	menu_Sound.AddOption (new CQMenuCvarSlider ("Ambient Level", &ambient_level, 0, 1, 0.05));
	menu_Sound.AddOption (new CQMenuCvarSlider ("Ambient Fade", &ambient_fade, 50, 200, 10));
	//menu_Sound.AddOption (new CQMenuSpacer (DIVIDER_LINE));
	//menu_Sound.AddOption (new CQMenuSpacer ("(Disabled Options)"));
	//menu_Sound.AddOption (TAG_SOUNDDISABLED, new CQMenuSpinControl ("Sound Speed", &soundspeednum, soundspeedlist));
	//menu_Sound.AddOption (TAG_SOUNDDISABLED, new CQMenuCvarToggle ("8-Bit Sounds", &loadas8bit));

	Cvar_Get (hudstyle, "cl_sbar");

	menu_EffectsSimple.AddOption (new CQMenuCustomEnter (Menu_WarpCustomEnter));
	menu_EffectsSimple.AddOption (new CQMenuCustomDraw (Menu_WarpCustomDraw));
	menu_EffectsSimple.AddOption (new CQMenuBanner (&gfx_p_option_lmp));
	menu_EffectsSimple.AddOption (new CQMenuTitle ("Interpolation"));
	menu_EffectsSimple.AddOption (new CQMenuCvarToggle ("Orientation", &r_lerporient, 0, 1));
	menu_EffectsSimple.AddOption (new CQMenuCvarToggle ("Frame", &r_lerpframe, 0, 1));
	menu_EffectsSimple.AddOption (new CQMenuCvarToggle ("Light Style", &r_lerplightstyle, 0, 1));
	menu_EffectsSimple.AddOption (new CQMenuTitle ("Lighting"));
	menu_EffectsSimple.AddOption (new CQMenuSpinControl ("Overbright Light", &overbright_num, overbright_options));
	menu_EffectsSimple.AddOption (new CQMenuCvarToggle ("Fullbrights", &gl_fullbrights, 0, 1));
	menu_EffectsSimple.AddOption (new CQMenuCvarToggle ("Extra Dynamic Light", &r_extradlight, 0, 1));
	menu_EffectsSimple.AddOption (new CQMenuSpinControl ("Dynamic Light Style", &flashblend_num, flashblend_options));
	menu_EffectsSimple.AddOption (new CQMenuCvarSlider ("MDL Light Scale", &r_aliaslightscale, 0, 5, 0.1));
	menu_EffectsSimple.AddOption (new CQMenuCvarSlider ("Lightmap Gamma", &lm_gamma, 1.75, 0.25, 0.05));
	menu_EffectsSimple.AddOption (new CQMenuTitle ("Particles"));
	menu_EffectsSimple.AddOption (new CQMenuSpinControl ("Particle Style", &particle_num, particle_options));
	menu_EffectsSimple.AddOption (new CQMenuCvarSlider ("Particle Size", &r_particlesize, 0.5, 10, 0.5));
	menu_EffectsSimple.AddOption (new CQMenuTitle ("Water and Liquids"));
	menu_EffectsSimple.AddOption (new CQMenuCvarSlider ("Water Alpha", &r_wateralpha, 0, 1, 0.1));
	menu_EffectsSimple.AddOption (new CQMenuSpinControl ("Underwater Warp", &waterwarp_num, waterwarp_options));
	menu_EffectsSimple.AddOption (new CQMenuCvarToggle ("Correct Color Shift", &r_truecontentscolour, 0, 1));

	// keybindings
	menu_Keybindings.AddOption (new CQMenuBanner (&gfx_ttl_cstm_lmp));
	menu_Keybindings.AddOption (new CQMenuCustomDraw (Menu_KeybindingsCustomDraw));

	// grab every key for these actions
	for (int i = 0; i < 256; i++)
		menu_Keybindings.AddOption (new CQMenuCustomKey (i, Menu_KeybindingsCustomKey));

	// video menu - the rest of the options are deferred until the menu is up!
	// note - the new char *** spinbox style means this isn't actually required any more,
	// but this style hasn't been let out into the wild yet, so it may have some subtle bugs i'm unaware of
	// see Menu_VideoCustomEnter in d3d_vidnt.cpp
	menu_Video.AddOption (new CQMenuBanner (&gfx_vidmodes_lmp));
	menu_Video.AddOption (new CQMenuTitle ("Configure Video Modes"));
	menu_Video.AddOption (new CQMenuCustomEnter (Menu_VideoCustomEnter));

	// gamedir menu
	menu_Game.AddOption (new CQMenuBanner (&gfx_p_option_lmp));

	// add in add-ons
	if (IsGameDir ("Hipnotic") || IsGameDir ("Rogue") || IsGameDir ("Quoth") || IsGameDir ("Nehahra"))
	{
		menu_Game.AddOption (new CQMenuTitle ("Select Game Add-Ons"));

		AddGameDirOption ("Hipnotic", &com_hipnotic);
		AddGameDirOption ("Rogue", &com_rogue);
		AddGameDirOption ("Nehahra", &com_nehahra);
		AddGameDirOption ("Quoth", &com_quoth);
	}

	EnumGameDirs ();
	menu_Game.AddOption (new CQMenuCustomEnter (Menu_GameCustomEnter));
	menu_Game.AddOption (new CQMenuCustomDraw (Menu_GameCustomDraw));
}


/*
========================================================================================================================

					HELP MENU

========================================================================================================================
*/

int menu_HelpPage = 0;
bool nehdemo = false;

void Menu_HelpCustomEnter (void)
{
	if (nehahra && !menu_advanced.integer)
	{
		nehdemo = false;
		FILE *f = fopen (va ("%s/endcred.dem", com_gamedir), "rb");

		if (f)
		{
			nehdemo = true;
			fclose (f);
			key_dest = key_game;

			// endcred.dem is not actually present in the nehahra download!
			if (sv.active)
				Cbuf_AddText ("disconnect\n");

			Cbuf_AddText ("playdemo endcred\n");

			return;
		}
	}
}


int Menu_HelpCustomDraw (int y)
{
	if (nehahra && !menu_advanced.integer && !nehdemo)
	{
		Menu_PrintCenterWhite (vid.currsize->height / 2 - 32, "Nehahra Credits Unavailable");
		return y;
	}
	else
	{
		extern qpic_t *menu_help_lmp[];

		Draw_Pic
		(
			(vid.currsize->width - menu_help_lmp[menu_HelpPage]->width) >> 1,
			y,
			menu_help_lmp[menu_HelpPage]
		);

		return y + menu_help_lmp[menu_HelpPage]->height + 10;
	}
}


void Menu_HelpCustomKey (int key)
{
	menu_soundlevel = m_sound_option;

	switch (key)
	{
	case K_UPARROW:
	case K_RIGHTARROW:
		if (++menu_HelpPage >= NUM_HELP_PAGES)
			menu_HelpPage = 0;

		break;

	case K_DOWNARROW:
	case K_LEFTARROW:
		if (--menu_HelpPage < 0)
			menu_HelpPage = NUM_HELP_PAGES - 1;

		break;
	}
}


void Menu_InitHelpMenu (void)
{
	menu_Help.AddOption (new CQMenuCustomEnter (Menu_HelpCustomEnter));
	menu_Help.AddOption (new CQMenuCustomDraw (Menu_HelpCustomDraw));
	menu_Help.AddOption (new CQMenuCustomKey (K_LEFTARROW, Menu_HelpCustomKey));
	menu_Help.AddOption (new CQMenuCustomKey (K_UPARROW, Menu_HelpCustomKey));
	menu_Help.AddOption (new CQMenuCustomKey (K_DOWNARROW, Menu_HelpCustomKey));
	menu_Help.AddOption (new CQMenuCustomKey (K_RIGHTARROW, Menu_HelpCustomKey));
}

int Menu_ControllerCustomDraw (int y)
{
	return y + 15;
}


void Menu_RescanControllers (void)
{
}


void Menu_InitControllerMenu (void)
{
}


typedef struct mapinfo_s
{
	bool loaded;
	char *bspname;
	char *mapname;
	char *skybox;
	char *wad;
	int ambience;
	int cdtrack;
} mapinfo_t;

mapinfo_t *menu_mapslist = NULL;
int num_menumaps = 0;

// this is used to provide access to the maps list in a spinbox for the multiplayer menu...
char **spinbox_maps = NULL;
char **spinbox_bsps = NULL;

void Menu_MapsCacheInfo (mapinfo_t *info, char *entlump)
{
	// initialize
	info->ambience = 0;
	info->cdtrack = 0;
	info->mapname = NULL;
	info->skybox = NULL;
	info->wad = NULL;

	// get a pointer to the entities lump
	char *data = entlump;
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
			Q_strncpy (key, &com_token[1], 39);
		else Q_strncpy (key, com_token, 39);

		// remove trailing spaces
		while (key[strlen (key) - 1] == ' ') key[strlen (key) - 1] = 0;

		// parse the value
		data = COM_Parse (data);

		// likewise should never happen (has already been successfully parsed server-side and any errors that
		// were going to happen would have happened then; we only check to guard against pointer badness)
		if (!data) return;

		// check the key for info we wanna cache - value is stored in com_token
		if (!stricmp (key, "message"))
		{
			info->mapname = (char *) GameZone->Alloc (strlen (com_token) + 1);
			strcpy (info->mapname, com_token);

			// evil maps with looooonnggg messages
			for (int i = 0;; i++)
			{
				if (!info->mapname[i]) break;

				if (i > 0 && info->mapname[i - 1] == '\\' && info->mapname[i] == 'n')
				{
					info->mapname[i - 1] = 0;
					break;
				}

				if (i == 39)
				{
					info->mapname[i] = 0;
					break;
				}
			}
		}
		else if (!stricmp (key, "sounds"))
		{
			info->cdtrack = atoi (com_token);
		}
		else if (!stricmp (key, "worldtype"))
		{
			info->ambience = atoi (com_token);
		}
		else if (!stricmp (key, "sky") || !stricmp (key, "skyname") || !stricmp (key, "q1sky") || !stricmp (key, "skybox"))
		{
			info->skybox = (char *) GameZone->Alloc (strlen (com_token) + 1);
			strcpy (info->skybox, com_token);
		}
		else if (!stricmp (key, "wad"))
		{
			info->wad = (char *) GameZone->Alloc (strlen (com_token) + 1);
			strcpy (info->wad, com_token);
		}
	}
}


void Menu_MapsOnDraw (int y, int itemnum)
{
	Menu_Print (-8, y, menu_mapslist[itemnum].bspname);
}


void Draw_Mapshot (char *name, int x, int y);

// split out as it's now also used for the demo menu
void Menu_DoMapInfo (int x, int y, int itemnum, bool showcdtrack = true)
{
	// map info - don't bother with wad (as it's a full file path, it can be mondo-long)
	if (menu_mapslist[itemnum].ambience == 0)
		Menu_PrintCenter (x, y + 10, "Ambience: Medieval");
	else if (menu_mapslist[itemnum].ambience == 1)
		Menu_PrintCenter (x, y + 10, "Ambience: Runic/Metal");
	else if (menu_mapslist[itemnum].ambience == 2)
		Menu_PrintCenter (x, y + 10, "Ambience: Base");
	else Menu_PrintCenter (x, y + 10, "Ambience: Medieval");

	int boxadd = 30;

	if (showcdtrack)
	{
		if (menu_mapslist[itemnum].cdtrack)
			Menu_PrintCenter (x, y + 20, va ("CD Track: %i", menu_mapslist[itemnum].cdtrack));
		else Menu_PrintCenter (x, y + 20, "CD Track: (not set)");
	}
	else boxadd = 20;

	if (menu_mapslist[itemnum].skybox)
		Menu_PrintCenter (x, y + boxadd, va ("Skybox: %s", menu_mapslist[itemnum].skybox));
	else Menu_PrintCenter (x, y + boxadd, "Skybox: (not set)");
}


void Menu_MapsOnHover (int initialy, int y, int itemnum)
{
	// highlight bar
	Menu_HighlightBar (-174, y, 172);

	// map info
	if (menu_mapslist[itemnum].mapname)
		Menu_PrintCenterWhite (272, initialy + 20, menu_mapslist[itemnum].mapname);
	else Menu_PrintCenterWhite (272, initialy, "(unknown)");

	Menu_PrintCenter (272, initialy + 35, DIVIDER_LINE);
	Draw_Mapshot (va ("maps/%s", menu_mapslist[itemnum].bspname), (vid.currsize->width - 320) / 2 + 208, initialy + 55);
	Menu_PrintCenter (272, initialy + 195, DIVIDER_LINE);
	Menu_DoMapInfo (272, initialy + 200, itemnum);
}


void Menu_MapsOnEnter (int itemnum)
{
	// launch the map command
	Cbuf_InsertText (va ("map %s\n", menu_mapslist[itemnum].bspname));
	Cbuf_Execute ();
}


bool ValidateMap (char *mapname, int itemnum)
{
	HANDLE fh = INVALID_HANDLE_VALUE;
	char fullmapname[MAX_PATH];

	_snprintf (fullmapname, 260, "maps/%s", mapname);

	COM_FOpenFile (fullmapname, &fh);

	if (fh == INVALID_HANDLE_VALUE) return false;

	// because the map could be coming from a PAK file we need to save out the base pos so that fseek is valid
	int basepos = SetFilePointer (fh, 0, NULL, FILE_CURRENT);
	dheader_t bsphead;

	int rlen = COM_FReadFile (fh, &bsphead, sizeof (dheader_t));

	if (rlen != sizeof (dheader_t))
	{
		COM_FCloseFile (&fh);
		return false;
	}

	if (bsphead.version != PR_BSPVERSION && bsphead.version != Q1_BSPVERSION)
	{
		// don't add maps with a bad version number to the list
		COM_FCloseFile (&fh);
		return false;
	}

	// find the entities lump
	SetFilePointer (fh, basepos + bsphead.lumps[LUMP_ENTITIES].fileofs, NULL, FILE_BEGIN);

	// read it all in
	char *entlump = (char *) Zone_Alloc (bsphead.lumps[LUMP_ENTITIES].filelen);
	rlen = COM_FReadFile (fh, entlump, bsphead.lumps[LUMP_ENTITIES].filelen);
	COM_FCloseFile (&fh);

	// not enough data
	if (rlen != bsphead.lumps[LUMP_ENTITIES].filelen)
	{
		Zone_Free (entlump);
		return false;
	}

	// true if the map is valid
	bool validmap = false;

	for (int i = 0; i < (bsphead.lumps[LUMP_ENTITIES].filelen - 11); i++)
	{
		// this check is potentially suspect as the entity could be called anything;
		// info_player_* is just an informal convention...!
		if (!strnicmp (&entlump[i], "info_player", 11))
		{
			// map is valid
			menu_mapslist[itemnum].bspname = (char *) GameZone->Alloc (strlen (mapname) + 1);
			strcpy (menu_mapslist[itemnum].bspname, mapname);

			// rip out the extension (so that the "map" command will work with it)
			for (int j = strlen (menu_mapslist[itemnum].bspname); j; j--)
			{
				if (menu_mapslist[itemnum].bspname[j] == '.')
				{
					menu_mapslist[itemnum].bspname[j] = 0;
					break;
				}
			}

			// one is all we need
			Menu_MapsCacheInfo (&menu_mapslist[itemnum], entlump);
			validmap = true;
			break;
		}
	}

	// found or not found
	Zone_Free (entlump);
	return validmap;
}


void Menu_MapsPopulate (void)
{
	// clear down previous map list
	menu_mapslist = NULL;

	char **MapList = NULL;
	int listlen = COM_BuildContentList (&MapList, "maps/", ".bsp");

	// set up the array for use
	int maplistlen = 0;
	menu_mapslist = (mapinfo_t *) GameZone->Alloc (listlen * sizeof (mapinfo_t));

	// fill it in
	for (int i = 0; i < listlen; i++)
	{
		// if the map doesn't have an info_player entity in it, don't add it to the list
		if (ValidateMap (MapList[i], maplistlen))
		{
			menu_mapslist[maplistlen].loaded = false;
			maplistlen++;
		}

		// clear down the source item as we don't need it any more
		Zone_Free (MapList[i]);
	}

	if (!maplistlen)
	{
		// OK, we had a crash with no demos installed, so let's also catch no maps installed
		Sys_Error ("Menu_MapsPopulate: No Maps Installed!\n(Are you crazy?)");
		return;
	}

	// need to store this out for freeing above
	num_menumaps = maplistlen;

	// now we also set up a spinbox-compatible list
	// add 1 for the NULL terminator
	spinbox_maps = (char **) GameZone->Alloc ((num_menumaps + 1) * sizeof (char *));
	spinbox_bsps = (char **) GameZone->Alloc ((num_menumaps + 1) * sizeof (char *));

	for (int i = 0; i < num_menumaps; i++)
	{
		// skip if the bsp name is unavailable
		if (!menu_mapslist[i].bspname[0]) continue;

		if (menu_mapslist[i].mapname && menu_mapslist[i].mapname[0])
		{
			// just copy in the pointers rather than allocating new memory.
			// also set the following item to NULL so that the list is always NULL termed
			spinbox_maps[i] = menu_mapslist[i].mapname;
			spinbox_maps[i + 1] = NULL;
		}
		else
		{
			// just copy in the pointers rather than allocating new memory.
			// also set the following item to NULL so that the list is always NULL termed
			// if the map name is unavailable we use the bsp name instead
			spinbox_maps[i] = menu_mapslist[i].bspname;
			spinbox_maps[i + 1] = NULL;
		}

		// we need map names as well as bsp names here...
		spinbox_bsps[i] = menu_mapslist[i].bspname;
		spinbox_bsps[i + 1] = NULL;
	}
}


int Menu_MapsCustomDraw (int y)
{
	return y;
}


void Menu_MapsCustomKey (int k)
{
}


char *democmds[] = {"playdemo", "timedemo", "record", NULL};
int democmd_num = 0;
char **demolist = NULL;
int demonum = 0;
int old_demonum = -1;
cvar_t dummy_demoname;
cvar_t dummy_cdtrack;
int demo_mapnum = 0;
int demo_skillnum = 0;

void Menu_DemoPopulate (void)
{
	// because there might be no demos we should NULL the list too
	demolist = NULL;

	char **alldemos = NULL;
	int numdemos = COM_BuildContentList (&alldemos, "", ".dem");
	demolist = (char **) GameZone->Alloc ((numdemos + 1) * sizeof (char *));

	if (numdemos)
	{
		for (int i = 0; i < numdemos; i++)
		{
			int thisdemolen = strlen (alldemos[i]);

			demolist[i] = (char *) GameZone->Alloc (thisdemolen + 1);
			strcpy (demolist[i], alldemos[i]);

			for (int j = thisdemolen; j; j--)
			{
				if (demolist[i][j] == '.')
				{
					demolist[i][j] = 0;
					break;
				}
			}

			demolist[i + 1] = NULL;
			Zone_Free (alldemos[i]);
		}
	}
	else demolist[0] = NULL;

	// set defaults
	demonum = 0;
	old_demonum = -1;
	demo_mapnum = 0;
	demo_skillnum = 1;
	Cvar_Set (&dummy_cdtrack, 2);
}


#define TAG_PLAYTIME	1
#define TAG_RECORD		2
#define TAG_RECORDCMD	4
#define TAG_PLAYONLY	8
#define TAG_TIMEONLY	16
#define TAG_SELECTMODE	32

void Menu_DemoCustomEnter (void)
{
	// fix up spin controls in case the game has changed and we now have an invalid selection
	Menu_FixupSpin (&demo_mapnum, spinbox_maps);
}


int Menu_DemoCustomDraw1 (int y)
{
	// go direct to record mode if there are no demos
	if (!demolist[0])
	{
		// also disable the mode selection so that we don't inadvertently trigger invalid modes
		menu_Demo.DisableMenuOptions (TAG_SELECTMODE);
		democmd_num = 2;
	}
	else menu_Demo.EnableMenuOptions (TAG_SELECTMODE);

	// fix up control visibility
	if (democmd_num == 2)
	{
		if (dummy_demoname.string[0])
			menu_Demo.EnableMenuOptions (TAG_RECORDCMD);
		else menu_Demo.DisableMenuOptions (TAG_RECORDCMD);

		menu_Demo.HideMenuOptions (TAG_PLAYONLY);
		menu_Demo.HideMenuOptions (TAG_TIMEONLY);
		menu_Demo.HideMenuOptions (TAG_PLAYTIME);
		menu_Demo.ShowMenuOptions (TAG_RECORD);
		menu_Demo.ShowMenuOptions (TAG_RECORDCMD);
	}
	else
	{
		if (democmd_num == 0)
		{
			menu_Demo.HideMenuOptions (TAG_TIMEONLY);
			menu_Demo.ShowMenuOptions (TAG_PLAYONLY);
		}
		else
		{
			menu_Demo.ShowMenuOptions (TAG_TIMEONLY);
			menu_Demo.HideMenuOptions (TAG_PLAYONLY);
		}

		menu_Demo.HideMenuOptions (TAG_RECORD);
		menu_Demo.HideMenuOptions (TAG_RECORDCMD);
		menu_Demo.ShowMenuOptions (TAG_PLAYTIME);
	}

	// y doesn't change
	return y;
}


typedef struct demo_serverinfo_s
{
	int proto_version;
	byte maxclients;
	byte gametype;
	char stuff[256];

	// these need to go at the end of the list as the rest above is read in that order
	// into the struct via an fread...
	char *levelname;
	char *levelname2;
	char *mapname;
	int cdtrack;
} demo_serverinfo_t;

demo_serverinfo_t dsi = {15, 1, 0, "", NULL, NULL, NULL, 0};

bool M_Menu_Demo_Info (char *demofile)
{
	HANDLE fh = INVALID_HANDLE_VALUE;
	int msgsize;
	float viewangs[3];

	// try opening on the root
	COM_FOpenFile (va ("%s.dem", demofile), &fh);

	// try opening in /demos
	if (fh == INVALID_HANDLE_VALUE)
		COM_FOpenFile (va ("demos/%s.dem", demofile), &fh);

	// we expect this to always work
	if (fh != INVALID_HANDLE_VALUE)
	{
		bool neg = false;
		dsi.cdtrack = 0;

		// read until we get a \n
		while (1)
		{
			char cmsg;

			int rlen = COM_FReadFile (fh, &cmsg, 1);

			if (cmsg == '\n') break;

			if (cmsg == '-')
				neg = true;
			else dsi.cdtrack = dsi.cdtrack * 10 + (cmsg - '0');
		}

		if (neg) dsi.cdtrack = -dsi.cdtrack;

		// now start parsing.  we expect this to work on most engines - so far it's been tested on TyrQuake, ProQuake, GLQuake and MHQuake demos.
		// we never know when an engine might try something fancy though... so we play safe and just fail...
		while (1)
		{
			// get the size of the message
			int rlen = COM_FReadFile (fh, &msgsize, 4);

			// read viewangles
			rlen = COM_FReadFile (fh, viewangs, sizeof (float) * 3);

			// read in the message
			byte *msgdata = (byte *) Zone_Alloc (msgsize);
			rlen = COM_FReadFile (fh, msgdata, msgsize);

			// parse the message
			for (int msgpos = 0;;)
			{
				// parse out the message id
				int msg = msgdata[msgpos++];

				// handle the message
				switch (msg)
				{
				case 0:
				case svc_nop:
					// just skip
					break;

				case svc_setpause:
					msgpos++;
					break;

				case svc_print:
					while (1)
					{
						int c = msgdata[msgpos++];

						if (c == 0 || c == -1) break;
					}

					break;

				case svc_setangle:
					// just get 3 bytes
					// (this is going to be different for different protocols)
					msgpos += 3;
					break;

				case svc_serverinfo:
					// copy out the serverinfo
					// the cd track will be stomped on by this fread, so we need to save it out then restore it
				{
					int savedcdtrack = dsi.cdtrack;
					memcpy (&dsi, &msgdata[msgpos], sizeof (demo_serverinfo_t));
					dsi.cdtrack = savedcdtrack;
				}

				// done
				COM_FCloseFile (&fh);
				Zone_Free (msgdata);
				return true;

				default:
					// unsupported
					COM_FCloseFile (&fh);
					Zone_Free (msgdata);
					return false;
				}

				// finished the message
				if (msgpos >= msgsize) break;
			}

			// release the data
			Zone_Free (msgdata);
		}

		COM_FCloseFile (&fh);

		return false;
	}

	// failed to open
	return false;
}


int Menu_DemoCustomDraw2 (int y)
{
	y += 5;

	if (democmd_num == 2)
	{
		Draw_Mapshot (va ("maps/%s", spinbox_bsps[demo_mapnum]), (vid.currsize->width - 320) / 2 + 96, y);
		y += 130;
		Menu_DoMapInfo (160, y, demo_mapnum, false);
	}
	else
	{
		if (demonum != old_demonum)
		{
			// pull out the demo info
			if (!M_Menu_Demo_Info (demolist[demonum]))
			{
				// reset to safe defaults
				dsi.cdtrack = 0;
				dsi.proto_version = 15;
				dsi.maxclients = 1;
				dsi.gametype = 0;
				dsi.stuff[0] = 0;
				dsi.levelname = NULL;
				dsi.levelname2 = NULL;
				dsi.mapname = NULL;
			}
			else
			{
				// save out
				dsi.levelname = &dsi.stuff[0];

				for (int i = 0;; i++)
				{
					if (dsi.stuff[i] == '/' || dsi.stuff[i] == '\\')
					{
						// the map name immediately follows the level name
						dsi.mapname = &dsi.stuff[i + 1];
						break;
					}
				}

				for (int i = 0;; i++)
				{
					if (dsi.mapname[i] == 0) break;

					if (dsi.mapname[i] == '.')
					{
						dsi.mapname[i] = 0;
						break;
					}
				}
			}

			old_demonum = demonum;
		}

		if (dsi.mapname)
			Draw_Mapshot (va ("maps/%s", dsi.mapname), (vid.currsize->width - 320) / 2 + 96, y);
		else Draw_Mapshot (NULL, (vid.currsize->width - 320) / 2 + 96, y);

		y += 130;

		if (dsi.levelname)
		{
			char templevelname[64];

			for (int i = 0;; i++)
			{
				templevelname[i] = dsi.levelname[i];

				// yeah, and fuck you too.
				if (i == 60) templevelname[i] = 0;

				if (!templevelname[i]) break;
			}

			// proper positioning
			Menu_PrintCenterWhite (y + 10, templevelname);
			Menu_PrintCenter (y + 25, va ("Maxclients: %i", dsi.maxclients));
			Menu_PrintCenter (y + 35, dsi.gametype ? "Deathmatch" : (dsi.maxclients > 1 ? "Cooperative" : "Single Player"));
			Menu_PrintCenter (y + 45, va ("CD Track: %i", dsi.cdtrack));
			y += 45;
		}
		else if (dsi.mapname)
		{
			// some maps don't have a name
			Menu_PrintCenterWhite (y + 10, dsi.mapname);
			Menu_PrintCenter (y + 25, va ("Maxclients: %i", dsi.maxclients));
			Menu_PrintCenter (y + 35, dsi.gametype ? "Deathmatch" : (dsi.maxclients > 1 ? "Cooperative" : "Single Player"));
			Menu_PrintCenter (y + 45, va ("CD Track: %i", dsi.cdtrack));
			y += 45;
		}

		// demo_mapnum is invalid here
		for (int i = 0;; i++)
		{
			if (!spinbox_bsps[i]) break;

			if (!stricmp (spinbox_bsps[i], dsi.mapname))
			{
				Menu_DoMapInfo (160, y, i, false);
				break;
			}
		}
	}

	y += 35;
	return y;
}


void Menu_DemoCommand (void)
{
	key_dest = key_game;

	if (democmd_num == 2)
	{
		Cbuf_InsertText
		(
			va
			(
				"disconnect; wait; skill %i; wait; record %s %s %i\n",
				demo_skillnum,
				dummy_demoname.string,
				spinbox_bsps[demo_mapnum],
				dummy_cdtrack.integer
			)
		);
	}
	else Cbuf_InsertText (va ("%s %s\n", democmds[democmd_num], demolist[demonum]));

	Cbuf_Execute ();
}


int menumapselectedbsp = 0;
int menumapsskillnum = 1;


char *memumapmodes[] = {"Map", "Changelevel", NULL};
int menumapmode = 0;

void Menu_MapsCustomEnter (void)
{
	// fix up spin controls in case the game has changed and we now have an invalid selection
	Menu_FixupSpin (&menumapselectedbsp, spinbox_maps);
	menumapsskillnum = skill.integer;
}


void Menu_RemoveMenu (void);

void Menu_MapCommand (void)
{
	// "map
	// launch the map command
	Menu_RemoveMenu ();
	Cvar_Set (&skill, menumapsskillnum);
	Cbuf_InsertText (va ("%s %s\n", memumapmodes[menumapmode], menu_mapslist[menumapselectedbsp].bspname));
	Cbuf_Execute ();
}


int Menu_MapsCustomDraw1 (int y)
{
	// same conditions as the changelevel command
	if (!sv.active || cls.demoplayback)
		menumapmode = 0;

	return y;
}


int Menu_MapsCustomDraw2 (int y)
{
	if (menumapmode == 0)
		Menu_PrintCenter (y, "Start on the map with default Weapons and Items");
	else Menu_PrintCenter (y, "Go to the map bringing all your stuff with you");

	return y + 15;
}


int Menu_MapsCustomDraw3 (int y)
{
	y += 5;
	Draw_Mapshot (va ("maps/%s", spinbox_bsps[menumapselectedbsp]), (vid.currsize->width - 320) / 2 + 96, y);
	y += 130;
	Menu_DoMapInfo (160, y, menumapselectedbsp);

	y += 45;

	return y;
}


void Menu_InitContentMenu (void)
{
	extern qpic_t *gfx_p_load_lmp;
	extern qpic_t *gfx_p_save_lmp;

	// maps
	menu_Maps.AddOption (new CQMenuBanner (&gfx_p_load_lmp));
	menu_Maps.AddOption (new CQMenuTitle ("Load a Map"));

	// allow selection by either map name or bsp name
	menu_Maps.AddOption (new CQMenuCustomEnter (Menu_MapsCustomEnter));
	menu_Maps.AddOption (new CQMenuCustomDraw (Menu_MapsCustomDraw1));
	menu_Maps.AddOption (new CQMenuSpinControl ("Select a Map", &menumapselectedbsp, &spinbox_bsps));
	menu_Maps.AddOption (new CQMenuSpinControl (NULL, &menumapselectedbsp, &spinbox_maps));
	menu_Maps.AddOption (new CQMenuSpacer (DIVIDER_LINE));
	menu_Maps.AddOption (new CQMenuSpinControl ("Skill", &menumapsskillnum, SkillNames));
	menu_Maps.AddOption (new CQMenuSpinControl ("Command", &menumapmode, memumapmodes));
	menu_Maps.AddOption (new CQMenuCustomDraw (Menu_MapsCustomDraw2));
	menu_Maps.AddOption (new CQMenuSpacer (DIVIDER_LINE));
	menu_Maps.AddOption (new CQMenuCommand ("Begin Map", Menu_MapCommand));
	menu_Maps.AddOption (new CQMenuSpacer (DIVIDER_LINE));
	menu_Maps.AddOption (new CQMenuCustomDraw (Menu_MapsCustomDraw3));
	menu_Maps.AddOption (new CQMenuSpacer (DIVIDER_LINE));
	menu_Maps.AddOption (new CQMenuCvarToggle ("Enable Mapshots", &r_automapshot, 0, 1));

	// demos
	menu_Demo.AddOption (new CQMenuCustomEnter (Menu_DemoCustomEnter));
	menu_Demo.AddOption (new CQMenuCustomDraw (Menu_DemoCustomDraw1));
	menu_Demo.AddOption (TAG_PLAYTIME, new CQMenuBanner (&gfx_p_load_lmp));
	menu_Demo.AddOption (TAG_RECORD, new CQMenuBanner (&gfx_p_save_lmp));
	menu_Demo.AddOption (TAG_PLAYONLY, new CQMenuTitle ("Run an Existing Demo"));
	menu_Demo.AddOption (TAG_TIMEONLY, new CQMenuTitle ("Benchmark an Existing Demo"));
	menu_Demo.AddOption (TAG_RECORD, new CQMenuTitle ("Record a New Demo"));
	menu_Demo.AddOption (TAG_SELECTMODE, new CQMenuSpinControl ("Command Mode", &democmd_num, democmds));
	menu_Demo.AddOption (TAG_PLAYTIME, new CQMenuSpinControl ("Demo File Name", &demonum, &demolist));
	menu_Demo.AddOption (TAG_RECORD, new CQMenuCvarTextbox ("New Demo File Name", &dummy_demoname, TBFLAGS_FILENAMEFLAGS));
	menu_Demo.AddOption (TAG_RECORD, new CQMenuSpacer (DIVIDER_LINE));
	menu_Demo.AddOption (TAG_RECORD, new CQMenuSpinControl ("Select a Map", &demo_mapnum, &spinbox_bsps));
	menu_Demo.AddOption (TAG_RECORD, new CQMenuSpinControl (NULL, &demo_mapnum, &spinbox_maps));
	menu_Demo.AddOption (TAG_RECORD, new CQMenuSpacer (DIVIDER_LINE));
	menu_Demo.AddOption (TAG_RECORD, new CQMenuSpinControl ("Skill", &demo_skillnum, SkillNames));
	menu_Demo.AddOption (TAG_RECORD, new CQMenuSpinControl ("CD Track", &dummy_cdtrack, 2, 11, 1, NULL, NULL));
	menu_Demo.AddOption (new CQMenuSpacer (DIVIDER_LINE));
	menu_Demo.AddOption (TAG_PLAYTIME, new CQMenuCommand ("Begin Playback", Menu_DemoCommand));
	menu_Demo.AddOption (TAG_RECORDCMD, new CQMenuCommand ("Begin Recording", Menu_DemoCommand));
	menu_Demo.AddOption (new CQMenuSpacer (DIVIDER_LINE));
	menu_Demo.AddOption (new CQMenuCustomDraw (Menu_DemoCustomDraw2));
	menu_Demo.AddOption (new CQMenuSpacer (DIVIDER_LINE));
	menu_Demo.AddOption (new CQMenuCvarToggle ("Enable Mapshots", &r_automapshot, 0, 1));
}


