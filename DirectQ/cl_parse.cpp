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
// cl_parse.c  -- parse a message received from the server

#include "quakedef.h"
#include "d3d_model.h"
#include "webdownload.h"

char *svc_strings[] =
{
	"svc_bad",
	"svc_nop",
	"svc_disconnect",
	"svc_updatestat",
	"svc_version",		// [long] server version
	"svc_setview",		// [short] entity number
	"svc_sound",			// <see code>
	"svc_time",			// [float] server time
	"svc_print",			// [string] null terminated string
	"svc_stufftext",		// [string] stuffed into client's console buffer
	// the string should be \n terminated
	"svc_setangle",		// [vec3] set the view angle to this absolute value

	"svc_serverinfo",		// [long] version
	// [string] signon string
	// [string]..[0]model cache [string]...[0]sounds cache
	// [string]..[0]item cache
	"svc_lightstyle",		// [byte] [string]
	"svc_updatename",		// [byte] [string]
	"svc_updatefrags",	// [byte] [short]
	"svc_clientdata",		// <shortbits + data>
	"svc_stopsound",		// <see code>
	"svc_updatecolors",	// [byte] [byte]
	"svc_particle",		// [vec3] <variable>
	"svc_damage",			// [byte] impact [byte] blood [vec3] from

	"svc_spawnstatic",
	"OBSOLETE svc_spawnbinary",
	"svc_spawnbaseline",

	"svc_temp_entity",		// <variable>
	"svc_setpause",
	"svc_signonnum",
	"svc_centerprint",
	"svc_killedmonster",
	"svc_foundsecret",
	"svc_spawnstaticsound",
	"svc_intermission",
	"svc_finale",			// [string] music [string] text
	"svc_cdtrack",			// [byte] track [byte] looptrack
	"svc_sellscreen",
	"svc_cutscene",
	"svc_showlmp",	// [string] iconlabel [string] lmpfile [byte] x [byte] y
	"svc_hidelmp",	// [string] iconlabel
	"svc_skybox", // [string] skyname
	"?", // 38
	"?", // 39
	"svc_bf", // 40						// no data
	"svc_fog", // 41					// [byte] density [byte] red [byte] green [byte] blue [float] time
	"svc_spawnbaseline2", //42			// support for large modelindex, large framenum, alpha, using flags
	"svc_spawnstatic2", // 43			// support for large modelindex, large framenum, alpha, using flags
	"svc_spawnstaticsound2", //	44		// [coord3] [short] samp [byte] vol [byte] aten
	"?", // 45
	"?", // 46
	"?", // 47
	"?", // 48
	"?", // 49
	"svc_skyboxsize", // [coord] size
	"svc_fog" // [byte] enable <optional past this point, only included if enable is true> [float] density [byte] red [byte] green [byte] blue
};

//=============================================================================

void CL_WipeParticles (void);

/*
==================
CL_ReadByteShort2

disgusting BJP protocol hackery crap; regular protocols just read a byte, BJP protocols may read a short
==================
*/
int CL_ReadByteShort2 (bool Compatibility)
{
	if (cl.Protocol == PROTOCOL_VERSION_NQ || cl.Protocol == PROTOCOL_VERSION_FITZ || cl.Protocol == PROTOCOL_VERSION_RMQ)
		return MSG_ReadByte ();
	else if (cl.Protocol < PROTOCOL_VERSION_BJP2 || Compatibility && cl.Protocol > PROTOCOL_VERSION_BJP2)
		return MSG_ReadByte (); // Some progs (Marcher) send sound services, maintain compatibility, kludge
	else return MSG_ReadShort ();
}


/*
==================
CL_ReadByteShort

disgusting BJP protocol hackery crap; regular protocols just read a byte, BJP protocols read a short
==================
*/
int CL_ReadByteShort (void)
{
	if (cl.Protocol == PROTOCOL_VERSION_NQ || cl.Protocol == PROTOCOL_VERSION_FITZ || cl.Protocol == PROTOCOL_VERSION_RMQ)
		return MSG_ReadByte ();
	else return MSG_ReadShort ();
}


int cl_entityallocs = 0;

entity_t *CL_AllocEntity (void)
{
	entity_t *ent = (entity_t *) ClientZone->Alloc (sizeof (entity_t));

	// set it's allocation number
	// allocnumbers deliberately start at 1 as number 0 is used for signifying an entity that should not be occluded
	cl_entityallocs++;
	ent->allocnum = cl_entityallocs;

	return ent;
}


/*
===============
CL_EntityNum

This function checks and tracks the total number of entities
===============
*/
entity_t *CL_EntityNum (int num)
{
	if (num >= cl.num_entities)
	{
		if (num >= MAX_EDICTS)
			Host_Error ("CL_EntityNum: %i is an invalid number", num);

		while (cl.num_entities <= num)
		{
			if (!cl_entities[cl.num_entities])
			{
				// alloc a new entity and set it's number
				cl_entities[cl.num_entities] = CL_AllocEntity ();
				cl_entities[cl.num_entities]->entnum = cl.num_entities;
				cl_entities[cl.num_entities]->alphaval = 255;
			}

			// force cl.numentities up until it exceeds the number we're looking for
			cl.num_entities++;
		}
	}

	return cl_entities[num];
}


/*
==================
CL_ParseStartSoundPacket
==================
*/
void CL_ParseStartSoundPacket (void)
{
	vec3_t  pos;
	int 	channel, ent;
	int 	sound_num;
	int 	volume;
	int 	field_mask;
	float 	attenuation;
	int		i;

	field_mask = MSG_ReadByte();

	if (field_mask & SND_VOLUME)
		volume = MSG_ReadByte ();
	else volume = DEFAULT_SOUND_PACKET_VOLUME;

	if (field_mask & SND_ATTENUATION)
		attenuation = MSG_ReadByte () / 64.0;
	else attenuation = DEFAULT_SOUND_PACKET_ATTENUATION;

	if (field_mask & SND_LARGEENTITY)
	{
		ent = (unsigned short) MSG_ReadShort ();
		channel = MSG_ReadByte ();
	}
	else
	{
		channel = (unsigned short) MSG_ReadShort ();
		ent = channel >> 3;
		channel &= 7;
	}

	if (field_mask & SND_LARGESOUND)
		sound_num = (unsigned short) MSG_ReadShort ();
	else sound_num = (unsigned short) CL_ReadByteShort2 (false);

	if (ent > MAX_EDICTS)
		Host_Error ("CL_ParseStartSoundPacket: ent = %i", ent);

	for (i = 0; i < 3; i++)
		pos[i] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);

	S_StartSound (ent, channel, cl.sound_precache[sound_num], pos, volume / 255.0, attenuation);
}

/*
==================
CL_KeepaliveMessage

When the client is taking a long time to load stuff, send keepalive messages
so the server doesn't disconnect.
==================
*/
void CL_KeepaliveMessage (bool showmsg = true)
{
	float	fTime;
	static float fLastMsg = 0;
	int		ret;
	sizebuf_t	old;

	// yikes!  this buffer was never increased!
	byte *olddata = (byte *) scratchbuf;

	if (sv.active) return;
	if (cls.demoplayback) return;

	// read messages from server, should just be nops
	old = net_message;
	memcpy (olddata, net_message.data, net_message.cursize);

	do
	{
		ret = CL_GetMessage ();

		switch (ret)
		{
		default:
			Host_Error ("CL_KeepaliveMessage: CL_GetMessage failed");
		case 0:
			break;	// nothing waiting
		case 1:
			Host_Error ("CL_KeepaliveMessage: received a message");
			break;
		case 2:
			if (MSG_ReadByte() != svc_nop)
				Host_Error ("CL_KeepaliveMessage: datagram wasn't a nop");

			break;
		}
	} while (ret);

	net_message = old;
	memcpy (net_message.data, olddata, net_message.cursize);

	// check time
	fTime = Sys_FloatTime ();

	if (fTime - fLastMsg < 5) return;

	fLastMsg = fTime;

	// write out a nop
	if (showmsg) Con_Printf ("--> client to server keepalive\n");

	MSG_WriteByte (&cls.message, clc_nop);
	NET_SendMessage (cls.netcon, &cls.message);
	SZ_Clear (&cls.message);
}

void Host_Frame (DWORD time);

static bool CL_WebDownloadProgress (int DownloadSize, int PercentDown)
{
	static int lastpercent = 666;
	int thispercent = (PercentDown / 10);

	CL_KeepaliveMessage (false);

	if (!DownloadSize || !PercentDown)
	{
		lastpercent = 666;
		return true;
	}

	if (lastpercent > thispercent) Con_Printf ("Downloading %i bytes\n", DownloadSize);

	cls.download.percent = PercentDown;

	if (lastpercent != thispercent)
	{
		// give an indication of time
		Con_Printf ("...Downloaded %i%%\n", PercentDown);
		lastpercent = thispercent;
	}

	// simulate a 1 ms frame
	Host_Frame (1);

	// abort if we disconnect
	return !cls.download.disconnect;
}


char *COM_GetFolderForFile (char *filename)
{
	static char foldername[MAX_PATH];

	strcpy (foldername, filename);

	for (int i = strlen (foldername) - 1; i; i--)
	{
		if (foldername[i] == '/' || foldername[i] == '\\')
		{
			foldername[i] = 0;
			break;
		}
	}

	return foldername;
}


/*
==================
CL_ParseServerInfo
==================
*/
// lets get these out of huge arrays on the stack
static char	**model_precache = NULL;
static char	**sound_precache = NULL;
static model_t **static_cl_model_precache = NULL;
static sfx_t **static_cl_sfx_precache = NULL;

extern cvar_t cl_web_download;
extern cvar_t cl_web_download_url;

void CL_DoWebDownload (char *filename)
{
	// create the directory for the target
	Sys_mkdir (COM_GetFolderForFile (filename));

	// to do - this will go into the progress display
	Con_Printf ("Downloading from %s%s\n\n", cl_web_download_url.string, filename);

	// this needs a screen update
	SCR_UpdateScreen (0);

	// default download params
	cls.download.web = true;
	cls.download.disconnect = false;
	cls.download.percent = 0;

	// let the Windows API do it's magic!!
	DWORD DLResult = Web_DoDownload
					 (
						 va ("%s%s", cl_web_download_url.string, filename),
						 va ("%s/%s", com_gamedir, filename),
						 CL_WebDownloadProgress
					 );

	// we're not downloading any more
	cls.download.web = false;
	cls.download.percent = 0;

	// true if the user type disconnect in the middle of the download
	// moved up to here because it could never be called in the old code!!!
	if (cls.download.disconnect)
	{
		Con_Printf ("\n"DIVIDER_LINE"\n");
		Con_Printf ("Download aborted\n");
		Con_Printf (DIVIDER_LINE"\n\n");

		cls.download.disconnect = false;
		CL_Disconnect_f ();
		return;
	}

	// check the result
	if (DLResult == DL_ERR_NO_ERROR)
	{
		// bugfix - if the map isn't on the server the server may give it a 404 page which would report as OK, so we need to
		// verify and validate the file as a BSP file.  this also fixes work PCs where crap like websense is in place.
		FILE *f = fopen (va ("%s/%s", com_gamedir, filename), "rb");

		if (f)
		{
			dheader_t h;
			fread (&h, sizeof (dheader_t), 1, f);
			fclose (f);

			switch (h.version)
			{
			case Q1_BSPVERSION:
			case PR_BSPVERSION:
				break;

			default:
				DeleteFile (va ("%s/%s", com_gamedir, filename));

				Con_Printf ("\n"DIVIDER_LINE"\n");
				Con_Printf ("Couldn't recognise %s as a BSP file\n"
					"Perhaps it is not on the server and you got a 404 page?\n", filename);
				Con_Printf (DIVIDER_LINE"\n\n");
				return;
			}
		}
		else
		{
			// this should never happen
			Con_Printf ("\n"DIVIDER_LINE"\n");
			Con_Printf ("Failed to download %s\n", filename);
			Con_Printf (DIVIDER_LINE"\n\n");
			return;
		}

		// now we know it worked
		Con_Printf ("\n"DIVIDER_LINE"\n");
		Con_Printf ("\nDownload %s succesful\n", filename);
		Con_Printf (DIVIDER_LINE"\n\n");

		// reconnect after each success
		extern char server_name[MAX_QPATH];
		extern int net_hostport;

		Cbuf_AddText (va ("connect %s:%u\n", server_name, net_hostport));
		return;
	}
	else
	{
		Con_Printf ("\n"DIVIDER_LINE"\n");
		Con_Printf ("\nDownload failed with error:\n  %s\n", Web_GetErrorString (DLResult));
		Con_Printf (DIVIDER_LINE"\n\n");
		return;
	}
}


CQuakeZone *PrecacheHeap = NULL;

void CL_ParseServerInfo (void)
{
	char	*str;
	int		i;
	int		nummodels, numsounds;

	// this function can call Con_Printf so explicitly wipe the particles in case Con_Printf
	// needs to call SCR_UpdateScreen.
	CL_WipeParticles ();

	// we can't rely on the map heap being good here as it may not exist on the first demo run
	// so we create a new heap for storing anything used in it.  is this correct?  surely it calls mod_forname?
	SAFE_DELETE (PrecacheHeap);
	PrecacheHeap = new CQuakeZone ();

	model_precache = (char **) PrecacheHeap->Alloc (MAX_MODELS * sizeof (char *));
	sound_precache = (char **) PrecacheHeap->Alloc (MAX_SOUNDS * sizeof (char *));

	static_cl_model_precache = (model_t **) PrecacheHeap->Alloc (MAX_MODELS * sizeof (model_t *));
	static_cl_sfx_precache = (sfx_t **) PrecacheHeap->Alloc (MAX_SOUNDS * sizeof (sfx_t *));

	// wipe the model and sounds precaches fully so that an attempt to reference one beyond
	// the limit of what's loaded will always fail
	for (i = 0; i < MAX_MODELS; i++)
	{
		model_precache[i] = NULL;
		static_cl_model_precache[i] = NULL;
	}

	for (i = 0; i < MAX_SOUNDS; i++)
	{
		sound_precache[i] = NULL;
		static_cl_sfx_precache[i] = NULL;
	}

	Con_DPrintf ("Serverinfo packet received.\n");

	// wipe the client_state_t struct
	CL_ClearState ();

	// parse protocol version number
	i = MSG_ReadLong ();

	if (i == PROTOCOL_VERSION_BJP || i == PROTOCOL_VERSION_BJP2 || i == PROTOCOL_VERSION_BJP3)
		Con_Printf ("\nusing BJP demo protocol %i\n", i);
	else if (i != PROTOCOL_VERSION_NQ && i != PROTOCOL_VERSION_FITZ && i != PROTOCOL_VERSION_RMQ)
	{
		Host_Error
		(
			"Server returned unknown protocol version %i, (not %i, %i or %i)",
			i,
			PROTOCOL_VERSION_NQ,
			PROTOCOL_VERSION_FITZ,
			PROTOCOL_VERSION_RMQ
		);
	}

	cl.Protocol = i;

	// get the correct protocol flags
	if (cl.Protocol == PROTOCOL_VERSION_RMQ)
		cl.PrototcolFlags = (unsigned) MSG_ReadLong ();
	else cl.PrototcolFlags = 0;

	// parse maxclients
	cl.maxclients = MSG_ReadByte ();

	if (cl.maxclients < 1 || cl.maxclients > MAX_SCOREBOARD)
	{
		Con_Printf ("Bad maxclients (%u) from server\n", cl.maxclients);
		return;
	}

	cl.scores = (scoreboard_t *) PrecacheHeap->Alloc (MAX_SCOREBOARD * sizeof (scoreboard_t));

	// parse gametype
	cl.gametype = MSG_ReadByte ();

	// parse signon message
	str = MSG_ReadString ();
	cl.levelname = (char *) PrecacheHeap->Alloc (strlen (str) + 1);
	strcpy (cl.levelname, str);

	// seperate the printfs so the server message can have a color
	Con_Printf ("\n\n\35\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\37\n\n");
	Con_Printf ("%c%s", 2, str);
	Con_Printf ("\n\n\35\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\37\n\n");

	// set up model and sound precache lists
	cl.model_precache = static_cl_model_precache;
	cl.sound_precache = static_cl_sfx_precache;

	// item 1 in each list is set to NULL so that we get proper list termination in the event of a return below
	cl.model_precache[1] = NULL;
	cl.sound_precache[1] = NULL;

	// first we go through and touch all of the precache data that still happens to
	// be in the cache, so precaching something else doesn't needlessly purge it
	for (nummodels = 1;; nummodels++)
	{
		str = MSG_ReadString ();

		if (!str[0])
			break;

		if (nummodels == MAX_MODELS)
		{
			Con_Printf ("Server sent too many model precaches\n");
			return;
		}

		model_precache[nummodels] = (char *) PrecacheHeap->Alloc (strlen (str) + 1);
		strcpy (model_precache[nummodels], str);
		Mod_TouchModel (str);
	}

	for (numsounds = 1;; numsounds++)
	{
		str = MSG_ReadString ();

		if (!str[0])
			break;

		if (numsounds == MAX_SOUNDS)
		{
			Con_Printf ("Server sent too many sound precaches\n");
			return;
		}

		sound_precache[numsounds] = (char *) PrecacheHeap->Alloc (strlen (str) + 1);
		strcpy (sound_precache[numsounds], str);
		S_TouchSound (str);
	}

	// now we try to load everything else until a cache allocation fails
	for (i = 1; i < nummodels; i++)
	{
		// don't crash because we're attempting downloads here
		cl.model_precache[i] = Mod_ForName (model_precache[i], false);

		// avoid memset 0 requirement
		cl.model_precache[i + 1] = NULL;

		if (cl.model_precache[i] == NULL)
		{
			// no web download on a local server or in demos
			if (sv.active || cls.demoplayback || !cl_web_download.value || !cl_web_download_url.string || !cl_web_download_url.string[0])
			{
				// if web download is not being used we don't bother with it
				// (note - this is now a Host_Error because with a NULL worldmodel we'll crash hard elsewhere)
				Host_Error ("Model %s not found\n", model_precache[i]);
				return;
			}

			// attempt a web download
			CL_DoWebDownload (model_precache[i]);

			// always
			return;
		}

		// send a keep-alive after each model
		CL_KeepaliveMessage (false);
	}

	// ensure that the worldmodel loads OK and crash it if not
	cl.model_precache[1] = Mod_ForName (model_precache[1], true);

	// now we do sounds
	S_BeginPrecaching ();

	for (i = 1; i < numsounds; i++)
	{
		cl.sound_precache[i] = S_PrecacheSound (sound_precache[i]);

		// avoid memset 0 requirement
		cl.sound_precache[i + 1] = NULL;

		CL_KeepaliveMessage ();
	}

	S_EndPrecaching ();

	// local state
	// entity 0 is the world
	cl_entities[0]->model = cl.worldmodel = cl.model_precache[1];

	char mapname[MAX_PATH];
	Q_strncpy (mapname, cl.worldmodel->name, MAX_PATH - 1);

	for (int i = strlen (cl.worldmodel->name); i; i--)
	{
		if (cl.worldmodel->name[i] == '/' || cl.worldmodel->name[i] == '\\')
		{
			Q_strncpy (mapname, &cl.worldmodel->name[i + 1], MAX_PATH - 1);
			break;
		}
	}

	extern HWND d3d_Window;

	if (cls.demoplayback)
		UpdateTitlebarText (cls.demoname);
	else UpdateTitlebarText (mapname);

	// clean up zone allocations
	Zone_Compact ();

	// set up map
	R_NewMap ();

	// noclip is turned off at start
	noclip_anglehack = false;

	// refresh ping and status for proquake
	cl.lastpingtime = -1;
	cl.laststatustime = -1;
}


/*
==================
CL_ParseUpdate

Parse an entity update message from the server
If an entities model or origin changes from frame to frame, it must be
relinked.  Other attributes can change without relinking.
==================
*/
void CL_ClearInterpolation (entity_t *ent, int clearflags)
{
	if (clearflags & CLEAR_POSES)
		ent->lerppose[LERP_LAST] = ent->lerppose[LERP_CURR] = ent->lerppose[LERP_CURR];

#if 0
	if (clearflags & CLEAR_ORIGIN)
	{
		VectorCopy2 (ent->lerporigin[LERP_LAST], ent->origin);
		VectorCopy2 (ent->lerporigin[LERP_CURR], ent->origin);
		VectorCopy2 (ent->moveorigin, ent->origin);
	}

	if (clearflags & CLEAR_ANGLES)
	{
		VectorCopy2 (ent->lerpangles[LERP_LAST], ent->angles);
		VectorCopy2 (ent->lerpangles[LERP_CURR], ent->angles);
		VectorCopy2 (ent->moveangles, ent->angles);
	}
#else
	if (clearflags & CLEAR_ORIGIN)
	{
		ent->translatestarttime = 0;
		ent->lerporigin[LERP_LAST][0] = ent->lerporigin[LERP_LAST][1] = ent->lerporigin[LERP_LAST][2] = 0;
		ent->lerporigin[LERP_CURR][0] = ent->lerporigin[LERP_CURR][1] = ent->lerporigin[LERP_CURR][2] = 0;
	}

	if (clearflags & CLEAR_ANGLES)
	{
		ent->rotatestarttime = 0;
		ent->lerpangles[LERP_LAST][0] = ent->lerpangles[LERP_LAST][1] = ent->lerpangles[LERP_LAST][2] = 0;
		ent->lerpangles[LERP_CURR][0] = ent->lerpangles[LERP_CURR][1] = ent->lerpangles[LERP_CURR][2] = 0;
	}
#endif
}


void CL_ParseUpdate (int bits)
{
	int			i;
	model_t		*model;
	int			modnum;
	bool	forcelink;
	entity_t	*ent;
	int			num;
	int			skin;

	if (cls.signon == SIGNON_CONNECTED - 1)
	{
		// first update is the final signon stage
		cls.signon = SIGNON_CONNECTED;
		CL_SignonReply ();
	}

	if (bits & U_MOREBITS)
	{
		i = MSG_ReadByte ();
		bits |= (i << 8);
	}

	if (cl.Protocol == PROTOCOL_VERSION_FITZ || cl.Protocol == PROTOCOL_VERSION_RMQ)
	{
		if (bits & U_EXTEND1) bits |= MSG_ReadByte () << 16;
		if (bits & U_EXTEND2) bits |= MSG_ReadByte () << 24;
	}

	if (bits & U_LONGENTITY)
		num = MSG_ReadShort ();
	else num = MSG_ReadByte ();

	// this is used for both getting an existing entity and creating a new one
	// eeewww.
	ent = CL_EntityNum (num);

	if (ent->msgtime != cl.mtime[1])
	{
		// entity was not present on the previous frame
		forcelink = true;
	}
	else forcelink = false;

	if (ent->msgtime + 0.2 < cl.mtime[0])
	{
		// more than 0.2 seconds since the last message (most entities think every 0.1 sec)
		CL_ClearInterpolation (ent, CLEAR_ALLLERP);
	}

	ent->msgtime = cl.mtime[0];

	if (bits & U_MODEL)
	{
		if (cl.Protocol == PROTOCOL_VERSION_FITZ || cl.Protocol == PROTOCOL_VERSION_RMQ)
			modnum = MSG_ReadByte ();
		else modnum = CL_ReadByteShort ();

		if (modnum >= MAX_MODELS) Host_Error ("CL_ParseModel: bad modnum");
	}
	else modnum = ent->baseline.modelindex;

	// moved before model change check as a change in model could make the baseline frame invalid
	// (e.g. if the ent was originally spawned on a frame other than 0)
	if (bits & U_FRAME)
		ent->frame = MSG_ReadByte ();
	else ent->frame = ent->baseline.frame;

	if (bits & U_COLORMAP)
		i = MSG_ReadByte ();
	else i = ent->baseline.colormap;

	if (!i)
	{
		// no defined skin color
		ent->playerskin = -1;
	}
	else
	{
		if (i > cl.maxclients)
		{
			// no defined skin color
			ent->playerskin = -1;
			Con_DPrintf ("CL_ParseUpdate: i >= cl.maxclients\n");
		}
		else
		{
			// store out the skin color for this player slot
			ent->playerskin = cl.scores[i - 1].colors;
		}
	}

	if (bits & U_SKIN)
		skin = MSG_ReadByte ();
	else skin = ent->baseline.skin;

	if (skin != ent->skinnum)
	{
		// skin has changed
		ent->skinnum = skin;
	}

	if (bits & U_EFFECTS)
		ent->effects = MSG_ReadByte ();
	else ent->effects = ent->baseline.effects;

	// shift the known values for interpolation
	VectorCopy2 (ent->msg_origins[1], ent->msg_origins[0]);
	VectorCopy2 (ent->msg_angles[1], ent->msg_angles[0]);

	if (bits & U_ORIGIN1)
		ent->msg_origins[0][0] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
	else ent->msg_origins[0][0] = ent->baseline.origin[0];

	if (bits & U_ANGLES1)
		ent->msg_angles[0][0] = MSG_ReadAngle (cl.Protocol, cl.PrototcolFlags);
	else ent->msg_angles[0][0] = ent->baseline.angles[0];

	if (bits & U_ORIGIN2)
		ent->msg_origins[0][1] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
	else ent->msg_origins[0][1] = ent->baseline.origin[1];

	if (bits & U_ANGLES2)
		ent->msg_angles[0][1] = MSG_ReadAngle (cl.Protocol, cl.PrototcolFlags);
	else ent->msg_angles[0][1] = ent->baseline.angles[1];

	if (bits & U_ORIGIN3)
		ent->msg_origins[0][2] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
	else ent->msg_origins[0][2] = ent->baseline.origin[2];

	if (bits & U_ANGLES3)
		ent->msg_angles[0][2] = MSG_ReadAngle (cl.Protocol, cl.PrototcolFlags);
	else ent->msg_angles[0][2] = ent->baseline.angles[2];

	// default lerp interval which we can assume for most entities
	ent->lerpinterval = 0.1f;

	if (cl.Protocol == PROTOCOL_VERSION_FITZ || cl.Protocol == PROTOCOL_VERSION_RMQ)
	{
		if (bits & U_ALPHA)
			ent->alphaval = MSG_ReadByte ();
		else ent->alphaval = ent->baseline.alpha;

		if (bits & U_FRAME2) ent->frame = (ent->frame & 0x00FF) | (MSG_ReadByte () << 8);
		if (bits & U_MODEL2) modnum = (modnum & 0x00FF) | (MSG_ReadByte () << 8);

#if 1
		if (bits & U_LERPFINISH)
		{
			ent->lerpinterval = (float) (MSG_ReadByte()) / 255.0f;
			ent->lerpflags |= LERP_FINISH;
			// Con_Printf ("Got a lerpinterval of %f for %s\n", ent->lerpinterval, ent->model->name);
		}
		else
		{
			ent->lerpinterval = 0.1f;
			ent->lerpflags &= ~LERP_FINISH;
		}
#else
		if (bits & U_LERPFINISH) MSG_ReadByte ();
#endif
	}
	else if (bits & U_TRANS) // && (cl.Protocol != PROTOCOL_VERSION_NQ || nehahra))
	{
		// the server controls the protocol so this is safe to do.
		// required as some engines add U_TRANS but don't change PROTOCOL_VERSION_NQ
		// retain neharha protocol compatibility; the 1st and 3rd do nothing yet...
		int transbits = MSG_ReadFloat ();
		ent->alphaval = MSG_ReadFloat () * 255;

		if (transbits == 2) MSG_ReadFloat ();
	}
	else if (ent != cl_entities[cl.viewentity])
	{
		ent->alphaval = 255;
	}

	// an alpha of 0 is equivalent to 255 (so that memset 0 will work correctly)
	if (ent->alphaval < 1) ent->alphaval = 255;

	if (bits & U_NOLERP)
	{
		// only interpolate orientation if we're not doing so on the server
		if (sv.active)
		{
			ent->lerpflags &= ~LERP_MOVESTEP;
			// ent->forcelink = true;
		}
		else
		{
			ent->lerpflags |= LERP_MOVESTEP;
			ent->forcelink = true;
		}
	}
	else ent->lerpflags &= ~LERP_MOVESTEP;

	// this was moved down for protocol fitz messaqe ordering because the model num could be changed by extend bits
	model = cl.model_precache[modnum];

	if (model != ent->model)
	{
		// test - check for entity model changes at runtime, as it fucks up interpolation.
		// this happens more often than you might think.
		// if (model && ent->model) Con_Printf ("Change from %s to %s\n", ent->model->name, model->name);
		ent->model = model;

		// automatic animation (torches, etc) can be either all together
		// or randomized
		if (model)
		{
			if (model->synctype == ST_RAND)
				ent->syncbase = (float) (rand () & 0x7fff) / 0x7fff;
			else ent->syncbase = 0.0;

			// ST_RAND is not always set on animations that should be out of lockstep
			ent->posebase = (float) (rand () & 0x7ff) / 0x7ff;
			ent->skinbase = (float) (rand () & 0x7ff) / 0x7ff;
		}
		else forcelink = true;	// hack to make null model players work

		// if the model has changed we must also reset the interpolation data
		// lerppose[LERP_LAST] and lerppose[LERP_CURR] are critical as they might be pointing to invalid frames in the new model!!!
		CL_ClearInterpolation (ent, CLEAR_POSES);

		// reset frame and skin too...!
		if (!(bits & U_FRAME)) ent->frame = 0;
		if (!(bits & U_SKIN)) ent->skinnum = 0;
	}

	if (forcelink)
	{
		// didn't have an update last message
		VectorCopy2 (ent->msg_origins[1], ent->msg_origins[0]);
		VectorCopy2 (ent->origin, ent->msg_origins[0]);
		VectorCopy2 (ent->msg_angles[1], ent->msg_angles[0]);
		VectorCopy2 (ent->angles, ent->msg_angles[0]);

		// update it's oldorg for particle spawning
		VectorCopy2 (ent->oldorg, ent->origin);

		ent->forcelink = true;

		// fix "dying throes" interpolation bug - reset interpolation data if the entity wasn't updated
		// lerppose[LERP_LAST] and lerppose[LERP_CURR] are critical here; the rest is done for completeness sake.
		CL_ClearInterpolation (ent, CLEAR_ALLLERP);
	}
}


/*
==================
CL_ParseBaseline
==================
*/
void CL_ParseBaseline (entity_t *ent, int version)
{
	int i;
	int bits = (version == 2) ? MSG_ReadByte () : 0;

	if (bits & B_LARGEMODEL)
		ent->baseline.modelindex = MSG_ReadShort ();
	else if (cl.Protocol == PROTOCOL_VERSION_FITZ || cl.Protocol == PROTOCOL_VERSION_RMQ)
 		ent->baseline.modelindex = MSG_ReadByte ();
	else ent->baseline.modelindex = CL_ReadByteShort ();

	ent->baseline.frame = (bits & B_LARGEFRAME) ? MSG_ReadShort () : MSG_ReadByte ();
	ent->baseline.colormap = MSG_ReadByte ();
	ent->baseline.skin = MSG_ReadByte ();

	for (i = 0; i < 3; i++)
	{
		ent->baseline.origin[i] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
		ent->baseline.angles[i] = MSG_ReadAngle (cl.Protocol, cl.PrototcolFlags);
	}

	if (bits & B_ALPHA)
		ent->baseline.alpha = MSG_ReadByte ();
	else ent->baseline.alpha = 255;
}


/*
==================
CL_ParseClientdata

Server information pertaining to this client only
==================
*/
void CL_ParseClientdata (void)
{
	int		i, j;
	int		bits;
	int		clnewstats[32];

	// copy out stats before the parse so we know what needs to be changed
	for (i = 0; i < 32; i++)
		clnewstats[i] = cl.stats[i];

	bits = (unsigned short) MSG_ReadShort ();

	if (bits & SU_EXTEND1) bits |= (MSG_ReadByte () << 16);
	if (bits & SU_EXTEND2) bits |= (MSG_ReadByte () << 24);

	if (bits & SU_VIEWHEIGHT)
		cl.viewheight = MSG_ReadChar ();
	else cl.viewheight = DEFAULT_VIEWHEIGHT;

	// DirectQ no longer does pitch drifting so just swallow the message
	if (bits & SU_IDEALPITCH) MSG_ReadChar ();

	VectorCopy2 (cl.mvelocity[1], cl.mvelocity[0]);

	for (i = 0; i < 3; i++)
	{
		if (bits & (SU_PUNCH1 << i))
			cl.punchangle[i] = MSG_ReadChar();
		else cl.punchangle[i] = 0;

		if (bits & (SU_VELOCITY1 << i))
			cl.mvelocity[0][i] = MSG_ReadChar() * 16;
		else cl.mvelocity[0][i] = 0;
	}

	// always sent
	if ((i = MSG_ReadLong ()) != cl.items)
	{
		// set flash times
		for (j = 0; j < 32; j++)
			if ((i & (1 << j)) && !(cl.items & (1 << j)))
				cl.itemgettime[j] = cl.time;

		cl.items = i;
	}

	cl.onground = (bits & SU_ONGROUND) != 0;
	cl.inwater = (bits & SU_INWATER) != 0;

	clnewstats[STAT_WEAPONFRAME] = (bits & SU_WEAPONFRAME) ? MSG_ReadByte () : 0;
	clnewstats[STAT_ARMOR] = (bits & SU_ARMOR) ? MSG_ReadByte () : 0;

	if (cl.Protocol == PROTOCOL_VERSION_FITZ || cl.Protocol == PROTOCOL_VERSION_RMQ)
		clnewstats[STAT_WEAPON] = (bits & SU_WEAPON) ? MSG_ReadByte () : 0;
	else clnewstats[STAT_WEAPON] = (bits & SU_WEAPON) ? CL_ReadByteShort () : 0;

	clnewstats[STAT_HEALTH] = MSG_ReadShort ();
	clnewstats[STAT_AMMO] = MSG_ReadByte ();

	// fixme - should be short for new protocol???
	clnewstats[STAT_SHELLS + 0] = MSG_ReadByte ();
	clnewstats[STAT_SHELLS + 1] = MSG_ReadByte ();
	clnewstats[STAT_SHELLS + 2] = MSG_ReadByte ();
	clnewstats[STAT_SHELLS + 3] = MSG_ReadByte ();

	if (standard_quake)
		clnewstats[STAT_ACTIVEWEAPON] = MSG_ReadByte ();
	else clnewstats[STAT_ACTIVEWEAPON] = (1 << (MSG_ReadByte ()));

	if (bits & SU_WEAPON2) clnewstats[STAT_WEAPON] |= (MSG_ReadByte () << 8);
	if (bits & SU_ARMOR2) clnewstats[STAT_ARMOR] |= (MSG_ReadByte () << 8);
	if (bits & SU_AMMO2) clnewstats[STAT_AMMO] |= (MSG_ReadByte () << 8);
	if (bits & SU_SHELLS2) clnewstats[STAT_SHELLS] |= (MSG_ReadByte () << 8);
	if (bits & SU_NAILS2) clnewstats[STAT_NAILS] |= (MSG_ReadByte () << 8);
	if (bits & SU_ROCKETS2) clnewstats[STAT_ROCKETS] |= (MSG_ReadByte () << 8);
	if (bits & SU_CELLS2) clnewstats[STAT_CELLS] |= (MSG_ReadByte () << 8);
	if (bits & SU_WEAPONFRAME2) clnewstats[STAT_WEAPONFRAME] |= (MSG_ReadByte () << 8);

	if (cl.stats[STAT_HEALTH] > 0 && clnewstats[STAT_HEALTH] < 1)
	{
		// update death location
		cl.death_location[0] = cl_entities[cl.viewentity]->origin[0];
		cl.death_location[1] = cl_entities[cl.viewentity]->origin[1];
		cl.death_location[2] = cl_entities[cl.viewentity]->origin[2];
		vid.recalc_refdef = true;
		Con_DPrintf ("updated death location\n");
	}
	else if (cl.stats[STAT_HEALTH] < 1 && clnewstats[STAT_HEALTH] > 0)
		vid.recalc_refdef = true;

	// now update the stats for real
	for (i = 0; i < 32; i++)
		cl.stats[i] = clnewstats[i];

	if (bits & SU_WEAPONALPHA)
		cl.viewent.alphaval = MSG_ReadByte ();
	else cl.viewent.alphaval = 255;
}


/*
=====================
CL_ParseStatic
=====================
*/
void R_AddEfrags (entity_t *ent);
void D3DMain_BBoxForEnt (entity_t *ent);
void D3DLight_PrepStaticEntityLighting (entity_t *ent);

void CL_ParseStatic (int version)
{
	if (!cl.worldmodel || !cl.worldmodel->brushhdr)
	{
		Host_Error ("CL_ParseStatic: spawn static without a world\n(are you missing a mod directory?)");
		return;
	}

	// just alloc in the map pool
	entity_t *ent = CL_AllocEntity ();

	// read in baseline state
	CL_ParseBaseline (ent, version);

	// copy it to the current state
	ent->model = cl.model_precache[ent->baseline.modelindex];
	ent->frame = ent->baseline.frame;
	ent->skinnum = ent->baseline.skin;
	ent->effects = ent->baseline.effects;
	ent->alphaval = ent->baseline.alpha;
	ent->efrag = NULL;

	VectorCopy2 (ent->origin, ent->baseline.origin);
	VectorCopy2 (ent->angles, ent->baseline.angles);

	// clear the interpolation data to get a valid baseline
	CL_ClearInterpolation (ent, CLEAR_ALLLERP);

	// ST_RAND is not always set on animations that should be out of lockstep
	ent->posebase = (float) (rand () & 0x7ff) / 0x7ff;
	ent->skinbase = (float) (rand () & 0x7ff) / 0x7ff;

	// some static ents don't have models; that's OK as we only use this for rendering them!
	if (ent->model)
	{
		R_AddEfrags (ent);

		// cut down on recursive lightpoint calls per frame at runtime
		D3DLight_PrepStaticEntityLighting (ent);

		// necessary to call this here???
		D3DMain_BBoxForEnt (ent);
	}
}


/*
===================
CL_ParseStaticSound
===================
*/
void CL_ParseStaticSound (int version)
{
	vec3_t		org;
	int			sound_num, vol, atten;
	int			i;

	for (i = 0; i < 3; i++)
		org[i] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);

	if (version == 2)
		sound_num = MSG_ReadShort ();
	else sound_num = CL_ReadByteShort2 (true);

	vol = MSG_ReadByte ();
	atten = MSG_ReadByte ();

	S_StaticSound (cl.sound_precache[sound_num], org, vol, atten);
}


#define SHOWNET(x) if(cl_shownet.value==2)Con_Printf ("%3i:%s\n", msg_readcount-1, x);

void SHOWLMP_decodehide (void);
void SHOWLMP_decodeshow (void);

int MSG_PeekByte (void);
void CL_ParseProQuakeMessage (void);
void CL_ParseProQuakeString (char *string);

/*
=====================
CL_ParseServerMessage
=====================
*/
void Fog_ParseServerMessage (void);
void Fog_Update (float density, float r, float g, float b, float time);

void CL_ParseServerMessage (void)
{
	int			cmd;
	int			i;

	// if recording demos, copy the message out
	if (cl_shownet.value == 1)
		Con_Printf ("%i ", net_message.cursize);
	else if (cl_shownet.value == 2)
		Con_Printf ("------------------\n");

	cl.onground = false;	// unless the server says otherwise

	// parse the message
	MSG_BeginReading ();

	static int lastcmd = 0;

	while (1)
	{
		if (msg_badread)
			Host_Error ("CL_ParseServerMessage: Bad server message");

		cmd = MSG_ReadByte ();

		if (cmd == -1)
		{
			SHOWNET ("END OF MESSAGE");
			return;		// end of message
		}

		// if the high bit of the command byte is set, it is a fast update
		if (cmd & 128)
		{
			SHOWNET ("fast update");
			CL_ParseUpdate (cmd & 127);
			continue;
		}

		SHOWNET (svc_strings[cmd]);

		// other commands
		switch (cmd)
		{
		default:
			Host_Error ("CL_ParseServerMessage: Illegible server message %i\nlast was %s\n", cmd, svc_strings[lastcmd]);
			break;

		case svc_nop:
			//			Con_Printf ("svc_nop\n");
			break;

		case svc_time:
			cl.mtime[1] = cl.mtime[0];
			cl.mtime[0] = MSG_ReadFloat ();
			// Con_Printf ("svc_time at %f\n", realtime);
			break;

		case svc_clientdata:
			CL_ParseClientdata ();
			break;

		case svc_version:
			i = MSG_ReadLong ();

			// svc_version is never used in the engine.  wtf?  maybe it's from an older version of stuff?
			// don't read flags anyway for compatibility as we have no control over what sent the message
			if (i == PROTOCOL_VERSION_BJP || i == PROTOCOL_VERSION_BJP2 || i == PROTOCOL_VERSION_BJP3)
				Con_Printf ("\nusing BJP demo protocol %i\n", i);
			else if (i != PROTOCOL_VERSION_NQ && i != PROTOCOL_VERSION_FITZ && i != PROTOCOL_VERSION_RMQ)
			{
				Host_Error
				(
					"CL_ParseServerMessage: Server is protocol %i instead of %i, %i or %i",
					i,
					PROTOCOL_VERSION_NQ,
					PROTOCOL_VERSION_FITZ,
					PROTOCOL_VERSION_RMQ
				);
			}

			cl.Protocol = i;
			Con_DPrintf ("Using protocol %i\n", i);

			break;

		case svc_disconnect:
			Con_Printf ("Disconnect\n");
			Host_EndGame ("Server disconnected\n");

		case svc_print:
			if (!cls.download.web)
			{
				char *str = MSG_ReadString ();

				// proquake messaging only exists with protocol 15
				if (cl.Protocol == PROTOCOL_VERSION_NQ)
				{
					// CL_ParseProQuakeString will hose the value of str unless cl.console_ping is set to
					// true before forwarding the command to the server; see Host_Ping_f for an example.
					CL_ParseProQuakeString (str);
				}

				Con_Printf ("%s", str);
			}

			break;

		case svc_centerprint:
			SCR_CenterPrint (MSG_ReadString ());
			break;

		case svc_stufftext:
		{
			// check for proquake messages
			// proquake messaging only exists with protocol 15
			if ((cl.Protocol == PROTOCOL_VERSION_NQ) && (MSG_PeekByte () == MOD_PROQUAKE))
				CL_ParseProQuakeMessage ();

			char *stufftxt = MSG_ReadString ();

			// Still want to add text, even on ProQuake messages.  This guarantees compatibility;
			// unrecognized messages will essentially be ignored but there will be no parse errors
			if (!strnicmp (stufftxt, "crosshair", 9) && nehahra)
			{
				// gotcha FUCKING nehahra
				// the crosshair cvar belongs to the PLAYER, not to the mod
				break;
			}

			Cbuf_AddText (stufftxt);
		}
		break;

		case svc_damage:
			V_ParseDamage ();
			break;

		case svc_serverinfo:
			CL_ParseServerInfo ();
			vid.recalc_refdef = true;	// leave intermission full screen
			break;

		case svc_setangle:
			cl.viewangles[0] = MSG_ReadAngle (cl.Protocol, cl.PrototcolFlags);
			cl.viewangles[1] = MSG_ReadAngle (cl.Protocol, cl.PrototcolFlags);
			cl.viewangles[2] = MSG_ReadAngle (cl.Protocol, cl.PrototcolFlags);
			break;

		case svc_setview:
			cl.viewentity = MSG_ReadShort ();
			break;

		case svc_lightstyle:
			i = MSG_ReadByte ();

			if (i >= MAX_LIGHTSTYLES)
			{
				Con_Printf ("CL_ParseServerMessage: svc_lightstyle > MAX_LIGHTSTYLES\n");
				MSG_ReadString ();
				break;
			}

			// note - 64, not 63, is intentional here
			Q_strncpy ((char *) cl_lightstyle[i].map,  MSG_ReadString (), 64);
			cl_lightstyle[i].length = strlen ((char *) cl_lightstyle[i].map);
			break;

		case svc_sound:
			CL_ParseStartSoundPacket();
			break;

		case svc_stopsound:
			i = MSG_ReadShort();
			S_StopSound (i >> 3, i & 7);
			break;

		case svc_updatename:
			i = MSG_ReadByte ();

			if (i >= cl.maxclients)
			{
				Con_DPrintf ("CL_ParseServerMessage: svc_updatename > MAX_SCOREBOARD\n");
				MSG_ReadString ();
				break;
			}

			Q_strncpy (cl.scores[i].name, MSG_ReadString (), 31);
			break;

		case svc_updatefrags:
			i = MSG_ReadByte ();

			if (i >= cl.maxclients)
			{
				Con_DPrintf ("CL_ParseServerMessage: svc_updatefrags > MAX_SCOREBOARD\n");
				MSG_ReadShort ();
				break;
			}

			cl.scores[i].frags = MSG_ReadShort ();
			break;

		case svc_updatecolors:
			i = MSG_ReadByte ();

			if (i >= cl.maxclients)
			{
				Con_DPrintf ("CL_ParseServerMessage: svc_updatecolors > MAX_SCOREBOARD\n");

				// read and discard the translation
				MSG_ReadByte ();
				break;
			}

			// make the new translation
			cl.scores[i].colors = MSG_ReadByte ();
			break;

		case svc_particle:
			R_ParseParticleEffect ();
			break;

		case svc_spawnbaseline:
			i = MSG_ReadShort ();

			// must use CL_EntityNum() to force cl.num_entities up
			CL_ParseBaseline (CL_EntityNum (i), 1);

			// clear the interpolation data to get a valid baseline
			CL_ClearInterpolation (CL_EntityNum (i), CLEAR_ALLLERP);

			break;

		case svc_spawnstatic:
			CL_ParseStatic (1);
			break;

		case svc_temp_entity:
			CL_ParseTEnt ();
			break;

		case svc_setpause:
		{
			cl.paused = MSG_ReadByte ();

			if (cl.paused)
			{
				CDAudio_Pause ();
			}
			else
			{
				CDAudio_Resume ();
			}
		}
		break;

		case svc_signonnum:
			i = MSG_ReadByte ();

			if (i <= cls.signon)
				Host_Error ("Received signon %i when at %i", i, cls.signon);

			cls.signon = i;
			CL_SignonReply ();
			break;

		case svc_killedmonster:
			cl.stats[STAT_MONSTERS]++;
			break;

		case svc_foundsecret:
			cl.stats[STAT_SECRETS]++;
			break;

		case svc_updatestat:
			i = MSG_ReadByte ();

			if (i < 0 || i >= MAX_CL_STATS)
			{
				Con_DPrintf ("CL_ParseServerMessage: svc_updatestat: %i is invalid\n", i);

				// keep message state consistent
				MSG_ReadLong ();
				break;
			}

			cl.stats[i] = MSG_ReadLong ();;
			break;

		case svc_spawnstaticsound:
			CL_ParseStaticSound (1);
			break;

		case svc_cdtrack:
			cl.cdtrack = MSG_ReadByte ();
			cl.looptrack = MSG_ReadByte ();

			if ((cls.demoplayback || cls.demorecording) && (cls.forcetrack != -1))
				CDAudio_Play ((byte) cls.forcetrack, true);
			else CDAudio_Play ((byte) cl.cdtrack, true);

			break;

		case svc_intermission:
			cl.intermission = 1;
			cl.completed_time = cl.time;
			vid.recalc_refdef = true;	// go to full screen
			break;

		case svc_finale:
			cl.intermission = 2;
			cl.completed_time = cl.time;
			vid.recalc_refdef = true;	// go to full screen
			SCR_CenterPrint (MSG_ReadString ());
			break;

		case svc_cutscene:
			cl.intermission = 3;
			cl.completed_time = cl.time;
			vid.recalc_refdef = true;	// go to full screen
			SCR_CenterPrint (MSG_ReadString ());
			break;

		case svc_sellscreen:
			Cmd_ExecuteString ("help", src_command);
			break;

		case svc_hidelmp:
			SHOWLMP_decodehide ();
			break;

		case svc_showlmp:
			SHOWLMP_decodeshow ();
			break;

		case svc_skybox:
			// case svc_skyboxfitz: (this is 37 too...)
		{
			// cheesy skybox loading
			char *skyname = MSG_ReadString ();
			Cmd_ExecuteString (va ("loadsky %s\n", skyname), src_command);
		}
		break;

		case svc_skyboxsize:
			// irrelevant in directQ
			MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
			break;

		case svc_fogfitz:
			Fog_ParseServerMessage ();
			break;

		case svc_fog:
			if (MSG_ReadByte ())
				Fog_Update (MSG_ReadFloat (), (MSG_ReadByte () / 255.0), (MSG_ReadByte () / 255.0), (MSG_ReadByte () / 255.0), 0);
			else Fog_Update (0, 0, 0, 0, 0);

			break;

		case svc_spawnbaseline2:
			i = MSG_ReadShort ();

			// must use CL_EntityNum() to force cl.num_entities up
			CL_ParseBaseline (CL_EntityNum (i), 2);

			// clear the interpolation data to get a valid baseline
			CL_ClearInterpolation (CL_EntityNum (i), CLEAR_ALLLERP);

			break;

		case svc_spawnstatic2:
			CL_ParseStatic (2);
			break;

		case svc_spawnstaticsound2:
			CL_ParseStaticSound (2);
			break;

		case svc_bf:
			Cmd_ExecuteString ("bf", src_command);
			break;
		}

		lastcmd = cmd;
	}
}


// JPG - added this
int MSG_ReadBytePQ (void)
{
	return MSG_ReadByte () * 16 + MSG_ReadByte () - 272;
}

// JPG - added this
int MSG_ReadShortPQ (void)
{
	return MSG_ReadBytePQ () * 256 + MSG_ReadBytePQ ();
}

/* JPG - added this function for ProQuake messages
=======================
CL_ParseProQuakeMessage
=======================
*/
void CL_ParseProQuakeMessage (void)
{
	int cmd, i;
	int team, frags, shirt, ping;

	MSG_ReadByte ();
	cmd = MSG_ReadByte ();

	switch (cmd)
	{
	case pqc_new_team:
		team = MSG_ReadByte () - 16;

		if (team < 0 || team > 13)
			Host_Error ("CL_ParseProQuakeMessage: pqc_new_team invalid team");

		shirt = MSG_ReadByte() - 16;
		cl.teamgame = true;

		if (!cl.teamscores) break;

		// cl.teamscores[team].frags = 0;	// JPG 3.20 - removed this
		cl.teamscores[team].colors = 16 * shirt + team;
		break;

	case pqc_erase_team:
		team = MSG_ReadByte() - 16;

		if (team < 0 || team > 13)
			Host_Error ("CL_ParseProQuakeMessage: pqc_erase_team invalid team");

		if (!cl.teamscores) break;

		cl.teamscores[team].colors = 0;
		cl.teamscores[team].frags = 0;		// JPG 3.20 - added this
		break;

	case pqc_team_frags:
		team = MSG_ReadByte() - 16;

		if (team < 0 || team > 13) Host_Error ("CL_ParseProQuakeMessage: pqc_team_frags invalid team");

		frags = MSG_ReadShortPQ();

		if (frags & 32768) frags = frags - 65536;

		if (!cl.teamscores) break;

		cl.teamscores[team].frags = frags;
		break;

	case pqc_match_time:
		cl.minutes = MSG_ReadBytePQ ();
		cl.seconds = MSG_ReadBytePQ ();
		cl.last_match_time = cl.time;
		break;

	case pqc_match_reset:
		if (!cl.teamscores) break;

		for (i = 0; i < 14; i++)
		{
			cl.teamscores[i].colors = 0;
			cl.teamscores[i].frags = 0;		// JPG 3.20 - added this
		}

		break;

	case pqc_ping_times:
		while (ping = MSG_ReadShortPQ ())
		{
			if ((ping / 4096) >= cl.maxclients)
			{
				Con_Printf ("CL_ParseProQuakeMessage: pqc_ping_times > MAX_SCOREBOARD\n");
				continue;
			}

			cl.scores[ping / 4096].ping = ping & 4095;
		}

		cl.lastpingtime = cl.time;
		break;
	}
}


void Q_Version (char *s)
{
	static float q_version_reply_time = -20.0; // Baker: so it can be instantly used
	char *t;
	int l = 0, n = 0;

	// Baker: do not allow spamming of it, 20 second interval max
	if (realtime - q_version_reply_time < 20)
		return;

	t = s;
	t += 1;  // Baker: lazy, to avoid name "q_version" triggering this; later do it "right"
	l = strlen (t);

	while (n < l)
	{
		if (!strncmp (t, ": q_version", 9))
		{
			Cbuf_AddText (va ("say DirectQ "DIRECTQ_VERSION"\n"));
			Cbuf_Execute ();
			q_version_reply_time = realtime;
			break; // Baker: only do once per string
		}

		n += 1;
		t += 1;
	}
}

/* JPG - on a svc_print, check to see if the string contains useful information
======================
CL_ParseProQuakeString
======================
*/
cvar_t pq_scoreboard_pings ("pq_scoreboard_pings", "1", CVAR_ARCHIVE);

// set cl.console_ping = true before sending a ping to the server if you don't want this to swallow the result
// set cl.console_status = true before sending a status to the server if you don't want this to swallow the result
void CL_ParseProQuakeString (char *string)
{
	static int checkping = -1;
	char *s, *s2, *s3;
	static int checkip = -1;	// player whose IP address we're expecting

	// JPG 1.05 - for ip logging
	static int remove_status = 0;
	static int begin_status = 0;
	static int playercount = 0;

	// JPG 3.02 - made this more robust.. try to eliminate screwups due to "unconnected" and '\n'
	s = string;

	if (!strcmp (string, "Client ping times:\n") && pq_scoreboard_pings.value)
	{
		cl.lastpingtime = cl.time;
		checkping = 0;

		if (!cl.console_ping) *string = 0;
	}
	else if (checkping >= 0)
	{
		while (*s == ' ') s++;

		int ping = 0;

		if (*s >= '0' && *s <= '9')
		{
			while (*s >= '0' && *s <= '9')
				ping = 10 * ping + *s++ - '0';

			if ((*s++ == ' ') && *s && (s2 = strchr (s, '\n')))
			{
				s3 = cl.scores[checkping].name;

				while ((s3 = strchr (s3, '\n')) && s2)
				{
					s3++;
					s2 = strchr (s2 + 1, '\n');
				}

				if (s2)
				{
					*s2 = 0;

					if (!strncmp (cl.scores[checkping].name, s, 15))
					{
						cl.scores[checkping].ping = ping > 9999 ? 9999 : ping;

						for (checkping++; !*cl.scores[checkping].name && checkping < cl.maxclients; checkping++);
					}

					*s2 = '\n';
				}

				if (!cl.console_ping) *string = 0;
				if (checkping == cl.maxclients) checkping = -1;
			}
			else checkping = -1;
		}
		else checkping = -1;

		cl.console_ping = cl.console_ping && (checkping >= 0);	// JPG 1.05 cl.sbar_ping -> cl.console_ping
	}

	// check for match time
	if (!strncmp (string, "Match ends in ", 14))
	{
		s = string + 14;

		if ((*s != 'T') && strchr (s, 'm'))
		{
			sscanf (s, "%d", &cl.minutes);
			cl.seconds = 0;
			cl.last_match_time = cl.time;
		}
	}
	else if (!strcmp (string, "Match paused\n"))
		cl.match_pause_time = cl.time;
	else if (!strcmp (string, "Match unpaused\n"))
	{
		cl.last_match_time += (cl.time - cl.match_pause_time);
		cl.match_pause_time = 0;
	}
	else if (!strcmp (string, "The match is over\n") || !strncmp (string, "Match begins in", 15))
		cl.minutes = 255;
	else if (checkping < 0)
	{
		s = string;
		int i = 0;

		while (*s >= '0' && *s <= '9')
			i = 10 * i + *s++ - '0';

		if (!strcmp (s, " minutes remaining\n"))
		{
			cl.minutes = i;
			cl.seconds = 0;
			cl.last_match_time = cl.time;
		}
	}

	// JPG 1.05 check for IP information
	if (iplog_size)
	{
		if (!strncmp (string, "host:    ", 9))
		{
			begin_status = 1;

			if (!cl.console_status)
				remove_status = 1;
		}

		if (begin_status && !strncmp (string, "players: ", 9))
		{
			begin_status = 0;
			remove_status = 0;

			if (sscanf (string + 9, "%d", &playercount))
			{
				if (!cl.console_status)
					*string = 0;
			}
			else playercount = 0;
		}
		else if (playercount && string[0] == '#')
		{
			if (!sscanf (string, "#%d", &checkip) || --checkip < 0 || checkip >= cl.maxclients) checkip = -1;
			if (!cl.console_status) *string = 0;

			remove_status = 0;
		}
		else if (checkip != -1)
		{
			int a, b, c;

			if (sscanf (string, "   %d.%d.%d", &a, &b, &c) == 3)
			{
				cl.scores[checkip].addr = (a << 16) | (b << 8) | c;
				IPLog_Add (cl.scores[checkip].addr, cl.scores[checkip].name);
			}

			checkip = -1;

			if (!cl.console_status) *string = 0;

			remove_status = 0;

			if (!--playercount) cl.console_status = 0;
		}
		else
		{
			playercount = 0;

			if (remove_status) *string = 0;
		}
	}

	Q_Version (string); //R00k: look for "q_version" requests
}

