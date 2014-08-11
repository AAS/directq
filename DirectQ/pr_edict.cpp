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
// sv_edict.c -- entity dictionary

#include "quakedef.h"
#include "pr_class.h"
#include "d3d_model.h"

CProgsDat *SVProgs = NULL;

int		type_size[8] = {1, sizeof (string_t) / 4, 1, 3, 1, 1, sizeof (func_t) / 4, sizeof (void *) / 4};

ddef_t *ED_FieldAtOfs (int ofs);
bool	ED_ParseEpair (void *base, ddef_t *key, char *s);

cvar_t	nomonsters ("nomonsters", "0");
cvar_t	gamecfg ("gamecfg", "0");
cvar_t	scratch1 ("scratch1", "0");
cvar_t	scratch2 ("scratch2", "0");
cvar_t	scratch3 ("scratch3", "0");
cvar_t	scratch4 ("scratch4", "0");
cvar_t	savedgamecfg ("savedgamecfg", "0", CVAR_ARCHIVE);
cvar_t	saved1 ("saved1", "0", CVAR_ARCHIVE);
cvar_t	saved2 ("saved2", "0", CVAR_ARCHIVE);
cvar_t	saved3 ("saved3", "0", CVAR_ARCHIVE);
cvar_t	saved4 ("saved4", "0", CVAR_ARCHIVE);


gefv_cache gefvCache[GEFV_CACHESIZE] = {{NULL, ""}, {NULL, ""}};

int ed_alpha;
int ed_fullbright;
int ed_ammo_shells1;
int ed_ammo_nails1;
int ed_ammo_lava_nails;
int ed_ammo_rockets1;
int ed_ammo_multi_rockets;
int ed_ammo_cells1;
int ed_ammo_plasma;
int ed_items2;
int ed_gravity;
int ed_idealpitch;
int ed_pitch_speed;
int eval_clientcamera; // DP_SV_CLIENTCAMERA

ddef_t *ED_FindField (char *name);

int FindFieldOffset (char *field)
{
	ddef_t *d;

	if (!(d = ED_FindField (field)))
		return 0;

	return d->ofs * 4;
}


void FindEdictFieldOffsets (void)
{
	// get field offsets for anything that's sent through GetEdictFieldValue
	ed_alpha = FindFieldOffset ("alpha");
	ed_fullbright = FindFieldOffset ("fullbright");
	ed_ammo_shells1 = FindFieldOffset ("ammo_shells1");
	ed_ammo_nails1 = FindFieldOffset ("ammo_nails1");
	ed_ammo_lava_nails = FindFieldOffset ("ammo_lava_nails");
	ed_ammo_rockets1 = FindFieldOffset ("ammo_rockets1");
	ed_ammo_multi_rockets = FindFieldOffset ("ammo_multi_rockets");
	ed_ammo_cells1 = FindFieldOffset ("ammo_cells1");
	ed_ammo_plasma = FindFieldOffset ("ammo_plasma");
	ed_items2 = FindFieldOffset ("items2");
	ed_gravity = FindFieldOffset ("gravity");
	ed_idealpitch = FindFieldOffset ("idealpitch");
	ed_pitch_speed = FindFieldOffset ("pitch_speed");
	eval_clientcamera = FindFieldOffset ("clientcamera");	// DP_SV_CLIENTCAMERA
}


/*
=================
ED_ClearEdict

Sets everything to NULL
=================
*/
void ED_ClearEdict (CProgsDat *Progs, edict_t *e)
{
	memset (&e->v, 0, SVProgs->QC->entityfields * 4);
	e->tracetimer = -1;
	e->steplerptime = 0;
	e->free = false;
}


/*
=================
ED_Alloc

Either finds a free edict, or allocates a new one.
Try to avoid reusing an entity that was recently freed, because it
can cause the client to think the entity morphed into something else
instead of being removed and recreated, which can cause interpolated
angles and bad trails.
=================
*/
void SV_AllocEdicts (int numedicts);

edict_t *ED_Alloc (CProgsDat *Progs)
{
	int			i;
	edict_t		*e;

	// start on the edict after the clients
	for (i = svs.maxclients + 1; i < SVProgs->NumEdicts; i++)
	{
		e = GetEdictForNumber (i);

		// the first couple seconds of server time can involve a lot of
		// freeing and allocating, so relax the replacement policy
		if (e->free && (e->freetime < 2.0f || sv.time - e->freetime > 0.5f))
		{
			ED_ClearEdict (Progs, e);
			return e;
		}
	}

	if (i >= MAX_EDICTS)
	{
		// if we hit the absolute upper limit just pick the one with the lowest free time
		float bestfree = sv.time;
		int bestedict = -1;

		for (i = svs.maxclients + 1; i < SVProgs->NumEdicts; i++)
		{
			e = GetEdictForNumber (i);

			if (e->free && e->freetime < bestfree)
			{
				bestedict = i;
				bestfree = e->freetime;
			}
		}

		if (bestedict > -1)
		{
			e = GetEdictForNumber (bestedict);
			ED_ClearEdict (Progs, e);

			return e;
		}

		// no free edicts at all!!!
		Sys_Error ("ED_Alloc: edict count at protocol maximum!");
	}

	// alloc 32 more edicts
	if (i >= SVProgs->MaxEdicts) SV_AllocEdicts (32);

	SVProgs->NumEdicts++;
	e = GetEdictForNumber (i);
	ED_ClearEdict (Progs, e);

	return e;
}

/*
=================
ED_Free

Marks the edict as free
FIXME: walk all entities and NULL out references to this entity
=================
*/
void ED_Free (edict_t *ed)
{
	SV_UnlinkEdict (ed);		// unlink from world bsp

	ed->free = true;
	ed->v.model = 0;
	ed->v.takedamage = 0;
	ed->v.modelindex = 0;
	ed->v.colormap = 0;
	ed->v.skin = 0;
	ed->v.frame = 0;
	VectorCopy (vec3_origin, ed->v.origin);
	VectorCopy (vec3_origin, ed->v.angles);
	ed->v.nextthink = -1;
	ed->v.solid = 0;
	ed->num_leafs = 0;

	ed->freetime = sv.time;
}

//===========================================================================

/*
============
ED_GlobalAtOfs
============
*/
ddef_t *ED_GlobalAtOfs (int ofs)
{
	ddef_t		*def;
	int			i;

	for (i = 0; i < SVProgs->QC->numglobaldefs; i++)
	{
		def = &SVProgs->GlobalDefs[i];

		if (def->ofs == ofs)
			return def;
	}

	return NULL;
}

/*
============
ED_FieldAtOfs
============
*/
ddef_t *ED_FieldAtOfs (int ofs)
{
	ddef_t		*def;
	int			i;

	for (i = 0; i < SVProgs->QC->numfielddefs; i++)
	{
		def = &SVProgs->FieldDefs[i];

		if (def->ofs == ofs)
			return def;
	}

	return NULL;
}

/*
============
ED_FindField
============
*/
ddef_t *ED_FindField (char *name)
{
	ddef_t		*def;
	int			i;

	for (i = 0; i < SVProgs->QC->numfielddefs; i++)
	{
		def = &SVProgs->FieldDefs[i];

		if (!strcmp (SVProgs->GetString (def->s_name), name))
			return def;
	}

	return NULL;
}


/*
============
ED_FindGlobal
============
*/
ddef_t *ED_FindGlobal (char *name)
{
	ddef_t		*def;
	int			i;

	for (i = 0; i < SVProgs->QC->numglobaldefs; i++)
	{
		def = &SVProgs->GlobalDefs[i];

		if (!strcmp (SVProgs->GetString (def->s_name), name))
			return def;
	}

	return NULL;
}


/*
============
ED_FindFunction
============
*/
dfunction_t *ED_FindFunction (char *name)
{
	dfunction_t		*func;
	int				i;

	for (i = 0; i < SVProgs->QC->numfunctions; i++)
	{
		func = &SVProgs->Functions[i];

		if (!strcmp (SVProgs->GetString (func->s_name), name))
			return func;
	}

	return NULL;
}


eval_t *GetEdictFieldValue (edict_t *ed, char *field)
{
	ddef_t			*def = NULL;
	int				i;
	static int		rep = 0;

	for (i = 0; i < GEFV_CACHESIZE; i++)
	{
		if (!strcmp (field, gefvCache[i].field))
		{
			def = gefvCache[i].pcache;
			goto Done;
		}
	}

	def = ED_FindField (field);

	if (strlen (field) < MAX_FIELD_LEN)
	{
		gefvCache[rep].pcache = def;
		strcpy (gefvCache[rep].field, field);
		rep ^= 1;
	}

Done:

	if (!def)
		return NULL;

	return (eval_t *) ((char *) &ed->v + def->ofs * 4);
}


/*
============
PR_ValueString

Returns a string describing *data in a type specific manner
=============
*/
char *PR_ValueString (etype_t type, eval_t *val)
{
	static char	line[256];
	ddef_t		*def;
	dfunction_t	*f;
	int			itype;

	itype = (int) type;
	itype &= ~DEF_SAVEGLOBAL;

	switch (itype)
	{
	case ev_string:
		_snprintf (line, 256, "%s", SVProgs->GetString (val->string));
		break;
	case ev_entity:
		_snprintf (line, 256, "entity %i", GetNumberForEdict (PROG_TO_EDICT (val->edict)));
		break;
	case ev_function:
		f = SVProgs->Functions + val->function;
		_snprintf (line, 256, "%s()", SVProgs->GetString (f->s_name));
		break;
	case ev_field:
		def = ED_FieldAtOfs (val->_int);
		_snprintf (line, 256, ".%s", SVProgs->GetString (def->s_name));
		break;
	case ev_void:
		_snprintf (line, 256, "void");
		break;
	case ev_float:
		_snprintf (line, 256, "%5.1f", val->_float);
		break;
	case ev_vector:
		_snprintf (line, 256, "'%5.1f %5.1f %5.1f'", val->vector[0], val->vector[1], val->vector[2]);
		break;
	case ev_pointer:
		_snprintf (line, 256, "pointer");
		break;
	default:
		_snprintf (line, 256, "bad type %i", itype);
		break;
	}

	return line;
}

/*
============
PR_UglyValueString

Returns a string describing *data in a type specific manner
Easier to parse than PR_ValueString
=============
*/
char *PR_UglyValueString (etype_t type, eval_t *val)
{
	static char	line[256];
	ddef_t		*def;
	dfunction_t	*f;
	int			itype;

	itype = (int) type;
	itype &= ~DEF_SAVEGLOBAL;

	switch (itype)
	{
	case ev_string:
		_snprintf (line, 256, "%s", SVProgs->GetString (val->string));
		break;
	case ev_entity:
		_snprintf (line, 256, "%i", GetNumberForEdict (PROG_TO_EDICT (val->edict)));
		break;
	case ev_function:
		f = SVProgs->Functions + val->function;
		_snprintf (line, 256, "%s", SVProgs->GetString (f->s_name));
		break;
	case ev_field:
		def = ED_FieldAtOfs (val->_int);
		_snprintf (line, 256, "%s", SVProgs->GetString (def->s_name));
		break;
	case ev_void:
		_snprintf (line, 256, "void");
		break;
	case ev_float:
		_snprintf (line, 256, "%f", val->_float);
		break;
	case ev_vector:
		_snprintf (line, 256, "%f %f %f", val->vector[0], val->vector[1], val->vector[2]);
		break;
	default:
		_snprintf (line, 256, "bad type %i", itype);
		break;
	}

	return line;
}

/*
============
PR_GlobalString

Returns a string with a description and the contents of a global,
padded to 20 field width
============
*/
char *PR_GlobalString (int ofs)
{
	char	*s;
	int		i;
	ddef_t	*def;
	void	*val;
	static char	line[128];

	val = (void *) &SVProgs->Globals[ofs];
	def = ED_GlobalAtOfs (ofs);

	if (!def)
		_snprintf (line, 128, "%i(???)", ofs);
	else
	{
		s = PR_ValueString ((etype_t) def->type, (eval_t *) val);
		_snprintf (line, 128, "%i(%s)%s", ofs, SVProgs->GetString (def->s_name), s);
	}

	i = strlen (line);

	for (; i < 20; i++)
		strcat (line, " ");

	strcat (line, " ");

	return line;
}

char *PR_GlobalStringNoContents (int ofs)
{
	int		i;
	ddef_t	*def;
	static char	line[128];

	def = ED_GlobalAtOfs (ofs);

	if (!def)
		_snprintf (line, 128, "%i(???)", ofs);
	else
		_snprintf (line, 128, "%i(%s)", ofs, SVProgs->GetString (def->s_name));

	i = strlen (line);

	for (; i < 20; i++)
		strcat (line, " ");

	strcat (line, " ");

	return line;
}


/*
=============
ED_Print

For debugging
=============
*/
char *edprintstring = NULL;

void ED_Print (edict_t *ed)
{
	int		l;
	ddef_t	*d;
	int		*v;
	int		i, j;
	char	*name;
	int		type;

	if (ed->free)
	{
		Con_Printf ("FREE\n");
		return;
	}

	Con_Printf ("\nEDICT %i:\n", GetNumberForEdict (ed));

	for (i = 1; i < SVProgs->QC->numfielddefs; i++)
	{
		d = &SVProgs->FieldDefs[i];
		name = SVProgs->GetString (d->s_name);

		if (name[strlen (name)-2] == '_')
			continue;	// skip _x, _y, _z vars

		v = (int *) ((char *) &ed->v + d->ofs * 4);

		// if the value is still all 0, skip the field
		type = d->type & ~DEF_SAVEGLOBAL;

		for (j = 0; j < type_size[type]; j++)
			if (v[j])
				break;

		if (j == type_size[type])
			continue;

		Con_Printf ("%s", name);
		l = strlen (name);

		while (l++ < 15)
			Con_Printf (" ");

		Con_Printf ("%s\n", PR_ValueString ((etype_t) d->type, (eval_t *) v));
	}
}

/*
=============
ED_Write

For savegames
=============
*/
void ED_Write (FILE *f, edict_t *ed)
{
	ddef_t	*d;
	int		*v;
	int		i, j;
	char	*name;
	int		type;

	fprintf (f, "{\n");

	if (ed->free)
	{
		fprintf (f, "}\n");
		return;
	}

	for (i = 1; i < SVProgs->QC->numfielddefs; i++)
	{
		d = &SVProgs->FieldDefs[i];
		name = SVProgs->GetString (d->s_name);

		if (name[strlen (name)-2] == '_')
			continue;	// skip _x, _y, _z vars

		v = (int *) ((char *) &ed->v + d->ofs * 4);

		// if the value is still all 0, skip the field
		type = d->type & ~DEF_SAVEGLOBAL;

		for (j = 0; j < type_size[type]; j++)
			if (v[j])
				break;

		if (j == type_size[type])
			continue;

		fprintf (f, "\"%s\" ", name);
		fprintf (f, "\"%s\"\n", PR_UglyValueString ((etype_t) d->type, (eval_t *) v));
	}

	fprintf (f, "}\n");
}

void ED_PrintNum (int ent)
{
	ED_Print (GetEdictForNumber (ent));
}

/*
=============
ED_PrintEdicts

For debugging, prints all the entities in the current server
=============
*/
void ED_PrintEdicts (void)
{
	int		i;

	Con_Printf ("%i entities\n", SVProgs->NumEdicts);

	for (i = 0; i < SVProgs->NumEdicts; i++)
		ED_PrintNum (i);
}

/*
=============
ED_PrintEdict_f

For debugging, prints a single edicy
=============
*/
void ED_PrintEdict_f (void)
{
	int		i;

	i = atoi (Cmd_Argv (1));

	if (i >= SVProgs->NumEdicts)
	{
		Con_Printf ("Bad edict number\n");
		return;
	}

	ED_PrintNum (i);
}

/*
=============
ED_Count

For debugging
=============
*/
void ED_Count (void)
{
	int		i;
	edict_t	*ent;
	int		active, models, solid, step;

	active = models = solid = step = 0;

	for (i = 0; i < SVProgs->NumEdicts; i++)
	{
		ent = GetEdictForNumber (i);

		if (ent->free)
			continue;

		active++;

		if (ent->v.solid)
			solid++;

		if (ent->v.model)
			models++;

		if (ent->v.movetype == MOVETYPE_STEP)
			step++;
	}

	Con_Printf ("num_edicts:%3i\n", SVProgs->NumEdicts);
	Con_Printf ("active    :%3i\n", active);
	Con_Printf ("view      :%3i\n", models);
	Con_Printf ("touch     :%3i\n", solid);
	Con_Printf ("step      :%3i\n", step);

}

/*
==============================================================================

					ARCHIVING GLOBALS

FIXME: need to tag constants, doesn't really work
==============================================================================
*/

/*
=============
ED_WriteGlobals
=============
*/
void ED_WriteGlobals (FILE *f)
{
	ddef_t		*def;
	int			i;
	char		*name;
	int			type;

	fprintf (f, "{\n");

	for (i = 0; i < SVProgs->QC->numglobaldefs; i++)
	{
		def = &SVProgs->GlobalDefs[i];
		type = def->type;

		if (!(def->type & DEF_SAVEGLOBAL))
			continue;

		type &= ~DEF_SAVEGLOBAL;

		if (type != ev_string
		&& type != ev_float
		&& type != ev_entity)
			continue;

		name = SVProgs->GetString (def->s_name);
		fprintf (f, "\"%s\" ", name);
		fprintf (f, "\"%s\"\n", PR_UglyValueString ((etype_t) type, (eval_t *) &SVProgs->Globals[def->ofs]));
	}

	fprintf (f, "}\n");
}

/*
=============
ED_ParseGlobals
=============
*/
void ED_ParseGlobals (char *data)
{
	char	keyname[64];
	ddef_t	*key;

	while (1)
	{
		// parse key
		data = COM_Parse (data);

		if (com_token[0] == '}')
			break;

		if (!data)
			Sys_Error ("ED_ParseEntity: EOF without closing brace");

		strcpy (keyname, com_token);

		// parse value
		data = COM_Parse (data);

		if (!data)
			Sys_Error ("ED_ParseEntity: EOF without closing brace");

		if (com_token[0] == '}')
			Sys_Error ("ED_ParseEntity: closing brace without data");

		key = ED_FindGlobal (keyname);

		if (!key)
		{
			Con_Printf ("'%s' is not a global\n", keyname);
			continue;
		}

		if (!ED_ParseEpair ((void *) SVProgs->Globals, key, com_token))
			Host_Error ("ED_ParseGlobals: parse error");
	}
}

//============================================================================


/*
=============
ED_NewString2
=============
*/
string_t ED_NewString2 (char *string)
{
	char	*new_p;
	int		i, l;
	string_t	num;

	l = strlen (string) + 1;
	num = SVProgs->AllocString (l, &new_p);

	for (i = 0; i < l; i++)
	{
		if (string[i] == '\\' && i < l - 1)
		{
			i++;

			if (string[i] == 'n')
				*new_p++ = '\n';
			else
				*new_p++ = '\\';
		}
		else
			*new_p++ = string[i];
	}

	return num;
}


/*
=============
ED_ParseEval

Can parse either fields or globals
returns false if error
=============
*/
bool ED_ParseEpair (void *base, ddef_t *key, char *s)
{
	int		i;
	char	string[128];
	ddef_t	*def;
	char	*v, *w;
	void	*d;
	dfunction_t	*func;

	d = (void *) ((int *) base + key->ofs);

	switch (key->type & ~DEF_SAVEGLOBAL)
	{
	case ev_string:
		*(string_t *) d = ED_NewString2 (s);
		break;

	case ev_float:
		*(float *) d = atof (s);
		break;

	case ev_vector:
		strcpy (string, s);
		v = string;
		w = string;

		for (i = 0; i < 3; i++)
		{
			while (*v && *v != ' ')
				v++;

			*v = 0;
			((float *) d)[i] = atof (w);
			w = v = v + 1;
		}

		break;

	case ev_entity:
		*(int *) d = EDICT_TO_PROG (GetEdictForNumber (atoi (s)));
		break;

	case ev_field:
		def = ED_FindField (s);

		if (!def)
		{
			Con_Printf ("Can't find field %s\n", s);
			return false;
		}

		* (int *) d = G_INT (def->ofs);
		break;

	case ev_function:
		func = ED_FindFunction (s);

		if (!func)
		{
			Con_Printf ("Can't find function %s\n", s);
			return false;
		}

		* (func_t *) d = func - SVProgs->Functions;
		break;

	default:
		break;
	}

	return true;
}

/*
====================
ED_ParseEdict

Parses an edict out of the given string, returning the new position
ed should be a properly initialized empty edict.
Used for initial level load and for savegames.
====================
*/
char *ED_ParseEdict (char *data, edict_t *ent)
{
	ddef_t		*key;
	bool	anglehack;
	bool	init;
	char		keyname[256];
	int			n;

	init = false;

	// clear it
	if (ent != SVProgs->EdictPointers[0])	// hack
		memset (&ent->v, 0, SVProgs->QC->entityfields * 4);

	// clear alpha
	ent->alphaval = 0;

	// go through all the dictionary pairs
	while (1)
	{
		// parse key
		data = COM_Parse (data);

		if (com_token[0] == '}')
			break;

		if (!data)
			Sys_Error ("ED_ParseEntity: EOF without closing brace");

		// anglehack is to allow QuakeEd to write single scalar angles
		// and allow them to be turned into vectors. (FIXME...)
		if (!strcmp (com_token, "angle"))
		{
			strcpy (com_token, "angles");
			anglehack = true;
		}
		else anglehack = false;

		// FIXME: change light to _light to get rid of this hack
		if (!strcmp (com_token, "light")) strcpy (com_token, "light_lev");	// hack for single light def

		strcpy (keyname, com_token);

		// another hack to fix keynames with trailing spaces
		n = strlen (keyname);

		while (n && keyname[n-1] == ' ')
		{
			keyname[n-1] = 0;
			n--;
		}

		// parse value
		data = COM_Parse (data);

		if (!data) Sys_Error ("ED_ParseEntity: EOF without closing brace");
		if (com_token[0] == '}') Sys_Error ("ED_ParseEntity: closing brace without data");

		init = true;

		// keynames with a leading underscore are used for utility comments,
		// and are immediately discarded by quake
		if (keyname[0] == '_')
			continue;

		// hack to support alpha even when progs doesn't know about it
		if (!strcmp (keyname, "alpha"))
		{
			float f = atof (com_token);
			ent->alphaval = (int) (f * 255);

			if (ent->alphaval < 1) ent->alphaval = 0;
			if (ent->alphaval > 255) ent->alphaval = 255;
		}

		key = ED_FindField (keyname);

		if (!key)
		{
			// the user doesn't need to see this
			Con_DPrintf ("'%s' is not a field\n", keyname);
			continue;
		}

		if (anglehack)
		{
			char	temp[32];
			strcpy (temp, com_token);
			_snprintf (com_token, 1024, "0 %s 0", temp);
		}

		if (!ED_ParseEpair ((void *) &ent->v, key, com_token))
			Host_Error ("ED_ParseEdict: parse error");
	}

	if (!init)
		ent->free = true;

	return data;
}


typedef struct entitystat_s
{
	char *name;
	char *firstword;
	int num_easy;
	int num_normal;
	int num_hard;
	int num_dm;
	struct entitystat_s *next;
} entitystat_t;

typedef struct levelstats_s
{
	int numents;
	int numenttypes;
	int nummodels;
	int numeasy;
	int numnormal;
	int numhard;
	int numdm;
	entitystat_t *entitystats;
	entitystat_t **sorted;
} levelstats_t;

levelstats_t sv_levelstats;

void SV_LevelStats_f (void)
{
	bool hasskills = false;

	if (sv_levelstats.numeasy != sv_levelstats.numnormal || sv_levelstats.numnormal != sv_levelstats.numhard || sv_levelstats.numeasy != sv_levelstats.numhard)
		hasskills = true;

	Con_Printf ("\nMap name:     %s\n", cl.levelname);
	Con_Printf ("File name:    %s\n", cl.worldmodel->name);
	Con_Printf ("Skill Modes:  %s\n", hasskills ? "Yes" : "No");
	Con_Printf ("CD Track:     %i\n", cl.cdtrack);
	Con_Printf ("Surfaces:     %i\n", cl.worldmodel->brushhdr->numsurfaces);
	Con_Printf ("Entities:     %i\n", sv_levelstats.numents);
	Con_Printf ("Models:       %i\n", sv_levelstats.nummodels);

	Con_Printf ("\n");

	Con_Printf ("%-32s  +------+------+------+------+\n", " ");
	Con_Printf ("%-32s  | EASY | NORM | HARD |  DM  |\n", " ");

	char lastent[256] = {0};

	for (entitystat_t *find = sv_levelstats.entitystats; find; find = find->next)
	{
		if (_stricmp (lastent, find->firstword))
		{
			Con_Printf ("%-32s  +------+------+------+------+\n", " ");
			strcpy (lastent, find->firstword);
		}

		Con_Printf ("%-32s  | %3i  | %3i  | %3i  | %3i  |\n", find->name, find->num_easy, find->num_normal, find->num_hard, find->num_dm);
	}

	Con_Printf ("%-32s  +------+------+------+------+\n", " ");

	Con_Printf
	(
		"%-32s  | %3i  | %3i  | %3i  | %3i  |\n",
		"Totals",
		sv_levelstats.numeasy,
		sv_levelstats.numnormal,
		sv_levelstats.numhard,
		sv_levelstats.numdm
	);

	Con_Printf ("%-32s  +------+------+------+------+\n", " ");
}


cmd_t sv_levelstats_cmd ("levelstats", SV_LevelStats_f);

void SV_AddEntityStat (edict_t *ed)
{
	// no classname
	if (!ed->v.classname) return;

	entitystat_t *es = NULL;
	char *classname = SVProgs->GetString (ed->v.classname);

	for (entitystat_t *find = sv_levelstats.entitystats; find; find = find->next)
	{
		if (!_stricmp (find->name, classname))
		{
			es = find;
			break;
		}
	}

	if (!es)
	{
		es = (entitystat_t *) ServerZone->Alloc (sizeof (entitystat_t));

		es->next = sv_levelstats.entitystats;
		sv_levelstats.entitystats = es;

		es->name = (char *) ServerZone->Alloc (strlen (classname) + 1);
		strcpy (es->name, classname);

		es->firstword = (char *) ServerZone->Alloc (strlen (classname) + 1);
		strcpy (es->firstword, classname);

		for (int i = 0;; i++)
		{
			if (!es->firstword[i]) break;

			if (es->firstword[i] == '_')
			{
				es->firstword[i] = 0;
				break;
			}
		}

		es->num_dm = es->num_easy = es->num_hard = es->num_normal = 0;

		sv_levelstats.numenttypes++;
	}

	int spflags = (int) ed->v.spawnflags;

	if (!(spflags & SPAWNFLAG_NOT_EASY))
	{
		es->num_easy++;
		sv_levelstats.numeasy++;
	}

	if (!(spflags & SPAWNFLAG_NOT_MEDIUM))
	{
		es->num_normal++;
		sv_levelstats.numnormal++;
	}

	if (!(spflags & SPAWNFLAG_NOT_HARD))
	{
		es->num_hard++;
		sv_levelstats.numdm++;
	}

	if (!(spflags & SPAWNFLAG_NOT_DEATHMATCH))
	{
		es->num_dm++;
		sv_levelstats.numdm++;
	}

	sv_levelstats.numents++;

	if (ed->v.model) sv_levelstats.nummodels++;
}


int SV_EntityStatSortFunc (entitystat_t **es1, entitystat_t **es2)
{
	return _stricmp (es1[0]->name, es2[0]->name);
}


/*
================
ED_LoadFromFile

The entities are directly placed in the array, rather than allocated with
ED_Alloc, because otherwise an error loading the map would have entity
number references out of order.

Creates a server's entity / program execution context by
parsing textual entity definitions out of an ent file.

Used for both fresh maps and savegame loads.  A fresh map would also need
to call ED_CallSpawnFunctions () to let the objects initialize themselves.
================
*/
void ED_LoadFromFile (char *data)
{
	memset (&sv_levelstats, 0, sizeof (levelstats_t));

	edict_t		*ent;
	int			inhibit;
	dfunction_t	*func;

	ent = NULL;
	inhibit = 0;
	SVProgs->GlobalStruct->time = sv.time;

	int ed_warning = 0;
	int ed_number = 0;

	// parse ents
	while (1)
	{
		// parse the opening brace
		data = COM_Parse (data);

		if (!data)
			break;

		if (com_token[0] != '{')
			Sys_Error ("ED_LoadFromFile: found %s when expecting {", com_token);

		if (!ent)
			ent = GetEdictForNumber (0);
		else
			ent = ED_Alloc (SVProgs);

		data = ED_ParseEdict (data, ent);

		SV_AddEntityStat (ent);

		// remove things from different skill levels or deathmatch
		if (deathmatch.value)
		{
			if (((int) ent->v.spawnflags & SPAWNFLAG_NOT_DEATHMATCH))
			{
				ED_Free (ent);
				inhibit++;
				continue;
			}
		}
		else if ((current_skill == 0 && ((int) ent->v.spawnflags & SPAWNFLAG_NOT_EASY)) ||
			(current_skill == 1 && ((int) ent->v.spawnflags & SPAWNFLAG_NOT_MEDIUM)) ||
			(current_skill >= 2 && ((int) ent->v.spawnflags & SPAWNFLAG_NOT_HARD)))
		{
			ED_Free (ent);
			inhibit++;
			continue;
		}

		// immediately call spawn function
		if (!ent->v.classname)
		{
			// reverted this back to a user print because it's a useful signal that you've got things set up wrong
			Con_Printf ("No classname for:\n");
			ed_warning++;

			if (developer.value) ED_Print (ent);

			ED_Free (ent);
			continue;
		}

		char *classname = SVProgs->GetString (ent->v.classname);

		// look for the spawn function
		func = ED_FindFunction (classname);

		if (!func)
		{
			if (!_stricmp (classname, "func_detail"))
			{
				// if we couldn't find a spawn function for a func_detail entity we must convert it back to a func_wall
				func = ED_FindFunction ("func_wall");
			}

			if (!func)
			{
				// made the console spamming developer only...
				Con_Printf ("No spawn function for: %s\n", classname);
				ed_warning++;

				if (developer.value) ED_Print (ent);

				ED_Free (ent);
				continue;
			}
		}

		if (!strcmp (classname, "monster_fish") && SVProgs->FishHack)
		{
			SVProgs->NumFish++;
			Con_DPrintf ("hacking fish for ID progs\n");
		}

		SVProgs->GlobalStruct->self = EDICT_TO_PROG (ent);
		SVProgs->ExecuteProgram (func - SVProgs->Functions);

		ed_number++;
	}

	if (ed_warning)
	{
		// simplified warning for non-developers
		Con_Printf ("Could not find classname and/or spawn functions for %i entities\n");
		Con_Printf ("Progs.dat may be invalid for current game\n");
		Con_Printf ("Use developer 1 and reload map for full list\n");
		ed_warning = 0;
	}

	Con_DPrintf ("%i entities with %i inhibited\n", ed_number, inhibit);

	sv_levelstats.sorted = (entitystat_t **) ServerZone->Alloc (sv_levelstats.numenttypes * sizeof (entitystat_t *));
	int nument = 0;

	for (entitystat_t *find = sv_levelstats.entitystats; find; find = find->next)
	{
		sv_levelstats.sorted[nument] = find;
		nument++;
	}

	qsort (sv_levelstats.sorted, sv_levelstats.numenttypes, sizeof (entitystat_t *), (sortfunc_t) SV_EntityStatSortFunc);

	for (int i = 1; i < sv_levelstats.numenttypes; i++)
	{
		sv_levelstats.sorted[i - 1]->next = sv_levelstats.sorted[i];
		sv_levelstats.sorted[i]->next = NULL;
	}

	sv_levelstats.entitystats = sv_levelstats.sorted[0];
}


/*
============
PR_Profile_f

============
*/
void PR_Profile_f (void)
{
	SVProgs->Profile ();
}


/*
===============
PR_Init
===============
*/
cmd_t ED_PrintEdict_f_Cmd ("edict", ED_PrintEdict_f);
cmd_t ED_PrintEdicts_Cmd ("edicts", ED_PrintEdicts);
cmd_t ED_Count_Cmd ("edictcount", ED_Count);
cmd_t PR_Profile_f_Cmd ("profile", PR_Profile_f);

void PR_Init (void)
{
	// used to register a buncha cvars and add some commands; not needed any more
}


edict_t *GetEdictForNumber (int n)
{
	// edict overflow
	if (n >= MAX_EDICTS)
		Host_Error ("GetEdictForNumber: bad number %i (max: %i)", n, SVProgs->MaxEdicts);

	// do it this way because we're not guaranteed that 16 allocs will get us to n edicts
	while (n >= SVProgs->MaxEdicts)
	{
		if (SVProgs->MaxEdicts >= MAX_EDICTS)
			Host_Error ("GetEdictForNumber: edict overflow\n");

		// this can happen in some mods
		SV_AllocEdicts (16);
	}

	if (n < 0 || n >= SVProgs->MaxEdicts)
		Host_Error ("GetEdictForNumber: bad number %i (max: %i)", n, SVProgs->MaxEdicts);

	return SVProgs->EdictPointers[n];
}

int GetNumberForEdict (edict_t *e)
{
	return e->ednum;
}


