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
// cvar.h

/*

cvar_t variables are used to hold scalar or string variables that can be changed or displayed at the console or 
prog code as well as accessed directly in C code.

it is sufficient to initialize a cvar_t with just the first two fields, or
you can add a CVAR_ARCHIVE flag for variables that you want saved to the configuration
file when the game is quit:

cvar_t	r_draworder ("r_draworder","1");
cvar_t	scr_screensize ("screensize","1",CVAR_ARCHIVE);

Cvars will register themselves on creation, so there is no need to call Cvar_Register on them.

C code usually just references a cvar in place:
if ( r_draworder.value )

It could optionally ask for the value to be looked up for a string name:
if (Cvar_VariableValue ("r_draworder"))

Interpreted prog code can access cvars with the cvar(name) or
cvar_set (name, value) internal functions:
teamplay = cvar("teamplay");
cvar_set ("registered", "1");

The user can access cvars from the console in two ways:
r_draworder			prints the current value
r_draworder 0		sets the current value to 0
Cvars are restricted from having the same names as commands to keep this
interface from being ambiguous.
*/

// changes are broadcast to all clients
#define CVAR_SERVER			1

// written to config.cfg
#define CVAR_ARCHIVE		2

// explicit value type cvar
#define CVAR_VALUE			4

// dummy cvar, only used for temp storage for menu controls that expect a cvar
// but where we don't want to give them a real one!
#define CVAR_DUMMY			8

// this cvar is written to a hud script
#define CVAR_HUD			16

// cvar will reject any attempt to set (thru cvar_set; members can be accessed directly)
// at least until we split up public/private properly...
#define CVAR_READONLY		32

// helps to prevent extra cvar scanning overhead from nehahra ugliness
#define CVAR_NEHAHRA		64

class cvar_t
{
public:
	// this one is for use when we just want a dummy cvar (e.g. to take a copy of an existing one for the menus)
	// these types are not registered and do not show up in completion lists
	cvar_t (void);
	~cvar_t (void);

	// this one allows specifying of cvars directly by usage flags
	// this is the preferred way and ultimately all cvars will be changed over to this
	cvar_t (char *cvarname, char *initialval, int useflags = 0);

	// same as above but it allows for an explicit value cvar
	cvar_t (char *cvarname, float initialval, int useflags = 0);

	char	*name;
	char	*string;
	float	value;
	int		integer;

	// usage flags
	int usage;

	// next in the chain
	cvar_t *next;
};


// overloads - set by name or variable and take float or string
void Cvar_Set (char *var_name, char *value);
void Cvar_Set (char *var_name, float value);
void Cvar_Set (cvar_t *var, float value);
void Cvar_Set (cvar_t *var, char *value);

float	Cvar_VariableValue (char *var_name);
// returns 0 if not defined or non numeric

char	*Cvar_VariableString (char *var_name);
// returns an empty string if not defined

void 	Cvar_WriteVariables (FILE *f);
// Writes lines containing "set variable value" for all variables
// with the archive flag set to true.

cvar_t *Cvar_FindVar (char *var_name);

extern cvar_t	*cvar_vars;
