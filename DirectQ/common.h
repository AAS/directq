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
// comndef.h  -- general definitions

#if !defined BYTE_DEFINED
typedef unsigned char 		byte;
#define BYTE_DEFINED 1
#endif

char *Q_strncpy (char *dst, const char *src, int len);


//============================================================================

typedef struct sizebuf_s
{
	bool	allowoverflow;	// if false, do a Sys_Error
	bool	overflowed;		// set to true if the buffer size failed
	byte	*data;
	int		maxsize;
	int		cursize;
} sizebuf_t;

void SZ_Alloc (sizebuf_t *buf, int startsize);
void SZ_Clear (sizebuf_t *buf);
void *SZ_GetSpace (sizebuf_t *buf, int length);
void SZ_Write (sizebuf_t *buf, void *data, int length);
void SZ_Print (sizebuf_t *buf, char *data);	// strcats onto the sizebuf

//============================================================================

typedef struct link_s
{
	struct link_s	*prev, *next;
} link_t;


void ClearLink (link_t *l);
void RemoveLink (link_t *l);
void InsertLinkBefore (link_t *l, link_t *before);
void InsertLinkAfter (link_t *l, link_t *after);

// (type *)STRUCT_FROM_LINK(link_t *link, type, member)
// ent = STRUCT_FROM_LINK(link,entity_t,order)
// FIXME: remove this mess!
#define	STRUCT_FROM_LINK(l,t,m) ((t *)((byte *)l - (int)&(((t *)0)->m)))

//============================================================================

#ifndef NULL
#define NULL ((void *)0)
#endif

#define Q_MAXCHAR ((char)0x7f)
#define Q_MAXSHORT ((short)0x7fff)
#define Q_MAXINT	((int)0x7fffffff)
#define Q_MAXLONG ((int)0x7fffffff)
#define Q_MAXFLOAT ((int)0x7fffffff)

#define Q_MINCHAR ((char)0x80)
#define Q_MINSHORT ((short)0x8000)
#define Q_MININT 	((int)0x80000000)
#define Q_MINLONG ((int)0x80000000)
#define Q_MINFLOAT ((int)0x7fffffff)

//============================================================================

extern	bool		bigendien;

extern	short	(*BigShort) (short l);
extern	short	(*LittleShort) (short l);
extern	int	(*BigLong) (int l);
extern	int	(*LittleLong) (int l);
extern	float	(*BigFloat) (float l);
extern	float	(*LittleFloat) (float l);

//============================================================================

void MSG_WriteChar (sizebuf_t *sb, int c);
void MSG_WriteByte (sizebuf_t *sb, int c);
void MSG_WriteShort (sizebuf_t *sb, int c);
void MSG_WriteLong (sizebuf_t *sb, int c);
void MSG_WriteFloat (sizebuf_t *sb, float f);
void MSG_WriteString (sizebuf_t *sb, char *s);
void MSG_WriteCoord (sizebuf_t *sb, float f);
void MSG_WriteAngle (sizebuf_t *sb, float f, bool fitzhack);

// these are to prevent crossing client/server boundaries when checking the
// protocol to decide which data format to use.  in theory the two numbers are
// the same so it should make no odds, but it just feels cleaner this way.
// in an ideal world each of client and server would have their own logically
// separate functions for reading and writing.
void MSG_WriteClientAngle (sizebuf_t *sb, float f, bool fitzhack);
float MSG_ReadServerAngle (bool fitzhack);

extern	int			msg_readcount;
extern	bool	msg_badread;		// set if a read goes beyond end of message

void MSG_BeginReading (void);
int MSG_ReadChar (void);
int MSG_ReadByte (void);
int MSG_ReadShort (void);
int MSG_ReadLong (void);
float MSG_ReadFloat (void);
char *MSG_ReadString (void);

float MSG_ReadCoord (void);
float MSG_ReadAngle (bool fitzhack);

//============================================================================

extern	char		com_token[1024];
extern	bool	com_eof;

#define COM_PARSE_TOKEN		false
#define COM_PARSE_LINE		true

char *COM_Parse (char *data, bool parsefullline = false);


extern	int		com_argc;
extern	char	**com_argv;

int COM_CheckParm (char *parm);
void COM_Init (char *path);
void COM_InitArgv (int argc, char **argv);

char *COM_SkipPath (char *pathname);
void COM_StripExtension (char *in, char *out);
void COM_FileBase (char *in, char *out);
void COM_DefaultExtension (char *path, char *extension);

char	*va(char *format, ...);
// does a varargs printf into a temp buffer


//============================================================================

extern int com_filesize;
struct cache_user_s;

// common.h doesn't know what MAX_PATH is
extern	char	com_gamedir[260];
extern char	com_gamename[260];

// common.h doesn't know what a HANDLE is...
int COM_FOpenFile (char *filename, void *hf);
int COM_FReadFile (void *fh, void *buf, int len);
int COM_FReadChar (void *fh);
int COM_FWriteFile (void *fh, void *buf, int len);
void COM_FCloseFile (void *fh);

byte *COM_LoadFile (char *path, class CQuakeHunk *spacebuf);
byte *COM_LoadFile (char *path, class CQuakeZone *heapbuf);
byte *COM_LoadFile (char *path);

void COM_ExecQuakeRC (void);

extern bool		standard_quake, rogue, hipnotic, nehahra;
bool IsTimeout (DWORD *PrevTime, DWORD WaitTime);


void COM_HashData (byte *hash, const void *data, int size);
#define COM_CheckHash(h1, h2) !(memcmp ((h1), (h2), 16))

void COM_SortStringList (char **stringlist, bool ascending);

#define COM_MAXGAMES 256

extern int com_numgames;
extern char *com_games[];

#define NO_PAK_CONTENT	1
#define NO_FS_CONTENT	2
#define PREPEND_PATH	4
#define NO_SORT_RESULT	8

// finding content
int COM_BuildContentList (char ***FileList, char *basedir, char *filetype, int flags = 0);


