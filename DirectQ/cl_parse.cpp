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
// cl_parse.c  -- parse a message received from the server

#include "quakedef.h"
#include "d3d_model.h"
#include "webdownload.h"

void D3D_TranslatePlayerSkin (int playernum);

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

/*
==================
CL_ReadByteShort2
==================
*/
/*
==================
CL_ReadByteShort2
==================
*/
int CL_ReadByteShort2 (bool Compatibility)
{
	if (cl.Protocol < PROTOCOL_VERSION_BJP2 || Compatibility && cl.Protocol > PROTOCOL_VERSION_BJP2)
		return MSG_ReadByte (); // Some progs (Marcher) send sound services, maintain compatibility, kludge
	else return MSG_ReadShort ();
}


/*
==================
CL_ReadByteShort
==================
*/
int CL_ReadByteShort (void)
{
	return cl.Protocol == PROTOCOL_VERSION ? MSG_ReadByte () : MSG_ReadShort ();
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
			Host_Error ("CL_EntityNum: %i is an invalid number",num);

		while (cl.num_entities <= num)
		{
			if (!cl_entities[cl.num_entities])
			{
				// alloc a new entity and set it's number
				cl_entities[cl.num_entities] = (entity_t *) Pool_Map->Alloc (sizeof (entity_t));
				cl_entities[cl.num_entities]->entnum = cl.num_entities;
			}

			// force cl.numentities up until it exceeds the number we're looking for
			cl_entities[cl.num_entities]->colormap = vid.colormap;
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
void CL_ParseStartSoundPacket(void)
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
	else
		volume = DEFAULT_SOUND_PACKET_VOLUME;
	
    if (field_mask & SND_ATTENUATION)
		attenuation = MSG_ReadByte () / 64.0;
	else
		attenuation = DEFAULT_SOUND_PACKET_ATTENUATION;
	
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
	
	for (i=0; i<3; i++)
		pos[i] = MSG_ReadCoord ();
 
    S_StartSound (ent, channel, cl.sound_precache[sound_num], pos, volume/255.0, attenuation);
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
	DWORD	time;
	static DWORD lastmsg;
	int		ret;
	sizebuf_t	old;
	byte		olddata[8192];
	
	if (sv.active)
		return;		// no need if server is local
	if (cls.demoplayback)
		return;

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
	time = Sys_DWORDTime ();

	if (time - lastmsg < 5000) return;

	lastmsg = time;

	// write out a nop
	if (showmsg) Con_Printf ("--> client to server keepalive\n");

	MSG_WriteByte (&cls.message, clc_nop);
	NET_SendMessage (cls.netcon, &cls.message);
	SZ_Clear (&cls.message);
}

void Host_Frame (DWORD time);

static bool CL_WebDownloadProgress (int DownloadSize, int PercentDown)
{
	static DWORD time, oldtime, newtime;
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

	newtime = Sys_DWORDTime ();
	time = newtime - oldtime;

	if (lastpercent != thispercent)
	{
		// give an indication of time
		Con_Printf ("...Downloaded %i%%\n", PercentDown);
		lastpercent = thispercent;
	}

	Host_Frame (time);

	oldtime = newtime;

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
	SCR_UpdateScreen ();

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
		Con_Printf ("Download aborted\n");
		cls.download.disconnect = false;
		CL_Disconnect_f ();
		return;
	}

	// check the result
	if (DLResult == DL_ERR_NO_ERROR)
	{
		// now we know it worked
		Con_Printf ("\nDownload succesful\n");

		// reconnect after each success
		extern char server_name[MAX_QPATH];
		extern int net_hostport;

		Cbuf_AddText (va ("connect %s:%u\n", server_name, net_hostport));
		return;
	}
	else
	{
		Con_Printf ("\nDownload failed with error:\n  %s\n", Web_GetErrorString (DLResult));
		return;
	}
}


void CL_ParseServerInfo (void)
{
	char	*str;
	int		i;
	int		nummodels, numsounds;

	if (!model_precache)
	{
		model_precache = (char **) Pool_Permanent->Alloc (MAX_MODELS * sizeof (char *));

		for (i = 0; i < MAX_MODELS; i++)
			model_precache[i] = (char *) Pool_Permanent->Alloc (MAX_QPATH * sizeof (char));
	}

	if (!sound_precache)
	{
		sound_precache = (char **) Pool_Permanent->Alloc (MAX_SOUNDS * sizeof (char *));

		for (i = 0; i < MAX_SOUNDS; i++)
			sound_precache[i] = (char *) Pool_Permanent->Alloc (MAX_QPATH * sizeof (char));
	}

	// alloc these in permanent memory first time they're needed
	if (!static_cl_model_precache) static_cl_model_precache = (model_t **) Pool_Permanent->Alloc (MAX_MODELS * sizeof (model_t *));
	if (!static_cl_sfx_precache) static_cl_sfx_precache = (sfx_t **) Pool_Permanent->Alloc (MAX_SOUNDS * sizeof (sfx_t *));

	// wipe the model and sounds precaches fully so that an attempt to reference one beyond
	// the limit of what's loaded will always fail
	for (i = 0; i < MAX_MODELS; i++) static_cl_model_precache[i] = NULL;
	for (i = 0; i < MAX_SOUNDS; i++) static_cl_sfx_precache[i] = NULL;

	Con_DPrintf ("Serverinfo packet received.\n");

	// wipe the client_state_t struct
	CL_ClearState ();

	// parse protocol version number
	i = MSG_ReadLong ();

	if (i != PROTOCOL_VERSION && i != PROTOCOL_VERSION_FITZ && (i < PROTOCOL_VERSION_BJP || i > PROTOCOL_VERSION_MH))
	{
		Host_Error
		(
			"Server returned unknown protocol version %i, (not %i, %i or %i-%i)",
			i,
			PROTOCOL_VERSION,
			PROTOCOL_VERSION_FITZ,
			PROTOCOL_VERSION_BJP,
			PROTOCOL_VERSION_MH
		);
	}

	cl.Protocol = i;

	// parse maxclients
	cl.maxclients = MSG_ReadByte ();

	if (cl.maxclients < 1 || cl.maxclients > MAX_SCOREBOARD)
	{
		Con_Printf ("Bad maxclients (%u) from server\n", cl.maxclients);
		return;
	}

	cl.scores = (scoreboard_t *) Pool_Map->Alloc (cl.maxclients * sizeof (scoreboard_t));

	// parse gametype
	cl.gametype = MSG_ReadByte ();

	// parse signon message
	str = MSG_ReadString ();
	cl.levelname = (char *) Pool_Map->Alloc (strlen (str) + 1);
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

		Q_strncpy (model_precache[nummodels], str, MAX_QPATH - 1);
		Mod_TouchModel (str);
	}

	for (numsounds = 1;; numsounds++)
	{
		str = MSG_ReadString ();
		if (!str[0])
			break;
		if (numsounds==MAX_SOUNDS)
		{
			Con_Printf ("Server sent too many sound precaches\n");
			return;
		}

		Q_strncpy (sound_precache[numsounds], str, MAX_QPATH - 1);
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
				Con_Printf ("Model %s not found\n", model_precache[i]);
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
		SetWindowText (d3d_Window, va ("DirectQ Release %s - %s - %s (%s)", DIRECTQ_VERSION, com_gamename, cl.levelname, cls.demoname));
	else SetWindowText (d3d_Window, va ("DirectQ Release %s - %s - %s (%s)", DIRECTQ_VERSION, com_gamename, cl.levelname, mapname));

	// clean up zone allocations
	Zone_Compact ();

	// set up map
	R_NewMap ();

	// noclip is turned off at start
	noclip_anglehack = false;
}


/*
==================
CL_ParseUpdate

Parse an entity update message from the server
If an entities model or origin changes from frame to frame, it must be
relinked.  Other attributes can change without relinking.
==================
*/
int	bitcounts[16];

void CL_ClearInterpolation (entity_t *ent)
{
	ent->frame_start_time = 0;
	ent->lastpose = ent->currpose;

	ent->translate_start_time = 0;
	ent->lastorigin[0] = ent->lastorigin[1] = ent->lastorigin[2] = 0;
	ent->currorigin[0] = ent->currorigin[1] = ent->currorigin[2] = 0;

	ent->rotate_start_time = 0;
	ent->lastangles[0] = ent->lastangles[1] = ent->lastangles[2] = 0;
	ent->currangles[0] = ent->currangles[1] = ent->currangles[2] = 0;
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

	if (cls.signon == SIGNONS - 1)
	{
		// first update is the final signon stage
		cls.signon = SIGNONS;
		CL_SignonReply ();
	}

	if (bits & U_MOREBITS)
	{
		i = MSG_ReadByte ();
		bits |= (i << 8);
	}

	if (cl.Protocol == PROTOCOL_VERSION_FITZ)
	{
		if (bits & U_EXTEND1) bits |= MSG_ReadByte () << 16;
		if (bits & U_EXTEND2) bits |= MSG_ReadByte () << 24;
	}

	if (bits & U_LONGENTITY)	
		num = MSG_ReadShort ();
	else num = MSG_ReadByte ();

	ent = CL_EntityNum (num);

	for (i = 0; i < 16; i++)
		if (bits & (1 << i))
			bitcounts[i]++;

	if (ent->msgtime != cl.mtime[1])
	{
		// entity was not present on the previous frame
		// assume it's not occluded until such a time as we prove otherwise
		ent->occluded = false;
		forcelink = true;
	}
	else forcelink = false;

	ent->msgtime = cl.mtime[0];

	if (bits & U_MODEL)
	{
		if (cl.Protocol == PROTOCOL_VERSION_FITZ)
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
		i = MSG_ReadByte();
	else i = ent->baseline.colormap;

	if (!i)
		ent->colormap = vid.colormap;
	else
	{
		if (i > cl.maxclients)
			Con_DPrintf ("CL_ParseUpdate: i >= cl.maxclients\n");
		else ent->colormap = cl.scores[i - 1].translations;
	}

	if (bits & U_SKIN)
		skin = MSG_ReadByte ();
	else skin = ent->baseline.skin;

	if (skin != ent->skinnum)
	{
		// skin has changed
		ent->skinnum = skin;

		// retranslate it if it's a player skin
		if (num > 0 && num <= cl.maxclients) D3D_TranslatePlayerSkin (num - 1);
	}

	if (bits & U_EFFECTS)
		ent->effects = MSG_ReadByte();
	else ent->effects = ent->baseline.effects;

	// shift the known values for interpolation
	VectorCopy (ent->msg_origins[0], ent->msg_origins[1]);
	VectorCopy (ent->msg_angles[0], ent->msg_angles[1]);

	if (bits & U_ORIGIN1)
		ent->msg_origins[0][0] = MSG_ReadCoord ();
	else ent->msg_origins[0][0] = ent->baseline.origin[0];

	if (bits & U_ANGLE1)
		ent->msg_angles[0][0] = MSG_ReadAngle (true);
	else ent->msg_angles[0][0] = ent->baseline.angles[0];

	if (bits & U_ORIGIN2)
		ent->msg_origins[0][1] = MSG_ReadCoord ();
	else ent->msg_origins[0][1] = ent->baseline.origin[1];

	if (bits & U_ANGLE2)
		ent->msg_angles[0][1] = MSG_ReadAngle (true);
	else ent->msg_angles[0][1] = ent->baseline.angles[1];

	if (bits & U_ORIGIN3)
		ent->msg_origins[0][2] = MSG_ReadCoord ();
	else ent->msg_origins[0][2] = ent->baseline.origin[2];

	if (bits & U_ANGLE3)
		ent->msg_angles[0][2] = MSG_ReadAngle (true);
	else ent->msg_angles[0][2] = ent->baseline.angles[2];

	if (cl.Protocol == PROTOCOL_VERSION_FITZ)
	{
		if (bits & U_ALPHA)
			ent->alphaval = MSG_ReadByte ();
		else ent->alphaval = ent->baseline.alpha;

		if (bits & U_FRAME2) ent->frame = (ent->frame & 0x00FF) | (MSG_ReadByte () << 8);
		if (bits & U_MODEL2) modnum = (modnum & 0x00FF) | (MSG_ReadByte () << 8);

		// directq doesn't do no fitzquake-style lerping so just silently read the byte
		if (bits & U_LERPFINISH) MSG_ReadByte ();
	}
	else if (bits & U_TRANS) // && (cl.Protocol != PROTOCOL_VERSION || nehahra))
	{
		// the server controls the protocol so this is safe to do.
		// required as some engines add U_TRANS but don't change PROTOCOL_VERSION
		// retain neharha protocol compatibility; the 1st and 3rd do nothing yet...
		int transbits = MSG_ReadFloat ();
		ent->alphaval = MSG_ReadFloat () * 256;
		if (transbits == 2) MSG_ReadFloat ();
	}
	else if (ent != cl_entities[cl.viewentity])
	{
		// this will stomp the alphaval set in chase, so don't do it
		ent->alphaval = 255;
	}

	// an alpha of 0 is equivalent to 255 (so that memset 0 will work correctly)
	if (ent->alphaval < 1) ent->alphaval = 255;

	if (bits & U_NOLERP) ent->forcelink = true;

	// this was moved down for protocol fitz messaqe ordering because the model num could be changed by extend bits
	model = cl.model_precache[modnum];

	if (model != ent->model)
	{
		// test - check for entity model changes at runtime, as it fucks up interpolation.
		// this happens more often than you might think.
		// if (model && ent->model) Con_Printf ("Change from %s to %s\n", ent->model->name, model->name);
		ent->model = model;

		// the new model will most likely have different bbox dimensions
		ent->occluded = false;

		// automatic animation (torches, etc) can be either all together
		// or randomized
		if (model)
		{
			if (model->synctype == ST_RAND)
				ent->syncbase = (float) (rand () &0x7fff) / 0x7fff;
			else ent->syncbase = 0.0;

			// ST_RAND is not always set on animations that should be out of lockstep
			ent->posebase = (float) (rand () & 0x7ff) / 0x7ff;
			ent->skinbase = (float) (rand () & 0x7ff) / 0x7ff;
		}
		else forcelink = true;	// hack to make null model players work

		// if the moddl has changed and it's a player skin we need to retranslate it
		if (num > 0 && num <= cl.maxclients) D3D_TranslatePlayerSkin (num - 1);

		// if the model has changed we must also reset the interpolation data
		// lastpose and currpose are critical as they might be pointing to invalid frames in the new model!!!
		CL_ClearInterpolation (ent);

		// reset frame and skin too...!
		if (!(bits & U_FRAME)) ent->frame = 0;
		if (!(bits & U_SKIN)) ent->skinnum = 0;
	}

	if (forcelink)
	{
		// didn't have an update last message
		VectorCopy (ent->msg_origins[0], ent->msg_origins[1]);
		VectorCopy (ent->msg_origins[0], ent->origin);
		VectorCopy (ent->msg_angles[0], ent->msg_angles[1]);
		VectorCopy (ent->msg_angles[0], ent->angles);

		ent->forcelink = true;

		// fix "dying throes" interpolation bug - reset interpolation data if the entity wasn't updated
		// lastpose and currpose are critical here; the rest is done for completeness sake.
		CL_ClearInterpolation (ent);
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
	else if (cl.Protocol == PROTOCOL_VERSION_FITZ)
		ent->baseline.modelindex = MSG_ReadByte ();
	else ent->baseline.modelindex = CL_ReadByteShort ();

	ent->baseline.frame = (bits & B_LARGEFRAME) ? MSG_ReadShort () : MSG_ReadByte ();
	ent->baseline.colormap = MSG_ReadByte ();
	ent->baseline.skin = MSG_ReadByte ();

	for (i = 0; i < 3; i++)
	{
		ent->baseline.origin[i] = MSG_ReadCoord ();
		ent->baseline.angles[i] = MSG_ReadAngle (true);
	}

	ent->baseline.alpha = (bits & B_ALPHA) ? MSG_ReadByte () : ENTALPHA_DEFAULT;
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

	bits = (unsigned short) MSG_ReadShort ();

	if (bits & SU_EXTEND1) bits |= (MSG_ReadByte () << 16);
	if (bits & SU_EXTEND2) bits |= (MSG_ReadByte () << 24);

	if (bits & SU_VIEWHEIGHT)
		cl.viewheight = MSG_ReadChar ();
	else cl.viewheight = DEFAULT_VIEWHEIGHT;

	if (bits & SU_IDEALPITCH)
		cl.idealpitch = MSG_ReadChar ();
	else cl.idealpitch = 0;

	VectorCopy (cl.mvelocity[0], cl.mvelocity[1]);

	for (i=0; i<3; i++)
	{
		if (bits & (SU_PUNCH1<<i) )
			cl.punchangle[i] = MSG_ReadChar();
		else cl.punchangle[i] = 0;

		if (bits & (SU_VELOCITY1<<i) )
			cl.mvelocity[0][i] = MSG_ReadChar()*16;
		else cl.mvelocity[0][i] = 0;
	}

// [always sent]	if (bits & SU_ITEMS)
		i = MSG_ReadLong ();

	if (cl.items != i)
	{
		// set flash times
		for (j=0; j<32; j++)
			if ( (i & (1<<j)) && !(cl.items & (1<<j)))
				cl.item_gettime[j] = cl.time;
		cl.items = i;
	}
		
	cl.onground = (bits & SU_ONGROUND) != 0;
	cl.inwater = (bits & SU_INWATER) != 0;

	if (bits & SU_WEAPONFRAME)
		cl.stats[STAT_WEAPONFRAME] = MSG_ReadByte ();
	else
		cl.stats[STAT_WEAPONFRAME] = 0;

	if (bits & SU_ARMOR)
		i = MSG_ReadByte ();
	else
		i = 0;
	if (cl.stats[STAT_ARMOR] != i)
	{
		cl.stats[STAT_ARMOR] = i;
	}

	if (bits & SU_WEAPON)
	{
		if (cl.Protocol == PROTOCOL_VERSION_FITZ)
			i = MSG_ReadByte ();
		else i = CL_ReadByteShort ();
	}
	else i = 0;

	if (cl.stats[STAT_WEAPON] != i)
	{
		cl.stats[STAT_WEAPON] = i;
	}

	i = MSG_ReadShort ();
	if (cl.stats[STAT_HEALTH] != i)
	{
		cl.stats[STAT_HEALTH] = i;
	}

	i = MSG_ReadByte ();
	if (cl.stats[STAT_AMMO] != i)
	{
		cl.stats[STAT_AMMO] = i;
	}

	for (i=0; i<4; i++)
	{
		j = MSG_ReadByte ();
		if (cl.stats[STAT_SHELLS+i] != j)
		{
			cl.stats[STAT_SHELLS+i] = j;
		}
	}

	i = MSG_ReadByte ();

	if (standard_quake)
	{
		if (cl.stats[STAT_ACTIVEWEAPON] != i)
		{
			cl.stats[STAT_ACTIVEWEAPON] = i;
		}
	}
	else
	{
		if (cl.stats[STAT_ACTIVEWEAPON] != (1<<i))
		{
			cl.stats[STAT_ACTIVEWEAPON] = (1<<i);
		}
	}

	if (bits & SU_WEAPON2) cl.stats[STAT_WEAPON] |= (MSG_ReadByte () << 8);
	if (bits & SU_ARMOR2) cl.stats[STAT_ARMOR] |= (MSG_ReadByte () << 8);
	if (bits & SU_AMMO2) cl.stats[STAT_AMMO] |= (MSG_ReadByte () << 8);
	if (bits & SU_SHELLS2) cl.stats[STAT_SHELLS] |= (MSG_ReadByte () << 8);
	if (bits & SU_NAILS2) cl.stats[STAT_NAILS] |= (MSG_ReadByte () << 8);
	if (bits & SU_ROCKETS2) cl.stats[STAT_ROCKETS] |= (MSG_ReadByte () << 8);
	if (bits & SU_CELLS2) cl.stats[STAT_CELLS] |= (MSG_ReadByte () << 8);
	if (bits & SU_WEAPONFRAME2) cl.stats[STAT_WEAPONFRAME] |= (MSG_ReadByte () << 8);

	if (bits & SU_WEAPONALPHA)
		cl.viewent.alphaval = MSG_ReadByte ();
	else cl.viewent.alphaval = 255;
}

/*
=====================
CL_NewTranslation
=====================
*/
void CL_NewTranslation (int slot)
{
	int		i, j;
	int		top, bottom;
	byte	*dest, *source;

	if (slot > cl.maxclients)
		Sys_Error ("CL_NewTranslation: slot > cl.maxclients");
	dest = cl.scores[slot].translations;
	source = vid.colormap;
	memcpy (dest, vid.colormap, sizeof(cl.scores[slot].translations));
	top = cl.scores[slot].colors & 0xf0;
	bottom = (cl.scores[slot].colors & 15) << 4;

	// retranslate it (runtime change of colour)
	D3D_TranslatePlayerSkin (slot);

	// fixme - old winquake?
	for (i = 0; i < VID_GRADES; i++, dest += 256, source += 256)
	{
		if (top < 128)	// the artists made some backwards ranges.  sigh.
			memcpy (dest + TOP_RANGE, source + top, 16);
		else
			for (j=0; j<16; j++)
				dest[TOP_RANGE+j] = source[top+15-j];
				
		if (bottom < 128)
			memcpy (dest + BOTTOM_RANGE, source + bottom, 16);
		else
			for (j=0; j<16; j++)
				dest[BOTTOM_RANGE+j] = source[bottom+15-j];		
	}
}


/*
=====================
CL_ParseStatic
=====================
*/
void R_AddEfrags (entity_t *ent);

void CL_ParseStatic (int version)
{
	if (!cl.worldmodel || !cl.worldmodel->brushhdr)
	{
		Host_Error ("CL_ParseStatic: spawn static without a world\n(are you missing a mod directory?)");
		return;
	}

	// just alloc in the map pool
	entity_t *ent = (entity_t *) Pool_Map->Alloc (sizeof (entity_t));
	memset (ent, 0, sizeof (entity_t));

	// read in baseline state
	CL_ParseBaseline (ent, version);

	// copy it to the current state
	ent->model = cl.model_precache[ent->baseline.modelindex];
	ent->frame = ent->baseline.frame;
	ent->colormap = vid.colormap;
	ent->skinnum = ent->baseline.skin;
	ent->effects = ent->baseline.effects;
	ent->alphaval = 255;
	ent->efrag = NULL;
	ent->occlusion = NULL;
	ent->occluded = false;

	VectorCopy (ent->baseline.origin, ent->origin);
	VectorCopy (ent->baseline.angles, ent->angles);

	// some static ents don't have models; that's OK as we only use this for rendering them!
	if (ent->model) R_AddEfrags (ent);
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
	
	for (i=0; i<3; i++)
		org[i] = MSG_ReadCoord ();

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

void D3D_DeleteTranslation (int playernum);

int MSG_PeekByte (void);
void CL_ParseProQuakeMessage (void);
void CL_ParseProQuakeString (char *string);

/*
=====================
CL_ParseServerMessage
=====================
*/
void CL_ParseServerMessage (void)
{
	int			cmd;
	int			i;

//
// if recording demos, copy the message out
//
	if (cl_shownet.value == 1)
		Con_Printf ("%i ",net_message.cursize);
	else if (cl_shownet.value == 2)
		Con_Printf ("------------------\n");
	
	cl.onground = false;	// unless the server says otherwise	
//
// parse the message
//
	MSG_BeginReading ();

	static int lastcmd = 0;

	while (1)
	{
		if (msg_badread)
			Host_Error ("CL_ParseServerMessage: Bad server message");

		cmd = MSG_ReadByte ();

		if (cmd == -1)
		{
			SHOWNET("END OF MESSAGE");
			return;		// end of message
		}

		// if the high bit of the command byte is set, it is a fast update
		if (cmd & 128)
		{
			SHOWNET("fast update");
			CL_ParseUpdate (cmd & 127);
			continue;
		}

		SHOWNET(svc_strings[cmd]);

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
			break;

		case svc_clientdata:
			CL_ParseClientdata ();
			break;

		case svc_version:
			i = MSG_ReadLong ();

			if (i != PROTOCOL_VERSION && i != PROTOCOL_VERSION_FITZ && (i < PROTOCOL_VERSION_BJP || i > PROTOCOL_VERSION_MH))
			{
				Host_Error
				(
					"CL_ParseServerMessage: Server is protocol %i instead of %i, %i or %i-%i",
					i,
					PROTOCOL_VERSION,
					PROTOCOL_VERSION_FITZ,
					PROTOCOL_VERSION_BJP,
					PROTOCOL_VERSION_MH
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
				if (cl.Protocol == PROTOCOL_VERSION)
					CL_ParseProQuakeString (str);

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
				if ((cl.Protocol == PROTOCOL_VERSION) && (MSG_PeekByte () == MOD_PROQUAKE))
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
			for (i=0; i<3; i++)
				cl.viewangles[i] = MSG_ReadAngle (true);
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
			Q_strncpy (cl_lightstyle[i].map,  MSG_ReadString(), 64);
			cl_lightstyle[i].length = strlen(cl_lightstyle[i].map);
			break;

		case svc_sound:
			CL_ParseStartSoundPacket();
			break;

		case svc_stopsound:
			i = MSG_ReadShort();
			S_StopSound(i>>3, i&7);
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

			// delete the old translation if it's unused
			D3D_DeleteTranslation (i);

			// make the new translation
			cl.scores[i].colors = MSG_ReadByte ();
			CL_NewTranslation (i);
			break;

		case svc_particle:
			R_ParseParticleEffect ();
			break;

		case svc_spawnbaseline:
			i = MSG_ReadShort ();
			// must use CL_EntityNum() to force cl.num_entities up
			CL_ParseBaseline (CL_EntityNum (i), 1);
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
			else
				CDAudio_Play ((byte) cl.cdtrack, true);
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
			MSG_ReadCoord ();
			break;

		case svc_fogfitz:
			// protocol compatibility; does nothing for now
			MSG_ReadByte ();
			MSG_ReadByte ();
			MSG_ReadByte ();
			MSG_ReadByte ();
			MSG_ReadShort ();
			break;

		case svc_fog:
			if (MSG_ReadByte ())
			{
				Cvar_Set ("gl_fogdensity", MSG_ReadFloat ());
				Cvar_Set ("gl_fogred", MSG_ReadByte () / 255.0);
				Cvar_Set ("gl_foggreen", MSG_ReadByte () / 255.0);
				Cvar_Set ("gl_fogblue", MSG_ReadByte () / 255.0);
				Cvar_Set ("gl_fogenable", 1);
			}
			else
			{
				Cvar_Set ("gl_fogenable", 0.0f);
				Cvar_Set ("gl_fogdensity", 0.0f);
			}

			break;

		case svc_spawnbaseline2:
			i = MSG_ReadShort ();
			// must use CL_EntityNum() to force cl.num_entities up
			CL_ParseBaseline (CL_EntityNum (i), 2);
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
				Host_Error ("CL_ParseProQuakeMessage: pqc_ping_times > MAX_SCOREBOARD");

			cl.scores[ping / 4096].ping = ping & 4095;
		}

		cl.last_ping_time = cl.time;
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
			Cbuf_AddText (va ("say ProQuake version (DirectQ emulated)\n"));
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

// no iplog in directq
#define iplog_size 0

void IPLog_Add (int blah1, char *blah2)
{
}

void CL_ParseProQuakeString (char *string)
{
	static int checkping = -1;
	int ping, i;
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
		cl.last_ping_time = cl.time;
		checkping = 0;
		if (!cl.console_ping) *string = 0;
	}
	else if (checkping >= 0)
	{
		while (*s == ' ') s++;
		ping = 0;

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

		if ((*s != 'T') && strchr(s, 'm'))
		{
			sscanf(s, "%d", &cl.minutes);
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
		i = 0;

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
