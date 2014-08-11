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
// console.c

#include "versions.h"

#ifdef NeXT
#include <libc.h>
#endif
#ifndef _MSC_VER
#include <unistd.h>
#endif
#include <fcntl.h>
#include "quakedef.h"

int 		con_linewidth;

#define		CON_CURSORSPEED		4.0f

#define		CON_TEXTSIZE	0x40000

bool 	con_forcedup;		// because no entities to refresh

int			con_totallines;		// total lines in console scrollback
int			con_backscroll;		// lines up from bottom to display
int			con_current;		// where next message will be printed
int			con_x;				// offset in current line for next print
char		*con_text = 0;

cvar_t		con_notifytime ("con_notifytime", "3");		//seconds
cvar_t		con_lineheight ("con_lineheight", "8", CVAR_ARCHIVE);
cvar_t		con_notify_lines ("con_notifylines", 5, CVAR_ARCHIVE);

// this define was a bit misnamed
#define	CON_MAXNOTIFYLINES 256

float		con_times[CON_MAXNOTIFYLINES];	// realtime time the line was generated
											// for transparent notify lines

int			con_vislines;

bool	con_debuglog;

#define		MAXCMDLINE	256
extern	char	key_lines[32][MAXCMDLINE];
extern	int		edit_line;
extern	int		key_linepos;
extern int	key_insert; // insert key toggle

bool	con_initialized;

int			con_notifylines;		// scan lines to clear for notify lines

extern void M_Menu_Main_f (void);

/*
================
Con_ToggleConsole_f
================
*/
void Con_RemoveConsole (void)
{
	key_dest = key_game;
	key_lines[edit_line][1] = 0;	// clear any typing
	key_linepos = 1;

	SCR_EndLoadingPlaque ();
	memset (con_times, 0, sizeof (con_times));
}


void Con_ToggleConsole_f (void)
{
	if (key_dest == key_console)
	{
		if (cls.state == ca_connected)
		{
			key_dest = key_game;
			key_lines[edit_line][1] = 0;	// clear any typing
			key_linepos = 1;
		}
		else
		{
			M_Menu_Main_f ();
		}
	}
	else key_dest = key_console;

	SCR_EndLoadingPlaque ();
	memset (con_times, 0, sizeof (con_times));
}

/*
================
Con_Clear_f
================
*/
void Con_Clear_f (void)
{
	if (con_text)
		memset (con_text, ' ', CON_TEXTSIZE);
}


/*
================
Con_ClearNotify
================
*/
void Con_ClearNotify (void)
{
	int		i;

	for (i = 0; i < CON_MAXNOTIFYLINES; i++)
		con_times[i] = 0;
}


/*
================
Con_MessageMode_f
================
*/
extern bool team_message;

void Con_MessageMode_f (void)
{
	key_dest = key_message;
	team_message = false;
}


/*
================
Con_MessageMode2_f
================
*/
void Con_MessageMode2_f (void)
{
	key_dest = key_message;
	team_message = true;
}


/*
================
Con_CheckResize

If the line width has changed, reformat the buffer.
================
*/
void Con_CheckResize (void)
{
	int		i, j, width, oldwidth, oldtotallines, numlines, numchars;

	width = (vid.width >> 3) - 2;

	if (width == con_linewidth)
		return;

	if (width < 1)			// video hasn't been initialized yet
	{
		width = 78;	// 640 width
		con_linewidth = width;
		con_totallines = CON_TEXTSIZE / con_linewidth;
		memset (con_text, ' ', CON_TEXTSIZE);
	}
	else
	{
		oldwidth = con_linewidth;
		con_linewidth = width;
		oldtotallines = con_totallines;
		con_totallines = CON_TEXTSIZE / con_linewidth;
		numlines = oldtotallines;

		if (con_totallines < numlines)
			numlines = con_totallines;

		numchars = oldwidth;

		if (con_linewidth < numchars)
			numchars = con_linewidth;

		char *tbuf = (char *) Zone_Alloc (CON_TEXTSIZE);

		memcpy (tbuf, con_text, CON_TEXTSIZE);
		memset (con_text, ' ', CON_TEXTSIZE);

		for (i = 0; i < numlines; i++)
		{
			for (j = 0; j < numchars; j++)
			{
				con_text[(con_totallines - 1 - i) * con_linewidth + j] =
					tbuf[((con_current - i + oldtotallines) %
							oldtotallines) * oldwidth + j];
			}
		}

		Zone_Free (tbuf);
		Con_ClearNotify ();
	}

	con_backscroll = 0;
	con_current = con_totallines - 1;
}


/*
================
Con_Init
================
*/
cmd_t Con_ToggleConsole_f_Cmd ("toggleconsole", Con_ToggleConsole_f);
cmd_t Con_MessageMode_f_Cmd ("messagemode", Con_MessageMode_f);
cmd_t Con_MessageMode2_f_Cmd ("messagemode2", Con_MessageMode2_f);
cmd_t Con_Clear_f_Cmd ("clear", Con_Clear_f);
cmd_t Con_Cls_f_Cmd ("cls", Con_Clear_f);
cvar_t condebug ("condebug", 0.0f);

void Con_Init (void)
{
#define MAXGAMEDIRLEN	1000
	char	temp[MAXGAMEDIRLEN+1];
	char	*t2 = "/qconsole.log";

	con_debuglog = COM_CheckParm ("-condebug");

	if (con_debuglog)
	{
		if (strlen (com_gamedir) < (MAXGAMEDIRLEN - strlen (t2)))
		{
			_snprintf (temp, 1001, "%s%s", com_gamedir, t2);
			unlink (temp);
		}
	}

	con_text = (char *) Zone_Alloc (CON_TEXTSIZE);
	memset (con_text, ' ', CON_TEXTSIZE);
	con_linewidth = -1;
	Con_CheckResize ();

	Con_Printf ("Console initialized.\n");

	con_initialized = true;
}


/*
===============
Con_Linefeed
===============
*/
void Con_Linefeed (void)
{
	con_x = 0;
	if (con_backscroll) con_backscroll++;
	con_current++;
	memset (&con_text[(con_current % con_totallines) * con_linewidth], ' ', con_linewidth);
}

/*
================
Con_Print

Handles cursor positioning, line wrapping, etc
All console printing must go through this in order to be logged to disk
If no console is visible, the notify window will pop up.
================
*/
static void Con_Print (char *txt, bool silent)
{
	int		y;
	int		c, l;
	static int	cr = 0;
	int		mask;

	// con_backscroll = 0;

	if (txt[0] == 1)
	{
		mask = 128;		// go to colored text
		S_LocalSound ("misc/talk.wav");
		// play talk wav
		txt++;
	}
	else if (txt[0] == 2)
	{
		mask = 128;		// go to colored text
		txt++;
	}
	else mask = 0;

	while ((c = *txt))
	{
		// count word length
		for (l = 0; l < con_linewidth; l++)
			if (txt[l] <= ' ')
				break;

		// word wrap
		if (l != con_linewidth && (con_x + l > con_linewidth))
			con_x = 0;

		txt++;

		if (cr)
		{
			con_current--;
			cr = false;
		}


		if (!con_x)
		{
			Con_Linefeed ();

			// mark time for transparent overlay
			if (con_current >= 0 && !silent)
				con_times[con_current % CON_MAXNOTIFYLINES] = realtime;
		}

		switch (c)
		{
		case '\n':
			con_x = 0;
			break;

		case '\r':
			con_x = 0;
			cr = 1;
			break;

		default:	// display character and advance
			y = con_current % con_totallines;
			con_text[y *con_linewidth+con_x] = c | mask;
			con_x++;

			if (con_x >= con_linewidth)
				con_x = 0;

			break;
		}
	}
}


/*
================
Con_DebugLog
================
*/
#define	MAXPRINTMSG	4096

char *ConDebugBuffer = NULL;
int ConDebugBufferLen = 0;

void Con_DebugFlush (char *file, char *fmt)
{
	if (!fmt[0]) return;

	FILE *f = fopen (file, "a");

	if (!f) return;

	// switch off buffering in mode 2 or if developer 1
	// (there doesn't seem to be much difference in these)
	if (condebug.integer > 1 || developer.integer)
		setvbuf (f, NULL, _IONBF, 0);

	fprintf (f, "%s", fmt);

	if (condebug.integer > 1 || developer.integer)
		fflush (f);

	fclose (f);
}


void Con_DebugLog (char *file, char *fmt)
{
	if (!ConDebugBuffer)
	{
		// alloc at MAXPRINTMSG so it won't ever need to be expanded
		ConDebugBuffer = (char *) Zone_Alloc (MAXPRINTMSG);
		ConDebugBuffer[0] = 0;
		ConDebugBufferLen = 0;
	}

	// use fast in-memory buffering if connected
	if (cls.state == ca_connected)
	{
		if (!(condebug.integer > 1 || developer.integer))
		{
			int len = strlen (fmt);

			if (len + ConDebugBufferLen + 1 >= MAXPRINTMSG)
			{
				// flush a full buffer
				Con_DebugFlush (file, ConDebugBuffer);
				ConDebugBuffer[0] = 0;
				ConDebugBufferLen = 0;
			}

			// append current text to the buffer
			strcat (ConDebugBuffer, fmt);
			ConDebugBufferLen += len;
			return;
		}
	}

	// flush anything from the current buffer
	Con_DebugFlush (file, ConDebugBuffer);
	ConDebugBuffer[0] = 0;
	ConDebugBufferLen = 0;

	// write the current message
	Con_DebugFlush (file, fmt);
}


/*
================
Con_Printf

Handles cursor positioning, line wrapping, etc
================
*/
void Menu_PutConsolePrintInbuffer (char *text);

static void Con_PrintfCommon (char *msg, bool silent)
{
	// log all messages to file
	if (con_debuglog || condebug.integer) Con_DebugLog (va ("%s/qconsole.log", com_gamedir), msg);

	if (!con_initialized) return;

	// write it to the scrollable buffer
	Con_Print (msg, silent);
}


void Con_SilentPrintf (char *fmt, ...)
{
	va_list		argptr;
	static char		msg[MAXPRINTMSG];

	va_start (argptr, fmt);
	_vsnprintf (msg, MAXPRINTMSG, fmt, argptr);
	va_end (argptr);

	Con_PrintfCommon (msg, true);
}


void Con_Printf (char *fmt, ...)
{
	va_list		argptr;
	static char		msg[MAXPRINTMSG];
	static bool	inupdate = false;

	va_start (argptr, fmt);
	_vsnprintf (msg, MAXPRINTMSG, fmt, argptr);
	va_end (argptr);

	QC_DebugOutput (msg);
	Con_PrintfCommon (msg, false);

	// take a copy for the menus
	if (key_dest == key_menu) Menu_PutConsolePrintInbuffer (msg);
}


/*
================
Con_DPrintf

A Con_Printf that only shows up if the "developer" cvar is set
================
*/
void Con_DPrintf (char *fmt, ...)
{
	va_list		argptr;
	static char		msg[MAXPRINTMSG];

	if (!developer.value)
		return;			// don't confuse non-developers with techie stuff...

	va_start (argptr, fmt);
	_vsnprintf (msg, MAXPRINTMSG, fmt, argptr);
	va_end (argptr);

	Con_Printf ("%s", msg);
}


/*
==================
Con_SafePrintf

Okay to call even when the screen can't be updated
==================
*/
void Con_SafePrintf (char *fmt, ...)
{
	va_list		argptr;
	char		msg[1024];
	int			temp;

	va_start (argptr, fmt);
	_vsnprintf (msg, 1024, fmt, argptr);
	va_end (argptr);

	temp = scr_disabled_for_loading;
	scr_disabled_for_loading = true;
	Con_Printf ("%s", msg);
	scr_disabled_for_loading = temp;
}


/*
==============================================================================

DRAWING

==============================================================================
*/


/*
================
Con_DrawInput

The input line scrolls horizontally if typing goes beyond the right edge
================
*/
void Con_DrawInput (void)
{
	int y;
	int i;
	char editlinecopy[256];
	char *text;

	// don't draw anything
	if (key_dest != key_console && !con_forcedup) return;

	// fill out remainder with spaces (those of a nervous disposition should look away now)
	for (i = strlen ((text = Q_strncpy (editlinecopy, key_lines[edit_line], 255))); i < 256; text[i++] = ' ');

	// add the cursor frame
	if ((int) (realtime * CON_CURSORSPEED) & 1) text[key_linepos] = 11 + 130 * key_insert;

	//	prestep if horizontally scrolling
	if (key_linepos >= con_linewidth) text += 1 + key_linepos - con_linewidth;

	for
	(
		i = 0, y = con_vislines - (con_lineheight.value * 2);
		i < con_linewidth;
		Draw_Character ((i + 1) << 3, con_vislines - (con_lineheight.value * 2), text[i]), i++
	);
}


/*
================
Con_DrawNotify

Draws the last few lines of output transparently over the game top
================
*/
void Con_DrawNotify (void)
{
	int		x, v;
	char	*text;
	int		i;
	float	time;
	extern char chat_buffer[];

	// let's not scrunch everything into one corner
	v = 2;

	if (con_lineheight.value < 1) Cvar_Set (&con_lineheight, 1);
	if (con_notify_lines.integer < 1) Cvar_Set (&con_notify_lines, 1);
	if (con_notify_lines.integer >= CON_MAXNOTIFYLINES) Cvar_Set (&con_notify_lines, CON_MAXNOTIFYLINES);

	for (i = con_current - con_notify_lines.integer + 1; i <= con_current; i++)
	{
		if (i < 0) continue;

		if ((time = con_times[i % CON_MAXNOTIFYLINES]) < 0.001f) continue;
		if ((time = realtime - time) > con_notifytime.value) continue;
		if (con_notifytime.value - time < 1.0f) D3D_Set2DShade (con_notifytime.value - time);

		text = con_text + (i % con_totallines) * con_linewidth;
		clearnotify = 0;

		for (x = 0; x < con_linewidth; x++)
			Draw_Character ((x + 1) << 3, v, text[x]);

		if (con_notifytime.value - time < 1.0f) D3D_Set2DShade (1.0f);

		v += con_lineheight.value;
	}

	if (key_dest == key_message)
	{
		clearnotify = 0;
		x = 0;

		Draw_Character (8, v, 's' + 128);
		Draw_Character (16, v, 'a' + 128);
		Draw_Character (24, v, 'y' + 128);
		Draw_Character (32, v, ':' + 128);

		while (chat_buffer[x])
		{
			Draw_Character ((x + 5) << 3, v, chat_buffer[x]);
			x++;
		}

		Draw_Character ((x + 5) << 3, v, 10 + ((int) (realtime * CON_CURSORSPEED) & 1));
		v += con_lineheight.value;
	}

	if (v > con_notifylines) con_notifylines = v;
}

/*
================
Con_DrawConsole

Draws the console with the solid background
The typing input line at the bottom should only be drawn if typing is allowed
================
*/
void Con_DrawConsole (int lines, bool drawinput)
{
	int				i, x, y;
	int				rows;
	char			*text;
	int				j;
	int				sb;

	if (lines <= 0)
		return;

	// draw the background
	Draw_ConsoleBackground (lines);

	// draw the text
	con_vislines = lines;

	rows = (lines - (con_lineheight.value * 2)) / con_lineheight.value;		// rows of text to draw
	y = lines - (con_lineheight.value * 2) - (rows * con_lineheight.value);	// may start slightly negative
	sb = con_backscroll ? 2 : 0;

	for (i = con_current - rows + 1; i <= con_current - sb; i++, y += con_lineheight.value)
	{
		if ((j = i - con_backscroll) < 0) j = 0;

		text = con_text + (j % con_totallines) * con_linewidth;

		for (x = 0; x < con_linewidth; x++)
			Draw_Character ((x + 1) << 3, y, text[x]);
	}

	// draw the input prompt, user text, and cursor if desired
	if (drawinput) Con_DrawInput ();

	if (sb)
	{
		y += con_lineheight.value;

		for (x = 0; x < con_linewidth; x += 4)
			Draw_Character ((x + 1) << 3, y, '^' + 128);
	}
}


