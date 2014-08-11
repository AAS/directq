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
#include "pr_class.h"

extern cvar_t	pausable;

int	current_skill;

void Mod_Print (void);

/*
==================
Host_Quit_f
==================
*/

void Menu_MainExitQuake (void);

void Host_Quit_f (void)
{
	CL_Disconnect ();
	Host_ShutdownServer (false);
	Sys_Quit ();
}


void Host_QuitWithPrompt_f (void)
{
	if (key_dest != key_console)
	{
		Menu_MainExitQuake ();
		return;
	}

	Host_Quit_f ();
}


/*
==================
Host_Status_f
==================
*/
void Host_Status_f (void)
{
	client_t	*client;
	int			seconds;
	int			minutes;
	int			hours = 0;
	int			j;
	void	(*print) (char * fmt, ...);

	if (cmd_source == src_command)
	{
		if (!sv.active)
		{
			Cmd_ForwardToServer ();
			return;
		}

		print = Con_Printf;
	}
	else
		print = SV_ClientPrintf;

	print ("host:    %s\n", Cvar_VariableString ("hostname"));
	print ("version: %4.2f\n", VERSION);

	if (tcpipAvailable) print ("tcp/ip:  %s\n", my_tcpip_address);

	print ("map:     %s\n", sv.name);
	print ("players: %i active (%i max)\n\n", net_activeconnections, svs.maxclients);

	for (j = 0, client = svs.clients; j < svs.maxclients; j++, client++)
	{
		if (!client->active)
			continue;

		seconds = (int) (net_time - client->netconnection->connecttime);
		minutes = seconds / 60;

		if (minutes)
		{
			seconds -= (minutes * 60);
			hours = minutes / 60;

			if (hours)
				minutes -= (hours * 60);
		}
		else
			hours = 0;

		print ("#%-2u %-16.16s  %3i  %2i:%02i:%02i\n", j + 1, client->name, (int) client->edict->v.frags, hours, minutes, seconds);
		print ("   %s\n", client->netconnection->address);
	}
}


/*
==================
Host_God_f

Sets client to godmode
==================
*/
void Host_God_f (void)
{
	if (cmd_source == src_command)
	{
		Cmd_ForwardToServer ();
		return;
	}

	if (SVProgs->GlobalStruct->deathmatch && !host_client->privileged)
		return;

	sv_player->v.flags = (int) sv_player->v.flags ^ FL_GODMODE;

	if (!((int) sv_player->v.flags & FL_GODMODE))
		SV_ClientPrintf ("godmode OFF\n");
	else
		SV_ClientPrintf ("godmode ON\n");
}

void Host_Notarget_f (void)
{
	if (cmd_source == src_command)
	{
		Cmd_ForwardToServer ();
		return;
	}

	if (SVProgs->GlobalStruct->deathmatch && !host_client->privileged)
		return;

	sv_player->v.flags = (int) sv_player->v.flags ^ FL_NOTARGET;

	if (!((int) sv_player->v.flags & FL_NOTARGET))
		SV_ClientPrintf ("notarget OFF\n");
	else SV_ClientPrintf ("notarget ON\n");
}

bool noclip_anglehack;

void Host_Noclip_f (void)
{
	if (cmd_source == src_command)
	{
		Cmd_ForwardToServer ();
		return;
	}

	if (SVProgs->GlobalStruct->deathmatch && !host_client->privileged)
		return;

	if (sv_player->v.movetype != MOVETYPE_NOCLIP)
	{
		noclip_anglehack = true;
		sv_player->v.movetype = MOVETYPE_NOCLIP;
		SV_ClientPrintf ("noclip ON\n");
	}
	else
	{
		noclip_anglehack = false;
		sv_player->v.movetype = MOVETYPE_WALK;
		SV_ClientPrintf ("noclip OFF\n");
	}
}

/*
==================
Host_Fly_f

Sets client to flymode
==================
*/
void Host_Fly_f (void)
{
	if (cmd_source == src_command)
	{
		Cmd_ForwardToServer ();
		return;
	}

	if (SVProgs->GlobalStruct->deathmatch && !host_client->privileged)
		return;

	if (sv_player->v.movetype != MOVETYPE_FLY)
	{
		sv_player->v.movetype = MOVETYPE_FLY;
		SV_ClientPrintf ("flymode ON\n");
	}
	else
	{
		sv_player->v.movetype = MOVETYPE_WALK;
		SV_ClientPrintf ("flymode OFF\n");
	}
}


/*
==================
Host_Ping_f

==================
*/
void Host_Ping_f (void)
{
	int		i, j;
	float	total;
	client_t	*client;

	if (cmd_source == src_command)
	{
		Cmd_ForwardToServer ();
		return;
	}

	SV_ClientPrintf ("Client ping times:\n");

	for (i = 0, client = svs.clients; i < svs.maxclients; i++, client++)
	{
		if (!client->active)
			continue;

		total = 0;

		for (j = 0; j < NUM_PING_TIMES; j++)
			total += client->ping_times[j];

		total /= NUM_PING_TIMES;
		SV_ClientPrintf ("%4i %s\n", (int) (total * 1000), client->name);
	}
}

/*
===============================================================================

SERVER TRANSITIONS

===============================================================================
*/


/*
======================
Host_Map_f

handle a
map <servername>
command from the console.  Active clients are kicked off.
======================
*/
void Host_Map_f (void)
{
	int		i;
	char	name[MAX_QPATH];

	if (cmd_source != src_command)
		return;

	cls.demonum = -1;		// stop demo loop in case this fails

	CL_Disconnect ();
	Host_ShutdownServer (false);

	key_dest = key_game;			// remove console or menu
	SCR_BeginLoadingPlaque ();

	cls.mapstring[0] = 0;

	// umm - did i write this?  whatever, it breaks the spawnparms below...
	// nope, this is original ID code.
	for (i = 0; i < Cmd_Argc (); i++)
	{
		strcat (cls.mapstring, Cmd_Argv (i));
		strcat (cls.mapstring, " ");
	}

	strcat (cls.mapstring, "\n");

	svs.serverflags = 0;			// haven't completed an episode yet
	Q_strncpy (name, Cmd_Argv (1), 63);

	SV_SpawnServer (name);

	if (!sv.active)
		return;

	strcpy (cls.spawnparms, "");

	for (i = 2; i < Cmd_Argc (); i++)
	{
		strcat (cls.spawnparms, Cmd_Argv (i));
		strcat (cls.spawnparms, " ");
	}

	Cmd_ExecuteString ("connect local", src_command);
}

/*
==================
Host_Changelevel_f

Goes to a new map, taking all clients along
==================
*/
void Host_Changelevel_f (void)
{
	char	level[MAX_QPATH];

	if (Cmd_Argc() != 2)
	{
		Con_Printf ("changelevel <levelname> : continue game on a new level\n");
		return;
	}

	if (!sv.active || cls.demoplayback)
	{
		Con_Printf ("Only the server may changelevel\n");
		return;
	}

	SV_SaveSpawnparms ();
	Q_strncpy (level, Cmd_Argv (1), 63);
	SV_SpawnServer (level);
}


/*
==================
Host_Restart_f

Restarts the current server for a dead player
==================
*/
void Host_Restart_f (void)
{
	char	mapname[MAX_QPATH];

	if (cls.demoplayback || !sv.active)
		return;

	if (cmd_source != src_command)
		return;

	Q_strncpy (mapname, sv.name, 63);	// must copy out, because it gets cleared
	// in sv_spawnserver

	SV_SpawnServer (mapname);
}

/*
==================
Host_Reconnect_f

This command causes the client to wait for the signon messages again.
This is sent just before a server changes levels
==================
*/
void Host_Reconnect_f (void)
{
	SCR_BeginLoadingPlaque ();
	cls.signon = 0;		// need new connection messages
}

/*
=====================
Host_Connect_f

User command to connect to server
=====================
*/
void NET_SetPort (char *newport);
char server_name[MAX_QPATH];

void Host_Connect_f (void)
{
	char	name[MAX_QPATH];

	cls.demonum = -1;		// stop demo loop in case this fails

	if (cls.demoplayback)
	{
		CL_StopPlayback ();
		CL_Disconnect ();
	}

	Q_strncpy (name, Cmd_Argv (1), 63);

	// use for connect host:port format
	char *portname = NULL;

	for (int i = strlen (name); i; i--)
	{
		if (name[i] == ':')
		{
			portname = &name[i + 1];
			name[i] = 0;
			break;
		}
	}

	// if a port was specified switch to it
	if (portname) NET_SetPort (portname);

	CL_EstablishConnection (name);
	Host_Reconnect_f ();

	// copy out for web download
	Q_strncpy (server_name, name, 63);
}


/*
===============================================================================

LOAD / SAVE GAME

===============================================================================
*/

/*
===============
Host_SavegameComment

Writes a SAVEGAME_COMMENT_LENGTH character comment describing the current
===============
*/
void Host_SavegameComment (char *text)
{
	int		i;
	char	kills[20];

	// fill with spaces
	for (i = 0; i < SAVEGAME_COMMENT_LENGTH; i++)
		text[i] = ' ';

	// copy in level name - tough shit if it's > 21
	Q_strncpy (text, cl.levelname, 21);

	// create the kills text
	_snprintf (kills, 20, "kills:%3i/%3i", cl.stats[STAT_MONSTERS], cl.stats[STAT_TOTALMONSTERS]);

	// copy it in - kills is always guaranteed to start at 22
	memcpy (text + 22, kills, strlen (kills));

	// convert space to _ to make stdio happy
	// also convert \0 - this is a hack to fix a hack in aguirre quake
	for (i = 0; i < SAVEGAME_COMMENT_LENGTH; i++)
		if (text[i] < 33 || text[i] > 126)
			text[i] = '_';

	// null terminate
	text[SAVEGAME_COMMENT_LENGTH] = '\0';
}


/*
===============
Host_Savegame_f
===============
*/
void SCR_Mapshot_f (char *shotname, bool report, bool overwrite);
void Draw_InvalidateMapshot (void);
void Menu_DirtySaveLoadMenu (void);

// note - this is only intended to be called from the menus or from Host_Savegame_f,
// where all the necessary validation and setup has already been performed!!!
void Host_DoSavegame (char *savename)
{
	char	name[256];
	FILE	*f;
	int		i;
	char	comment[SAVEGAME_COMMENT_LENGTH + 1];

	for (i = 0;; i++)
	{
		if (savename[i] == 0) break;

		// check for invalid chars
		if (savename[i] == '/' ||
				savename[i] == '\\' ||
				savename[i] == ':' ||
				savename[i] == '*' ||
				savename[i] == '?' ||
				savename[i] == '"' ||
				savename[i] == '<' ||
				savename[i] == '>' ||
				savename[i] == '|')
		{
			Con_Printf ("Invalid character: \"%c\" detected in save name\n", savename[i]);
			return;
		}
	}

	_snprintf (name, 256, "%s/save/%s", com_gamedir, savename);
	COM_DefaultExtension (name, ".sav");

	f = fopen (name, "w");

	if (!f)
	{
		Con_Printf ("ERROR: couldn't open %s.\n", name);
		return;
	}

	// saving can cause sound to stall while disk IO is busy so clear the buffer first
	S_ClearBuffer ();

	fprintf (f, "%i\n", SAVEGAME_VERSION);
	Host_SavegameComment (comment);
	fprintf (f, "%s\n", comment);

	for (i = 0; i < NUM_SPAWN_PARMS; i++)
		fprintf (f, "%f\n", svs.clients->spawn_parms[i]);

	fprintf (f, "%d\n", current_skill);
	fprintf (f, "%s\n", sv.name);
	fprintf (f, "%f\n", sv.time);

	// write the light styles
	for (i = 0; i < MAX_LIGHTSTYLES; i++)
	{
		if (sv.lightstyles[i])
			fprintf (f, "%s\n", sv.lightstyles[i]);
		else fprintf (f, "m\n");
	}

	ED_WriteGlobals (f);

	for (i = 0; i < SVProgs->NumEdicts; i++)
	{
		ED_Write (f, GetEdictForNumber (i));
		fflush (f);
	}

	fclose (f);

	Con_Printf ("Saved game to \"%s\"\n", savename);

	// write a mapshot (invalidate the previous one as the image will have changed)
	Draw_InvalidateMapshot ();
	SCR_Mapshot_f (name, false, true);
}


void Host_Savegame_f (void)
{
	int i;

	if (cmd_source != src_command) return;

	if (!sv.active)
	{
		Con_Printf ("Not playing a local game.\n");
		return;
	}

	if (cl.intermission)
	{
		Con_Printf ("Can't save in intermission.\n");
		return;
	}

	if (svs.maxclients != 1)
	{
		Con_Printf ("Can't save multiplayer games.\n");
		return;
	}

	if (Cmd_Argc() < 2)
	{
		Con_Printf ("save <savename> : save a game\n");
		return;
	}

	if (strstr (Cmd_Argv (1), ".."))
	{
		Con_Printf ("Relative pathnames are not allowed.\n");
		return;
	}

	for (i = 0; i < svs.maxclients; i++)
	{
		if (svs.clients[i].active && (svs.clients[i].edict->v.health <= 0))
		{
			Con_Printf ("Can't savegame with a dead player\n");
			return;
		}
	}

	Host_DoSavegame (Cmd_Args ());

	// this is only called from the command-line (or qc) now, so we must dirty the menus
	Menu_DirtySaveLoadMenu ();
}


/*
===============
Host_Loadgame_f
===============
*/
void Host_Loadgame_f (void)
{
	char	name[MAX_PATH];
	FILE	*f;
	char	mapname[MAX_QPATH];
	float	time, tfloat;
	char	str[32768], *start;
	int		i, r;
	edict_t	*ent;
	int		entnum;
	int		version;
	float	spawn_parms[NUM_SPAWN_PARMS];

	if (cmd_source != src_command)
		return;

	if (Cmd_Argc() < 2)
	{
		Con_Printf ("load <savename> : load a game\n");
		return;
	}

	char *loadname = Cmd_Args ();

	for (i = 0;; i++)
	{
		if (loadname[i] == 0) break;

		// check for invalid chars
		if (loadname[i] == '/' ||
			loadname[i] == '\\' ||
			loadname[i] == ':' ||
			loadname[i] == '*' ||
			loadname[i] == '?' ||
			loadname[i] == '"' ||
			loadname[i] == '<' ||
			loadname[i] == '>' ||
			loadname[i] == '|')
		{
			Con_Printf ("Invalid character: \"%c\" detected in load name\n", loadname[i]);
			return;
		}
	}

	cls.demonum = -1;		// stop demo loop in case this fails

	_snprintf (name, 128, "%s/save/%s", com_gamedir, loadname);
	COM_DefaultExtension (name, ".sav");

	// we can't call SCR_BeginLoadingPlaque, because too much stack space has
	// been used.  The menu calls it before stuffing loadgame command
	//	SCR_BeginLoadingPlaque ();

	Con_Printf ("Loading game from \"%s\"...\n", loadname);
	f = fopen (name, "r");

	if (!f)
	{
		// try a regular one (compatibility with old games)
		_snprintf (name, 128, "%s/%s", com_gamedir, loadname);
		COM_DefaultExtension (name, ".sav");

		f = fopen (name, "r");

		if (!f)
		{
			Con_Printf ("ERROR: couldn't open.\n");
			return;
		}
	}

	fscanf (f, "%i\n", &version);

	if (version != SAVEGAME_VERSION)
	{
		fclose (f);
		Con_Printf ("Savegame is version %i, not %i\n", version, SAVEGAME_VERSION);
		return;
	}

	fscanf (f, "%s\n", str);

	for (i = 0; i < NUM_SPAWN_PARMS; i++)
		fscanf (f, "%f\n", &spawn_parms[i]);

	// this silliness is so we can load 1.06 save files, which have float skill values
	fscanf (f, "%f\n", &tfloat);
	current_skill = (int) (tfloat + 0.1);
	Cvar_Set ("skill", (float) current_skill);

	fscanf (f, "%s\n", mapname);
	fscanf (f, "%f\n", &time);

	CL_Disconnect_f ();

	// ensure these aren't silly
	// moved to after the disconnect because they hang slightly on load
	Cvar_Set ("deathmatch", 0.0f);
	Cvar_Set ("coop", 0.0f);
	Cvar_Set ("teamplay", 0.0f);

	SV_SpawnServer (mapname);

	if (!sv.active)
	{
		Con_Printf ("Couldn't load map\n");
		return;
	}

	sv.paused = true;		// pause until all clients connect
	sv.loadgame = true;

	// load the light styles
	for (i = 0; i < MAX_LIGHTSTYLES; i++)
	{
		fscanf (f, "%s\n", str);
		sv.lightstyles[i] = (char *) MainHunk->Alloc (strlen (str) + 1);
		strcpy (sv.lightstyles[i], str);
	}

	// load the edicts out of the savegame file
	entnum = -1;		// -1 is the globals

	while (!feof (f))
	{
		for (i = 0; i < sizeof (str) - 1; i++)
		{
			r = fgetc (f);

			if (r == EOF || !r)
				break;

			str[i] = r;

			if (r == '}')
			{
				i++;
				break;
			}
		}

		if (i == sizeof (str) - 1)
			Sys_Error ("Loadgame buffer overflow");

		str[i] = 0;
		start = str;
		start = COM_Parse (str);

		if (!com_token[0])
			break;		// end of file

		if (strcmp (com_token, "{"))
		{
#if 0
			if (!strcmp (com_token, "/*"))
			{
				// dp puts /*..*/ comments in it's save games.  evil evil evil.
				// fortunately they're at the end so just skip over them.  must mod COM_Parse to skip them automatically...
				break;
			}
			else
#endif
			Sys_Error ("First token isn't a brace");
		}

		if (entnum == -1)
		{
			// parse the global vars
			ED_ParseGlobals (start);
		}
		else
		{
			// parse an edict
			ent = GetEdictForNumber (entnum);
			memset (&ent->v, 0, SVProgs->QC->entityfields * 4);
			ent->free = false;
			ED_ParseEdict (start, ent);

			// link it into the bsp tree
			if (!ent->free)
				SV_LinkEdict (ent, false);
		}

		entnum++;
	}

	SVProgs->NumEdicts = entnum;
	sv.time = time;

	fclose (f);

	for (i = 0; i < NUM_SPAWN_PARMS; i++)
		svs.clients->spawn_parms[i] = spawn_parms[i];

	CL_EstablishConnection ("local");
	Host_Reconnect_f ();
}

//============================================================================

/*
======================
Host_Name_f
======================
*/
void Host_Name_f (void)
{
	char	*newName;
	int a, b, c;	// JPG 1.05 - ip address logging

	if (Cmd_Argc () == 1)
	{
		Con_Printf ("\"name\" is \"%s\"\n", cl_name.string);
		return;
	}

	if (Cmd_Argc () == 2)
		newName = Cmd_Argv (1);
	else
		newName = Cmd_Args();

	newName[15] = 0;

	if (cmd_source == src_command)
	{
		if (strcmp (cl_name.string, newName) == 0)
			return;

		// set
		Cvar_Set ("_cl_name", newName);

		if (cls.state == ca_connected)
			Cmd_ForwardToServer ();

		return;
	}

	if (host_client->name[0] && strcmp (host_client->name, "unconnected"))
		if (strcmp (host_client->name, newName) != 0)
			Con_Printf ("%s renamed to %s\n", host_client->name, newName);

	strcpy (host_client->name, newName);
	host_client->edict->v.netname = host_client->name - SVProgs->Strings;

	// JPG 1.05 - log the IP address
	if (sscanf (host_client->netconnection->address, "%d.%d.%d", &a, &b, &c) == 3)
		IPLog_Add ((a << 16) | (b << 8) | c, newName);

	// send notification to all clients

	MSG_WriteByte (&sv.reliable_datagram, svc_updatename);
	MSG_WriteByte (&sv.reliable_datagram, host_client - svs.clients);
	MSG_WriteString (&sv.reliable_datagram, host_client->name);
}


void Host_Version_f (void)
{
	Con_Printf ("Version %4.2f\n", VERSION);
	Con_Printf ("Exe: "__TIME__" "__DATE__"\n");
}

#ifdef IDGODS
void Host_Please_f (void)
{
	client_t *cl;
	int			j;

	if (cmd_source != src_command)
		return;

	if ((Cmd_Argc () == 3) && strcmp (Cmd_Argv (1), "#") == 0)
	{
		j = atof (Cmd_Argv (2)) - 1;

		if (j < 0 || j >= svs.maxclients)
			return;

		if (!svs.clients[j].active)
			return;

		cl = &svs.clients[j];

		if (cl->privileged)
		{
			cl->privileged = false;
			cl->edict->v.flags = (int) cl->edict->v.flags & ~ (FL_GODMODE | FL_NOTARGET);
			cl->edict->v.movetype = MOVETYPE_WALK;
			noclip_anglehack = false;
		}
		else
			cl->privileged = true;
	}

	if (Cmd_Argc () != 2)
		return;

	for (j = 0, cl = svs.clients; j < svs.maxclients; j++, cl++)
	{
		if (!cl->active)
			continue;

		if (stricmp (cl->name, Cmd_Argv (1)) == 0)
		{
			if (cl->privileged)
			{
				cl->privileged = false;
				cl->edict->v.flags = (int) cl->edict->v.flags & ~ (FL_GODMODE | FL_NOTARGET);
				cl->edict->v.movetype = MOVETYPE_WALK;
				noclip_anglehack = false;
			}
			else
				cl->privileged = true;

			break;
		}
	}
}
#endif


#define SAY_BUF_LEN 2048

void Host_Say (bool teamonly)
{
	client_t *client;
	client_t *save;
	int		j;
	char	*p;
	char	text[SAY_BUF_LEN + 2];
	bool	fromServer = false;

	if (cmd_source == src_command)
	{
		Cmd_ForwardToServer ();
		return;
	}

	if (Cmd_Argc () < 2)
		return;

	save = host_client;

	p = Cmd_Args ();

	// remove quotes if present
	if (*p == '"')
	{
		p++;
		p[strlen (p) - 1] = 0;
	}

	// turn on color set 1
	if (!fromServer)
		_snprintf (text, SAY_BUF_LEN, "%c%s: ", 1, save->name);
	else _snprintf (text, SAY_BUF_LEN, "%c<%s> ", 1, hostname.string);

	j = sizeof (text) - 2 - strlen (text);  // -2 for /n and null terminator

	if (strlen (p) > j)
		p[j] = 0;

	strcat (text, p);
	strcat (text, "\n");

	for (j = 0, client = svs.clients; j < svs.maxclients; j++, client++)
	{
		if (!client || !client->active || !client->spawned)
			continue;

		if (teamplay.value && teamonly && client->edict->v.team != save->edict->v.team)
			continue;

		host_client = client;
		SV_ClientPrintf ("%s", text);
	}

	host_client = save;
}


void Host_Say_f (void)
{
	Host_Say (false);
}


void Host_Say_Team_f (void)
{
	Host_Say (true);
}


void Host_Tell_f (void)
{
	client_t *client;
	client_t *save;
	int		j;
	char	*p;
	char	text[SAY_BUF_LEN + 2];

	if (cmd_source == src_command)
	{
		Cmd_ForwardToServer ();
		return;
	}

	if (Cmd_Argc () < 3)
		return;

	strcpy (text, host_client->name);
	strcat (text, ": ");

	p = Cmd_Args();

	// remove quotes if present
	if (*p == '"')
	{
		p++;
		p[strlen (p)-1] = 0;
	}

	// check length & truncate if necessary
	j = sizeof (text) - 2 - strlen (text); // -2 for /n and null terminator

	if (strlen (p) > j)
		p[j] = 0;

	strcat (text, p);
	strcat (text, "\n");

	save = host_client;

	for (j = 0, client = svs.clients; j < svs.maxclients; j++, client++)
	{
		if (!client->active || !client->spawned)
			continue;

		if (stricmp (client->name, Cmd_Argv (1)))
			continue;

		host_client = client;
		SV_ClientPrintf ("%s", text);
		break;
	}

	host_client = save;
}


/*
==================
Host_Color_f
==================
*/
void Host_Color_f (void)
{
	int		top, bottom;
	int		playercolor;

	if (Cmd_Argc() == 1)
	{
		Con_Printf ("\"color\" is \"%i %i\"\n", ((int) cl_color.value) >> 4, ((int) cl_color.value) & 0x0f);
		Con_Printf ("color <0-13> [0-13]\n");
		return;
	}

	if (Cmd_Argc() == 2)
		top = bottom = atoi (Cmd_Argv (1));
	else
	{
		top = atoi (Cmd_Argv (1));
		bottom = atoi (Cmd_Argv (2));
	}

	top &= 15;

	if (top > 13)
		top = 13;

	bottom &= 15;

	if (bottom > 13)
		bottom = 13;

	playercolor = top * 16 + bottom;

	if (cmd_source == src_command)
	{
		Cvar_Set ("_cl_color", playercolor);

		if (cls.state == ca_connected)
			Cmd_ForwardToServer ();

		return;
	}

	host_client->colors = playercolor;
	host_client->edict->v.team = bottom + 1;

	// send notification to all clients
	MSG_WriteByte (&sv.reliable_datagram, svc_updatecolors);
	MSG_WriteByte (&sv.reliable_datagram, host_client - svs.clients);
	MSG_WriteByte (&sv.reliable_datagram, host_client->colors);
}

/*
==================
Host_Kill_f
==================
*/
void Host_Kill_f (void)
{
	if (cmd_source == src_command)
	{
		Cmd_ForwardToServer ();
		return;
	}

	if (sv_player->v.health <= 0)
	{
		SV_ClientPrintf ("Can't suicide -- allready dead!\n");
		return;
	}

	SVProgs->GlobalStruct->time = sv.time;
	SVProgs->GlobalStruct->self = EDICT_TO_PROG (sv_player);
	SVProgs->ExecuteProgram (SVProgs->GlobalStruct->ClientKill);
}


/*
==================
Host_Pause_f
==================
*/
void Host_Pause_f (void)
{
	if (cmd_source == src_command)
	{
		Cmd_ForwardToServer ();
		return;
	}

	if (!pausable.value)
		SV_ClientPrintf ("Pause not allowed.\n");
	else
	{
		sv.paused ^= 1;

		if (sv.paused)
		{
			SV_BroadcastPrintf ("%s paused the game\n", SVProgs->Strings + sv_player->v.netname);
		}
		else
		{
			SV_BroadcastPrintf ("%s unpaused the game\n", SVProgs->Strings + sv_player->v.netname);
		}

		// send notification to all clients
		MSG_WriteByte (&sv.reliable_datagram, svc_setpause);
		MSG_WriteByte (&sv.reliable_datagram, sv.paused);
	}
}

//===========================================================================


/*
==================
Host_PreSpawn_f
==================
*/
void Host_PreSpawn_f (void)
{
	if (cmd_source == src_command)
	{
		Con_Printf ("prespawn is not valid from the console\n");
		return;
	}

	if (host_client->spawned)
	{
		Con_Printf ("prespawn not valid -- allready spawned\n");
		return;
	}

	SZ_Write (&host_client->message, sv.signon.data, sv.signon.cursize);
	MSG_WriteByte (&host_client->message, svc_signonnum);
	MSG_WriteByte (&host_client->message, 2);
	host_client->sendsignon = true;
}


/*
==================
Host_Spawn_f
==================
*/
void Neh_GameStart (void);

void Host_Spawn_f (void)
{
	int		i;
	client_t	*client;
	edict_t	*ent;

	if (cmd_source == src_command)
	{
		Con_Printf ("spawn is not valid from the console\n");
		return;
	}

	if (host_client->spawned)
	{
		Con_Printf ("Spawn not valid -- allready spawned\n");
		return;
	}

	// run the entrance script
	if (sv.loadgame)
	{
		// loaded games are fully inited allready
		// if this is the last client to be connected, unpause
		sv.paused = false;

		if (nehahra) Neh_GameStart ();
	}
	else
	{
		// set up the edict
		ent = host_client->edict;

		memset (&ent->v, 0, SVProgs->QC->entityfields * 4);
		ent->v.colormap = GetNumberForEdict (ent);
		ent->v.team = (host_client->colors & 15) + 1;
		ent->v.netname = host_client->name - SVProgs->Strings;

		// copy spawn parms out of the client_t

		for (i = 0; i < NUM_SPAWN_PARMS; i++)
			(&SVProgs->GlobalStruct->parm1) [i] = host_client->spawn_parms[i];

		// call the spawn function

		SVProgs->GlobalStruct->time = sv.time;
		SVProgs->GlobalStruct->self = EDICT_TO_PROG (sv_player);
		SVProgs->ExecuteProgram (SVProgs->GlobalStruct->ClientConnect);
		SVProgs->ExecuteProgram (SVProgs->GlobalStruct->PutClientInServer);
	}

	// send all current names, colors, and frag counts
	SZ_Clear (&host_client->message);

	// send time of update
	MSG_WriteByte (&host_client->message, svc_time);
	MSG_WriteFloat (&host_client->message, sv.time);

	for (i = 0, client = svs.clients; i < svs.maxclients; i++, client++)
	{
		MSG_WriteByte (&host_client->message, svc_updatename);
		MSG_WriteByte (&host_client->message, i);
		MSG_WriteString (&host_client->message, client->name);
		MSG_WriteByte (&host_client->message, svc_updatefrags);
		MSG_WriteByte (&host_client->message, i);
		MSG_WriteShort (&host_client->message, client->old_frags);
		MSG_WriteByte (&host_client->message, svc_updatecolors);
		MSG_WriteByte (&host_client->message, i);
		MSG_WriteByte (&host_client->message, client->colors);
	}

	// send all current light styles
	for (i = 0; i < MAX_LIGHTSTYLES; i++)
	{
		MSG_WriteByte (&host_client->message, svc_lightstyle);
		MSG_WriteByte (&host_client->message, (char) i);
		MSG_WriteString (&host_client->message, sv.lightstyles[i]);
	}

	// send some stats
	MSG_WriteByte (&host_client->message, svc_updatestat);
	MSG_WriteByte (&host_client->message, STAT_TOTALSECRETS);
	MSG_WriteLong (&host_client->message, SVProgs->GlobalStruct->total_secrets);

	MSG_WriteByte (&host_client->message, svc_updatestat);
	MSG_WriteByte (&host_client->message, STAT_TOTALMONSTERS);
	MSG_WriteLong (&host_client->message, SVProgs->GlobalStruct->total_monsters);

	MSG_WriteByte (&host_client->message, svc_updatestat);
	MSG_WriteByte (&host_client->message, STAT_SECRETS);
	MSG_WriteLong (&host_client->message, SVProgs->GlobalStruct->found_secrets);

	MSG_WriteByte (&host_client->message, svc_updatestat);
	MSG_WriteByte (&host_client->message, STAT_MONSTERS);
	MSG_WriteLong (&host_client->message, SVProgs->GlobalStruct->killed_monsters);

	// send a fixangle
	// Never send a roll angle, because savegames can catch the server
	// in a state where it is expecting the client to correct the angle
	// and it won't happen if the game was just loaded, so you wind up
	// with a permanent head tilt
	ent = GetEdictForNumber (1 + (host_client - svs.clients));
	MSG_WriteByte (&host_client->message, svc_setangle);

	for (i = 0; i < 2; i++)
		MSG_WriteAngle (&host_client->message, ent->v.angles[i], sv.Protocol, sv.PrototcolFlags);

	MSG_WriteAngle (&host_client->message, 0, sv.Protocol, sv.PrototcolFlags);

	SV_WriteClientdataToMessage (sv_player, &host_client->message);

	MSG_WriteByte (&host_client->message, svc_signonnum);
	MSG_WriteByte (&host_client->message, 3);
	host_client->sendsignon = true;
}

/*
==================
Host_Begin_f
==================
*/
void Host_Begin_f (void)
{
	if (cmd_source == src_command)
	{
		Con_Printf ("begin is not valid from the console\n");
		return;
	}

	host_client->spawned = true;
}

//===========================================================================


/*
==================
Host_Kick_f

Kicks a user off of the server
==================
*/
void Host_Kick_f (void)
{
	char		*who;
	char		*message = NULL;
	client_t	*save;
	int			i;
	bool	byNumber = false;

	if (cmd_source == src_command)
	{
		if (!sv.active)
		{
			Cmd_ForwardToServer ();
			return;
		}
	}
	else if (SVProgs->GlobalStruct->deathmatch && !host_client->privileged)
		return;

	save = host_client;

	if (Cmd_Argc() > 2 && strcmp (Cmd_Argv (1), "#") == 0)
	{
		i = atof (Cmd_Argv (2)) - 1;

		if (i < 0 || i >= svs.maxclients)
			return;

		if (!svs.clients[i].active)
			return;

		host_client = &svs.clients[i];
		byNumber = true;
	}
	else
	{
		for (i = 0, host_client = svs.clients; i < svs.maxclients; i++, host_client++)
		{
			if (!host_client->active)
				continue;

			if (stricmp (host_client->name, Cmd_Argv (1)) == 0)
				break;
		}
	}

	if (i < svs.maxclients)
	{
		if (cmd_source == src_command)
			who = cl_name.string;
		else
			who = save->name;

		// can't kick yourself!
		if (host_client == save)
			return;

		if (Cmd_Argc() > 2)
		{
			message = COM_Parse (Cmd_Args());

			if (byNumber)
			{
				message++;							// skip the #

				while (*message == ' ')				// skip white space
					message++;

				message += strlen (Cmd_Argv (2));	// skip the number
			}

			while (*message && *message == ' ')
				message++;
		}

		if (message)
			SV_ClientPrintf ("Kicked by %s: %s\n", who, message);
		else
			SV_ClientPrintf ("Kicked by %s\n", who);

		SV_DropClient (false);
	}

	host_client = save;
}

/*
===============================================================================

DEBUGGING TOOLS

===============================================================================
*/

/*
==================
Host_Give_f
==================
*/
void Host_Give_f (void)
{
	char	*t;
	int		v;
	eval_t	*val;

	if (cmd_source == src_command)
	{
		Cmd_ForwardToServer ();
		return;
	}

	if (SVProgs->GlobalStruct->deathmatch && !host_client->privileged)
		return;

	t = Cmd_Argv (1);
	v = atoi (Cmd_Argv (2));

	switch (t[0])
	{
	case '0':
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
	case '8':
	case '9':

		// MED 01/04/97 added hipnotic give stuff
		if (hipnotic)
		{
			if (t[0] == '6')
			{
				if (t[1] == 'a')
					sv_player->v.items = (int) sv_player->v.items | HIT_PROXIMITY_GUN;
				else
					sv_player->v.items = (int) sv_player->v.items | IT_GRENADE_LAUNCHER;
			}
			else if (t[0] == '9')
				sv_player->v.items = (int) sv_player->v.items | HIT_LASER_CANNON;
			else if (t[0] == '0')
				sv_player->v.items = (int) sv_player->v.items | HIT_MJOLNIR;
			else if (t[0] >= '2')
				sv_player->v.items = (int) sv_player->v.items | (IT_SHOTGUN << (t[0] - '2'));
		}
		else
		{
			if (t[0] >= '2')
				sv_player->v.items = (int) sv_player->v.items | (IT_SHOTGUN << (t[0] - '2'));
		}

		break;

	case 's':

		if (rogue)
		{
			val = GETEDICTFIELDVALUEFAST (sv_player, ed_ammo_shells1);

			if (val) val->_float = v;
		}

		sv_player->v.ammo_shells = v;
		break;
	case 'n':

		if (rogue)
		{
			val = GETEDICTFIELDVALUEFAST (sv_player, ed_ammo_nails1);

			if (val)
			{
				val->_float = v;

				if (sv_player->v.weapon <= IT_LIGHTNING)
					sv_player->v.ammo_nails = v;
			}
		}
		else
		{
			sv_player->v.ammo_nails = v;
		}

		break;
	case 'l':

		if (rogue)
		{
			val = GETEDICTFIELDVALUEFAST (sv_player, ed_ammo_lava_nails);

			if (val)
			{
				val->_float = v;

				if (sv_player->v.weapon > IT_LIGHTNING)
					sv_player->v.ammo_nails = v;
			}
		}

		break;
	case 'r':

		if (rogue)
		{
			val = GETEDICTFIELDVALUEFAST (sv_player, ed_ammo_rockets1);

			if (val)
			{
				val->_float = v;

				if (sv_player->v.weapon <= IT_LIGHTNING)
					sv_player->v.ammo_rockets = v;
			}
		}
		else
		{
			sv_player->v.ammo_rockets = v;
		}

		break;
	case 'm':

		if (rogue)
		{
			val = GETEDICTFIELDVALUEFAST (sv_player, ed_ammo_multi_rockets);

			if (val)
			{
				val->_float = v;

				if (sv_player->v.weapon > IT_LIGHTNING)
					sv_player->v.ammo_rockets = v;
			}
		}

		break;

	case 'a':

		// remove all current armor
		if (rogue)
			sv_player->v.items = ((int) sv_player->v.items) & ~ (RIT_ARMOR1 | RIT_ARMOR2 | RIT_ARMOR3);
		else sv_player->v.items = ((int) sv_player->v.items) & ~ (IT_ARMOR1 | IT_ARMOR2 | IT_ARMOR3);

		// the types here came from id1 qc and may not be fully accurate for mods
		// who knows? who cares?  it's cheating anyway!!!
		if (v <= 100)
		{
			if (rogue)
				sv_player->v.items = ((int) sv_player->v.items) | RIT_ARMOR1;
			else sv_player->v.items = ((int) sv_player->v.items) | IT_ARMOR1;

			if (sv_player->v.armortype < 0.3f) sv_player->v.armortype = 0.3f;
		}
		else if (v <= 150)
		{
			if (rogue)
				sv_player->v.items = ((int) sv_player->v.items) | RIT_ARMOR2;
			else sv_player->v.items = ((int) sv_player->v.items) | IT_ARMOR2;

			if (sv_player->v.armortype < 0.6f) sv_player->v.armortype = 0.6f;
		}
		else
		{
			if (rogue)
				sv_player->v.items = ((int) sv_player->v.items) | RIT_ARMOR3;
			else sv_player->v.items = ((int) sv_player->v.items) | IT_ARMOR3;

			if (sv_player->v.armortype < 0.8f) sv_player->v.armortype = 0.8f;
		}

		sv_player->v.armorvalue = v;
		break;

	case 'h':

		// don't die!
		if (v < 25) v = 25;

		sv_player->v.health = v;
		break;

	case 'c':

		if (rogue)
		{
			val = GETEDICTFIELDVALUEFAST (sv_player, ed_ammo_cells1);

			if (val)
			{
				val->_float = v;

				if (sv_player->v.weapon <= IT_LIGHTNING)
					sv_player->v.ammo_cells = v;
			}
		}
		else
		{
			sv_player->v.ammo_cells = v;
		}

		break;

	case 'p':

		if (rogue)
		{
			val = GETEDICTFIELDVALUEFAST (sv_player, ed_ammo_plasma);

			if (val)
			{
				val->_float = v;

				if (sv_player->v.weapon > IT_LIGHTNING)
					sv_player->v.ammo_cells = v;
			}
		}

		break;
	}
}

edict_t	*FindViewthing (void)
{
	int		i;
	edict_t	*e;

	for (i = 0; i < SVProgs->NumEdicts; i++)
	{
		e = GetEdictForNumber (i);

		if (!strcmp (SVProgs->Strings + e->v.classname, "viewthing"))
			return e;
	}

	Con_Printf ("No viewthing on map\n");
	return NULL;
}

/*
==================
Host_Viewmodel_f
==================
*/
void Host_Viewmodel_f (void)
{
	edict_t	*e;
	model_t	*m;

	e = FindViewthing ();

	if (!e)
		return;

	m = Mod_ForName (Cmd_Argv (1), false);

	if (!m)
	{
		Con_Printf ("Can't load %s\n", Cmd_Argv (1));
		return;
	}

	e->v.frame = 0;
	cl.model_precache[(int) e->v.modelindex] = m;
}

/*
==================
Host_Viewframe_f
==================
*/
void Host_Viewframe_f (void)
{
	edict_t	*e;
	int		f;
	model_t	*m;

	e = FindViewthing ();

	if (!e)
		return;

	m = cl.model_precache[(int) e->v.modelindex];

	f = atoi (Cmd_Argv (1));

	if (f >= m->numframes)
		f = m->numframes - 1;

	e->v.frame = f;
}


void PrintFrameName (model_t *m, int frame)
{
	if (m->type != mod_alias) return;

	aliashdr_t 			*hdr;
	maliasframedesc_t	*pframedesc;

	hdr = m->aliashdr;

	if (!hdr) return;

	pframedesc = &hdr->frames[frame];

	Con_Printf ("frame %i: %s\n", frame, pframedesc->name);
}

/*
==================
Host_Viewnext_f
==================
*/
void Host_Viewnext_f (void)
{
	edict_t	*e;
	model_t	*m;

	e = FindViewthing ();

	if (!e)
		return;

	m = cl.model_precache[(int) e->v.modelindex];

	e->v.frame = e->v.frame + 1;

	if (e->v.frame >= m->numframes)
		e->v.frame = m->numframes - 1;

	PrintFrameName (m, e->v.frame);
}

/*
==================
Host_Viewprev_f
==================
*/
void Host_Viewprev_f (void)
{
	edict_t	*e;
	model_t	*m;

	e = FindViewthing ();

	if (!e)
		return;

	m = cl.model_precache[(int) e->v.modelindex];

	e->v.frame = e->v.frame - 1;

	if (e->v.frame < 0)
		e->v.frame = 0;

	PrintFrameName (m, e->v.frame);
}

/*
===============================================================================

DEMO LOOP CONTROL

===============================================================================
*/


/*
==================
Host_Startdemos_f
==================
*/
void Host_Startdemos_f (void)
{
	// this is to protect from startdemos triggering again on a game change
	static bool startdemos_firsttime = true;

	if (!startdemos_firsttime)
	{
		// stop the demo loop
		cls.demonum = -1;
		return;
	}

	startdemos_firsttime = false;

	int		i, c;

	c = Cmd_Argc () - 1;

	if (c > MAX_DEMOS)
	{
		Con_Printf ("Max %i demos in demoloop\n", MAX_DEMOS);
		c = MAX_DEMOS;
	}

	Con_SafePrintf ("%i demo(s) in loop\n", c);

	for (i = 1; i < c + 1; i++)
		Q_strncpy (cls.demos[i - 1], Cmd_Argv (i), sizeof (cls.demos[0]) - 1);

	if (!sv.active && cls.demonum != -1 && !cls.demoplayback)
	{
		cls.demonum = 0;
		CL_NextDemo ();
	}
	else cls.demonum = -1;
}


/*
==================
Host_Demos_f

Return to looping demos
==================
*/
void Host_Demos_f (void)
{
	if (cls.demonum == -1)
		cls.demonum = 1;

	CL_Disconnect_f ();
	CL_NextDemo ();
}

/*
==================
Host_Stopdemo_f

Return to looping demos
==================
*/
void Host_Stopdemo_f (void)
{
	if (!cls.demoplayback)
		return;

	CL_StopPlayback ();
	CL_Disconnect ();
}

//=============================================================================

// used to translate to non-fun characters for identify <name>
char unfun[129] =
	"................[]olzeasbt89...."
	"........[]......olzeasbt89..[.]."
	"aabcdefghijklmnopqrstuvwxyz[.].."
	".abcdefghijklmnopqrstuvwxyz[.]..";

// try to find s1 inside of s2
int unfun_match (char *s1, char *s2)
{
	int i;

	for (; *s2 ; s2++)
	{
		for (i = 0 ; s1[i] ; i++)
		{
			if (unfun[s1[i] & 127] != unfun[s2[i] & 127])
				break;
		}

		if (!s1[i])
			return true;
	}

	return false;
}

/* JPG 1.05
==================
Host_Identify_f

Print all known names for the specified player's ip address
==================
*/
void Host_Identify_f (void)
{
	int i;
	int a, b, c;
	char name[16];

	if (!iplog_size)
	{
		Con_Printf ("IP logging not available\nRemove -noiplog command line option\n"); // Baker 3.83: Now -iplog is the default
		return;
	}

	if (Cmd_Argc() < 2)
	{
		Con_Printf ("usage: identify <player number or name>\n");
		return;
	}

	if (sscanf (Cmd_Argv (1), "%d.%d.%d", &a, &b, &c) == 3)
	{
		Con_Printf ("known aliases for %d.%d.%d:\n", a, b, c);
		IPLog_Identify ((a << 16) | (b << 8) | c);
		return;
	}

	i = atoi (Cmd_Argv (1)) - 1;

	if (i == -1)
	{
		if (sv.active)
		{
			for (i = 0 ; i < svs.maxclients ; i++)
			{
				if (svs.clients[i].active && unfun_match (Cmd_Argv (1), svs.clients[i].name))
					break;
			}
		}
		else
		{
			for (i = 0 ; i < cl.maxclients ; i++)
			{
				if (unfun_match (Cmd_Argv (1), cl.scores[i].name))
					break;
			}
		}
	}

	if (sv.active)
	{
		if (i < 0 || i >= svs.maxclients || !svs.clients[i].active)
		{
			Con_Printf ("No such player\n");
			return;
		}

		if (sscanf (svs.clients[i].netconnection->address, "%d.%d.%d", &a, &b, &c) != 3)
		{
			Con_Printf ("Could not determine IP information for %s\n", svs.clients[i].name);
			return;
		}

		strncpy (name, svs.clients[i].name, 15);
		name[15] = 0;
		Con_Printf ("known aliases for %s:\n", name);
		IPLog_Identify ((a << 16) | (b << 8) | c);
	}
	else
	{
		if (i < 0 || i >= cl.maxclients || !cl.scores[i].name[0])
		{
			Con_Printf ("No such player\n");
			return;
		}

		if (!cl.scores[i].addr)
		{
			Con_Printf ("No IP information for %.15s\nUse 'status'\n", cl.scores[i].name);
			return;
		}

		strncpy (name, cl.scores[i].name, 15);
		name[15] = 0;
		Con_Printf ("known aliases for %s:\n", name);
		IPLog_Identify (cl.scores[i].addr);
	}
}


/*
==================
Host_InitCommands
==================
*/
cmd_t Host_Status_f_Cmd ("status", Host_Status_f);
cmd_t Host_QuitWithPrompt_f_Cmd ("quit_with_prompt", Host_QuitWithPrompt_f);
cmd_t Host_Quit_f_Cmd ("quit", Host_Quit_f);
cmd_t Host_God_f_Cmd ("god", Host_God_f);
cmd_t Host_Notarget_f_Cmd ("notarget", Host_Notarget_f);
cmd_t Host_Fly_f_Cmd ("fly", Host_Fly_f);
cmd_t Host_Map_f_Cmd ("map", Host_Map_f);
cmd_t Host_Restart_f_Cmd ("restart", Host_Restart_f);
cmd_t Host_Changelevel_f_Cmd ("changelevel", Host_Changelevel_f);
//cmd_t Host_Changelevel2_f_Cmd ("changelevel2", Host_Changelevel2_f);
cmd_t Host_Connect_f_Cmd ("connect", Host_Connect_f);
cmd_t Host_Reconnect_f_Cmd ("reconnect", Host_Reconnect_f);
cmd_t Host_Name_f_Cmd ("name", Host_Name_f);
cmd_t Host_Noclip_f_Cmd ("noclip", Host_Noclip_f);
cmd_t Host_Version_f_Cmd ("version", Host_Version_f);
//cmd_t Host_Please_f_Cmd ("please", Host_Please_f);
cmd_t Host_Say_f_Cmd ("say", Host_Say_f);
cmd_t Host_Say_Team_f_Cmd ("say_team", Host_Say_Team_f);
cmd_t Host_Tell_f_Cmd ("tell", Host_Tell_f);
cmd_t Host_Color_f_Cmd ("color", Host_Color_f);
cmd_t Host_Colour_f_Cmd ("colour", Host_Color_f);
cmd_t Host_Kill_f_Cmd ("kill", Host_Kill_f);
cmd_t Host_Pause_f_Cmd ("pause", Host_Pause_f);
cmd_t Host_Spawn_f_Cmd ("spawn", Host_Spawn_f);
cmd_t Host_Begin_f_Cmd ("begin", Host_Begin_f);
cmd_t Host_PreSpawn_f_Cmd ("prespawn", Host_PreSpawn_f);
cmd_t Host_Kick_f_Cmd ("kick", Host_Kick_f);
cmd_t Host_Ping_f_Cmd ("ping", Host_Ping_f);
cmd_t Host_Loadgame_f_Cmd ("load", Host_Loadgame_f);
cmd_t Host_Savegame_f_Cmd ("save", Host_Savegame_f);
cmd_t Host_Give_f_Cmd ("give", Host_Give_f);
cmd_t Host_Startdemos_f_Cmd ("startdemos", Host_Startdemos_f);
cmd_t Host_Demos_f_Cmd ("demos", Host_Demos_f);
cmd_t Host_Stopdemo_f_Cmd ("stopdemo", Host_Stopdemo_f);
cmd_t Host_Viewmodel_f_Cmd ("viewmodel", Host_Viewmodel_f);
cmd_t Host_Viewframe_f_Cmd ("viewframe", Host_Viewframe_f);
cmd_t Host_Viewnext_f_Cmd ("viewnext", Host_Viewnext_f);
cmd_t Host_Viewprev_f_Cmd ("viewprev", Host_Viewprev_f);
cmd_t Mod_Print_Cmd ("mcache", Mod_Print);

cmd_t Host_Identify_Cmd ("identify", Host_Identify_f);	// JPG 1.05 - player IP logging
cmd_t IPLog_Dump_Cmd ("ipdump", IPLog_Dump_f);			// JPG 1.05 - player IP logging
cmd_t IPLog_Import_Cmd ("ipmerge", IPLog_Import_f);		// JPG 3.00 - import an IP data file


void Host_InitCommands (void)
{
}
