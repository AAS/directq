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
// sys_win.c -- Win32 system interface code

#include "quakedef.h"
#include "winquake.h"
#include "errno.h"
#include "resource.h"
#include "shlobj.h"

SYSTEM_INFO SysInfo;
bool bWindowActive = false;
void AllowAccessibilityShortcutKeys (bool bAllowKeys);

int Sys_LoadResourceData (int resourceid, void **resbuf)
{
	// per MSDN, UnlockResource is obsolete and does nothing any more.  There is
	// no way to free the memory used by a resource after you're finished with it.
	// If you ask me this is kinda fucked, but what do I know?  We'll just leak it.
	HRSRC hResInfo = FindResource (NULL, MAKEINTRESOURCE (resourceid), RT_RCDATA);
	HGLOBAL hResData = LoadResource (NULL, hResInfo);
	resbuf[0] = (byte *) LockResource (hResData);
	return SizeofResource (NULL, hResInfo);
}


// we need this a lot so define it just once
HRESULT hr = S_OK;

#define PAUSE_SLEEP		50				// sleep time on pause or minimization
#define NOT_FOCUS_SLEEP	20				// sleep time when not focus

bool	ActiveApp, Minimized;
bool	WinNT = false;
bool	WinDWM = false;

static HANDLE	hFile;
static HANDLE	heventParent;
static HANDLE	heventChild;

int sys_time_period = 1;

int	Sys_FileExists (char *path)
{
	FILE	*f;

	if ((f = fopen (path, "rb")))
	{
		fclose (f);
		return 1;
	}

	return 0;
}


/*
==================
Sys_mkdir

A better Sys_mkdir.

Uses the Windows API instead of direct.h for better compatibility.
Doesn't need com_gamedir included in the path to make.
Will make all elements of a deeply nested path.
==================
*/
void Sys_mkdir (char *path)
{
	char fullpath[256];

	// if a full absolute path is given we just copy it out, otherwise we build from the gamedir
	if (path[1] == ':')
		Q_strncpy (fullpath, path, 255);
	else _snprintf (fullpath, 255, "%s/%s", com_gamedir, path);

	for (int i = 0;; i++)
	{
		if (!fullpath[i]) break;

		if (fullpath[i] == '/' || fullpath[i] == '\\')
		{
			// correct seperator
			fullpath[i] = '\\';

			if (i > 3)
			{
				// make all elements of the path
				fullpath[i] = 0;
				CreateDirectory (fullpath, NULL);
				fullpath[i] = '\\';
			}
		}
	}

	// final path
	CreateDirectory (fullpath, NULL);
}


/*
================
Sys_Init
================
*/
void Sys_Init (void)
{
	TIMECAPS tc;

	if (timeGetDevCaps (&tc, sizeof (TIMECAPS)) != TIMERR_NOERROR)
	{
		MessageBox
		(
			NULL,
			"A high resolution multimedia timer was not found on your system",
			"Error",
			MB_OK | MB_ICONERROR
		);

		exit (666);
	}

	// make sure that what timeGetDevCaps reported back is actually supported!
	for (sys_time_period = tc.wPeriodMin;; sys_time_period++)
	{
		MMRESULT mmr = timeBeginPeriod (sys_time_period);

		// can't support this time period
		if (mmr == TIMERR_NOCANDO) continue;

		// supported
		if (mmr == TIMERR_NOERROR) break;

		// as soon as we hit more than 1/10 second accuracy we abort
		if (sys_time_period > 100)
		{
			MessageBox
			(
				NULL,
				"A high resolution multimedia timer was not found on your system",
				"Error",
				MB_OK | MB_ICONERROR
			);

			exit (666);
		}
	}

	// run the timer once to get a valid baseline for subsequents
	Sys_Milliseconds ();
}


void Sys_Error (char *error, ...)
{
	va_list		argptr;
	char		text[1024], text2[1024];
	char		*text3 = "Press Enter to exit\n";
	char		*text4 = "***********************************\n";
	char		*text5 = "\n";
	DWORD		dummy;
	float		starttime;
	static int	in_sys_error0 = 0;
	static int	in_sys_error1 = 0;
	static int	in_sys_error2 = 0;
	static int	in_sys_error3 = 0;

#ifdef _DEBUG
	DebugBreak ();
#endif

	if (!in_sys_error3)
	{
		in_sys_error3 = 1;
	}

	va_start (argptr, error);
	_vsnprintf (text, 1024, error, argptr);
	va_end (argptr);

	QC_DebugOutput ("Sys_Error: %s", text);

	// switch to windowed so the message box is visible, unless we already
	// tried that and failed
	if (!in_sys_error0)
	{
		in_sys_error0 = 1;
		MessageBox (NULL, text, "Quake Error",
					MB_OK | MB_SETFOREGROUND | MB_ICONSTOP);
	}
	else
	{
		MessageBox (NULL, text, "Double Quake Error",
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

	timeEndPeriod (sys_time_period);
	exit (666);
}


void Sys_Quit (int ExitCode)
{
	Host_Shutdown ();
	timeEndPeriod (sys_time_period);
	AllowAccessibilityShortcutKeys (true);
	exit (ExitCode);
}


DWORD Sys_Milliseconds (void)
{
	static bool first = true;
	static DWORD oldtime = 0, basetime = 0, old = 0;
	DWORD newtime, now;

	now = timeGetTime () + basetime;

	if (first)
	{
		first = false;
		basetime = now;
		now = 0;
	}

	if (now < old)
	{
		// wrapped
		basetime += 0xffffffff;
		now += 0xffffffff;
	}

	old = now;
	newtime = now;

	if (newtime < oldtime)
		Sys_Error ("Sys_Milliseconds: time running backwards??\n");

	oldtime = newtime;

	return newtime;
}


float Sys_GetNextTime (void)
{
	return ((float) Sys_Milliseconds () / 1000.0f);
}


float Sys_GetFirstTime (void)
{
	return ((float) Sys_Milliseconds () / 1000.0f);
}


/*
================
Sys_FloatTime

================
*/
float Sys_FloatTime (void)
{
	static DWORD starttime = Sys_Milliseconds ();
	DWORD now = Sys_Milliseconds ();

	return ((float) (now - starttime) / 1000.0f);
}


void Sys_SendKeyEvents (void)
{
	MSG		msg;

	while (PeekMessage (&msg, NULL, 0, 0, PM_NOREMOVE))
	{
		// we always update if there are any event, even if we're paused
		scr_skipupdate = 0;

		if (!GetMessage (&msg, NULL, 0, 0))
			Sys_Quit (msg.wParam);

		TranslateMessage (&msg);
		DispatchMessage (&msg);
	}
}


/*
==================
WinMain
==================
*/
int			global_nCmdShow;
char		*argv[MAX_NUM_ARGVS];
static char	*empty_string = "";

bool CheckKnownContent (char *mask);

bool ValidateQuakeDirectory (char *quakedir)
{
	// check for known files that indicate a gamedir
	if (CheckKnownContent (va ("%s/ID1/pak0.pak", quakedir))) return true;
	if (CheckKnownContent (va ("%s/ID1/config.cfg", quakedir))) return true;
	if (CheckKnownContent (va ("%s/ID1/autoexec.cfg", quakedir))) return true;
	if (CheckKnownContent (va ("%s/ID1/progs.dat", quakedir))) return true;
	if (CheckKnownContent (va ("%s/ID1/gfx.wad", quakedir))) return true;

	// some gamedirs just have maps or models, or may have weirdly named paks
	if (CheckKnownContent (va ("%s/ID1/maps/*.bsp", quakedir))) return true;
	if (CheckKnownContent (va ("%s/ID1/progs/*.mdl", quakedir))) return true;
	if (CheckKnownContent (va ("%s/ID1/*.pak", quakedir))) return true;
	if (CheckKnownContent (va ("%s/ID1/*.pk3", quakedir))) return true;
	if (CheckKnownContent (va ("%s/ID1/*.zip", quakedir))) return true;

	// some gamedirs are just used for keeping stuff separate
	if (CheckKnownContent (va ("%s/ID1/*.sav", quakedir))) return true;
	if (CheckKnownContent (va ("%s/ID1/*.dem", quakedir))) return true;

	// not quake
	return false;
}


bool RecursiveCheckFolderForQuake (char *path)
{
	WIN32_FIND_DATA FindFileData;
	HANDLE hFind = INVALID_HANDLE_VALUE;
	char qpath[MAX_PATH];

	_snprintf (qpath, MAX_PATH, "%s\\*.*", path);
	hFind = FindFirstFile (qpath, &FindFileData);

	if (hFind == INVALID_HANDLE_VALUE)
	{
		// found no files
		FindClose (hFind);
		return false;
	}

	do
	{
		// directories only
		if (!(FindFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;

		// not interested in these types
		if (FindFileData.dwFileAttributes & FILE_ATTRIBUTE_COMPRESSED) continue;
		if (FindFileData.dwFileAttributes & FILE_ATTRIBUTE_ENCRYPTED) continue;
		if (FindFileData.dwFileAttributes & FILE_ATTRIBUTE_OFFLINE) continue;
		if (FindFileData.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) continue;
		if (FindFileData.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) continue;

		// don't do these as they can cause us to be recursing up and down the same folder tree multiple times!!!
		if (!strcmp (FindFileData.cFileName, ".")) continue;
		if (!strcmp (FindFileData.cFileName, "..")) continue;

		// build the full path (we can stomp the one we came in with as it's no longer needed)
		_snprintf (qpath, MAX_PATH, "%s\\%s", path, FindFileData.cFileName);

		// see if this directory is Quake
		if (ValidateQuakeDirectory (qpath))
		{
			// set to current directory
			SetCurrentDirectory (qpath);
			FindClose (hFind);
			return true;
		}

		// check subdirs
		if (RecursiveCheckFolderForQuake (qpath)) return true;
	} while (FindNextFile (hFind, &FindFileData));

	// done (not found)
	FindClose (hFind);
	return false;
}

char MyDesktopFolder[MAX_PATH] = {0};

char *WellKnownDirectories[] =
{
	"Quake",
	"Program Files\\Quake",
	"Program Files\\Games\\Quake",
	"Program Files (x86)\\Quake",
	"Program Files (x86)\\Games\\Quake",
	"Games\\Quake",
	MyDesktopFolder,
	NULL
};

bool CheckWellKnownDirectory (char *drive, char *wellknown)
{
	char path[MAX_PATH];

	_snprintf (path, MAX_PATH, "%s%s", drive, wellknown);

	if (ValidateQuakeDirectory (path))
	{
		SetCurrentDirectory (path);
		return true;
	}

	return false;
}


typedef struct drivespec_s
{
	char letter[4];
	bool valid;
} drivespec_t;

void SetQuakeDirectory (void)
{
	char currdir[MAX_PATH];

	// some people install quake to their desktops
	hr = SHGetFolderPath (NULL, CSIDL_DESKTOP, NULL, SHGFP_TYPE_CURRENT, MyDesktopFolder);

	if (FAILED (hr))
		MyDesktopFolder[0] = 0;
	else strcat (MyDesktopFolder, "\\quake");

	// if the current directory is the Quake directory, we search no more
	GetCurrentDirectory (MAX_PATH, currdir);

	if (ValidateQuakeDirectory (currdir)) return;

	// set up all drives
	drivespec_t drives[26];

	// check each in turn and flag as valid or not
	for (char c = 'A'; c <= 'Z'; c++)
	{
		// initially invalid
		int dnum = c - 'A';
		sprintf (drives[dnum].letter, "%c:\\", c);
		drives[dnum].valid = false;

		// start at c
		if (c < 'C') continue;

		UINT DriveType = GetDriveType (drives[dnum].letter);

		// don't check these types
		if (DriveType == DRIVE_NO_ROOT_DIR) continue;
		if (DriveType == DRIVE_UNKNOWN) continue;
		if (DriveType == DRIVE_CDROM) continue;
		if (DriveType == DRIVE_RAMDISK) continue;

		// it's valid for checking now
		drives[dnum].valid = true;
	}

	// check all drives (we expect that it will be in C:\Quake 99% of the time)
	// the first pass just checks the well-known directories for speed
	// (e.g so that D:\Quake won't cause a full scan of C)
	for (int d = 0; d < 26; d++)
	{
		if (!drives[d].valid) continue;

		// check some well-known directories (where we might reasonably expect to find Quake)
		for (int i = 0;; i++)
		{
			if (!WellKnownDirectories[i]) break;
			if (CheckWellKnownDirectory (drives[d].letter, WellKnownDirectories[i])) return;
		}
	}

	// if we still haven't found it we need to do a full HD scan.
	// be nice and ask the user...
	int conf = MessageBox (NULL, "Quake not found.\nPerform full disk scan?", "Alert", MB_YESNO | MB_ICONWARNING);

	if (conf == IDYES)
	{
		// second pass does a full scan of each drive
		for (int d = 0; d < 26; d++)
		{
			// not a validated drive
			if (!drives[d].valid) continue;

			// check everything
			if (RecursiveCheckFolderForQuake (drives[d].letter)) return;
		}
	}

	// oh shit
	MessageBox (NULL, "Could not locate Quake on your PC.\n\nPerhaps you need to move DirectQ.exe into C:\\Quake?", "Error", MB_OK | MB_ICONERROR);
	Sys_Quit (666);
}


STICKYKEYS StartupStickyKeys = {sizeof (STICKYKEYS), 0};
TOGGLEKEYS StartupToggleKeys = {sizeof (TOGGLEKEYS), 0};
FILTERKEYS StartupFilterKeys = {sizeof (FILTERKEYS), 0};


void AllowAccessibilityShortcutKeys (bool bAllowKeys)
{
	if (bAllowKeys)
	{
		// Restore StickyKeys/etc to original state
		// (note that this function is called "allow", not "enable"; if they were previously
		// disabled it will put them back that way too, it doesn't force them to be enabled.)
		SystemParametersInfo (SPI_SETSTICKYKEYS, sizeof (STICKYKEYS), &StartupStickyKeys, 0);
		SystemParametersInfo (SPI_SETTOGGLEKEYS, sizeof (TOGGLEKEYS), &StartupToggleKeys, 0);
		SystemParametersInfo (SPI_SETFILTERKEYS, sizeof (FILTERKEYS), &StartupFilterKeys, 0);
	}
	else
	{
		// Disable StickyKeys/etc shortcuts but if the accessibility feature is on,
		// then leave the settings alone as its probably being usefully used
		STICKYKEYS skOff = StartupStickyKeys;

		if ((skOff.dwFlags & SKF_STICKYKEYSON) == 0)
		{
			// Disable the hotkey and the confirmation
			skOff.dwFlags &= ~SKF_HOTKEYACTIVE;
			skOff.dwFlags &= ~SKF_CONFIRMHOTKEY;

			SystemParametersInfo (SPI_SETSTICKYKEYS, sizeof (STICKYKEYS), &skOff, 0);
		}

		TOGGLEKEYS tkOff = StartupToggleKeys;

		if ((tkOff.dwFlags & TKF_TOGGLEKEYSON) == 0)
		{
			// Disable the hotkey and the confirmation
			tkOff.dwFlags &= ~TKF_HOTKEYACTIVE;
			tkOff.dwFlags &= ~TKF_CONFIRMHOTKEY;

			SystemParametersInfo (SPI_SETTOGGLEKEYS, sizeof (TOGGLEKEYS), &tkOff, 0);
		}

		FILTERKEYS fkOff = StartupFilterKeys;

		if ((fkOff.dwFlags & FKF_FILTERKEYSON) == 0)
		{
			// Disable the hotkey and the confirmation
			fkOff.dwFlags &= ~FKF_HOTKEYACTIVE;
			fkOff.dwFlags &= ~FKF_CONFIRMHOTKEY;

			SystemParametersInfo (SPI_SETFILTERKEYS, sizeof (FILTERKEYS), &fkOff, 0);
		}
	}
}


int MapKey (int key);
void ClearAllStates (void);
LONG CDAudio_MessageHandler (HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

void VID_SetOSGamma (void);
void VID_SetAppGamma (void);
void CDAudio_Pause (void);
void CDAudio_Resume (void);

void AppActivate (BOOL fActive, BOOL minimize)
{
	static bool vid_wassuspended = false;
	extern bool vid_canalttab;

	ActiveApp = fActive;
	Minimized = minimize;

	if (fActive)
	{
		bWindowActive = true;
		block_drawing = false;

		// do this first as the api calls might affect the other stuff
		if (modestate == MS_FULLDIB)
		{
			if (vid_canalttab && vid_wassuspended)
			{
				vid_wassuspended = false;

				// ensure that the window is shown at at the top of the z order
				ShowWindow (d3d_Window, SW_SHOWNORMAL);
				SetForegroundWindow (d3d_Window);
			}
		}

		ClearAllStates ();
		IN_SetMouseState (modestate == MS_FULLDIB);

		// restore everything else
		VID_SetAppGamma ();
		CDAudio_Resume ();
		AllowAccessibilityShortcutKeys (false);

		// needed to reestablish the correct viewports
		vid.recalc_refdef = 1;
	}
	else
	{
		bWindowActive = false;
		ClearAllStates ();
		IN_SetMouseState (modestate == MS_FULLDIB);
		VID_SetOSGamma ();
		CDAudio_Pause ();
		S_ClearBuffer ();
		block_drawing = true;
		AllowAccessibilityShortcutKeys (true);

		if (modestate == MS_FULLDIB)
		{
			if (vid_canalttab)
			{
				vid_wassuspended = true;
			}
		}
	}
}


void D3DVid_ResizeWindow (HWND hWnd);

/* main window procedure */
LRESULT CALLBACK MainWndProc (HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	int fActive, fMinimized, temp;

	switch (Msg)
	{
	case WM_GETMINMAXINFO:
		{
			RECT windowrect;
			RECT clientrect;
			MINMAXINFO *mmi = (MINMAXINFO *) lParam;

			GetWindowRect (hWnd, &windowrect);
			GetClientRect (hWnd, &clientrect);

			mmi->ptMinTrackSize.x = 320 + ((windowrect.right - windowrect.left) - (clientrect.right - clientrect.left));
			mmi->ptMinTrackSize.y = 240 + ((windowrect.bottom - windowrect.top) - (clientrect.bottom - clientrect.top));
		}

		return 0;

	case WM_SIZE:
		D3DVid_ResizeWindow (hWnd);
		return 0;

		// events we want to discard
	case WM_CREATE: return 0;
	case WM_ERASEBKGND: return 1; // treachery!!! see your MSDN!
	case WM_SYSCHAR: return 0;

	case WM_INPUT:
		IN_ReadRawInput ((HRAWINPUT) lParam);
		return 0;

	case WM_SYSCOMMAND:
		switch (wParam & ~0x0F)
		{
		case SC_SCREENSAVE:
		case SC_MONITORPOWER:
			// prevent from happening
			return 0;

		default:
			return DefWindowProc (hWnd, Msg, wParam, lParam);
		}

	case WM_KILLFOCUS:
		if (modestate == MS_FULLDIB)
			ShowWindow (d3d_Window, SW_SHOWMINNOACTIVE);

		return 0;

	case WM_MOVE:
		// update cursor clip region
		IN_UpdateClipCursor ();
		return 0;

	case WM_CLOSE:
		if (MessageBox (d3d_Window, "Are you sure you want to quit?", "Confirm Exit", MB_YESNO | MB_SETFOREGROUND | MB_ICONQUESTION) == IDYES)
			Sys_Quit (0);

		return 0;

	case WM_ACTIVATE:
		fActive = LOWORD (wParam);
		fMinimized = (BOOL) HIWORD (wParam);
		AppActivate (!(fActive == WA_INACTIVE), fMinimized);

		// fix the leftover Alt from any Alt-Tab or the like that switched us away
		ClearAllStates ();
		IN_UpdateClipCursor ();

		return 0;

	case WM_DESTROY:
		if (d3d_Window)
			DestroyWindow (d3d_Window);

		PostQuitMessage (0);
		return 0;

	case MM_MCINOTIFY:
		return CDAudio_MessageHandler (hWnd, Msg, wParam, lParam);

	default:
		break;
	}

	// pass all unhandled messages to DefWindowProc
	return DefWindowProc (hWnd, Msg, wParam, lParam);
}


void Host_Frame (DWORD time);
void VID_DefaultMonitorGamma_f (void);
void D3DVid_DetectVSync (HWND hWnd);

void GetCrashReason (LPEXCEPTION_POINTERS ep);

// fixme - run shutdown through here (or else consolidate the restoration stuff in a separate function)
LONG WINAPI TildeDirectQ (LPEXCEPTION_POINTERS toast)
{
	// restore monitor gamma
	VID_DefaultMonitorGamma_f ();

	// restore default timer
	timeEndPeriod (sys_time_period);

	// get and display what caused the crash (debug builds only) or display a generic error (release builds)
	GetCrashReason (toast);

	// down she goes
	return EXCEPTION_EXECUTE_HANDLER;
}


int WINAPI WinMain (HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	InitCommonControls ();
	SetUnhandledExceptionFilter (TildeDirectQ);

	// in case we ever need it for anything...
	GetSystemInfo (&SysInfo);

	// set up and register the window class (d3d doesn't need CS_OWNDC)
	// do this before anything else so that we'll have it available for the splash too
	WNDCLASS wc;

	// here we use DefWindowProc because we're going to change it a few times and we don't want spurious messages
	wc.style = CS_CLASSDC | CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = (WNDPROC) DefWindowProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = hInstance;
	wc.hIcon = 0;
	wc.hCursor = LoadCursor (NULL, IDC_ARROW);
	wc.hbrBackground = NULL;
	wc.lpszMenuName = 0;
	wc.lpszClassName = D3D_WINDOW_CLASS_NAME;

	if (!RegisterClass (&wc))
	{
		Sys_Error ("D3D_CreateWindowClass: Failed to register Window Class");
		return 666;
	}

	// create our default window that we're going to use with everything
	d3d_Window = CreateWindowEx
	(
		0,
		D3D_WINDOW_CLASS_NAME,
		va ("DirectQ Release %s", DIRECTQ_VERSION),
		0,
		CW_USEDEFAULT, CW_USEDEFAULT,
		CW_USEDEFAULT, CW_USEDEFAULT,
		NULL,
		NULL,
		hInstance,
		NULL
	);

	if (!d3d_Window)
	{
		Sys_Error ("Couldn't create window");
		return 666;
	}

	OSVERSIONINFO vinfo;

	vinfo.dwOSVersionInfoSize = sizeof (vinfo);

	if (!GetVersionEx (&vinfo))
	{
		// if we couldn't get it we pop the warning but still let it run
		vinfo.dwMajorVersion = 0;
		vinfo.dwPlatformId = 0;
	}

	// we officially support v5 and above of Windows
	if (vinfo.dwMajorVersion < 5)
	{
		int mret = MessageBox
		(
			NULL,
			"!!! UNSUPPORTED !!!\n\nThis software may run on your Operating System\nbut is NOT officially supported.\n\nCertain pre-requisites are needed.\nNow might be a good time to read the readme.\n\nClick OK if you are sure you want to continue...",
			"Warning",
			MB_OKCANCEL | MB_ICONWARNING
		);

		if (mret == IDCANCEL) return 666;
	}

	if (vinfo.dwPlatformId == VER_PLATFORM_WIN32_NT)
		WinNT = true;
	else WinNT = false;

	// see if we're on vista or higher
	if (vinfo.dwMajorVersion >= 6)
		WinDWM = true;
	else WinDWM = false;

	quakeparms_t parms;
	char cwd[MAX_PATH];

	// attempt to get the path that the executable is in
	// if it doesn't work we just don't do it
	if (GetModuleFileName (NULL, cwd, 1023))
	{
		for (int i = strlen (cwd); i; i--)
		{
			if (cwd[i] == '/' || cwd[i] == '\\')
			{
				cwd[i] = 0;
				break;
			}
		}

		// attempt to set that path as the current working directory
		SetCurrentDirectory (cwd);
	}

	// Declare this process to be high DPI aware, and prevent automatic scaling
	HINSTANCE hUser32 = LoadLibrary ("user32.dll");

	if (hUser32)
	{
		typedef BOOL (WINAPI * LPSetProcessDPIAware) (void);
		LPSetProcessDPIAware pSetProcessDPIAware = (LPSetProcessDPIAware) GetProcAddress (hUser32, "SetProcessDPIAware");

		if (pSetProcessDPIAware) pSetProcessDPIAware ();

		UNLOAD_LIBRARY (hUser32);
	}

	// init memory pools
	// these need to be up as early as possible as other things in the startup use them
	Pool_Init ();

	global_nCmdShow = nCmdShow;

	// set the directory containing Quake
	SetQuakeDirectory ();

	if (!GetCurrentDirectory (sizeof (cwd), cwd))
		Sys_Error ("Couldn't determine current directory");

	// remove any trailing slash
	if (cwd[strlen (cwd) - 1] == '/' || cwd[strlen (cwd) - 1] == '\\')
		cwd[strlen (cwd) - 1] = 0;

	parms.basedir = cwd;
	parms.cachedir = NULL;

	parms.argc = 1;
	argv[0] = empty_string;

	// parse the command-line into args
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

	// Save the current sticky/toggle/filter key settings so they can be restored them later
	SystemParametersInfo (SPI_GETSTICKYKEYS, sizeof (STICKYKEYS), &StartupStickyKeys, 0);
	SystemParametersInfo (SPI_GETTOGGLEKEYS, sizeof (TOGGLEKEYS), &StartupToggleKeys, 0);
	SystemParametersInfo (SPI_GETFILTERKEYS, sizeof (FILTERKEYS), &StartupFilterKeys, 0);

	// Disable when full screen
	AllowAccessibilityShortcutKeys (false);

	Sys_Init ();
	D3DVid_DetectVSync (d3d_Window);
	Host_Init (&parms);

	DWORD oldtime = Sys_Milliseconds ();
	MSG msg;

	// prime the message
	PeekMessage (&msg, NULL, 0, 0, PM_NOREMOVE);

	// restructured main loop inspired by http://www.mvps.org/directx/articles/writing_the_game_loop.htm
	while (msg.message != WM_QUIT)
	{
		DWORD newtime = Sys_Milliseconds ();

		if (PeekMessage (&msg, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage (&msg);
			DispatchMessage (&msg);
		}
		else
		{
			// note - a normal frame needs to be run even if paused otherwise we'll never be able to unpause!!!
			if (cl.paused)
				Sleep (PAUSE_SLEEP);
			else if (!ActiveApp || Minimized || block_drawing)
				Sleep (NOT_FOCUS_SLEEP);

			// run a normal frame
			Host_Frame (newtime - oldtime);
			oldtime = newtime;
		}
	}

	// run through correct shutdown
	Sys_Quit (msg.wParam);
	return 0;
}

