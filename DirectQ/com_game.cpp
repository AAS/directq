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

cvar_t com_hipnotic ("com_hipnotic", 0.0f);
cvar_t com_rogue ("com_rogue", 0.0f);
cvar_t com_quoth ("com_quoth", 0.0f);
cvar_t com_nehahra ("com_nehahra", 0.0f);
bool WasInGameMenu = false;

void D3D_EnumExternalTextures (void);
pack_t *COM_LoadPackFile (char *packfile);

/*
=============
COM_ExecQuakeRC

This is a HUGE hack to inject an "exec directq.cfg" command after the "exec config.cfg" in a quake.rc file
and is provided for mods that use a custom quake.rc containing commands of their own.
=============
*/
void COM_ExecQuakeRC (void)
{
	char *rcfile = (char *) COM_LoadFile ("quake.rc");

	// didn't find it
	if (!rcfile) return;

	// alloc a new buffer to hold the new RC file.
	// this should give sufficient space even if it only contains a single "exec config.cfg"
	int len = strlen (rcfile) * 3;

	// alloc a new buffer including space for "exec directq.cfg"
	char *newrc = (char *) Zone_Alloc (len);
	char *oldrc = rcfile;
	char *rcnew = newrc;

	newrc[0] = 0;

	bool incomment = false;

	// this breaks with quoth's quake.rc...
	while (1)
	{
		// end of the file
		if (!(*oldrc))
		{
			*rcnew = 0;
			break;
		}

		// detect comments
		if (!strncmp (oldrc, "//", 2)) incomment = true;
		if (oldrc[0] == '\n') incomment = false;

		// look for config.cfg - there might be 2 or more spaces between exec and the filename!!!
		if (!strnicmp (oldrc, "config.cfg", 10) && !incomment)
		{
			// copy in the new exec statement, ensure that it's on the same line in
			// case the config.cfg entry is in a comment
			strcpy (&rcnew[0], "config.cfg;exec directq.cfg");

			// skip over
			rcnew += 27;
			oldrc += 10;
			continue;
		}

		// copy in text
		*rcnew++ = *oldrc++;
	}

	Cbuf_InsertText (newrc);
	Zone_Free (rcfile);
	Zone_Free (newrc);
}


// stuff we need to drop and reload
void D3D_ReleaseTextures (void);
void Host_WriteConfiguration (void);
void Draw_Init (void);
void HUD_Init (void);
void SCR_Init (void);
void R_InitResourceTextures (void);
void D3D_Init3DSceneTexture (void);
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
void D3D_InitHLSL (void);
void R_UnloadSkybox (void);
void Snd_Restart_f (void);

void COM_UnloadAllStuff (void)
{
	extern bool scr_initialized;
	extern bool signal_cacheclear;

	// disconnect from server and update the screen to keep things nice and clean
	CL_Disconnect_f ();
	SCR_UpdateScreen ();

	// prevent screen updates while changing
	scr_disabled_for_loading = true;
	scr_initialized = false;

	// clear cached models
	signal_cacheclear = true;

	// clear everything else
	S_StopAllSounds (true);
	Mod_ClearAll ();
	S_ClearSounds ();

	// drop everything we need to drop
	SAFE_DELETE (GameZone);
	MainCache->Flush ();

	SoundCache->Flush ();
	S_ClearSounds ();

	Snd_Restart_f ();

	SHOWLMP_newgame ();
	R_UnloadSkybox ();
	D3D_ReleaseTextures ();

	// do this too...
	Host_ClearMemory ();

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
	R_InitResourceTextures ();
	D3D_Init3DSceneTexture ();
	D3D_InitHLSL ();
	Draw_InvalidateMapshot ();
	Menu_SaveLoadInvalidate ();
	Menu_MapsPopulate ();
	Menu_DemoPopulate ();
	Menu_LoadAvailableSkyboxes ();
}


/*
================
COM_AddGameDirectory

Sets com_gamedir, adds the directory to the head of the path,
then loads and adds pak1.pak pak2.pak ... 
================
*/
int com_numgames = 0;
char *com_games[COM_MAXGAMES] = {NULL};

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
	extern HWND d3d_Window;
	SetWindowText (d3d_Window, va ("DirectQ Release %s - %s", DIRECTQ_VERSION, com_gamename));

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

	// add any other pak files, zip files or PK3 files in strict alphabetical order
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
			if (!stricmp (FindFileData.cFileName, "pak0.pak")) continue;
			if (!stricmp (FindFileData.cFileName, "pak1.pak")) continue;
			if (!stricmp (FindFileData.cFileName, "pak2.pak")) continue;
			if (!stricmp (FindFileData.cFileName, "pak3.pak")) continue;
			if (!stricmp (FindFileData.cFileName, "pak4.pak")) continue;
			if (!stricmp (FindFileData.cFileName, "pak5.pak")) continue;
			if (!stricmp (FindFileData.cFileName, "pak6.pak")) continue;
			if (!stricmp (FindFileData.cFileName, "pak7.pak")) continue;
			if (!stricmp (FindFileData.cFileName, "pak8.pak")) continue;
			if (!stricmp (FindFileData.cFileName, "pak9.pak")) continue;

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
			else if (COM_FindExtension (FindFileData.cFileName, ".zip") || COM_FindExtension (FindFileData.cFileName, ".pk3"))
			{
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
	// are added, so we ensure that the filesystem overrides pak files
	search = (searchpath_t *) GameZone->Alloc (sizeof(searchpath_t));
	Q_strncpy (search->filename, dir, 127);
	search->next = com_searchpaths;
	search->pack = NULL;
	search->pk3 = NULL;
	com_searchpaths = search;
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
	int i = COM_CheckParm ("-basedir");

	if (i && i < com_argc-1)
		Q_strncpy (basedir, com_argv[i + 1], 127);
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
		if (!stricmp (thisgame, "rogue")) loadgame = false;
		if (!stricmp (thisgame, "hipnotic")) loadgame = false;
		if (!stricmp (thisgame, "quoth")) loadgame = false;
		if (!stricmp (thisgame, "nehahra")) loadgame = false;
		if (!stricmp (thisgame, GAMENAME)) loadgame = false;

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

	// make directories we need
	Sys_mkdir ("save");
	Sys_mkdir ("screenshot");

	// enum and register external textures
	D3D_EnumExternalTextures ();

	// if the host isn't already up, don't bring anything up yet
	// (fixme - bring the host loader through here as well)
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
	scr_disabled_for_loading = false;

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

	// alloc space to copy out the game dirs
	// can't send cmd_args in direct as we will be modifying it...
	// matches space allocated for cmd_argv in cmd.cpp
	// can't alloc this dynamically as it takes down the engine (ouch!) - 80K ain't too bad anyway
	static char gamedirs[81920];
	gamedirs[0] = 0;

	// copy out delimiting with \n so that we can parse the string
	for (int i = 1; i < Cmd_Argc (); i++)
	{
		// we made sure that we had enough space above so we don't need to check for overflow here.
		strcat (gamedirs, Cmd_Argv (i));

		// don't forget the delimiter!
		strcat (gamedirs, "\n");
	}

	// load using the generated gamedirs string
	COM_LoadGame (gamedirs);
	WasInGameMenu = false;
}


// qrack uses gamedir
cmd_t COM_Game_Cmd ("game", COM_Game_f);
cmd_t COM_GameDir_Cmd ("gamedir", COM_Game_f);

