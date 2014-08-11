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
// sys_win.c -- Win32 system interface code

#include "quakedef.h"
#include "winquake.h"
#include "errno.h"
#include "resource.h"


#define MINIMUM_WIN_MEMORY		0x1000000
#define MAXIMUM_WIN_MEMORY		0x4000000

#define PAUSE_SLEEP		50				// sleep time on pause or minimization
#define NOT_FOCUS_SLEEP	20				// sleep time when not focus

int			starttime;
bool	ActiveApp, Minimized;
bool	WinNT;

static double		pfreq;
static double		curtime = 0.0;
static double		lastcurtime = 0.0;
static int			lowshift;
static bool		sc_return_on_enter = false;
HANDLE				hinput, houtput;

static char			*tracking_tag = "Clams & Mooses";

static HANDLE	tevent;
static HANDLE	hFile;
static HANDLE	heventParent;
static HANDLE	heventChild;

void MaskExceptions (void);
void Sys_InitFloatTime (void);
void Sys_PushFPCW_SetHigh (void);
void Sys_PopFPCW (void);

volatile int					sys_checksum;

CRITICAL_SECTION TheCS;

/*
================
Sys_PageIn
================
*/
void Sys_PageIn (void *ptr, int size)
{
	byte	*x;
	int		m, n;

// touch all the memory to make sure it's there. The 16-page skip is to
// keep Win 95 from thinking we're trying to page ourselves in (we are
// doing that, of course, but there's no reason we shouldn't)
	x = (byte *)ptr;

	for (n=0 ; n<4 ; n++)
	{
		for (m=0 ; m<(size - 16 * 0x1000) ; m += 4)
		{
			sys_checksum += *(int *)&x[m];
			sys_checksum += *(int *)&x[m + 16 * 0x1000];
		}
	}
}


/*
===============================================================================

FILE IO

===============================================================================
*/

typedef struct sys_handle_s
{
	int num;
	FILE *file;
	struct sys_handle_s *next;
} sys_handle_t;

sys_handle_t *active_handles = NULL;
int num_handles = 1;

sys_handle_t *findhandle (int hnum)
{
	// search for a match
	for (sys_handle_t *h = active_handles; h; h = h->next)
	{
		if (h->num == hnum)
		{
			// got it
			return h;
		}
	}

	// no match
	Sys_Error ("findhandle: could not find handle for num %i\n", hnum);
	return NULL;
}


sys_handle_t *findhandle (void)
{
	int i;
	sys_handle_t *h;

	// search for an unused one
	for (h = active_handles; h; h = h->next)
	{
		if (!h->file)
		{
			// unused
			Con_DPrintf ("Used handle %i\n", h->num);
			return h;
		}
	}

	// alloc a new handle
	h = (sys_handle_t *) Heap_QMalloc (sizeof (sys_handle_t));

	if (!h)
	{
		Sys_Error ("findhandle: failed to alloc a file handle");
		return NULL;
	}

	// link it in
	h->next = active_handles;
	active_handles = h;

	Con_DPrintf ("Allocated %i handles\n", num_handles);

	// fill in props
	h->file = NULL;
	h->num = num_handles++;

	// return the handle we got
	return h;
}


/*
================
filelength
================
*/
int filelength (FILE *f)
{
	int		pos;
	int		end;

	pos = ftell (f);
	fseek (f, 0, SEEK_END);
	end = ftell (f);
	fseek (f, pos, SEEK_SET);

	return end;
}

int Sys_FileOpenRead (char *path, int *hndl)
{
	int		retval;
	sys_handle_t *h;

	h = findhandle ();

	h->file = fopen (path, "rb");

	if (!h->file)
	{
		*hndl = -1;
		retval = -1;
	}
	else
	{
		*hndl = h->num;
		retval = filelength (h->file);
	}

	return retval;
}

int Sys_FileOpenWrite (char *path)
{
	sys_handle_t *h;

	h = findhandle ();

	h->file = fopen (path, "wb");

	if (!h->file)
		Sys_Error ("Error opening %s: %s", path, strerror(errno));

	return h->num;
}

void Sys_FileClose (int handle)
{
	sys_handle_t *h = findhandle (handle);

	// prevent double-close
	if (h->file)
	{
		// null it as well so that it becomes valid for a future findhandle ()
		fclose (h->file);
		h->file = NULL;
	}
}

void Sys_FileSeek (int handle, int position)
{
	sys_handle_t *h = findhandle (handle);

	if (!h->file)
	{
		Sys_Error ("Sys_FileSeek: file not open");
		return;
	}

	fseek (h->file, position, SEEK_SET);
}

int Sys_FileRead (int handle, void *dest, int count)
{
	int x;
	sys_handle_t *h = findhandle (handle);

	if (!h->file)
	{
		Sys_Error ("Sys_FileRead: file not open");
		return -1;
	}

	x = fread (dest, 1, count, h->file);
	return x;
}

int Sys_FileWrite (int handle, void *data, int count)
{
	int x;
	sys_handle_t *h = findhandle (handle);

	if (!h->file)
	{
		Sys_Error ("Sys_FileWrite: file not open");
		return -1;
	}

	x = fwrite (data, 1, count, h->file);
	return x;
}


int	Sys_FileTime (char *path)
{
	FILE	*f;
	int		retval;

	f = fopen (path, "rb");

	if (f)
	{
		fclose (f);
		retval = 1;
	}
	else
	{
		retval = -1;
	}

	return retval;
}

void Sys_mkdir (char *path)
{
	char fullpath[256];

	// silly me - this doesn't need quotes around a directory name with spaces
	sprintf (fullpath, "%s/%s", com_gamedir, path);

	for (int i = 0; ; i++)
	{
		if (!fullpath[i]) break;

		if (fullpath[i] == '/') fullpath[i] = '\\';
	}

	CreateDirectory (fullpath, NULL);
}


/*
===============================================================================

SYSTEM IO

===============================================================================
*/

/*
================
Sys_MakeCodeWriteable
================
*/
void Sys_MakeCodeWriteable (unsigned long startaddr, unsigned long length)
{
	DWORD  flOldProtect;

	if (!VirtualProtect((LPVOID)startaddr, length, PAGE_READWRITE, &flOldProtect))
   		Sys_Error("Protection change failed\n");
}


void Sys_SetFPCW (void)
{
}

void Sys_PushFPCW_SetHigh (void)
{
}

void Sys_PopFPCW (void)
{
}

void MaskExceptions (void)
{
}


/*
================
Sys_Init
================
*/
void Sys_Init (void)
{
	LARGE_INTEGER	PerformanceFreq;
	unsigned int	lowpart, highpart;
	OSVERSIONINFO	vinfo;

	MaskExceptions ();
	Sys_SetFPCW ();

	if (!QueryPerformanceFrequency (&PerformanceFreq))
		Sys_Error ("No hardware timer available");

// get 32 out of the 64 time bits such that we have around
// 1 microsecond resolution
	lowpart = (unsigned int)PerformanceFreq.LowPart;
	highpart = (unsigned int)PerformanceFreq.HighPart;
	lowshift = 0;

	while (highpart || (lowpart > 2000000.0))
	{
		lowshift++;
		lowpart >>= 1;
		lowpart |= (highpart & 1) << 31;
		highpart >>= 1;
	}

	pfreq = 1.0 / (double)lowpart;

	Sys_InitFloatTime ();

	vinfo.dwOSVersionInfoSize = sizeof(vinfo);

	if (!GetVersionEx (&vinfo))
		Sys_Error ("Couldn't get OS info");

	if ((vinfo.dwMajorVersion < 4) ||
		(vinfo.dwPlatformId == VER_PLATFORM_WIN32s))
	{
		Sys_Error ("WinQuake requires at least Win95 or NT 4.0");
	}

	if (vinfo.dwPlatformId == VER_PLATFORM_WIN32_NT)
		WinNT = true;
	else
		WinNT = false;
}


void Sys_Error (char *error, ...)
{
	va_list		argptr;
	char		text[1024], text2[1024];
	char		*text3 = "Press Enter to exit\n";
	char		*text4 = "***********************************\n";
	char		*text5 = "\n";
	DWORD		dummy;
	double		starttime;
	static int	in_sys_error0 = 0;
	static int	in_sys_error1 = 0;
	static int	in_sys_error2 = 0;
	static int	in_sys_error3 = 0;

	if (!in_sys_error3)
	{
		in_sys_error3 = 1;
	}

	va_start (argptr, error);
	vsprintf (text, error, argptr);
	va_end (argptr);

	// switch to windowed so the message box is visible, unless we already
	// tried that and failed
	if (!in_sys_error0)
	{
		in_sys_error0 = 1;
		IN_DeactivateMouse ();
		MessageBox(NULL, text, "Quake Error",
					MB_OK | MB_SETFOREGROUND | MB_ICONSTOP);
	}
	else
	{
		MessageBox(NULL, text, "Double Quake Error",
					MB_OK | MB_SETFOREGROUND | MB_ICONSTOP);
	}

	if (!in_sys_error1)
	{
		in_sys_error1 = 1;
		Host_Shutdown ();
	}

	// shut down QHOST hooks if necessary
	if (!in_sys_error2)
	{
		in_sys_error2 = 1;
	}

	exit (1);
}


void Sys_Quit (void)
{
	Host_Shutdown ();

	if (tevent) CloseHandle (tevent);

	exit (0);
}


/*
================
Sys_FloatTime
================
*/
double Sys_FloatTime (void)
{
	static int			sametimecount;
	static unsigned int	oldtime;
	static int			first = 1;
	LARGE_INTEGER		PerformanceCount;
	unsigned int		temp, t2;
	double				time;

	Sys_PushFPCW_SetHigh ();

	QueryPerformanceCounter (&PerformanceCount);

	temp = ((unsigned int)PerformanceCount.LowPart >> lowshift) |
		   ((unsigned int)PerformanceCount.HighPart << (32 - lowshift));

	if (first)
	{
		oldtime = temp;
		first = 0;
	}
	else
	{
	// check for turnover or backward time
		if ((temp <= oldtime) && ((oldtime - temp) < 0x10000000))
		{
			oldtime = temp;	// so we can't get stuck
		}
		else
		{
			t2 = temp - oldtime;

			time = (double)t2 * pfreq;
			oldtime = temp;

			curtime += time;

			if (curtime == lastcurtime)
			{
				sametimecount++;

				if (sametimecount > 100000)
				{
					curtime += 1.0;
					sametimecount = 0;
				}
			}
			else
			{
				sametimecount = 0;
			}

			lastcurtime = curtime;
		}
	}

	Sys_PopFPCW ();

    return curtime;
}


/*
================
Sys_InitFloatTime
================
*/
void Sys_InitFloatTime (void)
{
	int		j;

	Sys_FloatTime ();

	j = COM_CheckParm("-starttime");

	if (j)
	{
		curtime = (double) (Q_atof(com_argv[j+1]));
	}
	else
	{
		curtime = 0.0;
	}

	lastcurtime = curtime;
}


char *Sys_ConsoleInput (void)
{
	return NULL;
}

void Sys_Sleep (void)
{
	Sleep (1);
}


void Sys_SendKeyEvents (void)
{
    MSG        msg;

	while (PeekMessage (&msg, NULL, 0, 0, PM_NOREMOVE))
	{
	// we always update if there are any event, even if we're paused
		scr_skipupdate = 0;

		if (!GetMessage (&msg, NULL, 0, 0))
			Sys_Quit ();

      	TranslateMessage (&msg);
      	DispatchMessage (&msg);
	}
}


void Sys_ThreadCatchup (void)
{
	// DON'T PANIC!!!
	// per MSDN - "Specifies the time, in milliseconds, for which to suspend execution.
	// A value of zero causes the thread to relinquish the remainder of its time slice
	// to any other thread of equal priority that is ready to run. If there are no other
	// threads of equal priority ready to run, the function returns immediately, and
	// the thread continues execution."
	// DON'T PANIC!!!
	// DirectInput needs this, and anyway it makes the engine a more well-behaved citizen
	// in a multi-threaded environment.  Got a few extra FPS from it too... (confirming my
	// suspicion that something in Direct3D was running multi-threaded...
	// DON'T PANIC!!!
	Sleep (0);
	// DON'T PANIC!!!
}


/*
==============================================================================

 WINDOWS CRAP

==============================================================================
*/


/*
==================
WinMain
==================
*/
void SleepUntilInput (int time)
{
	MsgWaitForMultipleObjects (1, &tevent, FALSE, time, QS_ALLINPUT);
}


/*
==================
WinMain
==================
*/
HINSTANCE	global_hInstance;
int			global_nCmdShow;
char		*argv[MAX_NUM_ARGVS];
static char	*empty_string = "";

void Splash_Init (HINSTANCE hInstance);

int WINAPI WinMain (HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	quakeparms_t	parms;
	double			time, oldtime, newtime;
	static	char	cwd[1024];
	int				t;

    // previous instances do not exist in Win32
	// this is stupid as is can't be anothing but NULL...
    if (hPrevInstance) return 0;

	SetProcessAffinityMask (GetCurrentProcess (), 1);

	// mark the entire program as a critical section so that the 
	// multithreaded CRT and multithreading in DirectX don't trip us up.
	InitializeCriticalSection (&TheCS);
	EnterCriticalSection (&TheCS);

	// init memory heap
	Heap_Init ();

	global_hInstance = hInstance;
	global_nCmdShow = nCmdShow;

	if (!GetCurrentDirectory (sizeof (cwd), cwd))
		Sys_Error ("Couldn't determine current directory");

	if (cwd[Q_strlen(cwd)-1] == '/' || cwd[Q_strlen(cwd)-1] == '\\')
		cwd[Q_strlen(cwd)-1] = 0;

	parms.basedir = cwd;
	parms.cachedir = NULL;

	parms.argc = 1;
	argv[0] = empty_string;

	while (*lpCmdLine && (parms.argc < MAX_NUM_ARGVS))
	{
		while (*lpCmdLine && ((*lpCmdLine <= 32) || (*lpCmdLine > 126)))
			lpCmdLine++;

		if (*lpCmdLine)
		{
			argv[parms.argc] = lpCmdLine;
			parms.argc++;

			while (*lpCmdLine && ((*lpCmdLine > 32) && (*lpCmdLine <= 126)))
				lpCmdLine++;

			if (*lpCmdLine)
			{
				*lpCmdLine = 0;
				lpCmdLine++;
			}
		}
	}

	parms.argv = argv;

	COM_InitArgv (parms.argc, parms.argv);

	parms.argc = com_argc;
	parms.argv = com_argv;

	if (COM_CheckParm ("-dedicated") != 0)
	{
		// dedicated server is no longer supported
		MessageBox
		(
			NULL,
			"DirectQ can no longer run as a Dedicated Server.\nWhy would you want to anyway?\n\nUse another engine!",
			"Error",
			MB_OK | MB_ICONERROR | MB_SYSTEMMODAL | MB_SETFOREGROUND | MB_TOPMOST
		);

		return -1;
	}

	// initialize the splash screen
	Splash_Init (hInstance);

	if (!(tevent = CreateEvent (NULL, FALSE, FALSE, NULL))) Sys_Error ("Couldn't create event");

	Sys_Init ();

	Host_Init (&parms);

	oldtime = Sys_FloatTime ();

    /* main window message loop */
	while (1)
	{
		// yield the CPU for a little while when paused, minimized, or not the focus
		if ((cl.paused && (!ActiveApp && !DDActive)) || Minimized || block_drawing)
		{
			SleepUntilInput (PAUSE_SLEEP);
			scr_skipupdate = 1;		// no point in bothering to draw
		}
		else if (!ActiveApp && !DDActive)
		{
			SleepUntilInput (NOT_FOCUS_SLEEP);
		}

		newtime = Sys_FloatTime ();
		time = newtime - oldtime;

		Host_Frame (time);
		oldtime = newtime;

		// allow any other threads which may have been spawned by the application to grab a timeslice
		// (DirectX seems to spawn a few undocumented threads here and there which cause problems if you don't do this)
		Sys_ThreadCatchup ();
	}

    /* return success of application */
    return TRUE;
}

