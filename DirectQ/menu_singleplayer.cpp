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


cvar_t host_savenamebase ("host_savenamebase", "save_", CVAR_ARCHIVE);
extern cvar_t host_savedir;
extern char *SkillNames[];

/*
========================================================================================================================

					SINGLE PLAYER MENU

========================================================================================================================
*/

extern cvar_t com_nehahra;

void Menu_SPNewGame (void)
{
	if (sv.active)
	{
		if (!SCR_ModalMessage ("Are you sure you want to\nstart a new game?\n", "Confirm New Game", MB_YESNO))
			return;
	}

	key_dest = key_game;

	if (sv.active) Cbuf_AddText ("disconnect\n");

	// ensure cvars are appropriate for SP
	Cvar_Set ("deathmatch", 0.0f);
	Cvar_Set ("coop", 0.0f);
	Cvar_Set ("teamplay", 0.0f);

	// switch back to skill 1
	Cvar_Set ("skill", 1.0f);

	Cbuf_AddText ("maxplayers 1\n");

	// different start map (BASTARDS!)
	if (com_nehahra.value)
		Cbuf_AddText ("map nehstart\n");
	else Cbuf_AddText ("map start\n");

	Cbuf_Execute ();
}


void Menu_InitSPMenu (void)
{
	menu_Singleplayer.AddOption (new CQMenuBanner ("gfx/ttl_sgl.lmp"));
	menu_Singleplayer.AddOption (MENU_TAG_FULL, new CQMenuTitle ("Single Player Options"));
	menu_Singleplayer.AddOption (MENU_TAG_FULL, new CQMenuCommand ("Start a New Game", Menu_SPNewGame));
	menu_Singleplayer.AddOption (MENU_TAG_FULL, new CQMenuSpacer (DIVIDER_LINE));
	menu_Singleplayer.AddOption (MENU_TAG_FULL, new CQMenuSubMenu ("Load a Previously Saved Game", &menu_Load));
	menu_Singleplayer.AddOption (MENU_TAG_FULL, new CQMenuSpacer ());
	menu_Singleplayer.AddOption (MENU_TAG_FULL, new CQMenuSubMenu ("Save the Current Game", &menu_Save));

	menu_Singleplayer.AddOption (MENU_TAG_SIMPLE, new CQMenuSpacer (DIVIDER_LINE));
	menu_Singleplayer.AddOption (MENU_TAG_SIMPLE, new CQMenuCursorSubMenu (Menu_SPNewGame));
	menu_Singleplayer.AddOption (MENU_TAG_SIMPLE, new CQMenuCursorSubMenu (&menu_Load));
	menu_Singleplayer.AddOption (MENU_TAG_SIMPLE, new CQMenuCursorSubMenu (&menu_Save));
	menu_Singleplayer.AddOption (MENU_TAG_SIMPLE, new CQMenuChunkyPic ("gfx/sp_menu.lmp"));
}


/*
========================================================================================================================

					SAVE/LOAD MENUS

========================================================================================================================
*/

#define MAX_SAVE_DISPLAY	20
int NumSaves = 0;

bool savelistchanged = true;

CScrollBoxProvider *SaveScrollbox = NULL;
CScrollBoxProvider *LoadScrollbox = NULL;
CScrollBoxProvider *ActiveScrollbox = NULL;

void Menu_SaveLoadOnDraw (int y, int itemnum);
void Menu_SaveLoadOnHover (int initialy, int y, int itemnum);
void Menu_SaveLoadOnEnter (int itemnum);
void Menu_SaveLoadOnDelete (int itemnum);


typedef struct save_game_info_s
{
	char mapname[SAVEGAME_COMMENT_LENGTH + 1];
	char kills[64];
	int skill;
	char time[64];
	char secrets[64];
	char savetime[64];
	char filename[MAX_PATH];
} save_game_info_t;


// the old version was written before i was really comfortable using COM_Parse and dates back quite a few years
void Menu_ParseSaveInfo (FILE *f, char *filename, save_game_info_t *si)
{
	// blank the save info
	memset (si, 0, sizeof (save_game_info_t));

	// copy the file name
	strcpy (si->filename, filename);

	// set up intial kills and secrets
	si->kills[0] = '0';
	si->secrets[0] = '0';

	// the version was already checked coming into here so first thing we get is the savegame comment
	char str[32768], *start;

	// read the comment
	fscanf (f, "%s\n", str);

	// hack out the level name
	for (int i = strlen (str); i >= 0; i--)
	{
		// convert '_' back to ' '
		if (str[i] == '_') str[i] = ' ';

		// null term after map name
		if (!strnicmp (&str[i], "kills:", 6)) str[i] = 0;
	}

	// trim trailing spaces
	for (int i = strlen (str) - 1; i >= 0; i--)
	{
		if (str[i] != ' ')
		{
			str[i + 1] = 0;
			break;
		}
	}

	// copy in the map name
	strncpy (si->mapname, str, SAVEGAME_COMMENT_LENGTH);

	// these exist to soak up data we skip over
	float fsoak;
	int isoak;
	char csoak[256];

	// skip spawn parms
	for (int i = 0; i < NUM_SPAWN_PARMS; i++) fscanf (f, "%f\n", &fsoak);

	// skill is up next
	// read skill as a float, and convert to int
	fscanf (f, "%f\n", &fsoak);
	si->skill = (int) (fsoak + 0.1);

	// sanity check (in case the save is manually hacked...)
	if (si->skill > 3) si->skill = 3;
	if (si->skill < 0) si->skill = 0;

	// read bsp mapname
	fscanf (f, "%s\n", str);

	if (!si->mapname[0] || si->mapname[0] == 32)
	{
		// not every map has a friendly name
		strncpy (si->mapname, str, SAVEGAME_COMMENT_LENGTH);
		si->mapname[22] = 0;
	}

	// now read time as a float
	fscanf (f, "%f\n", &fsoak);

	// convert fsoak time to real time
	_snprintf (si->time, 64, "%02i:%02i", ((int) fsoak) / 60, (int) fsoak - (((int) fsoak) / 60) * 60);

	// skip lightstyles
	for (int i = 0; i < MAX_LIGHTSTYLES; i++) fscanf (f, "%s\n", str);

	// init counts
	int num_kills = 0;
	int total_kills = 0;
	int num_secrets = 0;
	int total_secrets = 0;

	// read the first edict - this will be the globalvars containing the info about kills and secrets
	for (int i = 0; i < 32760; i++)
	{
		// read and always null-term it
		str[i] = fgetc (f);
		str[i + 1] = 0;

		// done
		if (str[i] == EOF || !str[i])
		{
			str[i] = 0;
			break;
		}

		// done
		if (str[i] == '}')
		{
			str[i + 1] = 0;
			break;
		}
	}

	// no start yet
	start = NULL;

	// locate the opening brace
	for (int i = 0; ; i++)
	{
		if (str[i] == '{')
		{
			start = &str[i];
			break;
		}

		if (str[i] == 0) break;
	}

	// only parse if we have a start pointer
	if (start)
	{
		// p
		start = COM_Parse (start);

		// check
		if (com_token[0] && !strcmp (com_token, "{"))
		{
			char keyname[64];

			while (1)
			{
				// parse the key
				start = COM_Parse (start);

				// end
				if (com_token[0] == '}') break;
				if (!start) break;

				// copy out key
				strncpy (keyname, com_token, 63);

				// parse the value
				start = COM_Parse (start);

				// fail silently
				if (!start) break;
				if (com_token[0] == '}') break;

				// interpret - these are stored as floats in the save file
				if (!stricmp (keyname, "total_secrets")) total_secrets = (int) (atof (com_token) + 0.1f);
				if (!stricmp (keyname, "found_secrets")) num_secrets = (int) (atof (com_token) + 0.1f);
				if (!stricmp (keyname, "total_monsters")) total_kills = (int) (atof (com_token) + 0.1f);
				if (!stricmp (keyname, "killed_monsters")) num_kills = (int) (atof (com_token) + 0.1f);
			}
		}
	}

	// write out counts
	if (total_kills)
		_snprintf (si->kills, 64, "%i/%i", num_kills, total_kills);
	else _snprintf (si->kills, 64, "%i", num_kills);

	if (total_secrets)
		_snprintf (si->secrets, 64, "%i/%i", num_secrets, total_secrets);
	else _snprintf (si->secrets, 64, "%i", num_secrets);

	// because f came into here already opened
	fclose (f);

	// now we set up the time and date of the save file for display
	char name2[256];
	SYSTEMTIME st;

	// set up the file name for opening
	_snprintf (name2, 256, "%s/%s/%s", com_gamedir, host_savedir.string, filename);

	// open it again (isn't the Windows API beautiful?)
	HANDLE hFile = CreateFile (name2, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

	if (!hFile) return;

	BY_HANDLE_FILE_INFORMATION FileInfo;

	// retrieve the info
	GetFileInformationByHandle (hFile, &FileInfo);

	// use the last write time so that it's always valid for the last save
	// there's no FileTimeToLocalTime - grrrrr...!
	FILETIME lft;
	FileTimeToLocalFileTime (&FileInfo.ftLastWriteTime, &lft);
	FileTimeToSystemTime (&lft, &st);

	_snprintf (si->savetime, 64, "%04i/%02i/%02i %02i:%02i", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);

	// close the file handle
	COM_FCloseFile (&hFile);
}


class CSaveInfo
{
public:
	CSaveInfo (FILE *f, char *filename)
	{
		Menu_ParseSaveInfo (f, filename, &this->SaveInfo);
		NumSaves++;
	}

	void UpdateItem (void)
	{
		if (!this->SaveInfo.filename[0]) return;

		// update anything that could change (fixme - also update time when we fix it up above)
		_snprintf (this->SaveInfo.mapname, 40, "%s", cl.levelname);

		// remake quake compatibility
		if (cl.stats[STAT_TOTALMONSTERS] == 0)
			_snprintf (this->SaveInfo.kills, 64, "Kills:   %i", cl.stats[STAT_MONSTERS]);
		else _snprintf (this->SaveInfo.kills, 64, "Kills:   %i/%i", cl.stats[STAT_MONSTERS], cl.stats[STAT_TOTALMONSTERS]);

		// remake quake compatibility
		if (cl.stats[STAT_TOTALSECRETS] == 0)
			_snprintf (this->SaveInfo.secrets, 64, "Secrets: %i", cl.stats[STAT_SECRETS]);
		else _snprintf (this->SaveInfo.secrets, 64, "Secrets: %i/%i", cl.stats[STAT_SECRETS], cl.stats[STAT_TOTALSECRETS]);

		this->SaveInfo.skill = (int) skill.value;
		_snprintf (this->SaveInfo.time, 64, "%02i:%02i", (int) (cl.time / 60), (int) (cl.time - (int) (cl.time / 60) * 60));
	}

	CSaveInfo (void)
	{
		strcpy (this->SaveInfo.mapname, "<< New SaveGame >>");

		// hack - we'll use this to identift this item in the OnHover function
		this->SaveInfo.filename[0] = 0;
		NumSaves++;
	}

	~CSaveInfo ()
	{
		// cascade destructors along the list
		SAFE_DELETE (this->Next);
	}

	CSaveInfo *Next;
	save_game_info_t SaveInfo;
};


CSaveInfo *SaveInfoList = NULL;
CSaveInfo **SaveInfoArray = NULL;
CSaveInfo **ActiveSaveInfoArray = NULL;


void Menu_SaveLoadAddSave (WIN32_FIND_DATA *savefile)
{
	// not interested in these types
	if (savefile->dwFileAttributes & FILE_ATTRIBUTE_COMPRESSED) return;
	if (savefile->dwFileAttributes & FILE_ATTRIBUTE_ENCRYPTED) return;
	if (savefile->dwFileAttributes & FILE_ATTRIBUTE_OFFLINE) return;
	if (savefile->dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) return;
	if (savefile->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return;

	// attempt to open it
	// FUCK YOU!  I forgot to add in host_savedir here...
	FILE *f = fopen (va ("%s\\%s\\%s", com_gamedir, host_savedir.string, savefile->cFileName), "r");
	int version;

	// failed to open (we don't expect this to happen)
	if (!f) return;

	// check the version
	fscanf (f, "%i\n", &version);

	// check the version
	if (version != SAVEGAME_VERSION) return;

	CSaveInfo *si = new CSaveInfo (f, savefile->cFileName);

	// chain in reverse order so that most recent will be on top
	si->Next = SaveInfoList;
	SaveInfoList = si;
}


// for autocompletion
char **saveloadlist = NULL;

void Menu_SaveLoadScanSaves (void)
{
	if (!savelistchanged) return;

	// destroy the previous list
	if (SaveInfoList)
	{
		delete SaveInfoList;
		SaveInfoList = NULL;
	}

	if (SaveInfoArray)
	{
		Zone_Free (SaveInfoArray);
		SaveInfoArray = NULL;
		ActiveSaveInfoArray = NULL;
	}

	if (SaveScrollbox)
	{
		delete SaveScrollbox;
		SaveScrollbox = NULL;
	}

	if (LoadScrollbox)
	{
		delete LoadScrollbox;
		LoadScrollbox = NULL;
	}

	if (saveloadlist)
	{
		for (int i = 0; ; i++)
		{
			if (!saveloadlist[i]) break;
			Zone_Free (saveloadlist[i]);
		}

		Zone_Free (saveloadlist);
	}

	ActiveScrollbox = NULL;

	// no saves yet
	NumSaves = 0;

	// make the save directory
	COM_CheckContentDirectory (&host_savedir, true);

	WIN32_FIND_DATA FindFileData;
	HANDLE hFind = INVALID_HANDLE_VALUE;

	// look for a file
	hFind = FindFirstFile (va ("%s\\%s\\*.sav", com_gamedir, host_savedir.string), &FindFileData);

	if (hFind == INVALID_HANDLE_VALUE)
	{
		// found no files
		FindClose (hFind);
	}
	else
	{
		// add all the items
		do {Menu_SaveLoadAddSave (&FindFileData);} while (FindNextFile (hFind, &FindFileData));

		// close the finder
		FindClose (hFind);
	}

	// add the first item for the new save item
	CSaveInfo *si = new CSaveInfo ();
	si->Next = SaveInfoList;
	SaveInfoList = si;

	// now put them all in an array for easy access
	SaveInfoArray = (CSaveInfo **) Zone_Alloc (sizeof (CSaveInfo *) * NumSaves);
	int SaveIndex = 0;
	int slindex = 0;

	// for the autocomplete list - add 1 for null termination
	saveloadlist = (char **) Zone_Alloc (sizeof (char *) * (NumSaves + 1));
	saveloadlist[0] = NULL;

	for (CSaveInfo *si = SaveInfoList; si; si = si->Next)
	{
		SaveInfoArray[SaveIndex] = si;

		// only add games that have a filename
		if (si->SaveInfo.filename[0])
		{
			// add to the null terminated autocompletion list
			saveloadlist[slindex] = (char *) Zone_Alloc (strlen (si->SaveInfo.filename) + 1);
			strcpy (saveloadlist[slindex], si->SaveInfo.filename);

			// remove the .sav extension from the list entry
			for (int i = strlen (saveloadlist[slindex]); i; i--)
			{
				if (!stricmp (&saveloadlist[slindex][i], ".sav"))
				{
					saveloadlist[slindex][i] = 0;
					break;
				}
			}

			saveloadlist[slindex + 1] = NULL;
			slindex++;
		}

		SaveIndex++;
	}

	// the list doesn't come out in ascending order so sort it
	COM_SortStringList (saveloadlist, true);

	// create the scrollbox providers
	// save and load need separate providers as they have slightly different lists
	SaveScrollbox = new CScrollBoxProvider (NumSaves, MAX_SAVE_DISPLAY, 22);
	SaveScrollbox->SetDrawItemCallback (Menu_SaveLoadOnDraw);
	SaveScrollbox->SetHoverItemCallback (Menu_SaveLoadOnHover);
	SaveScrollbox->SetEnterItemCallback (Menu_SaveLoadOnEnter);
	SaveScrollbox->SetDeleteItemCallback (Menu_SaveLoadOnDelete);

	// note that numsaves is *always* guaranteed to be at least 1 as we added the "new savegame" item
	LoadScrollbox = new CScrollBoxProvider (NumSaves - 1, MAX_SAVE_DISPLAY, 22);
	LoadScrollbox->SetDrawItemCallback (Menu_SaveLoadOnDraw);
	LoadScrollbox->SetHoverItemCallback (Menu_SaveLoadOnHover);
	LoadScrollbox->SetEnterItemCallback (Menu_SaveLoadOnEnter);
	LoadScrollbox->SetDeleteItemCallback (Menu_SaveLoadOnDelete);

	// same list now
	savelistchanged = false;
}


void Menu_SaveLoadCheckSavedir (void)
{
	static char oldsavedir[128] = {0};

	// no change here
	if (!stricmp (oldsavedir, host_savedir.string)) return;

	// copy out
	strncpy (oldsavedir, host_savedir.string, 127);

	// dirty the list
	savelistchanged = true;
}


void Menu_SaveCustomEnter (void)
{
	if (!sv.active || cl.intermission || svs.maxclients != 1 || cl.stats[STAT_HEALTH] <= 0)
	{
		// can't save
		menu_soundlevel = m_sound_deny;
		return;
	}

	// scan the saves
	Menu_SaveLoadCheckSavedir ();
	Menu_SaveLoadScanSaves ();

	// set the active array to the full list
	ActiveSaveInfoArray = SaveInfoArray;
	ActiveScrollbox = SaveScrollbox;
}


void Menu_LoadCustomEnter (void)
{
	// scan the saves
	Menu_SaveLoadCheckSavedir ();
	Menu_SaveLoadScanSaves ();

	// begin the active array at the second item (skip over the "new savegame" item)
	ActiveSaveInfoArray = &SaveInfoArray[1];
	ActiveScrollbox = LoadScrollbox;

	// check number of saves - if 0, sound a deny
	if (!SaveInfoList) menu_soundlevel = m_sound_deny;
}


void Draw_Mapshot (char *name, int x, int y);

void Menu_SaveLoadOnHover (int initialy, int y, int itemnum)
{
	// highlight bar
	Menu_HighlightBar (-174, y, 172);

	// hack - used to identify the "new savegame" item
	if (!ActiveSaveInfoArray[itemnum]->SaveInfo.filename[0])
	{
		// new savegame
		Draw_Mapshot (NULL, (vid.width - 320) / 2 + 208, initialy + 8);
		Menu_PrintWhite (220, initialy + 145, "Current Stats");
		Menu_Print (218, initialy + 160, DIVIDER_LINE);

		// remake quake compatibility
		if (cl.stats[STAT_TOTALMONSTERS] == 0)
			Menu_Print (220, initialy + 175, va ("Kills:   %i", cl.stats[STAT_MONSTERS]));
		else Menu_Print (220, initialy + 175, va ("Kills:   %i/%i", cl.stats[STAT_MONSTERS], cl.stats[STAT_TOTALMONSTERS]));

		// remake quake compatibility
		if (cl.stats[STAT_TOTALSECRETS] == 0)
			Menu_Print (220, initialy + 175, va ("Secrets: %i", cl.stats[STAT_SECRETS]));
		else Menu_Print (220, initialy + 175, va ("Secrets: %i/%i", cl.stats[STAT_SECRETS], cl.stats[STAT_TOTALSECRETS]));

		Menu_Print (220, initialy + 199, va ("Skill:   %s", SkillNames[(int) skill.value]));
		Menu_Print (220, initialy + 211, va ("Time:    %02i:%02i", (int) (cl.time / 60), (int) (cl.time - (int) (cl.time / 60) * 60)));
	}
	else
	{
		// existing save game
		Draw_Mapshot (va ("%s/%s", host_savedir.string, ActiveSaveInfoArray[itemnum]->SaveInfo.filename), (vid.width - 320) / 2 + 208, initialy + 8);
		Menu_PrintWhite (220, initialy + 145, "Savegame Info");
		Menu_Print (218, initialy + 160, DIVIDER_LINE);
		Menu_Print (220, initialy + 175, va ("Kills:   %s", ActiveSaveInfoArray[itemnum]->SaveInfo.kills));
		Menu_Print (220, initialy + 187, va ("Secrets: %s", ActiveSaveInfoArray[itemnum]->SaveInfo.secrets));
		Menu_Print (220, initialy + 199, va ("Skill:   %s", SkillNames[ActiveSaveInfoArray[itemnum]->SaveInfo.skill]));
		Menu_Print (220, initialy + 211, va ("Time:    %s", ActiveSaveInfoArray[itemnum]->SaveInfo.time));
		Menu_Print (220, initialy + 221, va ("%s", ActiveSaveInfoArray[itemnum]->SaveInfo.savetime));
	}
}


void Menu_SaveLoadOnDraw (int y, int itemnum)
{
	// draw the item (not every savegame has a map name)
	if (ActiveSaveInfoArray[itemnum]->SaveInfo.mapname[0] == 32)
		Menu_Print (-8, y, ActiveSaveInfoArray[itemnum]->SaveInfo.filename);
	else if (ActiveSaveInfoArray[itemnum]->SaveInfo.mapname[0])
		Menu_Print (-8, y, ActiveSaveInfoArray[itemnum]->SaveInfo.mapname);
	else Menu_Print (-8, y, ActiveSaveInfoArray[itemnum]->SaveInfo.filename);
}


void Draw_InvalidateMapshot (void);
void Host_DoSavegame (char *savename);

void Menu_SaveLoadOnDelete (int itemnum)
{
	// can't delete this one!
	if (m_state == m_save && itemnum == 0)
	{
		SCR_ModalMessage ("You cannot delete\nthis item!\n", "Error", MB_OK);
		return;
	}

	int delsave = SCR_ModalMessage
	(
		"Are you sure that you want to\ndelete this save?\n",
		va ("Delete %s", ActiveSaveInfoArray[itemnum]->SaveInfo.filename),
		MB_YESNO
	);

	if (delsave)
	{
		// delete the save
		char delfile[MAX_PATH];

		_snprintf (delfile, MAX_PATH, "%s\\%s\\%s", com_gamedir, host_savedir.string, ActiveSaveInfoArray[itemnum]->SaveInfo.filename);

		// change / to \\ 
		for (int i = 0; ; i++)
		{
			if (!delfile[i]) break;
			if (delfile[i] == '/') delfile[i] = '\\';
		}

		if (!DeleteFile (delfile))
		{
			SCR_UpdateScreen ();
			SCR_ModalMessage ("Delete file failed\n", "Error", MB_OK);
			return;
		}

		// dirty and rescan
		savelistchanged = true;
		Menu_SaveLoadScanSaves ();

		// rerun the enter function
		if (m_state == m_save)
			Menu_SaveCustomEnter ();
		else Menu_LoadCustomEnter ();
	}
}


void Menu_SaveLoadOnEnter (int itemnum)
{
	// ensure the folder exists
	COM_CheckContentDirectory (&host_savedir, true);

	if (m_state == m_save)
	{
		if (itemnum == 0)
		{
			int i;

			// generate a new save name
			for (i = 0; i < 99999; i++)
			{
				FILE *f = fopen (va ("%s/%s/%s%05i.sav", com_gamedir, host_savedir.string, host_savenamebase.string, i), "r");

				if (!f)
				{
					// save to this one
					Host_DoSavegame (va ("%s%05i", host_savenamebase.string, i));

					// dirty the save list
					savelistchanged = true;

					// rescan here and now
					// (note - we should try to position the selection on this item also)
					Menu_SaveLoadScanSaves ();
					break;
				}

				fclose (f);
			}

			if (i == 100000)
				Con_Printf ("Menu_SaveLoadOnEnter: Failed to find a free savegame slot\n");

			// exit menus
			m_state = m_none;
			key_dest = key_game;
			return;
		}

		// overwrite save
		// this message was annoying
		//if (Menu_MessageBox (va ("%s\n\nAre you sure that you want to overwrite this save?", ActiveSaveInfoArray[itemnum]->SaveInfo.filename)))
		{
			// save it
			Host_DoSavegame (ActiveSaveInfoArray[itemnum]->SaveInfo.filename);

			// execute the command buffer now so that we have the save file as valid for the next step
			Cbuf_Execute ();

			// rather than dirtying the entire list we will just update the item
			ActiveSaveInfoArray[itemnum]->UpdateItem ();

			// need to invalidate the mapshot for it too!
			Draw_InvalidateMapshot ();

			// exit menus
			m_state = m_none;
			key_dest = key_game;
		}

		return;
	}

	// exit menus
	m_state = m_none;
	key_dest = key_game;

	// Host_Loadgame_f can't bring up the loading plaque because too much
	// stack space has been used, so do it now
	SCR_BeginLoadingPlaque ();

	// to do - check if client status has changed and if so, display a warning
	Cbuf_AddText (va ("load %s\n", ActiveSaveInfoArray[itemnum]->SaveInfo.filename));
}


void Menu_DirtySaveLoadMenu (void)
{
	// intended to be used when a save is issued from the command line or from QC
	// note - we *COULD* pass this a char * of the save name, check it against
	// the array contents and only update if it's a replacement item...
	savelistchanged = true;

	// rescan for autocompletion
	Menu_SaveLoadScanSaves ();
}


int Menu_SaveLoadCustomDraw (int y)
{
	// check for validity, report and get out
	if (m_state == m_save)
	{
		// false if a save cannot be done at this time
		bool validsave = true;

		if (!sv.active)
		{
			Menu_PrintCenter (y + 10, "You cannot save with an inactive local server");
			validsave = false;
		}
		else if (cl.intermission)
		{
			Menu_PrintCenter (y + 10, "You cannot save during an intermission");
			validsave = false;
		}
		else if (svs.maxclients != 1)
		{
			Menu_PrintCenter (y + 10, "You cannot save a multiplayer game");
			validsave = false;
		}
		else if (cl.stats[STAT_HEALTH] <= 0)
		{
			Menu_PrintCenter (y + 10, "You cannot save with a dead player");
			validsave = false;
		}

		if (!validsave)
		{
			Menu_PrintCenter (y + 30, "Press ESC to Exit this Menu");
			return y + 40;
		}
	}
	else
	{
		if (!SaveInfoList || !ActiveSaveInfoArray[0])
		{
			Menu_PrintCenter (y + 10, "There are no Saved Games to Load from");
			Menu_PrintCenter (y + 30, "Press ESC to Exit this Menu");
			return y + 40;
		}
	}

	if (ActiveScrollbox)
		y = ActiveScrollbox->DrawItems ((vid.width - 320) / 2 - 24, y);

	return y;
}


void Menu_SaveLoadCustomKey (int key)
{
	if (!SaveInfoList) return;
	if (!ActiveScrollbox) return;

	// get what the position is when we come in here
	int oldcurr = ActiveScrollbox->GetCurrent ();

	switch (key)
	{
	case K_DOWNARROW:
	case K_UPARROW:
		menu_soundlevel = m_sound_nav;

	case K_DEL:
	case K_ENTER:
		// fall through to default here as ActiveScrollbox might go to NULL
		// position don't change anyway so it's cool
		ActiveScrollbox->KeyFunc (key);

	default:
		// position won't change
		return;
	}

	// hmmm - this seems to do nothing...
	// get what the position is now
	int newcurr = ActiveScrollbox->GetCurrent ();

	// hasn't changed
	if (oldcurr == newcurr) return;

	// fix up the scrollboxes so that the current items track each other
	if (m_state == m_save)
	{
		// load item is one less
		LoadScrollbox->SetCurrent (newcurr - 1);
	}
	else
	{
		// save item is one less
		SaveScrollbox->SetCurrent (newcurr + 1);
	}
}


void Menu_InitSaveLoadMenu (void)
{
	menu_Save.AddOption (new CQMenuBanner ("gfx/p_save.lmp"));
	menu_Save.AddOption (new CQMenuTitle ("Save the Current Game"));
	menu_Save.AddOption (new CQMenuCustomEnter (Menu_SaveCustomEnter));
	menu_Save.AddOption (new CQMenuCustomDraw (Menu_SaveLoadCustomDraw));
	menu_Save.AddOption (new CQMenuCustomKey (K_UPARROW, Menu_SaveLoadCustomKey));
	menu_Save.AddOption (new CQMenuCustomKey (K_DOWNARROW, Menu_SaveLoadCustomKey));
	menu_Save.AddOption (new CQMenuCustomKey (K_DEL, Menu_SaveLoadCustomKey));
	menu_Save.AddOption (new CQMenuCustomKey (K_ENTER, Menu_SaveLoadCustomKey));

	menu_Load.AddOption (new CQMenuBanner ("gfx/p_load.lmp"));
	menu_Load.AddOption (new CQMenuTitle ("Load a Previously Saved Game"));
	menu_Load.AddOption (new CQMenuCustomEnter (Menu_LoadCustomEnter));
	menu_Load.AddOption (new CQMenuCustomDraw (Menu_SaveLoadCustomDraw));
	menu_Load.AddOption (new CQMenuCustomKey (K_UPARROW, Menu_SaveLoadCustomKey));
	menu_Load.AddOption (new CQMenuCustomKey (K_DOWNARROW, Menu_SaveLoadCustomKey));
	menu_Load.AddOption (new CQMenuCustomKey (K_DEL, Menu_SaveLoadCustomKey));
	menu_Load.AddOption (new CQMenuCustomKey (K_ENTER, Menu_SaveLoadCustomKey));

	// the initial scan at runtime can be slow owing to filesystem first access, so
	// run a pre-scan to speed that up a little...
	Menu_SaveLoadCheckSavedir ();
	Menu_SaveLoadScanSaves ();
}


void Menu_SaveLoadInvalidate (void)
{
	// dirty the lists
	savelistchanged = true;

	// rescan
	Menu_SaveLoadCheckSavedir ();
	Menu_SaveLoadScanSaves ();
}



