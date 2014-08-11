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


#include "quakedef.h"
#include "d3d_quake.h"

#include "winquake.h"
#include "resource.h"
#include <commctrl.h>


extern int NumFilteredStates;
void D3D_SetDefaultStates (void);

DWORD d3d_3DFilterType = D3DTEXF_LINEAR;
DWORD d3d_ZbufferEnableFunction = D3DZB_TRUE;

// declarations that don't really fit in anywhere else
LPDIRECT3D9 d3d_Object = NULL;
LPDIRECT3DDEVICE9 d3d_Device = NULL;
D3DADAPTER_IDENTIFIER9 d3d_Adapter;
D3DCAPS9 d3d_DeviceCaps;
d3d_global_caps_t d3d_GlobalCaps;
D3DPRESENT_PARAMETERS d3d_PresentParams;

extern LPDIRECT3DINDEXBUFFER9 d3d_DPSkyIndexes;
extern LPDIRECT3DVERTEXBUFFER9 d3d_DPSkyVerts;

DWORD d3d_VertexBufferUsage = 0;

// if true all per-frame state updates are forced
bool d3d_ForceStateUpdates = true;

// for various render-to-texture and render-to-surface stuff
LPDIRECT3DSURFACE9 d3d_BackBuffer = NULL;

void D3D_InitUnderwaterTexture (void);
void D3D_KillUnderwaterTexture (void);

// gamma ramps
D3DGAMMARAMP d3d_DefaultGammaRamp;
D3DGAMMARAMP d3d_ActiveGammaRamp;

// global video state
viddef_t	vid;

// quake palette
unsigned	d_8to24table[256];

// window state management
bool d3d_DeviceLost = false;
bool vid_canalttab = false;
bool vid_wassuspended = false;
bool vid_initialized = false;
bool DDActive = true;
bool scr_skipupdate;


CD3D_MatrixStack *d3d_WorldMatrixStack = NULL;
CD3D_MatrixStack *d3d_ViewMatrixStack = NULL;
CD3D_MatrixStack *d3d_ProjMatrixStack = NULL;


// RotateAxisLocal requires instantiating a class and filling it's members just to pass
// 3 floats to it!  These little babies avoid that silly nonsense.  SORT IT OUT MICROSOFT!!!
D3DXVECTOR3 XVECTOR (1, 0, 0);
D3DXVECTOR3 YVECTOR (0, 1, 0);
D3DXVECTOR3 ZVECTOR (0, 0, 1);

// message handlers
LRESULT CALLBACK MainWndProc (HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
LONG CDAudio_MessageHandler (HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

// forward declarations of video menu functions
void VID_MenuDraw (void);
void VID_MenuKey (int key);

// for building the menu after video comes up
void Menu_VideoBuild (void);

// input imports
void IN_ActivateMouse (void);
void IN_DeactivateMouse (void);
void IN_HideMouse (void);
void IN_ShowMouse (void);
int MapKey (int key);

void VID_UpdateWindowStatus (void);
void ClearAllStates (void);
void AppActivate (BOOL fActive, BOOL minimize);

#define D3D_WINDOW_CLASS_NAME "D3DQuake Window Class"

// fixme - merge these two
HWND d3d_Window;

modestate_t	modestate = MS_UNINIT;

void Splash_Destroy (void);

// video cvars
// force an invalid mode on initial entry
cvar_t		vid_mode ("vid_mode", "-666", CVAR_ARCHIVE);
cvar_t		d3d_mode ("d3d_mode", "0", CVAR_ARCHIVE);
cvar_t		vid_wait ("vid_wait", "0");
cvar_t		v_gamma ("gamma", "1", CVAR_ARCHIVE);
cvar_t		r_gamma ("r_gamma", "1", CVAR_ARCHIVE);
cvar_t		g_gamma ("g_gamma", "1", CVAR_ARCHIVE);
cvar_t		b_gamma ("b_gamma", "1", CVAR_ARCHIVE);

// consistency with DP and FQ
cvar_t r_anisotropicfilter ("gl_texture_anisotropy", "1", CVAR_ARCHIVE);


typedef struct d3d_ModeDesc_s
{
	D3DDISPLAYMODE d3d_Mode;
	bool AllowWindowed;
	int BPP;
	int ModeNum;

	char ModeDesc[64];

	struct d3d_ModeDesc_s *Next;
} d3d_ModeDesc_t;

d3d_ModeDesc_t *d3d_ModeList = NULL;

D3DDISPLAYMODE d3d_DesktopMode;
D3DDISPLAYMODE d3d_CurrentMode;


D3DDISPLAYMODE d3d_BadDisplayMode =
{
	666,
	666,
	666,
	D3DFMT_UNKNOWN
};

d3d_ModeDesc_t d3d_BadMode =
{
	d3d_BadDisplayMode,
	false,
	0,
	-1,
	"Bad Mode",
	NULL
};


// list of mode formats we wish to test
D3DFORMAT AdapterModeDescs[] =
{
	// enumerate in this order
	D3DFMT_R5G6B5,
	D3DFMT_X1R5G5B5,
	D3DFMT_A1R5G5B5,
	D3DFMT_X8R8G8B8,
	D3DFMT_A8R8G8B8,
	D3DFMT_A2R10G10B10,
	D3DFMT_UNKNOWN
};

char *FormatStrings[] =
{
	"R5G6B5",
	"X1R5G5B5",
	"A1R5G5B5",
	"X8R8G8B8",
	"A8R8G8B8",
	"A2R10G10B10",
	"Unknown (Windowed)"
};


// rather than having lots and lots and lots of globals all holding multiple instances of the same data, let's do
// something radical, scary and potentially downright dangerous, and clean things up a bit.
DWORD		WindowStyle, ExWindowStyle;
int			window_center_x, window_center_y, window_x, window_y, window_width, window_height;
RECT		window_rect;


void D3D_SetConSize (int modewidth, int modeheight);

void D3D_ResetWindow (D3DDISPLAYMODE *mode)
{
	if (mode->RefreshRate == 0)
	{
		// windowed mode
		WindowStyle = WS_OVERLAPPED | WS_BORDER | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
		ExWindowStyle = 0;
	}
	else
	{
		// fullscreen mode
		WindowStyle = WS_POPUP;
		ExWindowStyle = 0;
	}

	RECT rect;
	rect.left = 0;
	rect.top = 0;
	rect.right = mode->Width;
	rect.bottom = mode->Height;

	// evaluate the rect size for the style
	AdjustWindowRectEx (&rect, WindowStyle, FALSE, 0);

	int width = rect.right - rect.left;
	int height = rect.bottom - rect.top;

	// resize the window
	SetWindowPos (d3d_Window, HWND_TOP, 0, 0, width, height, SWP_NOMOVE | SWP_SHOWWINDOW);

	// reset the styles
	SetWindowLong (d3d_Window, GWL_EXSTYLE, ExWindowStyle);
	SetWindowLong (d3d_Window, GWL_STYLE, WindowStyle);

	// the style reset requires a SWP to update cached info
	SetWindowPos (d3d_Window, HWND_TOP, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOSIZE | SWP_NOMOVE | SWP_SHOWWINDOW);

	// re-center it if windowed
	if (mode->RefreshRate == 0)
	{
		modestate = MS_WINDOWED;

		SetWindowPos
		(
			d3d_Window,
			HWND_TOP,
			(d3d_DesktopMode.Width - mode->Width) / 2,
			(d3d_DesktopMode.Height - mode->Height) / 3,
			0,
			0,
			SWP_NOSIZE | SWP_NOZORDER | SWP_SHOWWINDOW | SWP_DRAWFRAME
		);
	}
	else
	{
		// fullscreen mode
		modestate = MS_FULLDIB;

		// needed because we're not getting WM_MOVE messages fullscreen on NT
		window_x = 0;
		window_y = 0;
	}

	// store rect to window_rect
	window_rect.bottom = rect.bottom;
	window_rect.left = rect.left;
	window_rect.right = rect.right;
	window_rect.top = rect.top;

	// set up glx/y/width/height
	glx = 0;
	gly = 0;
	glwidth = mode->Width;
	glheight = mode->Height;

	// more craziness, loads of different variables storing the same thing... yuck yuck yuck - somebody shoot Carmack.
	window_width = glwidth;
	window_height = glheight;

	// reset console size
	D3D_SetConSize (mode->Width, mode->Height);

	// force a status update (i *think* this is all we need here)
	VID_UpdateWindowStatus ();
}


void D3D_SetGamma (D3DGAMMARAMP *ramp)
{
	if (!ramp) return;

	HDC hDC = GetDC (d3d_Window);

	for (int i = 0; i < 256; i++)
	{
		// clamp to 0-255 range
		ramp->red[i] = BYTE_CLAMP (ramp->red[i]);
		ramp->green[i] = BYTE_CLAMP (ramp->green[i]);
		ramp->blue[i] = BYTE_CLAMP (ramp->blue[i]);

		// store in MSBs
		ramp->red[i] <<= 8;
		ramp->green[i] <<= 8;
		ramp->blue[i] <<= 8;
	}

	// set using GDI, NOT direct3d
	// the D3DGAMMARAMP is already in the exact layout GDI needs, ao we're good...
	SetDeviceGammaRamp (hDC, ramp);

	ReleaseDC (d3d_Window, hDC);
}


/*
================
VID_UpdateWindowStatus
================
*/
void VID_UpdateWindowStatus (void)
{
	window_rect.left = window_x;
	window_rect.top = window_y;
	window_rect.right = window_x + window_width;
	window_rect.bottom = window_y + window_height;
	window_center_x = (window_rect.left + window_rect.right) / 2;
	window_center_y = (window_rect.top + window_rect.bottom) / 2;

	IN_UpdateClipCursor ();
}


D3DFORMAT D3D_GetDepthStencilFormat (D3DDISPLAYMODE *mode)
{
	D3DFORMAT ModeFormat = mode->Format;

	if (ModeFormat == D3DFMT_UNKNOWN) ModeFormat = d3d_DesktopMode.Format;

	// prefer faster formats
	D3DFORMAT DepthStencilFormats[] = {D3DFMT_D24X8, D3DFMT_D16, D3DFMT_D24S8, D3DFMT_UNKNOWN};

	for (int i = 0; ; i++)
	{
		// ran out of formats
		if (DepthStencilFormats[i] == D3DFMT_UNKNOWN) break;

		// check that the format exists
		HRESULT hr = d3d_Object->CheckDeviceFormat
		(
			D3DADAPTER_DEFAULT,
			D3DDEVTYPE_HAL,
			ModeFormat,
			D3DUSAGE_DEPTHSTENCIL,
			D3DRTYPE_SURFACE,
			DepthStencilFormats[i]
		);

		// format does not exist
		if (FAILED (hr)) continue;

		// check that the format is compatible
		hr = d3d_Object->CheckDepthStencilMatch
		(
			D3DADAPTER_DEFAULT,
			D3DDEVTYPE_HAL,
			ModeFormat,
			ModeFormat,
			DepthStencilFormats[i]
		);

		// format is not compatible
		if (FAILED (hr)) continue;

		// format is good to use now
		return DepthStencilFormats[i];
	}

	// didn't find one
	Sys_Error ("D3D_GetDepthStencilFormat: Failed to find a valid DepthStencil format");

	// shut up compiler
	return D3DFMT_UNKNOWN;
}


void D3D_SetPresentParams (D3DPRESENT_PARAMETERS *pp, D3DDISPLAYMODE *mode)
{
	memset (pp, 0, sizeof (D3DPRESENT_PARAMETERS));

	if (mode->RefreshRate == 0)
	{
		pp->Windowed = TRUE;
		pp->BackBufferFormat = D3DFMT_UNKNOWN;
	}
	else
	{
		pp->Windowed = FALSE;
		pp->BackBufferFormat = mode->Format;
	}

	pp->FullScreen_RefreshRateInHz = mode->RefreshRate;

	d3d_GlobalCaps.DepthStencilFormat = D3D_GetDepthStencilFormat (mode);
	pp->AutoDepthStencilFormat = d3d_GlobalCaps.DepthStencilFormat;
	pp->EnableAutoDepthStencil = TRUE;
	pp->Flags = D3DPRESENTFLAG_DISCARD_DEPTHSTENCIL;

	pp->SwapEffect = D3DSWAPEFFECT_DISCARD;
	pp->PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
	pp->BackBufferCount = 1;
	pp->BackBufferWidth = mode->Width;
	pp->BackBufferHeight = mode->Height;
	pp->hDeviceWindow = d3d_Window;
	pp->MultiSampleQuality = 0;
	pp->MultiSampleType = D3DMULTISAMPLE_NONE;
}


void D3D_ModeDescription (d3d_ModeDesc_t *mode)
{
	Con_Printf
	(
		"%2i: %ix%ix%i (%s)\n",
		mode->ModeNum,
		mode->d3d_Mode.Width,
		mode->d3d_Mode.Height,
		mode->BPP,
		mode->ModeDesc
	);
}


void D3D_DescribeModes_f (void)
{
	for (d3d_ModeDesc_t *mode = d3d_ModeList; mode; mode = mode->Next)
		D3D_ModeDescription (mode);
}


void D3D_NumModes_f (void)
{
	int nummodes = 0;

	for (d3d_ModeDesc_t *mode = d3d_ModeList; mode; mode = mode->Next)
		nummodes++;

	if (nummodes == 1)
		Con_Printf ("%d video mode is available\n", nummodes);
	else
		Con_Printf ("%d video modes are available\n", nummodes);
}


bool D3D_ModeIsCurrent (d3d_ModeDesc_t *mode)
{
	if (d3d_CurrentMode.RefreshRate)
	{
		if (mode->d3d_Mode.Format != d3d_CurrentMode.Format) return false;
		if (mode->d3d_Mode.Height != d3d_CurrentMode.Height) return false;
		if (mode->d3d_Mode.RefreshRate != d3d_CurrentMode.RefreshRate) return false;
		if (mode->d3d_Mode.Width != d3d_CurrentMode.Width) return false;
	}
	else
	{
		if (!mode->AllowWindowed) return false;
		if (mode->d3d_Mode.Height != d3d_CurrentMode.Height) return false;
		if (mode->d3d_Mode.Width != d3d_CurrentMode.Width) return false;
	}

	return true;
}


void D3D_SetVidMode (void)
{
	// puts a correct number into the d3d_mode cvar
	for (d3d_ModeDesc_t *mode = d3d_ModeList; mode; mode = mode->Next)
	{
		if (D3D_ModeIsCurrent (mode))
		{
			// set the correct value
			Cvar_Set (&d3d_mode, mode->ModeNum);
			return;
		}
	}
}


void D3D_DescribeCurrentMode_f (void)
{
	for (d3d_ModeDesc_t *mode = d3d_ModeList; mode; mode = mode->Next)
	{
		if (!D3D_ModeIsCurrent(mode)) continue;

		D3D_ModeDescription (mode);
		return;
	}
}


void D3D_DescribeMode_f (void)
{
	int modenum = atoi (Cmd_Argv (1));

	for (d3d_ModeDesc_t *mode = d3d_ModeList; mode; mode = mode->Next)
	{
		if (mode->ModeNum != modenum) continue;

		D3D_ModeDescription (mode);
		return;
	}

	Con_Printf ("Unknown video mode: %i\n", modenum);
}


void D3D_VidRestart_f (void)
{
	// release anything that needs to be released
	D3D_KillUnderwaterTexture ();

	// ensure that present params are valid
	D3D_SetPresentParams (&d3d_PresentParams, &d3d_CurrentMode);

	// reset the device
	HRESULT hr = d3d_Device->Reset (&d3d_PresentParams);

	if (FAILED (hr))
	{
		Con_Printf ("D3D_VidRestart_f: Unable to Reset Device.\n");
		return;
	}

	// tell the main renderer that the device has been "lost"
	// (it hasn't really, this is just a trick to send it into normal recovery move)
	d3d_DeviceLost = true;

	// this won't actually be displayed until after the reset completes as SCR_UpdateScreen is blocked
	Con_Printf ("vid_restart OK\n");
}


D3DFORMAT D3D_VidFindBPP (int bpp)
{
	// any matching bpp
	D3DFORMAT AnyBPP = D3DFMT_UNKNOWN;

	// attempt to find an exact match BPP in the mode list
	for (d3d_ModeDesc_t *mode = d3d_ModeList; mode; mode = mode->Next)
	{
		// bpp match
		if (mode->BPP == bpp) return mode->d3d_Mode.Format;

		// store any valid bpp
		AnyBPP = mode->d3d_Mode.Format;
	}

	// return any bpp we got
	if (AnyBPP != D3DFMT_UNKNOWN) return AnyBPP;

	Sys_Error ("D3D_VidFindBPP: Failed to find matching BPP");

	// shut up compiler
	return D3DFMT_UNKNOWN;
}


void D3D_EnumerateVideoModes (void)
{
	// get the desktop mode for reference
	d3d_Object->GetAdapterDisplayMode (D3DADAPTER_DEFAULT, &d3d_DesktopMode);

	int NumModes = 0;
	int NumWindowedModes = 0;

	// get the size of the desktop working area.  this is used instead of the desktop resolution for
	// determining if a mode is valid for windowed operation, as the desktop size gives 768 as a
	// valid height in an 800 high display, meaning that the taskbar will occlude parts of the DirectQ window.
	RECT WorkArea;

	SystemParametersInfo (SPI_GETWORKAREA, 0, &WorkArea, 0);

	int MaxWindowWidth = WorkArea.right - WorkArea.left;
	int MaxWindowHeight = WorkArea.bottom - WorkArea.top;

	// enumerate the modes in the adapter
	for (int m = 0; ; m++)
	{
		// end of the list
		if (AdapterModeDescs[m] == D3DFMT_UNKNOWN) break;

		// see can we support a fullscreen format with this mode format
		HRESULT hr = d3d_Object->CheckDeviceType (D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, AdapterModeDescs[m], AdapterModeDescs[m], FALSE);
		if (FAILED (hr)) continue;

		// see can we create a standard texture on it
		hr = d3d_Object->CheckDeviceFormat (D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, AdapterModeDescs[m], 0, D3DRTYPE_TEXTURE, D3DFMT_X8R8G8B8);
		if (FAILED (hr)) continue;

		// see can we create an alpha texture on it
		hr = d3d_Object->CheckDeviceFormat (D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, AdapterModeDescs[m], 0, D3DRTYPE_TEXTURE, D3DFMT_A8R8G8B8);
		if (FAILED (hr)) continue;

		// get the count of modes for this format
		int ModeCount = d3d_Object->GetAdapterModeCount (D3DADAPTER_DEFAULT, AdapterModeDescs[m]);

		// no modes available for this format
		if (!ModeCount) continue;

		// enumerate them all
		for (int i = 0; i < ModeCount; i++)
		{
			D3DDISPLAYMODE mode;

			// get the mode description
			d3d_Object->EnumAdapterModes (D3DADAPTER_DEFAULT, AdapterModeDescs[m], i, &mode);

			// we're only interested in modes that match the desktop refresh rate
			if (mode.RefreshRate != d3d_DesktopMode.RefreshRate) continue;

			// don't allow modes < 640 x 480
			if (mode.Width < 640) continue;
			if (mode.Height < 480) continue;

			// if the mode width is < height we assume that we have a monitor capable of rotating it's desktop
			// and therefore we skip the mode
			if (mode.Width < mode.Height) continue;

			// store it in our master mode list
			d3d_ModeDesc_t *newmode;

			// we need to keep our own list because d3d makes it awkward for us by maintaining a separate list for each format
			if (!d3d_ModeList)
			{
				d3d_ModeList = (d3d_ModeDesc_t *) Pool_Alloc (POOL_PERMANENT, sizeof (d3d_ModeDesc_t));
				newmode = d3d_ModeList;
			}
			else
			{
				// add it to the end of the list
				for (newmode = d3d_ModeList; ; newmode = newmode->Next)
					if (!newmode->Next)
						break;

				newmode->Next = (d3d_ModeDesc_t *) Pool_Alloc (POOL_PERMANENT, sizeof (d3d_ModeDesc_t));
				newmode = newmode->Next;
			}

			// copy out
			newmode->d3d_Mode.Format = mode.Format;
			newmode->d3d_Mode.Height = mode.Height;
			newmode->d3d_Mode.RefreshRate = mode.RefreshRate;
			newmode->d3d_Mode.Width = mode.Width;

			// store bpp
			switch (mode.Format)
			{
			case D3DFMT_R5G6B5:
			case D3DFMT_X1R5G5B5:
			case D3DFMT_A1R5G5B5:
				newmode->BPP = 16;
				break;

			default:
				newmode->BPP = 32;
				break;
			}

			// store the description
			strcpy (newmode->ModeDesc, FormatStrings[m]);

			// store the number
			newmode->ModeNum = NumModes++;

			// end of list pointer
			newmode->Next = NULL;

			// see is it valid for windowed
			newmode->AllowWindowed = true;

			// valid windowed modes must be the same format as the desktop and less than it's resolution
			if (newmode->d3d_Mode.Width >= MaxWindowWidth) newmode->AllowWindowed = false;
			if (newmode->d3d_Mode.Height >= MaxWindowHeight) newmode->AllowWindowed = false;
			if (newmode->d3d_Mode.Format != d3d_DesktopMode.Format) newmode->AllowWindowed = false;

			if (newmode->AllowWindowed) NumWindowedModes++;
		}
	}

	if (!d3d_ModeList)
	{
		Sys_Error ("D3D_EnumerateVideoModes: No RGB fullscreen modes available");
		return;
	}

	// didn't find any windowed modes
	if (!NumWindowedModes) return;

	// now we emulate winquake by pushing windowed modes to the start of the list
	d3d_ModeDesc_t *WindowedModes = (d3d_ModeDesc_t *) Pool_Alloc (POOL_PERMANENT, NumWindowedModes * sizeof (d3d_ModeDesc_t));
	int wm = 0;

	// walk the main list adding any windowed modes to the windowed modes list
	for (d3d_ModeDesc_t *mode = d3d_ModeList; mode; mode = mode->Next)
	{
		// not windowed
		if (!mode->AllowWindowed) continue;

		// copy the mode in
		WindowedModes[wm].AllowWindowed = true;
		WindowedModes[wm].BPP = 0;
		WindowedModes[wm].d3d_Mode.Format = mode->d3d_Mode.Format;
		WindowedModes[wm].d3d_Mode.Height = mode->d3d_Mode.Height;
		WindowedModes[wm].d3d_Mode.RefreshRate = 0;
		WindowedModes[wm].d3d_Mode.Width = mode->d3d_Mode.Width;

		strcpy (WindowedModes[wm].ModeDesc, "Windowed");

		// set the original not to allow windowed
		mode->AllowWindowed = false;

		// link in
		WindowedModes[wm].Next = &WindowedModes[wm + 1];
		wm++;

		// check for end
		if (wm == NumWindowedModes)
		{
			WindowedModes[wm - 1].Next = d3d_ModeList;
			break;
		}
	}

	// set the main list pointer back to the windowed modes list
	d3d_ModeList = WindowedModes;
	wm = 0;

	// finally walk the list setting consecutive mode numbers
	for (d3d_ModeDesc_t *mode = d3d_ModeList; mode; mode->ModeNum = wm++, mode = mode->Next);
}


void D3D_FindModeForVidMode (D3DDISPLAYMODE *mode)
{
	for (d3d_ModeDesc_t *findmode = d3d_ModeList; findmode; findmode = findmode->Next)
	{
		// look for a match
		if (findmode->ModeNum == (int) d3d_mode.value)
		{
			// copy them out
			mode->Width = findmode->d3d_Mode.Width;
			mode->Height = findmode->d3d_Mode.Height;

			// be certain to copy these out too so that we know they're valid for windowed or fullscreen
			mode->Format = findmode->d3d_Mode.Format;
			mode->RefreshRate = findmode->d3d_Mode.RefreshRate;

			// done
			return;
		}
	}

	// didn't find a match so we'll just let it pass through
}


void D3D_FindBestWindowedMode (D3DDISPLAYMODE *mode)
{
	D3DDISPLAYMODE *best = NULL;

	for (d3d_ModeDesc_t *winmode = d3d_ModeList; winmode; winmode = winmode->Next)
	{
		// not valid for a windowed mode
		if (!winmode->AllowWindowed) continue;

		if (!best)
		{
			// if we don't have a best mode set yet, the first one we find is it!
			best = &winmode->d3d_Mode;
			continue;
		}

		if (winmode->d3d_Mode.Width == mode->Width && winmode->d3d_Mode.Height == mode->Height)
		{
			// exact match
			best = &winmode->d3d_Mode;
			break;
		}

		// if either of width or height were unspecified, take one that matches
		if (winmode->d3d_Mode.Width == mode->Width && mode->Height == 0) best = &winmode->d3d_Mode;
		if (winmode->d3d_Mode.Width == 0 && winmode->d3d_Mode.Height == mode->Height) best = &winmode->d3d_Mode;

		// if both were unspecified we take the current windowed mode as the best
		if (mode->Height == 0 && mode->Width == 0) best = &winmode->d3d_Mode;
	}

	if (!best)
	{
		Sys_Error ("D3D_FindBestWindowedMode: Failed to find a Windowed mode");
		return;
	}

	// store best back to mode
	mode->Height = best->Height;
	mode->Width = best->Width;
	mode->RefreshRate = 0;
}


void D3D_FindBestFullscreenMode (D3DDISPLAYMODE *mode)
{
	D3DDISPLAYMODE *best = NULL;

	for (d3d_ModeDesc_t *fsmode = d3d_ModeList; fsmode; fsmode = fsmode->Next)
	{
		// invalid format
		if (fsmode->d3d_Mode.Format != mode->Format) continue;

		if (!best)
		{
			// if we don't have a best mode set yet, the first one we find is it!
			best = &fsmode->d3d_Mode;
			continue;
		}

		if (fsmode->d3d_Mode.Width == mode->Width && fsmode->d3d_Mode.Height == mode->Height)
		{
			// exact match
			best = &fsmode->d3d_Mode;
			break;
		}

		// if either of width or height were unspecified, take one that matches
		if (fsmode->d3d_Mode.Width == mode->Width && mode->Height == 0) best = &fsmode->d3d_Mode;
		if (fsmode->d3d_Mode.Width == 0 && fsmode->d3d_Mode.Height == mode->Height) best = &fsmode->d3d_Mode;

		// if both were unspecified we take the current fullscreen mode as the best
		if (mode->Height == 0 && mode->Width == 0) best = &fsmode->d3d_Mode;
	}

	if (!best)
	{
		Sys_Error ("D3D_FindBestFullscreenMode: Failed to find a Fullscreen mode");
		return;
	}

	// store best back to mode
	mode->Height = best->Height;
	mode->Width = best->Width;
}


void D3D_InitDirect3D (D3DDISPLAYMODE *mode)
{
	D3D_SetPresentParams (&d3d_PresentParams, mode);

	// create flags - try 'em all until we run out of options!
	LONG DesiredFlags[] =
	{
		// prefer hardware vertex processing
		// we can't use a pure device as we want to do some gets on renderstates
		D3DCREATE_HARDWARE_VERTEXPROCESSING,
		D3DCREATE_SOFTWARE_VERTEXPROCESSING,
		-1
	};

	for (int i = 0; ; i++)
	{
		if (DesiredFlags[i] == -1)
		{
			// failed
			Sys_Error ("D3D_InitDirect3D: d3d_Object->CreateDevice failed");
			return;
		}

		// attempt to create the device
		// Quake requires D3DCREATE_FPU_PRESERVE or it's internal timers go totally out of whack.
		// D3DCREATE_DISABLE_DRIVER_MANAGEMENT seems recommended; per the SDK:
		// Direct3D drivers are free to implement the 'driver managed textures' capability, indicated by D3DCAPS2_CANMANAGERESOURCE, which
		// allows the driver to handle the resource management instead of the runtime. For the (rare) driver that implements this feature,
		// the exact behavior of the driver's resource manager can vary widely and you should contact the driver vendor for details on how
		// this works for their implementation. Alternatively, you can ensure that the runtime manager is always used instead by specifying 
		// D3DCREATE_DISABLE_DRIVER_MANAGEMENT when creating the device.
		HRESULT hr = d3d_Object->CreateDevice
		(
			D3DADAPTER_DEFAULT,
			D3DDEVTYPE_HAL,
			d3d_Window,
			DesiredFlags[i] | D3DCREATE_FPU_PRESERVE | D3DCREATE_DISABLE_DRIVER_MANAGEMENT,
			&d3d_PresentParams,
			&d3d_Device
		);

		if (SUCCEEDED (hr))
		{
			Con_Printf ("Video mode %i (%ix%i) Initialized\n", (int) d3d_mode.value, mode->Width, mode->Height);

			for (int j = 0; ; j++)
			{
				// we ensured that the bb will be one of these formats at startup time
				if (mode->Format == AdapterModeDescs[j])
				{
					Con_Printf ("Back Buffer Format: %s\n", FormatStrings[j]);
					break;
				}
			}

			Con_Printf ("Refresh Rate: %i Hz (%s)\n", mode->RefreshRate, mode->RefreshRate ? "Fullscreen" : "Windowed");
			Con_Printf ("\n");

			if (DesiredFlags[i] & D3DCREATE_PUREDEVICE)
				Con_Printf ("Created Direct3D Pure Device\n");
			else Con_Printf ("Created Direct3D Device\n");

			// this is silly - if it supports mixed it will always support hardware!
			if (DesiredFlags[i] & D3DCREATE_HARDWARE_VERTEXPROCESSING)
			{
				d3d_VertexBufferUsage = 0;
				Con_Printf ("Using Hardware Vertex Processing\n\n");
			}
			else
			{
				d3d_VertexBufferUsage = D3DUSAGE_SOFTWAREPROCESSING;
				Con_Printf ("Using Software Vertex Processing\n\n");
			}

			break;
		}
	}

	if (d3d_PresentParams.AutoDepthStencilFormat == D3DFMT_D24S8)
		Con_Printf ("Depth/Stencil format: D24S8\n");
	else if (d3d_PresentParams.AutoDepthStencilFormat == D3DFMT_D24X8)
		Con_Printf ("Depth/Stencil format: D24X8 (stencil buffer unavailable)\n");
	else if (d3d_PresentParams.AutoDepthStencilFormat == D3DFMT_D16)
		Con_Printf ("Depth/Stencil format: D16 (stencil buffer unavailable)\n");
	else
	{
		Sys_Error ("D3D_InitDirect3D: Unknown Depth/Stencil format");
		return;
	}

	// get capabilities on the device
	HRESULT hr = d3d_Device->GetDeviceCaps (&d3d_DeviceCaps);

	if (FAILED (hr))
	{
		Sys_Error ("D3D_InitDirect3D: Failed to retrieve device caps");
		return;
	}

	// report on selected ones
	Con_Printf ("Available Texture Memory: %i MB\n", (d3d_Device->GetAvailableTextureMem ()) / (1024 * 1024));
	Con_Printf ("Maximum Texture Blend Stages: %i\n", d3d_DeviceCaps.MaxTextureBlendStages);
	Con_Printf ("Maximum Simultaneous Textures: %i\n", d3d_DeviceCaps.MaxSimultaneousTextures);
	Con_Printf ("Maximum Texture Size: %i x %i\n", d3d_DeviceCaps.MaxTextureWidth, d3d_DeviceCaps.MaxTextureHeight);
	Con_Printf ("Maximum Anisotropic Filter: %i\n", d3d_DeviceCaps.MaxAnisotropy);

	// check for the availability of w buffering
	if (d3d_DeviceCaps.RasterCaps & D3DPRASTERCAPS_WBUFFER)
	{
		d3d_ZbufferEnableFunction = D3DZB_USEW;
		Con_Printf ("Using W-Buffer\n");
	}
	else d3d_ZbufferEnableFunction = D3DZB_TRUE;

	// test texture
	LPDIRECT3DTEXTURE9 tex;

	// check for compressed texture formats
	hr = d3d_Device->CreateTexture
	(
		128,
		128,
		1,
		0,
		D3DFMT_DXT1,
		D3DPOOL_DEFAULT,
		&tex,
		NULL
	);

	if (SUCCEEDED (hr))
	{
		// done with the texture
		tex->Release ();
		d3d_GlobalCaps.supportDXT1 = true;
		Con_Printf ("Allowing DXT1 Compressed Texture Format\n");
	}
	else d3d_GlobalCaps.supportDXT1 = false;

	// check for compressed texture formats
	hr = d3d_Device->CreateTexture
	(
		128,
		128,
		1,
		0,
		D3DFMT_DXT3,
		D3DPOOL_DEFAULT,
		&tex,
		NULL
	);

	if (SUCCEEDED (hr))
	{
		// done with the texture
		tex->Release ();
		d3d_GlobalCaps.supportDXT3 = true;
		Con_Printf ("Allowing DXT3 Compressed Texture Format\n");
	}
	else d3d_GlobalCaps.supportDXT3 = false;

	// check for compressed texture formats
	hr = d3d_Device->CreateTexture
	(
		128,
		128,
		1,
		0,
		D3DFMT_DXT5,
		D3DPOOL_DEFAULT,
		&tex,
		NULL
	);

	if (SUCCEEDED (hr))
	{
		// done with the texture
		tex->Release ();
		d3d_GlobalCaps.supportDXT5 = true;
		Con_Printf ("Allowing DXT5 Compressed Texture Format\n");
	}
	else d3d_GlobalCaps.supportDXT5 = false;

	D3D_InitUnderwaterTexture ();

	Con_Printf ("\n");

	// get the default gamma ramp for the device
	d3d_Device->GetGammaRamp (0, &d3d_DefaultGammaRamp);

	// copy it to the active gamma ramp
	memcpy (&d3d_ActiveGammaRamp, &d3d_DefaultGammaRamp, sizeof (D3DGAMMARAMP));

	// set default states
	D3D_SetDefaultStates ();

	// set the d3d_mode cvar correctly
	D3D_SetVidMode ();

	// build the rest of the video menu (deferred to here as it's dependent on video being up)
	Menu_VideoBuild ();
}


void D3D_CreateWindowClass (void)
{
	HINSTANCE hInstance = GetModuleHandle (NULL);
	WNDCLASS wc;

	// set up and register the window class (d3d doesn't need CS_OWNDC)
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = (WNDPROC) MainWndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = hInstance;
    wc.hIcon = 0;
    wc.hCursor = LoadCursor (NULL,IDC_ARROW);
	wc.hbrBackground = NULL;
    wc.lpszMenuName = 0;
    wc.lpszClassName = D3D_WINDOW_CLASS_NAME;

	if (!RegisterClass (&wc))
	{
		Sys_Error ("D3D_CreateWindowClass: Failed to register Window Class");
		return;
	}
}


void D3D_SetConSize (int modewidth, int modeheight)
{
	// adjust conwidth and conheight to match the mode aspect
	// they should be the same aspect as the mode, with width never less than 640 and height never less than 480
	vid.conheight = 480;
	vid.conwidth = 480 * modewidth / modeheight;

	// bring it up to 640
	if (vid.conwidth < 640)
	{
		vid.conwidth = 640;
		vid.conheight = vid.conwidth * modeheight / modewidth;
	}

	// set width and height
	vid.width = vid.conwidth;
	vid.height = vid.conheight;
}


void D3D_CreateWindow (D3DDISPLAYMODE *mode)
{
	// externs from the messy stuff in vidnt.c
	extern HINSTANCE global_hInstance;

	window_rect.top = window_rect.left = 0;

	// store these out so that they will remain valid after everything is set up
	int DIBWidth = window_rect.right = mode->Width;
	int DIBHeight = window_rect.bottom = mode->Height;

	if (mode->RefreshRate == 0)
	{
		// windowed mode
		WindowStyle = WS_OVERLAPPED | WS_BORDER | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
		ExWindowStyle = 0;
	}
	else
	{
		// fullscreen mode
		WindowStyle = WS_POPUP;
		ExWindowStyle = 0;
	}

	RECT rect = window_rect;
	AdjustWindowRectEx (&rect, WindowStyle, FALSE, 0);

	int width = rect.right - rect.left;
	int height = rect.bottom - rect.top;

	d3d_Window = CreateWindowEx
	(
		ExWindowStyle,
		D3D_WINDOW_CLASS_NAME,
		"DirectQ Version 1.7.3",
		WindowStyle,
		rect.left, rect.top,
		width,
		height,
		NULL,
		NULL,
		global_hInstance,
		NULL
	);

	if (!d3d_Window)
	{
		Sys_Error ("Couldn't create DIB window");
		return;
	}

	if (mode->RefreshRate == 0)
	{
		// windowed mode
		modestate = MS_WINDOWED;

		// Center and show the DIB window
		SetWindowPos
		(
			d3d_Window,
			HWND_TOP,
			(d3d_DesktopMode.Width - mode->Width) / 2,
			(d3d_DesktopMode.Height - mode->Height) / 3,
			0,
			0,
			SWP_NOSIZE | SWP_NOZORDER | SWP_SHOWWINDOW | SWP_DRAWFRAME
		);
	}
	else
	{
		// fullscreen mode
		modestate = MS_FULLDIB;

		// needed because we're not getting WM_MOVE messages fullscreen on NT
		window_x = 0;
		window_y = 0;
	}

	ShowWindow (d3d_Window, SW_SHOWDEFAULT);
	UpdateWindow (d3d_Window);

	// because we have set the background brush for the window to NULL
	// (to avoid flickering when re-sizing the window on the desktop),
	// we clear the window to black when created, otherwise it will be
	// empty while Quake starts up.  use rect here as window_rect isn't valid yet
	// (is this actually NEEDED in d3d?)
	HDC hdc = GetDC (d3d_Window);
	PatBlt (hdc, 0, 0, rect.right, rect.bottom, BLACKNESS);
	ReleaseDC (d3d_Window, hdc);

	D3D_SetConSize (mode->Width, mode->Height);

	HICON hIcon = LoadIcon (global_hInstance, MAKEINTRESOURCE (IDI_APPICON));

	SendMessage (d3d_Window, WM_SETICON, (WPARAM) TRUE, (LPARAM) hIcon);
	SendMessage (d3d_Window, WM_SETICON, (WPARAM) FALSE, (LPARAM) hIcon);

	window_width = DIBWidth;
	window_height = DIBHeight;

	VID_UpdateWindowStatus ();
}


void D3D_SetVideoMode (D3DDISPLAYMODE *mode)
{
	// suspend stuff that could mess us up while creating the window
	bool temp = scr_disabled_for_loading;
	scr_disabled_for_loading = true;
	CDAudio_Pause ();

	// if neither width nor height were specified we take it from the d3d_mode cvar
	// this then passes through to the find best... functions so that the rest of the mode is filled in
	if (mode->Width == 0 && mode->Height == 0) D3D_FindModeForVidMode (mode);

	// even if a mode is found we pass through here
	if (mode->RefreshRate == 0)
		D3D_FindBestWindowedMode (mode);
	else
		D3D_FindBestFullscreenMode (mode);

	// create the mode and activate input
	D3D_CreateWindow (mode);
	IN_ActivateMouse ();
	IN_HideMouse ();

	// now initialize direct 3d on the window
	D3D_InitDirect3D (mode);

	// now resume the messy-uppy stuff
	CDAudio_Resume ();
	scr_disabled_for_loading = temp;

	// now we try to make sure we get the focus on the mode switch, because
	// sometimes in some systems we don't.  We grab the foreground, then
	// finish setting up, pump all our messages, and sleep for a little while
	// to let messages finish bouncing around the system, then we put
	// ourselves at the top of the z order, then grab the foreground again,
	// Who knows if it helps, but it probably doesn't hurt
	SetForegroundWindow (d3d_Window);

	MSG msg;

	while (PeekMessage (&msg, NULL, 0, 0, PM_REMOVE))
	{
      	TranslateMessage (&msg);
      	DispatchMessage (&msg);
	}

	Sleep (100);

	SetWindowPos
	(
		d3d_Window,
		HWND_TOP,
		0,
		0,
		0,
		0,
		SWP_DRAWFRAME | SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOCOPYBITS
	);

	SetForegroundWindow (d3d_Window);

	// fix the leftover Alt from any Alt-Tab or the like that switched us away
	ClearAllStates ();

	// force an immediate recalc of the refdef
	vid.recalc_refdef = 1;
}


int VID_PaletteHack (int in)
{
	// bilinear filtering can result in a LOT of lost detail and brightness, so
	// here we hack the palette to *kinda* restore it.  it feels wrong to do this,
	// but it does bring back the old "WinQuake-ey" look a bit better.
	int out = in;

	int temp = in * in;
	temp /= 512;

	out += temp;

	return BYTE_CLAMP (out);
}


void VID_SetPalette (unsigned char *palette)
{
	byte	*pal;
	unsigned r,g,b;
	unsigned v;
	unsigned short i;
	unsigned	*table;

	// 8 8 8 encoding
	pal = palette;
	table = d_8to24table;

	for (i = 0; i < 256; i++)
	{
		r = VID_PaletteHack (pal[0]);
		g = VID_PaletteHack (pal[1]);
		b = VID_PaletteHack (pal[2]);
		pal += 3;

		// BGRA format for D3D
		v = (255 << 24) + (b << 0) + (g << 8) + (r << 16);
		*table++ = v;
	}

	// set correct alpha colour to avoid pink fringes
	d_8to24table[255] = 0;
}


static void Check_Gamma (unsigned char *pal)
{
	int i;

	// set this properly...
	if ((i = COM_CheckParm ("-gamma")) == 0)
		;
	else Cvar_Set (&v_gamma, atof (com_argv[i + 1]));
}


cmd_t D3D_DescribeModes_f_Cmd ("vid_describemodes", D3D_DescribeModes_f);
cmd_t D3D_NumModes_f_Cmd ("vid_nummodes", D3D_NumModes_f);
cmd_t D3D_DescribeCurrentMode_f_Cmd ("vid_describecurrentmode", D3D_DescribeCurrentMode_f);
cmd_t D3D_DescribeMode_f_Cmd ("vid_describemode", D3D_DescribeMode_f);
cmd_t D3D_VidRestart_f_Cmd ("vid_restart", D3D_VidRestart_f);

void D3D_VidInit (byte *palette)
{
	InitCommonControls ();

	// ensure
	memset (&d3d_RenderDef, 0, sizeof (d3d_renderdef_t));

	vid_initialized = true;

	vid.colormap = host_colormap;
	vid.fullbright = 256 - LittleLong (*((int *) vid.colormap + 2048));

	Check_Gamma (palette);
	VID_SetPalette (palette);

	vid_canalttab = true;

	// dump the colormap out to file so that we can have a look-see at what's in it
	// SCR_WriteDataToTGA ("colormap.tga", vid.colormap, 256, 64, 8, 24);

	if (!(d3d_Object = Direct3DCreate9 (D3D_SDK_VERSION)))
	{
		Sys_Error ("D3D_InitDirect3D - failed to initialize Direct 3D!");
		return;
	}

	// enumerate available modes
	D3D_EnumerateVideoModes ();

	d3d_Object->GetAdapterIdentifier (D3DADAPTER_DEFAULT, 0, &d3d_Adapter);

	Con_Printf ("\nInitialized Direct 3D on %s\n", d3d_Adapter.DeviceName);
	Con_Printf ("With %s (%s)\n", d3d_Adapter.Description, d3d_Adapter.Driver);
	Con_Printf ("\n");

	// detect nvidia
	d3d_GlobalCaps.isNvidia = false;

	// sigh... i thought we would have left this kinda crap behind with the last century...
	for (int i = 0; ; i++)
	{
		if (!d3d_Adapter.Description[i]) break;

		if (!strnicmp (&d3d_Adapter.Description[i], "nvidia", 6))
		{
			d3d_GlobalCaps.isNvidia = true;
			break;
		}
	}

	// check command-line args for resolution specifiers
	if (COM_CheckParm ("-window"))
	{
		// ensure that we're in an RGB mode if running windowed!
		HDC hdc = GetDC (NULL);

		if (GetDeviceCaps (hdc, RASTERCAPS) & RC_PALETTE)
		{
			Sys_Error ("D3D_VidStartup: Can't run Windowed Mode in non-RGB Display Mode");
			return;
		}

		ReleaseDC (NULL, hdc);

		// windowed modes use the same format as the desktop
		d3d_CurrentMode.Format = D3DFMT_UNKNOWN;

		// refresh rate 0
		d3d_CurrentMode.RefreshRate = 0;
	}
	else
	{
		// check for bpp
		if (COM_CheckParm ("-bpp"))
		{
			// retrieve the desired BPP
			int bpp = atoi (com_argv[COM_CheckParm ("-bpp") + 1]);

			// set the mode format according to the BPP
			if (bpp == 32)
				d3d_CurrentMode.Format = D3D_VidFindBPP (32);
			else d3d_CurrentMode.Format = D3D_VidFindBPP (16);
		}
		else
		{
			// use the desktop format
			d3d_CurrentMode.Format = d3d_DesktopMode.Format;
		}

		// refresh rate is always the same as the desktop
		d3d_CurrentMode.RefreshRate = d3d_DesktopMode.RefreshRate;
	}

	// check for height
	if (COM_CheckParm ("-width"))
	{
		// if they specify width store it out
		d3d_CurrentMode.Width = atoi (com_argv[COM_CheckParm ("-width") + 1]);
	}
	else
	{
		// unspecified - set it to 0
		d3d_CurrentMode.Width = 0;
	}

	// check for height
	if (COM_CheckParm ("-height"))
	{
		// if they specify height store it out
		d3d_CurrentMode.Height = atoi (com_argv[COM_CheckParm ("-height") + 1]);
	}
	else
	{
		// unspecified - set it to 0
		d3d_CurrentMode.Height = 0;
	}

	// set up the window class
	D3D_CreateWindowClass ();

	Splash_Destroy ();

	// set the selected video mode
	D3D_SetVideoMode (&d3d_CurrentMode);

	// finally set the palette
	VID_SetPalette (palette);
}


void D3D_ShutdownDirect3D (void)
{
	// restore original gamma ramp
	D3D_SetGamma (&d3d_DefaultGammaRamp);

	// release anything that needs to be released
	D3D_ReleaseTextures ();

	// release everything else
	SAFE_RELEASE (d3d_DPSkyIndexes);
	SAFE_RELEASE (d3d_DPSkyVerts);

	// destroy the device and object
	SAFE_RELEASE (d3d_Device);
	SAFE_RELEASE (d3d_Object);
}


void VID_Shutdown (void)
{
	if (vid_initialized)
	{
		vid_canalttab = false;

		D3D_ShutdownDirect3D ();

		AppActivate (false, false);
	}
}


// thanks to www.codesampler.com for the groundwork for this one
bool D3D_CheckRecoverDevice (void)
{
	// check was the device lost
	if (d3d_DeviceLost)
	{
		// yield some CPU time
		Sleep (100);

		// see if the device can be recovered
		HRESULT hr = d3d_Device->TestCooperativeLevel ();

		switch (hr)
		{
		case D3D_OK:
			// recreate anything that needs to be recreated
			D3D_InitUnderwaterTexture ();

			// set default states
			D3D_SetDefaultStates ();

			// force an update of per-frame checked states
			d3d_ForceStateUpdates = true;

			// force a recalc of the refdef
			vid.recalc_refdef = true;

			// the device is no longer lost
			d3d_DeviceLost = false;
			return true;

		case D3DERR_DEVICELOST:
			// the device cannot be recovered at this time
			break;

		case D3DERR_DEVICENOTRESET:
			// the device is ready to be reset
			// release anything that needs to be released
			D3D_KillUnderwaterTexture ();

			// ensure that present params are valid
			D3D_SetPresentParams (&d3d_PresentParams, &d3d_CurrentMode);

			// reset the device
			d3d_Device->Reset (&d3d_PresentParams);
			break;

		case D3DERR_DRIVERINTERNALERROR:
		default:
			// something bad happened
			// note: this isn't really a proper clean-up path as it doesn't do input/sound/etc cleanup either...
			DestroyWindow (d3d_Window);
			exit (0);
			break;
		}

		// not quite ready yet
		return false;
	}

	// device is not lost
	return true;
}


void D3D_CheckTextureFiltering (void)
{
	static int old_aniso = -1;
	int real_aniso;

	// check the cvar first for an early-out
	if (r_anisotropicfilter.integer == old_aniso && !d3d_ForceStateUpdates) return;

	// get the real value from the cvar - users may enter any old crap manually!!!
	for (real_aniso = 1; real_aniso < r_anisotropicfilter.value; real_aniso <<= 1);

	// clamp it
	if (real_aniso < 1) real_aniso = 1;
	if (real_aniso > d3d_DeviceCaps.MaxAnisotropy) real_aniso = d3d_DeviceCaps.MaxAnisotropy;

	// store it back
	Cvar_Set (&r_anisotropicfilter, real_aniso);

	// no change
	if (real_aniso == old_aniso && !d3d_ForceStateUpdates) return;

	// store out
	old_aniso = real_aniso;

	if (real_aniso == 1)
	{
		// regular linear filtering
		d3d_3DFilterType = D3DTEXF_LINEAR;

		if (key_dest == key_console) Con_Printf ("Set Linear Filtering\n");
	}
	else
	{
		// anisotropic filtering
		d3d_3DFilterType = D3DTEXF_ANISOTROPIC;

		if (key_dest == key_console) Con_Printf ("Set %i x Anisotropic Filtering\n", real_aniso);
	}

	for (int i = 0; i < d3d_DeviceCaps.MaxTextureBlendStages; i++)
	{
		D3D_SetSamplerState (i, D3DSAMP_MAXANISOTROPY, real_aniso);
		D3D_SetSamplerState (i, D3DSAMP_MINFILTER, d3d_3DFilterType);
		D3D_SetSamplerState (i, D3DSAMP_MIPFILTER, d3d_3DFilterType);
		D3D_SetSamplerState (i, D3DSAMP_MAGFILTER, d3d_3DFilterType);
	}
}


void D3D_CheckVidMode (void)
{
	// 0 is a valid d3d_mode!
	static int old_vidmode = -1;

	if (old_vidmode == d3d_mode.integer) return;

	if (old_vidmode == -1)
	{
		// first time is not a mode change
		old_vidmode = d3d_mode.integer;
		return;
	}

	// testing
	Con_DPrintf ("mode is %i (was %i)...\n", (int) d3d_mode.value, old_vidmode);

	// attempt to find the mode
	d3d_ModeDesc_t *findmode = NULL;

	// find the supplied mode and see if it has REALLY changed
	// (first time into this function it won't have)
	for (findmode = d3d_ModeList; findmode; findmode = findmode->Next)
	{
		// look for a match
		if (findmode->ModeNum == d3d_mode.integer)
		{
			// look for differences
			if (findmode->d3d_Mode.Format != d3d_CurrentMode.Format) break;
			if (findmode->d3d_Mode.Width != d3d_CurrentMode.Width) break;
			if (findmode->d3d_Mode.Height != d3d_CurrentMode.Height) break;
			if (findmode->d3d_Mode.RefreshRate != d3d_CurrentMode.RefreshRate) break;

			// no differences found so it hasn't changed, just get out
			Con_Printf ("Mode is unchanged\n");

			// store back to prevent this from repeatedly occurring
			old_vidmode = d3d_mode.integer;
			return;
		}
	}

	// ensure that the search found something
	if (!findmode)
	{
		// didn't find the requested new mode
		Con_Printf ("D3D_CheckVidMode: selected video mode not available\n");

		// restore the previous value
		Cvar_Set (&d3d_mode, old_vidmode);

		// get out
		return;
	}

	// store back (deferred to here so that we can restore the value if the mode isn't found)
	old_vidmode = d3d_mode.integer;

	// reset the window
	D3D_ResetWindow (&findmode->d3d_Mode);

	// store to current mode
	d3d_CurrentMode.Format = findmode->d3d_Mode.Format;
	d3d_CurrentMode.Height = findmode->d3d_Mode.Height;
	d3d_CurrentMode.RefreshRate = findmode->d3d_Mode.RefreshRate;
	d3d_CurrentMode.Width = findmode->d3d_Mode.Width;

	// restart video
	D3D_VidRestart_f ();

	// update the refdef
	vid.recalc_refdef = 1;
}


int D3D_AdjustGamma (float gammaval, int baseval)
{
	// calculate ramp for given gamma value
	int inf = 255 * pow ((float) ((baseval + 0.5) / 255.5), (float) gammaval) + 0.5;

	// return what we got
	return BYTE_CLAMP (inf);
}


void D3D_CheckGamma (void)
{
	static int oldvgamma = 0;
	static int oldrgamma = 0;
	static int oldggamma = 0;
	static int oldbgamma = 0;
	extern bool crosshair_recache;

	// didn't change - call me paranoid about floats!!!
	if ((int) (v_gamma.value * 100) == oldvgamma &&
		(int) (r_gamma.value * 100) == oldrgamma &&
		(int) (g_gamma.value * 100) == oldggamma &&
		(int) (b_gamma.value * 100) == oldbgamma &&
		!d3d_ForceStateUpdates)
	{
		// didn't change
		return;
	}

	// store back
	oldvgamma = (int) (v_gamma.value * 100);
	oldrgamma = (int) (r_gamma.value * 100);
	oldggamma = (int) (g_gamma.value * 100);
	oldbgamma = (int) (b_gamma.value * 100);

	// to do - make this independent for r/g/b - done.
	for (int i = 0; i < 256; i++)
	{
		// play nice - base this on the user's selected gamma rather than on 0-255
		// as they may have adjusted it for their own monitor.
		// adjust for each component initially, then adjust by master value
		d3d_ActiveGammaRamp.red[i] = D3D_AdjustGamma (r_gamma.value, d3d_DefaultGammaRamp.red[i]);
		d3d_ActiveGammaRamp.green[i] = D3D_AdjustGamma (g_gamma.value, d3d_DefaultGammaRamp.green[i]);
		d3d_ActiveGammaRamp.blue[i] = D3D_AdjustGamma (b_gamma.value, d3d_DefaultGammaRamp.blue[i]);

		// now adjust them all by the master
		d3d_ActiveGammaRamp.red[i] = D3D_AdjustGamma (v_gamma.value, d3d_ActiveGammaRamp.red[i]);
		d3d_ActiveGammaRamp.green[i] = D3D_AdjustGamma (v_gamma.value, d3d_ActiveGammaRamp.green[i]);
		d3d_ActiveGammaRamp.blue[i] = D3D_AdjustGamma (v_gamma.value, d3d_ActiveGammaRamp.blue[i]);
	}

	// set the new gamma ramp
	D3D_SetGamma (&d3d_ActiveGammaRamp);
}


void D3D_BeginRendering (int *x, int *y, int *width, int *height)
{
	// if (NumFilteredStates) Con_Printf ("Filtered %i redundant state changes\n", NumFilteredStates);

	NumFilteredStates = 0;

	// check for device recovery and recover it if needed
	if (!D3D_CheckRecoverDevice ()) return;

	// check for any changes to any display properties
	D3D_CheckGamma ();
	D3D_CheckVidMode ();
	D3D_CheckTextureFiltering ();

	// flush any state update forcing
	d3d_ForceStateUpdates = false;

	*x = *y = 0;
	*width = window_rect.right - window_rect.left;
	*height = window_rect.bottom - window_rect.top;

	// moved begin scene to here
	d3d_Device->BeginScene ();
}


void D3D_EndRendering (void)
{
	d3d_Device->EndScene ();

	HRESULT hr = d3d_Device->Present (NULL, NULL, NULL, NULL);

	// check for a lost device
	if (hr == D3DERR_DEVICELOST) d3d_DeviceLost = true;
}


void AppActivate (BOOL fActive, BOOL minimize)
{
	static BOOL	sound_active;

	ActiveApp = fActive;
	Minimized = minimize;

	// enable/disable sound on focus gain/loss
	if (!ActiveApp && sound_active)
		sound_active = false;
	else if (ActiveApp && !sound_active)
		sound_active = true;

	if (fActive)
	{
		IN_ActivateMouse ();
		IN_HideMouse ();

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
		IN_DeactivateMouse ();
		IN_ShowMouse ();

		if (modestate == MS_FULLDIB)
		{
			if (vid_canalttab)
			{ 
				vid_wassuspended = true;
			}
		}
	}
}


/* main window procedure */
LRESULT CALLBACK MainWndProc (HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    LONG    lRet = 1;
	int		fActive, fMinimized, temp;
	extern unsigned int uiWheelMessage;

	if (Msg == uiWheelMessage) Msg = WM_MOUSEWHEEL;

    switch (Msg)
    {
	case WM_GRAPHEVENT:
		DS_Event ();
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
		window_x = (int) LOWORD (lParam);
		window_y = (int) HIWORD (lParam);
		VID_UpdateWindowStatus ();
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

		if (wParam & MK_LBUTTON)
			temp |= 1;

		if (wParam & MK_RBUTTON)
			temp |= 2;

		if (wParam & MK_MBUTTON)
			temp |= 4;

		IN_MouseEvent (temp);

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


//========================================================
// Video menu stuff
//========================================================


#include "menu_common.h"

char **menu_videomodes = NULL;
int menu_videomodenum = 0;

char **menu_anisotropicmodes = NULL;
int menu_anisonum = 0;

extern cvar_t gl_conscale;
extern cvar_t scr_fov;
extern cvar_t scr_fovcompat;

#define TAG_VIDMODEAPPLY	1

// if these are changed they need to be changed in menu_other.cpp as well
#define MENU_TAG_SIMPLE		666
#define MENU_TAG_FULL		1313

void VID_ApplyModeChange (void)
{
	// the value here has already been forced to correct in the draw func so we just set it
	Cvar_Set (&d3d_mode, menu_videomodenum);

	// position the selection back at the video mode option
	menu_Video.Key (K_UPARROW);
}


int Menu_VideoCustomDraw (int y)
{
	// check for "Apply" on d3d_mode change
	if (menu_videomodenum == d3d_mode.integer)
		menu_Video.DisableOptions (TAG_VIDMODEAPPLY);
	else menu_Video.EnableOptions (TAG_VIDMODEAPPLY);

	d3d_ModeDesc_t *mode;
	int nummodes;

	// ensure videomode num is valid
	// (no longer forces an instant change)
	for (mode = d3d_ModeList, nummodes = 0; mode; mode = mode->Next, nummodes++)
	{
		if (menu_videomodenum == nummodes)
		{
			// store to d3d_mode cvar
			//d3d_mode.value = nummodes;
			//Cvar_Set (&d3d_mode, d3d_mode.value);
			goto do_aniso;
		}
	}

	// invalid mode in d3d_mode so force it to the current mode
	for (mode = d3d_ModeList, nummodes = 0; mode; mode = mode->Next, nummodes++)
	{
		if (D3D_ModeIsCurrent (mode))
		{
			menu_videomodenum = /*d3d_mode.value =*/ nummodes;
			//Cvar_Set (&d3d_mode, d3d_mode.value);
			break;
		}
	}

do_aniso:
	// no anisotropic filtering available
	if (d3d_DeviceCaps.MaxAnisotropy < 2) return y;

	// store the selected anisotropic filter into the r_aniso cvar
	for (int af = 1, i = 0; ; i++, af <<= 1)
	{
		if (i == menu_anisonum)
		{
			Con_DPrintf ("Setting r_anisotropicfilter to %i\n", af);
			Cvar_Set (&r_anisotropicfilter, af);
			break;
		}
	}

	return y;
}


void Menu_VideoCustomEnter (void)
{
	// take it from the d3d_mode cvar
	menu_videomodenum = (int) d3d_mode.value;

	int real_aniso;

	// no anisotropic filtering available
	if (d3d_DeviceCaps.MaxAnisotropy < 2) return;

	// get the real value from the cvar - users may enter any old crap manually!!!
	for (real_aniso = 1; real_aniso < r_anisotropicfilter.value; real_aniso <<= 1);

	// clamp it
	if (real_aniso < 1) real_aniso = 1;
	if (real_aniso > d3d_DeviceCaps.MaxAnisotropy) real_aniso = d3d_DeviceCaps.MaxAnisotropy;

	// store it back
	Cvar_Set (&r_anisotropicfilter, real_aniso);

	// now derive the menu entry from it
	for (int i = 0, af = 1; ; i++, af <<= 1)
	{
		if (af == real_aniso)
		{
			menu_anisonum = i;
			break;
		}
	}
}


void Menu_VideoBuild (void)
{
	d3d_ModeDesc_t *mode;
	int nummodes;

	// get the number of modes
	for (mode = d3d_ModeList, nummodes = 0; mode; mode = mode->Next, nummodes++);

	// add 1 for terminating NULL
	menu_videomodes = (char **) Pool_Alloc (POOL_PERMANENT, (nummodes + 1) * sizeof (char *));

	// now write them in
	for (mode = d3d_ModeList, nummodes = 0; mode; mode = mode->Next, nummodes++)
	{
		menu_videomodes[nummodes] = (char *) Pool_Alloc (POOL_PERMANENT, 128);

		_snprintf
		(
			menu_videomodes[nummodes],
			128,
			"%i x %i x %i (%s)",
			mode->d3d_Mode.Width,
			mode->d3d_Mode.Height,
			mode->BPP,
			mode->ModeDesc
		);

		// select current mode
		if (D3D_ModeIsCurrent (mode)) menu_videomodenum = nummodes;
	}

	// terminate with NULL
	menu_videomodes[nummodes] = NULL;

	menu_Video.AddOption (new CQMenuCustomDraw (Menu_VideoCustomDraw));
	menu_Video.AddOption (new CQMenuSpacer ("Select a Video Mode"));
	menu_Video.AddOption (new CQMenuSpinControl (NULL, &menu_videomodenum, menu_videomodes));
	menu_Video.AddOption (TAG_VIDMODEAPPLY, new CQMenuSpacer (DIVIDER_LINE));
	menu_Video.AddOption (TAG_VIDMODEAPPLY, new CQMenuCommand ("Apply Video Mode Change", VID_ApplyModeChange));

	// add the rest of the options to ensure that they;re kept in order
	menu_Video.AddOption (new CQMenuSpacer ());
	menu_Video.AddOption (MENU_TAG_FULL, new CQMenuTitle ("Configure Video Options"));
	menu_Video.AddOption (MENU_TAG_FULL, new CQMenuCvarSlider ("Screen Size", &scr_viewsize, 30, 120, 10));
	menu_Video.AddOption (MENU_TAG_FULL, new CQMenuCvarSlider ("Console Size", &gl_conscale, 1, 0, 0.1));

	if (d3d_DeviceCaps.MaxAnisotropy > 1)
	{
		// count the number of modes available
		for (int i = 1, mode = 1; ; i++, mode *= 2)
		{
			if (mode == d3d_DeviceCaps.MaxAnisotropy)
			{
				menu_anisotropicmodes = (char **) Pool_Alloc (POOL_PERMANENT, (i + 1) * sizeof (char *));

				for (int m = 0, f = 1; m < i; m++, f *= 2)
				{
					menu_anisotropicmodes[m] = (char *) Pool_Alloc (POOL_PERMANENT, 32);

					if (f == 1)
						strncpy (menu_anisotropicmodes[m], "Off", 32);
					else _snprintf (menu_anisotropicmodes[m], 32, "%i x Filtering", f);
				}

				menu_anisotropicmodes[i] = NULL;
				break;
			}
		}

		menu_Video.AddOption (new CQMenuSpinControl ("Anisotropic Filter", &menu_anisonum, menu_anisotropicmodes));
	}

	menu_Video.AddOption (MENU_TAG_FULL, new CQMenuCvarSlider ("Field of View", &scr_fov, 10, 170, 5));
	menu_Video.AddOption (MENU_TAG_FULL, new CQMenuCvarToggle ("Compatible FOV", &scr_fovcompat, 0, 1));
	menu_Video.AddOption (MENU_TAG_FULL, new CQMenuTitle ("Brightness Controls"));
	menu_Video.AddOption (MENU_TAG_FULL, new CQMenuCvarSlider ("Master Gamma", &v_gamma, 1.75, 0.25, 0.05));
	menu_Video.AddOption (MENU_TAG_FULL, new CQMenuCvarSlider ("Red Gamma", &r_gamma, 1.75, 0.25, 0.05));
	menu_Video.AddOption (MENU_TAG_FULL, new CQMenuCvarSlider ("Green Gamma", &g_gamma, 1.75, 0.25, 0.05));
	menu_Video.AddOption (MENU_TAG_FULL, new CQMenuCvarSlider ("Blue Gamma", &b_gamma, 1.75, 0.25, 0.05));
}


