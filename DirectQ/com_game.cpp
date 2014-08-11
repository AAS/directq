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
// common.c -- misc functions used in client and server

#include "quakedef.h"
#include "unzip.h"
#include <shlobj.h>

#include <io.h>

bool com_rmq = false;
int com_numgames = 0;
char *com_games[COM_MAXGAMES] = {NULL};

char com_homedir[260];

// alloc space to copy out the game dirs
// can't send cmd_args in direct as we will be modifying it...
// matches space allocated for cmd_argv in cmd.cpp
// can't alloc this dynamically as it takes down the engine (ouch!) - 80K ain't too bad anyway
static char mygamedirs[0x14050];

// runtime toggleable com_multiuser can put the engine into an infinite loop if cfg settings
// send it ping-ponging back and forth between on and off so we do it as a cmdline instead
bool com_multiuser = false;

void COM_CheckMultiUser (void)
{
	com_multiuser = COM_CheckParm ("-multiuser") ? true : false;
}

/*
void COM_LoadGame (char *gamename);

void COM_SetMultiUser (cvar_t *var)
{
	mygamedirs[0] = 0;

	// store out our current games
	// copy out delimiting with \n so that we can parse the string
	for (int i = 0; i < com_numgames; i++)
	{
		strcat (mygamedirs, com_games[i]);
		strcat (mygamedirs, "\n");
	}

	// reload the game
	COM_LoadGame (mygamedirs);
}


// i'd like this to be 1 but it's 0 for consistency with the original game
cvar_t com_multiuser ("com_multiuser", 0.0f, CVAR_ARCHIVE, COM_SetMultiUser);
*/

void UpdateTitlebarText (char *mapname)
{
	extern HWND d3d_Window;

	if (!mapname)
		SetWindowText (d3d_Window, va ("DirectQ Release %s - Game: %s", DIRECTQ_VERSION, com_gamename));
	else if (cls.demoplayback)
	{
		if (cl.levelname && cl.levelname[0])
			SetWindowText (d3d_Window, va ("DirectQ Release %s - Game: %s - Demo: %s (%s)", DIRECTQ_VERSION, com_gamename, mapname, cl.levelname));
		else SetWindowText (d3d_Window, va ("DirectQ Release %s - Game: %s - Demo: %s", DIRECTQ_VERSION, com_gamename, mapname));
	}
	else
	{
		if (cl.levelname && cl.levelname[0])
			SetWindowText (d3d_Window, va ("DirectQ Release %s - Game: %s - Map: %s (%s)", DIRECTQ_VERSION, com_gamename, mapname, cl.levelname));
		else SetWindowText (d3d_Window, va ("DirectQ Release %s - Game: %s - Map: %s", DIRECTQ_VERSION, com_gamename, mapname));
	}
}


cvar_t com_hipnotic ("com_hipnotic", 0.0f);
cvar_t com_rogue ("com_rogue", 0.0f);
cvar_t com_quoth ("com_quoth", 0.0f);
cvar_t com_nehahra ("com_nehahra", 0.0f);
bool WasInGameMenu = false;

pack_t *COM_LoadPackFile (char *packfile);

/*
=============
COM_ExecQuakeRC

=============
*/
void COM_ExecQuakeRC (void)
{
	// OK, I was stupid and didn't know how Cbuf_InsertText worked.  shoot me.
	Cbuf_InsertText ("exec quake.rc\n");
	return;
}


// stuff we need to drop and reload
void D3DTexture_Release (void);
void Host_WriteConfiguration (void);
void Draw_Init (void);
void HUD_Init (void);
void SCR_Init (void);
void R_InitResourceTextures (void);
void Draw_InvalidateMapshot (void);
void Menu_SaveLoadInvalidate (void);
void S_StopAllSounds (bool clear);
void Mod_ClearAll (void);
void S_ClearSounds (void);
void Menu_MapsPopulate (void);
void Menu_DemoPopulate (void);
void Menu_LoadAvailableSkyboxes (void);
void SHOWLMP_newgame (void);
void D3D_VidRestart_f (void);
void D3DSky_UnloadSkybox (void);
void CL_WipeParticles (void);
void D3DVid_LoseDeviceResources (void);
void D3DVid_RecoverDeviceResources (void);
void Cmd_ClearAlias_f (void);
void D3DMisc_CreatePalette (void);
void D3DMisc_ReleasePalette (void);
void D3DLight_ReleaseLightmaps (void);

void COM_UnloadAllStuff (void)
{
	extern bool scr_initialized;
	extern bool signal_cacheclear;
	extern char lastworldmodel[];

	// for skybox parsing
	lastworldmodel[0] = 0;

	// disconnect from server and update the screen to keep things nice and clean
	CL_Disconnect_f ();
	SCR_UpdateScreen (0);

	// prevent screen updates while changing
	Host_DisableForLoading (true);
	scr_initialized = false;

	// clear cached models
	signal_cacheclear = true;

	// clear everything else
	S_StopAllSounds (true);
	S_ClearSounds ();
	Mod_ClearAll ();

	// drop everything we need to drop
	SAFE_DELETE (GameZone);
	MainCache->Flush ();

	S_ClearSounds ();
	SoundCache->Flush ();

	SHOWLMP_newgame ();
	D3DSky_UnloadSkybox ();
	D3DTexture_Release ();
	CL_WipeParticles ();

	D3DVid_LoseDeviceResources ();
	D3DMisc_ReleasePalette ();
	D3DLight_ReleaseLightmaps ();

	// do this too...
	Host_ClearMemory ();

	// clear all alias commands
	Cmd_ClearAlias_f ();

	// start with a clean filesystem
	com_searchpaths = NULL;
}


void COM_LoadAllStuff (void)
{
	if (!W_LoadPalette ()) Sys_Error ("Could not locate Quake on your computer");
	if (!W_LoadWadFile ("gfx.wad")) Sys_Error ("Could not locate Quake on your computer");

	Draw_Init ();
	HUD_Init ();
	SCR_Init ();
	D3DVid_RecoverDeviceResources ();
	R_InitResourceTextures ();
	D3DMisc_CreatePalette ();

	// now run a screen update to reassure the user that something did actually happen
	SCR_UpdateScreen (0);

	// sprinkle some screen updates through here too to give a better sense of something happening
	Draw_InvalidateMapshot ();
	SCR_UpdateScreen (0);
	Menu_SaveLoadInvalidate ();
	SCR_UpdateScreen (0);
	Menu_MapsPopulate ();
	SCR_UpdateScreen (0);
	Menu_DemoPopulate ();
	SCR_UpdateScreen (0);
	Menu_LoadAvailableSkyboxes ();
	SCR_UpdateScreen (0);

	// force a video restart to flush and sync up everything
	Cbuf_InsertText ("vid_restart\n");
}


/*
================
COM_AddGameDirectory

Sets com_gamedir, adds the directory to the head of the path,
then loads and adds pak1.pak pak2.pak ...
================
*/
void COM_AddGameDirectory (char *dir)
{
	searchpath_t *search;
	char pakfile[MAX_PATH];

	// copy to com_gamedir so that the last gamedir added will be the one used
	Q_strncpy (com_gamedir, dir, 127);
	Q_strncpy (com_gamename, dir, 127);

	for (int i = strlen (com_gamedir); i; i--)
	{
		if (com_gamedir[i] == '/' || com_gamedir[i] == '\\')
		{
			strcpy (com_gamename, &com_gamedir[i + 1]);
			break;
		}
	}

	// store out the names of all currently loaded games
	if (com_numgames != COM_MAXGAMES)
	{
		com_games[com_numgames] = (char *) GameZone->Alloc (strlen (com_gamename) + 1);
		strcpy (com_games[com_numgames], com_gamename);
		com_numgames++;
		com_games[com_numgames] = NULL;
	}

	// update the window titlebar
	UpdateTitlebarText ();

	// add any pak files in the format pak0.pak pak1.pak, ...
	for (int i = 0; i < 10; i++)
	{
		_snprintf (pakfile, 128, "%s/pak%i.pak", dir, i);
		pack_t *pak = COM_LoadPackFile (pakfile);

		if (pak)
		{
			// link it in
			search = (searchpath_t *) GameZone->Alloc (sizeof (searchpath_t));
			search->pack = pak;
			search->pk3 = NULL;
			search->next = com_searchpaths;
			com_searchpaths = search;
		}
		else break;
	}

	// add any other pak files or PK3 files in strict alphabetical order
	WIN32_FIND_DATA FindFileData;
	HANDLE hFind = INVALID_HANDLE_VALUE;

	// look for a file (take all files so that we can also load PK3s)
	_snprintf (pakfile, 128, "%s/*.*", dir);
	hFind = FindFirstFile (pakfile, &FindFileData);

	if (hFind == INVALID_HANDLE_VALUE)
	{
		// found no files
		FindClose (hFind);
	}
	else
	{
		// add all the pak files
		do
		{
			// skip over PAK files already loaded
			if (!_stricmp (FindFileData.cFileName, "pak0.pak")) continue;
			if (!_stricmp (FindFileData.cFileName, "pak1.pak")) continue;
			if (!_stricmp (FindFileData.cFileName, "pak2.pak")) continue;
			if (!_stricmp (FindFileData.cFileName, "pak3.pak")) continue;
			if (!_stricmp (FindFileData.cFileName, "pak4.pak")) continue;
			if (!_stricmp (FindFileData.cFileName, "pak5.pak")) continue;
			if (!_stricmp (FindFileData.cFileName, "pak6.pak")) continue;
			if (!_stricmp (FindFileData.cFileName, "pak7.pak")) continue;
			if (!_stricmp (FindFileData.cFileName, "pak8.pak")) continue;
			if (!_stricmp (FindFileData.cFileName, "pak9.pak")) continue;

			// catch file copies for backup purposes
			if (!_strnicmp (FindFileData.cFileName, "Copy of ", 8)) continue;

			// send through the appropriate loader
			if (COM_FindExtension (FindFileData.cFileName, ".pak"))
			{
				// load the pak file
				_snprintf (pakfile, 128, "%s/%s", dir, FindFileData.cFileName);
				pack_t *pak = COM_LoadPackFile (pakfile);

				if (pak)
				{
					// link it in
					search = (searchpath_t *) GameZone->Alloc (sizeof (searchpath_t));
					search->pack = pak;
					search->pk3 = NULL;
					search->next = com_searchpaths;
					com_searchpaths = search;
				}
			}
			else if (COM_FindExtension (FindFileData.cFileName, ".pk3"))
			{
				// removed .zip file handling - Spike doesn't like it and he's probably right
				unzFile			uf;
				int				err;
				unz_global_info gi;
				unz_file_info	file_info;

				// load the pak file
				_snprintf (pakfile, 128, "%s/%s", dir, FindFileData.cFileName);
				uf = unzOpen (pakfile);
				err = unzGetGlobalInfo (uf, &gi);

				if (err == UNZ_OK)
				{
					pk3_t *pk3 = (pk3_t *) GameZone->Alloc (sizeof (pk3_t));
					char filename_inzip[64];
					int good_files = 0;

					pk3->numfiles = gi.number_entry;
					Q_strncpy (pk3->filename, pakfile, 127);
					pk3->files = (packfile_t *) GameZone->Alloc (sizeof (packfile_t) * pk3->numfiles);

					unzGoToFirstFile (uf);

					for (int i = 0; i < gi.number_entry; i++)
					{
						err = unzGetCurrentFileInfo (uf, &file_info, filename_inzip, sizeof (filename_inzip), NULL, 0, NULL, 0);

						if (err == UNZ_OK)
						{
							// pos is unused
							Q_strncpy (pk3->files[i].name, filename_inzip, 63);
							pk3->files[i].filelen = file_info.uncompressed_size;
							pk3->files[i].filepos = 0;

							// flag a good file here
							good_files++;
						}
						else
						{
							// bad entry
							pk3->files[i].name[0] = 0;
							pk3->files[i].filelen = 0;
							pk3->files[i].filepos = 0;
						}

						unzGoToNextFile (uf);
					}

					if (good_files)
					{
						// link it in
						search = (searchpath_t *) GameZone->Alloc (sizeof (searchpath_t));
						search->pack = NULL;
						search->pk3 = pk3;
						search->next = com_searchpaths;
						com_searchpaths = search;
						Con_SafePrintf ("Added packfile %s (%i files)\n", pk3->filename, pk3->numfiles);
					}
				}

				unzClose (uf);
			}
		} while (FindNextFile (hFind, &FindFileData));

		// close the finder
		FindClose (hFind);
	}

	// add the directory to the search path
	// this is done last as using a linked list will search in the reverse order to which they
	// are added, so we ensure that the filesystem overrides pak files, which is what we want
	search = (searchpath_t *) GameZone->Alloc (sizeof (searchpath_t));
	Q_strncpy (search->filename, dir, 127);
	search->next = com_searchpaths;
	search->pack = NULL;
	search->pk3 = NULL;
	com_searchpaths = search;
}


bool COM_ValidateGamedir (char *basedir, char *gamename)
{
	WIN32_FIND_DATA FindFileData;
	HANDLE hFind = INVALID_HANDLE_VALUE;
	char find_filter[MAX_PATH];
	bool isgamedir = false;

	_snprintf (find_filter, 260, "%s/%s/*", basedir, gamename);

	for (int i = 0;; i++)
	{
		if (find_filter[i] == 0) break;
		if (find_filter[i] == '/') find_filter[i] = '\\';
	}

	// try to find the first file in the path; any file or directory will do
	hFind = FindFirstFile (find_filter, &FindFileData);

	// now see what we got
	if (hFind == INVALID_HANDLE_VALUE)
		isgamedir = false;
	else isgamedir = true;

	// close the finder
	FindClose (hFind);

	return isgamedir;
}


char *rmqmaps[] =
{
	"maps/e1m1rq.bsp", "maps/e1m2rq.bsp", "maps/e1m3rq.bsp", "maps/e1m4rq.bsp", "maps/e1m5rq.bsp", "maps/e1m6rq.bsp", "maps/e1m7rq.bsp", "maps/e1m8rq.bsp",
	"maps/e2m1rq.bsp", "maps/e2m2rq.bsp", "maps/e2m3rq.bsp", "maps/e2m4rq.bsp", "maps/e2m5rq.bsp", "maps/e2m6rq.bsp", "maps/e2m7rq.bsp", "maps/e2m8rq.bsp",
	"maps/e3m1rq.bsp", "maps/e3m2rq.bsp", "maps/e3m3rq.bsp", "maps/e3m4rq.bsp", "maps/e3m5rq.bsp", "maps/e3m6rq.bsp", "maps/e3m7rq.bsp", "maps/e3m8rq.bsp",
	"maps/e4m1rq.bsp", "maps/e4m2rq.bsp", "maps/e4m3rq.bsp", "maps/e4m4rq.bsp", "maps/e4m5rq.bsp", "maps/e4m6rq.bsp", "maps/e4m7rq.bsp", "maps/e4m8rq.bsp",
	NULL
};


bool COM_RMQMapInPack (packfile_t *files, int numfiles)
{
	for (int i = 0; i < numfiles; i++)
	{
		for (int m = 0; ; m++)
		{
			if (!rmqmaps[m]) break;
			if (!_stricmp (files[i].name, rmqmaps[m])) return true;
		}
	}

	return false;
}


void COM_DetectRMQ (void)
{
	com_rmq = false;

	for (searchpath_t *search = com_searchpaths; search; search = search->next)
	{
		if (search->pack)
		{
			if (COM_RMQMapInPack (search->pack->files, search->pack->numfiles))
			{
				com_rmq = true;
				return;
			}
		}
		else if (search->pk3)
		{
			if (COM_RMQMapInPack (search->pk3->files, search->pk3->numfiles))
			{
				com_rmq = true;
				return;
			}
		}
		else
		{
			char netpath[MAX_PATH];

			for (int m = 0; ; m++)
			{
				if (!rmqmaps[m]) break;

				// check for a file in the directory tree
				_snprintf (netpath, 256, "%s/%s", search->filename, rmqmaps[m]);

				// quick check
				if (_access (netpath, 04) != -1)
				{
					com_rmq = true;
					return;
				}
			}
		}
	}
}


void COM_LoadGame (char *gamename)
{
	// no games to begin with
	com_numgames = 0;

	for (int i = 0; i < COM_MAXGAMES; i++) com_games[i] = NULL;

	if (host_initialized)
	{
		// store out our configuration before we go to the new game
		Host_WriteConfiguration ();

		// unload everything
		COM_UnloadAllStuff ();
	}

	if (!GameZone) GameZone = new CQuakeZone ();

	char basedir[MAX_PATH];

	// -basedir <path>
	// Overrides the system supplied base directory (under GAMENAME)
	// this is always active for the entire session
	int bd = COM_CheckParm ("-basedir");

	if (bd && bd < com_argc - 1)
		Q_strncpy (basedir, com_argv[bd + 1], 127);
	else Q_strncpy (basedir, host_parms.basedir, 127);

	int j = strlen (basedir);

	if (j > 0)
	{
		// remove terminating slash
		if ((basedir[j - 1] == '\\') || (basedir[j - 1] == '/'))
			basedir[j - 1] = 0;
	}

	// allow -game rogue/hipnotic/quoth as well as -rogue/hipnotic/quoth here
	if (COM_StringContains (gamename, "rogue")) Cvar_Set (&com_rogue, 1);
	if (COM_StringContains (gamename, "hipnotic")) Cvar_Set (&com_hipnotic, 1);
	if (COM_StringContains (gamename, "quoth")) Cvar_Set (&com_quoth, 1);
	if (COM_StringContains (gamename, "nehahra")) Cvar_Set (&com_nehahra, 1);

	// check status of add-ons; nothing yet...
	rogue = hipnotic = quoth = nehahra = false;
	standard_quake = true;

	if (com_rogue.value)
	{
		rogue = true;
		standard_quake = false;
	}

	if (com_hipnotic.value)
	{
		hipnotic = true;
		standard_quake = false;
	}

	if (com_quoth.value)
	{
		quoth = true;
		standard_quake = false;
	}

	if (com_nehahra.value)
	{
		nehahra = true;
		standard_quake = false;
	}

	// now add the base directory (ID1) (lowest priority)
	COM_AddGameDirectory (va ("%s/%s", basedir, GAMENAME));

	// add these in the same order as ID do (mission packs always get second-lowest priority)
	if (rogue) COM_AddGameDirectory (va ("%s/rogue", basedir));
	if (hipnotic) COM_AddGameDirectory (va ("%s/hipnotic", basedir));
	if (quoth) COM_AddGameDirectory (va ("%s/quoth", basedir));
	if (nehahra) COM_AddGameDirectory (va ("%s/nehahra", basedir));

	// add any other games in the list (everything else gets highest priority)
	char *thisgame = gamename;
	char *nextgame = gamename;

	for (;;)
	{
		// no more games
		if (!thisgame) break;
		if (!thisgame[0]) break;

		// find start pointer to next game
		for (int i = 0;; i++)
		{
			if (thisgame[i] == 0)
			{
				// end of list
				nextgame = &thisgame[i];
				break;
			}

			if (thisgame[i] == '\n')
			{
				// character after delimiter
				nextgame = &thisgame[i + 1];
				thisgame[i] = 0;
				break;
			}
		}

		// if false the game has already been loaded and so we don't load it again
		bool loadgame = true;

		// check for games already loaded
		if (!_stricmp (thisgame, "rogue")) loadgame = false;
		if (!_stricmp (thisgame, "hipnotic")) loadgame = false;
		if (!_stricmp (thisgame, "quoth")) loadgame = false;
		if (!_stricmp (thisgame, "nehahra")) loadgame = false;
		if (!_stricmp (thisgame, GAMENAME)) loadgame = false;

		// check is it actually a proper directory
		// this is because i'm a fucking butterfingers and always type stuff wrong :)
		if (!COM_ValidateGamedir (basedir, thisgame)) loadgame = false;

		// only load it if it hasn't already been loaded
		if (loadgame)
		{
			// do something interesting with thisgame
			Con_SafePrintf ("Loading Game: \"%s\"...\n", thisgame);
			COM_AddGameDirectory (va ("%s/%s", basedir, thisgame));
		}

		// go to next game
		thisgame = nextgame;
	}

	// hack to get the hipnotic sbar in quoth
	if (quoth) hipnotic = true;

	if (com_multiuser)
	{
		// optionally link My Documents folder in for multiuser/non-admin/network support
		SHGetFolderPath (NULL, CSIDL_PERSONAL, NULL, SHGFP_TYPE_CURRENT, com_homedir);
		strcat (com_homedir, "\\DirectQ");

		// com_gamedir needs to be loaded too so that settings for multiple games don't trample each other
		for (int i = strlen (com_gamedir) - 1; i; i--)
		{
			if (com_gamedir[i] == '/' || com_gamedir[i] == '\\')
			{
				strcat (com_homedir, &com_gamedir[i]);
				break;
			}
		}

		Sys_mkdir (com_homedir);
		COM_AddGameDirectory (com_homedir);
	}

	COM_DetectRMQ ();

	// if the host isn't already up, don't bring anything up yet
	// (bring the host loader through here as well)
	if (!host_initialized) return;

	// reload everything that needs to be reloaded
	COM_LoadAllStuff ();

	Con_SafePrintf ("\n");

	if (WasInGameMenu)
	{
		// toggle the menu if we called this from the menu
		Cbuf_InsertText ("togglemenu\n");
		WasInGameMenu = false;
	}

	// reload the configs as they may have changed
	COM_ExecQuakeRC ();

	Cbuf_Execute ();

	// not disabled any more
	Host_DisableForLoading (false);

	// force a stop of the demo loop in case we change while the game is running
	cls.demonum = -1;
}


void COM_Game_f (void)
{
	extern bool draw_init;

	draw_init = false;

	if (Cmd_Argc () < 2)
	{
		// this can come in from either a "game" or a "gamedir" command, so notify the user of the command they actually issued
		Con_Printf ("%s <gamename> <gamename> <gamename>...\nchanges the currently loaded game\n", Cmd_Argv (0));
		WasInGameMenu = false;
		return;
	}

	mygamedirs[0] = 0;

	// copy out delimiting with \n so that we can parse the string
	for (int i = 1; i < Cmd_Argc (); i++)
	{
		// we made sure that we had enough space above so we don't need to check for overflow here.
		strcat (mygamedirs, Cmd_Argv (i));

		// don't forget the delimiter!
		strcat (mygamedirs, "\n");
	}

	// load using the generated gamedirs string
	COM_LoadGame (mygamedirs);
	WasInGameMenu = false;
}


// qrack uses gamedir
cmd_t COM_Game_Cmd ("game", COM_Game_f);
cmd_t COM_GameDir_Cmd ("gamedir", COM_Game_f);

