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
// common.c -- misc functions used in client and server

#include "quakedef.h"
#include "unzip.h"


// used for generating md5 hashes
#include <wincrypt.h>

CQuakeZone *GameZone = NULL;

static bool listsortascorder = false;

int COM_ListSortFunc (const void *a, const void *b)
{
	if (listsortascorder)
	{
		char *a1 = * ((char **) a);
		char *b1 = * ((char **) b);
		return strcmp (a1, b1);
	}
	else
	{
		char *a1 = * ((char **) b);
		char *b1 = * ((char **) a);
		return strcmp (a1, b1);
	}
}

// sort a null terminated string list
void COM_SortStringList (char **stringlist, bool ascending)
{
	int listlen;

	// find the length of the list
	for (listlen = 0;; listlen++)
		if (!stringlist[listlen]) break;

	listsortascorder = ascending;
	qsort (stringlist, listlen, sizeof (char *), COM_ListSortFunc);
}


// MD5 hashes
extern "C" void MD5_Checksum (unsigned char *data, int dataLen, unsigned char *checksum);

void COM_HashData (byte *hash, const void *data, int size)
{
	MD5_Checksum ((unsigned char *) data, size, hash);
}


#define NUM_SAFE_ARGVS  7

static char     *largv[MAX_NUM_ARGVS + NUM_SAFE_ARGVS + 1];
static char     *argvdummy = " ";

static char     *safeargvs[NUM_SAFE_ARGVS] =
{"-stdvid", "-nolan", "-nosound", "-nocdaudio", "-nojoy", "-nomouse", "-dibonly"};

cvar_t  registered ("registered", "0");
cvar_t  cmdline ("cmdline", "0", CVAR_SERVER);

int             static_registered = 1;  // only for startup check, then set

bool		msg_suppress_1 = 0;

void COM_InitFilesystem (void);

// if a packfile directory differs from this, it is assumed to be hacked
#define PAK0_COUNT              339
#define PAK0_CRC                32981

char	com_token[1024];
int		com_argc;
char	**com_argv;

#define CMDLINE_LENGTH	256
char	com_cmdline[CMDLINE_LENGTH];

bool		standard_quake = true, rogue = false, hipnotic = false, quoth = false, nehahra = false, kurok = false;

/*
============================================================================

					BYTE ORDER FUNCTIONS

============================================================================
*/

bool        bigendien;

short (*BigShort) (short l);
short (*LittleShort) (short l);
int (*BigLong) (int l);
int (*LittleLong) (int l);
float (*BigFloat) (float l);
float (*LittleFloat) (float l);

short   ShortSwap (short l)
{
	byte    b1, b2;

	b1 = l & 255;
	b2 = (l >> 8) & 255;

	return (b1 << 8) + b2;
}

short   ShortNoSwap (short l)
{
	return l;
}

int    LongSwap (int l)
{
	byte    b1, b2, b3, b4;

	b1 = l & 255;
	b2 = (l >> 8) & 255;
	b3 = (l >> 16) & 255;
	b4 = (l >> 24) & 255;

	return ((int) b1 << 24) + ((int) b2 << 16) + ((int) b3 << 8) + b4;
}

int     LongNoSwap (int l)
{
	return l;
}

float FloatSwap (float f)
{
	union
	{
		float   f;
		byte    b[4];
	} dat1, dat2;


	dat1.f = f;
	dat2.b[0] = dat1.b[3];
	dat2.b[1] = dat1.b[2];
	dat2.b[2] = dat1.b[1];
	dat2.b[3] = dat1.b[0];
	return dat2.f;
}

float FloatNoSwap (float f)
{
	return f;
}

/*
================
COM_CheckParm

Returns the position (1 to argc-1) in the program's argument list
where the given parameter apears, or 0 if not present
================
*/
int COM_CheckParm (char *parm)
{
	int             i;

	for (i = 1; i < com_argc; i++)
	{
		if (!com_argv[i])
			continue;               // NEXTSTEP sometimes clears appkit vars.

		if (!strcmp (parm, com_argv[i]))
			return i;
	}

	return 0;
}

/*
================
COM_CheckRegistered

Looks for the pop.txt file and verifies it.
Sets the "registered" cvar.
Immediately exits out if an alternate game was attempted to be started without
being registered.
================
*/
void COM_CheckRegistered (void)
{
	HANDLE fh = INVALID_HANDLE_VALUE;
	unsigned short check[128];

	// allow this in shareware too
	Cvar_Set ("cmdline", com_cmdline);

	COM_FOpenFile ("gfx/pop.lmp", &fh);
	static_registered = 0;

	if (fh == INVALID_HANDLE_VALUE)
	{
		Con_SafePrintf ("Playing shareware version.\n");
		return;
	}

	int rcheck = COM_FReadFile (fh, check, sizeof (check));

	COM_FCloseFile (&fh);

	if (rcheck != sizeof (check))
	{
		Con_SafePrintf ("Corrupted pop.lmp file - reverting to shareware.");
		return;
	}

	// generate a hash of the pop.lmp data
	byte pophash[16];
	byte realpop[] = {11, 131, 239, 192, 65, 30, 123, 93, 203, 147, 122, 30, 66, 173, 55, 227};
	COM_HashData (pophash, check, sizeof (check));

	if (!COM_CheckHash (pophash, realpop))
	{
		Con_SafePrintf ("Corrupted pop.lmp file - reverting to shareware.");
		return;
	}

	Cvar_Set ("registered", "1");
	static_registered = 1;
	Con_SafePrintf ("Playing registered version.\n");
}


void COM_Path_f (void);


/*
================
COM_InitArgv
================
*/
void COM_InitArgv (int argc, char **argv)
{
	bool        safe;
	int             i, j, n;

	// reconstitute the command line for the cmdline externally visible cvar
	n = 0;

	for (j = 0; (j < MAX_NUM_ARGVS) && (j < argc); j++)
	{
		i = 0;

		while ((n < (CMDLINE_LENGTH - 1)) && argv[j][i])
			com_cmdline[n++] = argv[j][i++];

		if (n < (CMDLINE_LENGTH - 1))
			com_cmdline[n++] = ' ';
		else break;
	}

	com_cmdline[n] = 0;

	safe = false;

	for (com_argc = 0; (com_argc < MAX_NUM_ARGVS) && (com_argc < argc); com_argc++)
	{
		largv[com_argc] = argv[com_argc];

		if (!strcmp ("-safe", argv[com_argc]))
			safe = true;
	}

	if (safe)
	{
		// force all the safe-mode switches. Note that we reserved extra space in
		// case we need to add these, so we don't need an overflow check
		for (i = 0; i < NUM_SAFE_ARGVS; i++)
		{
			largv[com_argc] = safeargvs[i];
			com_argc++;
		}
	}

	largv[com_argc] = argvdummy;
	com_argv = largv;
}


/*
================
COM_Init
================
*/
cmd_t COM_Path_f_Cmd ("path", COM_Path_f);

void COM_Init (char *basedir)
{
	// because we're D3D, and therefore Windows only, we don't need to concern ourselves with this crap
	bigendien = false;
	BigShort = ShortSwap;
	LittleShort = ShortNoSwap;
	BigLong = LongSwap;
	LittleLong = LongNoSwap;
	BigFloat = FloatSwap;
	LittleFloat = FloatNoSwap;

	COM_InitFilesystem ();
	COM_CheckRegistered ();
}


/*
============
va

does a varargs printf into a temp buffer, so I don't need to have
varargs versions of all text functions.

FIXME: make varargs versions of some text functions which use this more commonly
============
*/
char *va (char *format, ...)
{
	va_list argptr;
	static char **string = NULL;
	static int bufnum = 0;

	if (!string)
	{
		string = (char **) Zone_Alloc (16 * sizeof (char *));

		// we can reduce to 1024 now that we're using the safe(r) version
		for (int i = 0; i < 16; i++)
			string[i] = (char *) Zone_Alloc (1024 * sizeof (char));
	}

	bufnum++;

	if (bufnum > 15) bufnum = 0;

	// make the buffer safe
	va_start (argptr, format);
	_vsnprintf (string[bufnum], 1024, format, argptr);
	va_end (argptr);

	return string[bufnum];
}


/// just for debugging
int memsearch (byte *start, int count, int search)
{
	int             i;

	for (i = 0; i < count; i++)
		if (start[i] == search)
			return i;

	return -1;
}


char *Q_strncpy (char *dst, const char *src, int len)
{
	// version of strncpy that ensures NULL terming of the dst string
	for (int i = 0; i < len; i++)
	{
		if (!src[i])
		{
			dst[i] = 0;
			break;
		}

		// ensure NULL termination
		dst[i] = src[i];
		dst[i + 1] = 0;
	}

	return dst;
}


bool COM_FindExtension (char *filename, char *ext)
{
	int fl = strlen (filename);
	int el = strlen (ext);

	if (el >= fl) return false;

	for (int i = 0;; i++)
	{
		if (!filename[i]) break;
		if (!stricmp (&filename[i], ext)) return true;
	}

	return false;
}


// case-insensitive strstr replacement
bool COM_StringContains (char *str1, char *str2)
{
	// sanity check args
	if (!str1) return false;
	if (!str2) return false;

	// OK, perf-wise it sucks, but - hey! - it doesn't really matter for the circumstances it's used in.
	for (int i = 0;; i++)
	{
		if (!str1[i]) break;
		if (!strnicmp (&str1[i], str2, strlen (str2))) return true;
	}

	// not found
	return false;
}


char *COM_ShiftTextColor (char *str)
{
	for (int i = 0;; i++)
	{
		if (!str[i]) break;

		str[i] = (str[i] + 128) & 255;
	}

	return str;
}

