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
// cvar.c -- dynamic variable tracking

#include "quakedef.h"

cvar_t	*cvar_vars;
char	*cvar_null_string = "";

// poxy fucking nehahra cvars
// and there I was thinking that HIPNOTIC was a collection of vile hacks...
cvar_t nehx00 ("nehx00", "0"); cvar_t nehx01 ("nehx01", "0"); cvar_t nehx02 ("nehx02", "0"); cvar_t nehx03 ("nehx03", "0");
cvar_t nehx04 ("nehx04", "0"); cvar_t nehx05 ("nehx05", "0"); cvar_t nehx06 ("nehx06", "0"); cvar_t nehx07 ("nehx07", "0");
cvar_t nehx08 ("nehx08", "0"); cvar_t nehx09 ("nehx09", "0"); cvar_t nehx10 ("nehx10", "0"); cvar_t nehx11 ("nehx11", "0");
cvar_t nehx12 ("nehx12", "0"); cvar_t nehx13 ("nehx13", "0"); cvar_t nehx14 ("nehx14", "0"); cvar_t nehx15 ("nehx15", "0");
cvar_t nehx16 ("nehx16", "0"); cvar_t nehx17 ("nehx17", "0"); cvar_t nehx18 ("nehx18", "0"); cvar_t nehx19 ("nehx19", "0");

/*
============
Cvar_FindVar
============
*/
cvar_t *Cvar_FindVar (char *var_name)
{
	cvar_t	*var;

	for (var=cvar_vars ; var ; var=var->next)
		if (!strcmp (var_name, var->name))
			return var;

	return NULL;
}

/*
============
Cvar_VariableValue
============
*/
float Cvar_VariableValue (char *var_name)
{
	cvar_t	*var;
	
	var = Cvar_FindVar (var_name);

	if (!var) return 0;

	return atof (var->string);
}


/*
============
Cvar_VariableString
============
*/
char *Cvar_VariableString (char *var_name)
{
	cvar_t *var;
	
	var = Cvar_FindVar (var_name);
	if (!var)
		return cvar_null_string;
	return var->string;
}


/*
============
Cvar_CompleteVariable
============
*/
char *Cvar_CompleteVariable (char *partial)
{
	cvar_t		*cvar;
	int			len;
	
	len = strlen(partial);
	
	if (!len)
		return NULL;
		
// check functions
	for (cvar=cvar_vars ; cvar ; cvar=cvar->next)
		if (!strnicmp (partial,cvar->name, len))
			return cvar->name;

	return NULL;
}


/*
============
Cvar_Set
============
*/
void Cvar_Set (cvar_t *var, char *value)
{
	bool changed;

	if (!var)
	{
		// there is an error in C code if this happens
		Con_Printf ("Cvar_Set: variable %s not found\n", var->name);
		return;
	}

	// reject set attempt
	if (var->usage & CVAR_READONLY)
	{
		Con_Printf ("Cvar_Set: var->usage & CVAR_READONLY\n");
		return;
	}

	changed = strcmp (var->string, value);

	if (changed)
	{
		Zone_Free (var->string);
		var->string = (char *) Zone_Alloc (strlen (value) + 1);
		strcpy (var->string, value);
		var->value = atof (var->string);
		var->integer = (int) var->value;
	}

	if ((var->usage & CVAR_SERVER) && changed)
	{
		if (sv.active)
			SV_BroadcastPrintf ("\"%s\" changed to \"%s\"\n", var->name, var->string);
	}
}


void Cvar_Set (cvar_t *var, float value)
{
	bool changed;

	if (!var)
	{
		// there is an error in C code if this happens
		Con_Printf ("Cvar_Set: variable %s not found\n", var->name);
		return;
	}

	// reject set attempt
	if (var->usage & CVAR_READONLY)
	{
		Con_Printf ("Cvar_Set: var->usage & CVAR_READONLY\n");
		return;
	}

	changed = (var->value != value);

	if (changed)
	{
		// store back to the cvar
		var->value = value;
		var->integer = (int) var->value;

		// copy out the value to a temp buffer
		char valbuf[32];
		_snprintf (valbuf, 32, "%g", var->value);

		Zone_Free (var->string);
		var->string = (char *) Zone_Alloc (strlen (valbuf) + 1);

		strcpy (var->string, valbuf);
	}

	if ((var->usage & CVAR_SERVER) && changed)
	{
		if (sv.active)
			SV_BroadcastPrintf ("\"%s\" changed to \"%s\"\n", var->name, var->string);
	}
}


void Cvar_Set (char *var_name, float value)
{
	Cvar_Set (Cvar_FindVar (var_name), value);
}


void Cvar_Set (char *var_name, char *value)
{
	Cvar_Set (Cvar_FindVar (var_name), value);
}


/*
============
Cvar_Register

Adds a freestanding variable to the variable list.
============
*/
static void Cvar_Register (cvar_t *variable)
{
	// these should never go through here but let's just be certain
	// (edit - actually it does - recursively - when setting up shadows; see below)
	if (variable->usage & CVAR_DUMMY) return;

	// first check to see if it has already been defined
	if (Cvar_FindVar (variable->name)) return;

	// check for overlap with a command
	if (Cmd_Exists (variable->name)) return;

	// store the value off
	variable->value = atof (variable->string);
	variable->integer = (int) variable->value;

	// link the variable in
	variable->next = cvar_vars;
	cvar_vars = variable;
}


/*
============
Cvar_Command

Handles variable inspection and changing from the console
============
*/
bool Cvar_Command (void)
{
	cvar_t *v;

	// check variables
	if (!(v = Cvar_FindVar (Cmd_Argv(0)))) return false;

	// perform a variable print or set
	if (Cmd_Argc() == 1)
	{
		Con_Printf ("\"%s\" is \"%s\"\n", v->name, v->string);
		return true;
	}

	Cvar_Set (v->name, Cmd_Argv(1));
	return true;
}


/*
============
Cvar_WriteVariables

Writes lines containing "set variable value" for all variables
with the archive flag set to true.
============
*/
void Cvar_WriteVariables (FILE *f)
{
	cvar_t	*var;

	for (var = cvar_vars ; var ; var = var->next)
		if (var->usage & CVAR_ARCHIVE)
			fprintf (f, "%s \"%s\"\n", var->name, var->string);
}


cvar_t::cvar_t (char *cvarname, char *initialval, int useflags)
{
	// alloc space
	this->name = (char *) Zone_Alloc (strlen (cvarname) + 1);
	this->string = (char *) Zone_Alloc (strlen (initialval) + 1);

	// copy in the data
	strcpy (this->name, cvarname);
	strcpy (this->string, initialval);
	this->usage = useflags;

	// self-register the cvar at construction time
	Cvar_Register (this);
}


cvar_t::cvar_t (char *cvarname, float initialval, int useflags)
{
	// alloc space
	this->name = (char *) Zone_Alloc (strlen (cvarname) + 1);

	// copy out the value to a temp buffer
	char valbuf[32];
	_snprintf (valbuf, 32, "%g", initialval);

	// alloc space for the string
	this->string = (char *) Zone_Alloc (strlen (valbuf) + 1);

	// copy in the data
	strcpy (this->name, cvarname);
	strcpy (this->string, valbuf);
	this->usage = useflags;

	// self-register the cvar at construction time
	Cvar_Register (this);
}


cvar_t::cvar_t (void)
{
	// dummy cvar for temp usage; not registered
	this->name = (char *) Zone_Alloc (2);
	this->string = (char *) Zone_Alloc (2);

	this->name[0] = 0;
	this->string[0] = 0;
	this->value = 0;
	this->usage = CVAR_DUMMY;
	this->integer = 0;
	this->next = NULL;
}


cvar_t::~cvar_t (void)
{
	// protect the zone from overflowing if cvars are declared in function scope
//	Zone_Free (this->name);
//	Zone_Free (this->string);
}
