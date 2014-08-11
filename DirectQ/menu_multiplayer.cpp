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

extern qpic_t *gfx_p_multi_lmp;

void Draw_Mapshot (char *name, int x, int y);
extern char *SkillNames[];

// maps are now taken from the maps menu loader
extern char **spinbox_maps;
extern char **spinbox_bsps;

CQMenu menu_Search (m_other);
CQMenu menu_SList (m_other);
CQMenu menu_FunName (m_other);

extern cvar_t cl_natfix;

#define TAG_ID1OPTIONS			1
#define TAG_HIPNOTICOPTIONS		2
#define TAG_ROGUEOPTIONS		4

/*
========================================================================================================================

					MULTI PLAYER MENU

========================================================================================================================
*/

int Menu_MultiplayerCustomDraw (int y)
{
	if (tcpipAvailable) return y;

	Menu_PrintCenterWhite (vid.currsize->height - 80, "No Communications Available");

	return y;
}


// dummy cvars to make textboxes not overwrite the real ones
cvar_t dummy_name;
cvar_t dummy_hostname;

// player colours
int setup_shirt, setup_oldshirt;
int setup_pants, setup_oldpants;

void Menu_SetupApplyFunc (void)
{
	// execute the commands
	Cbuf_AddText (va ("name \"%s\"\n", dummy_name.string));
	Cvar_Set ("hostname", dummy_hostname.string);

	if (setup_shirt != setup_oldshirt || setup_pants != setup_oldpants)
		Cbuf_AddText (va ("color %i %i\n", setup_shirt, setup_pants));

	// return to the multiplayer menu
	Menu_StackPop ();
}


void Menu_SetupCustomEnter (void)
{
	menu_soundlevel = m_sound_enter;

	// copy cvars out
	Cvar_Set (&dummy_name, cl_name.string);
	Cvar_Set (&dummy_hostname, hostname.string);

	setup_shirt = setup_oldshirt = ((int) cl_color.value) >> 4;
	setup_pants = setup_oldpants = ((int) cl_color.value) & 15;
}


byte identityTable[256];
byte translationTable[256];

void M_BuildTranslationTable (int top, int bottom)
{
	int		j;
	byte	*dest, *source;

	for (j = 0; j < 256; j++)
		identityTable[j] = j;

	dest = translationTable;
	source = identityTable;
	memcpy (dest, source, 256);

	if (top < 128)	// the artists made some backwards ranges.  sigh.
		memcpy (dest + TOP_RANGE, source + top, 16);
	else
		for (j = 0; j < 16; j++)
			dest[TOP_RANGE + j] = source[top + 15 - j];

	if (bottom < 128)
		memcpy (dest + BOTTOM_RANGE, source + bottom, 16);
	else
		for (j = 0; j < 16; j++)
			dest[BOTTOM_RANGE + j] = source[bottom + 15 - j];
}


#define TAG_SETUPAPPLY	1

int Menu_SetupCustomDraw (int y)
{
	int newy = y;

	extern qpic_t *gfx_bigbox_lmp;
	extern qpic_t *gfx_menuplyr_lmp;

	Draw_Pic ((vid.currsize->width - gfx_bigbox_lmp->width) / 2, y, gfx_bigbox_lmp);
	newy += gfx_bigbox_lmp->height + 5;

	M_BuildTranslationTable (setup_shirt * 16, setup_pants * 16);
	Draw_PicTranslate ((vid.currsize->width - gfx_menuplyr_lmp->width) / 2, y + 8, gfx_menuplyr_lmp, translationTable, setup_shirt, setup_pants);

	// check for an apply change
	if (setup_shirt != setup_oldshirt || setup_pants != setup_oldpants ||
		strcmp (dummy_name.string, cl_name.string) ||
		strcmp (dummy_hostname.string, hostname.string))
		menu_Setup.EnableMenuOptions (TAG_SETUPAPPLY);
	else menu_Setup.DisableMenuOptions (TAG_SETUPAPPLY);

	return newy;
}


// if starting a new game (i.e. a DirectQ server) protocol 15 should be the default
int selected_protocol = 0;

int Menu_TCPIPProtoDesc (int y)
{
	if (selected_protocol == 0)
		Menu_PrintCenter (y, "Use for best compatibility with all Quake Clients");
	else if (selected_protocol == 1)
		Menu_PrintCenter (y, "FitzQuake extended protocol");
	else if (selected_protocol == 2)
		Menu_PrintCenter (y, "RMQ extended protocol");
	else Menu_PrintCenter (y, "Unknown protocol");

	return y + 15;
}


int Menu_TCPIPCustomDraw (int y)
{
	Menu_Print (148 - strlen ("IP Address") * 8, y, "IP Address");
	Menu_PrintWhite (172, y, my_tcpip_address);

	y += 15;

	Menu_Print (148 - strlen ("Host Name") * 8, y, "Host Name");
	Menu_PrintWhite (172, y, hostname.string);

	y += 15;

	if (tcpipAvailable) return y;

	Menu_PrintCenterWhite (vid.currsize->height - 80, "No Communications Available");

	return y;
}


cvar_t dummy_port;
cvar_t dummy_remoteip;

void Menu_TCPIPCustomEnter (void)
{
	// copy out the port
	Cvar_Set (&dummy_port, va ("%i", DEFAULTnet_hostport));
	Cvar_Set (&dummy_remoteip, "0");
}


void Menu_TCPIPPostConfigCommon (void)
{
	// stop all demos
	Cbuf_AddText ("stopdemo\n");

	// retrieve the port
	int l =  atoi (dummy_port.string);

	// store it out
	if (l > 65535)
		net_hostport = DEFAULTnet_hostport;
	else net_hostport = l;
}


void Menu_TCPIPContinueToGameOptions (void)
{
	// complete net config
	Menu_TCPIPPostConfigCommon ();

	// fire the game options menu entry function
	Menu_StackPush (&menu_GameConfig);
}


cvar_t dummy_maxplayers;
cvar_t dummy_timelimit;
cvar_t dummy_fraglimit;
int dummy_skill;
int dummy_teamplay;

int map_number = 0;

char *ID1Teamplay[] = {"Off", "No Friendly Fire", "Friendly Fire", NULL};
char *RogueTeamplay[] = {"Off", "No Friendly Fire", "Friendly Fire", "Tag", "Capture the Flag", "One Flag CTF", "Three Team CTF", NULL};
char **ActiveTeamplay = ID1Teamplay;
char *GameTypes[] = {"Cooperative", "Deathmatch", NULL};
int gametype_number = 0;


void Menu_FixupSpin (int *spinnum, char **spintext)
{
	for (int i = 0;; i++)
	{
		if (!spintext[i])
		{
			// this is the only thing that's really safe here
			spinnum[0] = 0;
			return;
		}

		if (i == spinnum[0]) break;
	}
}


void Menu_GameConfigCustomEnter (void)
{
	// copy out cvars to prevent in-place editing of values screwing with current settings before we commit them
	// especially as many of these are broadcast cvars...
	dummy_maxplayers.value = svs.maxclients;
	dummy_fraglimit.value = fraglimit.value;
	dummy_timelimit.value = timelimit.value;
	dummy_skill = (int) (skill.value + 0.5);
	dummy_teamplay = (int) teamplay.value;

	// bound values so we don't crash or otherwise behave unexpectedly
	if (dummy_maxplayers.value < 2) dummy_maxplayers.value = 2; else if (dummy_maxplayers.value > MAX_SCOREBOARD) dummy_maxplayers.value = MAX_SCOREBOARD;
	if (dummy_fraglimit.value < 0) dummy_fraglimit.value = 0; else if (dummy_fraglimit.value > 100) dummy_fraglimit.value = 100;
	if (dummy_timelimit.value < 0) dummy_timelimit.value = 0; else if (dummy_timelimit.value > 60) dummy_timelimit.value = 60;
	if (dummy_skill < 0) dummy_skill = 0; else if (dummy_skill > 3) dummy_skill = 3;

	if (rogue) {if (dummy_teamplay < 0) dummy_teamplay = 0; else if (dummy_teamplay > 6) dummy_teamplay = 6;}
	else {if (dummy_teamplay < 0) dummy_teamplay = 0; else if (dummy_teamplay > 2) dummy_teamplay = 2;}

	if (coop.value)
		gametype_number = 0;
	else gametype_number = 1;

	// show/hide options depending on selected game
	if (rogue)
	{
		menu_GameConfig.ShowMenuOptions (TAG_ROGUEOPTIONS);
		menu_GameConfig.HideMenuOptions (TAG_HIPNOTICOPTIONS);
		menu_GameConfig.HideMenuOptions (TAG_ID1OPTIONS);

		// fix up spin controls in case the game has changed and we now have an invalid selection
		Menu_FixupSpin (&map_number, spinbox_maps);
		Menu_FixupSpin (&dummy_teamplay, RogueTeamplay);
	}
	else if (hipnotic)
	{
		menu_GameConfig.HideMenuOptions (TAG_ROGUEOPTIONS);
		menu_GameConfig.ShowMenuOptions (TAG_HIPNOTICOPTIONS);
		menu_GameConfig.HideMenuOptions (TAG_ID1OPTIONS);

		// fix up spin controls in case the game has changed and we now have an invalid selection
		Menu_FixupSpin (&map_number, spinbox_maps);
		Menu_FixupSpin (&dummy_teamplay, ID1Teamplay);
	}
	else
	{
		menu_GameConfig.HideMenuOptions (TAG_ROGUEOPTIONS);
		menu_GameConfig.HideMenuOptions (TAG_HIPNOTICOPTIONS);
		menu_GameConfig.ShowMenuOptions (TAG_ID1OPTIONS);

		// fix up spin controls in case the game has changed and we now have an invalid selection
		Menu_FixupSpin (&map_number, spinbox_maps);
		Menu_FixupSpin (&dummy_teamplay, ID1Teamplay);
	}
}


// adding protocol to the new game menu
extern char *protolist[];

void Menu_GameConfigBeginGame (void)
{
	// disconnet any current servers
	if (sv.active) Cbuf_AddText ("disconnect\n");

	// so host_netport will be re-examined
	Cbuf_AddText ("listen 0\n");
	Cbuf_AddText (va ("maxplayers %u\n", (int) dummy_maxplayers.value));
	Cbuf_AddText (va ("sv_protocol %s\n", protolist[selected_protocol]));
	SCR_BeginLoadingPlaque ();

	// copy cvar values back now that we're committed
	Cvar_Set (&timelimit, dummy_timelimit.value);
	Cvar_Set (&fraglimit, dummy_fraglimit.value);
	Cvar_Set (&skill, (float) dummy_skill);
	Cvar_Set (&teamplay, (float) dummy_teamplay);

	// set gametype
	if (gametype_number)
	{
		Cvar_Set (&coop, 0.0f);
		Cvar_Set (&deathmatch, 1.0f);
	}
	else
	{
		Cvar_Set (&coop, 1.0f);
		Cvar_Set (&deathmatch, 0.0f);
	}

	// load the correct map (no longer game-dependent)
	Cbuf_AddText (va ("map %s\n", spinbox_bsps[map_number]));
}


bool Menu_SearchComplete = false;
float Menu_SearchCompleteTime;

void Menu_SearchCustomEnter (void)
{
	slistSilent = true;
	slistLocal = false;
	Menu_SearchComplete = false;
	NET_Slist_f ();
}


int Menu_SearchCustomDraw (int y)
{
	if (slistInProgress)
	{
		Menu_PrintCenterWhite (y + 50, "...Searching...");
		NET_Poll ();
		return y;
	}

	if (!Menu_SearchComplete)
	{
		Menu_SearchComplete = true;
		Menu_SearchCompleteTime = realtime;
	}

	if (hostCacheCount)
	{
		Menu_StackPush (&menu_SList);
		return y;
	}

	Menu_PrintCenterWhite (y + 50, "No Quake Servers found");

	if ((realtime - Menu_SearchCompleteTime) < 3.0) return y;

	return y;
}


CScrollBoxProvider *MenuSListScrollbox = NULL;

void Menu_SListOnDraw (int y, int itemnum)
{
	// draw the item
	Menu_Print (-8, y, hostcache[itemnum].name);
}


void Menu_SListOnHover (int initialy, int y, int itemnum)
{
	// highlight bar
	Menu_HighlightBar (-174, y, 172);

	hostcache_t *hc = &hostcache[itemnum];

	// rest of info
	Draw_Mapshot (va ("maps/%s", hc->map), (vid.currsize->width - 320) / 2 + 208, initialy + 8);
	Menu_PrintWhite (228, initialy + 145, "Server Info");
	Menu_Print (218, initialy + 160, DIVIDER_LINE);

	char hostipconfig[32];
	char *hostport;

	strcpy (hostipconfig, hc->cname);

	for (int i = strlen (hostipconfig) - 1; i; i--)
	{
		if (hostipconfig[i] == ':')
		{
			hostport = &hostipconfig[i + 1];
			hostipconfig[i] = 0;
			break;
		}
	}

	Menu_Print (212, initialy + 175, va ("IP Addr: %s", hostipconfig));
	Menu_Print (212, initialy + 185, va ("Port:    %s", hostport));
	Menu_Print (218, initialy + 200, DIVIDER_LINE);
	Menu_Print (212, initialy + 215, va ("Map:     %s", hc->map));
	Menu_Print (212, initialy + 225, va ("Players: %i of %i", hc->users, hc->maxusers));
}


char m_return_reason [32];
CQMenu *Menu_NetReturn = NULL;

float Net_ErrorReturnTime = 0;

void NET_MenuReturn (void)
{
	if (Menu_NetReturn)
	{
		Net_ErrorReturnTime = realtime + 5;
		Menu_StackPop ();
	}

	Menu_NetReturn = NULL;
}


int Menu_NetErrorCustomDraw (int y)
{
	if (Net_ErrorReturnTime < realtime) return y;

	Draw_TextBox ((vid.currsize->width / 2) - 175, 80, 334, 90);
	Menu_PrintCenterWhite (100, "Connection Error");
	Menu_PrintCenter (115, DIVIDER_LINE);
	Menu_PrintCenter (130, "Failed to connect to remote server");
	Menu_PrintCenter (145, va ("The reason given was:"));
	Menu_PrintCenter (160, va ("\"%s\"", m_return_reason));

	return y;
}


void Menu_MultiplayerConnectToGame (CQMenu *returnmenu, char *remote)
{
	key_dest = key_game;
	m_state = m_none;
	Menu_NetReturn = returnmenu;
	m_return_reason[0] = 0;
	Cbuf_AddText (va ("connect \"%s\"\n", remote));
}


void Menu_SListOnEnter (int itemnum)
{
	// join this game
	Menu_MultiplayerConnectToGame (&menu_SList, hostcache[itemnum].cname);
}


void Menu_SListCustomKey (int key)
{
	if (!MenuSListScrollbox) return;

	switch (key)
	{
	case K_DOWNARROW:
	case K_UPARROW:
		menu_soundlevel = m_sound_nav;

	case K_ENTER:
		MenuSListScrollbox->KeyFunc (key);
		break;
	}
}


void Menu_SListCustomEnter (void)
{
	// sort the server list
	if (hostCacheCount > 1)
	{
		int	i, j;
		hostcache_t temp;

		for (i = 0; i < hostCacheCount; i++)
		{
			for (j = i + 1; j < hostCacheCount; j++)
			{
				if (strcmp (hostcache[j].name, hostcache[i].name) < 0)
				{
					memcpy (&temp, &hostcache[j], sizeof (hostcache_t));
					memcpy (&hostcache[j], &hostcache[i], sizeof (hostcache_t));
					memcpy (&hostcache[i], &temp, sizeof (hostcache_t));
				}
			}
		}
	}

	// create a scrollbox for it
	if (MenuSListScrollbox) delete MenuSListScrollbox;

	MenuSListScrollbox = new CScrollBoxProvider (hostCacheCount, 20, 22);
	MenuSListScrollbox->SetDrawItemCallback (Menu_SListOnDraw);
	MenuSListScrollbox->SetHoverItemCallback (Menu_SListOnHover);
	MenuSListScrollbox->SetEnterItemCallback (Menu_SListOnEnter);
}


int Menu_SListCustomDraw (int y)
{
	if (!MenuSListScrollbox) return y;

	// draw through the scrollbox provider
	MenuSListScrollbox->DrawItems ((vid.currsize->width - 320) / 2 - 24, y);

	return y;
}


void Menu_TCPIPJoinGameCommand (void)
{
	// connect to this game
	Menu_MultiplayerConnectToGame (&menu_TCPIPJoinGame, dummy_remoteip.string);
}


// don't initialize to character 0 as it's NULL and will end the string
char funname[1024] = {0};
int custnamecol = 1;
int custnamerow = 0;
bool custnamechanged = false;


int Menu_CustomNameCustomDraw (int y)
{
	// add 2 because we're going to back up a little for the textbox
	y += 2;

	// prompt and textbox - these replicate the code in CQMenuCvarTextbox::Draw
	Menu_Print (148 - strlen ("Player Name") * 8, y, "Player Name");
	Draw_Fill (vid.currsize->width / 2 - 160 + 168, y - 1, MAX_TBLENGTH * 8 + 4, 10, 20, 255);
	Draw_Fill (vid.currsize->width / 2 - 160 + 169, y, MAX_TBLENGTH * 8 + 2, 8, 0, 192);

	// name - use white print to avoid adding 128
	Menu_PrintWhite (170, y, funname);

	// add another 2 cos the tb comes down a little
	y += 17;

	Menu_PrintCenter (y, DIVIDER_LINE);
	y += 15;

	Draw_TextBox (((vid.currsize->width / 2) - 256) - 16, y - 5, 520, 128);
	y += 10;

	for (int cy = 0, cc = 0; cy < 8; cy++)
	{
		for (int cx = 0; cx < 32; cx++, cc++)
		{
			// prevent drawing of character 0 as it will terminate the name string
			if (!cx && !cy) continue;

			if (cy == custnamerow && cx == custnamecol)
				Menu_HighlightGeneric (((vid.currsize->width / 2) - 256) + (cx * 16) - 2, y - 2, 12, 12);

			Draw_Character (((vid.currsize->width / 2) - 256) + (cx * 16), y, cc);
		}

		y += 15;
	}

	if (custnamerow > 7) Menu_HighlightBar (y + 25 + (15 * (custnamerow - 8)));

	Menu_PrintCenter (y + 25, "Clear Name");

	if (!custnamechanged) D3D_Set2DShade (0.666f);

	Menu_PrintCenter (y + 40, "Apply Change");

	if (!custnamechanged) D3D_Set2DShade (1.0f);

	return (y + 10);
}


void Menu_CustomNameCustomKey (int k)
{
	int len = strlen (funname);

	if (k == K_ENTER)
	{
		if (custnamerow == 9)
		{
			// execute this immediately so that when we go back to the setup menu it's updated properly
			Cbuf_AddText (va ("name \"%s\"\n", funname));
			Cbuf_Execute ();

			// return to the setup menu
			Menu_StackPop ();
			return;
		}
		else if (custnamerow == 8)
		{
			// clear name
			funname[0] = 0;

			// prevent selection of empty names
			custnamechanged = false;
		}
		else
		{
			// select character
			int cc = custnamerow * 32 + custnamecol;

			// prevent NULL termination
			if (!cc) return;

			// don't overflow
			if (len > 1000) return;

			// append it
			funname[len] = cc;
			funname[len + 1] = 0;

			if (!strcmp (funname, cl_name.string))
				custnamechanged = false;
			else custnamechanged = true;
		}

		return;
	}

	if (k == K_BACKSPACE)
	{
		if (funname[0] == 0)
		{
			// can't backspace
			menu_soundlevel = m_sound_deny;
			return;
		}

		menu_soundlevel = m_sound_option;

		funname[len - 1] = 0;

		if (funname[0] != 0)
		{
			if (!strcmp (funname, cl_name.string))
				custnamechanged = false;
			else custnamechanged = true;
		}
		else custnamechanged = false;
	}

	menu_soundlevel = m_sound_option;

	int maxrow = 9;

	if (!custnamechanged) maxrow = 8;
	if (k == K_UPARROW) custnamerow--;
	if (k == K_RIGHTARROW) custnamecol++;
	if (k == K_DOWNARROW) custnamerow++;
	if (k == K_LEFTARROW) custnamecol--;
	if (k == K_HOME) custnamecol -= 8;
	if (k == K_END) custnamecol += 8;
	if (k == K_PGUP) custnamerow -= 3;
	if (k == K_PGDN) custnamerow += 3;

	// in theory if should be ok here, but let's get paranoid about it, OK?
	while (custnamerow < 0) custnamerow += (maxrow + 1);
	while (custnamerow > maxrow) custnamerow -= (maxrow + 1);
	while (custnamecol < 0) custnamecol += 32;
	while (custnamecol > 31) custnamecol -= 32;

	if (!custnamerow && !custnamecol)
	{
		// prevent selection of character 0 as it will terminate the name string
		if (k == K_UPARROW)
			custnamerow = maxrow;
		else if (k == K_RIGHTARROW)
			custnamecol = 1;
		else if (k == K_DOWNARROW)
			custnamerow = 1;
		else if (k == K_LEFTARROW)
			custnamecol = 31;
		else if (k == K_HOME)
			custnamecol = 31;
		else if (k == K_END)
			custnamecol = 1;
		else if (k == K_PGUP)
			custnamerow = 1;
		else if (k == K_PGDN)
			custnamerow = maxrow;
		else
		{
			// should never happen
			custnamecol = 1;
		}
	}
}


void Menu_CustomNameCustomEnter (void)
{
	menu_soundlevel = m_sound_enter;

	// copy cvars out
	Q_strncpy (funname, cl_name.string, 1023);

	custnamechanged = false;

	if (custnamerow == 9) custnamerow = 8;
}


void Menu_DoMapInfo (int x, int y, int itemnum, bool showcdtrack = true);

int Menu_GameConfigCustomDraw (int y)
{
	y += 5;
	Draw_Mapshot (va ("maps/%s", spinbox_bsps[map_number]), (vid.currsize->width - 320) / 2 + 96, y);
	y += 130;
	Menu_DoMapInfo (160, y, map_number);

	y += 45;

	return y;
}


#define TAG_TALLSCREEN	64

int Menu_GameConfigCustomDraw1 (int y)
{
	// hide the mapshot if we don't have enough vertical real-estate
	if (vid.currsize->height > 500)
		menu_GameConfig.ShowMenuOptions (TAG_TALLSCREEN);
	else menu_GameConfig.HideMenuOptions (TAG_TALLSCREEN);

	return y;
}


void Menu_InitMultiplayerMenu (void)
{
	extern qpic_t *gfx_mp_menu_lmp;
	extern cvar_t pq_moveup;
	extern cvar_t cl_fullpitch;
	extern cvar_t pq_scoreboard_pings;

	// top level menu
	menu_Multiplayer.AddOption (new CQMenuBanner (&gfx_p_multi_lmp));
	menu_Multiplayer.AddOption (new CQMenuSpacer (DIVIDER_LINE));
	menu_Multiplayer.AddOption (new CQMenuCursorSubMenu (&menu_TCPIPJoinGame));
	menu_Multiplayer.AddOption (new CQMenuCursorSubMenu (&menu_TCPIPNewGame));
	menu_Multiplayer.AddOption (new CQMenuCursorSubMenu (&menu_Setup));
	menu_Multiplayer.AddOption (new CQMenuChunkyPic (&gfx_mp_menu_lmp));

	menu_Multiplayer.AddOption (new CQMenuCustomDraw (Menu_MultiplayerCustomDraw));

	menu_FunName.AddOption (new CQMenuBanner (&gfx_p_multi_lmp));
	menu_FunName.AddOption (new CQMenuTitle ("Quake Name Generator"));
	menu_FunName.AddOption (new CQMenuCustomEnter (Menu_CustomNameCustomEnter));
	menu_FunName.AddOption (new CQMenuCustomDraw (Menu_CustomNameCustomDraw));
	menu_FunName.AddOption (new CQMenuSpacer (DIVIDER_LINE));
	menu_FunName.AddOption (new CQMenuCustomKey (K_UPARROW, Menu_CustomNameCustomKey));
	menu_FunName.AddOption (new CQMenuCustomKey (K_RIGHTARROW, Menu_CustomNameCustomKey));
	menu_FunName.AddOption (new CQMenuCustomKey (K_DOWNARROW, Menu_CustomNameCustomKey));
	menu_FunName.AddOption (new CQMenuCustomKey (K_LEFTARROW, Menu_CustomNameCustomKey));
	menu_FunName.AddOption (new CQMenuCustomKey (K_ENTER, Menu_CustomNameCustomKey));
	menu_FunName.AddOption (new CQMenuCustomKey (K_BACKSPACE, Menu_CustomNameCustomKey));
	menu_FunName.AddOption (new CQMenuCustomKey (K_HOME, Menu_CustomNameCustomKey));
	menu_FunName.AddOption (new CQMenuCustomKey (K_END, Menu_CustomNameCustomKey));
	menu_FunName.AddOption (new CQMenuCustomKey (K_PGUP, Menu_CustomNameCustomKey));
	menu_FunName.AddOption (new CQMenuCustomKey (K_PGDN, Menu_CustomNameCustomKey));

	// setup menu
	menu_Setup.AddOption (new CQMenuBanner (&gfx_p_multi_lmp));
	menu_Setup.AddOption (new CQMenuTitle ("Setup Player Options"));
	menu_Setup.AddOption (new CQMenuCustomEnter (Menu_SetupCustomEnter));
	menu_Setup.AddOption (new CQMenuCvarTextbox ("Player Name", &dummy_name));
	menu_Setup.AddOption (new CQMenuSubMenu ("Generate Custom Name", &menu_FunName));
	menu_Setup.AddOption (new CQMenuSpacer (DIVIDER_LINE));
	menu_Setup.AddOption (new CQMenuCvarTextbox ("Host Name", &dummy_hostname, TBFLAGS_ALPHANUMERICFLAGS));
	menu_Setup.AddOption (new CQMenuSpacer (DIVIDER_LINE));
	menu_Setup.AddOption (new CQMenuColourBar ("Shirt Colour", &setup_shirt));
	menu_Setup.AddOption (new CQMenuCustomDraw (Menu_SetupCustomDraw));
	menu_Setup.AddOption (new CQMenuColourBar ("Pants Colour", &setup_pants));
	menu_Setup.AddOption (new CQMenuSpacer (DIVIDER_LINE));
	menu_Setup.AddOption (TAG_SETUPAPPLY, new CQMenuCommand ("Apply Changes", Menu_SetupApplyFunc));
	menu_Setup.AddOption (new CQMenuSpacer ());
	menu_Setup.AddOption (new CQMenuTitle ("ProQuake Options"));
	menu_Setup.AddOption (new CQMenuCvarToggle ("Jump is Move Up", &pq_moveup, 0, 1));
	menu_Setup.AddOption (new CQMenuCvarToggle ("Full View Pitch", &cl_fullpitch, 0, 1));
	menu_Setup.AddOption (new CQMenuCvarToggle ("Ping in Scoreboard", &pq_scoreboard_pings, 0, 1));

	// start a new game
	menu_TCPIPNewGame.AddOption (new CQMenuBanner (&gfx_p_multi_lmp));
	menu_TCPIPNewGame.AddOption (new CQMenuTitle ("Configure TCP/IP Options - New Game"));
	menu_TCPIPNewGame.AddOption (new CQMenuCustomEnter (Menu_TCPIPCustomEnter));
	menu_TCPIPNewGame.AddOption (new CQMenuCustomDraw (Menu_TCPIPCustomDraw));
	menu_TCPIPNewGame.AddOption (new CQMenuCvarTextbox ("Port", &dummy_port, TBFLAG_ALLOWNUMBERS));
	menu_TCPIPNewGame.AddOption (new CQMenuSpacer (DIVIDER_LINE));
	menu_TCPIPNewGame.AddOption (new CQMenuSpinControl ("Protocol", &selected_protocol, protolist));
	menu_TCPIPNewGame.AddOption (new CQMenuCustomDraw (Menu_TCPIPProtoDesc));
	menu_TCPIPNewGame.AddOption (new CQMenuSpacer (DIVIDER_LINE));
	menu_TCPIPNewGame.AddOption (new CQMenuCvarToggle ("Use ProQuake NAT Fix", &cl_natfix, 0, 1));
	menu_TCPIPNewGame.AddOption (new CQMenuSpacer (DIVIDER_LINE));
	menu_TCPIPNewGame.AddOption (new CQMenuCommand ("Continue to Game Options", Menu_TCPIPContinueToGameOptions));

	// join an existing game
	menu_TCPIPJoinGame.AddOption (new CQMenuBanner (&gfx_p_multi_lmp));
	menu_TCPIPJoinGame.AddOption (new CQMenuTitle ("Configure TCP/IP Options - Join a Game"));
	menu_TCPIPJoinGame.AddOption (new CQMenuCustomEnter (Menu_TCPIPCustomEnter));
	menu_TCPIPJoinGame.AddOption (new CQMenuCustomDraw (Menu_TCPIPCustomDraw));
	menu_TCPIPJoinGame.AddOption (new CQMenuCvarTextbox ("Port", &dummy_port, TBFLAG_ALLOWNUMBERS));
	menu_TCPIPJoinGame.AddOption (new CQMenuSpacer (DIVIDER_LINE));
	menu_TCPIPJoinGame.AddOption (new CQMenuCvarToggle ("Use ProQuake NAT Fix", &cl_natfix, 0, 1));
	menu_TCPIPJoinGame.AddOption (new CQMenuSpacer (DIVIDER_LINE));
	menu_TCPIPJoinGame.AddOption (new CQMenuSubMenu ("Search for Local Games", &menu_Search));
	menu_TCPIPJoinGame.AddOption (new CQMenuSpacer (DIVIDER_LINE));
	menu_TCPIPJoinGame.AddOption (new CQMenuCvarTextbox ("Remote Server", &dummy_remoteip));
	menu_TCPIPJoinGame.AddOption (new CQMenuCommand ("Join this Game", Menu_TCPIPJoinGameCommand));
	menu_TCPIPJoinGame.AddOption (new CQMenuCustomDraw (Menu_NetErrorCustomDraw));

	// game options
	menu_GameConfig.AddOption (new CQMenuCustomDraw (Menu_GameConfigCustomDraw1));
	menu_GameConfig.AddOption (new CQMenuBanner (&gfx_p_multi_lmp));
	menu_GameConfig.AddOption (new CQMenuTitle ("Game Configuration Options"));
	menu_GameConfig.AddOption (new CQMenuCustomEnter (Menu_GameConfigCustomEnter));
	menu_GameConfig.AddOption (new CQMenuSpinControl ("Game Type", &gametype_number, GameTypes));
	menu_GameConfig.AddOption (TAG_ID1OPTIONS, new CQMenuSpinControl ("Teamplay", &dummy_teamplay, ID1Teamplay));
	menu_GameConfig.AddOption (TAG_HIPNOTICOPTIONS, new CQMenuSpinControl ("Teamplay", &dummy_teamplay, ID1Teamplay));
	menu_GameConfig.AddOption (TAG_ROGUEOPTIONS, new CQMenuSpinControl ("Teamplay", &dummy_teamplay, RogueTeamplay));
	menu_GameConfig.AddOption (new CQMenuSpinControl ("Maximum Players", &dummy_maxplayers, 2, MAX_SCOREBOARD, 1, NULL, NULL));
	menu_GameConfig.AddOption (new CQMenuSpinControl ("Skill", &dummy_skill, SkillNames));
	menu_GameConfig.AddOption (new CQMenuSpinControl ("Frag Limit", &dummy_fraglimit, 0, 100, 10, "None", "Frags"));
	menu_GameConfig.AddOption (new CQMenuSpinControl ("Time Limit", &dummy_timelimit, 0, 60, 5, "None", "Minutes"));
	menu_GameConfig.AddOption (new CQMenuSpacer (DIVIDER_LINE));

	// generic mapname list (use of char *** is intentional here)
	menu_GameConfig.AddOption (new CQMenuSpinControl ("Select a Map", &map_number, &spinbox_bsps));
	menu_GameConfig.AddOption (new CQMenuSpinControl (NULL, &map_number, &spinbox_maps));

	extern cvar_t r_automapshot;

	// add the rest of the options
	menu_GameConfig.AddOption (new CQMenuSpacer (DIVIDER_LINE));
	menu_GameConfig.AddOption (new CQMenuCommand ("Begin New Multiplayer Game", Menu_GameConfigBeginGame));
	menu_GameConfig.AddOption (TAG_TALLSCREEN, new CQMenuSpacer (DIVIDER_LINE));
	menu_GameConfig.AddOption (TAG_TALLSCREEN, new CQMenuCustomDraw (Menu_GameConfigCustomDraw));
	menu_GameConfig.AddOption (TAG_TALLSCREEN, new CQMenuSpacer (DIVIDER_LINE));
	menu_GameConfig.AddOption (TAG_TALLSCREEN, new CQMenuCvarToggle ("Enable Mapshots", &r_automapshot, 0, 1));

	// search for games
	menu_Search.AddOption (new CQMenuBanner (&gfx_p_multi_lmp));
	menu_Search.AddOption (new CQMenuTitle ("Search for Local Games"));
	menu_Search.AddOption (new CQMenuCustomEnter (Menu_SearchCustomEnter));
	menu_Search.AddOption (new CQMenuCustomDraw (Menu_SearchCustomDraw));

	// server list
	menu_SList.AddOption (new CQMenuBanner (&gfx_p_multi_lmp));
	menu_SList.AddOption (new CQMenuTitle ("Local Quake Server List"));
	menu_SList.AddOption (new CQMenuCustomEnter (Menu_SListCustomEnter));
	menu_SList.AddOption (new CQMenuCustomDraw (Menu_SListCustomDraw));
	menu_SList.AddOption (new CQMenuCustomDraw (Menu_NetErrorCustomDraw));
	menu_SList.AddOption (new CQMenuCustomKey (K_UPARROW, Menu_SListCustomKey));
	menu_SList.AddOption (new CQMenuCustomKey (K_DOWNARROW, Menu_SListCustomKey));
	menu_SList.AddOption (new CQMenuCustomKey (K_ENTER, Menu_SListCustomKey));
}


