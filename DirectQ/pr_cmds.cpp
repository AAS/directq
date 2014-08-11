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
#include "pr_class.h"

edict_t *ED_Alloc (CProgsDat *Progs);


// QuakeC debugger
cvar_t qc_debug ("qc_debug", "0");
cvar_t sv_permagibs ("sv_permagibs", "0", CVAR_ARCHIVE | CVAR_SERVER);

void QC_DebugOutput (char *debugtext, ...)
{
	if (!qc_debug.integer) return;

	if (!sv.active) return;

	va_list argptr;
	char string[1024];

	va_start (argptr, debugtext);
	_vsnprintf (string, 1024, debugtext, argptr);
	va_end (argptr);

	static int qc_olddebug = 0;

	if (qc_debug.integer & 1)
		Con_Printf ("QC Debug: %s\n", string);

	if (qc_debug.integer & 2)
	{
		FILE *f;
		char *modes[] = {"a", "w"};
		int modenum = 0;

		if (qc_debug.integer != qc_olddebug)
		{
			qc_olddebug = qc_debug.integer;
			modenum = 1;
		}

		if (!(f = fopen ("qcdebuglog.txt", modes[modenum]))) return;

		fprintf (f, "%s\n", string);
		fclose (f);
	}
}


#define	RETURN_EDICT(e) (((int *)SVProgs->Globals)[OFS_RETURN] = EDICT_TO_PROG(e))

cvar_t pr_checkextension ("pr_checkextension", "0");

// 2001-10-20 Extension System by LordHavoc  start
char *pr_extensions[] =
{
	// add the extension names here, syntax: "extensionname",
	// always end the list with NULL.
	// "DP_ENT_ALPHA",	// clashes with fitz U_FRAME2 message - sigh.
	"DP_TE_PARTICLERAIN",
	"DP_TE_PARTICLESNOW",
	"DP_SV_CLIENTCAMERA",
	"FRIK_FILE",
	NULL
};


bool extension_find (char *name)
{
	for (int i = 0;; i++)
	{
		if (!pr_extensions[i]) break;

		if (!stricmp (pr_extensions[i], name)) return true;
	}

	return false;
}


/*
=================
PF_extension_find
returns true if the extension is supported by the server

float extension_find(string name)
=================
*/
void PF_extension_find (void)
{
	G_FLOAT (OFS_RETURN) = extension_find (G_STRING (OFS_PARM0));
}
// 2001-10-20 Extension System by LordHavoc  end


// 2001-10-20 Extension System by LordHavoc  start
void PR_Extension_List_f (void)
{
	int		i;
	char	*partial;
	int		len;
	int		count;

	if (Cmd_Argc () > 1)
	{
		partial = Cmd_Argv (1);
		len = strlen (partial);
	}
	else
	{
		partial = NULL;
		len = 0;
	}

	count = 0;

	for (i = 0;; i++)
	{
		if (!pr_extensions[i]) break;

		if (partial && strnicmp (partial, pr_extensions[i], len))
		{
			continue;
		}

		count++;
		Con_Printf ("%s\n", pr_extensions[i]);
	}

	Con_Printf ("------------\n");

	if (partial)
	{
		Con_Printf ("%i beginning with \"%s\" out of ", count, partial);
	}

	Con_Printf ("%i extensions\n", i);
}

cmd_t PR_Extension_List_Cmd ("extensionlist", PR_Extension_List_f);
// 2001-10-20 Extension System by LordHavoc  end


/*
===============================================================================

						TEMP STRINGS

	Help to prevent buffer overflows, memory access violations, and
	other fun by maintaining a rotating set of temp strings.

===============================================================================
*/

#define PR_MAX_TEMP_STRING		1024
#define PR_NUM_TEMP_STRINGS		8
#define PR_TEMP_STRING_MASK		(PR_NUM_TEMP_STRINGS - 1)

char *PR_GetTempString (void)
{
	static char **pr_temp_strings = NULL;
	static int pr_temp_string_num = 0;

	if (!pr_temp_strings)
	{
		// place these in the permanent pool cos they'll always be needed
		pr_temp_strings = (char **) Zone_Alloc (sizeof (char *) * PR_NUM_TEMP_STRINGS);

		for (int i = 0; i < PR_NUM_TEMP_STRINGS; i++) pr_temp_strings[i] = (char *) Zone_Alloc (PR_MAX_TEMP_STRING);
	}

	// go to a new temp string, rotate the buffer if needed, and ensure that it's null termed
	(++pr_temp_string_num) &= PR_TEMP_STRING_MASK;
	pr_temp_strings[pr_temp_string_num][0] = 0;

	return pr_temp_strings[pr_temp_string_num];
}


/*
===============================================================================

						BUILT-IN FUNCTIONS

===============================================================================
*/

char *PF_VarString (int	first)
{
	static char *out = NULL;

	out = PR_GetTempString ();

	for (int i = first, len = 0; i < SVProgs->Argc; i++)
	{
		char *append = SVProgs->Strings + ((int *) SVProgs->Globals)[OFS_PARM0 + i * 3];
		float fvarstring = SVProgs->Globals[OFS_PARM0 + i * 3];
		int ivarstring = ((int *) SVProgs->Globals)[OFS_PARM0 + i * 3];

		if (fvarstring != fvarstring)
		{
			// http://www.doomworld.com/vb/everything-else/46926-sdl-quake/
			// PF_VarString NaN bug - this only happens when changing a level or exiting the game so swallow it silently
			out[0] = 0;
			return out;
		}

		// prevent buffer overflow in this function
		len += strlen (append) + 1;

		if (len >= PR_MAX_TEMP_STRING) break;

		strcat (out, append);
	}

	return out;
}


/*
=================
PF_errror

This is a TERMINAL error, which will kill off the entire server.
Dumps self.

error(value)
=================
*/
void PF_error (void)
{
	char	*s;
	edict_t	*ed;

	s = PF_VarString (0);
	Con_Printf ("======SERVER ERROR in %s:\n%s\n", SVProgs->Strings + SVProgs->XFunction->s_name, s);
	ed = PROG_TO_EDICT (SVProgs->GlobalStruct->self);
	ED_Print (ed);

	QC_DebugOutput ("======SERVER ERROR in %s:\n%s\n", SVProgs->Strings + SVProgs->XFunction->s_name, s);
	Host_Error ("Program error");
}


/*
=================
PF_objerror

Dumps out self, then an error message.  The program is aborted and self is
removed, but the level can continue.

objerror(value)
=================
*/
void PF_objerror (void)
{
	char	*s;
	edict_t	*ed;

	s = PF_VarString (0);
	Con_Printf ("======OBJECT ERROR in %s:\n%s\n", SVProgs->Strings + SVProgs->XFunction->s_name, s);
	ed = PROG_TO_EDICT (SVProgs->GlobalStruct->self);
	ED_Print (ed);
	ED_Free (ed);

	QC_DebugOutput ("======OBJECT ERROR in %s:\n%s\n", SVProgs->Strings + SVProgs->XFunction->s_name, s);
	// Host_Error ("Program error");	// fitz says this should not be fatal, so...
}



/*
==============
PF_makevectors

Writes new values for v_forward, v_up, and v_right based on angles
makevectors(vector)
==============
*/
void PF_makevectors (void)
{
	// this is the only place where the old anglevectors is used and only exists because the globals table has right and up out of order!!!
	// i could just adjust my avectors_t struct to this order but wtf.
	AngleVectors (G_VECTOR (OFS_PARM0), SVProgs->GlobalStruct->v_forward, SVProgs->GlobalStruct->v_right, SVProgs->GlobalStruct->v_up);
}

/*
=================
PF_setorigin

This is the only valid way to move an object without using the physics of the world (setting velocity and waiting).
Directly changing origin will not set internal links correctly, so clipping would be messed up.  This should be called
when an object is spawned, and then only if it is teleported.

setorigin (entity, origin)
=================
*/
void PF_setorigin (void)
{
	edict_t	*e;
	float	*org;

	e = G_EDICT (OFS_PARM0);
	org = G_VECTOR (OFS_PARM1);
	VectorCopy (org, e->v.origin);
	SV_LinkEdict (e, false);
}


void SetMinMaxSize (edict_t *e, float *min, float *max, bool rotate)
{
	float	*angles;
	vec3_t	rmin, rmax;
	float	bounds[2][3];
	float	xvector[2], yvector[2];
	float	a;
	vec3_t	base, transformed;
	int		i, j, k, l;

	for (i = 0; i < 3; i++)
		if (min[i] > max[i])
			SVProgs->RunError ("backwards mins/maxs");

	rotate = false;		// FIXME: implement rotation properly again

	if (!rotate)
	{
		VectorCopy (min, rmin);
		VectorCopy (max, rmax);
	}
	else
	{
		// find min / max for rotations
		angles = e->v.angles;

		a = angles[1] / 180 * D3DX_PI;

		xvector[0] = cos (a);
		xvector[1] = sin (a);
		yvector[0] = -sin (a);
		yvector[1] = cos (a);

		VectorCopy (min, bounds[0]);
		VectorCopy (max, bounds[1]);

		rmin[0] = rmin[1] = rmin[2] = 9999;
		rmax[0] = rmax[1] = rmax[2] = -9999;

		for (i = 0; i <= 1; i++)
		{
			base[0] = bounds[i][0];

			for (j = 0; j <= 1; j++)
			{
				base[1] = bounds[j][1];

				for (k = 0; k <= 1; k++)
				{
					base[2] = bounds[k][2];

					// transform the point
					transformed[0] = xvector[0] * base[0] + yvector[0] * base[1];
					transformed[1] = xvector[1] * base[0] + yvector[1] * base[1];
					transformed[2] = base[2];

					for (l = 0; l < 3; l++)
					{
						if (transformed[l] < rmin[l])
							rmin[l] = transformed[l];

						if (transformed[l] > rmax[l])
							rmax[l] = transformed[l];
					}
				}
			}
		}
	}

	// set derived values
	VectorCopy (rmin, e->v.mins);
	VectorCopy (rmax, e->v.maxs);
	VectorSubtract (max, min, e->v.size);

	SV_LinkEdict (e, false);
}

/*
=================
PF_setsize

the size box is rotated by the current angle

setsize (entity, minvector, maxvector)
=================
*/
void PF_setsize (void)
{
	edict_t	*e;
	float	*min, *max;

	e = G_EDICT (OFS_PARM0);
	min = G_VECTOR (OFS_PARM1);
	max = G_VECTOR (OFS_PARM2);
	SetMinMaxSize (e, min, max, false);
}


/*
=================
PF_setmodel

setmodel(entity, model)
=================
*/
void PF_setmodel (void)
{
	edict_t	*e;
	char	*m;
	model_t	*mod;
	int		i;

	e = G_EDICT (OFS_PARM0);
	m = G_STRING (OFS_PARM1);

	// rewrote to use array indexing instead of a pointer
	for (i = 0; sv.model_precache[i]; i++)
	{
		if (!sv.model_precache[i]) SVProgs->RunError ("no precache: %s\n", m);

		if (!strcmp (sv.model_precache[i], m)) break;
	}

	e->v.model = m - SVProgs->Strings;
	e->v.modelindex = i;

	mod = sv.models[(int) e->v.modelindex];

	if (mod)
		SetMinMaxSize (e, mod->mins, mod->maxs, true);
	else SetMinMaxSize (e, vec3_origin, vec3_origin, true);
}

/*
=================
PF_bprint

broadcast print to everyone on server

bprint(value)
=================
*/
void PF_bprint (void)
{
	char		*s;

	s = PF_VarString (0);
	SV_BroadcastPrintf ("%s", s);
}

/*
=================
PF_sprint

single print to a specific client

sprint(clientent, value)
=================
*/
void PF_sprint (void)
{
	char		*s;
	client_t	*client;
	int			entnum;

	entnum = G_EDICTNUM (OFS_PARM0);
	s = PF_VarString (1);

	if (entnum < 1 || entnum > svs.maxclients)
	{
		Con_Printf ("tried to sprint to a non-client\n");
		return;
	}

	client = &svs.clients[entnum - 1];

	MSG_WriteChar (&client->message, svc_print);
	MSG_WriteString (&client->message, s);
}


/*
=================
PF_centerprint

single print to a specific client

centerprint(clientent, value)
=================
*/
void PF_centerprint (void)
{
	char		*s;
	client_t	*client;
	int			entnum;

	entnum = G_EDICTNUM (OFS_PARM0);
	s = PF_VarString (1);

	if (entnum < 1 || entnum > svs.maxclients)
	{
		Con_Printf ("tried to sprint to a non-client\n");
		return;
	}

	client = &svs.clients[entnum-1];

	MSG_WriteChar (&client->message, svc_centerprint);
	MSG_WriteString (&client->message, s);
}


/*
=================
PF_normalize

vector normalize(vector)
=================
*/
void PF_normalize (void)
{
	float	*value1;
	vec3_t	newvalue;
	float	newval;

	value1 = G_VECTOR (OFS_PARM0);

	newval = value1[0] * value1[0] + value1[1] * value1[1] + value1[2] * value1[2];
	newval = sqrt (newval);

	if (newval == 0)
		newvalue[0] = newvalue[1] = newvalue[2] = 0;
	else
	{
		newval = 1 / newval;
		newvalue[0] = value1[0] * newval;
		newvalue[1] = value1[1] * newval;
		newvalue[2] = value1[2] * newval;
	}

	VectorCopy (newvalue, G_VECTOR (OFS_RETURN));
}

/*
=================
PF_vlen

scalar vlen(vector)
=================
*/
void PF_vlen (void)
{
	float	*value1;
	float	newval;

	value1 = G_VECTOR (OFS_PARM0);

	newval = value1[0] * value1[0] + value1[1] * value1[1] + value1[2] * value1[2];
	newval = sqrt (newval);

	G_FLOAT (OFS_RETURN) = newval;
}

/*
=================
PF_vectoyaw

float vectoyaw(vector)
=================
*/
void PF_vectoyaw (void)
{
	float	*value1;
	float	yaw;

	value1 = G_VECTOR (OFS_PARM0);

	if (value1[1] == 0 && value1[0] == 0)
		yaw = 0;
	else
	{
		yaw = (int) (atan2 (value1[1], value1[0]) * 180 / D3DX_PI);

		if (yaw < 0)
			yaw += 360;
	}

	G_FLOAT (OFS_RETURN) = yaw;
}


/*
=================
PF_vectoangles

vector vectoangles(vector)
=================
*/
void PF_vectoangles (void)
{
	float	*value1;
	float	forward;
	float	yaw, pitch;

	value1 = G_VECTOR (OFS_PARM0);

	if (value1[1] == 0 && value1[0] == 0)
	{
		yaw = 0;

		if (value1[2] > 0)
			pitch = 90;
		else
			pitch = 270;
	}
	else
	{
		yaw = (int) (atan2 (value1[1], value1[0]) * 180 / D3DX_PI);

		if (yaw < 0)
			yaw += 360;

		forward = sqrt (value1[0] * value1[0] + value1[1] * value1[1]);
		pitch = (int) (atan2 (value1[2], forward) * 180 / D3DX_PI);

		if (pitch < 0)
			pitch += 360;
	}

	G_FLOAT (OFS_RETURN + 0) = pitch;
	G_FLOAT (OFS_RETURN + 1) = yaw;
	G_FLOAT (OFS_RETURN + 2) = 0;
}

/*
=================
PF_Random

Returns a number from 0<= num < 1

random()
=================
*/
void PF_random (void)
{
	float		num;

	num = (rand () & 0x7fff) / ((float) 0x7fff);

	G_FLOAT (OFS_RETURN) = num;
}

/*
=================
PF_particle

particle(origin, color, count)
=================
*/
void PF_particle (void)
{
	float		*org, *dir;
	float		color;
	float		count;

	org = G_VECTOR (OFS_PARM0);
	dir = G_VECTOR (OFS_PARM1);
	color = G_FLOAT (OFS_PARM2);
	count = G_FLOAT (OFS_PARM3);
	SV_StartParticle (org, dir, color, count);
}


/*
=================
PF_ambientsound

=================
*/
void PF_ambientsound (void)
{
	char		**check;
	char		*samp;
	float		*pos;
	float 		vol, attenuation;
	int			i, soundnum;

	pos = G_VECTOR (OFS_PARM0);
	samp = G_STRING (OFS_PARM1);
	vol = G_FLOAT (OFS_PARM2);
	attenuation = G_FLOAT (OFS_PARM3);

	// check to see if samp was properly precached
	for (soundnum = 0, check = sv.sound_precache; *check; check++, soundnum++)
		if (!strcmp (*check, samp))
			break;

	if (!*check)
	{
		Con_Printf ("no precache: %s\n", samp);
		return;
	}

	// add an svc_spawnambient command to the level signon packet
	if (soundnum > 255 && (sv.Protocol == PROTOCOL_VERSION_FITZ || sv.Protocol == PROTOCOL_VERSION_RMQ))
		MSG_WriteByte (&sv.signon, svc_spawnstaticsound2);
	else MSG_WriteByte (&sv.signon, svc_spawnstaticsound);

	for (i = 0; i < 3; i++)
		MSG_WriteCoord (&sv.signon, pos[i], sv.Protocol, sv.PrototcolFlags);

	if (soundnum > 255 && (sv.Protocol == PROTOCOL_VERSION_FITZ || sv.Protocol == PROTOCOL_VERSION_RMQ))
		MSG_WriteShort (&sv.signon, soundnum);
	else MSG_WriteByte (&sv.signon, soundnum);

	MSG_WriteByte (&sv.signon, vol * 255);
	MSG_WriteByte (&sv.signon, attenuation * 64);
}


/*
=================
PF_sound

Each entity can have eight independant sound sources, like voice,
weapon, feet, etc.

Channel 0 is an auto-allocate channel, the others override anything
allready running on that entity/channel pair.

An attenuation of 0 will play full volume everywhere in the level.
Larger attenuations will drop off.

=================
*/
void PF_sound (void)
{
	char		*sample;
	int			channel;
	edict_t		*entity;
	int 		volume;
	float attenuation;

	entity = G_EDICT (OFS_PARM0);
	channel = G_FLOAT (OFS_PARM1);
	sample = G_STRING (OFS_PARM2);
	volume = G_FLOAT (OFS_PARM3) * 255;
	attenuation = G_FLOAT (OFS_PARM4);

	if (volume < 0 || volume > 255)
		Sys_Error ("SV_StartSound: volume = %i", volume);

	if (attenuation < 0 || attenuation > 4)
		Sys_Error ("SV_StartSound: attenuation = %f", attenuation);

	if (channel < 0 || channel > 7)
		Sys_Error ("SV_StartSound: channel = %i", channel);

	SV_StartSound (entity, channel, sample, volume, attenuation);
}

/*
=================
PF_break

break()
=================
*/
void PF_break (void)
{
	Con_Printf ("break statement\n");
	* (int *) - 4 = 0;	// dump to debugger
	//	SVProgs->RunError ("break statement");
}

/*
=================
PF_traceline

Used for use tracing and shot targeting
Traces are blocked by bbox and exact bsp entityes, and also slide box entities
if the tryents flag is set.

traceline (vector1, vector2, tryents)
=================
*/
void PF_traceline (void)
{
	float	*v1, *v2;
	trace_t	trace;
	int		nomonsters;
	edict_t	*ent;

	v1 = G_VECTOR (OFS_PARM0);
	v2 = G_VECTOR (OFS_PARM1);
	nomonsters = G_FLOAT (OFS_PARM2);
	ent = G_EDICT (OFS_PARM3);

	trace = SV_Move (v1, vec3_origin, vec3_origin, v2, nomonsters, ent);

	SVProgs->GlobalStruct->trace_allsolid = trace.allsolid;
	SVProgs->GlobalStruct->trace_startsolid = trace.startsolid;
	SVProgs->GlobalStruct->trace_fraction = trace.fraction;
	SVProgs->GlobalStruct->trace_inwater = trace.inwater;
	SVProgs->GlobalStruct->trace_inopen = trace.inopen;
	VectorCopy (trace.endpos, SVProgs->GlobalStruct->trace_endpos);
	VectorCopy (trace.plane.normal, SVProgs->GlobalStruct->trace_plane_normal);
	SVProgs->GlobalStruct->trace_plane_dist =  trace.plane.dist;

	if (trace.ent)
		SVProgs->GlobalStruct->trace_ent = EDICT_TO_PROG (trace.ent);
	else SVProgs->GlobalStruct->trace_ent = EDICT_TO_PROG (SVProgs->EdictPointers[0]);
}


extern trace_t SV_Trace_Toss (edict_t *ent, edict_t *ignore);

void PF_TraceToss (void)
{
	trace_t	trace;
	edict_t	*ent;
	edict_t	*ignore;

	ent = G_EDICT (OFS_PARM0);
	ignore = G_EDICT (OFS_PARM1);

	trace = SV_Trace_Toss (ent, ignore);

	SVProgs->GlobalStruct->trace_allsolid = trace.allsolid;
	SVProgs->GlobalStruct->trace_startsolid = trace.startsolid;
	SVProgs->GlobalStruct->trace_fraction = trace.fraction;
	SVProgs->GlobalStruct->trace_inwater = trace.inwater;
	SVProgs->GlobalStruct->trace_inopen = trace.inopen;
	VectorCopy (trace.endpos, SVProgs->GlobalStruct->trace_endpos);
	VectorCopy (trace.plane.normal, SVProgs->GlobalStruct->trace_plane_normal);
	SVProgs->GlobalStruct->trace_plane_dist =  trace.plane.dist;

	if (trace.ent)
		SVProgs->GlobalStruct->trace_ent = EDICT_TO_PROG (trace.ent);
	else SVProgs->GlobalStruct->trace_ent = EDICT_TO_PROG (SVProgs->EdictPointers[0]);
}


/*
=================
PF_checkpos

Returns true if the given entity can move to the given position from it's
current position by walking or rolling.
FIXME: make work...
scalar checkpos (entity, vector)
=================
*/
void PF_checkpos (void)
{
}

//============================================================================

byte *checkpvs = NULL;

int PF_newcheckclient (int check)
{
	int		i;
	byte	*pvs;
	edict_t	*ent;
	mleaf_t	*leaf;
	vec3_t	org;

	// cycle to the next one

	if (check < 1)
		check = 1;

	if (check > svs.maxclients)
		check = svs.maxclients;

	if (check == svs.maxclients)
		i = 1;
	else
		i = check + 1;

	for (;; i++)
	{
		if (i == svs.maxclients + 1)
			i = 1;

		ent = GetEdictForNumber (i);

		if (i == check)
			break;	// didn't find anything else

		if (ent->free)
			continue;

		if (ent->v.health <= 0)
			continue;

		if ((int) ent->v.flags & FL_NOTARGET)
			continue;

		// anything that is a client, or has a client as an enemy
		break;
	}

	// get the PVS for the entity
	VectorAdd (ent->v.origin, ent->v.view_ofs, org);
	leaf = Mod_PointInLeaf (org, sv.worldmodel);
	pvs = Mod_LeafPVS (leaf, sv.worldmodel);
	memcpy (checkpvs, pvs, (sv.worldmodel->brushhdr->numleafs + 7) >> 3);

	return i;
}

/*
=================
PF_checkclient

Returns a client (or object that has a client enemy) that would be a
valid target.

If there are more than one valid options, they are cycled each frame

If (self.origin + self.viewofs) is not in the PVS of the current target,
it is not returned at all.

name checkclient ()
=================
*/
#define	MAX_CHECK	16
int c_invis, c_notvis;

void PF_checkclient (void)
{
	edict_t	*ent, *self;
	mleaf_t	*leaf;
	int		l;
	vec3_t	view;

	// find a new check if on a new frame
	if (sv.time - sv.lastchecktime >= 0.1f)
	{
		sv.lastcheck = PF_newcheckclient (sv.lastcheck);
		sv.lastchecktime = sv.time;
	}

	// return check if it might be visible
	ent = GetEdictForNumber (sv.lastcheck);

	if (ent->free || ent->v.health <= 0)
	{
		RETURN_EDICT (SVProgs->EdictPointers[0]);
		return;
	}

	// if current entity can't possibly see the check entity, return 0
	self = PROG_TO_EDICT (SVProgs->GlobalStruct->self);
	VectorAdd (self->v.origin, self->v.view_ofs, view);
	leaf = Mod_PointInLeaf (view, sv.worldmodel);
	l = (leaf - sv.worldmodel->brushhdr->leafs) - 1;

	if ((l < 0) || !(checkpvs[l >> 3] & (1 << (l & 7))))
	{
		c_notvis++;
		RETURN_EDICT (SVProgs->EdictPointers[0]);
		return;
	}

	// might be able to see it
	c_invis++;
	RETURN_EDICT (ent);
}

//============================================================================


/*
=================
PF_stuffcmd

Sends text over to the client's execution buffer

stuffcmd (clientent, value)
=================
*/
cvar_t pr_show_stuffcmd ("pr_show_stuffcmd", "0");

void PF_stuffcmd (void)
{
	int		entnum;
	char	*str;
	client_t	*old;

	entnum = G_EDICTNUM (OFS_PARM0);

	if (entnum < 1 || entnum > svs.maxclients)
		SVProgs->RunError ("Parm 0 not a client");

	str = G_STRING (OFS_PARM1);

	if (pr_show_stuffcmd.integer)
		Con_Printf ("stuffcmd: %s\n", str);

	old = host_client;
	host_client = &svs.clients[entnum-1];
	Host_ClientCommands ("%s", str);
	host_client = old;
}

/*
=================
PF_localcmd

Sends text over to the client's execution buffer

localcmd (string)
=================
*/
void PF_localcmd (void)
{
	char	*str;

	str = G_STRING (OFS_PARM0);
	QC_DebugOutput ("Execing cmd \"%s\"", str);

	Cbuf_AddText (str);
}

/*
=================
PF_cvar

float cvar (string)
=================
*/
void PF_cvar (void)
{
	char	*str;

	str = G_STRING (OFS_PARM0);

	G_FLOAT (OFS_RETURN) = Cvar_VariableValue (str);
}


/*
=================
PF_cvar_set

float cvar (string)
=================
*/
void PF_cvar_set (void)
{
	char	*var, *val;

	var = G_STRING (OFS_PARM0);
	val = G_STRING (OFS_PARM1);

	QC_DebugOutput ("Setting cvar \"%s\" to \"%s\"", var, val);

	Cvar_Set (var, val);
}


/*
=================
PF_findradius

Returns a chain of entities that have origins within a spherical area

findradius (origin, radius)
=================
*/
void PF_findradius (void)
{
	edict_t	*ent, *chain;
	float	rad;
	float	*org;
	vec3_t	eorg;
	int		i, j;

	chain = (edict_t *) SVProgs->EdictPointers[0];

	org = G_VECTOR (OFS_PARM0);
	rad = G_FLOAT (OFS_PARM1);

	ent = NEXT_EDICT (SVProgs->EdictPointers[0]);

	for (i = 1; i < SVProgs->NumEdicts; i++, ent = NEXT_EDICT (ent))
	{
		if (ent->free)
			continue;

		if (ent->v.solid == SOLID_NOT)
			continue;

		for (j = 0; j < 3; j++)
			eorg[j] = org[j] - (ent->v.origin[j] + (ent->v.mins[j] + ent->v.maxs[j]) * 0.5);

		if (Length (eorg) > rad)
			continue;

		ent->v.chain = EDICT_TO_PROG (chain);
		chain = ent;
	}

	RETURN_EDICT (chain);
}


/*
=========
PF_dprint
=========
*/
void PF_dprint (void)
{
	Con_DPrintf ("%s", PF_VarString (0));
}


void PF_ftos (void)
{
	float	v;
	v = G_FLOAT (OFS_PARM0);

	char *pr_string_temp = PR_GetTempString ();

	if (v == (int) v)
		_snprintf (pr_string_temp, 128, "%d", (int) v);
	else
		_snprintf (pr_string_temp, 128, "%f", v);

	G_INT (OFS_RETURN) = pr_string_temp - SVProgs->Strings;
}

void PF_fabs (void)
{
	float	v;
	v = G_FLOAT (OFS_PARM0);
	G_FLOAT (OFS_RETURN) = fabs (v);
}

void PF_vtos (void)
{
	char *pr_string_temp = PR_GetTempString ();

	_snprintf (pr_string_temp, 128, "'%5.1f %5.1f %5.1f'", G_VECTOR (OFS_PARM0)[0], G_VECTOR (OFS_PARM0)[1], G_VECTOR (OFS_PARM0)[2]);
	G_INT (OFS_RETURN) = pr_string_temp - SVProgs->Strings;
}

void PF_etos (void)
{
	char *pr_string_temp = PR_GetTempString ();

	_snprintf (pr_string_temp, 128, "entity %i", G_EDICTNUM (OFS_PARM0));
	G_INT (OFS_RETURN) = pr_string_temp - SVProgs->Strings;
}

void PF_Spawn (void)
{
	edict_t	*ed;
	ed = ED_Alloc (SVProgs);
	RETURN_EDICT (ed);
}

void MakeMeAStaticEntity (sizebuf_t *buf, edict_t *ent);

void PF_Remove (void)
{
	edict_t	*ed;

	ed = G_EDICT (OFS_PARM0);

	if (sv_permagibs.value)
	{
		// permanent gibs
		model_t *mod = sv.models[(int) ed->v.modelindex];

		if (mod)
		{
			if (mod->type == mod_alias)
			{
				if (!strnicmp (mod->name, "progs/gib", 9))
				{
					// Con_Printf ("wrote gib as new static entity to client\n");
					// MakeMeAStaticEntity (&sv.datagram, ed);
					return;
				}
			}
		}
	}

	ED_Free (ed);
}


// entity (entity start, .string field, string match) find = #5;
void PF_Find (void)
{
	int		e;
	int		f;
	char	*s, *t;
	edict_t	*ed;

	e = G_EDICTNUM (OFS_PARM0);
	f = G_INT (OFS_PARM1);
	s = G_STRING (OFS_PARM2);

	if (!s)
		SVProgs->RunError ("PF_Find: bad search string");

	for (e++; e < SVProgs->NumEdicts; e++)
	{
		ed = GetEdictForNumber (e);

		if (ed->free)
			continue;

		t = E_STRING (ed, f);

		if (!t)
			continue;

		if (!strcmp (t, s))
		{
			RETURN_EDICT (ed);
			return;
		}
	}

	RETURN_EDICT (SVProgs->EdictPointers[0]);
}


void PR_CheckEmptyString (char *s)
{
	if (s[0] <= ' ')
		SVProgs->RunError ("Bad string");
}

void PF_precache_file (void)
{
	// precache_file is only used to copy files with qcc, it does nothing
	G_INT (OFS_RETURN) = G_INT (OFS_PARM0);
}

void PF_precache_sound (void)
{
	char	*s;
	int		i;

	if (sv.state != ss_loading)
		SVProgs->RunError ("PF_Precache_*: Precache can only be done in spawn functions");

	s = G_STRING (OFS_PARM0);
	G_INT (OFS_RETURN) = G_INT (OFS_PARM0);
	PR_CheckEmptyString (s);

	for (i = 0; i < MAX_SOUNDS; i++)
	{
		if (!sv.sound_precache[i])
		{
			sv.sound_precache[i] = s;
			return;
		}

		if (!strcmp (sv.sound_precache[i], s))
			return;
	}

	SVProgs->RunError ("PF_precache_sound: overflow");
}

void PF_precache_model (void)
{
	char	*s;
	int		i;

	if (sv.state != ss_loading)
		SVProgs->RunError ("PF_Precache_*: Precache can only be done in spawn functions");

	s = G_STRING (OFS_PARM0);
	G_INT (OFS_RETURN) = G_INT (OFS_PARM0);
	PR_CheckEmptyString (s);

	for (i = 0; i < MAX_MODELS; i++)
	{
		if (!sv.model_precache[i])
		{
			sv.model_precache[i] = s;
			sv.models[i] = Mod_ForName (s, true);
			return;
		}

		if (!strcmp (sv.model_precache[i], s))
			return;
	}

	SVProgs->RunError ("PF_precache_model: overflow");
}


void PF_coredump (void)
{
	ED_PrintEdicts ();
}

void PF_traceon (void)
{
	SVProgs->Trace = true;
}

void PF_traceoff (void)
{
	SVProgs->Trace = false;
}

void PF_eprint (void)
{
	ED_PrintNum (G_EDICTNUM (OFS_PARM0));
}

/*
===============
PF_walkmove

float(float yaw, float dist) walkmove
===============
*/
void PF_walkmove (void)
{
	edict_t	*ent;
	float	yaw, dist;
	vec3_t	move;
	dfunction_t	*oldf;
	int 	oldself;

	ent = PROG_TO_EDICT (SVProgs->GlobalStruct->self);
	yaw = G_FLOAT (OFS_PARM0);
	dist = G_FLOAT (OFS_PARM1);

	if (!((int) ent->v.flags & (FL_ONGROUND | FL_FLY | FL_SWIM)))
	{
		G_FLOAT (OFS_RETURN) = 0;
		return;
	}

	yaw = yaw * D3DX_PI * 2 / 360;

	move[0] = cos (yaw) * dist;
	move[1] = sin (yaw) * dist;
	move[2] = 0;

	// save program state, because SV_movestep may call other progs
	oldf = SVProgs->XFunction;
	oldself = SVProgs->GlobalStruct->self;

	G_FLOAT (OFS_RETURN) = SV_movestep (ent, move, true);

	// restore program state
	SVProgs->XFunction = oldf;
	SVProgs->GlobalStruct->self = oldself;
}

/*
===============
PF_droptofloor

void() droptofloor
===============
*/
void PF_droptofloor (void)
{
	edict_t		*ent;
	vec3_t		end;
	trace_t		trace;

	ent = PROG_TO_EDICT (SVProgs->GlobalStruct->self);

	VectorCopy (ent->v.origin, end);
	end[2] -= 256;

	trace = SV_Move (ent->v.origin, ent->v.mins, ent->v.maxs, end, false, ent);

	if (trace.fraction == 1 || trace.allsolid)
		G_FLOAT (OFS_RETURN) = 0;
	else
	{
		VectorCopy (trace.endpos, ent->v.origin);
		SV_LinkEdict (ent, false);
		ent->v.flags = (int) ent->v.flags | FL_ONGROUND;
		ent->v.groundentity = EDICT_TO_PROG (trace.ent);
		G_FLOAT (OFS_RETURN) = 1;
	}
}

/*
===============
PF_lightstyle

void(float style, string value) lightstyle
===============
*/
void PF_lightstyle (void)
{
	int		style;
	char	*val;
	client_t	*client;
	int			j;

	style = G_FLOAT (OFS_PARM0);
	val = G_STRING (OFS_PARM1);

	// change the string in sv
	sv.lightstyles[style] = val;

	// send message to all clients on this server
	if (sv.state != ss_active)
		return;

	for (j = 0, client = svs.clients; j < svs.maxclients; j++, client++)
		if (client->active || client->spawned)
		{
			MSG_WriteChar (&client->message, svc_lightstyle);
			MSG_WriteChar (&client->message, style);
			MSG_WriteString (&client->message, val);
		}
}

void PF_rint (void)
{
	float	f;
	f = G_FLOAT (OFS_PARM0);

	if (f > 0)
		G_FLOAT (OFS_RETURN) = (int) (f + 0.5);
	else
		G_FLOAT (OFS_RETURN) = (int) (f - 0.5);
}
void PF_floor (void)
{
	G_FLOAT (OFS_RETURN) = floor (G_FLOAT (OFS_PARM0));
}
void PF_ceil (void)
{
	G_FLOAT (OFS_RETURN) = ceil (G_FLOAT (OFS_PARM0));
}


/*
=============
PF_checkbottom
=============
*/
void PF_checkbottom (void)
{
	edict_t	*ent;

	ent = G_EDICT (OFS_PARM0);

	G_FLOAT (OFS_RETURN) = SV_CheckBottom (ent);
}

/*
=============
PF_pointcontents
=============
*/
void PF_pointcontents (void)
{
	float	*v;

	v = G_VECTOR (OFS_PARM0);

	G_FLOAT (OFS_RETURN) = SV_PointContents (v);
}

/*
=============
PF_nextent

entity nextent(entity)
=============
*/
void PF_nextent (void)
{
	int		i;
	edict_t	*ent;

	i = G_EDICTNUM (OFS_PARM0);

	while (1)
	{
		i++;

		if (i == SVProgs->NumEdicts)
		{
			RETURN_EDICT (SVProgs->EdictPointers[0]);
			return;
		}

		ent = GetEdictForNumber (i);

		if (!ent->free)
		{
			RETURN_EDICT (ent);
			return;
		}
	}
}


/*
=============
PF_aim

Pick a vector for the player to shoot along
vector aim(entity, missilespeed)
=============
*/
cvar_t	sv_aim ("sv_aim", "0.93");

void PF_aim (void)
{
	edict_t	*ent, *check, *bestent;
	vec3_t	start, dir, end, bestdir;
	int		i, j;
	trace_t	tr;
	float	dist, bestdist;
	float	speed;

	ent = G_EDICT (OFS_PARM0);
	speed = G_FLOAT (OFS_PARM1);

	VectorCopy (ent->v.origin, start);
	start[2] += 20;

	// try sending a trace straight
	VectorCopy (SVProgs->GlobalStruct->v_forward, dir);
	VectorMultiplyAdd (start, 2048, dir, end);
	tr = SV_Move (start, vec3_origin, vec3_origin, end, false, ent);

	if (tr.ent && tr.ent->v.takedamage == DAMAGE_AIM
			&& (!teamplay.value || ent->v.team <= 0 || ent->v.team != tr.ent->v.team))
	{
		VectorCopy (SVProgs->GlobalStruct->v_forward, G_VECTOR (OFS_RETURN));
		return;
	}

	// try all possible entities
	VectorCopy (dir, bestdir);
	bestdist = sv_aim.value;
	bestent = NULL;

	check = NEXT_EDICT (SVProgs->EdictPointers[0]);

	for (i = 1; i < SVProgs->NumEdicts; i++, check = NEXT_EDICT (check))
	{
		if (check->v.takedamage != DAMAGE_AIM)
			continue;

		if (check == ent)
			continue;

		if (teamplay.value && ent->v.team > 0 && ent->v.team == check->v.team)
			continue;	// don't aim at teammate

		for (j = 0; j < 3; j++)
			end[j] = check->v.origin[j]
					 + 0.5 * (check->v.mins[j] + check->v.maxs[j]);

		VectorSubtract (end, start, dir);
		VectorNormalize (dir);
		dist = DotProduct (dir, SVProgs->GlobalStruct->v_forward);

		if (dist < bestdist)
			continue;	// to far to turn

		tr = SV_Move (start, vec3_origin, vec3_origin, end, false, ent);

		if (tr.ent == check)
		{
			// can shoot at this one
			bestdist = dist;
			bestent = check;
		}
	}

	if (bestent)
	{
		VectorSubtract (bestent->v.origin, ent->v.origin, dir);
		dist = DotProduct (dir, SVProgs->GlobalStruct->v_forward);
		VectorScale (SVProgs->GlobalStruct->v_forward, dist, end);
		end[2] = dir[2];
		VectorNormalize (end);
		VectorCopy (end, G_VECTOR (OFS_RETURN));
	}
	else
	{
		VectorCopy (bestdir, G_VECTOR (OFS_RETURN));
	}
}

/*
==============
PF_changeyaw

This was a major timewaster in progs, so it was converted to C
==============
*/
void PF_changeyaw (void)
{
	edict_t		*ent;
	float		ideal, current, move, speed;

	ent = PROG_TO_EDICT (SVProgs->GlobalStruct->self);
	current = anglemod (ent->v.angles[1]);
	ideal = ent->v.ideal_yaw;
	speed = ent->v.yaw_speed;

	if (current == ideal)
		return;

	move = ideal - current;

	if (ideal > current)
	{
		if (move >= 180)
			move = move - 360;
	}
	else
	{
		if (move <= -180)
			move = move + 360;
	}

	if (move > 0)
	{
		if (move > speed)
			move = speed;
	}
	else
	{
		if (move < -speed)
			move = -speed;
	}

	ent->v.angles[1] = anglemod (current + move);
}

/*
==============
PF_changepitch
==============
*/
void PF_changepitch (void)
{
	edict_t	*ent;
	float	ideal, current, move, speed;
	eval_t	*val;

	ent = G_EDICT (OFS_PARM0);

	if (ent == SVProgs->EdictPointers[0])
	{
		// attempt to modify the world entity; just fail silently
		// EdictErr ("PF_changepitch", "modify", true, ent);
		return;
	}

	current = anglemod (ent->v.angles[0]);

	extern int ed_idealpitch;
	extern int ed_pitch_speed;

	if (val = GETEDICTFIELDVALUEFAST (ent, ed_idealpitch))
		ideal = val->_float;
	else
	{
		SVProgs->RunError ("PF_changepitch: .float idealpitch and .float pitch_speed must be defined to use changepitch");
		return;
	}

	if (val = GETEDICTFIELDVALUEFAST (ent, ed_pitch_speed))
		speed = val->_float;
	else
	{
		SVProgs->RunError ("PF_changepitch: .float idealpitch and .float pitch_speed must be defined to use changepitch");
		return;
	}

	if (current == ideal)
		return;

	move = ideal - current;

	if (ideal > current)
	{
		if (move >= 180)
			move = move - 360;
	}
	else
	{
		if (move <= -180)
			move = move + 360;
	}

	if (move > 0)
	{
		if (move > speed)
			move = speed;
	}
	else
	{
		if (move < -speed)
			move = -speed;
	}

	ent->v.angles[0] = anglemod (current + move);
}


/*
===============================================================================

MESSAGE WRITING

===============================================================================
*/

#define	MSG_BROADCAST	0		// unreliable to all
#define	MSG_ONE			1		// reliable to one (msg_entity)
#define	MSG_ALL			2		// reliable to all
#define	MSG_INIT		3		// write to the init string

sizebuf_t *WriteDest (void)
{
	int		entnum;
	int		dest;
	edict_t	*ent;

	dest = G_FLOAT (OFS_PARM0);

	switch (dest)
	{
	case MSG_BROADCAST:
		return &sv.datagram;

	case MSG_ONE:
		ent = PROG_TO_EDICT (SVProgs->GlobalStruct->msg_entity);
		entnum = GetNumberForEdict (ent);

		if (entnum < 1 || entnum > svs.maxclients)
			SVProgs->RunError ("WriteDest: not a client");

		return &svs.clients[entnum-1].message;

	case MSG_ALL:
		return &sv.reliable_datagram;

	case MSG_INIT:
		return &sv.signon;

	default:
		SVProgs->RunError ("WriteDest: bad destination");
		break;
	}

	return NULL;
}

void PF_WriteByte (void)
{
	MSG_WriteByte (WriteDest(), G_FLOAT (OFS_PARM1));
}

void PF_WriteChar (void)
{
	MSG_WriteChar (WriteDest(), G_FLOAT (OFS_PARM1));
}

void PF_WriteShort (void)
{
	MSG_WriteShort (WriteDest(), G_FLOAT (OFS_PARM1));
}

void PF_WriteLong (void)
{
	MSG_WriteLong (WriteDest(), G_FLOAT (OFS_PARM1));
}

void PF_WriteAngle (void)
{
	MSG_WriteAngle (WriteDest(), G_FLOAT (OFS_PARM1), sv.Protocol, sv.PrototcolFlags, 0);
}

void PF_WriteCoord (void)
{
	MSG_WriteCoord (WriteDest(), G_FLOAT (OFS_PARM1), sv.Protocol, sv.PrototcolFlags);
}

void PF_WriteString (void)
{
	MSG_WriteString (WriteDest(), G_STRING (OFS_PARM1));
}


void PF_WriteEntity (void)
{
	MSG_WriteShort (WriteDest(), G_EDICTNUM (OFS_PARM1));
}

//=============================================================================

int SV_ModelIndex (char *name);

void MakeMeAStaticEntity (sizebuf_t *buf, edict_t *ent)
{
	int		i;
	int		bits = 0;

	if (sv.Protocol == PROTOCOL_VERSION_FITZ || sv.Protocol == PROTOCOL_VERSION_RMQ)
	{
		// never send alpha
		if (SV_ModelIndex (SVProgs->Strings + ent->v.model) & 0xFF00) bits |= B_LARGEMODEL;
		if ((int) (ent->v.frame) & 0xFF00) bits |= B_LARGEFRAME;
		if (ent->alpha < 255) bits |= B_ALPHA;
	}
	else
	{
		if (SV_ModelIndex (SVProgs->Strings + ent->v.model) & 0xFF00 || (int) (ent->v.frame) & 0xFF00)
		{
			// can't display the correct model & frame, so don't show it at all
			ED_Free (ent);
			return;
		}
	}

	if (bits)
	{
		MSG_WriteByte (buf, svc_spawnstatic2);
		MSG_WriteByte (buf, bits);
	}
	else MSG_WriteByte (buf, svc_spawnstatic);

	if (bits & B_LARGEMODEL)
		MSG_WriteShort (buf, SV_ModelIndex (SVProgs->Strings + ent->v.model));
	else SV_WriteByteShort (buf, SV_ModelIndex (SVProgs->Strings + ent->v.model));

	if (bits & B_LARGEFRAME)
		MSG_WriteShort (buf, ent->v.frame);
	else MSG_WriteByte (buf, ent->v.frame);

	MSG_WriteByte (buf, ent->v.colormap);
	MSG_WriteByte (buf, ent->v.skin);

	for (i = 0; i < 3; i++)
	{
		MSG_WriteCoord (buf, ent->v.origin[i], sv.Protocol, sv.PrototcolFlags);
		MSG_WriteAngle (buf, ent->v.angles[i], sv.Protocol, sv.PrototcolFlags, i);
	}

	if (bits & B_ALPHA) MSG_WriteByte (buf, ent->alpha);

	// throw the entity away now
	ED_Free (ent);
}


void PF_makestatic (void)
{
	edict_t	*ent = G_EDICT (OFS_PARM0);

	MakeMeAStaticEntity (&sv.signon, ent);
}

//=============================================================================

/*
==============
PF_setspawnparms
==============
*/
void PF_setspawnparms (void)
{
	edict_t	*ent;
	int		i;
	client_t	*client;

	ent = G_EDICT (OFS_PARM0);
	i = GetNumberForEdict (ent);

	if (i < 1 || i > svs.maxclients)
		SVProgs->RunError ("Entity is not a client");

	// copy spawn parms out of the client_t
	client = svs.clients + (i - 1);

	for (i = 0; i < NUM_SPAWN_PARMS; i++)
		(&SVProgs->GlobalStruct->parm1)[i] = client->spawn_parms[i];
}

/*
==============
PF_changelevel
==============
*/
void PF_changelevel (void)
{
	char	*s;

	// make sure we don't issue two changelevels
	if (svs.changelevel_issued) return;

	svs.changelevel_issued = true;

	s = G_STRING (OFS_PARM0);

	Cbuf_AddText (va ("changelevel %s\n", s));
}


void PF_sin (void)
{
	G_FLOAT (OFS_RETURN) = sin (G_FLOAT (OFS_PARM0));
}

void PF_cos (void)
{
	G_FLOAT (OFS_RETURN) = cos (G_FLOAT (OFS_PARM0));
}

void PF_sqrt (void)
{
	G_FLOAT (OFS_RETURN) = sqrt (G_FLOAT (OFS_PARM0));
}


void PF_Fixme (void)
{
	// don't crash
	// Con_SafePrintf ("PF_Fixme: unimplemented bulitin\n");
}


// 2001-09-14 Enhanced BuiltIn Function System (EBFS) by Maddes  start
/*
=================
PF_builtin_find

float builtin_find (string)
=================
*/
void PF_builtin_find (void)
{
	int		j;
	float	funcno;
	char	*funcname;

	funcno = 0;
	funcname = G_STRING (OFS_PARM0);

	// search function name
	for (j = 1; j < pr_ebfs_numbuiltins; j++)
	{
		if ((pr_ebfs_builtins[j].funcname) && (!(stricmp (funcname, pr_ebfs_builtins[j].funcname))))
		{
			break;	// found
		}
	}

	if (j < pr_ebfs_numbuiltins)
	{
		funcno = pr_ebfs_builtins[j].funcno;
	}

	G_FLOAT (OFS_RETURN) = funcno;
}


void PF_te_particlerain (void)
{
	if (G_FLOAT (OFS_PARM3) < 1) return;

	MSG_WriteByte (&sv.datagram, svc_temp_entity);
	MSG_WriteByte (&sv.datagram, TE_PARTICLERAIN);

	// min
	MSG_WriteCoord (&sv.datagram, G_VECTOR (OFS_PARM0)[0], sv.Protocol, sv.PrototcolFlags);
	MSG_WriteCoord (&sv.datagram, G_VECTOR (OFS_PARM0)[1], sv.Protocol, sv.PrototcolFlags);
	MSG_WriteCoord (&sv.datagram, G_VECTOR (OFS_PARM0)[2], sv.Protocol, sv.PrototcolFlags);

	// max
	MSG_WriteCoord (&sv.datagram, G_VECTOR (OFS_PARM1)[0], sv.Protocol, sv.PrototcolFlags);
	MSG_WriteCoord (&sv.datagram, G_VECTOR (OFS_PARM1)[1], sv.Protocol, sv.PrototcolFlags);
	MSG_WriteCoord (&sv.datagram, G_VECTOR (OFS_PARM1)[2], sv.Protocol, sv.PrototcolFlags);

	// velocity
	MSG_WriteCoord (&sv.datagram, G_VECTOR (OFS_PARM2)[0], sv.Protocol, sv.PrototcolFlags);
	MSG_WriteCoord (&sv.datagram, G_VECTOR (OFS_PARM2)[1], sv.Protocol, sv.PrototcolFlags);
	MSG_WriteCoord (&sv.datagram, G_VECTOR (OFS_PARM2)[2], sv.Protocol, sv.PrototcolFlags);

	// count - already tested for < 1 above
	MSG_WriteShort (&sv.datagram, G_FLOAT (OFS_PARM3) > 65535 ? 65535 : G_FLOAT (OFS_PARM3));

	// color
	MSG_WriteByte (&sv.datagram, G_FLOAT (OFS_PARM4));
}


void PF_te_particlesnow (void)
{
	if (G_FLOAT (OFS_PARM3) < 1) return;

	MSG_WriteByte (&sv.datagram, svc_temp_entity);
	MSG_WriteByte (&sv.datagram, TE_PARTICLESNOW);

	// min
	MSG_WriteCoord (&sv.datagram, G_VECTOR (OFS_PARM0)[0], sv.Protocol, sv.PrototcolFlags);
	MSG_WriteCoord (&sv.datagram, G_VECTOR (OFS_PARM0)[1], sv.Protocol, sv.PrototcolFlags);
	MSG_WriteCoord (&sv.datagram, G_VECTOR (OFS_PARM0)[2], sv.Protocol, sv.PrototcolFlags);

	// max
	MSG_WriteCoord (&sv.datagram, G_VECTOR (OFS_PARM1)[0], sv.Protocol, sv.PrototcolFlags);
	MSG_WriteCoord (&sv.datagram, G_VECTOR (OFS_PARM1)[1], sv.Protocol, sv.PrototcolFlags);
	MSG_WriteCoord (&sv.datagram, G_VECTOR (OFS_PARM1)[2], sv.Protocol, sv.PrototcolFlags);

	// velocity
	MSG_WriteCoord (&sv.datagram, G_VECTOR (OFS_PARM2)[0], sv.Protocol, sv.PrototcolFlags);
	MSG_WriteCoord (&sv.datagram, G_VECTOR (OFS_PARM2)[1], sv.Protocol, sv.PrototcolFlags);
	MSG_WriteCoord (&sv.datagram, G_VECTOR (OFS_PARM2)[2], sv.Protocol, sv.PrototcolFlags);

	// count - already tested for < 1 above
	MSG_WriteShort (&sv.datagram, G_FLOAT (OFS_PARM3) > 65535 ? 65535 : G_FLOAT (OFS_PARM3));

	// color
	MSG_WriteByte (&sv.datagram, G_FLOAT (OFS_PARM4));
}


/*
=================
PF_stof

float stof (string)
=================
*/

// thanks Zoid, taken from QuakeWorld
void PF_stof (void)
{
	char	*s;

	s = G_STRING (OFS_PARM0);
	G_FLOAT (OFS_RETURN) = atof (s);
}


/*
=================
PF_stov

vector stov (string)
=================
*/
void PF_stov (void)
{
	char *v;
	int i;
	vec3_t d;

	v = G_STRING (OFS_PARM0);

	for (i = 0; i < 3; i++)
	{
		while (v && (v[0] == ' ' || v[0] == '\'')) //skip unneeded data
			v++;

		d[i] = atof (v);

		while (v && v[0] != ' ') // skip to next space
			v++;
	}

	VectorCopy2 (G_VECTOR (OFS_RETURN), d);
}


// 2001-09-20 QuakeC string manipulation by FrikaC/Maddes  start
/*
=================
PF_strzone

string strzone (string)
=================
*/
void PF_strzone (void)
{
	char *m, *p;

	m = G_STRING (OFS_PARM0);
	p = (char *) Zone_Alloc (strlen (m) + 1);

	strcpy (p, m);

	G_INT (OFS_RETURN) = p - SVProgs->Strings;
}


/*
=================
PF_strunzone

string strunzone (string)
=================
*/
void PF_strunzone (void)
{
	// the compiler freaks if I pass this directly into Zone_Free...
	void *blah = (void *) (G_STRING (OFS_PARM0));

	// fixme - was this a crash condition in the original implementation?
	// if so, find out why and fix it!
	Zone_Free (blah);

	G_INT (OFS_PARM0) = OFS_NULL; // empty the def
};


/*
=================
PF_strlen

float strlen (string)
=================
*/
void PF_strlen (void)
{
	char *p = G_STRING (OFS_PARM0);

	G_FLOAT (OFS_RETURN) = strlen (p);
}


/*
=================
PF_strcat

string strcat (string, string)
=================
*/
void PF_strcat (void)
{
	char *s1, *s2;
	int		maxlen;	// 2001-10-25 Enhanced temp string handling by Maddes
	char	*pr_string_temp = PR_GetTempString ();

	s1 = G_STRING (OFS_PARM0);
	s2 = PF_VarString (1);

	// 2001-10-25 Enhanced temp string handling by Maddes  start
	pr_string_temp[0] = 0;

	if (strlen (s1) < PR_MAX_TEMP_STRING)
	{
		strcpy (pr_string_temp, s1);
	}
	else
	{
		strncpy (pr_string_temp, s1, PR_MAX_TEMP_STRING);
		pr_string_temp[PR_MAX_TEMP_STRING - 1] = 0;
	}

	maxlen = PR_MAX_TEMP_STRING - strlen (pr_string_temp) - 1;	// -1 is EndOfString

	if (maxlen > 0)
	{
		if (maxlen > strlen (s2))
		{
			strcat (pr_string_temp, s2);
		}
		else
		{
			strncat (pr_string_temp, s2, maxlen);
			pr_string_temp[PR_MAX_TEMP_STRING - 1] = 0;
		}
	}

	// 2001-10-25 Enhanced temp string handling by Maddes  end
	G_INT (OFS_RETURN) = pr_string_temp - SVProgs->Strings;
}


/*
=================
PF_substring

string substring (string, float, float)
=================
*/
void PF_substring (void)
{
	int		offset, length;
	int		maxoffset;		// 2001-10-25 Enhanced temp string handling by Maddes
	char	*p;
	char	*pr_string_temp = PR_GetTempString ();

	p = G_STRING (OFS_PARM0);

	offset = (int) G_FLOAT (OFS_PARM1); // for some reason, Quake doesn't like G_INT
	length = (int) G_FLOAT (OFS_PARM2);

	// cap values
	maxoffset = strlen (p);

	if (offset > maxoffset)
	{
		offset = maxoffset;
	}

	if (offset < 0)
		offset = 0;

	// 2001-10-25 Enhanced temp string handling by Maddes  start
	if (length >= PR_MAX_TEMP_STRING)
		length = PR_MAX_TEMP_STRING - 1;

	// 2001-10-25 Enhanced temp string handling by Maddes  end

	if (length < 0)
		length = 0;

	p += offset;

	strncpy (pr_string_temp, p, length);
	pr_string_temp[length] = 0;

	G_INT (OFS_RETURN) = pr_string_temp - SVProgs->Strings;
}
// 2001-09-20 QuakeC string manipulation by FrikaC/Maddes  end


// mh - fixes many things wrong with FRIK_FILE - no longer uses the Zone as a temp buffer for appending files,
// supports proper appending (why FRIK_FILE just didn't have a Sys_FileOpenAppend I'll never know), uses FILE *
// pointers instead of the handles system (but maintains compatibility), etc.

// for handy switching between values.
typedef union
{
	float flt;
	int i;
	FILE *f;
} QFILE;

// 2001-09-20 QuakeC file access by FrikaC/Maddes  start
/*
=================
PF_fopen

float fopen (string,float)
=================
*/
void PF_fopen (void)
{
	char *p = G_STRING (OFS_PARM0);
	int fmode = G_FLOAT (OFS_PARM1);
	QFILE f = {0};

	switch (fmode)
	{
	case 0: // read
		f.f = fopen (va ("%s/%s", com_gamedir, p), "rb");
		break;

	case 1: // append
		f.f = fopen (va ("%s/%s", com_gamedir, p), "ab");
		break;

	default: // write
		f.f = fopen (va ("%s/%s", com_gamedir, p), "wb");
		break;
	}

	// this madness is required for compatibility with the old handle based system that set a handle of -1 if it couldn't open a file
	G_FLOAT (OFS_RETURN) = f.f ? f.flt : -1;
}


/*
=================
PF_fclose

void fclose (float)
=================
*/
void PF_fclose (void)
{
	QFILE f = {0};

	f.flt = G_FLOAT (OFS_PARM0);
	fclose (f.f);
}


/*
=================
PF_fgets

string fgets (float)
=================
*/
void PF_fgets (void)
{
	// reads one line (up to a \n) into a string
	int		i;
	int		count;
	char	buffer;
	char	*pr_string_temp = PR_GetTempString ();
	QFILE	f = {0};

	f.flt = G_FLOAT (OFS_PARM0);
	count = fread (&buffer, 1, 1, f.f);

	if (count && buffer == '\r')	// carriage return
	{
		count = fread (&buffer, 1, 1, f.f);
	}

	if (!count)	// EndOfFile
	{
		G_INT (OFS_RETURN) = OFS_NULL;	// void string
		return;
	}

	i = 0;

	while (count && buffer != '\n')
	{
		if (i < PR_MAX_TEMP_STRING - 1)	// no place for character in temp string
		{
			pr_string_temp[i++] = buffer;
		}

		// read next character
		count = fread (&buffer, 1, 1, f.f);

		if (count && buffer == '\r')	// carriage return
		{
			count = fread (&buffer, 1, 1, f.f);
		}
	}

	pr_string_temp[i] = 0;

	G_INT (OFS_RETURN) = pr_string_temp - SVProgs->Strings;
}


/*
=================
PF_fputs

void fputs (float,string)
=================
*/
void PF_fputs (void)
{
	// writes to file, like bprint
	QFILE f = {0};
	f.flt = G_FLOAT (OFS_PARM0);
	char *str = PF_VarString (1);

	fwrite (str, strlen (str), 1, f.f);
}
// 2001-09-20 QuakeC file access by FrikaC/Maddes  end


// 2001-09-14 Enhanced BuiltIn Function System (EBFS) by Maddes  start
/*
=============
PR_BuiltInList_f

For debugging, prints all builtin functions with assigned and default number
=============
*/
void PR_BuiltInList_f (void)
{
	int		i;
	char	*partial;
	int		len;
	int		count;

	if (Cmd_Argc () > 1)
	{
		partial = Cmd_Argv (1);
		len = strlen (partial);
	}
	else
	{
		partial = NULL;
		len = 0;
	}

	count = 0;

	for (i = 1; i < pr_ebfs_numbuiltins; i++)
	{
		if (partial && strnicmp (partial, pr_ebfs_builtins[i].funcname, len))
		{
			continue;
		}

		count++;
		Con_Printf ("%i(%i): %s\n", pr_ebfs_builtins[i].funcno, pr_ebfs_builtins[i].default_funcno, pr_ebfs_builtins[i].funcname);
	}

	Con_Printf ("------------\n");

	if (partial)
	{
		Con_Printf ("%i beginning with \"%s\" out of ", count, partial);
	}

	Con_Printf ("%i builtin functions\n", i);
}

cmd_t PR_BuiltInList_Cmd ("builtinlist", PR_BuiltInList_f);
// 2001-09-14 Enhanced BuiltIn Function System (EBFS) by Maddes  end


// 2001-09-14 Enhanced BuiltIn Function System (EBFS) by Maddes  end
// 2001-09-14 Enhanced BuiltIn Function System (EBFS) by Maddes  start
// for builtin function definitions see Quake Standards Group at http://www.quakesrc.org/
ebfs_builtin_t pr_ebfs_builtins[] =
{
	{   0, NULL, PF_Fixme},		// has to be first entry as it is needed for initialization in PR_LoadProgs()
	{   1, "makevectors", PF_makevectors},	// void(entity e)	makevectors 		= #1;
	{   2, "setorigin", PF_setorigin},		// void(entity e, vector o) setorigin	= #2;
	{   3, "setmodel", PF_setmodel},		// void(entity e, string m) setmodel	= #3;
	{   4, "setsize", PF_setsize},			// void(entity e, vector min, vector max) setsize = #4;
	//	{   5, "fixme", PF_Fixme},				// void(entity e, vector min, vector max) setabssize = #5;
	{   6, "break", PF_break},				// void() break						= #6;
	{   7, "random", PF_random},			// float() random						= #7;
	{   8, "sound", PF_sound},				// void(entity e, float chan, string samp) sound = #8;
	{   9, "normalize", PF_normalize},		// vector(vector v) normalize			= #9;
	{  10, "error", PF_error},				// void(string e) error				= #10;
	{  11, "objerror", PF_objerror},		// void(string e) objerror				= #11;
	{  12, "vlen", PF_vlen},				// float(vector v) vlen				= #12;
	{  13, "vectoyaw", PF_vectoyaw},		// float(vector v) vectoyaw		= #13;
	{  14, "spawn", PF_Spawn},				// entity() spawn						= #14;
	{  15, "remove", PF_Remove},			// void(entity e) remove				= #15;
	{  16, "traceline", PF_traceline},		// float(vector v1, vector v2, float tryents) traceline = #16;
	{  17, "checkclient", PF_checkclient},	// entity() clientlist					= #17;
	{  18, "find", PF_Find},				// entity(entity start, .string fld, string match) find = #18;
	{  19, "precache_sound", PF_precache_sound},	// void(string s) precache_sound		= #19;
	{  20, "precache_model", PF_precache_model},	// void(string s) precache_model		= #20;
	{  21, "stuffcmd", PF_stuffcmd},		// void(entity client, string s)stuffcmd = #21;
	{  22, "findradius", PF_findradius},	// entity(vector org, float rad) findradius = #22;
	{  23, "bprint", PF_bprint},			// void(string s) bprint				= #23;
	{  24, "sprint", PF_sprint},			// void(entity client, string s) sprint = #24;
	{  25, "dprint", PF_dprint},			// void(string s) dprint				= #25;
	{  26, "ftos", PF_ftos},				// void(string s) ftos				= #26;
	{  27, "vtos", PF_vtos},				// void(string s) vtos				= #27;
	{  28, "coredump", PF_coredump},
	{  29, "traceon", PF_traceon},
	{  30, "traceoff", PF_traceoff},
	{  31, "eprint", PF_eprint},			// void(entity e) debug print an entire entity
	{  32, "walkmove", PF_walkmove},		// float(float yaw, float dist) walkmove
	//	{  33, "fixme", PF_Fixme},				// float(float yaw, float dist) walkmove
	{  34, "droptofloor", PF_droptofloor},
	{  35, "lightstyle", PF_lightstyle},
	{  36, "rint", PF_rint},
	{  37, "floor", PF_floor},
	{  38, "ceil", PF_ceil},
	//	{  39, "fixme", PF_Fixme},
	{  40, "checkbottom", PF_checkbottom},
	{  41, "pointcontents", PF_pointcontents},
	//	{  42, "fixme", PF_Fixme},
	{  43, "fabs", PF_fabs},
	{  44, "aim", PF_aim},
	{  45, "cvar", PF_cvar},
	{  46, "localcmd", PF_localcmd},
	{  47, "nextent", PF_nextent},
	{  48, "particle", PF_particle},
	{  49, "ChangeYaw", PF_changeyaw},
	//	{  50, "fixme", PF_Fixme},
	{  51, "vectoangles", PF_vectoangles},
	{  52, "WriteByte", PF_WriteByte},
	{  53, "WriteChar", PF_WriteChar},
	{  54, "WriteShort", PF_WriteShort},
	{  55, "WriteLong", PF_WriteLong},
	{  56, "WriteCoord", PF_WriteCoord},
	{  57, "WriteAngle", PF_WriteAngle},
	{  58, "WriteString", PF_WriteString},
	{  59, "WriteEntity", PF_WriteEntity},
#ifdef QUAKE2
	{  60, "sin", PF_sin},
	{  61, "cos", PF_cos},
	{  62, "sqrt", PF_sqrt},
	{  63, "changepitch", PF_changepitch},
#endif
	// nehahra needs this
	{  64, "TraceToss", PF_TraceToss},
#ifdef QUAKE2
	{  65, "etos", PF_etos},
	{  66, "WaterMove", PF_WaterMove},
#endif
	{  67, "movetogoal", SV_MoveToGoal},
	{  68, "precache_file", PF_precache_file},
	{  69, "makestatic", PF_makestatic},
	{  70, "changelevel", PF_changelevel},
	//	{  71, "fixme", PF_Fixme},
	{  72, "cvar_set", PF_cvar_set},
	{  73, "centerprint", PF_centerprint},
	{  74, "ambientsound", PF_ambientsound},
	{  75, "precache_model2", PF_precache_model},
	{  76, "precache_sound2", PF_precache_sound},	// precache_sound2 is different only for qcc
	{  77, "precache_file2", PF_precache_file},
	{  78, "setspawnparms", PF_setspawnparms},
	{  81, "stof", PF_stof},	// 2001-09-20 QuakeC string manipulation by FrikaC/Maddes
	// 2001-11-15 DarkPlaces general builtin functions by Lord Havoc  start
	// not implemented yet
	/*
		{  90, "tracebox", PF_tracebox},
		{  91, "randomvec", PF_randomvec},
		{  92, "getlight", PF_GetLight},	// not implemented yet
		{  93, "cvar_create", PF_cvar_create},		// 2001-09-18 New BuiltIn Function: cvar_create() by Maddes
		{  94, "fmin", PF_fmin},
		{  95, "fmax", PF_fmax},
		{  96, "fbound", PF_fbound},
		{  97, "fpow", PF_fpow},
		{  98, "findfloat", PF_FindFloat},
	*/
	{ PR_DEFAULT_FUNCNO_EXTENSION_FIND, "extension_find", PF_extension_find},	// 2001-10-20 Extension System by Lord Havoc/Maddes
	/*
		{   0, "registercvar", PF_cvar_create},	// 0 indicates that this entry is just for remapping (because of name change)
	*/
	{   0, "checkextension", PF_extension_find},
	// 2001-11-15 DarkPlaces general builtin functions by Lord Havoc  end
	{ PR_DEFAULT_FUNCNO_BUILTIN_FIND, "builtin_find", PF_builtin_find},	// 2001-09-14 Enhanced BuiltIn Function System (EBFS) by Maddes
	// not implemented yet
	/*
		{ 101, "cmd_find", PF_cmd_find},		// 2001-09-16 New BuiltIn Function: cmd_find() by Maddes
		{ 102, "cvar_find", PF_cvar_find},		// 2001-09-16 New BuiltIn Function: cvar_find() by Maddes
		{ 103, "cvar_string", PF_cvar_string},	// 2001-09-16 New BuiltIn Function: cvar_string() by Maddes
		{ 105, "cvar_free", PF_cvar_free},		// 2001-09-18 New BuiltIn Function: cvar_free() by Maddes
		{ 106, "NVS_InitSVCMsg", PF_NVS_InitSVCMsg},	// 2000-05-02 NVS SVC by Maddes
		{ 107, "WriteFloat", PF_WriteFloat},	// 2001-09-16 New BuiltIn Function: WriteFloat() by Maddes
		{ 108, "etof", PF_etof},	// 2001-09-25 New BuiltIn Function: etof() by Maddes
		{ 109, "ftoe", PF_ftoe},	// 2001-09-25 New BuiltIn Function: ftoe() by Maddes
	*/
	// 2001-09-20 QuakeC file access by FrikaC/Maddes  start
	// not implemented yet
		{ 110, "fopen", PF_fopen},
		{ 111, "fclose", PF_fclose},
		{ 112, "fgets", PF_fgets},
		{ 113, "fputs", PF_fputs},
		{   0, "open", PF_fopen},		// 0 indicates that this entry is just for remapping (because of name and number change)
		{   0, "close", PF_fclose},
		{   0, "read", PF_fgets},
		{   0, "write", PF_fputs},
	// 2001-09-20 QuakeC file access by FrikaC/Maddes  end
	// 2001-09-20 QuakeC string manipulation by FrikaC/Maddes  start
	// not implemented yet
		{ 114, "strlen", PF_strlen},
		{ 115, "strcat", PF_strcat},
		{ 116, "substring", PF_substring},
	{ 117, "stov", PF_stov},
	{ 118, "strzone", PF_strzone},
	{ 119, "strunzone", PF_strunzone},
	{   0, "zone", PF_strzone},		// 0 indicates that this entry is just for remapping (because of name and number change)
	{   0, "unzone", PF_strunzone},
	// 2001-09-20 QuakeC string manipulation by FrikaC/Maddes  end
	// 2001-11-15 DarkPlaces general builtin functions by Lord Havoc  start
	// not implemented yet
	/*
		{ 400, "copyentity", PF_...},
		{ 401, "setcolor", PF_...},
		{ 402, "findchain", PF_...},
		{ 403, "findchainfloat", PF_...},
		{ 404, "effect", PF_...},
		{ 405, "te_blood", PF_...},
		{ 406, "te_bloodshower", PF_...},
		{ 407, "te_explosionrgb", PF_...},
		{ 408, "te_particlecube", PF_...},
		*/
	{409, "te_particlerain", PF_te_particlerain},
	{410, "te_particlesnow", PF_te_particlesnow},
	/*
	{ 411, "te_spark", PF_...},
	{ 412, "te_gunshotquad", PF_...},
	{ 413, "te_spikequad", PF_...},
	{ 414, "te_superspikequad", PF_...},
	{ 415, "te_explosionquad", PF_...},
	{ 416, "te_smallflash", PF_...},
	{ 417, "te_customflash", PF_...},
	{ 418, "te_gunshot", PF_...},
	{ 419, "te_spike", PF_...},
	{ 420, "te_superspike", PF_...},
	{ 421, "te_explosion", PF_...},
	{ 422, "te_tarexplosion", PF_...},
	{ 423, "te_wizspike", PF_...},
	{ 424, "te_knightspike", PF_...},
	{ 425, "te_lavasplash", PF_...},
	{ 426, "te_teleport", PF_...},
	{ 427, "te_explosion2", PF_...},
	{ 428, "te_lightning1", PF_...},
	{ 429, "te_lightning2", PF_...},
	{ 430, "te_lightning3", PF_...},
	{ 431, "te_beam", PF_...},
	{ 432, "vectorvectors", PF_...},
	*/
	// 2001-11-15 DarkPlaces general builtin functions by Lord Havoc  end
};

int pr_ebfs_numbuiltins = sizeof (pr_ebfs_builtins) / sizeof (pr_ebfs_builtins[0]);
// 2001-09-14 Enhanced BuiltIn Function System (EBFS) by Maddes  end

builtin_t *pr_builtins;
int pr_numbuiltins;

void PR_InitBuiltIns (void)
{
	// this is now done on progs load
}


