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
// sv_main.c -- server main program

#include "quakedef.h"
#include "d3d_model.h"
#include "pr_class.h"

cvar_t	sv_progs ("sv_progs", "progs.dat");

bool spawnserver = false;

int		sv_max_datagram = MAX_DATAGRAM; // Must be set so client will work without server

server_t		sv;
server_static_t	svs;

// inline model names for precache - extra space for an extra digit
char	localmodels[MAX_MODELS][6];


CQuakeHunk *Pool_Edicts = NULL;

void SV_AllocEdicts (int numedicts)
{
#if 0

	// alloc the edicts pool on first use
	if (!Pool_Edicts)
	{
		// init to correct max edict size
		int edictsize = SVProgs->EdictSize * MAX_EDICTS + 0xfffff;
		edictsize /= 0x100000;

		// some day i'll change this to an edict_t ** and be able to get rid of this nonsense...
		Pool_Edicts = new CQuakeHunk (edictsize);
	}

#endif

	// edictpointers MUST be alloced first so that edicts are a linear array...!
	if (!SVProgs->EdictPointers) SVProgs->EdictPointers = (edict_t **) MainHunk->Alloc (MAX_EDICTS * sizeof (edict_t *));

#if 0
	edict_t *edbuf = (edict_t *) Pool_Edicts->Alloc (numedicts * SVProgs->EdictSize);
	edict_t *ed = edbuf;

	if (!SVProgs->Edicts) SVProgs->Edicts = edbuf;

	// link 'em in and set up the numbering for EDICT_TO_PROG and PROG_TO_EDICT
	// can't do ed++ because the size of an edict_t differs.  lovely, isn't it?
	// can't use edictpointers yet because they have not been set up
	for (int i = 0; i < numedicts; i++, ed = ((edict_t *) ((byte *) ed + SVProgs->EdictSize)))
	{
		SVProgs->EdictPointers[SVProgs->MaxEdicts + i] = ed;

		ed->ednum = SVProgs->MaxEdicts + i;
		ed->Prog = ed->ednum * SVProgs->EdictSize;
	}

#else
	// edicts are variable sized
	byte *edbuf = (byte *) MainHunk->Alloc (numedicts * SVProgs->EdictSize);

	// can't use edictpointers yet because they have not been set up
	for (int i = 0; i < numedicts; i++, edbuf += SVProgs->EdictSize)
	{
		edict_t *ed = (edict_t *) edbuf;
		SVProgs->EdictPointers[SVProgs->MaxEdicts + i] = ed;

		ed->ednum = SVProgs->MaxEdicts + i;
		ed->Prog = ed->ednum * SVProgs->EdictSize;

		// this is just test code to ensure that the allocation is valid and that
		// there is no remaining code assuming that the edicts are in consecutive memory
		// MainHunk->Alloc ((rand () & 255) + 1);
	}

#endif

	SVProgs->MaxEdicts += numedicts;

	Con_DPrintf ("%i edicts\n", SVProgs->MaxEdicts);
}


/*
==================
SV_WriteByteShort2
==================
*/
void SV_WriteByteShort2 (sizebuf_t *sb, int c, bool Compatibility)
{
	MSG_WriteByte (sb, c);
}


/*
==================
SV_WriteByteShort
==================
*/
void SV_WriteByteShort (sizebuf_t *sb, int c)
{
	MSG_WriteByte (sb, c);
}


//============================================================================

/*
===============
SV_SetProtocol_f
===============
*/
static int sv_protocol = PROTOCOL_VERSION_FITZ;
extern char *protolist[];

static void SV_SetProtocol_f (void)
{
	if (Cmd_Argc() == 1)
	{
		Con_Printf ("sv_protocol is %d\n", sv_protocol);
		return;
	}

	char *newprotocol = Cmd_Argv (1);

	for (int i = 0;; i++)
	{
		if (!protolist[i]) break;

		if (!stricmp (protolist[i], newprotocol))
		{
			if (!i)
				sv_protocol = 15;
			else if (i == 1)
				sv_protocol = 666;
			else sv_protocol = 999;

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
void SV_Init (void)
{
	for (int i = 0; i < MAX_MODELS; i++) _snprintf (localmodels[i], 6, "*%i", i);

	memset (&sv, 0, sizeof (server_t));
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

	if (sv.datagram.cursize > MAX_DATAGRAM2 - 16)
		return;

	MSG_WriteByte (&sv.datagram, svc_particle);

	MSG_WriteCoord (&sv.datagram, org[0], sv.Protocol, sv.PrototcolFlags);
	MSG_WriteCoord (&sv.datagram, org[1], sv.Protocol, sv.PrototcolFlags);
	MSG_WriteCoord (&sv.datagram, org[2], sv.Protocol, sv.PrototcolFlags);

	for (i = 0; i < 3; i++)
	{
		v = dir[i] * 16;

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

	if (sv.datagram.cursize > MAX_DATAGRAM2 - 16)
		return;

	// find precache number for sound
	for (sound_num = 1; sound_num < MAX_SOUNDS && sv.sound_precache[sound_num]; sound_num++)
		if (!strcmp (sample, sv.sound_precache[sound_num]))
			break;

	if (sound_num == MAX_SOUNDS || !sv.sound_precache[sound_num])
	{
		Con_Printf ("SV_StartSound: %s not precached\n", sample);
		return;
	}

	ent = GetNumberForEdict (entity);

	// note - DirectQ is limited to 8192 entities so don't check for SND_LARGEENTITY
	// we must be able to read it client-side however
	field_mask = 0;

	if (volume != DEFAULT_SOUND_PACKET_VOLUME) field_mask |= SND_VOLUME;
	if (attenuation != DEFAULT_SOUND_PACKET_ATTENUATION) field_mask |= SND_ATTENUATION;

	if (ent >= 8192 && (sv.Protocol == PROTOCOL_VERSION_FITZ || sv.Protocol == PROTOCOL_VERSION_RMQ))
		field_mask |= SND_LARGEENTITY;

	if ((sound_num >= 256 || channel >= 8) && (sv.Protocol == PROTOCOL_VERSION_FITZ || sv.Protocol == PROTOCOL_VERSION_RMQ))
		field_mask |= SND_LARGESOUND;

	// directed messages go only to the entity the are targeted on
	MSG_WriteByte (&sv.datagram, svc_sound);
	MSG_WriteByte (&sv.datagram, field_mask);

	if (field_mask & SND_VOLUME) MSG_WriteByte (&sv.datagram, volume);
	if (field_mask & SND_ATTENUATION) MSG_WriteByte (&sv.datagram, attenuation * 64);

	if (field_mask & SND_LARGEENTITY)
	{
		MSG_WriteShort (&sv.datagram, ent);
		MSG_WriteByte (&sv.datagram, channel);
	}
	else MSG_WriteShort (&sv.datagram, (ent << 3) | channel);

	if (field_mask & SND_LARGESOUND)
		MSG_WriteShort (&sv.datagram, sound_num);
	else SV_WriteByteShort2 (&sv.datagram, sound_num, false);

	for (i = 0; i < 3; i++)
	{
		MSG_WriteCoord
		(
			&sv.datagram,
			entity->v.origin[i] + 0.5 * (entity->v.mins[i] + entity->v.maxs[i]),
			sv.Protocol,
			sv.PrototcolFlags
		);
	}
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
	_snprintf (message, 2048, "%c\nVERSION %1.2f SERVER (%i CRC)", 2, VERSION, SVProgs->CRC);
	MSG_WriteString (&client->message, message);

	MSG_WriteByte (&client->message, svc_serverinfo);
	MSG_WriteLong (&client->message, sv.Protocol);

	if (sv.Protocol == PROTOCOL_VERSION_RMQ)
	{
		// send the flags now so that we know what protocol features to expect
		MSG_WriteLong (&client->message, sv.PrototcolFlags);
	}

	MSG_WriteByte (&client->message, svs.maxclients);

	if (!coop.value && deathmatch.value)
		MSG_WriteByte (&client->message, GAME_DEATHMATCH);
	else
		MSG_WriteByte (&client->message, GAME_COOP);

	_snprintf (message, 2048, SVProgs->Strings + SVProgs->EdictPointers[0]->v.message);

	MSG_WriteString (&client->message, message);

	for (s = sv.model_precache + 1; *s; s++)
		MSG_WriteString (&client->message, *s);

	MSG_WriteByte (&client->message, 0);

	// this is a hack for the "items/damage2.wav is not precached" bug;
	// happens on maps where you do impulse 255 but the map doesn't have a quad
	for (int i = 1; i < MAX_SOUNDS; i++)
	{
		// this is just the first free slot and the items/damage2.wav sound is inserted into it
		if (!sv.sound_precache[i])
		{
			// Con_Printf ("Forcing precache of %s\n", Damage2Hack);
			sv.sound_precache[i] = Damage2Hack;
			break;
		}

		// this catches cases where the sound has already been set up for precaching
		if (!stricmp (sv.sound_precache[i], Damage2Hack)) break;
	}

	for (s = sv.sound_precache + 1; *s; s++)
		MSG_WriteString (&client->message, *s);

	MSG_WriteByte (&client->message, 0);

	// send music
	// (sent twice - eh???)
	MSG_WriteByte (&client->message, svc_cdtrack);
	MSG_WriteByte (&client->message, SVProgs->EdictPointers[0]->v.sounds);
	MSG_WriteByte (&client->message, SVProgs->EdictPointers[0]->v.sounds);

	// set view
	// DP_SV_CLIENTCAMERA
	client->clientcamera = GetNumberForEdict (client->edict);
	MSG_WriteByte (&client->message, svc_setview);
	MSG_WriteShort (&client->message, client->clientcamera);

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
	edictnum = clientnum + 1;
	ent = GetEdictForNumber (edictnum);

	// copy off the netconnection so that it'rs preserved when we wipe the client_t and can be restored afterwards
	netconnection = client->netconnection;

	// copy off the spawn parms so that they're preserved when we wipe the client_t and can be restored afterwards
	if (sv.loadgame) memcpy (spawn_parms, client->spawn_parms, sizeof (float) * NUM_SPAWN_PARMS);

	// safely wipe the client
	Host_SafeWipeClient (client);

	// now we restore the copied off netconnection
	client->netconnection = netconnection;

	// and set up the rest of the client_t as a default unconnected client for now
	strcpy (client->name, "unconnected");
	client->active = true;
	client->spawned = false;
	client->edict = ent;
	client->message.data = client->msgbuf;
	client->message.maxsize = MAX_MSGLEN;	// ha!  sizeof (client->msgbuf) my arse!
	client->message.allowoverflow = true;		// we can catch it
	client->privileged = false;

	if (sv.loadgame)
	{
		// now we restore the copied off spawn parms
		memcpy (client->spawn_parms, spawn_parms, sizeof (float) * NUM_SPAWN_PARMS);
	}
	else
	{
		// call the progs to get default spawn parms for the new client
		SVProgs->ExecuteProgram (SVProgs->GlobalStruct->SetNewParms);

		for (i = 0; i < NUM_SPAWN_PARMS; i++)
			client->spawn_parms[i] = (&SVProgs->GlobalStruct->parm1)[i];
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

	// check for new connections
	while (1)
	{
		ret = NET_CheckNewConnections ();

		if (!ret)
			break;

		// init a new client structure
		for (i = 0; i < svs.maxclients; i++)
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
cvar_t sv_novis ("sv_novis", "0", CVAR_SERVER);

void SV_AddToFatPVS (vec3_t org, mnode_t *node)
{
	while (1)
	{
		// if this is a leaf, accumulate the pvs bits
		if (node->contents < 0)
		{
			if (node->contents != CONTENTS_SOLID)
			{
				byte *pvs = Mod_LeafPVS ((mleaf_t *) node, sv.worldmodel);

				for (int i = 0; i < fatbytes; i++) fatpvs[i] |= pvs[i];
			}

			return;
		}

		float d = SV_PlaneDist (node->plane, org);

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
	fatbytes = (sv.worldmodel->brushhdr->numleafs + 31) >> 3;

	if (!fatpvs) fatpvs = (byte *) MainHunk->Alloc (fatbytes);

	memset (fatpvs, 0, fatbytes);
	SV_AddToFatPVS (org, sv.worldmodel->brushhdr->nodes);

	return fatpvs;
}


/*
=============
SV_WriteEntitiesToClient

=============
*/
void SV_WriteEntitiesToClient (client_t *client, sizebuf_t *msg)
{
	int		e, i;
	int		bits;
	vec3_t	org;
	float	miss, fullbright;
	edict_t	*ent;
	edict_t *clent;
	int alpha;

	// DP_SV_CLIENTCAMERA
	clent = GetEdictForNumber (client->clientcamera);

	// find the client's PVS
	VectorAdd (clent->v.origin, clent->v.view_ofs, org);

	sv_frame--;
	byte *pvs = SV_FatPVS (org);

	// send over all entities (excpet the client) that touch the pvs
	ent = NEXT_EDICT (SVProgs->EdictPointers[0]);

	for (e = 1; e < SVProgs->NumEdicts; e++, ent = NEXT_EDICT (ent))
	{
		// ignore if not touching a PV leaf
		if (ent != clent)	// clent is ALLWAYS sent
		{
			// ignore ents without visible models
			if (!ent->v.modelindex || !SVProgs->Strings[ent->v.model]) continue;

			// ignore if not touching a PV leaf
			for (i = 0; i < ent->num_leafs; i++)
				if (pvs[ent->leafnums[i] >> 3] & (1 << (ent->leafnums[i] & 7)))
					break;

			// not visible
			if (i == ent->num_leafs) continue;
		}

		// send an update
		bits = 0;

		// only transmit origin if changed from the baseline
		if (ent->v.origin[0] != ent->baseline.origin[0]) bits |= U_ORIGIN1;
		if (ent->v.origin[1] != ent->baseline.origin[1]) bits |= U_ORIGIN2;
		if (ent->v.origin[2] != ent->baseline.origin[2]) bits |= U_ORIGIN3;

		// only transmit angles if changed from the baseline
		if (ent->v.angles[0] != ent->baseline.angles[0]) bits |= U_ANGLES1;
		if (ent->v.angles[1] != ent->baseline.angles[1]) bits |= U_ANGLES2;
		if (ent->v.angles[2] != ent->baseline.angles[2]) bits |= U_ANGLES3;

		// check everything else
		if (ent->v.movetype == MOVETYPE_STEP) bits |= U_NOLERP;
		if (ent->baseline.colormap != ent->v.colormap) bits |= U_COLORMAP;
		if (ent->baseline.skin != ent->v.skin) bits |= U_SKIN;
		if (ent->baseline.frame != ent->v.frame) bits |= U_FRAME;
		if (ent->baseline.effects != ent->v.effects) bits |= U_EFFECTS;
		if (ent->baseline.modelindex != ent->v.modelindex) bits |= U_MODEL;

		// assume that alpha is not going to change
		alpha = ent->baseline.alpha;

		// figure out alpha
		if (nehahra && sv.Protocol == PROTOCOL_VERSION_NQ)
		{
		}
		else if (nehahra)
		{
		}
		else if (sv.Protocol != PROTOCOL_VERSION_NQ)
		{
			alpha = ent->alpha;
		}

		// Nehahra: Model Alpha
		eval_t  *val;
		alpha = ent->alpha;

		if (ed_alpha && alpha > 254)
		{
			// if we have an alpha field in the progs we use it
			if (val = GETEDICTFIELDVALUEFAST (ent, ed_alpha))
			{
				float falpha = val->_float * 255.0f;

				alpha = falpha;

				if (alpha < 1) alpha = 255;
				if (alpha > 255) alpha = 255;
			}
			else alpha = 255;
		}

		if (ed_fullbright)
		{
			if (val = GETEDICTFIELDVALUEFAST (ent, ed_fullbright))
				fullbright = val->_float;
			else fullbright = 0;
		}
		else fullbright = 0;

		// only send if not protocol 15 - note - FUCKING nehahra uses protocol 15 but sends non-standard messages - FUCK FUCK FUCK
//		if ((alpha < 255 || fullbright) && (sv.Protocol != PROTOCOL_VERSION_NQ || nehahra)) bits |= U_TRANS;

		if (sv.Protocol == PROTOCOL_VERSION_FITZ || sv.Protocol == PROTOCOL_VERSION_RMQ)
		{
			// certain FQ protocol messages are not yet implemented
			// if (ent->baseline.alpha != alpha) bits |= U_ALPHA;
			if (bits & U_FRAME && (int) ent->v.frame & 0xFF00) bits |= U_FRAME2;
			if (bits & U_MODEL && (int) ent->v.modelindex & 0xFF00) bits |= U_MODEL2;
			if (ent->sendinterval) bits |= U_LERPFINISH;
			if (bits >= 65536) bits |= U_EXTEND1;
			if (bits >= 16777216) bits |= U_EXTEND2;
		}

		if (e >= 256) bits |= U_LONGENTITY;
		if (bits >= 256) bits |= U_MOREBITS;

		// original + missing for worst case
		int packetsize = 16 + 2;

		// if (bits & U_TRANS) packetsize += 12;
		if (sv.Protocol != PROTOCOL_VERSION_NQ) ++packetsize;
		if (sv_max_datagram == MAX_DATAGRAM) packetsize += 256;

		if (msg->maxsize - msg->cursize < packetsize)
		{
			Con_Printf ("packet overflow\n");
			return;
		}

		// write the message
		MSG_WriteByte (msg, bits | U_SIGNAL);

		if (bits & U_MOREBITS) MSG_WriteByte (msg, bits >> 8);
		if (bits & U_EXTEND1) MSG_WriteByte (msg, bits >> 16);
		if (bits & U_EXTEND2) MSG_WriteByte (msg, bits >> 24);

		if (bits & U_LONGENTITY)
			MSG_WriteShort (msg, e);
		else MSG_WriteByte (msg, e);

		if (bits & U_MODEL) SV_WriteByteShort (msg, ent->v.modelindex);
		if (bits & U_FRAME) MSG_WriteByte (msg, ent->v.frame);
		if (bits & U_COLORMAP) MSG_WriteByte (msg, ent->v.colormap);
		if (bits & U_SKIN) MSG_WriteByte (msg, ent->v.skin);
		if (bits & U_EFFECTS) MSG_WriteByte (msg, ent->v.effects);
		if (bits & U_ORIGIN1) MSG_WriteCoord (msg, ent->v.origin[0], sv.Protocol, sv.PrototcolFlags);
		if (bits & U_ANGLES1) MSG_WriteAngle (msg, ent->v.angles[0], sv.Protocol, sv.PrototcolFlags, 0);
		if (bits & U_ORIGIN2) MSG_WriteCoord (msg, ent->v.origin[1], sv.Protocol, sv.PrototcolFlags);
		if (bits & U_ANGLES2) MSG_WriteAngle (msg, ent->v.angles[1], sv.Protocol, sv.PrototcolFlags, 1);
		if (bits & U_ORIGIN3) MSG_WriteCoord (msg, ent->v.origin[2], sv.Protocol, sv.PrototcolFlags);
		if (bits & U_ANGLES3) MSG_WriteAngle (msg, ent->v.angles[2], sv.Protocol, sv.PrototcolFlags, 2);
		if (bits & U_ALPHA) MSG_WriteByte (msg, alpha);
		if (bits & U_FRAME2) MSG_WriteByte (msg, (int) ent->v.frame >> 8);
		if (bits & U_MODEL2) MSG_WriteByte (msg, (int) ent->v.modelindex >> 8);

		if (bits & U_LERPFINISH)
			MSG_WriteByte (msg, (byte) (Q_rint ((ent->v.nextthink - sv.time) * 255)));

		/*
		if ((bits & U_TRANS) && (sv.Protocol != PROTOCOL_VERSION_NQ) && (sv.Protocol != PROTOCOL_VERSION_FITZ) && (sv.Protocol != PROTOCOL_VERSION_RMQ))
		{
			// Nehahra/.alpha
			MSG_WriteFloat (msg, 2);
			MSG_WriteFloat (msg, (float) ent->alpha / 255.0f);
			MSG_WriteFloat (msg, fullbright);
		}
		*/
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

	ent = NEXT_EDICT (SVProgs->EdictPointers[0]);

	for (e = 1; e < SVProgs->NumEdicts; e++, ent = NEXT_EDICT (ent))
	{
		ent->v.effects = (int) ent->v.effects & ~EF_MUZZLEFLASH;
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
	eval_t	*val;

	// send a damage message
	if (ent->v.dmg_take || ent->v.dmg_save)
	{
		other = PROG_TO_EDICT (ent->v.dmg_inflictor);
		MSG_WriteByte (msg, svc_damage);
		MSG_WriteByte (msg, ent->v.dmg_save);
		MSG_WriteByte (msg, ent->v.dmg_take);

		for (i = 0; i < 3; i++)
		{
			MSG_WriteCoord
			(
				msg,
				other->v.origin[i] + 0.5 * (other->v.mins[i] + other->v.maxs[i]),
				sv.Protocol,
				sv.PrototcolFlags
			);
		}

		ent->v.dmg_take = 0;
		ent->v.dmg_save = 0;
	}

	// send the current viewpos offset from the view entity
	SV_SetIdealPitch ();

	// a fixangle might get lost in a dropped packet.  Oh well.
	if (ent->v.fixangle)
	{
		MSG_WriteByte (msg, svc_setangle);

		for (i = 0; i < 3; i++)
			MSG_WriteAngle (msg, ent->v.angles[i], sv.Protocol, sv.PrototcolFlags, i);

		ent->v.fixangle = 0;
	}

	bits = 0;

	if (ent->v.view_ofs[2] != DEFAULT_VIEWHEIGHT) bits |= SU_VIEWHEIGHT;
	if (ent->v.idealpitch) bits |= SU_IDEALPITCH;

	// stuff the sigil bits into the high bits of items for sbar, or else
	// mix in items2
	val = GETEDICTFIELDVALUEFAST (ent, ed_items2);

	if (val)
		items = (int) ent->v.items | ((int) val->_float << 23);
	else items = (int) ent->v.items | ((int) SVProgs->GlobalStruct->serverflags << 28);

	// always sent
	bits |= SU_ITEMS;

	if ((int) ent->v.flags & FL_ONGROUND) bits |= SU_ONGROUND;
	if (ent->v.waterlevel >= 2) bits |= SU_INWATER;

	for (i = 0; i < 3; i++)
	{
		if (ent->v.punchangle[i]) bits |= (SU_PUNCH1 << i);
		if (ent->v.velocity[i]) bits |= (SU_VELOCITY1 << i);
	}

	if (ent->v.weaponframe) bits |= SU_WEAPONFRAME;
	if (ent->v.armorvalue) bits |= SU_ARMOR;

	// always sent
	bits |= SU_WEAPON;

	if (sv.Protocol == PROTOCOL_VERSION_FITZ || sv.Protocol == PROTOCOL_VERSION_RMQ)
	{
		if ((bits & SU_WEAPON) && (SV_ModelIndex (SVProgs->Strings + ent->v.weaponmodel) & 0xFF00)) bits |= SU_WEAPON2;
		if ((int) ent->v.armorvalue & 0xFF00) bits |= SU_ARMOR2;
		if ((int) ent->v.currentammo & 0xFF00) bits |= SU_AMMO2;
		if ((int) ent->v.ammo_shells & 0xFF00) bits |= SU_SHELLS2;
		if ((int) ent->v.ammo_nails & 0xFF00) bits |= SU_NAILS2;
		if ((int) ent->v.ammo_rockets & 0xFF00) bits |= SU_ROCKETS2;
		if ((int) ent->v.ammo_cells & 0xFF00) bits |= SU_CELLS2;
		if ((bits & SU_WEAPONFRAME) && ((int) ent->v.weaponframe & 0xFF00)) bits |= SU_WEAPONFRAME2;
		if ((bits & SU_WEAPON) && ent->alpha != 255) bits |= SU_WEAPONALPHA;
		if (bits >= 65536) bits |= SU_EXTEND1;
		if (bits >= 16777216) bits |= SU_EXTEND2;
	}

	// send the data
	MSG_WriteByte (msg, svc_clientdata);
	MSG_WriteShort (msg, bits);

	if (bits & SU_EXTEND1) MSG_WriteByte (msg, bits >> 16);
	if (bits & SU_EXTEND2) MSG_WriteByte (msg, bits >> 24);
	if (bits & SU_VIEWHEIGHT) MSG_WriteChar (msg, ent->v.view_ofs[2]);
	if (bits & SU_IDEALPITCH) MSG_WriteChar (msg, ent->v.idealpitch);

	for (i = 0; i < 3; i++)
	{
		if (bits & (SU_PUNCH1 << i)) MSG_WriteChar (msg, ent->v.punchangle[i]);
		if (bits & (SU_VELOCITY1 << i)) MSG_WriteChar (msg, ent->v.velocity[i] / 16);
	}

	// always sent
	MSG_WriteLong (msg, items);

	if (bits & SU_WEAPONFRAME) MSG_WriteByte (msg, ent->v.weaponframe);
	if (bits & SU_ARMOR) MSG_WriteByte (msg, ent->v.armorvalue);

	// always sent
	SV_WriteByteShort (msg, SV_ModelIndex (SVProgs->Strings + ent->v.weaponmodel));

	MSG_WriteShort (msg, ent->v.health);
	MSG_WriteByte (msg, ent->v.currentammo);
	MSG_WriteByte (msg, ent->v.ammo_shells);
	MSG_WriteByte (msg, ent->v.ammo_nails);
	MSG_WriteByte (msg, ent->v.ammo_rockets);
	MSG_WriteByte (msg, ent->v.ammo_cells);

	if (standard_quake)
		MSG_WriteByte (msg, ent->v.weapon);
	else
	{
		for (i = 0; i < 32; i++)
		{
			if (((int) ent->v.weapon) & (1 << i))
			{
				MSG_WriteByte (msg, i);
				break;
			}
		}
	}

	if (bits & SU_WEAPON2) MSG_WriteByte (msg, SV_ModelIndex (SVProgs->Strings + ent->v.weaponmodel) >> 8);
	if (bits & SU_ARMOR2) MSG_WriteByte (msg, (int) ent->v.armorvalue >> 8);
	if (bits & SU_AMMO2) MSG_WriteByte (msg, (int) ent->v.currentammo >> 8);
	if (bits & SU_SHELLS2) MSG_WriteByte (msg, (int) ent->v.ammo_shells >> 8);
	if (bits & SU_NAILS2) MSG_WriteByte (msg, (int) ent->v.ammo_nails >> 8);
	if (bits & SU_ROCKETS2) MSG_WriteByte (msg, (int) ent->v.ammo_rockets >> 8);
	if (bits & SU_CELLS2) MSG_WriteByte (msg, (int) ent->v.ammo_cells >> 8);
	if (bits & SU_WEAPONFRAME2) MSG_WriteByte (msg, (int) ent->v.weaponframe >> 8);
	if (bits & SU_WEAPONALPHA) MSG_WriteByte (msg, ent->alpha);
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

	// DP_SV_CLIENTCAMERA : client, not client->edict
	SV_WriteEntitiesToClient (client, &msg);

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
extern int eval_clientcamera; // DP_SV_CLIENTCAMERA
// 	GETEDICTFIELDVALUEFAST

void SV_UpdateToReliableMessages (void)
{
	int			i, j;
	client_t *client;
	eval_t *val;

	// check for changes to be sent over the reliable streams
	for (i = 0, host_client = svs.clients; i < svs.maxclients; i++, host_client++)
	{
		// DP_SV_CLIENTCAMERA
		if ((val = GETEDICTFIELDVALUEFAST (host_client->edict, eval_clientcamera)) && val->edict > 0)
		{
			int oldclientcamera = host_client->clientcamera;

			// would val->edict ever be >= sv.max_edicts in rmq???
			if (val->edict >= SVProgs->MaxEdicts || (GetEdictForNumber (val->edict))->free)
				val->edict = host_client->clientcamera = GetNumberForEdict (host_client->edict);
			else host_client->clientcamera = val->edict;

			if (oldclientcamera != host_client->clientcamera)
			{
				MSG_WriteByte (&host_client->message, svc_setview);
				MSG_WriteShort (&host_client->message, host_client->clientcamera);
			}
		}

		if (host_client->old_frags != host_client->edict->v.frags)
		{
			for (j = 0, client = svs.clients; j < svs.maxclients; j++, client++)
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

	for (j = 0, client = svs.clients; j < svs.maxclients; j++, client++)
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
	msg.maxsize = sizeof (buf);
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
	for (i = 0, host_client = svs.clients; i < svs.maxclients; i++, host_client++)
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

		// joe: NAT fix from ProQuake
		if (host_client->netconnection->net_wait)
			continue;

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
		if (!strcmp (sv.model_precache[i], name))
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
	edict_t		*svent;
	int			entnum;
	int			bits;

	for (entnum = 0; entnum < SVProgs->NumEdicts; entnum++)
	{
		// get the current server version
		svent = GetEdictForNumber (entnum);

		if (svent->free) continue;
		if (entnum > svs.maxclients && !svent->v.modelindex) continue;

		// create entity baseline
		VectorCopy (svent->v.origin, svent->baseline.origin);
		VectorCopy (svent->v.angles, svent->baseline.angles);
		svent->baseline.frame = svent->v.frame;
		svent->baseline.skin = svent->v.skin;

		if (entnum > 0 && entnum <= svs.maxclients)
		{
			// player model
			svent->baseline.colormap = entnum;
			svent->baseline.modelindex = SV_ModelIndex ("progs/player.mdl");
			svent->baseline.alpha = 255;
		}
		else
		{
			// other model
			svent->baseline.colormap = 0;
			svent->baseline.modelindex = SV_ModelIndex (SVProgs->Strings + svent->v.model);
			svent->baseline.alpha = svent->alpha;
		}

		bits = 0;

		if (sv.Protocol == PROTOCOL_VERSION_FITZ || sv.Protocol == PROTOCOL_VERSION_RMQ)
		{
			if (svent->baseline.modelindex & 0xFF00) bits |= B_LARGEMODEL;
			if (svent->baseline.frame & 0xFF00) bits |= B_LARGEFRAME;
			if (svent->baseline.alpha < 255) bits |= B_ALPHA;
		}
		else svent->baseline.alpha = 255;

		if (sv.Protocol == PROTOCOL_VERSION_NQ)
		{
			// still want to send a baseline here so just trunc the stuff
			if (svent->baseline.modelindex & 0xFF00) svent->baseline.modelindex = 0;
			if (svent->baseline.frame & 0xFF00) svent->baseline.frame = 0;
		}

		// add to the message
		if (bits)
			MSG_WriteByte (&sv.signon, svc_spawnbaseline2);
		else MSG_WriteByte (&sv.signon, svc_spawnbaseline);

		MSG_WriteShort (&sv.signon, entnum);

		if (bits) MSG_WriteByte (&sv.signon, bits);

		if (bits & B_LARGEMODEL)
			MSG_WriteShort (&sv.signon, svent->baseline.modelindex);
		else SV_WriteByteShort (&sv.signon, svent->baseline.modelindex);

		if (bits & B_LARGEFRAME)
			MSG_WriteShort (&sv.signon, svent->baseline.frame);
		else MSG_WriteByte (&sv.signon, svent->baseline.frame);

		MSG_WriteByte (&sv.signon, svent->baseline.colormap);
		MSG_WriteByte (&sv.signon, svent->baseline.skin);

		for (i = 0; i < 3; i++)
		{
			MSG_WriteCoord (&sv.signon, svent->baseline.origin[i], sv.Protocol, sv.PrototcolFlags);
			MSG_WriteAngle (&sv.signon, svent->baseline.angles[i], sv.Protocol, sv.PrototcolFlags, i);
		}

		if (bits & B_ALPHA) MSG_WriteByte (&sv.signon, svent->baseline.alpha);
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

	svs.serverflags = SVProgs->GlobalStruct->serverflags;

	for (i = 0, host_client = svs.clients; i < svs.maxclients; i++, host_client++)
	{
		if (!host_client->active)
			continue;

		// call the progs to get default spawn parms for the new client
		SVProgs->GlobalStruct->self = EDICT_TO_PROG (host_client->edict);
		SVProgs->ExecuteProgram (SVProgs->GlobalStruct->SetChangeParms);

		for (j = 0; j < NUM_SPAWN_PARMS; j++)
			host_client->spawn_parms[j] = (&SVProgs->GlobalStruct->parm1)[j];
	}
}


/*
================
SV_SpawnServer

This is called at the start of each level
================
*/
extern float ScrCenterTimeOff;
void CL_WipeParticles (void);
void Mod_InitForMap (model_t *mod);

void SV_SpawnServer (char *server)
{
	edict_t		*ent;
	int			i;

	// let's not have any servers with no name
	if (hostname.string[0] == 0) Cvar_Set ("hostname", "UNNAMED");

	ScrCenterTimeOff = 0;
	CL_WipeParticles ();

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
	if (SVProgs) delete SVProgs;

	SVProgs = new CProgsDat ();
	SVProgs->LoadProgs ("progs.dat", &sv_progs);

	// bring up the worldmodel early so that we can get a count on the number of edicts needed
	spawnserver = true;
	_snprintf (sv.modelname, 64, "maps/%s.bsp", server);
	sv.worldmodel = Mod_ForName (sv.modelname, false);
	spawnserver = false;

	if (!sv.worldmodel)
	{
		Con_Printf ("Couldn't spawn server %s\n", sv.modelname);
		sv.active = false;
		return;
	}

	// the server needs these up ASAP as it's going to run some physics frames shortly
	Mod_InitForMap (sv.worldmodel);

	// no edicts yet
	SVProgs->NumEdicts = 0;
	SVProgs->MaxEdicts = 0;

	// clear down in case the edict size has changed (e.g. if the game changed)
	SAFE_DELETE (Pool_Edicts);

	// alloc an initial batch of 128 edicts
	// SVProgs->Edicts = NULL;
	SVProgs->EdictPointers = NULL;
	SV_AllocEdicts (128);

	sv.Protocol = sv_protocol;

	// set the correct protocol flags; these will always be 0 unless we're on PROTOCOL_VERSION_RMQ
	// 24-bit gives the herky-jerkies on plats so just don't do it...
	if (sv.Protocol == PROTOCOL_VERSION_RMQ)
		sv.PrototcolFlags = PRFL_FLOATCOORD | PRFL_SHORTANGLE;
	else sv.PrototcolFlags = 0;

	sv_max_datagram = (sv.Protocol == PROTOCOL_VERSION_NQ ? 1024 : MAX_DATAGRAM); // Limit packet size if old protocol

	sv.datagram.maxsize = MAX_DATAGRAM2;
	sv.datagram.cursize = 0;
	sv.datagram.data = sv.datagram_buf;

	sv.reliable_datagram.maxsize = MAX_DATAGRAM2;
	sv.reliable_datagram.cursize = 0;
	sv.reliable_datagram.data = sv.reliable_datagram_buf;

	sv.signon.maxsize = sizeof (sv.signon_buf);
	sv.signon.cursize = sv.signondiff = 0;
	sv.signon.data = sv.signon_buf;

	// leave slots at start for clients only
	SVProgs->NumEdicts = svs.maxclients + 1;

	for (i = 0; i < svs.maxclients; i++)
	{
		ent = GetEdictForNumber (i + 1);
		svs.clients[i].edict = ent;
	}

	sv.state = ss_loading;
	sv.paused = false;
	sv.time = 1.0;
	sv.models[1] = sv.worldmodel;

	// clear world interaction links
	SV_ClearWorld ();

	sv.sound_precache[0] = SVProgs->Strings;
	sv.model_precache[0] = SVProgs->Strings;
	sv.model_precache[1] = sv.modelname;

	for (i = 1; i < sv.worldmodel->brushhdr->numsubmodels; i++)
	{
		// prevent crash
		if (i >= MAX_MODELS) break;

		// old limit (this will be trunc'ed by WriteByteShort anyway...)
		if (sv.Protocol == PROTOCOL_VERSION_NQ && i > 255) break;

		sv.model_precache[1 + i] = localmodels[i];
		sv.models[i + 1] = Mod_ForName (localmodels[i], false);
	}

	// load the rest of the entities
	ent = GetEdictForNumber (0);
	memset (&ent->v, 0, SVProgs->QC->entityfields * 4);
	ent->free = false;
	ent->v.model = sv.worldmodel->name - SVProgs->Strings;
	ent->v.modelindex = 1;		// world model
	ent->v.solid = SOLID_BSP;
	ent->v.movetype = MOVETYPE_PUSH;

	// these are needed here so that we can filter edicts in or out depending on the game type
	if (coop.value)
		SVProgs->GlobalStruct->coop = coop.value;
	else SVProgs->GlobalStruct->deathmatch = deathmatch.value;

	SVProgs->GlobalStruct->mapname = sv.name - SVProgs->Strings;

	// serverflags are for cross level information (sigils)
	SVProgs->GlobalStruct->serverflags = svs.serverflags;

	ED_LoadFromFile (sv.worldmodel->brushhdr->entities);

	sv.active = true;

	// all setup is completed, any further precache statements are errors
	sv.state = ss_active;

	// run two frames to allow everything to settle
	SV_Physics (0.1);
	SV_Physics (0.1);

	// create a baseline for more efficient communications
	SV_CreateBaseline ();

	// send serverinfo to all connected clients
	for (i = 0, host_client = svs.clients; i < svs.maxclients; i++, host_client++)
		if (host_client->active)
			SV_SendServerinfo (host_client);

	Con_DPrintf ("Allocated %i edicts\n", SVProgs->NumEdicts);
	Con_DPrintf ("Server spawned.\n");
}

