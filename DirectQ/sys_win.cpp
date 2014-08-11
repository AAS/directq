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
#include "shlobj.h"

HHOOK hKeyboardHook = NULL;
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
bool	WinNT;

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

	timeEndPeriod (sys_time_period);
	exit (1);
}


void Sys_Quit (void)
{
	Host_Shutdown ();
	timeEndPeriod (sys_time_period);
	UnhookWindowsHookEx (hKeyboardHook);
	AllowAccessibilityShortcutKeys (true);
	exit (0);
}


/*
================
Sys_FloatTime

unfortunately this code is STILL used throughout the engine
================
*/
float Sys_FloatTime (void)
{
	// adjust for rounding errors
	return ((float) Sys_DWORDTime () + 0.5f) / 1000.0f;
}


DWORD Sys_DWORDTime (void)
{
	static LONGLONG starttime = timeGetTime ();

	LONGLONG now = timeGetTime ();

	while (now < starttime)
	{
		now += 0xffffffff;
		now += 1;
	}

	return (DWORD) (now - starttime);
}


void Sys_SendKeyEvents (void)
{
	MSG		msg;

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
	if (CheckKnownContent (va ("%s/ID1/save/*.sav", quakedir))) return true;

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

char MyDesktopFolder[MAX_PATH];

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

void Splash_Init (void);
void Splash_Destroy (void);

void SetQuakeDirectory (void)
{
	char currdir[MAX_PATH];

	// some people install quake to their desktops
	SHGetFolderPath (NULL, CSIDL_DESKTOP, NULL, SHGFP_TYPE_CURRENT, MyDesktopFolder);
	strcat (MyDesktopFolder, "\\quake");

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
		// show the splash screen at this point in time so that the user knows something is happening
		Splash_Init ();

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
	Splash_Destroy ();
	MessageBox (NULL, "Could not locate Quake on your PC.\n\nPerhaps you need to move DirectQ.exe into C:\\Quake?", "Error", MB_OK | MB_ICONERROR);
	Sys_Quit ();
}


void IN_ActivateMouse (void);
void IN_DeactivateMouse (void);
int MapKey (int key);
void ClearAllStates (void);
LONG CDAudio_MessageHandler(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

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
		IN_ActivateMouse ();
		IN_ShowMouse (FALSE);
		VID_SetAppGamma ();
		CDAudio_Resume ();
		block_drawing = false;

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

		// needed to reestablish the correct viewports
		vid.recalc_refdef = 1;
	}
	else
	{
		bWindowActive = false;
		IN_DeactivateMouse ();
		IN_ShowMouse (TRUE);
		VID_SetOSGamma ();
		CDAudio_Pause ();
		S_ClearBuffer ();
		block_drawing = true;

		if (modestate == MS_FULLDIB)
		{
			if (vid_canalttab)
			{ 
				vid_wassuspended = true;
			}
		}
	}
}


STICKYKEYS StartupStickyKeys = {sizeof (STICKYKEYS), 0};
TOGGLEKEYS StartupToggleKeys = {sizeof (TOGGLEKEYS), 0};
FILTERKEYS StartupFilterKeys = {sizeof (FILTERKEYS), 0};    


void AllowAccessibilityShortcutKeys (bool bAllowKeys)
{
	if (bAllowKeys)
	{
		// Restore StickyKeys/etc to original state and enable Windows key
		STICKYKEYS sk = StartupStickyKeys;
		TOGGLEKEYS tk = StartupToggleKeys;
		FILTERKEYS fk = StartupFilterKeys;

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


/* main window procedure */
LRESULT CALLBACK MainWndProc (HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	// wow, this is a WEIRD way of doing a window proc...
    LONG    lRet = 1;
	int		fActive, fMinimized, temp;
	extern unsigned int uiWheelMessage;

	if (Msg == uiWheelMessage) Msg = WM_MOUSEWHEEL;

    switch (Msg)
    {
	case WM_SYSCOMMAND:
		switch (wParam & ~0x0F)
		{
        case SC_SCREENSAVE:
        case SC_MONITORPOWER:
			// prevent from happening
			break;

		default:
			return DefWindowProc (hWnd, Msg, wParam, lParam);
		}
		break;

	case WM_PAINT:
		// minimal WM_PAINT processing
		ValidateRect (hWnd, NULL);
		break;

	case WM_ERASEBKGND:
		// don't let windows handle background erasures
		break;

	case WM_KILLFOCUS:
		if (modestate == MS_FULLDIB)
			ShowWindow (d3d_Window, SW_SHOWMINNOACTIVE);
		break;

	case WM_CREATE:
		break;

	case WM_MOVE:
		// update cursor clip region
		IN_UpdateClipCursor ();
		break;

	case WM_KEYDOWN:
	case WM_SYSKEYDOWN:
		Key_Event (MapKey (lParam), true);
		break;

	case WM_KEYUP:
	case WM_SYSKEYUP:
		Key_Event (MapKey (lParam), false);
		break;

	case WM_SYSCHAR:
		// keep Alt-Space from happening
		break;

	// this is complicated because Win32 seems to pack multiple mouse events into
	// one update sometimes, so we always check all states and look for events
	case WM_LBUTTONDOWN:
	case WM_LBUTTONUP:
	case WM_RBUTTONDOWN:
	case WM_RBUTTONUP:
	case WM_MBUTTONDOWN:
	case WM_MBUTTONUP:
	case WM_MOUSEMOVE:
		temp = 0;

		if (wParam & MK_LBUTTON) temp |= 1;
		if (wParam & MK_RBUTTON) temp |= 2;
		if (wParam & MK_MBUTTON) temp |= 4;

		// 3 buttons and we always send even if temp is 0 so that we'll get key up events on them too
		IN_MouseEvent (temp, 3, false);
		break;

	// JACK: This is the mouse wheel with the Intellimouse
	// Its delta is either positive or neg, and we generate the proper
	// Event.
	case WM_MOUSEWHEEL:
		if ((short) HIWORD (wParam) > 0)
		{
			Key_Event (K_MWHEELUP, true);
			Key_Event (K_MWHEELUP, false);
		}
		else
		{
			Key_Event (K_MWHEELDOWN, true);
			Key_Event (K_MWHEELDOWN, false);
		}

		break;

    case WM_SIZE:
        break;

   	case WM_CLOSE:
		if (MessageBox (d3d_Window, "Are you sure you want to quit?", "Confirm Exit", MB_YESNO | MB_SETFOREGROUND | MB_ICONQUESTION) == IDYES)
			Sys_Quit ();
	    break;

	case WM_ACTIVATE:
		fActive = LOWORD (wParam);
		fMinimized = (BOOL) HIWORD (wParam);
		AppActivate (!(fActive == WA_INACTIVE), fMinimized);

		// fix the leftover Alt from any Alt-Tab or the like that switched us away
		ClearAllStates ();

		break;

   	case WM_DESTROY:
		if (d3d_Window)
			DestroyWindow (d3d_Window);

        PostQuitMessage (0);
	    break;

	case MM_MCINOTIFY:
        lRet = CDAudio_MessageHandler (hWnd, Msg, wParam, lParam);
		break;

    default:
        // pass all unhandled messages to DefWindowProc
        lRet = DefWindowProc (hWnd, Msg, wParam, lParam);
	    break;
    }

    /* return 1 if handled message, 0 if not */
    return lRet;
}


LRESULT CALLBACK LowLevelKeyboardProc (int nCode, WPARAM wParam, LPARAM lParam)
{
	// do not process message
	if (nCode < 0 || nCode != HC_ACTION)
		return CallNextHookEx (hKeyboardHook, nCode, wParam, lParam);

	bool bEatKeystroke = false;
	KBDLLHOOKSTRUCT *p = (KBDLLHOOKSTRUCT *) lParam;

	switch (wParam)
	{
	case WM_KEYDOWN:
	case WM_KEYUP:
		bEatKeystroke = (bWindowActive && ((p->vkCode == VK_LWIN) || (p->vkCode == VK_RWIN)));
		break;

	default:
		break;
	}

	if (bEatKeystroke)
		return 1;
	else return CallNextHookEx (hKeyboardHook, nCode, wParam, lParam);
}


void Host_Frame (DWORD time);
void D3D_CreateShadeDots (void);

int WINAPI WinMain (HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	//MessageBox (NULL, "boo", "boo", MB_OK);

	InitCommonControls ();

	// set up and register the window class (d3d doesn't need CS_OWNDC)
	// do this before anything else so that we'll have it available for the splash too
	WNDCLASS wc;

	// here we use DefWindowProc because we're going to change it a few times and we don't want spurious messages
	wc.style = CS_CLASSDC | CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = (WNDPROC) DefWindowProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = GetModuleHandle (NULL);
	wc.hIcon = 0;
	wc.hCursor = LoadCursor (NULL,IDC_ARROW);
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
		CW_DEFAULT, CW_DEFAULT,
		CW_DEFAULT, CW_DEFAULT,
		GetDesktopWindow (),
		NULL,
		GetModuleHandle (NULL),
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

	quakeparms_t parms;
	char cwd[MAX_PATH];

	// previous instances do not exist in Win32
	// this is stupid as is can't be anothing but NULL...
	if (hPrevInstance) return 0;

	// Declare this process to be high DPI aware, and prevent automatic scaling
	HINSTANCE hUser32 = LoadLibrary ("user32.dll");

	if (hUser32)
	{
		typedef BOOL (WINAPI *LPSetProcessDPIAware) (void);
		LPSetProcessDPIAware pSetProcessDPIAware = (LPSetProcessDPIAware) GetProcAddress (hUser32, "SetProcessDPIAware");

		if (pSetProcessDPIAware) pSetProcessDPIAware ();
		UNLOAD_LIBRARY (hUser32);
	}

	// init memory pools
	// these need to be up as early as possible as other things in the startup use them
	Pool_Init ();

	global_nCmdShow = nCmdShow;

	// calc aliasmodel shading dotproducts
	// these are done from here to preserve stack space
	D3D_CreateShadeDots ();

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

	// initialize the splash screen
	Splash_Init ();

	DWORD time, oldtime, newtime;

    // Save the current sticky/toggle/filter key settings so they can be restored them later
    SystemParametersInfo (SPI_GETSTICKYKEYS, sizeof (STICKYKEYS), &StartupStickyKeys, 0);
    SystemParametersInfo (SPI_GETTOGGLEKEYS, sizeof (TOGGLEKEYS), &StartupToggleKeys, 0);
    SystemParametersInfo (SPI_GETFILTERKEYS, sizeof (FILTERKEYS), &StartupFilterKeys, 0);
 
    // Disable when full screen
    AllowAccessibilityShortcutKeys (false);

	// Initialization
    hKeyboardHook = SetWindowsHookEx (WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle (NULL), 0);

	Sys_Init ();
	Host_Init (&parms);
	oldtime = timeGetTime ();

	// main window message loop
	while (1)
	{
		// yield the CPU for a little while when paused, minimized, or not the focus
		if (cl.paused || !ActiveApp || Minimized || block_drawing)
		{
			// no point in bothering to draw
			scr_skipupdate = 1;

			if (cl.paused)
				Sleep (PAUSE_SLEEP);
			else Sleep (NOT_FOCUS_SLEEP);
		}

		newtime = timeGetTime ();

		// check for integer wraparound
		if (newtime < oldtime)
		{
			oldtime = newtime;
			continue;
		}

		// don't update if no time has passed
		if (newtime == oldtime) continue;

		time = newtime - oldtime;

		Host_Frame (time);
		oldtime = newtime;
	}

	// success of application
	return 0;
}

