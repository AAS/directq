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

// possible commands to execute
static cmd_t *cmd_functions;


/*
=============================================================================

						COMMAND AUTOCOMPLETION

=============================================================================
*/

typedef struct complist_s
{
	char type[10];
	char name[128];
} complist_t;

complist_t *complist = NULL;


void Cmd_BuildCompletionList (void)
{
	// numbers of cvars and cmds
	int numcomplist;

	// cvars and cmds
	cvar_t *var;
	cmd_t *cmd;

	// count the number of cvars and cmds we have
	for (var = cvar_vars, numcomplist = 0; var; var = var->next) numcomplist++;
	for (cmd = cmd_functions; cmd; cmd = cmd->next) numcomplist++;

	// alloc space for the completion list (add some overshoot here; we need 1 to NULL terminate the list)
	complist = (complist_t *) Heap_TagAlloc (TAG_CONSOLE, (numcomplist + 1) * sizeof (complist_t));

	// current item we're working on
	complist_t *complistcurrent = complist;

	// write in cvars
	for (var = cvar_vars; var; var = var->next, complistcurrent++)
	{
		strcpy (complistcurrent->type, "(cvar)");
		strncpy (complistcurrent->name, var->name, 127);
	}

	// write in cmds
	for (cmd = cmd_functions; cmd; cmd = cmd->next, complistcurrent++)
	{
		strcpy (complistcurrent->type, "(cmd) ");
		strncpy (complistcurrent->name, cmd->name, 127);
	}

	// sort before termination
	if (numcomplist > 0)
	{
		int	i, j;
		complist_t temp;

		for (i = 0; i < numcomplist; i++)
		{
			for (j = i + 1; j < numcomplist; j++)
			{
				if (strcmp (complist[j].name, complist[i].name) < 0)
				{
					Q_memcpy (&temp, &complist[j], sizeof (complist_t));
					Q_memcpy (&complist[j], &complist[i], sizeof (complist_t));
					Q_memcpy (&complist[i], &temp, sizeof (complist_t));
				}
			}
		}
	}

	// terminate the list
	complistcurrent->name[0] = 0;
	complistcurrent->type[0] = 0;
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

	for (cl = complist, nummatches = 0; cl->name[0] && cl->type[0]; cl++)
	{
		if (!strnicmp (partial, cl->name, len))
		{
			if (conout) Con_Printf ("%s %s\n", cl->type, cl->name);

			// copy to the current position in the cycle
			if (nummatches == matchcycle)
				strncpy (currentmatch, cl->name, 127);

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

//=============================================================================

void Cmd_ForwardToServer (void);

// if this is false we suppress all commands (except "exec") and all output
extern bool full_initialized;

#define	MAX_ALIAS_NAME	32

typedef struct cmdalias_s
{
	struct cmdalias_s	*next;
	char	name[MAX_ALIAS_NAME];
	char	*value;
} cmdalias_t;

cmdalias_t	*cmd_alias;

int trashtest;
int *trashspot;

bool	cmd_wait;

//=============================================================================

/*
============
Cmd_Wait_f

Causes execution of the remainder of the command buffer to be delayed until
next frame.  This allows commands like:
bind g "impulse 5 ; +attack ; wait ; -attack ; impulse 2"
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
	SZ_Alloc (&cmd_text, 262144);
}


/*
============
Cbuf_AddText

Adds command text at the end of the buffer
============
*/
void Cbuf_AddText (char *text)
{
	int		l;
	
	l = Q_strlen (text);

	if (cmd_text.cursize + l >= cmd_text.maxsize)
	{
		Con_Printf ("Cbuf_AddText: overflow\n");
		return;
	}

	SZ_Write (&cmd_text, text, Q_strlen (text));
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
	char	*temp;
	int		templen;

	// copy off any commands still remaining in the exec buffer
	templen = cmd_text.cursize;

	if (templen)
	{
		temp = (char *) Heap_QMalloc (templen);
		Q_memcpy (temp, cmd_text.data, templen);
		SZ_Clear (&cmd_text);
	}
	else
		temp = NULL;	// shut up compiler

	// add the entire text of the file
	Cbuf_AddText (text);

	// add the copied off data
	if (templen)
	{
		SZ_Write (&cmd_text, temp, templen);
		Heap_QFree (temp);
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
		// find a \n or ; line break
		text = (char *)cmd_text.data;

		quotes = 0;
		for (i=0 ; i< cmd_text.cursize ; i++)
		{
			if (text[i] == '"')
				quotes++;
			if ( !(quotes&1) &&  text[i] == ';')
				break;	// don't break if inside a quoted string
			if (text[i] == '\n')
				break;
		}

		memcpy (line, text, i);
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
			Q_memcpy (text, text+i, cmd_text.cursize);
		}

		// execute the command line
		Cmd_ExecuteString (line, src_command);

		if (cmd_wait)
		{	// skip out while text still remains in buffer, leaving it
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
		if (!com_argv[i])
			continue;		// NEXTSTEP nulls out -NXHost
		s += Q_strlen (com_argv[i]) + 1;
	}

	if (!s) return;

	text = (char *) Heap_QMalloc (s + 1);
	text[0] = 0;

	for (i = 1; i < com_argc; i++)
	{
		if (!com_argv[i])
			continue;		// NEXTSTEP nulls out -NXHost
		Q_strcat (text,com_argv[i]);
		if (i != com_argc - 1) Q_strcat (text, " ");
	}

	// pull out the commands
	build = (char *) Heap_QMalloc (s+1);
	build[0] = 0;

	for (i = 0; i < s - 1; i++)
	{
		if (text[i] == '+')
		{
			i++;

			for (j = i; (text[j] != '+') && (text[j] != '-') && (text[j] != 0); j++);

			c = text[j];
			text[j] = 0;

			Q_strcat (build, text + i);
			Q_strcat (build, "\n");
			text[j] = c;
			i = j - 1;
		}
	}

	if (build[0]) Cbuf_InsertText (build);

	Heap_QFree (text);
	Heap_QFree (build);
}


/*
===============
Cmd_Exec_f
===============
*/
void Cmd_Exec_f (void)
{
	char	*f;
	int		mark;

	if (Cmd_Argc () != 2)
	{
		Con_Printf ("exec <filename> : execute a script file\n");
		return;
	}

	char cfgfile[128];

	// i hate it when i forget to add ".cfg" to an exec command, so i fixed it
	strncpy (cfgfile, Cmd_Argv (1), 127);
	COM_DefaultExtension (cfgfile, ".cfg");

	f = (char *) COM_LoadTempFile (cfgfile);

	if (!f)
	{
		Con_Printf ("couldn't exec \"%s\"\n", cfgfile);
		return;
	}

	Con_Printf ("execing \"%s\"\n", cfgfile);

	// fix if a config file isn't \n terminated
	Cbuf_InsertText (f);
	Cbuf_InsertText ("\n");
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
	
	for (i=1 ; i<Cmd_Argc() ; i++)
		Con_Printf ("%s ",Cmd_Argv(i));
	Con_Printf ("\n");
}

/*
===============
Cmd_Alias_f

Creates a new command that executes a command string (possibly ; seperated)
===============
*/

char *CopyString (char *in)
{
	char	*out;

	out = (char *) Heap_QMalloc (strlen(in)+1);
	strcpy (out, in);
	return out;
}


void Cmd_Alias_f (void)
{
	cmdalias_t	*a;
	char		cmd[1024];
	int			i, c;
	char		*s;

	if (Cmd_Argc() == 1)
	{
		Con_Printf ("Current alias commands:\n");
		for (a = cmd_alias ; a ; a=a->next)
			Con_Printf ("%s : %s\n", a->name, a->value);
		return;
	}

	s = Cmd_Argv(1);
	if (strlen(s) >= MAX_ALIAS_NAME)
	{
		Con_Printf ("Alias name is too long\n");
		return;
	}

	// if the alias allready exists, reuse it
	for (a = cmd_alias ; a ; a=a->next)
	{
		if (!strcmp(s, a->name))
		{
			Heap_QFree (a->value);
			break;
		}
	}

	if (!a)
	{
		a = (cmdalias_t *) Heap_QMalloc (sizeof (cmdalias_t));
		a->next = cmd_alias;
		cmd_alias = a;
	}

	strcpy (a->name, s);	

	// copy the rest of the command line
	cmd[0] = 0;		// start out with a null string
	c = Cmd_Argc();
	for (i=2 ; i< c ; i++)
	{
		strcat (cmd, Cmd_Argv(i));
		if (i != c)
			strcat (cmd, " ");
	}
	strcat (cmd, "\n");

	a->value = CopyString (cmd);
}

/*
=============================================================================

					COMMAND EXECUTION

=============================================================================
*/

#define	MAX_ARGS		80

static	int			cmd_argc;
static	char		cmd_argv[MAX_ARGS][1024];
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
	Cmd_BuildCompletionList ();
}

/*
============
Cmd_Argc
============
*/
int		Cmd_Argc (void)
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
	if ((unsigned)arg >= cmd_argc)
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
			Q_strcpy (cmd_argv[cmd_argc], com_token);
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
		if (!Q_strcmp (newcmd->name, cmd->name))
			return;

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
		if (!Q_strcmp (cmd_name, cmd->name))
			return true;
	}

	return false;
}


/*
============
Cmd_CompleteCommand
============
*/
char *Cmd_CompleteCommand (char *partial)
{
	cmd_t	*cmd;
	int				len;
	
	len = Q_strlen(partial);
	
	if (!len)
		return NULL;

	// check functions
	for (cmd=cmd_functions ; cmd ; cmd=cmd->next)
		if (!Q_strncmp (partial,cmd->name, len))
			return cmd->name;

	return NULL;
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
	cmd_t	*cmd;
	cmdalias_t		*a;

	cmd_source = src;
	Cmd_TokenizeString (text);

	// execute the command line
	if (!Cmd_Argc())
		return;		// no tokens

	// check functions
	for (cmd = cmd_functions; cmd; cmd = cmd->next)
	{
		if (!Q_strcasecmp (cmd_argv[0], cmd->name))
		{
			if (full_initialized)
			{
				// execute normally
				cmd->function ();
				return;
			}
			else
			{
				if (!stricmp (cmd->name, "exec"))
				{
					// allow exec commands before everything comes up as they can call
					// into other configs which also store cvars
					cmd->function ();
					return;
				}
				else 
				{
					// we found it anyway but we're not really interested in it just yet
					return;
				}
			}
		}
	}

	// check alias
	for (a = cmd_alias; a; a = a->next)
	{
		if (!Q_strcasecmp (cmd_argv[0], a->name))
		{
			Cbuf_InsertText (a->value);
			return;
		}
	}

	// check cvars
	if (!Cvar_Command ())
	{
		// suppress all output until we're fully up
		if (full_initialized) Con_Printf ("Unknown command \"%s\"\n", Cmd_Argv(0));
	}
}


/*
===================
Cmd_ForwardToServer

Sends the entire command line over to the server
===================
*/
void Cmd_ForwardToServer (void)
{
	if (cls.state != ca_connected)
	{
		Con_Printf ("Can't \"%s\", not connected\n", Cmd_Argv(0));
		return;
	}
	
	if (cls.demoplayback)
		return;		// not really connected

	MSG_WriteByte (&cls.message, clc_stringcmd);
	if (Q_strcasecmp(Cmd_Argv(0), "cmd") != 0)
	{
		SZ_Print (&cls.message, Cmd_Argv(0));
		SZ_Print (&cls.message, " ");
	}
	if (Cmd_Argc() > 1)
		SZ_Print (&cls.message, Cmd_Args());
	else
		SZ_Print (&cls.message, "\n");
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
	int i;
	
	if (!parm)
	{
		Con_DPrintf ("Cmd_CheckParm: NULL\n");
		return 0;
	}

	for (i = 1; i < Cmd_Argc (); i++)
		if (! Q_strcasecmp (parm, Cmd_Argv (i)))
			return i;
			
	return 0;
}


cmd_t::cmd_t (char *cmdname, xcommand_t cmdcmd)
{
	strncpy (this->name, cmdname, 127);
	this->function = cmdcmd;

	// just add it
	Cmd_Add (this);
}

