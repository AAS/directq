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


#include "quakedef.h"
#include "menu_common.h"
#include "winquake.h"

// STL vector and sort need these
#include <vector>
#include <algorithm>

void COM_BuildContentList (std::vector<char *> &FileList, char *basedir, char *filetype);

// cvars used
extern cvar_t xi_usecontroller;
extern cvar_t xi_dpadarrowmap;
extern cvar_t xi_axislx;
extern cvar_t xi_axisly;
extern cvar_t xi_axisrx;
extern cvar_t xi_axisry;
extern cvar_t xi_axislt;
extern cvar_t xi_axisrt;
extern cvar_t gl_texturedir;
extern cvar_t scr_screenshotformat;
extern cvar_t scr_screenshotdir;
extern cvar_t r_lerporient;
extern cvar_t r_lerpframe;
extern cvar_t r_lerplightstyle;
extern cvar_t host_savedir;
extern cvar_t m_filter;
extern cvar_t m_look;
extern cvar_t m_boost;
extern cvar_t chase_back;
extern cvar_t chase_up;
extern cvar_t chase_right;
extern cvar_t chase_active;
extern cvar_t chase_scale;
extern cvar_t ds_musicdir;
extern cvar_t sound_nominal_clip_dist;
extern cvar_t ambient_level;
extern cvar_t ambient_fade;
extern cvar_t r_monolight;
extern cvar_t r_extradlight;
extern cvar_t r_rapidfire;
extern cvar_t hud_defaulthud;
extern cvar_t r_warpspeed;
extern cvar_t r_lavaalpha;
extern cvar_t r_telealpha;
extern cvar_t r_slimealpha;
extern cvar_t r_lockalpha;
extern cvar_t m_directinput;
extern cvar_t in_joystick;
extern cvar_t m_accellevel;
extern cvar_t m_accelthreshold1;
extern cvar_t m_accelthreshold2;
extern cvar_t r_zhack;
extern cvar_t r_automapshot;
extern cvar_t com_hipnotic;
extern cvar_t com_rogue;
extern cvar_t com_quoth;
extern cvar_t scr_centerlog;
extern cvar_t host_savenamebase;
extern cvar_t scr_shotnamebase;
extern cvar_t r_skywarp;
extern cvar_t r_skybackscroll;
extern cvar_t r_skyfrontscroll;
extern cvar_t r_waterwarptime;
extern cvar_t r_waterwarpscale;
extern cvar_t r_defaultshaderprecision;
extern cvar_t r_warpshaderprecision;
extern cvar_t menu_fillcolor;
extern cvar_t r_skyalpha;

CQMenu menu_Main (NULL, m_main);
CQMenu menu_Singleplayer (&menu_Main, m_other);
CQMenu menu_Save (&menu_Singleplayer, m_save);
CQMenu menu_Load (&menu_Singleplayer, m_load);
CQMenu menu_Multiplayer (&menu_Main, m_multiplayer);
CQMenu menu_TCPIPNewGame (&menu_Multiplayer, m_lanconfig_newgame);
CQMenu menu_TCPIPJoinGame (&menu_Multiplayer, m_lanconfig_joingame);
CQMenu menu_GameConfig (&menu_TCPIPNewGame, m_gameoptions);
CQMenu menu_Setup (&menu_Multiplayer, m_setup);
CQMenu menu_Options (&menu_Main, m_options);
CQMenu menu_Menu (&menu_Options, m_video);
CQMenu menu_Video (&menu_Options, m_video);
CQMenu menu_Sound (&menu_Options, m_other);
CQMenu menu_Help (&menu_Main, m_help);
CQMenu menu_Input (&menu_Options, m_other);
CQMenu menu_Keybindings (&menu_Input, m_keys);
CQMenu menu_Effects (&menu_Options, m_other);
CQMenu menu_WarpSurf (&menu_Options, m_other);
CQMenu menu_ContentDir (&menu_Options, m_other);
CQMenu menu_Chase (&menu_Options, m_other);
CQMenu menu_Game (&menu_Main, m_other);
CQMenu menu_Controller (&menu_Input, m_other);
CQMenu menu_Maps (&menu_Main, m_other);
CQMenu menu_Demo (&menu_Main, m_other);
extern CQMenu menu_HUD;

// for use in various menus
char *SkillNames[] =
{
	"Easy",
	"Normal",
	"Hard",
	"NIGHTMARE",
	NULL
};


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

	if (cls.demonum != -1 && !cls.demoplayback && cls.state != ca_connected) CL_NextDemo ();
}


void Menu_MainCustomEnter (void)
{
	m_save_demonum = cls.demonum;
	cls.demonum = -1;
}


void Menu_MainExitQuake (void)
{
//	if (sv.active)
		if (!SCR_ModalMessage ("Are you sure you want to\nExit Quake?\n", "Confirm Exit", MB_YESNO))
			return;

	// exit - run one final screen update to flush any pending rendering
	key_dest = key_console;
	SCR_UpdateScreen ();
	Host_Quit_f ();
}


void Menu_InitMainMenu (void)
{
	menu_Main.AddOption (new CQMenuBanner ("gfx/ttl_main.lmp"));
	menu_Main.AddOption (new CQMenuTitle ("Select a Submenu"));
	menu_Main.AddOption (new CQMenuSubMenu ("Single Player Menu", &menu_Singleplayer));
	menu_Main.AddOption (new CQMenuSpacer ());
	menu_Main.AddOption (new CQMenuSubMenu ("Multiplayer Menu", &menu_Multiplayer));
	menu_Main.AddOption (new CQMenuSpacer ());
	menu_Main.AddOption (new CQMenuSubMenu ("Configure Game Options", &menu_Options));
	menu_Main.AddOption (new CQMenuSpacer ());
	menu_Main.AddOption (new CQMenuSubMenu ("Select Game Directories", &menu_Game));
	menu_Main.AddOption (new CQMenuSpacer ());
	menu_Main.AddOption (new CQMenuSubMenu ("Load a Map", &menu_Maps));
	menu_Main.AddOption (new CQMenuSpacer ());
	menu_Main.AddOption (new CQMenuSubMenu ("Run or Record a Demo", &menu_Demo));
	menu_Main.AddOption (new CQMenuSpacer ());
	menu_Main.AddOption (new CQMenuSubMenu ("Help/Ordering", &menu_Help));
	menu_Main.AddOption (new CQMenuSpacer ());
	menu_Main.AddOption (new CQMenuCommand ("Exit Quake", Menu_MainExitQuake));
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

void Menu_OptionsResetToDefaults (void)
{
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
	{"+lookup", 		"Look Up"},
	{"+lookdown", 		"Look Down"},
	{"centerview", 		"Center View"},
	{"+mlook", 			"Mouse Look"},
	{"+klook", 			"Keyboard Look"},
	{"+moveup",			"Swim Up"},
	{"+movedown",		"Swim Down"}
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
			sprintf (cmd, "bind \"%s\" \"%s\"\n", Key_KeynumToString (key), key_bindnames[bind_cursor][0]);
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
		menu_Options.EnterMenu ();
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

bool CheckKnownFile (char *path)
{
	FILE *f = fopen (path, "rb");

	if (f)
	{
		fclose (f);
		return true;
	}

	return false;
}


bool IsGameDir (char *path)
{
	// check for files that indicate a gamedir
	if (CheckKnownFile (va ("%s/pak0.pak", path))) return true;
	if (CheckKnownFile (va ("%s/config.cfg", path))) return true;
	if (CheckKnownFile (va ("%s/autoexec.cfg", path))) return true;
	if (CheckKnownFile (va ("%s/progs.dat", path))) return true;
	if (CheckKnownFile (va ("%s/gfx.wad", path))) return true;

	return false;
}


void AddGameDirOption (char *dirname, cvar_t *togglecvar)
{
	if (!IsGameDir (dirname)) return;

	if (togglecvar)
		menu_Game.AddOption (new CQMenuCvarToggle (dirname, togglecvar, 0, 1));
	else menu_Game.AddOption (new CQMenuSpacer (dirname));
}

#define MAX_GAME_DIRS	1024
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
	for (gamedirnum = 0; ; gamedirnum++)
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


void Menu_GameLoadGames (void)
{
	// drop out of the menus
	key_dest = key_game;

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
		if (!stricmp (FindFileData.cFileName, "rogue")) continue;
		if (!stricmp (FindFileData.cFileName, "hipnotic")) continue;
		if (!stricmp (FindFileData.cFileName, "quoth")) continue;

		// ensure that it's a game dir
		if (!IsGameDir (FindFileData.cFileName)) continue;

		// store it out
		gamedirs[numgamedirs] = (char *) Heap_QMalloc (strlen (FindFileData.cFileName) + 1);
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

	if (IsGameDir ("Hipnotic") || IsGameDir ("Rogue") || IsGameDir ("Quoth") || numgamedirs)
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


char *ShotTypes[] = {"TGA", "BMP", "PNG", "DDS", "JPG", NULL};
int shottype_number = 0;
int old_shottype_number = 0;

void Menu_ContentCustomEnter (void)
{
	// select the correct shot type
	for (int i = 0; ; i++)
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


char *skywarpstyles[] = {"DirectQ", "Classic", NULL};

// keep these consistent with the loader
extern char *TextureExtensions[];
extern char *sbdir[];
extern char *suf[];
char **skybox_menulist = NULL;
int skybox_menunumber = 0;
int old_skybox_menunumber = 0;

#define TAG_SKYBOXAPPLY		1
#define TAG_WATERWARP		2
#define TAG_WATERALPHA		3

void Menu_LoadAvailableSkyboxes (void)
{
	if (skybox_menulist)
	{
		for (int i = 0; ; i++)
		{
			if (!skybox_menulist[i]) break;
			free (skybox_menulist[i]);
		}

		free (skybox_menulist);
		skybox_menulist = NULL;
	}

	std::vector<char *> SkyboxList;

	for (int i = 0; ; i++)
	{
		if (!sbdir[i]) break;

		for (int j = 0; ; j++)
		{
			if (!TextureExtensions[j]) break;

			// because we allow skyboxes components to be missing (crazy modders!) we need to check all 6 suffixes
			for (int s = 0; s < 6; s++)
			{
				COM_BuildContentList (SkyboxList, va ("%s/", sbdir[i]), va ("%s.%s", suf[s], TextureExtensions[j]));
			}
		}
	}

	// set up the array for use
	int listlen = SkyboxList.size ();

	// because we added each skybox component (to cover for crazy modders who leave one out) we now go
	// through the list and remove duplicates, copying it into a second list as we go.  this is what we're reduced to. :(
	std::vector<char *> NewSkyboxList;

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

		int newlen = NewSkyboxList.size ();
		bool present = false;

		for (int j = 0; j < newlen; j++)
		{
			if (!stricmp (NewSkyboxList[j], SkyboxList[i]))
			{
				present = true;
				break;
			}
		}

		if (!present)
		{
			char *addbox = (char *) malloc (strlen (SkyboxList[i]) + 1);
			strcpy (addbox, SkyboxList[i]);
			NewSkyboxList.push_back (addbox);
		}

		free (SkyboxList[i]);
	}

	SkyboxList.clear ();
	listlen = NewSkyboxList.size ();

	// alloc a buffer for the menu list (add 1 to null term the list)
	// and a second for the first item, which will be "no skybox"
	skybox_menulist = (char **) malloc ((listlen + 2) * sizeof (char *));

	// this is needed for cases where there are no skyboxes present
	skybox_menulist[1] = NULL;

	for (int i = 0; i < listlen; i++)
	{
		skybox_menulist[i + 1] = (char *) malloc (strlen (NewSkyboxList[i]) + 1);
		strcpy (skybox_menulist[i + 1], NewSkyboxList[i]);

		// it's the callers responsibility to free up the list generated by COM_BuildContentList
		free (NewSkyboxList[i]);

		// always null term the next item to ensure that the menu list is null termed itself
		skybox_menulist[i + 2] = NULL;
	}

	NewSkyboxList.clear ();

	// set up item 0 (ensure that it's name can't conflict with a valid name)
	skybox_menulist[0] = (char *) malloc (20);
	strcpy (skybox_menulist[0], "none/no skybox");

	// new menu
	skybox_menunumber = 0;
	old_skybox_menunumber = 0;
}


int Menu_WarpCustomDraw (int y)
{
	if (skybox_menunumber != old_skybox_menunumber)
	{
		menu_WarpSurf.EnableOptions (TAG_SKYBOXAPPLY);
		// this broke the enable/disable switch - it's now done at load time instead
		//old_skybox_menunumber = skybox_menunumber;
	}
	else menu_WarpSurf.DisableOptions (TAG_SKYBOXAPPLY);

	// this is a little cleaner now...
	if (r_lockalpha.value)
		menu_WarpSurf.DisableOptions (TAG_WATERALPHA);
	else menu_WarpSurf.EnableOptions (TAG_WATERALPHA);

	// sky warp style
	r_skywarp.integer = !!r_skywarp.integer;
	Cvar_Set (&r_skywarp, r_skywarp.integer);

	// water warp
	if (r_waterwarp.value)
		menu_WarpSurf.EnableOptions (TAG_WATERWARP);
	else menu_WarpSurf.DisableOptions (TAG_WATERWARP);

	// keep y
	return y;
}


void Menu_WarpCustomEnter (void)
{
	skybox_menunumber = 0;
	old_skybox_menunumber = 0;

	extern char CachedSkyBoxName[];

	// find the current skybox
	for (int i = 0; ; i++)
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


void R_LoadSkyBox (char *basename, bool feedback);

void Menu_WarpSkyBoxApply (void)
{
	if (skybox_menunumber)
		R_LoadSkyBox (skybox_menulist[skybox_menunumber], true);
	else
	{
		// item 0 is "no skybox"
		R_LoadSkyBox (skybox_menulist[skybox_menunumber], false);
		Con_Printf ("Skybox Unloaded\n");
	}

	old_skybox_menunumber = skybox_menunumber;
}


int Menu_MenuCustomDraw (int y)
{
	// save out menu highlight colour
	Cvar_Set (&menu_fillcolor, menu_fillcolor.integer);

	return y;
}


void Menu_InitOptionsMenu (void)
{
	// options
	menu_Options.AddOption (new CQMenuBanner ("gfx/p_option.lmp"));
	menu_Options.AddOption (new CQMenuTitle ("Configuration Management"));
	menu_Options.AddOption (new CQMenuCommand ("Go to Console", Menu_OptionsGoToConsole));
	menu_Options.AddOption (new CQMenuCommand ("Reset to Defaults", Menu_OptionsResetToDefaults));
	menu_Options.AddOption (new CQMenuCommand ("Save Current Configuration", Host_WriteConfiguration));
	menu_Options.AddOption (new CQMenuTitle ("Options"));
	menu_Options.AddOption (new CQMenuSubMenu ("Video and View Options", &menu_Video));
	menu_Options.AddOption (new CQMenuSubMenu ("Special Effects Options", &menu_Effects));
	menu_Options.AddOption (new CQMenuSubMenu ("Warp Surfaces Options", &menu_WarpSurf));
	menu_Options.AddOption (new CQMenuSubMenu ("Sound Options", &menu_Sound));
	menu_Options.AddOption (new CQMenuSubMenu ("Movement and Input Options", &menu_Input));
	menu_Options.AddOption (new CQMenuSubMenu ("Chase Camera Options", &menu_Chase));
	menu_Options.AddOption (new CQMenuSubMenu ("Menu Layout and Configuration", &menu_Menu));
	menu_Options.AddOption (new CQMenuSubMenu ("HUD Layout and Configuration", &menu_HUD));
	menu_Options.AddOption (new CQMenuSpacer (DIVIDER_LINE));
	menu_Options.AddOption (new CQMenuSubMenu ("Content Options", &menu_ContentDir));

	menu_Menu.AddOption (new CQMenuBanner ("gfx/p_option.lmp"));
	menu_Menu.AddOption (new CQMenuCustomDraw (Menu_MenuCustomDraw));
	menu_Menu.AddOption (new CQMenuTitle ("Menu Layout and Configuration"));
	menu_Menu.AddOption (new CQMenuColourBar ("Highlight Colour", &menu_fillcolor.integer));

	// warp
	menu_WarpSurf.AddOption (new CQMenuCustomEnter (Menu_WarpCustomEnter));
	menu_WarpSurf.AddOption (new CQMenuCustomDraw (Menu_WarpCustomDraw));
	menu_WarpSurf.AddOption (new CQMenuBanner ("gfx/p_option.lmp"));
	menu_WarpSurf.AddOption (new CQMenuTitle ("Scrolling Sky"));
	menu_WarpSurf.AddOption (new CQMenuSpinControl ("Style", &r_skywarp.integer, skywarpstyles));
	menu_WarpSurf.AddOption (new CQMenuCvarSlider ("Back Layer Scroll", &r_skybackscroll, 0, 32, 2));
	menu_WarpSurf.AddOption (new CQMenuCvarSlider ("Front Layer Scroll", &r_skyfrontscroll, 0, 32, 2));
	menu_WarpSurf.AddOption (new CQMenuCvarSlider ("Front Layer Alpha", &r_skyalpha, 0, 1, 0.1f));
	menu_WarpSurf.AddOption (new CQMenuTitle ("Skyboxes"));
	// note - char *** is intentional here...
	menu_WarpSurf.AddOption (new CQMenuSpinControl ("Skybox Name", &skybox_menunumber, &skybox_menulist));
	menu_WarpSurf.AddOption (TAG_SKYBOXAPPLY, new CQMenuCommand ("Load this Skybox", Menu_WarpSkyBoxApply));
	menu_WarpSurf.AddOption (new CQMenuTitle ("Water And Other Liquids"));
	menu_WarpSurf.AddOption (new CQMenuCvarSlider ("Water Warp Speed", &r_warpspeed, 1, 10, 1));
	menu_WarpSurf.AddOption (new CQMenuCvarToggle ("Lock Alpha Sliders", &r_lockalpha, 0, 1));
	menu_WarpSurf.AddOption (new CQMenuCvarSlider ("Water Alpha", &r_wateralpha, 0, 1, 0.1));
	menu_WarpSurf.AddOption (TAG_WATERALPHA, new CQMenuCvarSlider ("Lava Alpha", &r_lavaalpha, 0, 1, 0.1));
	menu_WarpSurf.AddOption (TAG_WATERALPHA, new CQMenuCvarSlider ("Slime Alpha", &r_slimealpha, 0, 1, 0.1));
	menu_WarpSurf.AddOption (TAG_WATERALPHA, new CQMenuCvarSlider ("Teleport Alpha", &r_telealpha, 0, 1, 0.1));
	menu_WarpSurf.AddOption (new CQMenuTitle ("Underwater"));
	menu_WarpSurf.AddOption (new CQMenuCvarToggle ("Underwater Warp", &r_waterwarp, 0, 1));
	menu_WarpSurf.AddOption (TAG_WATERWARP, new CQMenuCvarSlider ("Warp Speed", &r_waterwarptime, 0, 8, 1));
	menu_WarpSurf.AddOption (TAG_WATERWARP, new CQMenuCvarSlider ("Warp Scale", &r_waterwarpscale, 0, 0.25, 0.025));

	// sound
	menu_Sound.AddOption (new CQMenuBanner ("gfx/p_option.lmp"));
	menu_Sound.AddOption (new CQMenuTitle ("Sound Options"));
	menu_Sound.AddOption (new CQMenuCvarSlider ("Music Volume", &bgmvolume, 0, 1, 0.05));
	menu_Sound.AddOption (new CQMenuCvarSlider ("Sound Volume", &volume, 0, 1, 0.05));
	menu_Sound.AddOption (new CQMenuCvarSlider ("Clip Distance", &sound_nominal_clip_dist, 500, 2000, 100));
	menu_Sound.AddOption (new CQMenuCvarSlider ("Ambient Level", &ambient_level, 0, 1, 0.05));
	menu_Sound.AddOption (new CQMenuCvarSlider ("Ambient Fade", &ambient_fade, 50, 200, 10));

	// chase cam - now that it's fixed, let's expose the wee bugger
	menu_Chase.AddOption (new CQMenuBanner ("gfx/p_option.lmp"));
	menu_Chase.AddOption (new CQMenuTitle ("Chase Camera Options"));
	menu_Chase.AddOption (new CQMenuCvarToggle ("Chase Active", &chase_active, 0, 1));
	menu_Chase.AddOption (new CQMenuSpacer (DIVIDER_LINE));
	menu_Chase.AddOption (new CQMenuCvarSlider ("Chase Back", &chase_back, 50, 500, 25));
	menu_Chase.AddOption (new CQMenuCvarSlider ("Chase Up", &chase_up, -64, 256, 16));
	menu_Chase.AddOption (new CQMenuCvarSlider ("Chase Right", &chase_right, -64, 64, 8));
	menu_Chase.AddOption (new CQMenuSpacer (DIVIDER_LINE));
	menu_Chase.AddOption (new CQMenuCvarSlider ("Smoothness", &chase_scale, 0.2, 1.8, 0.1));

	// input
	menu_Input.AddOption (new CQMenuBanner ("gfx/p_option.lmp"));
	menu_Input.AddOption (new CQMenuTitle ("Input Options"));
	menu_Input.AddOption (new CQMenuSubMenu ("Customize Controls", &menu_Keybindings));
	menu_Input.AddOption (new CQMenuSubMenu ("Configure XBox 360 Controller", &menu_Controller));
	menu_Input.AddOption (new CQMenuSpacer (DIVIDER_LINE));
	menu_Input.AddOption (new CQMenuCvarToggle ("Use DirectInput", &m_directinput));
	menu_Input.AddOption (new CQMenuCvarToggle ("Use Joystick", &in_joystick));
	menu_Input.AddOption (new CQMenuSpacer (DIVIDER_LINE));
	menu_Input.AddOption (new CQMenuCvarSlider ("Mouse Speed", &sensitivity, 1, 21, 1));
	menu_Input.AddOption (new CQMenuCvarSlider ("Mouse Acceleration", &m_accellevel, 0, 2, 1));
	menu_Input.AddOption (new CQMenuCvarSlider ("Threshold 1", &m_accelthreshold1, 0, 20, 1));
	menu_Input.AddOption (new CQMenuCvarSlider ("Threshold 2", &m_accelthreshold2, 0, 20, 1));
	menu_Input.AddOption (new CQMenuCvarSlider ("DirectInput Boost", &m_boost, 1, 5, 0.5));
	menu_Input.AddOption (new CQMenuCvarToggle ("Mouse Look", &m_look, 0, 1));
	menu_Input.AddOption (new CQMenuCvarToggle ("Mouse Filter", &m_filter, 0, 1));
	menu_Input.AddOption (new CQMenuCvarToggle ("Invert Mouse", &m_pitch, 0.022, -0.022));
	menu_Input.AddOption (new CQMenuTitle ("Movement Options"));
	menu_Input.AddOption (new CQMenuCvarToggle ("Always Run", &cl_forwardspeed, 200, 400));
	menu_Input.AddOption (new CQMenuCvarToggle ("Lookspring", &lookspring, 0, 1));
	menu_Input.AddOption (new CQMenuCvarToggle ("Lookstrafe", &lookstrafe, 0, 1));

	// effects
	menu_Effects.AddOption (new CQMenuBanner ("gfx/p_option.lmp"));
	menu_Effects.AddOption (new CQMenuCustomDraw (Menu_EffectsCustomDraw));
	menu_Effects.AddOption (new CQMenuTitle ("Interpolation"));
	menu_Effects.AddOption (new CQMenuCvarToggle ("Orientation", &r_lerporient, 0, 1));
	menu_Effects.AddOption (new CQMenuCvarToggle ("Frame", &r_lerpframe, 0, 1));
	menu_Effects.AddOption (new CQMenuCvarToggle ("Light Style", &r_lerplightstyle, 0, 1));
	menu_Effects.AddOption (new CQMenuTitle ("Lighting"));
	menu_Effects.AddOption (new CQMenuCvarToggle ("Coloured Light", &r_monolight, 1, 0));
	menu_Effects.AddOption (new CQMenuCvarToggle ("Extra DLights", &r_extradlight, 0, 1));
	menu_Effects.AddOption (new CQMenuCvarToggle ("Rapid Fire Effect", &r_rapidfire, 0, 1));
	menu_Effects.AddOption (new CQMenuTitle ("Other Effects"));
	menu_Effects.AddOption (new CQMenuCvarToggle ("Z-Fighting Hack", &r_zhack, 0, 1));

	// this is to give users some control over where content items go as they're not standardised per engine
	// in the case of music we always fall back to /sound/cdtracks, then /music in addition to what the user
	// selected as there is one popular engine that intentionally broke compatibility with everything else.
	menu_ContentDir.AddOption (new CQMenuCustomEnter (Menu_ContentCustomEnter));
	menu_ContentDir.AddOption (new CQMenuBanner ("gfx/p_option.lmp"));
	menu_ContentDir.AddOption (new CQMenuTitle ("General Content Options"));
	menu_ContentDir.AddOption (new CQMenuCvarToggle ("Automatic Mapshots", &r_automapshot, 0, 1));
	menu_ContentDir.AddOption (new CQMenuTitle ("Screenshot Content Options"));
	menu_ContentDir.AddOption (new CQMenuCvarTextbox ("Shot Directory", &scr_screenshotdir, TBFLAGS_FOLDERNAMEFLAGS));
	menu_ContentDir.AddOption (new CQMenuCvarTextbox ("Shot Base Name", &scr_shotnamebase, TBFLAGS_FILENAMEFLAGS));
	menu_ContentDir.AddOption (new CQMenuSpinControl ("Shot Format", &shottype_number, ShotTypes));
	menu_ContentDir.AddOption (new CQMenuTitle ("Save Game Content Options"));
	menu_ContentDir.AddOption (new CQMenuCvarTextbox ("Save Directory", &host_savedir, TBFLAGS_FOLDERNAMEFLAGS));
	menu_ContentDir.AddOption (new CQMenuCvarTextbox ("Save Base Name", &host_savenamebase, TBFLAGS_FILENAMEFLAGS));
	menu_ContentDir.AddOption (new CQMenuTitle ("Other Content Options"));
	menu_ContentDir.AddOption (new CQMenuCvarTextbox ("Textures Directory", &gl_texturedir, TBFLAGS_FOLDERNAMEFLAGS));
	menu_ContentDir.AddOption (new CQMenuCvarTextbox ("Music Directory", &ds_musicdir, TBFLAGS_FOLDERNAMEFLAGS));
	menu_ContentDir.AddOption (new CQMenuCvarTextbox ("Default HUD Name", &hud_defaulthud, TBFLAGS_FILENAMEFLAGS));

	// this is needed so that we can always save back the correct screenshot format to the cvar
	menu_ContentDir.AddOption (new CQMenuCustomDraw (Menu_ContentCustomDraw));

	// keybindings
	menu_Keybindings.AddOption (new CQMenuBanner ("gfx/ttl_cstm.lmp"));
	menu_Keybindings.AddOption (new CQMenuCustomDraw (Menu_KeybindingsCustomDraw));

	// grab every key for these actions
	for (int i = 0; i < 256; i++)
		menu_Keybindings.AddOption (new CQMenuCustomKey (i, Menu_KeybindingsCustomKey));

	// video menu - the rest of the options are deferred until the menu is up!
	// note - the new char *** spinbox style means this isn't actually required any more,
	// but this style hasn't been let out into the wild yet, so it may have some subtle bugs i'm unaware of
	// see Menu_VideoCustomEnter in d3d_vidnt.cpp
	menu_Video.AddOption (new CQMenuBanner ("gfx/vidmodes.lmp"));
	menu_Video.AddOption (new CQMenuTitle ("Configure Video Modes"));
	menu_Video.AddOption (new CQMenuCustomEnter (Menu_VideoCustomEnter));

	// gamedir menu
	menu_Game.AddOption (new CQMenuBanner ("gfx/p_option.lmp"));

	// add in add-ons
	if (IsGameDir ("Hipnotic") || IsGameDir ("Rogue") || IsGameDir ("Quoth"))
	{
		menu_Game.AddOption (new CQMenuTitle ("Select Game Add-Ons"));
		AddGameDirOption ("Hipnotic", &com_hipnotic);
		AddGameDirOption ("Rogue", &com_rogue);
		AddGameDirOption ("Quoth", &com_quoth);
	}

	EnumGameDirs ();
	menu_Game.AddOption (new CQMenuCustomEnter (Menu_GameCustomEnter));
}


/*
========================================================================================================================

					HELP MENU

========================================================================================================================
*/

int menu_HelpPage = 0;
#define	NUM_HELP_PAGES	6


void Menu_HelpCustomEnter (void)
{
	// switch the help page depending on whether we're playing the registered version or not
	if (registered.value)
		menu_HelpPage = 1;
	else menu_HelpPage = 0;
}


int Menu_HelpCustomDraw (int y)
{
	qpic_t *pic = Draw_CachePic (va ("gfx/help%i.lmp", menu_HelpPage));
	Draw_Pic ((vid.width - pic->width) >> 1, y, pic);
	return y + pic->height + 10;
}


void Menu_HelpCustomKey (int key)
{
	menu_soundlevel = m_sound_option;

	switch (key)
	{
	case K_UPARROW:
	case K_RIGHTARROW:
		if (registered.value)
		{
			if (++menu_HelpPage >= NUM_HELP_PAGES)
				menu_HelpPage = 1;
		}
		else
		{
			if (++menu_HelpPage >= NUM_HELP_PAGES)
				menu_HelpPage = 0;
		}
		break;

	case K_DOWNARROW:
	case K_LEFTARROW:
		if (registered.value)
		{
			if (--menu_HelpPage < 1)
				menu_HelpPage = NUM_HELP_PAGES - 1;
		}
		else
		{
			if (--menu_HelpPage < 0)
				menu_HelpPage = NUM_HELP_PAGES - 1;
		}
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

char *AxisActions[] =
{
	"No Action",
	"Look Down/Up",
	"Move Fwd/Back",
	"Turn Left/Right",
	"Strafe Left/Right",
	"Look Up/Down",
	"Move Back/Fwd",
	"Turn Right/Left",
	"Strafe Right/Left",
	NULL
};


int Menu_ControllerCustomDraw (int y)
{
	// force a correct set
	Cvar_Set (&xi_axislx, xi_axislx.integer);
	Cvar_Set (&xi_axisly, xi_axisly.integer);
	Cvar_Set (&xi_axisrx, xi_axisrx.integer);
	Cvar_Set (&xi_axisry, xi_axisry.integer);
	Cvar_Set (&xi_axislt, xi_axislt.integer);
	Cvar_Set (&xi_axisrt, xi_axisrt.integer);

	extern int xiActiveController;

	if (xiActiveController < 0)
		Menu_PrintCenter (y, "No Controllers Found");
	else
		Menu_PrintCenter (y, va ("Found Controller on Port %i", xiActiveController));

	return y + 15;
}


void IN_StartupXInput (void);
void IN_ActivateMouse (void);

void Menu_RescanControllers (void)
{
	// rescan for controllers and activate them
	IN_StartupXInput ();
	IN_ActivateMouse ();
}


void Menu_InitControllerMenu (void)
{
	// options
	menu_Controller.AddOption (new CQMenuBanner ("gfx/p_option.lmp"));
	menu_Controller.AddOption (new CQMenuTitle ("XBox 360 Controller Options"));
	menu_Controller.AddOption (new CQMenuCustomDraw (Menu_ControllerCustomDraw));
	menu_Controller.AddOption (new CQMenuCommand ("Rescan for Controllers", Menu_RescanControllers));
	menu_Controller.AddOption (new CQMenuSpacer (DIVIDER_LINE));
	menu_Controller.AddOption (new CQMenuCvarToggle ("Use Controller", &xi_usecontroller, 0, 1));
	menu_Controller.AddOption (new CQMenuCvarToggle ("Map DPAD to Arrows", &xi_dpadarrowmap, 0, 1));
	menu_Controller.AddOption (new CQMenuTitle ("Controller Axis Actions"));
	menu_Controller.AddOption (new CQMenuSpinControl ("Left Thumb X", &xi_axislx.integer, AxisActions));
	menu_Controller.AddOption (new CQMenuSpinControl ("Left Thumb Y", &xi_axisly.integer, AxisActions));
	menu_Controller.AddOption (new CQMenuSpinControl ("Right Thumb X", &xi_axisrx.integer, AxisActions));
	menu_Controller.AddOption (new CQMenuSpinControl ("Right Thumb Y", &xi_axisry.integer, AxisActions));
	menu_Controller.AddOption (new CQMenuSpinControl ("Left Trigger", &xi_axislt.integer, AxisActions));
	menu_Controller.AddOption (new CQMenuSpinControl ("Right Trigger", &xi_axisrt.integer, AxisActions));
}


typedef struct mapinfo_s
{
	char *bspname;
	char *mapname;
	char *skybox;
	char *wad;
	int ambience;
	int cdtrack;
} mapinfo_t;

mapinfo_t *menu_mapslist = NULL;
int num_menumaps = 0;
CScrollBoxProvider *MapListScrollBox = NULL;

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
			strncpy (key, &com_token[1], 39);
		else strncpy (key, com_token, 39);

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
			info->mapname = (char *) malloc (strlen (com_token) + 1);
			strcpy (info->mapname, com_token);
		}
		else if (!stricmp (key, "sounds"))
		{
			info->cdtrack = Q_atoi (com_token);
		}
		else if (!stricmp (key, "worldtype"))
		{
			info->ambience = Q_atoi (com_token);
		}
		else if (!stricmp (key, "sky") || !stricmp (key, "skyname") || !stricmp (key, "q1sky") || !stricmp (key, "skybox"))
		{
			info->skybox = (char *) malloc (strlen (com_token) + 1);
			strcpy (info->skybox, com_token);
		}
		else if (!stricmp (key, "wad"))
		{
			info->wad = (char *) malloc (strlen (com_token) + 1);
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

	Draw_Mapshot (va ("maps/%s", menu_mapslist[itemnum].bspname), (vid.width - 320) / 2 + 208, initialy + 55);

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
	FILE *f;
	char fullmapname[MAX_PATH];

	sprintf (fullmapname, "maps/%s", mapname);

	COM_FOpenFile (fullmapname, &f);

	if (!f) return false;

	// because the map could be coming from a PAK file we need to save out the base pos so that fseek is valid
	int basepos = ftell (f);
	dheader_t bsphead;

	fread (&bsphead, sizeof (dheader_t), 1, f);

	if (bsphead.version != BSPVERSION)
	{
		// don't add maps with a bad version number to the list
		fclose (f);
		return false;
	}

	// find the entities lump
	fseek (f, basepos + bsphead.lumps[LUMP_ENTITIES].fileofs, SEEK_SET);

	// read it all in
	char *entlump = (char *) malloc (bsphead.lumps[LUMP_ENTITIES].filelen);
	fread (entlump, bsphead.lumps[LUMP_ENTITIES].filelen, 1, f);

	// done with the file now
	fclose (f);

	// true if the map is valid
	bool validmap = false;

	for (int i = 0; i < (bsphead.lumps[LUMP_ENTITIES].filelen - 11); i++)
	{
		if (!strnicmp (&entlump[i], "info_player", 11))
		{
			// map is valid
			menu_mapslist[itemnum].bspname = (char *) malloc (strlen (mapname) + 1);
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

	// not found
	free (entlump);
	return validmap;
}


void Menu_MapsPopulate (void)
{
	// clear down previous map list
	if (menu_mapslist)
	{
		for (int i = 0; i < num_menumaps; i++)
		{
			if (menu_mapslist[i].bspname) free (menu_mapslist[i].bspname);
			if (menu_mapslist[i].mapname) free (menu_mapslist[i].mapname);
			if (menu_mapslist[i].skybox) free (menu_mapslist[i].skybox);
			if (menu_mapslist[i].wad) free (menu_mapslist[i].wad);
		}

		// because someone somewhere sometime might attempt to run quake without any BSP files available
		// we need to null the list as well as free it
		free (menu_mapslist);
		menu_mapslist = NULL;
	}

	if (MapListScrollBox) delete MapListScrollBox;

	std::vector<char *> MapList;

	COM_BuildContentList (MapList, "maps/", ".bsp");

	// set up the array for use
	int listlen = MapList.size ();
	int maplistlen = 0;

	menu_mapslist = (mapinfo_t *) malloc (listlen * sizeof (mapinfo_t));

	// fill it in
	for (int i = 0; i < listlen; i++)
	{
		// if the map doesn't have an info_player entity in it, don't add it to the list
		if (ValidateMap (MapList[i], maplistlen)) maplistlen++;

		// clear down the source vector item as we don't need it any more
		free (MapList[i]);
	}

	// release the vector
	MapList.clear ();

	// set up the scrollbox
	MapListScrollBox = new CScrollBoxProvider (maplistlen, 22, 22);
	MapListScrollBox->SetDrawItemCallback (Menu_MapsOnDraw);
	MapListScrollBox->SetHoverItemCallback (Menu_MapsOnHover);
	MapListScrollBox->SetEnterItemCallback (Menu_MapsOnEnter);

	// need to store this out for freeing above
	num_menumaps = maplistlen;

	// now we also set up a spinbox-compatible list
	if (spinbox_maps) free (spinbox_maps);
	if (spinbox_bsps) free (spinbox_bsps);

	// add 1 for the NULL terminator
	spinbox_maps = (char **) malloc ((num_menumaps + 1) * sizeof (char *));
	spinbox_bsps = (char **) malloc ((num_menumaps + 1) * sizeof (char *));

	for (int i = 0; i < num_menumaps; i++)
	{
		// just copy in the pointers rather than allocating new memory.
		// also set the following item to NULL so that the list is always NULL termed
		spinbox_maps[i] = menu_mapslist[i].mapname;
		spinbox_maps[i + 1] = NULL;

		// we need map names as well as bsp names here...
		spinbox_bsps[i] = menu_mapslist[i].bspname;
		spinbox_bsps[i + 1] = NULL;
	}
}


int Menu_MapsCustomDraw (int y)
{
	if (MapListScrollBox)
		y = MapListScrollBox->DrawItems ((vid.width - 320) / 2 - 24, y);

	return y;
}


void Menu_MapsCustomKey (int k)
{
	if (!MapListScrollBox) return;

	switch (k)
	{
	case K_DOWNARROW:
	case K_UPARROW:
		menu_soundlevel = m_sound_nav;

		// enter item callback needs this...!
	case K_ENTER:
		MapListScrollBox->KeyFunc (k);
		break;
	}
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
	if (demolist)
	{
		for (int i = 0; ; i++)
		{
			if (!demolist[i]) break;
			free (demolist[i]);
		}

		// because there might be no demos we should NULL the list too
		demolist = NULL;
	}

	std::vector<char *> demovector;
	COM_BuildContentList (demovector, "", ".dem");

	// by now it should be obvious what we're doing here...
	int numdemos = demovector.size ();
	demolist = (char **) malloc ((numdemos + 1) * sizeof (char *));

	for (int i = 0; i < numdemos; i++)
	{
		int thisdemolen = strlen (demovector[i]);

		demolist[i] = (char *) malloc (thisdemolen + 1);
		strcpy (demolist[i], demovector[i]);

		for (int j = thisdemolen; j; j--)
		{
			if (demolist[i][j] == '.')
			{
				demolist[i][j] = 0;
				break;
			}
		}

		demolist[i + 1] = NULL;
		free (demovector[i]);
	}

	// set defaults
	demonum = 0;
	old_demonum = -1;
	demo_mapnum = 0;
	demo_skillnum = 1;
	Cvar_Set (&dummy_cdtrack, 2);
}


#define TAG_PLAYTIME	1
#define TAG_RECORD		2
#define TAG_RECORDCMD	3
#define TAG_PLAYONLY	4
#define TAG_TIMEONLY	5

int Menu_DemoCustomDraw1 (int y)
{
	// fix up control visibility
	if (democmd_num == 2)
	{
		if (dummy_demoname.string[0])
			menu_Demo.EnableOptions (TAG_RECORDCMD);
		else menu_Demo.DisableOptions (TAG_RECORDCMD);

		menu_Demo.HideOptions (TAG_PLAYONLY);
		menu_Demo.HideOptions (TAG_TIMEONLY);
		menu_Demo.HideOptions (TAG_PLAYTIME);
		menu_Demo.ShowOptions (TAG_RECORD);
		menu_Demo.ShowOptions (TAG_RECORDCMD);
	}
	else
	{
		if (democmd_num == 0)
		{
			menu_Demo.HideOptions (TAG_TIMEONLY);
			menu_Demo.ShowOptions (TAG_PLAYONLY);
		}
		else
		{
			menu_Demo.ShowOptions (TAG_TIMEONLY);
			menu_Demo.HideOptions (TAG_PLAYONLY);
		}

		menu_Demo.HideOptions (TAG_RECORD);
		menu_Demo.HideOptions (TAG_RECORDCMD);
		menu_Demo.ShowOptions (TAG_PLAYTIME);
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
	FILE *f;
	int msg;
	int msgsize;
	float viewangs[3];

	// try opening on the root
	COM_FOpenFile (va ("%s.dem", demofile), &f);

	// try opening in /demos
	if (!f) COM_FOpenFile (va ("demos/%s.dem", demofile), &f);

	// we expect this to always work
	if (f)
	{
		bool neg = false;
		dsi.cdtrack = 0;

		// read until we get a \n
		while ((msg = getc (f)) != '\n')
			if (msg == '-')
				neg = true;
			else
				dsi.cdtrack = dsi.cdtrack * 10 + (msg - '0');

		if (neg) dsi.cdtrack = -dsi.cdtrack;

		// now start parsing.  we expect this to work on most engines - so far it's been tested on TyrQuake, ProQuake, GLQuake and MHQuake demos.
		// we never know when an engine might try something fancy though... so we play safe and just fail...
		while (1)
		{
			// get the size of the message
			fread (&msgsize, 4, 1, f);

			// read viewangles
			fread (viewangs, sizeof (float), 3, f);

			// read in the message
			byte *msgdata = (byte *) malloc (msgsize);
			fread (msgdata, msgsize, 1, f);

			// parse the message
			for (int msgpos = 0;;)
			{
				// parse out the message id
				msg = msgdata[msgpos++];

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

					/*
					// this might be valid...
					if (dsi.proto_version != PROTOCOL_VERSION)
					{
						fclose (f);
						free (msgdata);
						return false;
					}
					*/

					// done
					fclose (f);
					free (msgdata);
					return true;

				default:
					// unsupported
					fclose (f);
					free (msgdata);
					return false;
				}

				// finished the message
				if (msgpos >= msgsize) break;
			}

			// release the data
			free (msgdata);
		}

		fclose (f);

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
		Draw_Mapshot (va ("maps/%s", spinbox_bsps[demo_mapnum]), (vid.width - 320) / 2 + 96, y);
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

				for (int i = 0; ; i++)
				{
					if (dsi.stuff[i] == '/' || dsi.stuff[i] == '\\')
					{
						// the map name immediately follows the level name
						dsi.mapname = &dsi.stuff[i + 1];
						break;
					}
				}

				for (int i = 0; ; i++)
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
			Draw_Mapshot (va ("maps/%s", dsi.mapname), (vid.width - 320) / 2 + 96, y);
		else Draw_Mapshot (NULL, (vid.width - 320) / 2 + 96, y);

		y += 130;

		if (dsi.levelname)
		{
			// proper positioning
			Menu_PrintCenterWhite (y + 10, dsi.levelname);
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
		for (int i = 0; ; i++)
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


void Menu_InitContentMenu (void)
{
	// maps
	menu_Maps.AddOption (new CQMenuBanner ("gfx/p_load.lmp"));
	menu_Maps.AddOption (new CQMenuTitle ("Load a Map"));
	menu_Maps.AddOption (new CQMenuCustomDraw (Menu_MapsCustomDraw));
	menu_Maps.AddOption (new CQMenuCustomKey (K_UPARROW, Menu_MapsCustomKey));
	menu_Maps.AddOption (new CQMenuCustomKey (K_DOWNARROW, Menu_MapsCustomKey));
	menu_Maps.AddOption (new CQMenuCustomKey (K_ENTER, Menu_MapsCustomKey));

	// demos
	menu_Demo.AddOption (new CQMenuCustomDraw (Menu_DemoCustomDraw1));
	menu_Demo.AddOption (TAG_PLAYTIME, new CQMenuBanner ("gfx/p_load.lmp"));
	menu_Demo.AddOption (TAG_RECORD, new CQMenuBanner ("gfx/p_save.lmp"));
	menu_Demo.AddOption (TAG_PLAYONLY, new CQMenuTitle ("Run an Existing Demo"));
	menu_Demo.AddOption (TAG_TIMEONLY, new CQMenuTitle ("Benchmark an Existing Demo"));
	menu_Demo.AddOption (TAG_RECORD, new CQMenuTitle ("Record a New Demo"));
	menu_Demo.AddOption (new CQMenuSpinControl ("Command Mode", &democmd_num, democmds));
	menu_Demo.AddOption (TAG_PLAYTIME, new CQMenuSpinControl ("Demo File Name", &demonum, &demolist));
	menu_Demo.AddOption (TAG_RECORD, new CQMenuCvarTextbox ("New Demo File Name", &dummy_demoname, TBFLAGS_FILENAMEFLAGS));
	menu_Demo.AddOption (TAG_RECORD, new CQMenuSpacer (DIVIDER_LINE));
	menu_Demo.AddOption (TAG_RECORD, new CQMenuSpacer ("Map to Record From"));
	menu_Demo.AddOption (TAG_RECORD, new CQMenuSpinControl (NULL, &demo_mapnum, &spinbox_maps));
	menu_Demo.AddOption (new CQMenuSpacer (DIVIDER_LINE));
	menu_Demo.AddOption (new CQMenuCustomDraw (Menu_DemoCustomDraw2));
	menu_Demo.AddOption (TAG_RECORD, new CQMenuSpacer (DIVIDER_LINE));
	menu_Demo.AddOption (TAG_RECORD, new CQMenuSpinControl ("Skill", &demo_skillnum, SkillNames));
	menu_Demo.AddOption (TAG_RECORD, new CQMenuSpinControl ("CD Track", &dummy_cdtrack, 2, 11, 1, NULL, NULL));
	menu_Demo.AddOption (new CQMenuSpacer (DIVIDER_LINE));
	menu_Demo.AddOption (TAG_PLAYTIME, new CQMenuCommand ("Begin Playback", Menu_DemoCommand));
	menu_Demo.AddOption (TAG_RECORDCMD, new CQMenuCommand ("Begin Recording", Menu_DemoCommand));
}

