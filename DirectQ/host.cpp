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
// host.c -- coordinates spawning and killing of local servers

#include "quakedef.h"
#include "winquake.h"
#include "d3d_quake.h"

/*

A server can allways be started, even if the system started out as a client
to a remote system.

Memory is cleared / released when a server or client begins, not when they end.

*/

quakeparms_t host_parms;

bool	host_initialized;		// true if into command execution

float		host_frametime;
float		host_time;
float		realtime;				// without any filtering or bounding
float		oldrealtime;			// last frame run
int			host_framecount;

client_t	*host_client;			// current client

jmp_buf 	host_abortserver;

byte		*host_basepal;
byte		*host_colormap;

cvar_t	host_savedir ("host_savedir", "save", CVAR_ARCHIVE);

cvar_t	host_framerate ("host_framerate","0");	// set for slow motion
cvar_t	host_speeds ("host_speeds","0");			// set for running times
cvar_t	sv_speed ("sv_speed", "1", CVAR_SERVER);

cvar_t	sys_ticrate ("sys_ticrate","0.05");
cvar_t	serverprofile ("serverprofile","0");

cvar_t	fraglimit ("fraglimit","0",CVAR_SERVER);
cvar_t	timelimit ("timelimit","0",CVAR_SERVER);
cvar_t	teamplay ("teamplay","0",CVAR_SERVER);

cvar_t	samelevel ("samelevel","0");
cvar_t	noexit ("noexit","0",CVAR_SERVER);

cvar_t	developer ("developer","0");

cvar_t	skill ("skill","1");						// 0 - 3
cvar_t	deathmatch ("deathmatch","0");			// 0, 1, or 2
cvar_t	coop ("coop","0");			// 0 or 1

cvar_t	pausable ("pausable","1");

cvar_t	temp1 ("temp1", "0");
cvar_t	temp2 ("temp2", "0");
cvar_t	temp3 ("temp3", "0");
cvar_t	temp4 ("temp4", "0");


/*
================
Host_EndGame
================
*/
void Host_EndGame (char *message, ...)
{
	va_list		argptr;
	char		string[1024];
	
	va_start (argptr,message);
	_vsnprintf (string,1024,message,argptr);
	va_end (argptr);
	Con_DPrintf ("Host_EndGame: %s\n",string);
	
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

	va_start (argptr,error);
	_vsnprintf (string,1024,error,argptr);
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
		else svs.maxclients = 8;
	}

	// don't let silly values go in
	if (svs.maxclients < 1)
		svs.maxclients = 8;
	else if (svs.maxclients > MAX_SCOREBOARD)
		svs.maxclients = MAX_SCOREBOARD;

	svs.maxclientslimit = svs.maxclients;

	if (svs.maxclientslimit < 4)
		svs.maxclientslimit = 4;

	// allocate space for the max clients
	svs.clients = (client_s *) Pool_Alloc (POOL_PERMANENT, svs.maxclientslimit * sizeof (client_t));

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
	
	host_time = 1.0;		// so a think at time 0 won't get called
}


/*
===============
Host_WriteConfiguration

Writes key bindings and archived cvars to config.cfg
===============
*/
extern cvar_t hud_defaulthud;
extern cvar_t hud_autosave;

void Host_WriteConfiguration (void)
{
	FILE	*f;

	if (host_initialized)
	{
		// force all open buffers to commit and close
		while (_fcloseall ());

		if (!(f = fopen (va ("%s/config.cfg", com_gamedir), "w")))
		{
			// if we failed to open it, we sleep for a bit, then attempt it again.  windows or something seems to
			// lock on to a file handle for a while even after it's been closed, so this might help giving it a
			// hint that it needs to unlock
			Sleep (1000);

			if (!(f = fopen (va ("%s/config.cfg", com_gamedir), "w")))
			{
				// if after this amount of time it's still locked open, something has gone askew
				Con_Printf ("Couldn't write config.cfg.\n");
				return;
			}
		}

		Key_WriteBindings (f);
		Cvar_WriteVariables (f);

		fclose (f);

		Con_Printf ("Wrote config.cfg\n");
	}

	if (hud_autosave.value)
	{
		// save out the user's HUD cfg as well
		Cbuf_InsertText (va ("savehud %s\n", hud_defaulthud.string));
		Cbuf_Execute ();
	}
}


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
	char		string[1024];
	
	va_start (argptr,fmt);
	_vsnprintf (string, 1024,fmt,argptr);
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

	va_start (argptr,fmt);
	_vsnprintf (string, 1024, fmt,argptr);
	va_end (argptr);

	for (i=0 ; i<svs.maxclients ; i++)
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

	va_start (argptr,fmt);
	_vsnprintf (string, 1024, fmt,argptr);
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
			saveSelf = pr_global_struct->self;
			pr_global_struct->self = EDICT_TO_PROG(host_client->edict);
			PR_ExecuteProgram (pr_global_struct->ClientDisconnect);
			pr_global_struct->self = saveSelf;
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
	for (i=0, client = svs.clients ; i<svs.maxclients ; i++, client++)
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
void Host_ShutdownServer(bool crash)
{
	int		i;
	int		count;
	sizebuf_t	buf;
	char		message[4];
	float	start;

	if (!sv.active)
		return;

	sv.active = false;

// stop all client sounds immediately
	if (cls.state == ca_connected)
		CL_Disconnect ();

// flush any pending messages - like the score!!!
	start = Sys_FloatTime();
	do
	{
		count = 0;
		for (i=0, host_client = svs.clients ; i<svs.maxclients ; i++, host_client++)
		{
			if (host_client->active && host_client->message.cursize)
			{
				if (NET_CanSendMessage (host_client->netconnection))
				{
					NET_SendMessage(host_client->netconnection, &host_client->message);
					SZ_Clear (&host_client->message);
				}
				else
				{
					NET_GetMessage(host_client->netconnection);
					count++;
				}
			}
		}
		if ((Sys_FloatTime() - start) > 3.0)
			break;
	}
	while (count);

// make sure all the clients know we're disconnecting
	buf.data = (byte *) message;
	buf.maxsize = 4;
	buf.cursize = 0;
	MSG_WriteByte(&buf, svc_disconnect);
	count = NET_SendToAll(&buf, 5);
	if (count)
		Con_Printf("Host_ShutdownServer: NET_SendToAll failed for %u clients\n", count);

	for (i=0, host_client = svs.clients ; i<svs.maxclients ; i++, host_client++)
		if (host_client->active)
			SV_DropClient(crash);

	// clear structures
	memset (&sv, 0, sizeof(sv));
	memset (svs.clients, 0, svs.maxclientslimit*sizeof(client_t));
	PR_ClearProgs ();
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

void D3D_EvictTextures (void);

void Host_ClearMemory (void)
{
	// clear anything that needs to be cleared specifically
	S_StopAllSounds (true);
	Mod_ClearAll ();
	D3D_EvictTextures ();
	S_ClearSounds ();

	cls.signon = 0;

	if (signal_cacheclear)
	{
		// clear the cache if signalled
		Pool_Free (POOL_CACHE);
		signal_cacheclear = false;
	}

	// clear virtual memory pools for the map and any scratch allocations
	Pool_Free (POOL_MAP);
	Pool_Free (POOL_TEMP);
	Pool_Free (POOL_LOADFILE);

	// wipe the client and server structs
	memset (&sv, 0, sizeof (sv));
	memset (&cl, 0, sizeof (cl));

	// clear out the progs structs
	PR_ClearProgs ();

	// force all open buffers to commit and close
	// can't do this because of demos!!!
	// while (_fcloseall ());
}


//============================================================================


/*
===================
Host_FilterTime

Returns false if the time is too short to run a frame
===================
*/
// changed to host_maxfps for compatibility reasons.
// really need to find a way to have 2 names refer to the same variable here...
cvar_t host_maxfps ("host_maxfps", 72, CVAR_ARCHIVE | CVAR_SERVER);

bool Host_FilterTime (float time)
{
	realtime += time;

	// bound sensibly
	if (host_maxfps.value < 30) Cvar_Set (&host_maxfps, 30);
	if (host_maxfps.value > 666) Cvar_Set (&host_maxfps, 666);

	if (!cls.timedemo && (realtime - oldrealtime < (1.0f / host_maxfps.value)))
		return false;		// framerate is too high

	host_frametime = realtime - oldrealtime;
	oldrealtime = realtime;

	if (host_framerate.value > 0)
		host_frametime = host_framerate.value;
	else
	{
		// don't allow really long or short frames
		if (host_frametime > 0.1) host_frametime = 0.1;
		if (host_frametime < 0.001) host_frametime = 0.001;
	}
	
	return true;
}


/*
==================
Host_ServerFrame

==================
*/
void Host_ServerFrame (void)
{
	// run the world state	
	pr_global_struct->frametime = host_frametime;

	// set the time and clear the general datagram
	SV_ClearDatagram ();

	// check for new clients
	SV_CheckForNewClients ();

	// read client messages
	SV_RunClients ();

	// move things around and think
	// always pause in single player if in console or menus
	if (!sv.paused && (svs.maxclients > 1 || key_dest == key_game)) SV_Physics ();

	// send all messages to the clients
	SV_SendClientMessages ();
}


/*
==================
Host_Frame

Runs all active servers
==================
*/
float host_refreshrate = 0;
cvar_t r_filterrefresh ("r_filterrefresh", 0.0f, CVAR_ARCHIVE);

void Host_SetRefreshRate (int rate)
{
	// 5 is an arbitrary upper limit here
	if (r_filterrefresh.value < 0) Cvar_Set (&r_filterrefresh, 0.0f);
	if (r_filterrefresh.value > 5) Cvar_Set (&r_filterrefresh, 5.0f);

	// sanity check
	if (rate < 1) rate = 666;

	if (!r_filterrefresh.value)
		host_refreshrate = 0;
	else host_refreshrate = (1.0 / (float) (rate * r_filterrefresh.value));
}


void _Host_Frame (float time)
{
	static float time1 = 0;
	static float time2 = 0;
	static float time3 = 0;
	int pass1, pass2, pass3;
	static float host_refreshtime = 666;

	// something bad happened, or the server disconnected
	if (setjmp (host_abortserver))
		return;

	// keep the random time dependent
	rand ();

	// decide the simulation time - don't run too fast, or packets will flood out
	if (!Host_FilterTime (time)) return;

	// get new key events
	Sys_SendKeyEvents ();

	// allow mice or other external controllers to add commands
	IN_Commands ();

	// process console commands
	Cbuf_Execute ();

	NET_Poll ();

	// if running the server locally, make intentions now
	if (sv.active) CL_SendCmd ();

	// run the server
	if (sv.active) Host_ServerFrame ();

	// if running the server remotely, send intentions now after
	// the incoming messages have been read
	if (!sv.active) CL_SendCmd ();

	host_time += host_frametime;
	host_refreshtime += host_frametime;

	// fetch results from server
	if (cls.state == ca_connected) CL_ReadFromServer ();

	// update video
	if (host_speeds.value) time1 = Sys_FloatTime ();

	// never refresh at > the r_filterrefresh.value x screen's refresh rate, no matter how fast everything else is running.
	// this prevents gfx cards with limited resources from bottlenecking when everything else is
	// running too fast (to do: is this the primary cause of lockups with OpenGL?)
	if (host_refreshtime >= host_refreshrate)
	{
		SCR_UpdateScreen ();
		host_refreshtime = 0;
	}

	if (host_speeds.value) time2 = Sys_FloatTime ();

	// update audio
	if (cls.signon == SIGNONS)
	{
		S_Update (r_origin, vpn, vright, vup);
		CL_DecayLights ();
	}
	else S_Update (vec3_origin, vec3_origin, vec3_origin, vec3_origin);

	CDAudio_Update ();

	if (host_speeds.value)
	{
		pass1 = (time1 - time3) * 1000;
		time3 = Sys_FloatTime ();
		pass2 = (time2 - time1) * 1000;
		pass3 = (time3 - time2) * 1000;

		Con_Printf
		(
			"Total: %3i  Server: %3i  Refresh: %3i  Sound: %3i\n",
			pass1 + pass2 + pass3,
			pass1,
			pass2,
			pass3
		);
	}

	host_framecount++;
}


void Host_Frame (float time)
{
	float	time1, time2;
	static float	timetotal;
	static int		timecount;
	int		i, c, m;

	// don't go too slow
	if (sv_speed.value < 0.1) Cvar_Set (&sv_speed, 0.1f);

	// allow to adjust the speed of the server for slowmo/fast forward effects and stuff
	time *= sv_speed.value;

	if (!serverprofile.value)
	{
		_Host_Frame (time);
		return;
	}
	
	time1 = Sys_FloatTime ();
	_Host_Frame (time);
	time2 = Sys_FloatTime ();	
	
	timetotal += time2 - time1;
	timecount++;
	
	if (timecount < 1000)
		return;

	m = timetotal*1000/timecount;
	timecount = 0;
	timetotal = 0;
	c = 0;
	for (i=0 ; i<svs.maxclients ; i++)
	{
		if (svs.clients[i].active)
			c++;
	}

	Con_Printf ("serverprofile: %2i clients %2i msec\n",  c,  m);
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
void D3D_VidInit (byte *palette);
bool full_initialized = false;
extern cvar_t hud_defaulthud;
void Menu_CommonInit (void);
void PR_InitBuiltIns (void);
void Menu_MapsPopulate (void);
void Menu_DemoPopulate (void);
void Menu_LoadAvailableSkyboxes (void);

void Host_Init (quakeparms_t *parms)
{
	// hold back some things until we're fully up
	full_initialized = false;

	memcpy (&host_parms, parms, sizeof (quakeparms_t));
	// host_parms = *parms;

	com_argc = parms->argc;
	com_argv = parms->argv;

	PR_InitBuiltIns ();
	Cbuf_Init ();
	Cmd_Init ();	
	V_Init ();
	Chase_Init ();
	COM_Init (parms->basedir);

	// as soon as the filesystem comes up we want to load the configs so that cvars will have correct
	// values before we proceed with anything else.  this is possible to do as we've made our cvars
	// self-registering, so we don't need to worry about subsequent registrations or cvars that don't exist.
	Cbuf_InsertText ("exec quake.rc\n");

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

	Con_Printf ("Exe: "__TIME__" "__DATE__"\n");

	R_InitTextures ();
 
	host_basepal = (byte *) COM_LoadHunkFile ("gfx/palette.lmp");
	if (!host_basepal) Sys_Error ("Couldn't load gfx/palette.lmp");
	host_colormap = (byte *) COM_LoadHunkFile ("gfx/colormap.lmp");
	if (!host_colormap) Sys_Error ("Couldn't load gfx/colormap.lmp");

	D3D_VidInit (host_basepal);

	Draw_Init ();
	SCR_Init ();
	R_Init ();
	// FIXME: doesn't use the new one-window approach yet
	S_Init ();
	DS_Init ();
	CDAudio_Init ();
	HUD_Init ();
	CL_Init ();
	IN_Init ();

	// everythings up now
	full_initialized = true;

	// load the configs fully
	Cbuf_InsertText ("exec quake.rc\n");

	// anything allocated after this point will be cleared between maps
	host_initialized = true;
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

	CDAudio_Shutdown ();
	DS_Shutdown ();
	NET_Shutdown ();
	S_Shutdown();
	IN_Shutdown ();
	VID_Shutdown();
}


void Host_Shutdown (void)
{
	static bool isdown = false;

	if (isdown) return;

	isdown = true;

	Host_ShutdownGame ();
}

