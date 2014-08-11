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
	"?", // 40
	"?", // 41
	"?", // 42
	"?", // 43
	"?", // 44
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
				cl_entities[cl.num_entities] = (entity_t *) Pool_Alloc (POOL_MAP, sizeof (entity_t));
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
	
	channel = MSG_ReadShort ();
	sound_num = CL_ReadByteShort2 (false);

	ent = channel >> 3;
	channel &= 7;

	if (ent > MAX_EDICTS)
		Host_Error ("CL_ParseStartSoundPacket: ent = %i", ent);
	
	for (i=0 ; i<3 ; i++)
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
void CL_KeepaliveMessage (void)
{
	float	time;
	static float lastmsg;
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
	time = Sys_FloatTime ();
	if (time - lastmsg < 5)
		return;
	lastmsg = time;

// write out a nop
	Con_Printf ("--> client to server keepalive\n");

	MSG_WriteByte (&cls.message, clc_nop);
	NET_SendMessage (cls.netcon, &cls.message);
	SZ_Clear (&cls.message);
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

void CL_ParseServerInfo (void)
{
	char	*str;
	int		i;
	int		nummodels, numsounds;

	if (!model_precache)
	{
		model_precache = (char **) Pool_Alloc (POOL_PERMANENT, MAX_MODELS * sizeof (char *));

		for (i = 0; i < MAX_MODELS; i++)
			model_precache[i] = (char *) Pool_Alloc (POOL_PERMANENT, MAX_QPATH * sizeof (char));
	}

	if (!sound_precache)
	{
		sound_precache = (char **) Pool_Alloc (POOL_PERMANENT, MAX_SOUNDS * sizeof (char *));

		for (i = 0; i < MAX_SOUNDS; i++)
			sound_precache[i] = (char *) Pool_Alloc (POOL_PERMANENT, MAX_QPATH * sizeof (char));
	}

	// alloc these in permanent memory first time they're needed
	if (!static_cl_model_precache) static_cl_model_precache = (model_t **) Pool_Alloc (POOL_PERMANENT, MAX_MODELS * sizeof (model_t *));
	if (!static_cl_sfx_precache) static_cl_sfx_precache = (sfx_t **) Pool_Alloc (POOL_PERMANENT, MAX_SOUNDS * sizeof (sfx_t *));

	Con_DPrintf ("Serverinfo packet received.\n");

	// wipe the client_state_t struct
	CL_ClearState ();

	// parse protocol version number
	i = MSG_ReadLong ();

	if (i != PROTOCOL_VERSION && (i < PROTOCOL_VERSION_BJP || i > PROTOCOL_VERSION_MH))
		Host_Error ("Server returned unknown protocol version %i, (not %i or %i-%i)", i, PROTOCOL_VERSION, PROTOCOL_VERSION_BJP, PROTOCOL_VERSION_MH);

	cl.Protocol = i;

	// parse maxclients
	cl.maxclients = MSG_ReadByte ();

	if (cl.maxclients < 1 || cl.maxclients > MAX_SCOREBOARD)
	{
		Con_Printf("Bad maxclients (%u) from server\n", cl.maxclients);
		return;
	}

	cl.scores = (scoreboard_t *) Pool_Alloc (POOL_MAP, cl.maxclients * sizeof (*cl.scores));

	// parse gametype
	cl.gametype = MSG_ReadByte ();

	// parse signon message
	str = MSG_ReadString ();
	strncpy (cl.levelname, str, sizeof (cl.levelname) - 1);

	// seperate the printfs so the server message can have a color
	Con_Printf("\n\n\35\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\37\n\n");
	Con_Printf ("%c%s\n", 2, str);

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

		strncpy (model_precache[nummodels], str, MAX_QPATH - 1);
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

		strncpy (sound_precache[numsounds], str, MAX_QPATH - 1);
		S_TouchSound (str);
	}

	// now we try to load everything else until a cache allocation fails
	for (i = 1; i < nummodels; i++)
	{
		cl.model_precache[i] = Mod_ForName (model_precache[i], false);

		// avoid memset 0 requirement
		cl.model_precache[i + 1] = NULL;

		if (cl.model_precache[i] == NULL)
		{
			Con_Printf("Model %s not found\n", model_precache[i]);
			return;
		}

		CL_KeepaliveMessage ();
	}

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

	// take a pointer to the brush header
	cl.worldbrush = cl.worldmodel->bh;

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
		bits |= (i<<8);
	}

	if (bits & U_LONGENTITY)	
		num = MSG_ReadShort ();
	else num = MSG_ReadByte ();

	ent = CL_EntityNum (num);

	for (i = 0; i < 16; i++)
		if (bits & (1 << i))
			bitcounts[i]++;

	if (ent->msgtime != cl.mtime[1])
		forcelink = true;	// no previous frame to lerp from
	else forcelink = false;

	ent->msgtime = cl.mtime[0];

	if (bits & U_MODEL)
	{
		modnum = CL_ReadByteShort ();
		if (modnum >= MAX_MODELS)
			Host_Error ("CL_ParseModel: bad modnum");
	}
	else modnum = ent->baseline.modelindex;

	model = cl.model_precache[modnum];

	// moved before model change check as a change in model could make the baseline frame invalid
	// (e.g. if the ent was originally spawned on a frame other than 0)
	if (bits & U_FRAME)
		ent->frame = MSG_ReadByte ();
	else ent->frame = ent->baseline.frame;

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
				ent->syncbase = (float)(rand () &0x7fff) / 0x7fff;
			else ent->syncbase = 0.0;
		}
		else forcelink = true;	// hack to make null model players work

		if (num > 0 && num <= cl.maxclients) D3D_TranslatePlayerSkin (num - 1);

		// if the model has changed we must also reset the interpolation data
		// pose1 and pose2 are critical as they might be pointing to invalid frames in the new model!!!
		ent->frame_start_time = 0;
		ent->frame_interval = 0;
		ent->pose1 = ent->pose2 = 0;
		ent->translate_start_time = 0;
		ent->origin1[0] = ent->origin1[1] = ent->origin1[2] = 0;
		ent->origin2[0] = ent->origin2[1] = ent->origin2[2] = 0;
		ent->rotate_start_time = 0;
		ent->angles1[0] = ent->angles1[1] = ent->angles1[2] = 0;
		ent->angles2[0] = ent->angles2[1] = ent->angles2[2] = 0;

		// reset frame too...!
		ent->frame = 0;
	}

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
		skin = MSG_ReadByte();
	else skin = ent->baseline.skin;

	if (skin != ent->skinnum)
	{
		ent->skinnum = skin;
		if (num > 0 && num <= cl.maxclients)
			D3D_TranslatePlayerSkin (num - 1);
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
		ent->msg_angles[0][0] = MSG_ReadAngle();
	else ent->msg_angles[0][0] = ent->baseline.angles[0];

	if (bits & U_ORIGIN2)
		ent->msg_origins[0][1] = MSG_ReadCoord ();
	else ent->msg_origins[0][1] = ent->baseline.origin[1];

	if (bits & U_ANGLE2)
		ent->msg_angles[0][1] = MSG_ReadAngle ();
	else ent->msg_angles[0][1] = ent->baseline.angles[1];

	if (bits & U_ORIGIN3)
		ent->msg_origins[0][2] = MSG_ReadCoord ();
	else ent->msg_origins[0][2] = ent->baseline.origin[2];

	if (bits & U_ANGLE3)
		ent->msg_angles[0][2] = MSG_ReadAngle ();
	else ent->msg_angles[0][2] = ent->baseline.angles[2];

	// the server controls the protocol so this is safe to do.
	// required as some engines add U_TRANS but don't change PROTOCOL_VERSION
	if (bits & U_TRANS) // && (cl.Protocol != PROTOCOL_VERSION || nehahra))
	{
		// retain neharha protocol compatibility; the 1st and 3rd do nothing yet...
		int transbits = MSG_ReadFloat ();
		ent->alphaval = MSG_ReadFloat () * 256;
		if (transbits == 2) MSG_ReadFloat ();

		// hack - nehahra sends alpha of 0 in some (many?) cases
		// bjp nehahra engine does this too
		if (ent->alphaval < 1 && nehahra) ent->alphaval = 255;
	}
	else if (ent != cl_entities[cl.viewentity])
	{
		// this will stomp the alphaval set in chase, so don't do it
		ent->alphaval = 255;
	}

	if (bits & U_NOLERP) ent->forcelink = true;

	if (forcelink)
	{
		// didn't have an update last message
		VectorCopy (ent->msg_origins[0], ent->msg_origins[1]);
		VectorCopy (ent->msg_origins[0], ent->origin);
		VectorCopy (ent->msg_angles[0], ent->msg_angles[1]);
		VectorCopy (ent->msg_angles[0], ent->angles);
		ent->forcelink = true;

		// fix "dying throes" interpolation bug - reset interpolation data if the entity wasn't updated
		// pose1 and pose2 are critical here; the rest is done for completeness sake.
		ent->frame_start_time = 0;
		ent->frame_interval = 0;
		ent->pose1 = ent->pose2 = 0;
		ent->translate_start_time = 0;
		ent->origin1[0] = ent->origin1[1] = ent->origin1[2] = 0;
		ent->origin2[0] = ent->origin2[1] = ent->origin2[2] = 0;
		ent->rotate_start_time = 0;
		ent->angles1[0] = ent->angles1[1] = ent->angles1[2] = 0;
		ent->angles2[0] = ent->angles2[1] = ent->angles2[2] = 0;
	}

	if (cl.Protocol >= PROTOCOL_VERSION_MH)
	{
		// update the baseline
		VectorCopy (ent->msg_angles[0], ent->baseline.angles);
		VectorCopy (ent->msg_origins[0], ent->baseline.origin);
	}
}


/*
==================
CL_ParseBaseline
==================
*/
void CL_ParseBaseline (entity_t *ent)
{
	int			i;
	
	ent->baseline.modelindex = CL_ReadByteShort ();
	ent->baseline.frame = MSG_ReadByte ();
	ent->baseline.colormap = MSG_ReadByte();
	ent->baseline.skin = MSG_ReadByte();
	for (i=0 ; i<3 ; i++)
	{
		ent->baseline.origin[i] = MSG_ReadCoord ();
		ent->baseline.angles[i] = MSG_ReadAngle ();
	}
}


/*
==================
CL_ParseClientdata

Server information pertaining to this client only
==================
*/
void CL_ParseClientdata (int bits)
{
	int		i, j;
	
	if (bits & SU_VIEWHEIGHT)
		cl.viewheight = MSG_ReadChar ();
	else
		cl.viewheight = DEFAULT_VIEWHEIGHT;

	if (bits & SU_IDEALPITCH)
		cl.idealpitch = MSG_ReadChar ();
	else
		cl.idealpitch = 0;
	
	VectorCopy (cl.mvelocity[0], cl.mvelocity[1]);
	for (i=0 ; i<3 ; i++)
	{
		if (bits & (SU_PUNCH1<<i) )
			cl.punchangle[i] = MSG_ReadChar();
		else
			cl.punchangle[i] = 0;
		if (bits & (SU_VELOCITY1<<i) )
			cl.mvelocity[0][i] = MSG_ReadChar()*16;
		else
			cl.mvelocity[0][i] = 0;
	}

// [always sent]	if (bits & SU_ITEMS)
		i = MSG_ReadLong ();

	if (cl.items != i)
	{
		// set flash times
		for (j=0 ; j<32 ; j++)
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
		i = CL_ReadByteShort ();
	else
		i = 0;
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

	for (i=0 ; i<4 ; i++)
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
	bottom = (cl.scores[slot].colors &15)<<4;

	D3D_TranslatePlayerSkin (slot);

	// fixme - old winquake?
	for (i = 0; i < VID_GRADES; i++, dest += 256, source += 256)
	{
		if (top < 128)	// the artists made some backwards ranges.  sigh.
			memcpy (dest + TOP_RANGE, source + top, 16);
		else
			for (j=0 ; j<16 ; j++)
				dest[TOP_RANGE+j] = source[top+15-j];
				
		if (bottom < 128)
			memcpy (dest + BOTTOM_RANGE, source + bottom, 16);
		else
			for (j=0 ; j<16 ; j++)
				dest[BOTTOM_RANGE+j] = source[bottom+15-j];		
	}
}


vec3_t absmins;
vec3_t absmaxs;

// this is basically the "a lof of this goes away" thing in the old gl_refrag...
// or at least one version of it.  see also R_AddStaticEntitiesForLeaf and the various struct defs
void CL_FindTouchedLeafs (entity_t *ent, mnode_t *node)
{
	mplane_t	*splitplane;
	int			sides;

loc0:;
	if (node->contents == CONTENTS_SOLID) return;

	// add as a touched leaf if the node is a leaf
	if (node->contents < 0)
	{
loc1:;
		mleaf_t *leaf = (mleaf_t *) node;
		staticent_t *se = (staticent_t *) Pool_Alloc (POOL_MAP, sizeof (staticent_t));
		se->ent = ent;

		se->next = leaf->statics;
		leaf->statics = se;
		return;
	}

	splitplane = node->plane;
	sides = BOX_ON_PLANE_SIDE (absmins, absmaxs, splitplane);

	// recurse down the contacted sides
#if 1
	if ((sides & 1) && node->children[0]->contents != CONTENTS_SOLID)
	{
		if (!(sides & 2) && node->children[0]->contents < 0)
		{
			node = node->children[0];
			goto loc1;
		}
		else if (!(sides & 2))
		{
			node = node->children[0];
			goto loc0;
		}
		else CL_FindTouchedLeafs (ent, node->children[0]);
	}

	if ((sides & 2) && node->children[1]->contents != CONTENTS_SOLID)
	{
		// test for a leaf and drop out if so, otherwise it's a node so go round again
		node = node->children[1];

		if (node->contents < 0)
			goto loc1;
		else goto loc0;	// CL_FindTouchedLeafs (ent, node);
	}
#else
	switch (sides)
	{
	case 1:
		node = node->children[0];
		goto loc0;

	case 2:
		node = node->children[1];
		goto loc0;

	default:
		// (sides & 1) && (sides & 2)
		if (node->children[0]->contents != CONTENTS_SOLID)
			CL_FindTouchedLeafs (ent, node->children[0]);

		node = node->children[1];
		goto loc0;
	}
#endif
}


/*
=====================
CL_ParseStatic
=====================
*/
void CL_ParseStatic (void)
{
	if (!cl.worldbrush)
	{
		Host_Error ("CL_ParseStatic: spawn static without a world\n(are you missing a mod directory?)");
		return;
	}

	entity_t *ent;

	ent = (entity_t *) Pool_Alloc (POOL_MAP, sizeof (entity_t));
	CL_ParseBaseline (ent);

	// copy it to the current state
	ent->model = cl.model_precache[ent->baseline.modelindex];
	ent->frame = ent->baseline.frame;
	ent->colormap = vid.colormap;
	ent->skinnum = ent->baseline.skin;
	ent->effects = ent->baseline.effects;
	ent->alphaval = 255;

	VectorCopy (ent->baseline.origin, ent->origin);
	VectorCopy (ent->baseline.angles, ent->angles);

	// some static ents don't have models; that's OK as we only use this for rendering them!
	if (ent->model)
	{
		// setup absmin and absmax for the entity
		VectorAdd (ent->origin, ent->model->mins, absmins);
		VectorAdd (ent->origin, ent->model->maxs, absmaxs);

		// find all leafs which this static ent touches
		CL_FindTouchedLeafs (ent, cl.worldbrush->nodes);
	}

	// not removed yet
	ent->staticremoved = false;
}


/*
===================
CL_ParseStaticSound
===================
*/
void CL_ParseStaticSound (void)
{
	vec3_t		org;
	int			sound_num, vol, atten;
	int			i;
	
	for (i=0 ; i<3 ; i++)
		org[i] = MSG_ReadCoord ();
	sound_num = CL_ReadByteShort2 (true);
	vol = MSG_ReadByte ();
	atten = MSG_ReadByte ();
	
	S_StaticSound (cl.sound_precache[sound_num], org, vol, atten);
}


#define SHOWNET(x) if(cl_shownet.value==2)Con_Printf ("%3i:%s\n", msg_readcount-1, x);

void SHOWLMP_decodehide (void);
void SHOWLMP_decodeshow (void);

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
			i = MSG_ReadShort ();
			CL_ParseClientdata (i);
			break;

		case svc_version:
			i = MSG_ReadLong ();
			if (i != PROTOCOL_VERSION && (i < PROTOCOL_VERSION_BJP || i > PROTOCOL_VERSION_MH))
				Host_Error ("CL_ParseServerMessage: Server is protocol %i instead of %i or %i-%i", i, PROTOCOL_VERSION, PROTOCOL_VERSION_BJP, PROTOCOL_VERSION_MH);

			cl.Protocol = i;

			break;

		case svc_disconnect:
			Host_EndGame ("Server disconnected\n");

		case svc_print:
			Con_Printf ("%s", MSG_ReadString ());
			break;

		case svc_centerprint:
			SCR_CenterPrint (MSG_ReadString ());
			break;

		case svc_stufftext:
			{
				char *stufftxt = MSG_ReadString ();

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
			for (i=0 ; i<3 ; i++)
				cl.viewangles[i] = MSG_ReadAngle ();
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
			strncpy (cl_lightstyle[i].map,  MSG_ReadString(), 64);
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

			strncpy (cl.scores[i].name, MSG_ReadString (), 31);
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
				MSG_ReadByte ();
				break;
			}

			cl.scores[i].colors = MSG_ReadByte ();
			CL_NewTranslation (i);
			break;

		case svc_particle:
			R_ParseParticleEffect ();
			break;

		case svc_spawnbaseline:
			i = MSG_ReadShort ();
			// must use CL_EntityNum() to force cl.num_entities up
			CL_ParseBaseline (CL_EntityNum(i));
			break;
		case svc_spawnstatic:
			CL_ParseStatic ();
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
			CL_ParseStaticSound ();
			break;

		case svc_cdtrack:
			cl.cdtrack = MSG_ReadByte ();
			cl.looptrack = MSG_ReadByte ();
			if ( (cls.demoplayback || cls.demorecording) && (cls.forcetrack != -1) )
				CDAudio_Play ((byte)cls.forcetrack, true);
			else
				CDAudio_Play ((byte)cl.cdtrack, true);
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
			{
				// cheesy skybox loading
				char *skyname = MSG_ReadString ();
				Cmd_ExecuteString (va ("loadsky %s\n", skyname), src_command);
			}
			break;

		case svc_skyboxsize:
			MSG_ReadCoord ();
			break;

		case svc_fog:
			if (MSG_ReadByte())
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
		}

		lastcmd = cmd;
	}
}

