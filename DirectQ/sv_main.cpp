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
// sv_main.c -- server main program

#include "quakedef.h"

// switched default to 1 cos of optimized tracing; name is for consistency with other engines
cvar_t sv_antiwallhack ("sv_cullentities", "1", CVAR_ARCHIVE | CVAR_SERVER);

int		sv_max_datagram = MAX_DATAGRAM; // Must be set so client will work without server

server_t		sv;
server_static_t	svs;

// inline model names for precache - extra space for an extra digit
char	localmodels[MAX_MODELS][6];


edict_t *SV_AllocEdicts (int numedicts)
{
	sv.max_edicts += numedicts;
	edict_t *edbuf = (edict_t *) Pool_Alloc (POOL_EDICTS, numedicts * pr_edict_size);

	Con_DPrintf ("%i edicts\n", sv.max_edicts);

	return edbuf;
}


/*
==================
SV_WriteByteShort2
==================
*/
void SV_WriteByteShort2 (sizebuf_t *sb, int c, bool Compatibility)
{
	// Some progs (Marcher) send sound services, maintain compatibility, kludge
	if (sv.Protocol < PROTOCOL_VERSION_BJP2 || Compatibility && sv.Protocol > PROTOCOL_VERSION_BJP2)
		MSG_WriteByte (sb, c);
	else MSG_WriteShort (sb, c);
}


/*
==================
SV_WriteByteShort
==================
*/
void SV_WriteByteShort (sizebuf_t *sb, int c)
{
	int PrevSize = sv.signon.cursize;

	if (sv.Protocol == PROTOCOL_VERSION)
		MSG_WriteByte (sb, c);
	else
		MSG_WriteShort (sb, c); // PROTOCOL_VERSION_BJP-PROTOCOL_VERSION_MH

	if (sb == &sv.signon) sv.signondiff += sv.signon.cursize - (PrevSize + 1); // Track extra bytes due to >256 model support, kludge
}


//============================================================================

/*
===============
SV_SetProtocol_f
===============
*/
static int sv_protocol = PROTOCOL_VERSION_MH;
extern char *protolist[];

static void SV_SetProtocol_f (void)
{
	if (Cmd_Argc() == 1)
	{
		Con_Printf ("sv_protocol is %d\n", sv_protocol);
		return;
	}

	char *newprotocol = Cmd_Argv (1);

	for (int i = 0; ; i++)
	{
		if (!protolist[i]) break;

		if (!stricmp (protolist[i], newprotocol))
		{
			// note - this assumes that new protocols are numbered sequentially from 10000
			if (!i)
				sv_protocol = 15;
			else sv_protocol = 9999 + i;

			return;
		}
	}

	sv_protocol = atoi (newprotocol);
}

cmd_t SV_SetProtocol_Cmd ("sv_protocol", SV_SetProtocol_f);

/*
===============
SV_Init
===============
*/
void PR_StackInit (void);

void SV_Init (void)
{
	for (int i = 0; i < MAX_MODELS; i++) _snprintf (localmodels[i], 6, "*%i", i);

	memset (&sv, 0, sizeof (server_t));

	// any other server-side init goes here (mostly memory allocs for the large static buffers in use)
	PR_StackInit ();
}


/*
=============================================================================

EVENT MESSAGES

=============================================================================
*/

/*  
==================
SV_StartParticle

Make sure the event gets sent to all clients
==================
*/
void SV_StartParticle (vec3_t org, vec3_t dir, int color, int count)
{
	int		i, v;

	if (sv.datagram.cursize > MAX_DATAGRAM2-16)
		return;	
	MSG_WriteByte (&sv.datagram, svc_particle);
	MSG_WriteCoord (&sv.datagram, org[0]);
	MSG_WriteCoord (&sv.datagram, org[1]);
	MSG_WriteCoord (&sv.datagram, org[2]);
	for (i=0 ; i<3 ; i++)
	{
		v = dir[i]*16;
		if (v > 127)
			v = 127;
		else if (v < -128)
			v = -128;
		MSG_WriteChar (&sv.datagram, v);
	}

	MSG_WriteByte (&sv.datagram, count);
	MSG_WriteByte (&sv.datagram, color);
}           

/*  
==================
SV_StartSound

Each entity can have eight independant sound sources, like voice,
weapon, feet, etc.

Channel 0 is an auto-allocate channel, the others override anything
allready running on that entity/channel pair.

An attenuation of 0 will play full volume everywhere in the level.
Larger attenuations will drop off.  (max 4 attenuation)

==================
*/
void SV_StartSound (edict_t *entity, int channel, char *sample, int volume, float attenuation)
{
    int sound_num;
    int field_mask;
    int	i;
	int ent;

	// these were always stupid as errors
	if (volume < 0)
	{
		Con_DPrintf ("SV_StartSound: volume = %i\n", volume);
		volume = 0;
	}

	if (volume > 255)
	{
		Con_DPrintf ("SV_StartSound: volume = %i\n", volume);
		volume = 255;
	}

	if (attenuation < 0)
	{
		Con_DPrintf ("SV_StartSound: attenuation = %f\n", attenuation);
		attenuation = 0;
	}

	if (attenuation > 4)
	{
		Con_DPrintf ("SV_StartSound: attenuation = %f\n", attenuation);
		attenuation = 4;
	}

	if (channel < 0)
	{
		Con_DPrintf ("SV_StartSound: channel = %i\n", channel);
		channel = 0;
	}

	if (channel > 7)
	{
		Con_DPrintf ("SV_StartSound: channel = %i\n", channel);
		channel = 7;
	}

	if (sv.datagram.cursize > MAX_DATAGRAM2-16)
		return;

	// find precache number for sound
    for (sound_num=1 ; sound_num < MAX_SOUNDS && sv.sound_precache[sound_num] ; sound_num++)
        if (!strcmp(sample, sv.sound_precache[sound_num]))
            break;

    if ( sound_num == MAX_SOUNDS || !sv.sound_precache[sound_num] )
    {
        Con_Printf ("SV_StartSound: %s not precached\n", sample);
        return;
    }
    
	ent = NUM_FOR_EDICT(entity);

	channel = (ent<<3) | channel;

	field_mask = 0;
	if (volume != DEFAULT_SOUND_PACKET_VOLUME)
		field_mask |= SND_VOLUME;
	if (attenuation != DEFAULT_SOUND_PACKET_ATTENUATION)
		field_mask |= SND_ATTENUATION;

	// directed messages go only to the entity the are targeted on
	MSG_WriteByte (&sv.datagram, svc_sound);
	MSG_WriteByte (&sv.datagram, field_mask);
	if (field_mask & SND_VOLUME)
		MSG_WriteByte (&sv.datagram, volume);
	if (field_mask & SND_ATTENUATION)
		MSG_WriteByte (&sv.datagram, attenuation*64);
	MSG_WriteShort (&sv.datagram, channel);
	SV_WriteByteShort2 (&sv.datagram, sound_num, false);
	for (i=0 ; i<3 ; i++)
		MSG_WriteCoord (&sv.datagram, entity->v.origin[i]+0.5*(entity->v.mins[i]+entity->v.maxs[i]));
}


/*
==============================================================================

CLIENT SPAWNING

==============================================================================
*/

/*
================
SV_SendServerinfo

Sends the first message from the server to a connected client.
This will be sent on the initial connection and upon each server load.
================
*/
char *Damage2Hack = "items/damage2.wav";

void SV_SendServerinfo (client_t *client)
{
	char			**s;
	char			message[2048];

	MSG_WriteByte (&client->message, svc_print);
	_snprintf (message, 2048, "%c\nVERSION %1.2f SERVER (%i CRC)", 2, VERSION, pr_crc);
	MSG_WriteString (&client->message,message);

	MSG_WriteByte (&client->message, svc_serverinfo);
	MSG_WriteLong (&client->message, sv.Protocol);
	MSG_WriteByte (&client->message, svs.maxclients);

	if (!coop.value && deathmatch.value)
		MSG_WriteByte (&client->message, GAME_DEATHMATCH);
	else
		MSG_WriteByte (&client->message, GAME_COOP);

	_snprintf (message, 2048, pr_strings + sv.edicts->v.message);

	MSG_WriteString (&client->message, message);

	for (s = sv.model_precache+1 ; *s ; s++)
		MSG_WriteString (&client->message, *s);
	MSG_WriteByte (&client->message, 0);

	// this is a hack for the "items/damage2.wav is not precached" bug;
	// happens on maps where you do impulse 255 but the map doesn't have a quad
	for (int i = 1; i < MAX_SOUNDS; i++)
	{
		if (!sv.sound_precache[i])
		{
			// Con_Printf ("Forcing precache of %s\n", Damage2Hack);
			sv.sound_precache[i] = Damage2Hack;
			break;
		}

		if (!stricmp (sv.sound_precache[i], Damage2Hack)) break;
	}

	for (s = sv.sound_precache+1 ; *s ; s++)
		MSG_WriteString (&client->message, *s);
	MSG_WriteByte (&client->message, 0);

// send music
	MSG_WriteByte (&client->message, svc_cdtrack);
	MSG_WriteByte (&client->message, sv.edicts->v.sounds);
	MSG_WriteByte (&client->message, sv.edicts->v.sounds);

// set view	
	MSG_WriteByte (&client->message, svc_setview);
	MSG_WriteShort (&client->message, NUM_FOR_EDICT(client->edict));

	MSG_WriteByte (&client->message, svc_signonnum);
	MSG_WriteByte (&client->message, 1);

	client->sendsignon = true;
	client->spawned = false;		// need prespawn, spawn, etc
}

/*
================
SV_ConnectClient

Initializes a client_t for a new net connection.  This will only be called
once for a player each game, not once for each level change.
================
*/
void SV_ConnectClient (int clientnum)
{
	edict_t			*ent;
	client_t		*client;
	int				edictnum;
	struct qsocket_s *netconnection;
	int				i;
	float			spawn_parms[NUM_SPAWN_PARMS];

	client = svs.clients + clientnum;

	Con_DPrintf ("Client %s connected\n", client->netconnection->address);

	edictnum = clientnum+1;

	ent = EDICT_NUM(edictnum);
	
// set up the client_t
	netconnection = client->netconnection;
	
	if (sv.loadgame)
		memcpy (spawn_parms, client->spawn_parms, sizeof(spawn_parms));
	memset (client, 0, sizeof(*client));
	client->netconnection = netconnection;

	strcpy (client->name, "unconnected");
	client->active = true;
	client->spawned = false;
	client->edict = ent;
	client->message.data = client->msgbuf;
	client->message.maxsize = sizeof(client->msgbuf);
	client->message.allowoverflow = true;		// we can catch it

#ifdef IDGODS
	client->privileged = IsID(&client->netconnection->addr);
#else	
	client->privileged = false;				
#endif

	if (sv.loadgame)
		memcpy (client->spawn_parms, spawn_parms, sizeof(spawn_parms));
	else
	{
	// call the progs to get default spawn parms for the new client
		PR_ExecuteProgram (pr_global_struct->SetNewParms);
		for (i=0 ; i<NUM_SPAWN_PARMS ; i++)
			client->spawn_parms[i] = (&pr_global_struct->parm1)[i];
	}

	SV_SendServerinfo (client);
}


/*
===================
SV_CheckForNewClients

===================
*/
void SV_CheckForNewClients (void)
{
	struct qsocket_s	*ret;
	int				i;
		
//
// check for new connections
//
	while (1)
	{
		ret = NET_CheckNewConnections ();
		if (!ret)
			break;

		// init a new client structure
		for (i=0 ; i<svs.maxclients ; i++)
			if (!svs.clients[i].active)
				break;
		if (i == svs.maxclients)
			Host_Error ("Host_CheckForNewClients: no free clients");

		svs.clients[i].netconnection = ret;
		SV_ConnectClient (i);	
	
		net_activeconnections++;
	}
}



/*
===============================================================================

FRAME UPDATES

===============================================================================
*/

/*
==================
SV_ClearDatagram

==================
*/
void SV_ClearDatagram (void)
{
	SZ_Clear (&sv.datagram);
}

/*
=============================================================================

The PVS must include a small area around the client to allow head bobbing
or other small motion on the client side.  Otherwise, a bob might cause an
entity that should be visible to not show up, especially when the bob
crosses a waterline.

=============================================================================
*/

int		fatbytes;
byte	*fatpvs = NULL;

// control how fat the fatpvs is
cvar_t sv_pvsfat ("sv_pvsfat", "8", CVAR_ARCHIVE | CVAR_SERVER);

void SV_AddToFatPVS (vec3_t org, mnode_t *node)
{
	int		i;
	byte	*pvs;
	mplane_t	*plane;
	float	d;

	while (1)
	{
		// if this is a leaf, accumulate the pvs bits
		if (node->contents < 0)
		{
			if (node->contents != CONTENTS_SOLID)
			{
				pvs = Mod_LeafPVS ((mleaf_t *) node, sv.worldmodel);
				for (i = 0; i < fatbytes ; i++) fatpvs[i] |= pvs[i];
			}

			return;
		}

		plane = node->plane;

		d = DotProduct (org, plane->normal) - plane->dist;

		if (d > sv_pvsfat.integer)
			node = node->children[0];
		else if (d < -sv_pvsfat.integer)
			node = node->children[1];
		else
		{
			// go down both
			SV_AddToFatPVS (org, node->children[0]);
			node = node->children[1];
		}
	}
}


/*
=============
SV_FatPVS

Calculates a PVS that is the inclusive or of all leafs within 8 pixels of the
given point.
=============
*/
byte *SV_FatPVS (vec3_t org)
{
	fatbytes = (sv.worldmodel->bh->numleafs + 31) >> 3;

	if (!fatpvs) fatpvs = (byte *) Pool_Alloc (POOL_MAP, fatbytes);

	memset (fatpvs, 0, fatbytes);
	SV_AddToFatPVS (org, sv.worldmodel->bh->nodes);

	return fatpvs;
}

//=============================================================================


#define offsetrandom(MIN,MAX) ((rand () & 32767) * (((MAX)-(MIN)) * (1.0f / 32767.0f)) + (MIN))
trace_t SV_ClipMoveToEntity (edict_t *ent, vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end);

bool SV_VisibleToClient (edict_t *viewer, edict_t *seen)
{
	// allow anti-wallhack to be switched off
	if (!sv_antiwallhack.integer) return true;

	// dont cull doors and plats :(
	if (seen->v.movetype == MOVETYPE_PUSH) return true;

    trace_t	tr;

	vec3_t start =
	{
		viewer->v.origin[0],
		viewer->v.origin[1],
		viewer->v.origin[2] + viewer->v.view_ofs[2]
	};

    //aim straight at the center of "seen" from our eyes
	vec3_t end =
	{
		0.5f * (seen->v.mins[0] + seen->v.maxs[0]),
		0.5f * (seen->v.mins[1] + seen->v.maxs[1]),
		0.5f * (seen->v.mins[2] + seen->v.maxs[2])
	};

	memset (&tr, 0, sizeof (trace_t));
	tr.fraction = 1;

	// no need to do a direct line of sight test here
	// 4 traces seems to be sufficient; most entities will be hit after 1 or 2
	for (int i = 0; i < 4; i++)
	{
		end[0] = seen->v.origin[0] + offsetrandom (seen->v.mins[0], seen->v.maxs[0]);
		end[1] = seen->v.origin[1] + offsetrandom (seen->v.mins[1], seen->v.maxs[1]);
		end[2] = seen->v.origin[2] + offsetrandom (seen->v.mins[2], seen->v.maxs[2]);

		tr = SV_ClipMoveToEntity (sv.edicts, start, vec3_origin, vec3_origin, end);

		if (tr.fraction == 1)
		{
			seen->tracetimer = sv.time + 0.2f;
			break;
		}
	}

	return (seen->tracetimer > sv.time);
}


/*
===============
SV_FindTouchedLeafs

moved to here, deferred find until we hit actual sending to client, and
added test vs client pvs in finding.

note - if directq is being used as a server, this may increase the server processing
===============
*/
void SV_FindTouchedLeafs (edict_t *ent, mnode_t *node, byte *pvs)
{
	mplane_t	*splitplane;
	int			sides;
	int			leafnum;

loc0:;
	// ent already touches a leaf
	if (ent->touchleaf) return;

	// hit solid
	if (node->contents == CONTENTS_SOLID) return;

	// add an efrag if the node is a leaf
	// this is used for sending ents to the client so it needs to stay
	if (node->contents < 0)
	{
loc1:;
		leafnum = ((mleaf_t *) node) - sv.worldmodel->bh->leafs - 1;

		if (pvs[leafnum >> 3] & (1 << (leafnum & 7)))
			ent->touchleaf = true;

		return;
	}

	// NODE_MIXED
	splitplane = node->plane;
	sides = BOX_ON_PLANE_SIDE (ent->v.absmin, ent->v.absmax, splitplane);

	// recurse down the contacted sides, start dropping out if we hit anything
	if ((sides & 1) && !ent->touchleaf && node->children[0]->contents != CONTENTS_SOLID)
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
		else SV_FindTouchedLeafs (ent, node->children[0], pvs);
	}

	if ((sides & 2) && !ent->touchleaf && node->children[1]->contents != CONTENTS_SOLID)
	{
		// test for a leaf and drop out if so, otherwise it's a node so go round again
		node = node->children[1];

		if (node->contents < 0)
			goto loc1;
		else goto loc0;	// SV_FindTouchedLeafs (ent, node, pvs);
	}
}


/*
=============
SV_WriteEntitiesToClient

=============
*/
void SV_WriteEntitiesToClient (edict_t *clent, sizebuf_t *msg)
{
	int		e, i;
	int		bits;
	byte	*pvs;
	vec3_t	org;
	float	miss, alpha, fullbright;
	edict_t	*ent;

	// find the client's PVS
	VectorAdd (clent->v.origin, clent->v.view_ofs, org);
	pvs = SV_FatPVS (org);

	// send over all entities (excpet the client) that touch the pvs
	ent = NEXT_EDICT (sv.edicts);

	int NumCulledEnts = 0;

	for (e = 1; e < sv.num_edicts; e++, ent = NEXT_EDICT (ent))
	{
		// ignore if not touching a PV leaf
		if (ent != clent)	// clent is ALLWAYS sent
		{
			// ignore ents without visible models
			if (!ent->v.modelindex || !pr_strings[ent->v.model]) continue;

			// link to PVS leafs - deferred to here so that we can compare leafs that are touched to the PVS.
			// this is less optimal on one hand as it now needs to be done separately for each client, rather than once
			// only (covering all clients), but more optimal on the other as it only needs to hit one leaf and will
			// start dropping out of the recursion as soon as it does so.  on balance it should be more optimal overall.
			ent->touchleaf = false;
			SV_FindTouchedLeafs (ent, sv.worldmodel->bh->nodes, pvs);

			// if the entity didn't touch any leafs in the pvs don't send it to the client
			if (!ent->touchleaf)
			{
				//NumCulledEnts++;
				continue;
			}

			// server side occlusion check
			// NOTE - this gives wallhack protection too, but this engine is not really intended for use as a server.
			// Problems with it: it doesn't check against brush models, and it doesn't check static entities.
			// this is included primarily because it seems the correct thing to do. :)
			if (!SV_VisibleToClient (clent, ent))
			{
				NumCulledEnts++;
				continue;
			}
		}

		// send an update
		// all updates are stored back to the baseline so that only actual changes from the previous
		// transmission (rather than from when spawned) are sent - helps relieve packet overflows!
		bits = 0;

		// only transmit origin if changed from the baseline
		if ((int) ent->v.origin[0] != (int) ent->baseline.origin[0]) bits |= U_ORIGIN1;
		if ((int) ent->v.origin[1] != (int) ent->baseline.origin[1]) bits |= U_ORIGIN2;
		if ((int) ent->v.origin[2] != (int) ent->baseline.origin[2]) bits |= U_ORIGIN3;

		// only transmit angles if changed from the baseline
		if ((int) ent->v.angles[0] != (int) ent->baseline.angles[0]) bits |= U_ANGLE1;
		if ((int) ent->v.angles[1] != (int) ent->baseline.angles[1]) bits |= U_ANGLE2;
		if ((int) ent->v.angles[2] != (int) ent->baseline.angles[2]) bits |= U_ANGLE3;

		// check everything else
		if (ent->v.movetype == MOVETYPE_STEP) bits |= U_NOLERP;
		if (ent->baseline.colormap != ent->v.colormap) bits |= U_COLORMAP;
		if (ent->baseline.skin != ent->v.skin) bits |= U_SKIN;
		if (ent->baseline.frame != ent->v.frame) bits |= U_FRAME;
		if (ent->baseline.effects != ent->v.effects) bits |= U_EFFECTS;
		if (ent->baseline.modelindex != ent->v.modelindex) bits |= U_MODEL;

		// Nehahra: Model Alpha
		eval_t  *val;

		if (val = GETEDICTFIELDVALUEFAST (ent, ed_alpha))
			alpha = val->_float;
		else alpha = 1;

		if (val = GETEDICTFIELDVALUEFAST (ent, ed_fullbright))
			fullbright = val->_float;
		else fullbright = 0;

		// only send if not protocol 15 - note - FUCKING nehahra uses protocol 15 but sends non-standard messages - FUCK FUCK FUCK
		if (((alpha < 1) && (alpha > 0) || fullbright) && (sv.Protocol != PROTOCOL_VERSION || nehahra)) bits |= U_TRANS;

		if (e >= 256) bits |= U_LONGENTITY;
		if (bits >= 256) bits |= U_MOREBITS;

		// original + missing for worst case
		int packetsize = 16 + 2;

		if (bits & U_TRANS) packetsize += 12;
		if (sv.Protocol != PROTOCOL_VERSION) ++packetsize;
		if (sv_max_datagram == MAX_DATAGRAM) packetsize += 256;

		if (msg->maxsize - msg->cursize < packetsize)
		{
			Con_Printf ("packet overflow\n");
			return;
		}

		// write the message
		MSG_WriteByte (msg, bits | U_SIGNAL);

		if (bits & U_MOREBITS) MSG_WriteByte (msg, bits >> 8);

		if (bits & U_LONGENTITY)
			MSG_WriteShort (msg, e);
		else MSG_WriteByte (msg, e);

		if (bits & U_MODEL) SV_WriteByteShort (msg, ent->v.modelindex);
		if (bits & U_FRAME) MSG_WriteByte (msg, ent->v.frame);
		if (bits & U_COLORMAP) MSG_WriteByte (msg, ent->v.colormap);
		if (bits & U_SKIN) MSG_WriteByte (msg, ent->v.skin);
		if (bits & U_EFFECTS) MSG_WriteByte (msg, ent->v.effects);
		if (bits & U_ORIGIN1) MSG_WriteCoord (msg, ent->v.origin[0]);	
		if (bits & U_ANGLE1) MSG_WriteAngle (msg, ent->v.angles[0]);
		if (bits & U_ORIGIN2) MSG_WriteCoord (msg, ent->v.origin[1]);
		if (bits & U_ANGLE2) MSG_WriteAngle (msg, ent->v.angles[1]);
		if (bits & U_ORIGIN3) MSG_WriteCoord (msg, ent->v.origin[2]);
		if (bits & U_ANGLE3) MSG_WriteAngle (msg, ent->v.angles[2]);

		if ((bits & U_TRANS) && (sv.Protocol != PROTOCOL_VERSION))
		{
			// Nehahra/.alpha
			MSG_WriteFloat (msg, 2);
			MSG_WriteFloat (msg, alpha);
			MSG_WriteFloat (msg, fullbright);
		}

		// copy origin and angles back to baseline
		if (sv.Protocol >= PROTOCOL_VERSION_MH)
		{
			// update baseline
			VectorCopy (ent->v.origin, ent->baseline.origin);
			VectorCopy (ent->v.angles, ent->baseline.angles);
		}
	}

	// if (NumCulledEnts) Con_Printf ("Culled %i entities\n", NumCulledEnts);
}


/*
=============
SV_CleanupEnts

=============
*/
void SV_CleanupEnts (void)
{
	int		e;
	edict_t	*ent;
	
	ent = NEXT_EDICT(sv.edicts);
	for (e=1 ; e<sv.num_edicts ; e++, ent = NEXT_EDICT(ent))
	{
		ent->v.effects = (int)ent->v.effects & ~EF_MUZZLEFLASH;
	}
}


/*
==================
SV_WriteClientdataToMessage

==================
*/
void SV_WriteClientdataToMessage (edict_t *ent, sizebuf_t *msg)
{
	int		bits;
	int		i;
	edict_t	*other;
	int		items;
#ifndef QUAKE2
	eval_t	*val;
#endif

//
// send a damage message
//
	if (ent->v.dmg_take || ent->v.dmg_save)
	{
		other = PROG_TO_EDICT(ent->v.dmg_inflictor);
		MSG_WriteByte (msg, svc_damage);
		MSG_WriteByte (msg, ent->v.dmg_save);
		MSG_WriteByte (msg, ent->v.dmg_take);
		for (i=0 ; i<3 ; i++)
			MSG_WriteCoord (msg, other->v.origin[i] + 0.5*(other->v.mins[i] + other->v.maxs[i]));
	
		ent->v.dmg_take = 0;
		ent->v.dmg_save = 0;
	}

//
// send the current viewpos offset from the view entity
//
	SV_SetIdealPitch ();		// how much to look up / down ideally

// a fixangle might get lost in a dropped packet.  Oh well.
	if ( ent->v.fixangle )
	{
		MSG_WriteByte (msg, svc_setangle);
		for (i=0 ; i < 3 ; i++)
			MSG_WriteAngle (msg, ent->v.angles[i] );
		ent->v.fixangle = 0;
	}

	bits = 0;
	
	if (ent->v.view_ofs[2] != DEFAULT_VIEWHEIGHT)
		bits |= SU_VIEWHEIGHT;
		
	if (ent->v.idealpitch)
		bits |= SU_IDEALPITCH;

// stuff the sigil bits into the high bits of items for sbar, or else
// mix in items2
	val = GETEDICTFIELDVALUEFAST (ent, ed_items2);

	if (val)
		items = (int)ent->v.items | ((int)val->_float << 23);
	else
		items = (int)ent->v.items | ((int)pr_global_struct->serverflags << 28);

	bits |= SU_ITEMS;
	
	if ( (int)ent->v.flags & FL_ONGROUND)
		bits |= SU_ONGROUND;

	if ( ent->v.waterlevel >= 2)
		bits |= SU_INWATER;
	
	for (i=0 ; i<3 ; i++)
	{
		if (ent->v.punchangle[i])
			bits |= (SU_PUNCH1<<i);
		if (ent->v.velocity[i])
			bits |= (SU_VELOCITY1<<i);
	}
	
	if (ent->v.weaponframe)
		bits |= SU_WEAPONFRAME;

	if (ent->v.armorvalue)
		bits |= SU_ARMOR;

//	if (ent->v.weapon)
		bits |= SU_WEAPON;

// send the data

	MSG_WriteByte (msg, svc_clientdata);
	MSG_WriteShort (msg, bits);

	if (bits & SU_VIEWHEIGHT)
		MSG_WriteChar (msg, ent->v.view_ofs[2]);

	if (bits & SU_IDEALPITCH)
		MSG_WriteChar (msg, ent->v.idealpitch);

	for (i=0 ; i<3 ; i++)
	{
		if (bits & (SU_PUNCH1<<i))
			MSG_WriteChar (msg, ent->v.punchangle[i]);
		if (bits & (SU_VELOCITY1<<i))
			MSG_WriteChar (msg, ent->v.velocity[i]/16);
	}

// [always sent]	if (bits & SU_ITEMS)
	MSG_WriteLong (msg, items);

	if (bits & SU_WEAPONFRAME)
		MSG_WriteByte (msg, ent->v.weaponframe);
	if (bits & SU_ARMOR)
		MSG_WriteByte (msg, ent->v.armorvalue);
	if (bits & SU_WEAPON)
		SV_WriteByteShort (msg, SV_ModelIndex(pr_strings+ent->v.weaponmodel));
	
	MSG_WriteShort (msg, ent->v.health);
	MSG_WriteByte (msg, ent->v.currentammo);
	MSG_WriteByte (msg, ent->v.ammo_shells);
	MSG_WriteByte (msg, ent->v.ammo_nails);
	MSG_WriteByte (msg, ent->v.ammo_rockets);
	MSG_WriteByte (msg, ent->v.ammo_cells);

	if (standard_quake)
	{
		MSG_WriteByte (msg, ent->v.weapon);
	}
	else
	{
		for(i=0;i<32;i++)
		{
			if ( ((int)ent->v.weapon) & (1<<i) )
			{
				MSG_WriteByte (msg, i);
				break;
			}
		}
	}
}

/*
=======================
SV_SendClientDatagram
=======================
*/
bool SV_SendClientDatagram (client_t *client)
{
	byte		buf[MAX_DATAGRAM];
	sizebuf_t	msg;

	msg.data = buf;
	msg.maxsize = MAX_DATAGRAM2;
	msg.cursize = 0;

	MSG_WriteByte (&msg, svc_time);
	MSG_WriteFloat (&msg, sv.time);

	// add the client specific data to the datagram
	SV_WriteClientdataToMessage (client->edict, &msg);

	SV_WriteEntitiesToClient (client->edict, &msg);

	// copy the server datagram if there is space
	if (msg.cursize + sv.datagram.cursize < msg.maxsize)
		SZ_Write (&msg, sv.datagram.data, sv.datagram.cursize);

	// send the datagram
	if (NET_SendUnreliableMessage (client->netconnection, &msg) == -1)
	{
		SV_DropClient (true);// if the message couldn't send, kick off
		return false;
	}
	
	return true;
}

/*
=======================
SV_UpdateToReliableMessages
=======================
*/
void SV_UpdateToReliableMessages (void)
{
	int			i, j;
	client_t *client;

	// check for changes to be sent over the reliable streams
	for (i=0, host_client = svs.clients ; i<svs.maxclients ; i++, host_client++)
	{
		if (host_client->old_frags != host_client->edict->v.frags)
		{
			for (j=0, client = svs.clients ; j<svs.maxclients ; j++, client++)
			{
				if (!client->active)
					continue;
				MSG_WriteByte (&client->message, svc_updatefrags);
				MSG_WriteByte (&client->message, i);
				MSG_WriteShort (&client->message, host_client->edict->v.frags);
			}

			host_client->old_frags = host_client->edict->v.frags;
		}
	}

	for (j=0, client = svs.clients ; j<svs.maxclients ; j++, client++)
	{
		if (!client->active)
			continue;
		SZ_Write (&client->message, sv.reliable_datagram.data, sv.reliable_datagram.cursize);
	}

	SZ_Clear (&sv.reliable_datagram);
}


/*
=======================
SV_SendNop

Send a nop message without trashing or sending the accumulated client
message buffer
=======================
*/
void SV_SendNop (client_t *client)
{
	sizebuf_t	msg;
	byte		buf[4];
	
	msg.data = buf;
	msg.maxsize = sizeof(buf);
	msg.cursize = 0;

	MSG_WriteChar (&msg, svc_nop);

	if (NET_SendUnreliableMessage (client->netconnection, &msg) == -1)
		SV_DropClient (true);	// if the message couldn't send, kick off
	client->last_message = realtime;
}

/*
=======================
SV_SendClientMessages
=======================
*/
void SV_SendClientMessages (void)
{
	int			i;

	// update frags, names, etc
	SV_UpdateToReliableMessages ();

	// build individual updates
	for (i=0, host_client = svs.clients ; i<svs.maxclients ; i++, host_client++)
	{
		if (!host_client->active)
			continue;

		if (host_client->spawned)
		{
			if (!SV_SendClientDatagram (host_client))
				continue;
		}
		else
		{
			// the player isn't totally in the game yet
			// send small keepalive messages if too much time has passed
			// send a full message when the next signon stage has been requested
			// some other message data (name changes, etc) may accumulate 
			// between signon stages
			if (!host_client->sendsignon)
			{
				if (realtime - host_client->last_message > 5)
					SV_SendNop (host_client);
				continue;	// don't send out non-signon messages
			}
		}

		// check for an overflowed message.  Should only happen
		// on a very fucked up connection that backs up a lot, then
		// changes level
		if (host_client->message.overflowed)
		{
			SV_DropClient (true);
			host_client->message.overflowed = false;
			continue;
		}

		if (host_client->message.cursize || host_client->dropasap)
		{
			if (!NET_CanSendMessage (host_client->netconnection))
			{
//				I_Printf ("can't write\n");
				continue;
			}

			if (host_client->dropasap)
				SV_DropClient (false);	// went to another level
			else
			{
				if (NET_SendMessage (host_client->netconnection, &host_client->message) == -1)
					SV_DropClient (true);	// if the message couldn't send, kick off
				SZ_Clear (&host_client->message);
				host_client->last_message = realtime;
				host_client->sendsignon = false;
			}
		}
	}

	// clear muzzle flashes
	SV_CleanupEnts ();
}


/*
==============================================================================

SERVER SPAWNING

==============================================================================
*/

/*
================
SV_ModelIndex

================
*/
int SV_ModelIndex (char *name)
{
	int		i;

	if (!name || !name[0])
		return 0;

	for (i = 0; i < MAX_MODELS && sv.model_precache[i]; i++)
		if (!strcmp(sv.model_precache[i], name))
			return i;

	// fixme - precache it if not already precached!!!
	if (i == MAX_MODELS || !sv.model_precache[i])
		Host_Error ("SV_ModelIndex: model %s not precached", name);

	return i;
}


/*
================
SV_CreateBaseline

================
*/
void SV_CreateBaseline (void)
{
	int			i;
	edict_t			*svent;
	int				entnum;	

	for (entnum = 0; entnum < sv.num_edicts; entnum++)
	{
		// get the current server version
		svent = EDICT_NUM(entnum);

		if (svent->free) continue;
		if (entnum > svs.maxclients && !svent->v.modelindex) continue;

		// create entity baseline
		VectorCopy (svent->v.origin, svent->baseline.origin);
		VectorCopy (svent->v.angles, svent->baseline.angles);
		svent->baseline.frame = svent->v.frame;
		svent->baseline.skin = svent->v.skin;

		if (entnum > 0 && entnum <= svs.maxclients)
		{
			svent->baseline.colormap = entnum;
			svent->baseline.modelindex = SV_ModelIndex ("progs/player.mdl");
		}
		else
		{
			svent->baseline.colormap = 0;
			svent->baseline.modelindex = SV_ModelIndex (pr_strings + svent->v.model);
		}

		// add to the message
		MSG_WriteByte (&sv.signon,svc_spawnbaseline);		
		MSG_WriteShort (&sv.signon,entnum);

		SV_WriteByteShort (&sv.signon, svent->baseline.modelindex);
		MSG_WriteByte (&sv.signon, svent->baseline.frame);
		MSG_WriteByte (&sv.signon, svent->baseline.colormap);
		MSG_WriteByte (&sv.signon, svent->baseline.skin);

		for (i=0 ; i<3 ; i++)
		{
			MSG_WriteCoord(&sv.signon, svent->baseline.origin[i]);
			MSG_WriteAngle(&sv.signon, svent->baseline.angles[i]);
		}
	}
	// Check normal signon size (8192) and normal MAX_MSGLEN-2 (8000-2),
	// the latter limit being lower and occurring in Host_PreSpawn_f
	if (sv.signon.cursize - sv.signondiff > 8000 - 2)
	{
		Con_DPrintf ("SV_CreateBaseline: ");
		Con_DPrintf ("excessive signon buffer size (%d, normal max = %d)\n", sv.signon.cursize - sv.signondiff, 8000 - 2);
	}
}


/*
================
SV_SendReconnect

Tell all the clients that the server is changing levels
================
*/
void SV_SendReconnect (void)
{
	char	data[128];
	sizebuf_t	msg;

	msg.data = (byte *) data;
	msg.cursize = 0;
	msg.maxsize = sizeof (data);

	MSG_WriteChar (&msg, svc_stufftext);
	MSG_WriteString (&msg, "reconnect\n");
	NET_SendToAll (&msg, 5);

	Cmd_ExecuteString ("reconnect\n", src_command);
}


/*
================
SV_SaveSpawnparms

Grabs the current state of each client for saving across the
transition to another level
================
*/
void SV_SaveSpawnparms (void)
{
	int		i, j;

	svs.serverflags = pr_global_struct->serverflags;

	for (i=0, host_client = svs.clients ; i<svs.maxclients ; i++, host_client++)
	{
		if (!host_client->active)
			continue;

	// call the progs to get default spawn parms for the new client
		pr_global_struct->self = EDICT_TO_PROG(host_client->edict);
		PR_ExecuteProgram (pr_global_struct->SetChangeParms);
		for (j=0 ; j<NUM_SPAWN_PARMS ; j++)
			host_client->spawn_parms[j] = (&pr_global_struct->parm1)[j];
	}
}


/*
================
SV_SpawnServer

This is called at the start of each level
================
*/
extern float		scr_centertime_off;

void SV_SpawnServer (char *server)
{
	edict_t		*ent;
	int			i;

	// let's not have any servers with no name
	if (hostname.string[0] == 0) Cvar_Set ("hostname", "UNNAMED");

	scr_centertime_off = 0;

	Con_DPrintf ("SpawnServer: %s\n", server);
	svs.changelevel_issued = false;		// now safe to issue another

	// tell all connected clients that we are going to a new level
	if (sv.active) SV_SendReconnect ();

	// make cvars consistant
	if (coop.value) Cvar_Set ("deathmatch", 0.0f);
	current_skill = (int) (skill.value + 0.5);
	if (current_skill < 0) current_skill = 0;
	if (current_skill > 3) current_skill = 3;

	Cvar_Set ("skill", (float) current_skill);
	
	// set up the new server
	Host_ClearMemory ();

	memset (&sv, 0, sizeof (sv));

	strcpy (sv.name, server);

	// load progs to get entity field count
	PR_LoadProgs ();

	// bring up the worldmodel early so that we can get a count on the number of edicts needed
	_snprintf (sv.modelname, 64, "maps/%s.bsp", server);
	sv.worldmodel = Mod_ForName (sv.modelname, false);

	if (!sv.worldmodel)
	{
		Con_Printf ("Couldn't spawn server %s\n", sv.modelname);
		sv.active = false;
		return;
	}

	// clear the fat pvs
	fatpvs = NULL;

	// reserve memory for edicts but don't commit it yet
	// reset the memory pool for edicts
	Pool_Reset (POOL_EDICTS, MAX_EDICTS * pr_edict_size);
	sv.num_edicts = 0;
	sv.max_edicts = 0;

	// alloc an initial batch of 128 edicts
	sv.edicts = SV_AllocEdicts (128);

	sv.Protocol = sv_protocol;
	sv_max_datagram = (sv.Protocol == PROTOCOL_VERSION ? 1024 : MAX_DATAGRAM); // Limit packet size if old protocol

	sv.datagram.maxsize = MAX_DATAGRAM2;
	sv.datagram.cursize = 0;
	sv.datagram.data = sv.datagram_buf;

	sv.reliable_datagram.maxsize = MAX_DATAGRAM2;
	sv.reliable_datagram.cursize = 0;
	sv.reliable_datagram.data = sv.reliable_datagram_buf;

	sv.signon.maxsize = sizeof(sv.signon_buf);
	sv.signon.cursize = sv.signondiff = 0;
	sv.signon.data = sv.signon_buf;

	// leave slots at start for clients only
	sv.num_edicts = svs.maxclients + 1;

	for (i = 0; i < svs.maxclients; i++)
	{
		ent = EDICT_NUM (i + 1);
		svs.clients[i].edict = ent;
	}

	sv.state = ss_loading;
	sv.paused = false;

	sv.time = 1.0;

	sv.models[1] = sv.worldmodel;

	// clear world interaction links
	SV_ClearWorld ();

	sv.sound_precache[0] = pr_strings;

	sv.model_precache[0] = pr_strings;
	sv.model_precache[1] = sv.modelname;

	for (i = 1; i < sv.worldmodel->bh->numsubmodels; i++)
	{
		// prevent crash
		if (i >= MAX_MODELS) break;

		// old limit (this will be trunc'ed by WriteByteShort anyway...)
		if (sv.Protocol == PROTOCOL_VERSION && i > 255) break;

		sv.model_precache[1 + i] = localmodels[i];
		sv.models[i + 1] = Mod_ForName (localmodels[i], false);
	}

	// load the rest of the entities
	ent = EDICT_NUM (0);
	memset (&ent->v, 0, qcprogs.entityfields * 4);
	ent->free = false;
	ent->v.model = sv.worldmodel->name - pr_strings;
	ent->v.modelindex = 1;		// world model
	ent->v.solid = SOLID_BSP;
	ent->v.movetype = MOVETYPE_PUSH;

	if (coop.value)
		pr_global_struct->coop = coop.value;
	else
		pr_global_struct->deathmatch = deathmatch.value;

	pr_global_struct->mapname = sv.name - pr_strings;

	// serverflags are for cross level information (sigils)
	pr_global_struct->serverflags = svs.serverflags;

	ED_LoadFromFile (sv.worldmodel->bh->entities);

	sv.active = true;

	// all setup is completed, any further precache statements are errors
	sv.state = ss_active;

	// run two frames to allow everything to settle
	host_frametime = 0.1;
	SV_Physics ();
	SV_Physics ();

	// create a baseline for more efficient communications
	SV_CreateBaseline ();

	// send serverinfo to all connected clients
	for (i=0,host_client = svs.clients ; i<svs.maxclients ; i++, host_client++)
		if (host_client->active)
			SV_SendServerinfo (host_client);

	Con_DPrintf ("Allocated %i edicts\n", sv.num_edicts);

	Con_DPrintf ("Server spawned.\n");
}

