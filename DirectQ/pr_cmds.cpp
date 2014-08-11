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

#include "quakedef.h"

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


#define	RETURN_EDICT(e) (((int *)pr_globals)[OFS_RETURN] = EDICT_TO_PROG(e))

cvar_t pr_checkextension ("pr_checkextension", "1", CVAR_READONLY);

char *pr_extensions = NULL;

/*
===============================================================================

						TEMP STRINGS

	Help to prevent buffer overflows, memory access violations, and
	other fun by maintaining a rotating set of temp strings.

===============================================================================
*/

#define PR_MAX_TEMP_STRING		1024
#define PR_NUM_TEMP_STRINGS		32
#define PR_TEMP_STRING_MASK		(PR_NUM_TEMP_STRINGS - 1)

char *PR_GetTempString (void)
{
	static char **pr_temp_strings = NULL;
	static int pr_temp_string_num = 0;

	if (!pr_temp_strings)
	{
		// place these in the permanent pool cos they'll always be needed
		pr_temp_strings = (char **) Pool_Alloc (POOL_PERMANENT, sizeof (char *) * PR_NUM_TEMP_STRINGS);

		for (int i = 0; i < PR_NUM_TEMP_STRINGS; i++) pr_temp_strings[i] = (char *) Pool_Alloc (POOL_PERMANENT, PR_MAX_TEMP_STRING);
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

	for (int i = first, len = 0; i < pr_argc; i++)
	{
		char *append = G_STRING ((OFS_PARM0 + i * 3));

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

	s = PF_VarString(0);
	Con_Printf ("======SERVER ERROR in %s:\n%s\n", pr_strings + pr_xfunction->s_name, s);
	ed = PROG_TO_EDICT (pr_global_struct->self);
	ED_Print (ed);

	QC_DebugOutput ("======SERVER ERROR in %s:\n%s\n", pr_strings + pr_xfunction->s_name, s);
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
	
	s = PF_VarString(0);
	Con_Printf ("======OBJECT ERROR in %s:\n%s\n", pr_strings + pr_xfunction->s_name, s);
	ed = PROG_TO_EDICT(pr_global_struct->self);
	ED_Print (ed);
	ED_Free (ed);

	QC_DebugOutput ("======OBJECT ERROR in %s:\n%s\n", pr_strings + pr_xfunction->s_name, s);
	Host_Error ("Program error");
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
	AngleVectors (G_VECTOR(OFS_PARM0), pr_global_struct->v_forward, pr_global_struct->v_right, pr_global_struct->v_up);
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
	
	e = G_EDICT(OFS_PARM0);
	org = G_VECTOR(OFS_PARM1);
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
	
	for (i=0 ; i<3 ; i++)
		if (min[i] > max[i])
			PR_RunError ("backwards mins/maxs");

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
		
		a = angles[1]/180 * M_PI;
		
		xvector[0] = cos(a);
		xvector[1] = sin(a);
		yvector[0] = -sin(a);
		yvector[1] = cos(a);
		
		VectorCopy (min, bounds[0]);
		VectorCopy (max, bounds[1]);
		
		rmin[0] = rmin[1] = rmin[2] = 9999;
		rmax[0] = rmax[1] = rmax[2] = -9999;
		
		for (i=0 ; i<= 1 ; i++)
		{
			base[0] = bounds[i][0];
			for (j=0 ; j<= 1 ; j++)
			{
				base[1] = bounds[j][1];
				for (k=0 ; k<= 1 ; k++)
				{
					base[2] = bounds[k][2];
					
				// transform the point
					transformed[0] = xvector[0]*base[0] + yvector[0]*base[1];
					transformed[1] = xvector[1]*base[0] + yvector[1]*base[1];
					transformed[2] = base[2];
					
					for (l=0 ; l<3 ; l++)
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
	
	e = G_EDICT(OFS_PARM0);
	min = G_VECTOR(OFS_PARM1);
	max = G_VECTOR(OFS_PARM2);
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
		if (!sv.model_precache[i]) PR_RunError ("no precache: %s\n", m);
		if (!strcmp (sv.model_precache[i], m)) break;
	}

	e->v.model = m - pr_strings;
	e->v.modelindex = i;

	mod = sv.models[(int)e->v.modelindex];

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

	s = PF_VarString(0);
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
	
	entnum = G_EDICTNUM(OFS_PARM0);
	s = PF_VarString(1);
	
	if (entnum < 1 || entnum > svs.maxclients)
	{
		Con_Printf ("tried to sprint to a non-client\n");
		return;
	}
		
	client = &svs.clients[entnum-1];
		
	MSG_WriteChar (&client->message,svc_centerprint);
	MSG_WriteString (&client->message, s );
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
	
	value1 = G_VECTOR(OFS_PARM0);

	newval = value1[0] * value1[0] + value1[1] * value1[1] + value1[2]*value1[2];
	newval = sqrt(newval);
	
	if (newval == 0)
		newvalue[0] = newvalue[1] = newvalue[2] = 0;
	else
	{
		newval = 1/newval;
		newvalue[0] = value1[0] * newval;
		newvalue[1] = value1[1] * newval;
		newvalue[2] = value1[2] * newval;
	}
	
	VectorCopy (newvalue, G_VECTOR(OFS_RETURN));	
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
	
	value1 = G_VECTOR(OFS_PARM0);

	newval = value1[0] * value1[0] + value1[1] * value1[1] + value1[2]*value1[2];
	newval = sqrt(newval);
	
	G_FLOAT(OFS_RETURN) = newval;
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
	
	value1 = G_VECTOR(OFS_PARM0);

	if (value1[1] == 0 && value1[0] == 0)
		yaw = 0;
	else
	{
		yaw = (int) (atan2(value1[1], value1[0]) * 180 / M_PI);
		if (yaw < 0)
			yaw += 360;
	}

	G_FLOAT(OFS_RETURN) = yaw;
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
	
	value1 = G_VECTOR(OFS_PARM0);

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
		yaw = (int) (atan2(value1[1], value1[0]) * 180 / M_PI);
		if (yaw < 0)
			yaw += 360;

		forward = sqrt (value1[0]*value1[0] + value1[1]*value1[1]);
		pitch = (int) (atan2(value1[2], forward) * 180 / M_PI);
		if (pitch < 0)
			pitch += 360;
	}

	G_FLOAT(OFS_RETURN+0) = pitch;
	G_FLOAT(OFS_RETURN+1) = yaw;
	G_FLOAT(OFS_RETURN+2) = 0;
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
		
	num = (rand ()&0x7fff) / ((float)0x7fff);
	
	G_FLOAT(OFS_RETURN) = num;
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
			
	org = G_VECTOR(OFS_PARM0);
	dir = G_VECTOR(OFS_PARM1);
	color = G_FLOAT(OFS_PARM2);
	count = G_FLOAT(OFS_PARM3);
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
	samp = G_STRING(OFS_PARM1);
	vol = G_FLOAT(OFS_PARM2);
	attenuation = G_FLOAT(OFS_PARM3);

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
	if (soundnum > 255 && sv.Protocol == PROTOCOL_VERSION_FITZ)
		MSG_WriteByte (&sv.signon, svc_spawnstaticsound2);
	else MSG_WriteByte (&sv.signon, svc_spawnstaticsound);

	for (i = 0; i < 3; i++)
		MSG_WriteCoord (&sv.signon, pos[i]);

	if (soundnum > 255 && sv.Protocol == PROTOCOL_VERSION_FITZ)
		MSG_WriteShort (&sv.signon, soundnum);
	else if (sv.Protocol != PROTOCOL_VERSION_BJP2)
		MSG_WriteByte (&sv.signon, soundnum);
	else MSG_WriteShort (&sv.signon, soundnum);

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

	entity = G_EDICT(OFS_PARM0);
	channel = G_FLOAT(OFS_PARM1);
	sample = G_STRING(OFS_PARM2);
	volume = G_FLOAT(OFS_PARM3) * 255;
	attenuation = G_FLOAT(OFS_PARM4);
	
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
*(int *)-4 = 0;	// dump to debugger
//	PR_RunError ("break statement");
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

	v1 = G_VECTOR(OFS_PARM0);
	v2 = G_VECTOR(OFS_PARM1);
	nomonsters = G_FLOAT(OFS_PARM2);
	ent = G_EDICT(OFS_PARM3);

	trace = SV_Move (v1, vec3_origin, vec3_origin, v2, nomonsters, ent);

	pr_global_struct->trace_allsolid = trace.allsolid;
	pr_global_struct->trace_startsolid = trace.startsolid;
	pr_global_struct->trace_fraction = trace.fraction;
	pr_global_struct->trace_inwater = trace.inwater;
	pr_global_struct->trace_inopen = trace.inopen;
	VectorCopy (trace.endpos, pr_global_struct->trace_endpos);
	VectorCopy (trace.plane.normal, pr_global_struct->trace_plane_normal);
	pr_global_struct->trace_plane_dist =  trace.plane.dist;	
	if (trace.ent)
		pr_global_struct->trace_ent = EDICT_TO_PROG(trace.ent);
	else
		pr_global_struct->trace_ent = EDICT_TO_PROG(sv.edicts);
}


extern trace_t SV_Trace_Toss (edict_t *ent, edict_t *ignore);

void PF_TraceToss (void)
{
	trace_t	trace;
	edict_t	*ent;
	edict_t	*ignore;

	ent = G_EDICT(OFS_PARM0);
	ignore = G_EDICT(OFS_PARM1);

	trace = SV_Trace_Toss (ent, ignore);

	pr_global_struct->trace_allsolid = trace.allsolid;
	pr_global_struct->trace_startsolid = trace.startsolid;
	pr_global_struct->trace_fraction = trace.fraction;
	pr_global_struct->trace_inwater = trace.inwater;
	pr_global_struct->trace_inopen = trace.inopen;
	VectorCopy (trace.endpos, pr_global_struct->trace_endpos);
	VectorCopy (trace.plane.normal, pr_global_struct->trace_plane_normal);
	pr_global_struct->trace_plane_dist =  trace.plane.dist;	
	if (trace.ent)
		pr_global_struct->trace_ent = EDICT_TO_PROG(trace.ent);
	else
		pr_global_struct->trace_ent = EDICT_TO_PROG(sv.edicts);
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

byte	checkpvs[(MAX_MAP_LEAFS + 7) / 8];

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

	for ( ;  ; i++)
	{
		if (i == svs.maxclients+1)
			i = 1;

		ent = EDICT_NUM(i);

		if (i == check)
			break;	// didn't find anything else

		if (ent->free)
			continue;
		if (ent->v.health <= 0)
			continue;
		if ((int)ent->v.flags & FL_NOTARGET)
			continue;

	// anything that is a client, or has a client as an enemy
		break;
	}

// get the PVS for the entity
	VectorAdd (ent->v.origin, ent->v.view_ofs, org);
	leaf = Mod_PointInLeaf (org, sv.worldmodel);
	pvs = Mod_LeafPVS (leaf, sv.worldmodel);
	memcpy (checkpvs, pvs, (sv.worldmodel->bh->numleafs+7)>>3 );

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
	if (sv.time - sv.lastchecktime >= 0.1)
	{
		sv.lastcheck = PF_newcheckclient (sv.lastcheck);
		sv.lastchecktime = sv.time;
	}

// return check if it might be visible	
	ent = EDICT_NUM(sv.lastcheck);
	if (ent->free || ent->v.health <= 0)
	{
		RETURN_EDICT(sv.edicts);
		return;
	}

// if current entity can't possibly see the check entity, return 0
	self = PROG_TO_EDICT(pr_global_struct->self);
	VectorAdd (self->v.origin, self->v.view_ofs, view);
	leaf = Mod_PointInLeaf (view, sv.worldmodel);
	l = (leaf - sv.worldmodel->bh->leafs) - 1;
	if ( (l<0) || !(checkpvs[l>>3] & (1<<(l&7)) ) )
	{
c_notvis++;
		RETURN_EDICT(sv.edicts);
		return;
	}

// might be able to see it
c_invis++;
	RETURN_EDICT(ent);
}

//============================================================================


/*
=================
PF_stuffcmd

Sends text over to the client's execution buffer

stuffcmd (clientent, value)
=================
*/
void PF_stuffcmd (void)
{
	int		entnum;
	char	*str;
	client_t	*old;

	entnum = G_EDICTNUM (OFS_PARM0);

	if (entnum < 1 || entnum > svs.maxclients)
		PR_RunError ("Parm 0 not a client");

	str = G_STRING (OFS_PARM1);	

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

	str = G_STRING(OFS_PARM0);
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
	
	G_FLOAT(OFS_RETURN) = Cvar_VariableValue (str);
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

	chain = (edict_t *)sv.edicts;
	
	org = G_VECTOR(OFS_PARM0);
	rad = G_FLOAT(OFS_PARM1);

	ent = NEXT_EDICT(sv.edicts);
	for (i=1 ; i<sv.num_edicts ; i++, ent = NEXT_EDICT(ent))
	{
		if (ent->free)
			continue;
		if (ent->v.solid == SOLID_NOT)
			continue;
		for (j=0 ; j<3 ; j++)
			eorg[j] = org[j] - (ent->v.origin[j] + (ent->v.mins[j] + ent->v.maxs[j])*0.5);			
		if (Length(eorg) > rad)
			continue;
			
		ent->v.chain = EDICT_TO_PROG(chain);
		chain = ent;
	}

	RETURN_EDICT(chain);
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

	G_INT (OFS_RETURN) = pr_string_temp - pr_strings;
}

void PF_fabs (void)
{
	float	v;
	v = G_FLOAT(OFS_PARM0);
	G_FLOAT(OFS_RETURN) = fabs(v);
}

void PF_vtos (void)
{
	char *pr_string_temp = PR_GetTempString ();

	_snprintf (pr_string_temp, 128, "'%5.1f %5.1f %5.1f'", G_VECTOR(OFS_PARM0)[0], G_VECTOR(OFS_PARM0)[1], G_VECTOR(OFS_PARM0)[2]);
	G_INT(OFS_RETURN) = pr_string_temp - pr_strings;
}

void PF_etos (void)
{
	char *pr_string_temp = PR_GetTempString ();

	_snprintf (pr_string_temp, 128, "entity %i", G_EDICTNUM(OFS_PARM0));
	G_INT(OFS_RETURN) = pr_string_temp - pr_strings;
}

void PF_Spawn (void)
{
	edict_t	*ed;
	ed = ED_Alloc();
	RETURN_EDICT(ed);
}

void PF_Remove (void)
{
	edict_t	*ed;
	
	ed = G_EDICT(OFS_PARM0);

	if (sv_permagibs.value)
	{
		// permanent gibs
		model_t *mod = sv.models[(int) ed->v.modelindex];

		if (mod)
			if (mod->type == mod_alias)
				if (!strnicmp (mod->name, "progs/gib", 9))
					return;
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

	e = G_EDICTNUM(OFS_PARM0);
	f = G_INT(OFS_PARM1);
	s = G_STRING (OFS_PARM2);

	if (!s)
		PR_RunError ("PF_Find: bad search string");

	for (e++ ; e < sv.num_edicts ; e++)
	{
		ed = EDICT_NUM(e);
		if (ed->free)
			continue;
		t = E_STRING(ed,f);
		if (!t)
			continue;
		if (!strcmp(t,s))
		{
			RETURN_EDICT(ed);
			return;
		}
	}

	RETURN_EDICT(sv.edicts);
}


void PR_CheckEmptyString (char *s)
{
	if (s[0] <= ' ')
		PR_RunError ("Bad string");
}

void PF_precache_file (void)
{	// precache_file is only used to copy files with qcc, it does nothing
	G_INT(OFS_RETURN) = G_INT(OFS_PARM0);
}

void PF_precache_sound (void)
{
	char	*s;
	int		i;
	
	if (sv.state != ss_loading)
		PR_RunError ("PF_Precache_*: Precache can only be done in spawn functions");
		
	s = G_STRING (OFS_PARM0);
	G_INT(OFS_RETURN) = G_INT(OFS_PARM0);
	PR_CheckEmptyString (s);
	
	for (i=0 ; i<MAX_SOUNDS ; i++)
	{
		if (!sv.sound_precache[i])
		{
			sv.sound_precache[i] = s;
			return;
		}
		if (!strcmp(sv.sound_precache[i], s))
			return;
	}
	PR_RunError ("PF_precache_sound: overflow");
}

void PF_precache_model (void)
{
	char	*s;
	int		i;
	
	if (sv.state != ss_loading)
		PR_RunError ("PF_Precache_*: Precache can only be done in spawn functions");
		
	s = G_STRING (OFS_PARM0);
	G_INT(OFS_RETURN) = G_INT(OFS_PARM0);
	PR_CheckEmptyString (s);

	for (i = 0; i < MAX_MODELS; i++)
	{
		if (!sv.model_precache[i])
		{
			sv.model_precache[i] = s;
			sv.models[i] = Mod_ForName (s, true);
			return;
		}
		if (!strcmp(sv.model_precache[i], s))
			return;
	}

	PR_RunError ("PF_precache_model: overflow");
}


void PF_coredump (void)
{
	ED_PrintEdicts ();
}

void PF_traceon (void)
{
	pr_trace = true;
}

void PF_traceoff (void)
{
	pr_trace = false;
}

void PF_eprint (void)
{
	ED_PrintNum (G_EDICTNUM(OFS_PARM0));
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
	
	ent = PROG_TO_EDICT(pr_global_struct->self);
	yaw = G_FLOAT(OFS_PARM0);
	dist = G_FLOAT(OFS_PARM1);
	
	if ( !( (int)ent->v.flags & (FL_ONGROUND|FL_FLY|FL_SWIM) ) )
	{
		G_FLOAT(OFS_RETURN) = 0;
		return;
	}

	yaw = yaw*M_PI*2 / 360;
	
	move[0] = cos(yaw)*dist;
	move[1] = sin(yaw)*dist;
	move[2] = 0;

// save program state, because SV_movestep may call other progs
	oldf = pr_xfunction;
	oldself = pr_global_struct->self;
	
	G_FLOAT(OFS_RETURN) = SV_movestep(ent, move, true);
	
	
// restore program state
	pr_xfunction = oldf;
	pr_global_struct->self = oldself;
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

	ent = PROG_TO_EDICT(pr_global_struct->self);

	VectorCopy (ent->v.origin, end);
	end[2] -= 256;

	trace = SV_Move (ent->v.origin, ent->v.mins, ent->v.maxs, end, false, ent);

	if (trace.fraction == 1 || trace.allsolid)
		G_FLOAT(OFS_RETURN) = 0;
	else
	{
		VectorCopy (trace.endpos, ent->v.origin);
		SV_LinkEdict (ent, false);
		ent->v.flags = (int)ent->v.flags | FL_ONGROUND;
		ent->v.groundentity = EDICT_TO_PROG(trace.ent);
		G_FLOAT(OFS_RETURN) = 1;
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
	
	style = G_FLOAT(OFS_PARM0);
	val = G_STRING (OFS_PARM1);

// change the string in sv
	sv.lightstyles[style] = val;
	
// send message to all clients on this server
	if (sv.state != ss_active)
		return;
	
	for (j=0, client = svs.clients ; j<svs.maxclients ; j++, client++)
		if (client->active || client->spawned)
		{
			MSG_WriteChar (&client->message, svc_lightstyle);
			MSG_WriteChar (&client->message,style);
			MSG_WriteString (&client->message, val);
		}
}

void PF_rint (void)
{
	float	f;
	f = G_FLOAT(OFS_PARM0);
	if (f > 0)
		G_FLOAT(OFS_RETURN) = (int)(f + 0.5);
	else
		G_FLOAT(OFS_RETURN) = (int)(f - 0.5);
}
void PF_floor (void)
{
	G_FLOAT(OFS_RETURN) = floor(G_FLOAT(OFS_PARM0));
}
void PF_ceil (void)
{
	G_FLOAT(OFS_RETURN) = ceil(G_FLOAT(OFS_PARM0));
}


/*
=============
PF_checkbottom
=============
*/
void PF_checkbottom (void)
{
	edict_t	*ent;
	
	ent = G_EDICT(OFS_PARM0);

	G_FLOAT(OFS_RETURN) = SV_CheckBottom (ent);
}

/*
=============
PF_pointcontents
=============
*/
void PF_pointcontents (void)
{
	float	*v;
	
	v = G_VECTOR(OFS_PARM0);

	G_FLOAT(OFS_RETURN) = SV_PointContents (v);	
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
	
	i = G_EDICTNUM(OFS_PARM0);
	while (1)
	{
		i++;
		if (i == sv.num_edicts)
		{
			RETURN_EDICT(sv.edicts);
			return;
		}
		ent = EDICT_NUM(i);
		if (!ent->free)
		{
			RETURN_EDICT(ent);
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
	
	ent = G_EDICT(OFS_PARM0);
	speed = G_FLOAT(OFS_PARM1);

	VectorCopy (ent->v.origin, start);
	start[2] += 20;

// try sending a trace straight
	VectorCopy (pr_global_struct->v_forward, dir);
	VectorMA (start, 2048, dir, end);
	tr = SV_Move (start, vec3_origin, vec3_origin, end, false, ent);
	if (tr.ent && tr.ent->v.takedamage == DAMAGE_AIM
	&& (!teamplay.value || ent->v.team <=0 || ent->v.team != tr.ent->v.team) )
	{
		VectorCopy (pr_global_struct->v_forward, G_VECTOR(OFS_RETURN));
		return;
	}

// try all possible entities
	VectorCopy (dir, bestdir);
	bestdist = sv_aim.value;
	bestent = NULL;
	
	check = NEXT_EDICT(sv.edicts);
	for (i=1 ; i<sv.num_edicts ; i++, check = NEXT_EDICT(check) )
	{
		if (check->v.takedamage != DAMAGE_AIM)
			continue;
		if (check == ent)
			continue;
		if (teamplay.value && ent->v.team > 0 && ent->v.team == check->v.team)
			continue;	// don't aim at teammate
		for (j=0 ; j<3 ; j++)
			end[j] = check->v.origin[j]
			+ 0.5*(check->v.mins[j] + check->v.maxs[j]);
		VectorSubtract (end, start, dir);
		VectorNormalize (dir);
		dist = DotProduct (dir, pr_global_struct->v_forward);
		if (dist < bestdist)
			continue;	// to far to turn
		tr = SV_Move (start, vec3_origin, vec3_origin, end, false, ent);
		if (tr.ent == check)
		{	// can shoot at this one
			bestdist = dist;
			bestent = check;
		}
	}
	
	if (bestent)
	{
		VectorSubtract (bestent->v.origin, ent->v.origin, dir);
		dist = DotProduct (dir, pr_global_struct->v_forward);
		VectorScale (pr_global_struct->v_forward, dist, end);
		end[2] = dir[2];
		VectorNormalize (end);
		VectorCopy (end, G_VECTOR(OFS_RETURN));	
	}
	else
	{
		VectorCopy (bestdir, G_VECTOR(OFS_RETURN));
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
	
	ent = PROG_TO_EDICT(pr_global_struct->self);
	current = anglemod( ent->v.angles[1] );
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

	if (ent == sv.edicts)
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
		PR_RunError ("PF_changepitch: .float idealpitch and .float pitch_speed must be defined to use changepitch");
		return;
	}

	if (val = GETEDICTFIELDVALUEFAST (ent, ed_pitch_speed))
		speed = val->_float;
	else
	{
		PR_RunError ("PF_changepitch: .float idealpitch and .float pitch_speed must be defined to use changepitch");
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

	dest = G_FLOAT(OFS_PARM0);
	switch (dest)
	{
	case MSG_BROADCAST:
		return &sv.datagram;
	
	case MSG_ONE:
		ent = PROG_TO_EDICT(pr_global_struct->msg_entity);
		entnum = NUM_FOR_EDICT(ent);
		if (entnum < 1 || entnum > svs.maxclients)
			PR_RunError ("WriteDest: not a client");
		return &svs.clients[entnum-1].message;
		
	case MSG_ALL:
		return &sv.reliable_datagram;
	
	case MSG_INIT:
		return &sv.signon;

	default:
		PR_RunError ("WriteDest: bad destination");
		break;
	}
	
	return NULL;
}

void PF_WriteByte (void)
{
	MSG_WriteByte (WriteDest(), G_FLOAT(OFS_PARM1));
}

void PF_WriteChar (void)
{
	MSG_WriteChar (WriteDest(), G_FLOAT(OFS_PARM1));
}

void PF_WriteShort (void)
{
	MSG_WriteShort (WriteDest(), G_FLOAT(OFS_PARM1));
}

void PF_WriteLong (void)
{
	MSG_WriteLong (WriteDest(), G_FLOAT(OFS_PARM1));
}

void PF_WriteAngle (void)
{
	MSG_WriteAngle (WriteDest(), G_FLOAT(OFS_PARM1), true);
}

void PF_WriteCoord (void)
{
	MSG_WriteCoord (WriteDest(), G_FLOAT(OFS_PARM1));
}

void PF_WriteString (void)
{
	MSG_WriteString (WriteDest(), G_STRING (OFS_PARM1));
}


void PF_WriteEntity (void)
{
	MSG_WriteShort (WriteDest(), G_EDICTNUM(OFS_PARM1));
}

//=============================================================================

int SV_ModelIndex (char *name);

void PF_makestatic (void)
{
	edict_t	*ent;
	int		i;
	int		bits = 0;

	ent = G_EDICT (OFS_PARM0);

	if (sv.Protocol == PROTOCOL_VERSION_FITZ)
	{
		// never send alpha
		if (SV_ModelIndex (pr_strings + ent->v.model) & 0xFF00) bits |= B_LARGEMODEL;
		if ((int) (ent->v.frame) & 0xFF00) bits |= B_LARGEFRAME;
	}
	else
	{
		if (SV_ModelIndex (pr_strings + ent->v.model) & 0xFF00 || (int) (ent->v.frame) & 0xFF00)
		{
			// can't display the correct model & frame, so don't show it at all
			ED_Free (ent);
			return;
		}
	}

	if (bits)
	{
		MSG_WriteByte (&sv.signon, svc_spawnstatic2);
		MSG_WriteByte (&sv.signon, bits);
	}
	else MSG_WriteByte (&sv.signon, svc_spawnstatic);

	if (bits & B_LARGEMODEL)
		MSG_WriteShort (&sv.signon, SV_ModelIndex (pr_strings + ent->v.model));
	else SV_WriteByteShort (&sv.signon, SV_ModelIndex (pr_strings + ent->v.model));

	if (bits & B_LARGEFRAME)
		MSG_WriteShort (&sv.signon, ent->v.frame);
	else MSG_WriteByte (&sv.signon, ent->v.frame);

	MSG_WriteByte (&sv.signon, ent->v.colormap);
	MSG_WriteByte (&sv.signon, ent->v.skin);

	for (i = 0; i < 3; i++)
	{
		MSG_WriteCoord (&sv.signon, ent->v.origin[i]);
		MSG_WriteAngle (&sv.signon, ent->v.angles[i], true);
	}

	// never send FQ alpha but the client must be capable of reading it if it receives it
	// throw the entity away now
	ED_Free (ent);
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

	ent = G_EDICT(OFS_PARM0);
	i = NUM_FOR_EDICT(ent);
	if (i < 1 || i > svs.maxclients)
		PR_RunError ("Entity is not a client");

	// copy spawn parms out of the client_t
	client = svs.clients + (i-1);

	for (i=0 ; i< NUM_SPAWN_PARMS ; i++)
		(&pr_global_struct->parm1)[i] = client->spawn_parms[i];
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

	Cbuf_AddText (va ("changelevel %s\n",s));
}


void PF_sin (void)
{
	G_FLOAT(OFS_RETURN) = sin(G_FLOAT(OFS_PARM0));
}

void PF_cos (void)
{
	G_FLOAT(OFS_RETURN) = cos(G_FLOAT(OFS_PARM0));
}

void PF_sqrt (void)
{
	G_FLOAT (OFS_RETURN) = sqrt (G_FLOAT(OFS_PARM0));
}


void PF_Fixme (void)
{
	// don't crash
	Con_SafePrintf ("PF_Fixme: unimplemented bulitin\n");
}


void PF_CheckExtension (void)
{
	// darkplaces extension check
	if (pr_checkextension.value)
	{
		// check in pr_extensions
		char *ext = PF_VarString (0);
	}

	// unimplemented extension
	G_FLOAT (OFS_RETURN) = 0;
}


builtin_t *pr_builtins;
int pr_numbuiltins;

#define PR_MAX_BUILTINS  1000

void PR_InitBuiltIns (void)
{
	pr_builtins = (builtin_t *) Pool_Alloc (POOL_PERMANENT, sizeof (builtin_t) * PR_MAX_BUILTINS);

	// set all builtins to unimplemented
	for (int i = 0; i < PR_MAX_BUILTINS; i++)
		pr_builtins[i] = PF_Fixme;

	// setup used builtins
	pr_builtins[1] = PF_makevectors;
	pr_builtins[2] = PF_setorigin;
	pr_builtins[3] = PF_setmodel;
	pr_builtins[4] = PF_setsize;
	pr_builtins[6] = PF_break;
	pr_builtins[7] = PF_random;
	pr_builtins[8] = PF_sound;
	pr_builtins[9] = PF_normalize;
	pr_builtins[10] = PF_error;
	pr_builtins[11] = PF_objerror;
	pr_builtins[12] = PF_vlen;
	pr_builtins[13] = PF_vectoyaw;
	pr_builtins[14] = PF_Spawn;
	pr_builtins[15] = PF_Remove;
	pr_builtins[16] = PF_traceline;
	pr_builtins[17] = PF_checkclient;
	pr_builtins[18] = PF_Find;
	pr_builtins[19] = PF_precache_sound;
	pr_builtins[20] = PF_precache_model;
	pr_builtins[21] = PF_stuffcmd;
	pr_builtins[22] = PF_findradius;
	pr_builtins[23] = PF_bprint;
	pr_builtins[24] = PF_sprint;
	pr_builtins[25] = PF_dprint;
	pr_builtins[26] = PF_ftos;
	pr_builtins[27] = PF_vtos;
	pr_builtins[28] = PF_coredump;
	pr_builtins[29] = PF_traceon;
	pr_builtins[30] = PF_traceoff;
	pr_builtins[31] = PF_eprint;
	pr_builtins[32] = PF_walkmove;
	pr_builtins[34] = PF_droptofloor;
	pr_builtins[35] = PF_lightstyle;
	pr_builtins[36] = PF_rint;
	pr_builtins[37] = PF_floor;
	pr_builtins[38] = PF_ceil;
	pr_builtins[40] = PF_checkbottom;
	pr_builtins[41] = PF_pointcontents;
	pr_builtins[43] = PF_fabs;
	pr_builtins[44] = PF_aim;
	pr_builtins[45] = PF_cvar;
	pr_builtins[46] = PF_localcmd;
	pr_builtins[47] = PF_nextent;
	pr_builtins[48] = PF_particle;
	pr_builtins[49] = PF_changeyaw;
	pr_builtins[51] = PF_vectoangles;
	pr_builtins[52] = PF_WriteByte;
	pr_builtins[53] = PF_WriteChar;
	pr_builtins[54] = PF_WriteShort;
	pr_builtins[55] = PF_WriteLong;
	pr_builtins[56] = PF_WriteCoord;
	pr_builtins[57] = PF_WriteAngle;
	pr_builtins[58] = PF_WriteString;
	pr_builtins[59] = PF_WriteEntity;
	pr_builtins[60] = PF_sin;
	pr_builtins[61] = PF_cos;
	pr_builtins[62] = PF_sqrt;
	pr_builtins[63] = PF_changepitch;
	pr_builtins[64] = PF_TraceToss;
	pr_builtins[65] = PF_etos;
	pr_builtins[67] = SV_MoveToGoal;
	pr_builtins[68] = PF_precache_file;
	pr_builtins[69] = PF_makestatic;
	pr_builtins[70] = PF_changelevel;
	pr_builtins[72] = PF_cvar_set;
	pr_builtins[73] = PF_centerprint;
	pr_builtins[74] = PF_ambientsound;
	pr_builtins[75] = PF_precache_model;
	pr_builtins[76] = PF_precache_sound;
	pr_builtins[77] = PF_precache_file;
	pr_builtins[78] = PF_setspawnparms;
	pr_builtins[99] = PF_CheckExtension;

	// count number of implemented builtins
	for (int i = 0; i < PR_MAX_BUILTINS; i++)
	{
		if (pr_builtins[i] == PF_Fixme) continue;

		// last builtin that's not a fixme
		pr_numbuiltins = i;
	}

	// add 1 cos it's 0-based
	pr_numbuiltins++;
}


