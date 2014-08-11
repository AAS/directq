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
// common.c -- misc functions used in client and server

#include "quakedef.h"
#include "unzip.h"
#include <vector>
#include <algorithm>


// used for generating md5 hashes
#include <wincrypt.h>


static bool listsortascorder = false;

static int COM_ListSortFunc (const void *a, const void *b)
{
	if (listsortascorder)
	{
		char *a1 = *((char **) a);
		char *b1 = *((char **) b);
		return strcmp (a1, b1);
	}
	else
	{
		char *a1 = *((char **) b);
		char *b1 = *((char **) a);
		return strcmp (a1, b1);
	}
}

// sort a null terminated string list
void COM_SortStringList (char **stringlist, bool ascending)
{
	int listlen;

	// find the length of the list
	for (listlen = 0; ; listlen++)
		if (!stringlist[listlen]) break;

	listsortascorder = ascending;
	qsort (stringlist, listlen, sizeof (char *), COM_ListSortFunc);
}


// MD5 hashes
void COM_HashData (byte *hash, const void *data, int size)
{
	// generate an MD5 hash of an image's data
	HCRYPTPROV hCryptProv;
	HCRYPTHASH hHash;

	// acquire the cryptographic context (can we cache this?)
	if (CryptAcquireContext (&hCryptProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT | CRYPT_MACHINE_KEYSET))
	{
		// create a hashing algorithm (can we cache this?)
		if (CryptCreateHash (hCryptProv, CALG_MD5, 0, 0, &hHash))
		{
			// hash the data
			if (CryptHashData (hHash, (const BYTE *) data, size, 0))
			{
				DWORD dwHashLen = 16;

				// retrieve the hash
				if (CryptGetHashParam (hHash, HP_HASHVAL, hash, &dwHashLen, 0)) 
				{
					// hashed OK
				}
				else
				{
					// oh crap
 					Sys_Error ("COM_HashData: CryptGetHashParam failed");
				}
			}
			else
			{
				// oh crap
				Sys_Error ("COM_HashData: CryptHashData failed");
			}

			CryptDestroyHash (hHash); 
		}
		else
		{
			// oh crap
			Sys_Error ("COM_HashData: CryptCreateHash failed");
		}

	    CryptReleaseContext (hCryptProv, 0);
	}
	else
	{
		// oh crap
		Sys_Error ("COM_HashData: CryptAcquireContext failed");
	}
}


#define NUM_SAFE_ARGVS  7

static char     *largv[MAX_NUM_ARGVS + NUM_SAFE_ARGVS + 1];
static char     *argvdummy = " ";

static char     *safeargvs[NUM_SAFE_ARGVS] =
	{"-stdvid", "-nolan", "-nosound", "-nocdaudio", "-nojoy", "-nomouse", "-dibonly"};

cvar_t  registered ("registered","0");
cvar_t  cmdline ("cmdline","0", CVAR_SERVER);

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

bool		standard_quake = true, rogue = false, hipnotic = false, quoth = false, nehahra = false;

cvar_t com_hipnotic ("com_hipnotic", 0.0f);
cvar_t com_rogue ("com_rogue", 0.0f);
cvar_t com_quoth ("com_quoth", 0.0f);
cvar_t com_nehahra ("com_nehahra", 0.0f);

bool IsTimeout (float *PrevTime, float WaitTime)
{
	float CurrTime;

	CurrTime = Sys_FloatTime ();

	if (*PrevTime && CurrTime - *PrevTime < WaitTime)
		return false;

	*PrevTime = CurrTime;

	return true;
}

/*


All of Quake's data access is through a hierchal file system, but the contents of the file system can be transparently merged from several sources.

The "base directory" is the path to the directory holding the quake.exe and all game directories.  The sys_* files pass this to host_init in quakeparms_t->basedir.  This can be overridden with the "-basedir" command line parm to allow code debugging in a different directory.  The base directory is
only used during filesystem initialization.

The "game directory" is the first tree on the search path and directory that all generated files (savegames, screenshots, demos, config files) will be saved to.  This can be overridden with the "-game" command line parameter.  The game directory can never be changed while quake is executing.  This is a precacution against having a malicious server instruct clients to write files over areas they shouldn't.


FIXME:
The file "parms.txt" will be read out of the game directory and appended to the current command line arguments to allow different games to initialize startup parms differently.  This could be used to add a "-sspeed 22050" for the high quality sound edition.  Because they are added at the end, they will not override an explicit setting on the original command line.

*/

//============================================================================


// ClearLink is used for new headnodes
void ClearLink (link_t *l)
{
	l->prev = l->next = l;
}

void RemoveLink (link_t *l)
{
	l->next->prev = l->prev;
	l->prev->next = l->next;
}

void InsertLinkBefore (link_t *l, link_t *before)
{
	l->next = before;
	l->prev = before->prev;
	l->prev->next = l;
	l->next->prev = l;
}
void InsertLinkAfter (link_t *l, link_t *after)
{
	l->next = after->next;
	l->prev = after;
	l->prev->next = l;
	l->next->prev = l;
}


/*
============================================================================

					BYTE ORDER FUNCTIONS

============================================================================
*/

bool        bigendien;

short   (*BigShort) (short l);
short   (*LittleShort) (short l);
int     (*BigLong) (int l);
int     (*LittleLong) (int l);
float   (*BigFloat) (float l);
float   (*LittleFloat) (float l);

short   ShortSwap (short l)
{
	byte    b1,b2;

	b1 = l&255;
	b2 = (l>>8)&255;

	return (b1<<8) + b2;
}

short   ShortNoSwap (short l)
{
	return l;
}

int    LongSwap (int l)
{
	byte    b1,b2,b3,b4;

	b1 = l&255;
	b2 = (l>>8)&255;
	b3 = (l>>16)&255;
	b4 = (l>>24)&255;

	return ((int)b1<<24) + ((int)b2<<16) + ((int)b3<<8) + b4;
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
==============================================================================

			MESSAGE IO FUNCTIONS

Handles byte ordering and avoids alignment errors
==============================================================================
*/

//
// writing functions
//

void MSG_WriteChar (sizebuf_t *sb, int c)
{
	byte    *buf;

	buf = (byte *) SZ_GetSpace (sb, 1);
	buf[0] = c;
}

void MSG_WriteByte (sizebuf_t *sb, int c)
{
	byte    *buf;

	buf = (byte *) SZ_GetSpace (sb, 1);
	buf[0] = c;
}

void MSG_WriteShort (sizebuf_t *sb, int c)
{
	byte    *buf;

	buf = (byte *) SZ_GetSpace (sb, 2);
	buf[0] = c&0xff;
	buf[1] = c>>8;
}

void MSG_WriteLong (sizebuf_t *sb, int c)
{
	byte    *buf;
	
	buf = (byte *) SZ_GetSpace (sb, 4);
	buf[0] = c&0xff;
	buf[1] = (c>>8)&0xff;
	buf[2] = (c>>16)&0xff;
	buf[3] = c>>24;
}

void MSG_WriteFloat (sizebuf_t *sb, float f)
{
	union
	{
		float   f;
		int     l;
	} dat;
	
	
	dat.f = f;
	dat.l = LittleLong (dat.l);
	
	SZ_Write (sb, &dat.l, 4);
}

void MSG_WriteString (sizebuf_t *sb, char *s)
{
	if (!s)
		SZ_Write (sb, "", 1);
	else
		SZ_Write (sb, s, strlen(s)+1);
}

void MSG_WriteCoord (sizebuf_t *sb, float f)
{
	MSG_WriteShort (sb, (int)(f*8));
}

void MSG_WriteAngle (sizebuf_t *sb, float f)
{
	MSG_WriteByte (sb, ((int)f*256/360) & 255);
}

//
// reading functions
//
int                     msg_readcount;
bool        msg_badread;

void MSG_BeginReading (void)
{
	msg_readcount = 0;
	msg_badread = false;
}

// returns -1 and sets msg_badread if no more characters are available
int MSG_ReadChar (void)
{
	int     c;
	
	if (msg_readcount+1 > net_message.cursize)
	{
		msg_badread = true;
		return -1;
	}
		
	c = (signed char)net_message.data[msg_readcount];
	msg_readcount++;
	
	return c;
}

int MSG_ReadByte (void)
{
	int     c;

	if (msg_readcount+1 > net_message.cursize)
	{
		msg_badread = true;
		return -1;
	}

	c = (unsigned char)net_message.data[msg_readcount];
	msg_readcount++;

	return c;
}

int MSG_ReadShort (void)
{
	int     c;
	
	if (msg_readcount+2 > net_message.cursize)
	{
		msg_badread = true;
		return -1;
	}
		
	c = (short)(net_message.data[msg_readcount]
	+ (net_message.data[msg_readcount+1]<<8));
	
	msg_readcount += 2;
	
	return c;
}

int MSG_ReadLong (void)
{
	int     c;
	
	if (msg_readcount+4 > net_message.cursize)
	{
		msg_badread = true;
		return -1;
	}
		
	c = net_message.data[msg_readcount]
	+ (net_message.data[msg_readcount+1]<<8)
	+ (net_message.data[msg_readcount+2]<<16)
	+ (net_message.data[msg_readcount+3]<<24);
	
	msg_readcount += 4;
	
	return c;
}

float MSG_ReadFloat (void)
{
	union
	{
		byte    b[4];
		float   f;
		int     l;
	} dat;
	
	dat.b[0] =      net_message.data[msg_readcount];
	dat.b[1] =      net_message.data[msg_readcount+1];
	dat.b[2] =      net_message.data[msg_readcount+2];
	dat.b[3] =      net_message.data[msg_readcount+3];
	msg_readcount += 4;
	
	dat.l = LittleLong (dat.l);

	return dat.f;   
}

char *MSG_ReadString (void)
{
	static char     string[2048];
	int             l,c;
	
	l = 0;
	do
	{
		c = MSG_ReadChar ();
		if (c == -1 || c == 0)
			break;
		string[l] = c;
		l++;
	} while (l < sizeof(string) - 1);
	
	string[l] = 0;
	
	return string;
}

float MSG_ReadCoord (void)
{
	return MSG_ReadShort() * (1.0/8);
}

float MSG_ReadAngle (void)
{
	return MSG_ReadChar() * (360.0/256);
}



//===========================================================================

void SZ_Alloc (sizebuf_t *buf, int startsize)
{
	if (startsize < 256)
		startsize = 256;

	buf->data = (byte *) Pool_Alloc (POOL_PERMANENT, startsize);
	buf->maxsize = startsize;
	buf->cursize = 0;
}


void SZ_Clear (sizebuf_t *buf)
{
	buf->cursize = 0;
}

void *SZ_GetSpace (sizebuf_t *buf, int length)
{
	void    *data;

	if (buf->cursize + length > buf->maxsize)
	{
		if (!buf->allowoverflow)
			Sys_Error ("SZ_GetSpace: overflow without allowoverflow set");
		
		if (length > buf->maxsize)
			Sys_Error ("SZ_GetSpace: %i is > full buffer size", length);
			
		buf->overflowed = true;
		Con_Printf ("SZ_GetSpace: overflow");
		SZ_Clear (buf); 
	}

	data = buf->data + buf->cursize;
	buf->cursize += length;
	
	return data;
}

void SZ_Write (sizebuf_t *buf, void *data, int length)
{
	memcpy (SZ_GetSpace(buf,length),data,length);         
}

void SZ_Print (sizebuf_t *buf, char *data)
{
	int             len;
	
	len = strlen(data)+1;

// byte * cast to keep VC++ happy
	if (buf->data[buf->cursize-1])
		memcpy ((byte *)SZ_GetSpace(buf, len),data,len); // no trailing 0
	else
		memcpy ((byte *)SZ_GetSpace(buf, len-1)-1,data,len); // write over trailing 0
}


//============================================================================


/*
============
COM_SkipPath
============
*/
char *COM_SkipPath (char *pathname)
{
	char    *last;
	
	last = pathname;
	while (*pathname)
	{
		if (*pathname=='/')
			last = pathname+1;
		pathname++;
	}
	return last;
}

/*
============
COM_StripExtension
============
*/
void COM_StripExtension (char *in, char *out)
{
	while (*in && *in != '.')
		*out++ = *in++;
	*out = 0;
}

/*
============
COM_FileExtension
============
*/
char *COM_FileExtension (char *in)
{
	static char exten[8];
	int             i;

	while (*in && *in != '.')
		in++;
	if (!*in)
		return "";
	in++;
	for (i=0 ; i<7 && *in ; i++,in++)
		exten[i] = *in;
	exten[i] = 0;
	return exten;
}

/*
============
COM_FileBase
============
*/
void COM_FileBase (char *in, char *out)
{
	char *s, *s2;

	s = in + strlen(in) - 1;

	while (s != in && *s != '.')
		s--;
	
	for (s2 = s ; *s2 && *s2 != '/' ; s2--)
	;
	
	if (s-s2 < 2)
		strcpy (out,"?model?");
	else
	{
		s--;
		strncpy (out,s2+1, s-s2);
		out[s-s2] = 0;
	}
}


/*
==================
COM_DefaultExtension
==================
*/
void COM_DefaultExtension (char *path, char *extension)
{
	char    *src;
//
// if path doesn't have a .EXT, append extension
// (extension should include the .)
//
	src = path + strlen(path) - 1;

	while (*src != '/' && src != path)
	{
		if (*src == '.')
			return;                 // it has an extension
		src--;
	}

	strcat (path, extension);
}


/*
==============
COM_Parse

Parse a token out of a string
==============
*/
char *COM_Parse (char *data)
{
#if 0
	// regular q1 version
	int             c;
	int             len;
	
	len = 0;
	com_token[0] = 0;
	
	if (!data)
		return NULL;
		
// skip whitespace
skipwhite:
	while ( (c = *data) <= ' ')
	{
		if (c == 0)
			return NULL;                    // end of file;
		data++;
	}
	
// skip // comments
	if (c=='/' && data[1] == '/')
	{
		while (*data && *data != '\n')
			data++;
		goto skipwhite;
	}
	

// handle quoted strings specially
	if (c == '\"')
	{
		data++;
		while (1)
		{
			c = *data++;
			if (c=='\"' || !c)
			{
				com_token[len] = 0;
				return data;
			}
			com_token[len] = c;
			len++;
		}
	}

// parse single characters
	if (c=='{' || c=='}'|| c==')'|| c=='(' || c=='\'' || c==':')
	{
		com_token[len] = c;
		len++;
		com_token[len] = 0;
		return data+1;
	}

// parse a regular word
	do
	{
		com_token[len] = c;
		data++;
		len++;
		c = *data;
	if (c=='{' || c=='}'|| c==')'|| c=='(' || c=='\'' || c==':')
			break;
	} while (c>32);
	
	com_token[len] = 0;
	return data;
#else
	// qw version
	int c;
	int len;

	len = 0;
	com_token[0] = 0;

	if (!data) return NULL;

	// skip whitespace
skipwhite:
	while ((c = *data) <= ' ')
	{
		// eof
		if (c == 0) return NULL;
		data++;
	}

	// skip // comments
	if (c=='/' && data[1] == '/')
	{
		while (*data && *data != '\n')
			data++;
		goto skipwhite;
	}

	// handle quoted strings specially
	if (c == '\"')
	{
		data++;

		while (1)
		{
			c = *data++;

			if (c == '\"' || !c)
			{
				com_token[len] = 0;
				return data;
			}

			com_token[len] = c;
			len++;
		}
	}

	// parse a regular word
	do
	{
		com_token[len] = c;
		data++;
		len++;
		c = *data;
	} while (c > 32);

	com_token[len] = 0;

	return data;
#endif
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
	
	for (i=1 ; i<com_argc ; i++)
	{
		if (!com_argv[i])
			continue;               // NEXTSTEP sometimes clears appkit vars.
		if (!strcmp (parm,com_argv[i]))
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
	int i;

	// allow this in shareware too
	Cvar_Set ("cmdline", com_cmdline);

	COM_FOpenFile ("gfx/pop.lmp", &fh);
	static_registered = 0;

	if (fh == INVALID_HANDLE_VALUE)
	{
		Con_SafePrintf ("Playing shareware version.\n");
		return;
	}

	DWORD rcheck;

	ReadFile (fh, check, sizeof (check), &rcheck, NULL);
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
	byte    swaptest[2] = {1,0};

// set the byte swapping variables in a portable manner 
	if ( *(short *)swaptest == 1)
	{
		bigendien = false;
		BigShort = ShortSwap;
		LittleShort = ShortNoSwap;
		BigLong = LongSwap;
		LittleLong = LongNoSwap;
		BigFloat = FloatSwap;
		LittleFloat = FloatNoSwap;
	}
	else
	{
		bigendien = true;
		BigShort = ShortNoSwap;
		LittleShort = ShortSwap;
		BigLong = LongNoSwap;
		LittleLong = LongSwap;
		BigFloat = FloatNoSwap;
		LittleFloat = FloatSwap;
	}

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
		string = (char **) Pool_Alloc (POOL_PERMANENT, 16 * sizeof (char *));

		// we can reduce to 1024 now that we're using the safe(r) version
		for (int i = 0; i < 16; i++)
			string[i] = (char *) Pool_Alloc (POOL_PERMANENT, 1024 * sizeof (char));
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
int     memsearch (byte *start, int count, int search)
{
	int             i;
	
	for (i=0 ; i<count ; i++)
		if (start[i] == search)
			return i;
	return -1;
}

/*
=============================================================================

QUAKE FILESYSTEM

=============================================================================
*/

int     com_filesize;


//
// in memory
//

typedef struct
{
	char    name[MAX_QPATH];
	int     filepos, filelen;
} packfile_t;

typedef struct pack_s
{
	char    filename[MAX_OSPATH];
	int             handle;
	int             numfiles;
	packfile_t      *files;
} pack_t;


typedef struct pk3_s
{
	char			filename[MAX_OSPATH];
	int             numfiles;
	packfile_t      *files;
} pk3_t;


//
// on disk
//
typedef struct
{
	char    name[56];
	int             filepos, filelen;
} dpackfile_t;

typedef struct
{
	char    id[4];
	int             dirofs;
	int             dirlen;
} dpackheader_t;

#define MAX_FILES_IN_PACK       2048

char    com_gamedir[MAX_OSPATH];

typedef struct searchpath_s
{
	char    filename[MAX_OSPATH];
	pack_t  *pack;          // only one of filename / pack will be used
	pk3_t *pk3;
	struct searchpath_s *next;
} searchpath_t;

searchpath_t    *com_searchpaths = NULL;

/*
============
COM_Path_f

============
*/
void COM_Path_f (void)
{
	searchpath_t    *s;

	Con_Printf ("Current search path:\n");

	for (s = com_searchpaths; s; s = s->next)
	{
		if (s->pack)
		{
			Con_Printf ("%s (%i files)\n", s->pack->filename, s->pack->numfiles);
		}
		else Con_Printf ("%s\n", s->filename);
	}
}


/*
============
COM_CreatePath

Only used for CopyFile
============
*/
void    COM_CreatePath (char *path)
{
	char    *ofs;
	
	for (ofs = path+1 ; *ofs ; ofs++)
	{
		if (*ofs == '/')
		{       // create the directory
			*ofs = 0;
			Sys_mkdir (path);
			*ofs = '/';
		}
	}
}


bool SortCompare (char *left, char *right)
{
	if (stricmp (left, right) < 0)
		return true;
	else return false;
}


bool CheckExists (std::vector<char *> &FileList, char *mapname)
{
	int ListLen = FileList.size ();

	// look for a match
	for (int i = 0; i < ListLen; i++)
		if (!stricmp (FileList[i], mapname))
			return true;

	// no match found
	return false;
}


void COM_BuildContentList (std::vector<char *> &FileList, char *basedir, char *filetype)
{
	/*
	initial clear down breaks skybox menu loading
	if (FileList.size () > 0)
	{
		// clear down previous
		int size = FileList.size ();

		for (int i = 0; i < size; i++)
			Zone_Free (FileList[i]);

		FileList.clear ();
	}
	*/

	int dirlen = strlen (basedir);
	int typelen = strlen (filetype);

	for (searchpath_t *search = com_searchpaths; search; search = search->next)
	{
		if (search->pack)
		{
			pack_t *pak = search->pack;

			for (int i = 0; i < pak->numfiles; i++)
			{
				int filelen = strlen (pak->files[i].name);

				if (filelen < typelen + dirlen) continue;
				if (strnicmp (pak->files[i].name, basedir, dirlen)) continue;
				if (stricmp (&pak->files[i].name[filelen - typelen], filetype)) continue;
				if (CheckExists (FileList, &pak->files[i].name[dirlen])) continue;

				char *filename = (char *) Zone_Alloc (strlen (&pak->files[i].name[dirlen]) + 1);
				strcpy (filename, &pak->files[i].name[dirlen]);
				FileList.push_back (filename);
			}
		}
		else if (search->pk3)
		{
			pk3_t *pak = search->pk3;

			for (int i = 0; i < pak->numfiles; i++)
			{
				int filelen = strlen (pak->files[i].name);

				if (filelen < typelen + dirlen) continue;
				if (strnicmp (pak->files[i].name, basedir, dirlen)) continue;
				if (stricmp (&pak->files[i].name[filelen - typelen], filetype)) continue;
				if (CheckExists (FileList, &pak->files[i].name[dirlen])) continue;

				char *filename = (char *) Zone_Alloc (strlen (&pak->files[i].name[dirlen]) + 1);
				strcpy (filename, &pak->files[i].name[dirlen]);
				FileList.push_back (filename);
			}
		}
		else
		{
			WIN32_FIND_DATA FindFileData;
			HANDLE hFind = INVALID_HANDLE_VALUE;
			char find_filter[MAX_PATH];

			_snprintf (find_filter, 260, "%s/%s*%s", search->filename, basedir, filetype);

			for (int i = 0; ; i++)
			{
				if (find_filter[i] == 0) break;
				if (find_filter[i] == '/') find_filter[i] = '\\';
			}

			hFind = FindFirstFile (find_filter, &FindFileData);

			if (hFind == INVALID_HANDLE_VALUE)
			{
				// found no files
				FindClose (hFind);
				continue;
			}

			do
			{
				// not interested
				if (FindFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
				if (FindFileData.dwFileAttributes & FILE_ATTRIBUTE_COMPRESSED) continue;
				if (FindFileData.dwFileAttributes & FILE_ATTRIBUTE_ENCRYPTED) continue;
				if (FindFileData.dwFileAttributes & FILE_ATTRIBUTE_OFFLINE) continue;
				if (FindFileData.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) continue;
				if (CheckExists (FileList, FindFileData.cFileName)) continue;

				char *filename = (char *) Zone_Alloc (strlen (FindFileData.cFileName) + 1);
				strcpy (filename, FindFileData.cFileName);
				FileList.push_back (filename);
			} while (FindNextFile (hFind, &FindFileData));

			// done
			FindClose (hFind);
		}
	}

	// sort the list
	std::sort (FileList.begin (), FileList.end (), SortCompare);
}


HANDLE COM_MakeTempFile (char *tmpfile)
{
	char fpath1[MAX_PATH];
	char fpath2[MAX_PATH];

	if (!GetTempPath (MAX_PATH, fpath1))
	{
		// oh crap
		return NULL;
	}

	// ensure it exists
	CreateDirectory (fpath1, NULL);

	// for convenience we keep all temp files under a "DirectQ" subdir
	_snprintf (fpath2, MAX_PATH, "%s\\DirectQ", fpath1);
	CreateDirectory (fpath2, NULL);

	// copy out the full new path to the directory and replace any '/' with '\\'
	_snprintf (fpath1, MAX_PATH, "%s\\%s", fpath2, tmpfile);

	for (int i = 0; ; i++)
	{
		if (fpath1[i] == 0) break;
		if (fpath1[i] == '/') fpath1[i] = '\\';
	}

	// remove the file name
	for (int i = strlen (fpath1) - 1; i; i--)
	{
		if (fpath1[i] == '\\')
		{
			fpath1[i] = 0;
			break;
		}
	}

	// create the full path
	for (int i = strlen (fpath2) + 1; ; i++)
	{
		if (fpath1[i] == 0) break;

		if (fpath1[i] == '\\')
		{
			fpath1[i] = 0;
			CreateDirectory (fpath1, NULL);
			fpath1[i] = '\\';
		}
	}

	// the above doesn't create the last item in the path so do it here
	CreateDirectory (fpath1, NULL);

	// because we modified the full filename buffer above we need to reconstruct it before going any further
	_snprintf (fpath1, MAX_PATH, "%s\\%s", fpath2, tmpfile);

	// create the file - see http://blogs.msdn.com/larryosterman/archive/2004/04/19/116084.aspx for
	// further info on the flags chosen here.  while researching this I came across this beautiful quote:
	// "in linux i can simply mount \\straw\gambler to /home/gambler after a reinstall and all my files 
	// and settings are there".  this is an affliction that seems to plague Linux bois everywhere - a 
	// tendency to only focus on what *they* want, to the exclusion of the rest of the world.  in a proper
	// modern and rational OS your aunt Mabel and a Business Studies student with no IT aptitude should be
	// able to do these things too... so do grow up.
	HANDLE hf = CreateFile
	(
		fpath1,
		GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ,
		NULL,
		CREATE_ALWAYS,
		FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
		NULL
	);

	if (hf == INVALID_HANDLE_VALUE)
	{
		//printf ("hf == INVALID_HANDLE_VALUE\n");
		return NULL;
	}

	return hf;
}


HANDLE COM_UnzipPK3FileToTemp (pk3_t *pk3, char *filename)
{
	// initial scan ensures the file is present before opening the zip (perf)
	for (int i = 0; i < pk3->numfiles; i++)
		if (!stricmp (pk3->files[i].name, filename))
			goto unzOK;

	// not present
	return INVALID_HANDLE_VALUE;

unzOK:;
	unzFile			uf = NULL;
	int				err;
	unz_global_info gi;
	unz_file_info	file_info;

	uf = unzOpen (pk3->filename);
	err = unzGetGlobalInfo (uf, &gi);

	if (err == UNZ_OK)
	{
		char filename_inzip[64];

		unzGoToFirstFile (uf);

		for (int i = 0; i < gi.number_entry; i++)
		{
			err = unzOpenCurrentFile (uf);

			if (err != UNZ_OK) goto unzError;

			err = unzGetCurrentFileInfo (uf, &file_info, filename_inzip, sizeof (filename_inzip), NULL, 0, NULL, 0);

			if (err == UNZ_OK)
			{
				if (!stricmp (filename_inzip, filename))
				{
					// got it, so unzip it to the temp folder
					byte unztemp[1024];
					DWORD byteswritten;

					HANDLE pk3handle = COM_MakeTempFile (filename);

					for (;;)
					{
						int bytesread = unzReadCurrentFile (uf, unztemp, 1024);

						if (bytesread < 0)
						{
							unzCloseCurrentFile (uf);
							COM_FCloseFile (&pk3handle);
							goto unzError;
						}

						if (bytesread == 0) break;

						WriteFile (pk3handle, unztemp, bytesread, &byteswritten, NULL);
					}

					unzCloseCurrentFile (uf);
					unzClose (uf);
					return pk3handle;
				}
			}

			unzGoToNextFile (uf);
		}
	}

unzError:;
	// didn't find it
	unzClose (uf);
	return INVALID_HANDLE_VALUE;
}


int COM_FWriteFile (void *fh, void *buf, int len)
{
	DWORD byteswritten;

	BOOL ret = WriteFile ((HANDLE) fh, buf, len, &byteswritten, NULL);

	if (ret && byteswritten == len)
		return 1;
	else return 0;
}


int COM_FReadFile (void *fh, void *buf, int len)
{
	DWORD bytesread;

	BOOL ret = ReadFile ((HANDLE) fh, buf, len, &bytesread, NULL);

	if (ret)
		return (int) bytesread;
	else return -1;
}


int COM_FReadChar (void *fh)
{
	char rc;
	DWORD bytesread;

	BOOL ret = ReadFile ((HANDLE) fh, &rc, 1, &bytesread, NULL);

	if (ret && bytesread == 1)
		return (int) (byte) rc;
	else return -1;
}


int COM_FOpenFile (char *filename, void *hf)
{
	// don't error out here
	if (!hf)
	{
		Con_Printf ("COM_FOpenFile: hFile not set");
		com_filesize = -1;
		return -1;
	}

	// silliness here and above is needed because common.h doesn't know what a HANDLE is
	HANDLE *hFile = (HANDLE *) hf;

	// ensure for the close test below
	*hFile = INVALID_HANDLE_VALUE;

	// search pak files second
	for (searchpath_t *search = com_searchpaths; search; search = search->next)
	{
		// ensure...
		if (*hFile != INVALID_HANDLE_VALUE) COM_FCloseFile (hFile);
		*hFile = INVALID_HANDLE_VALUE;
		com_filesize = -1;

		if (search->pack)
		{
			// look through all the pak file elements
			pack_t *pak = search->pack;

			for (int i = 0; i < pak->numfiles; i++)
			{
				if (!stricmp (pak->files[i].name, filename))
				{
					// note - we need to share read access because e.g. a demo could result in 2 simultaneous
					// reads, one for the .dem file and one for a .bsp file
					*hFile = CreateFile
					(
						pak->filename,
						GENERIC_READ,
						FILE_SHARE_READ,
						NULL,
						OPEN_EXISTING,
						FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_NO_RECALL | FILE_FLAG_SEQUENTIAL_SCAN,
						NULL
					);

					// this can happen if a PAK file was enumerated on startup but deleted while running
					if (*hFile == INVALID_HANDLE_VALUE)
					{
						DWORD dwerr = GetLastError ();
						continue;
					}

					SetFilePointer (*hFile, pak->files[i].filepos, NULL, FILE_BEGIN);

					com_filesize = pak->files[i].filelen;
					return com_filesize;
				}
			}
		}
		else if (search->pk3)
		{
			HANDLE pk3handle = COM_UnzipPK3FileToTemp (search->pk3, filename);

			if (pk3handle != INVALID_HANDLE_VALUE)
			{
				*hFile = pk3handle;
				SetFilePointer (*hFile, 0, NULL, FILE_BEGIN);
				com_filesize = GetFileSize (*hFile, NULL);
				return com_filesize;
			}
		}
		else
		{
			char netpath[MAX_OSPATH];

			// check for a file in the directory tree
			_snprintf (netpath, 128, "%s/%s", search->filename, filename);

			// note - we need to share read access because e.g. a demo could result in 2 simultaneous
			// reads, one for the .dem file and one for a .bsp file
			*hFile = CreateFile
			(
				netpath,
				GENERIC_READ,
				FILE_SHARE_READ,
				NULL,
				OPEN_EXISTING,
				FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_NO_RECALL | FILE_FLAG_SEQUENTIAL_SCAN,
				NULL
			);

			if (*hFile == INVALID_HANDLE_VALUE) continue;

			com_filesize = GetFileSize (*hFile, NULL);
			return com_filesize;
		}
	}

	// not found
	*hFile = INVALID_HANDLE_VALUE;
	com_filesize = -1;
	return -1;
}


void COM_FCloseFile (void *fh)
{
	CloseHandle (*((HANDLE *) fh));
	*((HANDLE *) fh) = INVALID_HANDLE_VALUE;
}


/*
============
COM_LoadFile

Filename are reletive to the quake directory.
Always appends a 0 byte.
============
*/
static byte *COM_LoadFile (char *path, int usehunk)
{
	HANDLE	fh = INVALID_HANDLE_VALUE;
	byte    *buf = NULL;

	// look for it in the filesystem or pack files
	int len = COM_FOpenFile (path, &fh);

	if (fh == INVALID_HANDLE_VALUE) return NULL;

	if (usehunk == 1)
		buf = (byte *) Pool_Alloc (POOL_GAME, len + 1);
	else if (usehunk == 2)
		buf = (byte *) Pool_Alloc (POOL_LOADFILE, len + 1);
	else
	{
		Con_DPrintf ("COM_LoadFile: bad usehunk\n");
		COM_FCloseFile (&fh);
		return NULL;
	}

	if (!buf)
	{
		Con_DPrintf ("COM_LoadFile: not enough space for %s", path);
		COM_FCloseFile (&fh);
		return NULL;
	}

	((byte *) buf)[len] = 0;
	DWORD rlen = 0;

	ReadFile (fh, buf, len, &rlen, NULL);
	COM_FCloseFile (&fh);

	return buf;
}


byte *COM_LoadHunkFile (char *path)
{
	return COM_LoadFile (path, 1);
}

byte *COM_LoadTempFile (char *path)
{
	return COM_LoadFile (path, 2);
}


/*
=================
COM_LoadPackFile

Takes an explicit (not game tree related) path to a pak file.

Loads the header and directory, adding the files at the beginning
of the list so they override previous pack files.
=================
*/
pack_t *COM_LoadPackFile (char *packfile)
{
	dpackheader_t header;
	int i;
	packfile_t *newfiles;
	int numpackfiles;
	pack_t *pack;
	FILE *packfp;
	dpackfile_t *info;
	unsigned short crc;

	// read and validate the header
	if (!(packfp = fopen (packfile, "rb"))) return NULL;
	fread (&header, sizeof (header), 1, packfp);

	if (header.id[0] != 'P' || header.id[1] != 'A' || header.id[2] != 'C' || header.id[3] != 'K')
	{
		Con_Printf ("%s is not a packfile", packfile);
		fclose (packfp);
		return NULL;
	}

	header.dirofs = LittleLong (header.dirofs);
	header.dirlen = LittleLong (header.dirlen);

	numpackfiles = header.dirlen / sizeof (dpackfile_t);

	newfiles = (packfile_t *) Pool_Alloc (POOL_GAME, numpackfiles * sizeof (packfile_t));
	info = (dpackfile_t *) Pool_Alloc (POOL_TEMP, numpackfiles * sizeof (dpackfile_t));

	fseek (packfp, header.dirofs, SEEK_SET);
	fread (info, header.dirlen, 1, packfp);
	fclose (packfp);

	// parse the directory
	for (i = 0; i < numpackfiles; i++)
	{
		strncpy (newfiles[i].name, info[i].name, 63);
		newfiles[i].filepos = LittleLong (info[i].filepos);
		newfiles[i].filelen = LittleLong (info[i].filelen);
	}

	pack = (pack_t *) Pool_Alloc (POOL_GAME, sizeof (pack_t));
	strncpy (pack->filename, packfile, 127);
	pack->numfiles = numpackfiles;
	pack->files = newfiles;

	Con_Printf ("Added packfile %s (%i files)\n", packfile, numpackfiles);
	return pack;
}


bool COM_FindExtension (char *filename, char *ext)
{
	int fl = strlen (filename);
	int el = strlen (ext);

	if (el >= fl) return false;

	for (int i = 0; ; i++)
	{
		if (!filename[i]) break;
		if (!stricmp (&filename[i], ext)) return true;
	}

	return false;
}


/*
================
COM_AddGameDirectory

Sets com_gamedir, adds the directory to the head of the path,
then loads and adds pak1.pak pak2.pak ... 
================
*/
void COM_AddGameDirectory (char *dir)
{
	searchpath_t *search;
	char pakfile[MAX_OSPATH];

	// copy to com_gamedir so that the last gamedir added will be the one used
	strncpy (com_gamedir, dir, 127);

	// add any pak files in the format pak0.pak pak1.pak, ...
	WIN32_FIND_DATA FindFileData;
	HANDLE hFind = INVALID_HANDLE_VALUE;

	// look for a file (take all files so that we can also load PK3s)
	_snprintf (pakfile, 128, "%s/*.*", dir);
	hFind = FindFirstFile (pakfile, &FindFileData);

	if (hFind == INVALID_HANDLE_VALUE)
	{
		// found no files
		FindClose (hFind);
	}
	else
	{
		// add all the pak files
		do
		{
			if (COM_FindExtension (FindFileData.cFileName, ".pak"))
			{
				// load the pak file
				_snprintf (pakfile, 128, "%s/%s", dir, FindFileData.cFileName);
				pack_t *pak = COM_LoadPackFile (pakfile);

				if (pak)
				{
					// link it in
					search = (searchpath_t *) Pool_Alloc (POOL_GAME, sizeof (searchpath_t));
					search->pack = pak;
					search->pk3 = NULL;
					search->next = com_searchpaths;
					com_searchpaths = search;
				}
			}
			else if (COM_FindExtension (FindFileData.cFileName, ".zip") || COM_FindExtension (FindFileData.cFileName, ".pk3"))
			{
				unzFile			uf;
				int				err;
				unz_global_info gi;
				unz_file_info	file_info;

				// load the pak file
				_snprintf (pakfile, 128, "%s/%s", dir, FindFileData.cFileName);
				uf = unzOpen (pakfile);
				err = unzGetGlobalInfo (uf, &gi);

				if (err == UNZ_OK)
				{
					pk3_t *pk3 = (pk3_t *) Pool_Alloc (POOL_GAME, sizeof (pk3_t));
					char filename_inzip[64];
					int good_files = 0;

					pk3->numfiles = gi.number_entry;
					strncpy (pk3->filename, pakfile, 127);
					pk3->files = (packfile_t *) Pool_Alloc (POOL_GAME, sizeof (packfile_t) * pk3->numfiles);

					unzGoToFirstFile (uf);

					for (int i = 0; i < gi.number_entry; i++)
					{
						err = unzGetCurrentFileInfo (uf, &file_info, filename_inzip, sizeof (filename_inzip), NULL, 0, NULL, 0);

						if (err == UNZ_OK)
						{
							// length and pos are unused
							strncpy (pk3->files[i].name, filename_inzip, 63);
							pk3->files[i].filelen = 0;
							pk3->files[i].filepos = 0;

							// flag a good file here
							good_files++;
						}
						else
						{
							// bad entry
							pk3->files[i].name[0] = 0;
							pk3->files[i].filelen = 0;
							pk3->files[i].filepos = 0;
						}

						unzGoToNextFile (uf);
					}

					if (good_files)
					{
						// link it in
						search = (searchpath_t *) Pool_Alloc (POOL_GAME, sizeof (searchpath_t));
						search->pack = NULL;
						search->pk3 = pk3;
						search->next = com_searchpaths;
						com_searchpaths = search;
						Con_Printf ("Added packfile %s (%i files)\n", pk3->filename, pk3->numfiles);
					}
				}

				unzClose (uf);
			}
		} while (FindNextFile (hFind, &FindFileData));

		// close the finder
		FindClose (hFind);
	}

	// add the directory to the search path
	// this is done last as using a linked list will search in the reverse order to which they
	// are added, so we ensure that the filesystem overrides pak files
	search = (searchpath_t *) Pool_Alloc (POOL_GAME, sizeof(searchpath_t));
	strncpy (search->filename, dir, 127);
	search->next = com_searchpaths;
	search->pack = NULL;
	search->pk3 = NULL;
	com_searchpaths = search;
}


void COM_CheckContentDirectory (cvar_t *contdir, bool createifneeded)
{
	// now that we give the user control over the names of some of these
	// we'll need to perform some validation.  note: this function happens at
	// runtime, and possibly when the UI isn't even available yet, so we
	// can't display any errors and let the user correct them.  Also, it seems
	// bad form to crash the game, so instead we silently correct.

	// create the directory if needed
	if (createifneeded) Sys_mkdir (contdir->string);
}


// stuff we need to drop and reload
void D3D_ReleaseTextures (void);
void Host_WriteConfiguration (void);
void Draw_Init (void);
void HUD_Init (void);
void SCR_Init (void);
void R_InitResourceTextures (void);
void D3D_InitUnderwaterTexture (void);
void Draw_InvalidateMapshot (void);
void Menu_SaveLoadInvalidate (void);
void S_StopAllSounds (bool clear);
void Mod_ClearAll (void);
void S_ClearSounds (void);
void Menu_MapsPopulate (void);
void Menu_DemoPopulate (void);
void Menu_LoadAvailableSkyboxes (void);
void D3D_SetDefaultStates (void);
void SHOWLMP_newgame (void);

bool COM_StringContains (char *str1, char *str2)
{
	// sanity check args
	if (!str1) return false;
	if (!str2) return false;

	// OK, perf-wise it sucks, but - hey! - it doesn't really matter for the circumstances it's used in.
	for (int i = 0; ; i++)
	{
		if (!str1[i]) break;
		if (!strnicmp (&str1[i], str2, strlen (str2))) return true;
	}

	// not found
	return false;
}


void COM_UnloadAllStuff (void)
{
	extern bool scr_initialized;

	// start with a clean filesystem
	com_searchpaths = NULL;

	// drop everything we need to drop
	Pool_Free (POOL_GAME);
	Pool_Free (POOL_CACHE);
	SHOWLMP_newgame ();
	CL_Disconnect_f ();
	D3D_ReleaseTextures ();

	// prevent screen updates while changing
	scr_disabled_for_loading = true;
	scr_initialized = false;

	// clear everything else
	S_StopAllSounds (true);
	Mod_ClearAll ();
	S_ClearSounds ();

	// do this too...
	Host_ClearMemory ();
}


void COM_LoadAllStuff (void)
{
	host_basepal = (byte *) COM_LoadHunkFile ("gfx/palette.lmp");
	host_colormap = (byte *) COM_LoadHunkFile ("gfx/colormap.lmp");
	W_LoadWadFile ("gfx.wad");
	Draw_Init ();
	HUD_Init ();
	SCR_Init ();
	R_InitResourceTextures ();
	D3D_InitUnderwaterTexture ();
	Draw_InvalidateMapshot ();
	Menu_SaveLoadInvalidate ();
	Menu_MapsPopulate ();
	Menu_DemoPopulate ();
	Menu_LoadAvailableSkyboxes ();
}


void COM_LoadGame (char *gamename)
{
	SCR_UpdateScreen ();

	if (host_initialized)
	{
		// store out our configuration before we go to the new game
		Host_WriteConfiguration ();

		// unload everything
		COM_UnloadAllStuff ();
	}

	char basedir[MAX_OSPATH];

	// -basedir <path>
	// Overrides the system supplied base directory (under GAMENAME)
	// this is always active for the entire session
	int i = COM_CheckParm ("-basedir");

	if (i && i < com_argc-1)
		strncpy (basedir, com_argv[i + 1], 127);
	else strncpy (basedir, host_parms.basedir, 127);

	int j = strlen (basedir);

	if (j > 0)
	{
		// remove terminating slash
		if ((basedir[j - 1] == '\\') || (basedir[j - 1] == '/'))
			basedir[j - 1] = 0;
	}

	// allow -game rogue/hipnotic/quoth as well as -rogue/hipnotic/quoth here
	if (COM_StringContains (gamename, "rogue")) Cvar_Set (&com_rogue, 1);
	if (COM_StringContains (gamename, "hipnotic")) Cvar_Set (&com_hipnotic, 1);
	if (COM_StringContains (gamename, "quoth")) Cvar_Set (&com_quoth, 1);
	if (COM_StringContains (gamename, "nehahra")) Cvar_Set (&com_nehahra, 1);

	// check status of add-ons; nothing yet...
	rogue = hipnotic = quoth = nehahra = false;
	standard_quake = true;

	if (com_rogue.value)
	{
		rogue = true;
		standard_quake = false;
	}

	if (com_hipnotic.value)
	{
		hipnotic = true;
		standard_quake = false;
	}

	if (com_quoth.value)
	{
		// quoth needs this for it's hud layout.
		// hipnotic = true;
		quoth = true;
		standard_quake = false;
	}

	if (com_nehahra.value)
	{
		nehahra = true;
		standard_quake = false;
	}

	// now add the base directory (ID1) (lowest priority)
	COM_AddGameDirectory (va ("%s/%s", basedir, GAMENAME));

	// add these in the same order as ID do (mission packs always get second-lowest priority)
	if (rogue) COM_AddGameDirectory (va ("%s/rogue", basedir));
	if (hipnotic) COM_AddGameDirectory (va ("%s/hipnotic", basedir));
	if (quoth) COM_AddGameDirectory (va ("%s/quoth", basedir));
	if (nehahra) COM_AddGameDirectory (va ("%s/nehahra", basedir));

	// add any other games in the list (everything else gets highest priority)
	char *thisgame = gamename;
	char *nextgame = gamename;

	for (;;)
	{
		// no more games
		if (!thisgame) break;
		if (!thisgame[0]) break;

		// find start pointer to next game
		for (int i = 0; ; i++)
		{
			if (thisgame[i] == 0)
			{
				// end of list
				nextgame = &thisgame[i];
				break;
			}

			if (thisgame[i] == '\n')
			{
				// character after delimiter
				nextgame = &thisgame[i + 1];
				thisgame[i] = 0;
				break;
			}
		}

		// if false the game has already been loaded and so we don't load it again
		bool loadgame = true;

		// check for games already loaded
		if (!stricmp (thisgame, "rogue")) loadgame = false;
		if (!stricmp (thisgame, "hipnotic")) loadgame = false;
		if (!stricmp (thisgame, "quoth")) loadgame = false;
		if (!stricmp (thisgame, "nehahra")) loadgame = false;
		if (!stricmp (thisgame, GAMENAME)) loadgame = false;

		// only load it if it hasn't already been loaded
		if (loadgame)
		{
			// do something interesting with thisgame
			Con_Printf ("Loading Game: \"%s\"...\n", thisgame);
			COM_AddGameDirectory (va ("%s/%s", basedir, thisgame));
		}

		// go to next game
		thisgame = nextgame;
	}

	// if the host isn't already up, don't bring anything up yet
	if (!host_initialized) return;

	// reload everything that needs to be reloaded
	COM_LoadAllStuff ();

	Con_Printf ("\n");

	// reload the configs as they may have changed
	Cbuf_InsertText ("exec quake.rc\n");
	Cbuf_Execute ();

	// not disabled any more
	scr_disabled_for_loading = false;

	// force a stop of the demo loop in case we change while the game is running
	cls.demonum = -1;
}


void COM_Game_f (void)
{
	if (Cmd_Argc () < 2)
	{
		// this can come in from either a "game" or a "gamedir" command, so notify the user of the command they actually issued
		Con_Printf ("%s <gamename> <gamename> <gamename>...\nchanges the currently loaded game\n", Cmd_Argv (0));
		return;
	}

	// alloc space to copy out the game dirs
	// can't send cmd_args in direct as we will be modifying it...
	// matches space allocated for cmd_argv in cmd.cpp
	// can't alloc this dynamically as it takes down the engine (ouch!) - 80K ain't too bad anyway
	static char gamedirs[81920];
	gamedirs[0] = 0;

	// copy out delimiting with \n so that we can parse the string
	for (int i = 1; i < Cmd_Argc (); i++)
	{
		// we made sure that we had enough space above so we don't need to check for overflow here.
		strcat (gamedirs, Cmd_Argv (i));

		// don't forget the delimiter!
		strcat (gamedirs, "\n");
	}

	// load using the generated gamedirs string
	COM_LoadGame (gamedirs);
}


// qrack uses gamedir
cmd_t COM_Game_Cmd ("game", COM_Game_f);
cmd_t COM_GameDir_Cmd ("gamedir", COM_Game_f);

void COM_InitFilesystem (void)
{
	// check for expansion packs
	// (these are only checked at startup as the player might want to switch them off during gameplay; otherwise
	// they would be enforced on always)
	if (COM_CheckParm ("-rogue")) Cvar_Set (&com_rogue, 1);
	if (COM_CheckParm ("-hipnotic")) Cvar_Set (&com_hipnotic, 1);
	if (COM_CheckParm ("-quoth")) Cvar_Set (&com_quoth, 1);
	if (COM_CheckParm ("-nehahra")) Cvar_Set (&com_nehahra, 1);

	// -game <gamedir>
	// adds gamedir as an override game
	int i = COM_CheckParm ("-game");

	// load the specified game
	if (i && i < com_argc - 1)
		COM_LoadGame (com_argv[i + 1]);
	else COM_LoadGame (NULL);
}

