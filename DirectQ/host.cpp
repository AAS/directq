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
// host.c -- coordinates spawning and killing of local servers

#include "quakedef.h"
#include "d3d_model.h"
#include "winquake.h"
#include "d3d_quake.h"
#include "pr_class.h"

/*

A server can allways be started, even if the system started out as a client
to a remote system.

Memory is cleared / released when a server or client begins, not when they end.

*/

quakeparms_t host_parms;

bool	host_initialized;		// true if into command execution

float		realtime;				// without any filtering or bounding

int			host_framecount;

client_t	*host_client;			// current client

jmp_buf 	host_abortserver;

cvar_t	host_framerate ("host_framerate", "0");	// set for slow motion
cvar_t	sys_ticrate ("sys_ticrate", "0.05");

cvar_t	fraglimit ("fraglimit", "0", CVAR_SERVER);
cvar_t	timelimit ("timelimit", "0", CVAR_SERVER);
cvar_t	teamplay ("teamplay", "0", CVAR_SERVER);

cvar_t	samelevel ("samelevel", "0");
cvar_t	noexit ("noexit", "0", CVAR_SERVER);

cvar_t	developer ("developer", "0");

cvar_t	skill ("skill", "1");						// 0 - 3
cvar_t	deathmatch ("deathmatch", "0");			// 0, 1, or 2
cvar_t	coop ("coop", "0");			// 0 or 1

cvar_t	pausable ("pausable", "1");

cvar_t	temp1 ("temp1", "0");
cvar_t	temp2 ("temp2", "0");
cvar_t	temp3 ("temp3", "0");
cvar_t	temp4 ("temp4", "0");


// reserve space for all clients.  these need to be kept in static memory so that the addresses
// of member variables remain valid during level transitions/etc
client_t host_svsclients[MAX_SCOREBOARD];

void Host_SafeWipeClient (client_t *client)
{
	// copy out anything that uses a pointer in the client_t struct
	byte *msgbuf = client->msgbuf;
	float *ping_times = client->ping_times;
	float *spawn_parms = client->spawn_parms;

	// wipe the contents of what we copied out
	if (msgbuf) memset (msgbuf, 0, MAX_MSGLEN);
	if (ping_times) memset (ping_times, 0, sizeof (float) * NUM_PING_TIMES);
	if (spawn_parms) memset (spawn_parms, 0, sizeof (float) * NUM_SPAWN_PARMS);

	// now we can safely wipe the struct
	memset (client, 0, sizeof (client_t));

	// and now we restore what we copied out
	client->msgbuf = msgbuf;
	client->ping_times = ping_times;
	client->spawn_parms = spawn_parms;
}


void Host_InitClients (int numclients)
{
	client_t *client = host_svsclients;

	for (int i = 0; i < MAX_SCOREBOARD; i++, client++)
	{
		// safely wipe the client
		Host_SafeWipeClient (client);

		if (i < numclients)
		{
			if (!client->ping_times)
				client->ping_times = (float *) MainZone->Alloc (sizeof (float) * NUM_PING_TIMES);
			else memset (client->ping_times, 0, sizeof (float) * NUM_PING_TIMES);

			if (!client->spawn_parms)
				client->spawn_parms = (float *) MainZone->Alloc (sizeof (float) * NUM_SPAWN_PARMS);
			else memset (client->spawn_parms, 0, sizeof (float) * NUM_SPAWN_PARMS);

			// this is going to be an active client so set up memory/etc for it
			if (!client->msgbuf)
				client->msgbuf = (byte *) MainZone->Alloc (MAX_MSGLEN);
			else memset (client->msgbuf, 0, MAX_MSGLEN);

			// set up the new message buffer correctly (this is just harmless paranoia as it's also done in SV_ConnectClient)
			client->message.data = client->msgbuf;
		}
		else
		{
			// this is an inactive client so release memory/etc
			if (client->msgbuf)
			{
				MainZone->Free (client->msgbuf);
				client->msgbuf = NULL;
			}

			if (client->ping_times)
			{
				MainZone->Free (client->ping_times);
				client->ping_times = NULL;
			}

			if (client->spawn_parms)
			{
				MainZone->Free (client->spawn_parms);
				client->spawn_parms = NULL;
			}
		}
	}
}


/*
================
Host_EndGame
================
*/
void Host_EndGame (char *message, ...)
{
	va_list		argptr;
	char		string[1024];

	va_start (argptr, message);
	_vsnprintf (string, 1024, message, argptr);
	va_end (argptr);
	Con_DPrintf ("Host_EndGame: %s\n", string);

	if (sv.active)
		Host_ShutdownServer (false);

	if (cls.demonum != -1)
		CL_NextDemo ();
	else
		CL_Disconnect ();

	longjmp (host_abortserver, 1);
}

/*
================
Host_Error

This shuts down both the client and server
================
*/
void Host_Error (char *error, ...)
{
	va_list		argptr;
	char		string[1024];
	static	bool inerror = false;

	if (inerror)
		Sys_Error ("Host_Error: recursively entered");

	inerror = true;

	SCR_EndLoadingPlaque ();		// reenable screen updates

	va_start (argptr, error);
	_vsnprintf (string, 1024, error, argptr);
	va_end (argptr);

	Con_Printf ("Host_Error: %s\n", string);
	QC_DebugOutput ("Host_Error: %s\n", string);

	if (sv.active)
		Host_ShutdownServer (false);

	CL_Disconnect ();
	cls.demonum = -1;

	inerror = false;

	longjmp (host_abortserver, 1);
}

/*
================
Host_FindMaxClients
================
*/
void Host_FindMaxClients (void)
{
	int		i;

	svs.maxclients = 1;

	// initially disconnected
	cls.state = ca_disconnected;

	i = COM_CheckParm ("-listen");

	// check for a listen server
	if (i)
	{
		if (i != (com_argc - 1))
			svs.maxclients = atoi (com_argv[i + 1]);
		else svs.maxclients = MAX_SCOREBOARD;
	}

	// don't let silly values go in
	if (svs.maxclients < 1)
		svs.maxclients = 1;
	else if (svs.maxclients > MAX_SCOREBOARD)
		svs.maxclients = MAX_SCOREBOARD;

	// allocate space for the initial clients
	svs.clients = host_svsclients;
	Host_InitClients (svs.maxclients);

	// if we request more than 1 client we set the appropriate game mode
	if (svs.maxclients > 1)
		Cvar_Set ("deathmatch", 1.0);
	else Cvar_Set ("deathmatch", 0.0);
}


/*
=======================
Host_InitLocal
======================
*/
void Host_InitLocal (void)
{
	Host_InitCommands ();

	Host_FindMaxClients ();
}


/*
===============
Host_WriteConfiguration

Writes key bindings and archived cvars
===============
*/
void Cmd_WriteAlias (FILE *f);

void Host_WriteConfiguration (void)
{
	FILE	*f;

	if (host_initialized)
	{
		// force all open buffers to commit and close
		while (_fcloseall ());

#if 1

		if (!(f = fopen (va ("%s/directq.cfg", com_gamedir), "w")))
#else
		if (!(f = fopen (va ("%s/config.cfg", com_gamedir), "w")))
#endif
		{
			// if we failed to open it, we sleep for a bit, then attempt it again.  windows or something seems to
			// lock on to a file handle for a while even after it's been closed, so this might help giving it a
			// hint that it needs to unlock
			Sleep (1000);

#if 1

			if (!(f = fopen (va ("%s/directq.cfg", com_gamedir), "w")))
#else
			if (!(f = fopen (va ("%s/config.cfg", com_gamedir), "w")))
#endif
			{
				// if after this amount of time it's still locked open, something has gone askew
#if 1
				Con_SafePrintf ("Couldn't write directq.cfg.\n");
#else
				Con_SafePrintf ("Couldn't write config.cfg.\n");
#endif
				return;
			}
		}

		Key_WriteBindings (f);
		Cvar_WriteVariables (f);
		Cmd_WriteAlias (f);	// needed for the RMQ hook otherwise it gets wiped

		fclose (f);

#if 1
		Con_SafePrintf ("Wrote directq.cfg\n");
#else
		Con_SafePrintf ("Wrote config.cfg\n");
#endif
	}
}


cmd_t Host_WriteConfiguration_Cmd ("Host_WriteConfiguration", Host_WriteConfiguration);

/*
=================
SV_ClientPrintf

Sends text across to be displayed
FIXME: make this just a stuffed echo?
=================
*/
void SV_ClientPrintf (char *fmt, ...)
{
	va_list		argptr;
	char		string[2048];

	va_start (argptr, fmt);
	_vsnprintf (string, 2048, fmt, argptr);
	va_end (argptr);

	MSG_WriteByte (&host_client->message, svc_print);
	MSG_WriteString (&host_client->message, string);
}

/*
=================
SV_BroadcastPrintf

Sends text to all active clients
=================
*/
void SV_BroadcastPrintf (char *fmt, ...)
{
	va_list		argptr;
	char		string[1024];
	int			i;

	va_start (argptr, fmt);
	_vsnprintf (string, 1024, fmt, argptr);
	va_end (argptr);

	for (i = 0; i < svs.maxclients; i++)
		if (svs.clients[i].active && svs.clients[i].spawned)
		{
			MSG_WriteByte (&svs.clients[i].message, svc_print);
			MSG_WriteString (&svs.clients[i].message, string);
		}
}

/*
=================
Host_ClientCommands

Send text over to the client to be executed
=================
*/
void Host_ClientCommands (char *fmt, ...)
{
	va_list		argptr;
	char		string[1024];

	va_start (argptr, fmt);
	_vsnprintf (string, 1020, fmt, argptr);
	va_end (argptr);

	MSG_WriteByte (&host_client->message, svc_stufftext);
	MSG_WriteString (&host_client->message, string);
}


/*
=====================
SV_DropClient

Called when the player is getting totally kicked off the host
if (crash = true), don't bother sending signofs
=====================
*/
void SV_DropClient (bool crash)
{
	int		saveSelf;
	int		i;
	client_t *client;

	if (!crash)
	{
		// send any final messages (don't check for errors)
		if (NET_CanSendMessage (host_client->netconnection))
		{
			MSG_WriteByte (&host_client->message, svc_disconnect);
			NET_SendMessage (host_client->netconnection, &host_client->message);
		}

		if (host_client->edict && host_client->spawned)
		{
			// call the prog function for removing a client
			// this will set the body to a dead frame, among other things
			saveSelf = SVProgs->GlobalStruct->self;
			SVProgs->GlobalStruct->self = EDICT_TO_PROG (host_client->edict);
			SVProgs->ExecuteProgram (SVProgs->GlobalStruct->ClientDisconnect);
			SVProgs->GlobalStruct->self = saveSelf;
		}
	}

	// break the net connection
	NET_Close (host_client->netconnection);
	host_client->netconnection = NULL;

	// free the client (the body stays around)
	host_client->active = false;
	host_client->name[0] = 0;
	host_client->old_frags = -999999;
	net_activeconnections--;

	// send notification to all clients
	for (i = 0, client = svs.clients; i < svs.maxclients; i++, client++)
	{
		if (!client->active)
			continue;

		MSG_WriteByte (&client->message, svc_updatename);
		MSG_WriteByte (&client->message, host_client - svs.clients);
		MSG_WriteString (&client->message, "");
		MSG_WriteByte (&client->message, svc_updatefrags);
		MSG_WriteByte (&client->message, host_client - svs.clients);
		MSG_WriteShort (&client->message, 0);
		MSG_WriteByte (&client->message, svc_updatecolors);
		MSG_WriteByte (&client->message, host_client - svs.clients);
		MSG_WriteByte (&client->message, 0);
	}
}

/*
==================
Host_ShutdownServer

This only happens at the end of a game, not between levels
==================
*/
void Host_ShutdownServer (bool crash)
{
	int		i;
	int		count;
	sizebuf_t	buf;
	char		message[4];

	if (!sv.active)
		return;

	sv.active = false;

	// stop all client sounds immediately
	if (cls.state == ca_connected)
		CL_Disconnect ();

	// flush any pending messages - like the score!!!
	DWORD dwStart = Sys_Milliseconds ();

	do
	{
		count = 0;

		for (i = 0, host_client = svs.clients; i < svs.maxclients; i++, host_client++)
		{
			if (host_client->active && host_client->message.cursize)
			{
				if (NET_CanSendMessage (host_client->netconnection))
				{
					NET_SendMessage (host_client->netconnection, &host_client->message);
					SZ_Clear (&host_client->message);
				}
				else
				{
					NET_GetMessage (host_client->netconnection);
					count++;
				}
			}
		}

		if ((Sys_Milliseconds () - dwStart) > 3000) break;
	} while (count);

	// make sure all the clients know we're disconnecting
	buf.data = (byte *) message;
	buf.maxsize = 4;
	buf.cursize = 0;
	MSG_WriteByte (&buf, svc_disconnect);
	count = NET_SendToAll (&buf, 5);

	if (count)
		Con_Printf ("Host_ShutdownServer: NET_SendToAll failed for %u clients\n", count);

	for (i = 0, host_client = svs.clients; i < svs.maxclients; i++, host_client++)
		if (host_client->active)
			SV_DropClient (crash);

	// safely wipe the client_t structs
	for (i = 0; i < svs.maxclients; i++)
		Host_SafeWipeClient (&svs.clients[i]);

	// clear structures
	memset (&sv, 0, sizeof (sv));
}


/*
================
Host_ClearMemory

This clears all the memory used by both the client and server, but does
not reinitialize anything.
================
*/
bool signal_cacheclear = false;

void Cmd_SignalCacheClear_f (void)
{
	if (!signal_cacheclear)
	{
		signal_cacheclear = true;
		Con_Printf ("Cache memory will be cleared on the next map load\n");
	}
	else
	{
		signal_cacheclear = false;
		Con_Printf ("Cache memory will not be cleared\n");
	}
}

cmd_t Cmd_SignalCacheClear ("cacheclear", Cmd_SignalCacheClear_f);
void CL_ClearCLStruct (void);

void Host_ClearMemory (void)
{
	// clear anything that needs to be cleared specifically
	S_StopAllSounds (true);
	Mod_ClearAll ();
	S_ClearSounds ();

	cls.signon = 0;

	if (signal_cacheclear)
	{
		// clear the cache if signalled
		MainCache->Flush ();
		signal_cacheclear = false;
	}

	// clear virtual memory pools for the map
	MainHunk->Free ();

	// this is not used in the current code but we'll keep it around in case we ever need it
	SAFE_DELETE (MapZone);
	MapZone = new CQuakeZone ();

	// wipe the client and server structs
	memset (&sv, 0, sizeof (sv));
	CL_ClearCLStruct ();
}


//============================================================================


/*
===================
Host_FilterTime

Returns false if the time is too short to run a frame
===================
*/
cvar_t host_maxfps ("host_maxfps", 72, CVAR_ARCHIVE | CVAR_SERVER);

cvar_alias_t cl_maxfps ("cl_maxfps", &host_maxfps);
cvar_alias_t sv_maxfps ("sv_maxfps", &host_maxfps);
cvar_alias_t pq_maxfps ("pq_maxfps", &host_maxfps);

/*
==================
Host_ServerFrame

==================
*/
void Host_ServerFrame (float frametime)
{
	// run the world state
	SVProgs->GlobalStruct->frametime = frametime;

	// set the time and clear the general datagram
	SV_ClearDatagram ();

	// check for new clients
	SV_CheckForNewClients ();

	// read client messages
	SV_RunClients (frametime);

	// move things around and think
	// always pause in single player if in console or menus
	if (!sv.paused && (svs.maxclients > 1 || key_dest == key_game))
		SV_Physics (frametime);

	// send all messages to the clients
	SV_SendClientMessages ();
}


/*
==================
Host_Frame

Runs all active servers
==================
*/

void CL_SendLagMove (void);
void IN_ReadKeyboardEvents (void);

void SCR_SetTimeout (float timeout);
void R_UpdateParticles (void);
void SCR_SetUpToDrawConsole (float frametime);

void Host_Frame (DWORD time)
{
	// something bad happened, or the server disconnected
	if (setjmp (host_abortserver)) return;

	// read the keyboard
	// IN_ReadKeyboardEvents ();

	// keep the random time dependent
	rand ();

	static DWORD dwOldRealTime = 0;
	static DWORD dwRealTime = 0;				// millisecond version to keep timings steady

	// keep timings steady
	dwRealTime += time;

	// prevent division by zero (and stalling the host)
	if (host_maxfps.value < 1) Cvar_Set (&host_maxfps, 1);

	// attempt to compensate for integer division by rounding to the nearest
	DWORD dwLockTime = (DWORD) ((1000.0f / host_maxfps.value) + 0.5f);

	if (!cls.timedemo && (dwRealTime - dwOldRealTime) < dwLockTime)
	{
		// JPG - if we're not doing a frame, still check for lagged moves to send
		if (!sv.active && !cls.demoplayback && (cl.movemessages > 2)) CL_SendLagMove ();
	}
	else
	{
		// move these back to DWORDs to keep timings steady; there is a tendency for them to drift at present
		float host_frametime = (float) (dwRealTime - dwOldRealTime) * 0.001f;

		realtime = (float) dwRealTime * 0.001f;
		dwOldRealTime = dwRealTime;

		// more integer compensation here
		// weird things happen if we're running at > 1000 fps and we clamp frametimes to 1 ms so don't do that!
		if (host_framerate.value > 0)
			host_frametime = host_framerate.value;
		else if (host_frametime > 0.1f) host_frametime = 0.1f;

		// run the message loop
		Sys_SendKeyEvents ();

		// process console commands
		Cbuf_Execute ();

		NET_Poll ();

		if (sv.active) CL_SendCmd ();

		// if running the server locally, make intentions now
		if (sv.active)
			Host_ServerFrame (host_frametime);
		else CL_SendCmd ();

		// fetch results from server
		if (cls.state == ca_connected) CL_ReadFromServer (host_frametime);

		// set up the console (FPS dependent)
		SCR_SetUpToDrawConsole (host_frametime);

		// update display - V_RenderView is now called separately so that SCR_UpdateScreen can be independent of it
		V_RenderView (cl.time, host_frametime);

		SCR_UpdateScreen (host_frametime);

		// run anything that needs to be done after the screen update
		// update particles
		R_UpdateParticles ();

		// update dlights
		if (cls.signon == SIGNONS) CL_DecayLights ();

		// update sound
		if (cls.signon == SIGNONS)
			S_Update (host_frametime, r_origin, vpn, vright, vup);
		else S_Update (host_frametime, vec3_origin, vec3_origin, vec3_origin, vec3_origin);

		// finish sound
		CDAudio_Update ();
		MediaPlayer_Update ();

		host_framecount++;
	}
}


//============================================================================


void Host_InitVCR (quakeparms_t *parms)
{
}

/*
====================
Host_Init
====================
*/
void D3DVid_Init (void);
bool full_initialized = false;
void Menu_CommonInit (void);
void PR_InitBuiltIns (void);
void Menu_MapsPopulate (void);
void Menu_DemoPopulate (void);
void Menu_LoadAvailableSkyboxes (void);
void SCR_QuakeIsLoading (int stage, int maxstage);
void Cvar_MakeCompatLayer (void);
void Cmd_MakeCompatLayer (void);


void Host_Init (quakeparms_t *parms)
{
	// hold back some things until we're fully up
	full_initialized = false;

	memcpy (&host_parms, parms, sizeof (quakeparms_t));
	// host_parms = *parms;

	com_argc = parms->argc;
	com_argv = parms->argv;

	// build the compatibility layers
	Cvar_MakeCompatLayer ();
	Cmd_MakeCompatLayer ();

	//PR_InitBuiltIns ();
	Cbuf_Init ();
	Cmd_Init ();
	V_Init ();
	Chase_Init ();
	COM_Init (parms->basedir);

	// as soon as the filesystem comes up we want to load the configs so that cvars will have correct
	// values before we proceed with anything else.  this is possible to do as we've made our cvars
	// self-registering, so we don't need to worry about subsequent registrations or cvars that don't exist.
	COM_ExecQuakeRC ();

	// execute immediately rather than deferring
	Cbuf_Execute ();

	Host_InitLocal ();

	if (!W_LoadWadFile ("gfx.wad")) Sys_Error ("Could not locate Quake on your computer");

	Key_Init ();
	Con_Init ();
	Menu_CommonInit ();
	Menu_MapsPopulate ();
	Menu_DemoPopulate ();
	Menu_LoadAvailableSkyboxes ();
	PR_Init ();
	Mod_Init ();
	NET_Init ();
	SV_Init ();
	IPLog_Init ();	// JPG 1.05 - ip address logging

	Con_SafePrintf ("Exe: "__TIME__" "__DATE__"\n");

	R_InitTextures ();

	if (!W_LoadPalette ()) Sys_Error ("Could not locate Quake on your computer");

	D3DVid_Init ();

	Draw_Init (); SCR_QuakeIsLoading (1, 9);
	SCR_Init (); SCR_QuakeIsLoading (2, 9);
	R_Init (); SCR_QuakeIsLoading (3, 9);
	S_Init (); SCR_QuakeIsLoading (4, 9);
	MediaPlayer_Init (); SCR_QuakeIsLoading (5, 9);
	CDAudio_Init (); SCR_QuakeIsLoading (6, 9);
	HUD_Init (); SCR_QuakeIsLoading (7, 9);
	CL_Init (); SCR_QuakeIsLoading (8, 9);
	IN_Init (); SCR_QuakeIsLoading (9, 9);

	// everythings up now
	full_initialized = true;

	COM_ExecQuakeRC ();

	// anything allocated after this point will be cleared between maps
	host_initialized = true;

	UpdateTitlebarText ();
}


/*
===============
Host_Shutdown

FIXME: this is a callback from Sys_Quit and Sys_Error.  It would be better
to run quit through here before the final handoff to the sys code.
===============
*/
void Host_ShutdownGame (void)
{
	// keep Con_Printf from trying to update the screen
	scr_disabled_for_loading = true;

	Host_WriteConfiguration ();
	IPLog_WriteLog ();	// JPG 1.05 - ip loggging

	CDAudio_Shutdown ();
	MediaPlayer_Shutdown ();
	NET_Shutdown ();
	S_Shutdown();
	IN_Shutdown ();
	VID_Shutdown ();
}


void Host_Shutdown (void)
{
	static bool isdown = false;

	if (isdown) return;

	isdown = true;

	Host_ShutdownGame ();
}

