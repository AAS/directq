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
// net_main.c

#include "quakedef.h"

// joe: rcon from ProQuake
#define RCON_BUFF_SIZE	8192
char		rcon_buff[RCON_BUFF_SIZE];
sizebuf_t	rcon_message = {false, false, (byte *) rcon_buff, RCON_BUFF_SIZE, 0};
bool	rcon_active = false;

cvar_t	cl_password ("cl_password", "");		// joe: password protection from ProQuake
cvar_t	rcon_password ("rcon_password", "");		// joe: rcon password from ProQuake
cvar_t	rcon_server ("rcon_server", "");		// joe: rcon server from ProQuake

qsocket_t	*net_activeSockets = NULL;
qsocket_t	*net_freeSockets = NULL;
int			net_numsockets = 0;

bool	tcpipAvailable = false;

int			net_hostport;
int			DEFAULTnet_hostport = 26000;

char		my_tcpip_address[NET_NAMELEN];

static bool	listening = false;

bool	slistInProgress = false;
bool	slistSilent = false;
bool	slistLocal = true;
static float	slistStartTime;
static int		slistLastShown;

static void Slist_Send(void *soak);
static void Slist_Poll(void *soak);
PollProcedure	slistSendProcedure = {NULL, 0.0, Slist_Send};
PollProcedure	slistPollProcedure = {NULL, 0.0, Slist_Poll};


sizebuf_t		net_message;
int				net_activeconnections = 0;

int messagesSent = 0;
int messagesReceived = 0;
int unreliableMessagesSent = 0;
int unreliableMessagesReceived = 0;

cvar_t	net_messagetimeout ("net_messagetimeout","300");
cvar_t	hostname ("hostname", "UNNAMED");

bool	configRestored = false;

// these two macros are to make the code more readable
#define sfunc	net_drivers[sock->driver]
#define net_DriverFunc	net_drivers[net_driverlevel]

int	net_driverlevel;


float			net_time;

float SetNetTime(void)
{
	net_time = Sys_FloatTime();
	return net_time;
}


/*
===================
NET_NewQSocket

Called by drivers when a new communications endpoint is required
The sequence and buffer fields will be filled in properly
===================
*/
qsocket_t *NET_NewQSocket (void)
{
	qsocket_t	*sock;

	if (net_freeSockets == NULL)
		return NULL;

	if (net_activeconnections >= svs.maxclients)
		return NULL;

	// get one from free list
	sock = net_freeSockets;
	net_freeSockets = sock->next;

	// add it to active list
	sock->next = net_activeSockets;
	net_activeSockets = sock;

	sock->disconnected = false;
	sock->connecttime = net_time;
	strcpy (sock->address,"UNSET ADDRESS");
	sock->driver = net_driverlevel;
	sock->socket = 0;
	sock->driverdata = NULL;
	sock->canSend = true;
	sock->sendNext = false;
	sock->lastMessageTime = net_time;
	sock->ackSequence = 0;
	sock->sendSequence = 0;
	sock->unreliableSendSequence = 0;
	sock->sendMessageLength = 0;
	sock->receiveSequence = 0;
	sock->unreliableReceiveSequence = 0;
	sock->receiveMessageLength = 0;

	return sock;
}


void NET_FreeQSocket(qsocket_t *sock)
{
	qsocket_t	*s;

	// remove it from active list
	if (sock == net_activeSockets)
		net_activeSockets = net_activeSockets->next;
	else
	{
		for (s = net_activeSockets; s; s = s->next)
		{
			if (s->next == sock)
			{
				s->next = sock->next;
				break;
			}
		}

		if (!s) Sys_Error ("NET_FreeQSocket: not active\n");
	}

	// add it to free list
	sock->next = net_freeSockets;
	net_freeSockets = sock;
	sock->disconnected = true;
}


void NET_AllocQSockets (int numsockets)
{
	static qsocket_t *sockets = NULL;
	qsocket_t *s;

	if (sockets) MainZone->Free (sockets);

	// add an additional socket for the server
	net_numsockets = (numsockets + 1);
	net_activeSockets = NULL;
	net_freeSockets = NULL;
	sockets = (qsocket_t *) MainZone->Alloc (net_numsockets * sizeof (qsocket_t));

	for (int i = 0; i < net_numsockets; i++)
	{
		s = (qsocket_t *) &sockets[i];
		s->next = net_freeSockets;
		net_freeSockets = s;
		s->disconnected = true;
	}
}


static void NET_Listen_f (void)
{
	if (Cmd_Argc () != 2)
	{
		Con_Printf ("\"listen\" is \"%u\"\n", listening ? 1 : 0);
		return;
	}

	listening = atoi(Cmd_Argv(1)) ? true : false;

	for (net_driverlevel=0; net_driverlevel<net_numdrivers; net_driverlevel++)
	{
		if (net_drivers[net_driverlevel].initialized == false)
			continue;
		net_DriverFunc.Listen (listening);
	}
}


static void MaxPlayers_f (void)
{
	int 	n;

	if (Cmd_Argc () != 2)
	{
		Con_Printf ("\"maxplayers\" is \"%u\"\n", svs.maxclients);
		return;
	}

	if (sv.active)
	{
		Con_Printf ("maxplayers can not be changed while a server is running.\n");
		return;
	}

	n = atoi (Cmd_Argv(1));

	if (n < 1) n = 1;

	if (n > MAX_SCOREBOARD)
	{
		n = MAX_SCOREBOARD;
		Con_Printf ("\"maxplayers\" set to \"%u\"\n", n);
	}

	if ((n == 1) && listening) Cbuf_AddText ("listen 0\n");
	if ((n > 1) && (!listening)) Cbuf_AddText ("listen 1\n");

	svs.maxclients = n;

	// recreate the client structs
	MainZone->Free (svs.clients);
	svs.clients = (client_s *) Zone_Alloc (svs.maxclients * sizeof (client_t));

	// realloc sockets
	NET_AllocQSockets (svs.maxclients);

	if (n == 1)
		Cvar_Set ("deathmatch", "0");
	else Cvar_Set ("deathmatch", "1");
}


void NET_SetPort (char *newport)
{
	int n = atoi (newport);

	if (n < 1 || n > 65534)
	{
		Con_Printf ("Bad value, must be between 1 and 65534\n");
		return;
	}

	DEFAULTnet_hostport = n;
	net_hostport = n;

	if (listening)
	{
		// force a change to the new port
		Cbuf_AddText ("listen 0\n");
		Cbuf_AddText ("listen 1\n");
	}
}


static void NET_Port_f (void)
{
	int 	n;

	if (Cmd_Argc () != 2)
	{
		Con_Printf ("\"port\" is \"%u\"\n", net_hostport);
		return;
	}

	NET_SetPort (Cmd_Argv (1));
}


static void PrintSlistHeader(void)
{
	Con_Printf ("Server          Map             Users\n");
	Con_Printf ("--------------- --------------- -----\n");
	slistLastShown = 0;
}


static void PrintSlist(void)
{
	int n;

	for (n = slistLastShown; n < hostCacheCount; n++)
	{
		if (hostcache[n].maxusers)
			Con_Printf ("%-15.15s %-15.15s %2u/%2u\n", hostcache[n].name, hostcache[n].map, hostcache[n].users, hostcache[n].maxusers);
		else
			Con_Printf ("%-15.15s %-15.15s\n", hostcache[n].name, hostcache[n].map);
	}
	slistLastShown = n;
}


static void PrintSlistTrailer(void)
{
	if (hostCacheCount)
		Con_Printf ("== end list ==\n\n");
	else
		Con_Printf ("No Quake servers found.\n\n");
}


void NET_Slist_f (void)
{
	if (slistInProgress)
		return;

	if (! slistSilent)
	{
		Con_Printf ("Looking for Quake servers...\n");
		PrintSlistHeader();
	}

	slistInProgress = true;
	slistStartTime = Sys_FloatTime();

	SchedulePollProcedure(&slistSendProcedure, 0.0);
	SchedulePollProcedure(&slistPollProcedure, 0.1);

	hostCacheCount = 0;
}


static void Slist_Send (void *soak)
{
	for (net_driverlevel=0; net_driverlevel < net_numdrivers; net_driverlevel++)
	{
		if (!slistLocal && net_driverlevel == 0)
			continue;
		if (net_drivers[net_driverlevel].initialized == false)
			continue;
		net_DriverFunc.SearchForHosts (true);
	}

	if ((Sys_FloatTime() - slistStartTime) < 0.5)
		SchedulePollProcedure(&slistSendProcedure, 0.75);
}


static void Slist_Poll (void *soak)
{
	for (net_driverlevel=0; net_driverlevel < net_numdrivers; net_driverlevel++)
	{
		if (!slistLocal && net_driverlevel == 0)
			continue;
		if (net_drivers[net_driverlevel].initialized == false)
			continue;
		net_DriverFunc.SearchForHosts (false);
	}

	if (! slistSilent)
		PrintSlist();

	if ((Sys_FloatTime() - slistStartTime) < 1.5)
	{
		SchedulePollProcedure(&slistPollProcedure, 0.1);
		return;
	}

	if (! slistSilent)
		PrintSlistTrailer();
	slistInProgress = false;
	slistSilent = false;
	slistLocal = true;
}


/*
===================
NET_Connect
===================
*/

int hostCacheCount = 0;
hostcache_t hostcache[HOSTCACHESIZE];

qsocket_t *NET_Connect (char *host)
{
	qsocket_t		*ret;
	int				n;
	int				numdrivers = net_numdrivers;

	SetNetTime();

	if (host && *host == 0)
		host = NULL;

	if (host)
	{
		if (stricmp (host, "local") == 0)
		{
			numdrivers = 1;
			goto JustDoIt;
		}

		if (hostCacheCount)
		{
			for (n = 0; n < hostCacheCount; n++)
			{
				if (stricmp (host, hostcache[n].name) == 0)
				{
					host = hostcache[n].cname;
					break;
				}
			}

			if (n < hostCacheCount)
				goto JustDoIt;
		}
	}

	slistSilent = host ? true : false;
	NET_Slist_f ();

	while(slistInProgress)
		NET_Poll();

	if (host == NULL)
	{
		if (hostCacheCount != 1)
			return NULL;
		host = hostcache[0].cname;
		Con_Printf ("Connecting to...\n%s @ %s\n\n", hostcache[0].name, host);
	}

	if (hostCacheCount)
	{
		for (n = 0; n < hostCacheCount; n++)
		{
			if (stricmp (host, hostcache[n].name) == 0)
			{
				host = hostcache[n].cname;
				break;
			}
		}
	}

JustDoIt:
	for (net_driverlevel = 0; net_driverlevel < numdrivers; net_driverlevel++)
	{
		if (net_drivers[net_driverlevel].initialized == false)
			continue;
		ret = net_DriverFunc.Connect (host);
		if (ret)
			return ret;
	}

	if (host)
	{
		Con_Printf ("\n");
		PrintSlistHeader();
		PrintSlist();
		PrintSlistTrailer();
	}

	return NULL;
}


/*
===================
NET_CheckNewConnections
===================
*/

struct
{
	float	time;
	int		op;
	long	session;
} vcrConnect;

qsocket_t *NET_CheckNewConnections (void)
{
	qsocket_t	*ret;

	SetNetTime();

	for (net_driverlevel=0; net_driverlevel<net_numdrivers; net_driverlevel++)
	{
		if (net_drivers[net_driverlevel].initialized == false) continue;
		if (net_driverlevel && listening == false) continue;

		ret = net_DriverFunc.CheckNewConnections ();

		if (ret) return ret;
	}

	return NULL;
}

/*
===================
NET_Close
===================
*/
void NET_Close (qsocket_t *sock)
{
	if (!sock)
		return;

	if (sock->disconnected)
		return;

	SetNetTime();

	// call the driver_Close function
	sfunc.Close (sock);

	NET_FreeQSocket(sock);
}


/*
=================
NET_GetMessage

If there is a complete message, return it in net_message

returns 0 if no data is waiting
returns 1 if a message was received
returns -1 if connection is invalid
=================
*/

struct
{
	float	time;
	int		op;
	long	session;
	int		ret;
	int		len;
} vcrGetMessage;

extern void PrintStats(qsocket_t *s);

int	NET_GetMessage (qsocket_t *sock)
{
	int ret;

	if (!sock)
	{
		Con_Printf ("NET_GetMessage: NULL socket\n");
		return -1;
	}

	if (sock->disconnected)
	{
		Con_Printf ("NET_GetMessage: disconnected socket\n");
		return -1;
	}

	SetNetTime();

	ret = sfunc.QGetMessage(sock);

	// see if this connection has timed out
	if (ret == 0 && sock->driver)
	{
		if (net_time - sock->lastMessageTime > net_messagetimeout.value)
		{
			NET_Close(sock);
			Con_Printf ("NET_GetMessage: timed out\n");
			return -1;
		}
	}

	if (ret > 0)
	{
		if (sock->driver)
		{
			sock->lastMessageTime = net_time;
			if (ret == 1)
				messagesReceived++;
			else if (ret == 2)
				unreliableMessagesReceived++;
		}
	}

	if (ret == -1)
		Con_Printf ("NET_GetMessage: generic bad vibes\n");

	return ret;
}


/*
==================
NET_SendMessage

Try to send a complete length+message unit over the reliable stream.
returns 0 if the message cannot be delivered reliably, but the connection
		is still considered valid
returns 1 if the message was sent properly
returns -1 if the connection died
==================
*/
struct
{
	float	time;
	int		op;
	long	session;
	int		r;
} vcrSendMessage;

int NET_SendMessage (qsocket_t *sock, sizebuf_t *data)
{
	int		r;
	
	if (!sock)
		return -1;

	if (sock->disconnected)
	{
		Con_Printf ("NET_SendMessage: disconnected socket\n");
		return -1;
	}

	SetNetTime();
	r = sfunc.QSendMessage(sock, data);
	if (r == 1 && sock->driver)
		messagesSent++;

	return r;
}


int NET_SendUnreliableMessage (qsocket_t *sock, sizebuf_t *data)
{
	int		r;
	
	if (!sock)
		return -1;

	if (sock->disconnected)
	{
		Con_Printf ("NET_SendMessage: disconnected socket\n");
		return -1;
	}

	SetNetTime();
	r = sfunc.SendUnreliableMessage(sock, data);
	if (r == 1 && sock->driver)
		unreliableMessagesSent++;

	return r;
}


/*
==================
NET_CanSendMessage

Returns true or false if the given qsocket can currently accept a
message to be transmitted.
==================
*/
bool NET_CanSendMessage (qsocket_t *sock)
{
	int		r;
	
	if (!sock)
		return false;

	if (sock->disconnected)
		return false;

	SetNetTime();

	r = sfunc.CanSendMessage(sock);

	return r;
}


int NET_SendToAll(sizebuf_t *data, int blocktime)
{
	float		start;
	int			i;
	int			count = 0;
	bool	state1 [MAX_SCOREBOARD];
	bool	state2 [MAX_SCOREBOARD];

	for (i=0, host_client = svs.clients; i<svs.maxclients; i++, host_client++)
	{
		if (!host_client->netconnection)
			continue;
		if (host_client->active)
		{
			if (host_client->netconnection->driver == 0)
			{
				NET_SendMessage(host_client->netconnection, data);
				state1[i] = true;
				state2[i] = true;
				continue;
			}
			count++;
			state1[i] = false;
			state2[i] = false;
		}
		else
		{
			state1[i] = true;
			state2[i] = true;
		}
	}

	start = Sys_FloatTime();

	while (count)
	{
		count = 0;
		for (i=0, host_client = svs.clients; i<svs.maxclients; i++, host_client++)
		{
			if (!state1[i])
			{
				if (NET_CanSendMessage (host_client->netconnection))
				{
					state1[i] = true;
					NET_SendMessage(host_client->netconnection, data);
				}
				else
				{
					NET_GetMessage (host_client->netconnection);
				}
				count++;
				continue;
			}

			if (!state2[i])
			{
				if (NET_CanSendMessage (host_client->netconnection))
				{
					state2[i] = true;
				}
				else
				{
					NET_GetMessage (host_client->netconnection);
				}
				count++;
				continue;
			}
		}
		if ((Sys_FloatTime() - start) > blocktime)
			break;
	}
	return count;
}


//=============================================================================

/*
====================
NET_Init
====================
*/
cmd_t NET_Slist_f_Cmd ("slist", NET_Slist_f);
cmd_t NET_Listen_f_Cmd ("listen", NET_Listen_f);
cmd_t MaxPlayers_f_Cmd ("maxplayers", MaxPlayers_f);
cmd_t NET_Port_f_Cmd ("port", NET_Port_f);

void NET_Init (void)
{
	int			i;
	int			controlSocket;

	i = COM_CheckParm ("-port");

	if (!i) i = COM_CheckParm ("-udpport");

	if (i)
	{
		if (i < com_argc-1)
			DEFAULTnet_hostport = atoi (com_argv[i+1]);
		else
			Sys_Error ("NET_Init: you must specify a number after -port");
	}

	net_hostport = DEFAULTnet_hostport;

	if (COM_CheckParm ("-listen"))
		listening = true;

	NET_AllocQSockets (1);
	SetNetTime ();

	// allocate space for network message buffer
	SZ_Alloc (&net_message, NET_MAXMESSAGE);

	// initialize all the drivers
	for (net_driverlevel=0; net_driverlevel<net_numdrivers; net_driverlevel++)
	{
		controlSocket = net_drivers[net_driverlevel].Init();
		if (controlSocket == -1)
			continue;
		net_drivers[net_driverlevel].initialized = true;
		net_drivers[net_driverlevel].controlSock = controlSocket;
		if (listening)
			net_drivers[net_driverlevel].Listen (true);
	}

	if (*my_tcpip_address) Con_DPrintf("TCP/IP address %s\n", my_tcpip_address);
}


/*
====================
NET_Shutdown
====================
*/

void NET_Shutdown (void)
{
	qsocket_t	*sock;

	SetNetTime();

	for (sock = net_activeSockets; sock; sock = sock->next)
		NET_Close (sock);

	// shutdown the drivers
	for (net_driverlevel = 0; net_driverlevel < net_numdrivers; net_driverlevel++)
	{
		if (net_drivers[net_driverlevel].initialized == true)
		{
			net_drivers[net_driverlevel].Shutdown ();
			net_drivers[net_driverlevel].initialized = false;
		}
	}
}


static PollProcedure *pollProcedureList = NULL;

void NET_Poll(void)
{
	PollProcedure *pp;
	bool	useModem;

	if (!configRestored)
		configRestored = true;

	SetNetTime();

	for (pp = pollProcedureList; pp; pp = pp->next)
	{
		if (pp->nextTime > net_time)
			break;
		pollProcedureList = pp->next;
		pp->procedure(pp->arg);
	}
}


void SchedulePollProcedure(PollProcedure *proc, float timeOffset)
{
	PollProcedure *pp, *prev;

	proc->nextTime = Sys_FloatTime() + timeOffset;
	for (pp = pollProcedureList, prev = NULL; pp; pp = pp->next)
	{
		if (pp->nextTime >= proc->nextTime)
			break;
		prev = pp;
	}

	if (prev == NULL)
	{
		proc->next = pollProcedureList;
		pollProcedureList = proc;
		return;
	}

	proc->next = pp;
	prev->next = proc;
}

