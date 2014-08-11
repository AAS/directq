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
// quakedef.h -- primary header for client

// be a little kinder to the CRT in Quake by telling it to act like it's single-threaded
// #define _CRT_DISABLE_PERFCRIT_LOCKS

// here we define a windows version to ensure that we'll always compile OK
#define WINVER 0x0500
#define _WIN32_WINNT 0x0500

#define DIRECTQ_VERSION "1.8.2"

// let's be able to do assertions everywhere
#include <assert.h>

#include <windows.h>
#include <d3d9.h>
#include <d3dx9.h>


// disable unwanted warnings
#pragma warning (disable: 4305)
#pragma warning (disable: 4244)
#pragma warning (disable: 4311)
#pragma warning (disable: 4018)
#pragma warning (disable: 4800)
#pragma warning (disable: 4267)
#pragma warning (disable: 4996)
#pragma warning (disable: 4995)
#pragma warning (disable: 4312)

typedef char quakepath[260];


// savegame version for all savegames
#define	SAVEGAME_VERSION	5

#define	QUAKE_GAME			// as opposed to utilities

#define	VERSION				1.09
#define	GLQUAKE_VERSION		1.00
#define	D3DQUAKE_VERSION	0.01
#define	WINQUAKE_VERSION	0.996
#define	LINUX_VERSION		1.30
#define	X11_VERSION			1.10

// 1/32 epsilon to keep floating point happy
#define	DIST_EPSILON	(0.03125)

// used in various places
#define DIVIDER_LINE		"\35\36\36\36\36\36\36\36\36\36\36\36\36\37"


#define	GAMENAME	"id1"

#include <math.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>

// was 1; does this actually DO anything?  Not mentioned in MSDN; must be WinQuake or DOSQuake legacy
#define UNALIGNED_OK	0

// !!! if this is changed, it must be changed in d_ifacea.h too !!!
#define CACHE_SIZE	32		// used to align key data structures

#define UNUSED(x)	(x = x)	// for pesky compiler / lint warnings

#define MAX_NUM_ARGVS	50

// up / down
#define	PITCH	0

// left / right
#define	YAW		1

// fall over
#define	ROLL	2


// max length of a quake game pathname
// this cannot be changed as struct members on disk use it
#define	MAX_QPATH		64


#define	ON_EPSILON		0.1			// point on plane side epsilon

#define	MAX_MSGLEN		65536
#define	MAX_DATAGRAM	65536
#define	MAX_DATAGRAM2	sv_max_datagram

extern int		sv_max_datagram;	// is default MAX_DATAGRAM

//
// per-level limits
//
#define	MAX_EDICTS		65536		// protocol limit
#define	MAX_LIGHTSTYLES	64

// protocol limit values - bumped from bjp
#define	MAX_MODELS		4096		// these are sent over the net as bytes
#define	MAX_SOUNDS		4096		// so they cannot be blindly increased

#define	SAVEGAME_COMMENT_LENGTH	39

#define	MAX_STYLESTRING	64

//
// stats are integers communicated to the client by the server
//
#define	MAX_CL_STATS		32
#define	STAT_HEALTH			0
#define	STAT_FRAGS			1
#define	STAT_WEAPON			2
#define	STAT_AMMO			3
#define	STAT_ARMOR			4
#define	STAT_WEAPONFRAME	5
#define	STAT_SHELLS			6
#define	STAT_NAILS			7
#define	STAT_ROCKETS		8
#define	STAT_CELLS			9
#define	STAT_ACTIVEWEAPON	10
#define	STAT_TOTALSECRETS	11
#define	STAT_TOTALMONSTERS	12
#define	STAT_SECRETS		13		// bumped on client side by svc_foundsecret
#define	STAT_MONSTERS		14		// bumped by svc_killedmonster

// stock defines

#define	IT_SHOTGUN				1
#define	IT_SUPER_SHOTGUN		2
#define	IT_NAILGUN				4
#define	IT_SUPER_NAILGUN		8
#define	IT_GRENADE_LAUNCHER		16
#define	IT_ROCKET_LAUNCHER		32
#define	IT_LIGHTNING			64
#define IT_SUPER_LIGHTNING      128
#define IT_SHELLS               256
#define IT_NAILS                512
#define IT_ROCKETS              1024
#define IT_CELLS                2048
#define IT_AXE                  4096
#define IT_ARMOR1               8192
#define IT_ARMOR2               16384
#define IT_ARMOR3               32768
#define IT_SUPERHEALTH          65536
#define IT_KEY1                 131072
#define IT_KEY2                 262144
#define	IT_INVISIBILITY			524288
#define	IT_INVULNERABILITY		1048576
#define	IT_SUIT					2097152
#define	IT_QUAD					4194304
#define IT_SIGIL1               (1<<28)
#define IT_SIGIL2               (1<<29)
#define IT_SIGIL3               (1<<30)
#define IT_SIGIL4               (1<<31)

//===========================================
//rogue changed and added defines

#define RIT_SHELLS              128
#define RIT_NAILS               256
#define RIT_ROCKETS             512
#define RIT_CELLS               1024
#define RIT_AXE                 2048
#define RIT_LAVA_NAILGUN        4096
#define RIT_LAVA_SUPER_NAILGUN  8192
#define RIT_MULTI_GRENADE       16384
#define RIT_MULTI_ROCKET        32768
#define RIT_PLASMA_GUN          65536
#define RIT_ARMOR1              8388608
#define RIT_ARMOR2              16777216
#define RIT_ARMOR3              33554432
#define RIT_LAVA_NAILS          67108864
#define RIT_PLASMA_AMMO         134217728
#define RIT_MULTI_ROCKETS       268435456
#define RIT_SHIELD              536870912
#define RIT_ANTIGRAV            1073741824
#define RIT_SUPERHEALTH         2147483648

//MED 01/04/97 added hipnotic defines
//===========================================
//hipnotic added defines
#define HIT_PROXIMITY_GUN_BIT 16
#define HIT_MJOLNIR_BIT       7
#define HIT_LASER_CANNON_BIT  23
#define HIT_PROXIMITY_GUN   (1<<HIT_PROXIMITY_GUN_BIT)
#define HIT_MJOLNIR         (1<<HIT_MJOLNIR_BIT)
#define HIT_LASER_CANNON    (1<<HIT_LASER_CANNON_BIT)
#define HIT_WETSUIT         (1<<(23+2))
#define HIT_EMPATHY_SHIELDS (1<<(23+3))

//===========================================

#define	MAX_SCOREBOARD		16
#define	MAX_SCOREBOARDNAME	32

#define	SOUND_CHANNELS		8

// This makes anyone on id's net privileged
// Use for multiplayer testing only - VERY dangerous!!!
// #define IDGODS

#include "common.h"
#include "bspfile.h"
#include "vid.h"
#include "sys.h"
#include "heap.h"
#include "mathlib.h"

typedef struct
{
	vec3_t	origin;
	vec3_t	angles;
	int		modelindex;
	int		frame;
	int		colormap;
	int		skin;
	int		effects;
	byte	alpha;
} entity_state_t;


#include "wad.h"
#include "draw.h"
#include "cvar.h"
#include "screen.h"
#include "net.h"
#include "protocol.h"
#include "cmd.h"
#include "hud.h"
#include "sound.h"
#include "render.h"
#include "client.h"
#include "progs.h"
#include "server.h"
#include "input.h"
#include "world.h"
#include "keys.h"
#include "console.h"
#include "view.h"
#include "crc.h"
#include "cdaudio.h"
#include "dshow_mp3.h"


//=============================================================================

// the host system specifies the base of the directory tree, the
// command line parms passed to the program, and the amount of memory
// available for the program to use

typedef struct
{
	char	*basedir;
	char	*cachedir;		// for development over ISDN lines
	int		argc;
	char	**argv;
} quakeparms_t;


//=============================================================================



extern bool noclip_anglehack;


//
// host
//
extern	quakeparms_t host_parms;

extern	cvar_t		sys_ticrate;
extern	cvar_t		sys_nostdout;
extern	cvar_t		developer;

extern	bool	host_initialized;		// true if into command execution
extern	float		host_frametime;
extern	DWORD		dwHostFrameTime;
extern	byte		*host_basepal;
extern	byte		*host_colormap;
extern	int			host_framecount;	// incremented every frame, never reset
extern	float		realtime;			// not bounded in any way, changed at
										// start of every frame, never reset

void Host_ClearMemory (void);
void Host_ServerFrame (void);
void Host_InitCommands (void);
void Host_Init (quakeparms_t *parms);
void Host_Shutdown(void);
void Host_Error (char *error, ...);
void Host_EndGame (char *message, ...);
void Host_Quit_f (void);
void Host_ClientCommands (char *fmt, ...);
void Host_ShutdownServer (bool crash);

extern bool		msg_suppress_1;		// suppresses resolution and cache size console output
										//  an fullscreen DIB focus gain/loss
extern int			current_skill;		// skill level for currently loaded level (in case
										//  the user changes the cvar while the level is
										//  running, this reflects the level actually in use)

// make these accessible throught the engine
extern cvar_t temp1;
extern cvar_t temp2;
extern cvar_t temp3;
extern cvar_t temp4;

extern cvar_t scratch1;
extern cvar_t scratch2;
extern cvar_t scratch3;
extern cvar_t scratch4;

extern cvar_t saved1;
extern cvar_t saved2;
extern cvar_t saved3;
extern cvar_t saved4;


// chase
extern	cvar_t	chase_active;

void Chase_Init (void);
void Chase_Reset (void);
void Chase_Update (void);

// object release for all COM objects and interfaces
#define SAFE_RELEASE(COM_Generic) {if ((COM_Generic)) {(COM_Generic)->Release (); (COM_Generic) = NULL;}}

// and for C++ new stuffies
#define SAFE_DELETE(p) {if (p) delete (p); (p) = NULL;}

// can't put this in common.h as it doens't know what a cvar_t is
// optionally creates the directory if it doesn't exist
void COM_CheckContentDirectory (cvar_t *contdir, bool createifneeded);

// likewise since i moved it to a class
extern cvar_t registered;

// line testing
float CastRay (float *p1, float *p2);

// using byte colours and having the macros wrap may have seemed like a good idea to someone somewhere sometime
// say in 1996, when every single byte or cpu cycle was precious...
#define BYTE_CLAMP(i) (int) ((((i) > 255) ? 255 : (((i) < 0) ? 0 : (i))))

// generic useful stuff
int Sys_LoadResourceData (int resourceid, void **resbuf);

