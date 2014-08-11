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
// cmd.c -- Quake script command processing module

#include "quakedef.h"
#include "location.h"

// forward declaration of alias types for addition to completion lists
typedef struct cmdalias_s
{
	struct cmdalias_s	*next;
	char	*name;
	char	*value;
} cmdalias_t;

cmdalias_t	*cmd_alias = NULL;


// possible commands to execute
static cmd_t *cmd_functions = NULL;

void Cmd_Inc_f (void)
{
	switch (Cmd_Argc ())
	{
	default:
	case 1:
		Con_Printf ("inc <cvar> [amount] : increment cvar\n");
		break;
	case 2:
		Cvar_Set (Cmd_Argv (1), Cvar_VariableValue (Cmd_Argv (1)) + 1);
		break;
	case 3:
		Cvar_Set (Cmd_Argv (1), Cvar_VariableValue (Cmd_Argv (1)) + atof (Cmd_Argv (2)));
		break;
	}
}


cmd_t Cmd_Inc_Cmd ("inc", Cmd_Inc_f);

// this dummy function exists so that no command will have a NULL function
void Cmd_Compat (void) {}

/*
=============================================================================

						COMMAND AUTOCOMPLETION

		This is now used for actual command execution too...

=============================================================================
*/

typedef struct complist_s
{
	// note - !!!if the order here is changed then the struct inits below also need to be changed!!!
	// search for every occurance of bsearch and do the necessary...
	char *name;
	cmdalias_t *als;
	cmd_t *cmd;
	cvar_t *var;
} complist_t;

complist_t *complist = NULL;

// numbers of cvars and cmds
int numcomplist = 0;

int CmdCvarCompareFunc (const void *a, const void *b)
{
	complist_t *cc1 = (complist_t *) a;
	complist_t *cc2 = (complist_t *) b;

	return stricmp (cc1->name, cc2->name);
}


void Cmd_BuildCompletionList (void)
{
	// nothing to start with
	numcomplist = 0;

	// count the number of cvars and cmds we have
	for (cvar_t *var = cvar_vars; var; var = var->next) numcomplist++;
	for (cmd_t *cmd = cmd_functions; cmd; cmd = cmd->next) numcomplist++;
	for (cmdalias_t *ali = cmd_alias; ali; ali = ali->next) numcomplist++;
	for (cvar_alias_t *ali = cvar_alias_vars; ali; ali = ali->next) numcomplist++;

	// alloc space for the completion list (add some overshoot here; we need 1 to NULL terminate the list)
	// place in zone so that we can rebuild the list if we ever need to.
	if (complist) Zone_Free (complist);
	complist = (complist_t *) Zone_Alloc ((numcomplist + 1) * sizeof (complist_t));

	// current item we're working on
	complist_t *complistcurrent = complist;

	// write in cvars
	for (cvar_t *var = cvar_vars; var; var = var->next, complistcurrent++)
	{
		complistcurrent->name = var->name;
		complistcurrent->als = NULL;
		complistcurrent->cmd = NULL;
		complistcurrent->var = var;
	}

	// write in alias cvars
	for (cvar_alias_t *ali = cvar_alias_vars; ali; ali = ali->next, complistcurrent++)
	{
		complistcurrent->name = ali->name;
		complistcurrent->als = NULL;
		complistcurrent->cmd = NULL;
		complistcurrent->var = ali->var;
	}

	// write in cmds
	for (cmd_t *cmd = cmd_functions; cmd; cmd = cmd->next, complistcurrent++)
	{
		// replace NULL function commands with a dummy that does nothing
		if (!cmd->function)
		{
			cmd->usage = CMD_COMPAT;
			cmd->function = Cmd_Compat;
		}

		complistcurrent->name = cmd->name;
		complistcurrent->als = NULL;
		complistcurrent->cmd = cmd;
		complistcurrent->var = NULL;
	}

	// write in aliases
	for (cmdalias_t *als = cmd_alias; als; als = als->next, complistcurrent++)
	{
		complistcurrent->name = als->name;
		complistcurrent->als = als;
		complistcurrent->cmd = NULL;
		complistcurrent->var = NULL;
	}

	// sort before termination
	qsort ((void *) complist, numcomplist, sizeof (complist_t), CmdCvarCompareFunc);

	// terminate the list
	complistcurrent->name = NULL;
	complistcurrent->als = NULL;
	complistcurrent->cmd = NULL;
	complistcurrent->var = NULL;
}


void Key_PrintMatch (char *cmd);

int Cmd_Match (char *partial, int matchcycle, bool conout)
{
	complist_t *cl = NULL;
	int len = strlen (partial);
	int nummatches;
	char currentmatch[128];

	if (!len) return 0;

	if (conout) Con_Printf ("]%s\n", partial);

	for (cl = complist, nummatches = 0; cl->name && (cl->cmd || cl->var || cl->als); cl++)
	{
		// skip nehahra if we're not running nehahra
		if (cl->var && !nehahra && (cl->var->usage & CVAR_NEHAHRA)) continue;

		// skip compatibility cvars
		if (cl->var && (cl->var->usage & CVAR_COMPAT)) continue;

		// skip compatibility commands
		if (cl->cmd && (cl->cmd->usage == CMD_COMPAT)) continue;

		assert (cl->name);
		assert ((cl->als || cl->cmd || cl->var));

		if (!strnicmp (partial, cl->name, len))
		{
			if (conout)
			{
				if (cl->cmd)
					Con_Printf ("  (cmd) ");
				else if (cl->var)
					Con_Printf (" (cvar) ");
				else if (cl->als)
					Con_Printf ("(alias) ");
				else Con_Printf ("  (bad) ");

				Con_Printf ("%s\n", cl->name);
			}

			// copy to the current position in the cycle
			if (nummatches == matchcycle)
				Q_strncpy (currentmatch, cl->name, 127);

			nummatches++;
		}
	}

	if (!nummatches)
	{
		// note - the full list is pretty huge - over 400 lines - so it's worse than useless putting it on-screen
		Con_Printf ("Could not match \"%s\"\n", partial);
		return 0;
	}

	// fill the command buffer with the current cycle position
	Key_PrintMatch (currentmatch);

	// return the number of matches
	return nummatches;
}


cvar_t *Cmd_FindCvar (char *name)
{
	complist_t key = {name, NULL, NULL, NULL};
	complist_t *cl = (complist_t *) bsearch (&key, complist, numcomplist, sizeof (complist_t), CmdCvarCompareFunc);

	if (!cl)
		return NULL;
	else if (cl->var)
	{
		// skip nehahra cvars if we're not running nehahra
		if (!nehahra && (cl->var->usage & CVAR_NEHAHRA)) return NULL;

		return cl->var;
	}
	else return NULL;
}


//=============================================================================

void CmdCvarList (bool dumpcmd, bool dumpvar)
{
	if (Cmd_Argc () == 1)
	{
		for (int i = 0; i < numcomplist; i++)
		{
			if (complist[i].cmd && dumpcmd) Con_Printf ("%s\n", complist[i].name);
			if (complist[i].var && dumpvar) Con_Printf ("%s\n", complist[i].name);
		}

		Con_Printf ("Use \"%s <filename>\" to dump to file\n", Cmd_Argv (0));
		return;
	}

	// because I just know somebody's gonna do this...
	char filename[MAX_PATH] = {0};

	for (int i = 1; i < Cmd_Argc (); i++)
	{
		// prevent a buffer overrun here
		if ((strlen (filename) + strlen (Cmd_Argv (i)) + 2) >= MAX_PATH) break;

		strcat (filename, Cmd_Argv (i));
		strcat (filename, " ");
	}

	for (int i = strlen (filename) - 1; i; i--)
	{
		if (filename[i] == ' ')
		{
			filename[i] = 0;
			break;
		}
	}

	FILE *f = fopen (filename, "w");

	if (!f)
	{
		Con_Printf ("Couldn't create \"%s\"\n", filename);
		return;
	}

	Con_Printf ("Dumping %s to \"%s\"... ", Cmd_Argv (0), filename);

	for (int i = 0; i < numcomplist; i++)
	{
		if (complist[i].cmd && dumpcmd) fprintf (f, "%s\n", complist[i].name);
		if (complist[i].var && dumpvar) fprintf (f, "%s\n", complist[i].name);
	}

	Con_Printf ("done\n");
	fclose (f);
}


void CmdList_f (void) {CmdCvarList (true, false);}
void CvarList_f (void) {CmdCvarList (false, true);}
void CmdCvarList_f (void) {CmdCvarList (true, true);}

cmd_t CmdList_Cmd ("cmdlist", CmdList_f);
cmd_t CvarList_Cmd ("cvarlist", CvarList_f);
cmd_t CmdCvarList_Cmd ("cmdcvarlist", CmdCvarList_f);

//=============================================================================

void Cmd_ForwardToServer (void);

// if this is false we suppress all commands (except "exec") and all output
extern bool full_initialized;

bool	cmd_wait;

//=============================================================================

/*
============
Cmd_Wait_f

Causes execution of the remainder of the command buffer to be delayed until
next frame.  This allows commands like:
bind g "impulse 5; +attack; wait; -attack; impulse 2"
============
*/
void Cmd_Wait_f (void)
{
	cmd_wait = true;
}


/*
=============================================================================

						COMMAND BUFFER

=============================================================================
*/

sizebuf_t	cmd_text;

/*
============
Cbuf_Init
============
*/
void Cbuf_Init (void)
{
	// space for commands and script files
	// take 64 k
	SZ_Alloc (&cmd_text, 0x10000);
}


/*
============
Cbuf_AddText

Adds command text at the end of the buffer
============
*/
void Cbuf_AddText (char *text)
{
	int l = strlen (text);

	if (cmd_text.cursize + l >= cmd_text.maxsize)
	{
		Con_Printf ("Cbuf_AddText: overflow\n");
		return;
	}

	SZ_Write (&cmd_text, text, strlen (text));
}


/*
============
Cbuf_InsertText

Adds command text immediately after the current command
Adds a \n to the text
FIXME: actually change the command buffer to do less copying
============
*/
void Cbuf_InsertText (char *text)
{
	char	*temp = NULL;
	int		templen = 0;

	// copy off any commands still remaining in the exec buffer
	templen = cmd_text.cursize;

	if (templen)
	{
		temp = (char *) Zone_Alloc (templen);
		Q_MemCpy (temp, cmd_text.data, templen);
		SZ_Clear (&cmd_text);
	}

	// add the entire text of the file
	Cbuf_AddText (text);

	// add the copied off data
	if (templen)
	{
		SZ_Write (&cmd_text, temp, templen);
		Zone_Free (temp);
	}
}


/*
============
Cbuf_Execute
============
*/
void Cbuf_Execute (void)
{
	int		i;
	char	*text;
	char	line[1024];
	int		quotes;

	while (cmd_text.cursize)
	{
		// find a \n or; line break
		text = (char *) cmd_text.data;

		quotes = 0;

		for (i = 0; i < cmd_text.cursize; i++)
		{
			if (text[i] == '"') quotes++;

			// don't break if inside a quoted string
			if (!(quotes & 1) &&  text[i] == ';') break;
			if (text[i] == '\n') break;
		}

		Q_MemCpy (line, text, i);
		line[i] = 0;

		// delete the text from the command buffer and move remaining commands down
		// this is necessary because commands (exec, alias) can insert data at the
		// beginning of the text buffer
		if (i == cmd_text.cursize)
			cmd_text.cursize = 0;
		else
		{
			i++;
			cmd_text.cursize -= i;
			Q_MemCpy (text, text + i, cmd_text.cursize);
		}

		// execute the command line
		Cmd_ExecuteString (line, src_command);

		if (cmd_wait)
		{
			// skip out while text still remains in buffer, leaving it
			// for next frame
			cmd_wait = false;
			break;
		}
	}
}


/*
==============================================================================

						SCRIPT COMMANDS

==============================================================================
*/

/*
===============
Cmd_StuffCmds_f

Adds command line parameters as script statements
Commands lead with a +, and continue until a - or another +
quake +prog jctest.qp +cmd amlev1
quake -nosound +cmd amlev1
===============
*/
void Cmd_StuffCmds_f (void)
{
	int		i, j;
	int		s;
	char	*text, *build, c;

	if (Cmd_Argc () != 1)
	{
		Con_Printf ("stuffcmds : execute command line parameters\n");
		return;
	}

	// build the combined string to parse from
	s = 0;

	for (i = 1; i < com_argc; i++)
	{
		// NEXTSTEP nulls out -NXHost
		if (!com_argv[i]) continue;

		s += strlen (com_argv[i]) + 1;
	}

	if (!s) return;

	text = (char *) Zone_Alloc (s + 1);
	text[0] = 0;

	for (i = 1; i < com_argc; i++)
	{
		// NEXTSTEP nulls out -NXHost
		if (!com_argv[i]) continue;

		strcat (text, com_argv[i]);
		if (i != com_argc - 1) strcat (text, " ");
	}

	// pull out the commands
	build = (char *) Zone_Alloc (s + 1);
	build[0] = 0;

	for (i = 0; i < s - 1; i++)
	{
		if (text[i] == '+')
		{
			i++;

			for (j = i; (text[j] != '+') && (text[j] != '-') && (text[j] != 0); j++);

			c = text[j];
			text[j] = 0;

			strcat (build, text + i);
			strcat (build, "\n");
			text[j] = c;
			i = j - 1;
		}
	}

	if (build[0]) Cbuf_InsertText (build);

	Zone_Free (text);
	Zone_Free (build);
}


/*
===============
Cmd_Exec_f
===============
*/
void Cmd_Exec_f (void)
{
	if (Cmd_Argc () != 2)
	{
		Con_SafePrintf ("exec <filename> : execute a script file\n");
		return;
	}

	char cfgfile[128];
	Q_strncpy (cfgfile, Cmd_Argv (1), 127);
	char *f = (char *) COM_LoadFile (cfgfile);

	if (!f)
	{
		// i hate it when i forget to add ".cfg" to an exec command, so i fixed it
		COM_DefaultExtension (cfgfile, ".cfg");
		f = (char *) COM_LoadFile (cfgfile);

		if (!f)
		{
			Con_SafePrintf ("couldn't exec \"%s\"\n", cfgfile);
			return;
		}
	}

	Con_SafePrintf ("execing \"%s\"\n", cfgfile);

	// fix if a config file isn't \n terminated
	Cbuf_InsertText (f);
	Cbuf_InsertText ("\n");
	Zone_Free (f);
}


/*
===============
Cmd_Echo_f

Just prints the rest of the line to the console
===============
*/
void Cmd_Echo_f (void)
{
	int		i;

	for (i = 1; i < Cmd_Argc (); i++)
		Con_Printf ("%s ",Cmd_Argv (i));

	Con_Printf ("\n");
}

/*
===============
Cmd_Alias_f

Creates a new command that executes a command string (possibly; seperated)
===============
*/

void Cmd_Alias_f (void)
{
	cmdalias_t	*a;
	char		cmd[1024];
	int			i, c;
	char		*s;

	if (Cmd_Argc () == 1)
	{
		Con_Printf ("Current alias commands:\n");

		for (a = cmd_alias; a; a = a->next)
			Con_Printf ("\"%s\" : \"%s\"\n", a->name, a->value);

		return;
	}

	s = Cmd_Argv (1);

	// try to find it first so that we can access it quickly for printing/etc
	complist_t key = {s, NULL, NULL, NULL};
	complist_t *cl = (complist_t *) bsearch (&key, complist, numcomplist, sizeof (complist_t), CmdCvarCompareFunc);

	if (Cmd_Argc () == 2)
	{
		if (cl)
		{
			// protect us oh lord from stupid programmer errors
			assert (cl->name);
			assert ((cl->als || cl->cmd || cl->var));

			if (cl->als)
				Con_Printf ("\"%s\" : \"%s\"\n", cl->als->name, cl->als->value);
			else if (cl->cmd)
				Con_Printf ("\"%s\" is a command\n", s);
			else if (cl->var)
				Con_Printf ("\"%s\" is a cvar\n", s);
		}
		else Con_Printf ("alias \"%s\" is not found\n", s);

		return;
	}

	if (cl)
	{
		// protect us oh lord from stupid programmer errors
		assert (cl->name);
		assert ((cl->als || cl->cmd || cl->var));

		if (cl->als)
		{
			// if the alias already exists we reuse it, just free the value
			Zone_Free (cl->als->value);
			cl->als->value = NULL;
			a = cl->als;
		}
		else if (cl->cmd)
		{
			Con_Printf ("\"%s\" is already a command\n", s);
			return;
		}
		else if (cl->var)
		{
			Con_Printf ("\"%s\" is already a cvar\n", s);
			return;
		}
	}
	else
	{
		// create a new alias
		a = (cmdalias_t *) Zone_Alloc (sizeof (cmdalias_t));

		// this is a safe strcpy cos we define the dest size ourselves
		a->name = (char *) Zone_Alloc (strlen (s) + 1);
		strcpy (a->name, s);

		// link it into the alias list
		a->next = cmd_alias;
		cmd_alias = a;
	}

	// ensure that we haven't missed anything or been stomped
	assert (a);

	// start out with a null string
	cmd[0] = 0;

	// copy the rest of the command line
	c = Cmd_Argc ();

	for (i = 2; i < c; i++)
	{
		strcat (cmd, Cmd_Argv (i));

		if (i != c)
			strcat (cmd, " ");
	}

	strcat (cmd, "\n");

	a->value = (char *) Zone_Alloc (strlen (cmd) + 1);
	strcpy (a->value, cmd);

	// rebuild the autocomplete list
	Cmd_BuildCompletionList ();
}


/*
=============================================================================

					COMMAND EXECUTION

=============================================================================
*/

#define	MAX_ARGS		80

static	int			cmd_argc;
static	char		**cmd_argv = NULL;
static	char		*cmd_null_string = "";
static	char		*cmd_args = NULL;

cmd_source_t	cmd_source;


/*
============
Cmd_Init
============
*/
cmd_t Cmd_StuffCmds_Cmd ("stuffcmds",Cmd_StuffCmds_f);
cmd_t Cmd_Exec_Cmd ("exec",Cmd_Exec_f);
cmd_t Cmd_Echo_Cmd ("echo",Cmd_Echo_f);
cmd_t Cmd_Alias_Cmd ("alias",Cmd_Alias_f);
cmd_t Cmd_ForwardToServer_Cmd ("cmd", Cmd_ForwardToServer);
cmd_t Cmd_Wait_Cmd ("wait", Cmd_Wait_f);

void Cmd_Init (void)
{
	// all our cvars and cmds are up now, so we build the sorted autocomplete list
	// this can be dynamically rebuilt at run time; e.g. if a new alias is added
	Cmd_BuildCompletionList ();
}


/*
============
Cmd_Argc
============
*/
int Cmd_Argc (void)
{
	return cmd_argc;
}

/*
============
Cmd_Argv
============
*/
char *Cmd_Argv (int arg)
{
	if ((unsigned) arg >= cmd_argc)
		return cmd_null_string;

	return cmd_argv[arg];	
}


/*
============
Cmd_Args
============
*/
char *Cmd_Args (void)
{
	return cmd_args;
}


/*
============
Cmd_TokenizeString

Parses the given string into command line tokens.
============
*/
void Cmd_TokenizeString (char *text)
{
	int		i;

	if (!cmd_argv)
	{
		// because these are reused every frame as commands are executed,
		// we put them in a large one-time-only block instead of dynamically allocating
		cmd_argv = (char **) Zone_Alloc (80 * sizeof (char *));

		for (i = 0; i < MAX_ARGS; i++)
			cmd_argv[i] = (char *) Zone_Alloc (1024);
	}

	cmd_argc = 0;
	cmd_args = NULL;

	while (1)
	{
		// skip whitespace up to a /n
		while (*text && *text <= ' ' && *text != '\n')
			text++;

		if (*text == '\n')
		{
			// a newline seperates commands in the buffer
			text++;
			break;
		}

		if (!*text) return;

		if (cmd_argc == 1) cmd_args = text;

		if (!(text = COM_Parse (text))) return;

		if (cmd_argc < MAX_ARGS)
		{
			// prevent overflow
			Q_strncpy (cmd_argv[cmd_argc], com_token, 1023);
			cmd_argc++;
		}
	}
}


/*
============
Cmd_Add
============
*/
void Cmd_Add (cmd_t *newcmd)
{
	// fail if the command is a variable name
	if (Cvar_VariableString (newcmd->name)[0]) return;

	// fail if the command already exists
	for (cmd_t *cmd = cmd_functions; cmd; cmd = cmd->next)
	{
		if (!strcmp (newcmd->name, cmd->name))
		{
			// silent fail
			return;
		}
	}

	if (!newcmd->function)
	{
		newcmd->function = Cmd_Compat;
		newcmd->usage = CMD_COMPAT;
	}
	else newcmd->usage = CMD_NORMAL;

	// link in
	newcmd->next = cmd_functions;
	cmd_functions = newcmd;
}


/*
============
Cmd_Exists
============
*/
bool Cmd_Exists (char *cmd_name)
{
	cmd_t	*cmd;

	for (cmd = cmd_functions; cmd; cmd = cmd->next)
	{
		if (!strcmp (cmd_name, cmd->name))
			return true;
	}

	return false;
}


/*
============
Cmd_ExecuteString

A complete command line has been parsed, so try to execute it
FIXME: lookupnoadd the token to speed search?
============
*/
void Cmd_ExecuteString (char *text, cmd_source_t src)
{
	cmd_source = src;
	Cmd_TokenizeString (text);

	// execute the command line
	// check for tokens
	if (!Cmd_Argc ()) return;

	// run a binary search for faster comparison
	// reuse the autocomplete list for this as it's already a sorted array
	complist_t key = {cmd_argv[0], NULL, NULL, NULL};
	complist_t *cl = (complist_t *) bsearch (&key, complist, numcomplist, sizeof (complist_t), CmdCvarCompareFunc);

	if (!cl)
	{
		// only complain if we're up fully
		if (full_initialized)
			Con_Printf ("Unknown command \"%s\"\n", Cmd_Argv (0));

		return;
	}
	else
	{
		assert (cl->name);
		assert ((cl->als || cl->cmd || cl->var));

		if (cl->cmd)
		{
			// skip compatibility commands
			if (cl->cmd->usage == CMD_COMPAT) return;

			if (full_initialized)
			{
				// execute normally
				cl->cmd->function ();
				return;
			}
			else
			{
				if (!stricmp (cl->cmd->name, "exec"))
				{
					// allow exec commands before everything comes up as they can call
					// into other configs which also store cvars
					cl->cmd->function ();
					return;
				}
			}
		}
		else if (cl->als)
		{
			Cbuf_InsertText (cl->als->value);
			return;
		}
		else if (cl->var)
		{
			// skip nehahra cvars if we're not running nehahra
			if (!nehahra && (cl->var->usage & CVAR_NEHAHRA))
			{
				Con_Printf ("Unknown command \"%s\"\n", cl->var->name);
				return;
			}

			// silently ignore compatibility cvars
			if (cl->var->usage & CVAR_COMPAT) return;

			// perform a variable print or set
			if (Cmd_Argc () == 1)
				Con_Printf ("\"%s\" is \"%s\"\n", cl->var->name, cl->var->string);
			else Cvar_Set (cl->var, Cmd_Argv (1));
		}
	}
}


/*
===================
Cmd_ForwardToServer

Sends the entire command line over to the server
===================
*/
// JPG - added these for %r formatting
cvar_t	pq_needrl ("pq_needrl", "I need RL", CVAR_ARCHIVE);
cvar_t	pq_haverl ("pq_haverl", "I have RL", CVAR_ARCHIVE);
cvar_t	pq_needrox ("pq_needrox", "I need rockets", CVAR_ARCHIVE);

// JPG - added these for %p formatting
cvar_t	pq_quad ("pq_quad", "quad", CVAR_ARCHIVE);
cvar_t	pq_pent ("pq_pent", "pent", CVAR_ARCHIVE);
cvar_t	pq_ring ("pq_ring", "eyes", CVAR_ARCHIVE);

// JPG 3.00 - added these for %w formatting
cvar_t	pq_weapons ("pq_weapons", "SSG:NG:SNG:GL:RL:LG", CVAR_ARCHIVE);
cvar_t	pq_noweapons ("pq_noweapons", "no weapons", CVAR_ARCHIVE);

void Cmd_ForwardToServer (void)
{
	char *src, *dst, buff[128];			// JPG - used for say/say_team formatting
	int minutes, seconds, match_time;	// JPG - used for %t

	if (cls.state != ca_connected)
	{
		Con_Printf ("Can't \"%s\", not connected\n", Cmd_Argv (0));
		return;
	}

	// not really connected
	if (cls.demoplayback) return;

	MSG_WriteByte (&cls.message, clc_stringcmd);

	// JPG - handle say separately for formatting
	if ((!stricmp (Cmd_Argv (0), "say") || !stricmp (Cmd_Argv (0), "say_team")) && Cmd_Argc () > 1)
	{
		SZ_Print (&cls.message, Cmd_Argv (0));
		SZ_Print (&cls.message, " ");

		src = Cmd_Args ();
		dst = buff;

		while (*src && dst - buff < 100)
		{
			if (*src == '%')
			{
				// mh - made this case-insensitive
				switch (*++src)
				{
				case 'H':
				case 'h':
					dst += sprintf (dst, "%d", cl.stats[STAT_HEALTH]);
					break;

				case 'A':
				case 'a':
					dst += sprintf (dst, "%d", cl.stats[STAT_ARMOR]);
					break;

				case 'R':
				case 'r':
					if (cl.stats[STAT_HEALTH] > 0 && (cl.items & IT_ROCKET_LAUNCHER))
					{
						if (cl.stats[STAT_ROCKETS] < 5)
							dst += sprintf (dst, "%s", pq_needrox.string);
						else
							dst += sprintf (dst, "%s", pq_haverl.string);
					}
					else dst += sprintf (dst, "%s", pq_needrl.string);

					break;

				case 'L':
				case 'l':
					dst += sprintf (dst, "%s", LOC_GetLocation (cl_entities[cl.viewentity]->origin));
					break;

				case 'D':
				case 'd':
					dst += sprintf (dst, "%s", LOC_GetLocation (cl.death_location));
					break;

				case 'C':
				case 'c':
					dst += sprintf (dst, "%d", cl.stats[STAT_CELLS]);
					break;

				case 'X':
				case 'x':
					dst += sprintf (dst, "%d", cl.stats[STAT_ROCKETS]);
					break;

				case 'P':
				case 'p':
					if (cl.stats[STAT_HEALTH] > 0)
					{
						if (cl.items & IT_QUAD)
						{
							dst += sprintf (dst, "%s", pq_quad.string);
							if (cl.items & (IT_INVULNERABILITY | IT_INVISIBILITY)) *dst++ = ',';
						}

						if (cl.items & IT_INVULNERABILITY)
						{
							dst += sprintf (dst, "%s", pq_pent.string);
							if (cl.items & IT_INVISIBILITY) *dst++ = ',';
						}

						if (cl.items & IT_INVISIBILITY) dst += sprintf (dst, "%s", pq_ring.string);
					}
					break;

				case 'W':
				case 'w':	// JPG 3.00
					{
						int first = 1;
						int item;
						char *ch = pq_weapons.string;

						if (cl.stats[STAT_HEALTH] > 0)
						{
							for (item = IT_SUPER_SHOTGUN; item <= IT_LIGHTNING; item *= 2)
							{
								if (*ch != ':' && (cl.items & item))
								{
									if (!first) *dst++ = ',';
									first = 0;

									while (*ch && *ch != ':')
										*dst++ = *ch++;
								}

								while (*ch && *ch != ':') *ch++;
								if (*ch) *ch++;
								if (!*ch) break;
							}
						}

						if (first) dst += sprintf (dst, "%s", pq_noweapons.string);
					}

					break;

				case '%':
					*dst++ = '%';
					break;

				case 'T':
				case 't':
					if ((cl.minutes || cl.seconds) && cl.seconds < 128)
					{
						if (cl.match_pause_time)
							match_time = ceil (60.0 * cl.minutes + cl.seconds - (cl.match_pause_time - cl.last_match_time));
						else match_time = ceil (60.0 * cl.minutes + cl.seconds - (cl.time - cl.last_match_time));

						minutes = match_time / 60;
						seconds = match_time - 60 * minutes;
					}
					else
					{
						minutes = cl.time / 60;
						seconds = cl.time - 60 * minutes;
						minutes &= 511;
					}

					dst += sprintf (dst, "%d:%02d", minutes, seconds);
					break;

				default:
					*dst++ = '%';
					*dst++ = *src;
					break;
				}

				if (*src) src++;
			}
			else *dst++ = *src++;
		}

		*dst = 0;
		SZ_Print (&cls.message, buff);
		return;
	}

	if (stricmp (Cmd_Argv(0), "cmd") != 0)
	{
		SZ_Print (&cls.message, Cmd_Argv (0));
		SZ_Print (&cls.message, " ");
	}

	if (Cmd_Argc () > 1)
		SZ_Print (&cls.message, Cmd_Args ());
	else SZ_Print (&cls.message, "\n");
}


/*
================
Cmd_CheckParm

Returns the position (1 to argc-1) in the command's argument list
where the given parameter apears, or 0 if not present
================
*/

int Cmd_CheckParm (char *parm)
{
	if (!parm)
	{
		Con_DPrintf ("Cmd_CheckParm: NULL\n");
		return 0;
	}

	for (int i = 1; i < Cmd_Argc (); i++)
		if (!stricmp (parm, Cmd_Argv (i)))
			return i;

	return 0;
}


cmd_t::cmd_t (char *cmdname, xcommand_t cmdcmd)
{
	this->name = (char *) Zone_Alloc (strlen (cmdname) + 1);

	strcpy (this->name, cmdname);

	if (cmdcmd)
	{
		this->function = cmdcmd;
		this->usage = CMD_NORMAL;
	}
	else
	{
		this->function = Cmd_Compat;
		this->usage = CMD_COMPAT;
	}

	// just add it
	Cmd_Add (this);
}

