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


#include "quakedef.h"
#include "d3d_model.h"
#include "d3d_quake.h"

#include "winquake.h"
#include "resource.h"
#include <commctrl.h>

#ifdef _DEBUG
#pragma comment (lib, "d3d9.lib")
//#pragma comment (lib, "d3dx9d.lib")
#pragma comment (lib, "d3dx9.lib")
#else
#pragma comment (lib, "d3d9.lib")
#pragma comment (lib, "d3dx9.lib")
#endif

#define VIDDRIVER_VERSION "1.8.7"
cvar_t viddriver_version ("viddriver_version", "unknown", CVAR_ARCHIVE);

void D3D_SetAllStates (void);

extern bool WinDWM;

// declarations that don't really fit in anywhere else
LPDIRECT3D9 d3d_Object = NULL;
LPDIRECT3DDEVICE9 d3d_Device = NULL;

D3DADAPTER_IDENTIFIER9 d3d_Adapter;
D3DCAPS9 d3d_DeviceCaps;
d3d_global_caps_t d3d_GlobalCaps;
D3DPRESENT_PARAMETERS d3d_PresentParams;

// global video state
viddef_t	vid;

// window state management
bool vid_canalttab = false;
bool vid_initialized = false;
bool scr_skipupdate;

// forward declarations of video menu functions
void VID_MenuDraw (void);
void VID_MenuKey (int key);

// for building the menu after video comes up
void Menu_VideoBuild (void);

// hlsl
void D3DHLSL_Init (void);
void D3DHLSL_Shutdown (void);

// lightmaps
void D3D_LoseLightmapResources (void);
void D3D_RecoverLightmapResources (void);
void D3DRTT_CreateRTTTexture (void);

// fixme - merge these two
HWND d3d_Window;

modestate_t	modestate = MS_UNINIT;

// video cvars
// force an invalid mode on initial entry
cvar_t		vid_mode ("vid_mode", "-666", CVAR_ARCHIVE);
cvar_t		d3d_mode ("d3d_mode", "-1", CVAR_ARCHIVE);
cvar_t		vid_wait ("vid_wait", "0");
cvar_t		v_gamma ("gamma", "1", CVAR_ARCHIVE);
cvar_t		r_gamma ("r_gamma", "1", CVAR_ARCHIVE);
cvar_t		g_gamma ("g_gamma", "1", CVAR_ARCHIVE);
cvar_t		b_gamma ("b_gamma", "1", CVAR_ARCHIVE);
cvar_t		vid_vsync ("vid_vsync", "0", CVAR_ARCHIVE);

typedef struct vid_gammaramp_s
{
	WORD r[256];
	WORD g[256];
	WORD b[256];
} vid_gammaramp_t;

vid_gammaramp_t d3d_DefaultGamma;
vid_gammaramp_t d3d_CurrentGamma;

void VID_SetGammaGeneric (vid_gammaramp_t *gr)
{
	HDC hdc = GetDC (NULL);
	SetDeviceGammaRamp (hdc, gr);
	ReleaseDC (NULL, hdc);
}


void VID_SetOSGamma (void) {VID_SetGammaGeneric (&d3d_DefaultGamma);}
void VID_SetAppGamma (void) {VID_SetGammaGeneric (&d3d_CurrentGamma);}


// consistency with DP and FQ
cvar_t r_anisotropicfilter ("gl_texture_anisotropy", "1", CVAR_ARCHIVE);
cvar_alias_t r_anisotropicfilter_alias ("r_anisotropicfilter", &r_anisotropicfilter);

cvar_t gl_triplebuffer ("gl_triplebuffer", 1, CVAR_ARCHIVE);

d3d_ModeDesc_t *d3d_ModeList = NULL;

int d3d_NumModes = 0;
int d3d_NumWindowedModes = 0;

D3DDISPLAYMODE d3d_DesktopMode;
D3DDISPLAYMODE d3d_CurrentMode;

d3d_ModeDesc_t d3d_BadMode = {{666, 666, 666, D3DFMT_UNKNOWN}, false, 0, -1, "Bad Mode"};


// list of mode formats we wish to test
D3DFORMAT d3d_AdapterModeDescs[] =
{
	// enumerate in this order
	D3DFMT_R5G6B5,
	D3DFMT_X1R5G5B5,
	D3DFMT_A1R5G5B5,
	D3DFMT_X8R8G8B8,
	D3DFMT_UNKNOWN
};


// rather than having lots and lots and lots of globals all holding multiple instances of the same data, let's do
// something radical, scary and potentially downright dangerous, and clean things up a bit.
DWORD WindowStyle, ExWindowStyle;

void ClearAllStates (void);
void AppActivate (BOOL fActive, BOOL minimize);


void D3DVid_Restart_f (void);


void D3DVid_ResizeWindow (HWND hWnd)
{
	if (!d3d_Device) return;
	if (d3d_CurrentMode.RefreshRate != 0) return;

	// reset the device to resize the backbuffer
	RECT clientrect;
	GetClientRect (hWnd, &clientrect);

	// check the client rect dimensions to make sure it's valid
	if (clientrect.right - clientrect.left < 1) return;
	if (clientrect.bottom - clientrect.top < 1) return;

	d3d_CurrentMode.Width = clientrect.right - clientrect.left;
	d3d_CurrentMode.Height = clientrect.bottom - clientrect.top;

	d3d_PresentParams.BackBufferWidth = d3d_CurrentMode.Width;
	d3d_PresentParams.BackBufferHeight = d3d_CurrentMode.Height;

	vid.recalc_refdef = 1;
	IN_UpdateClipCursor ();
	D3DVid_Restart_f ();
}


void D3DVid_SetWindowStyles (D3DDISPLAYMODE *mode)
{
	if (mode->RefreshRate == 0)
	{
		// windowed mode
		WindowStyle = WS_TILEDWINDOW;//WS_OVERLAPPED | WS_BORDER | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
		ExWindowStyle = WS_EX_TOPMOST;
	}
	else
	{
		// fullscreen mode
		WindowStyle = WS_POPUP;
		ExWindowStyle = WS_EX_TOPMOST;
	}
}


void D3DVid_ResetWindow (D3DDISPLAYMODE *mode)
{
	D3DVid_SetWindowStyles (mode);

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
	}

	// update cursor clip region
	IN_UpdateClipCursor ();
}


DWORD D3DVid_GetPresentInterval (void)
{
	// per the documentation for D3DPRESENT these are always available
	if (vid_vsync.integer)
		return D3DPRESENT_INTERVAL_ONE;
	else return D3DPRESENT_INTERVAL_IMMEDIATE;
}


D3DFORMAT D3DVid_GetDepthStencilFormat (D3DDISPLAYMODE *mode)
{
	D3DFORMAT ModeFormat = mode->Format;

	// 24 bit needs to come first because a 16 bit depth buffer is just not good enough for to prevent precision trouble in places
	D3DFORMAT d3d_AllowedDepthFormats[] = {D3DFMT_D24S8, D3DFMT_D24X8, D3DFMT_D16, D3DFMT_UNKNOWN};

	if (ModeFormat == D3DFMT_UNKNOWN) ModeFormat = d3d_DesktopMode.Format;

	for (int i = 0;; i++)
	{
		// ran out of formats
		if (d3d_AllowedDepthFormats[i] == D3DFMT_UNKNOWN) break;

		// check that the format exists
		hr = d3d_Object->CheckDeviceFormat
			 (
				 D3DADAPTER_DEFAULT,
				 D3DDEVTYPE_HAL,
				 ModeFormat,
				 D3DUSAGE_DEPTHSTENCIL,
				 D3DRTYPE_SURFACE,
				 d3d_AllowedDepthFormats[i]
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
				 d3d_AllowedDepthFormats[i]
			 );

		// format is not compatible
		if (FAILED (hr)) continue;

		// format is good to use now
		return d3d_AllowedDepthFormats[i];
	}

	// didn't find one
	Sys_Error ("D3DVid_GetDepthStencilFormat: Failed to find a valid DepthStencil format");

	// shut up compiler
	return D3DFMT_UNKNOWN;
}


void D3DVid_SetPresentParams (D3DPRESENT_PARAMETERS *pp, D3DDISPLAYMODE *mode)
{
	memset (pp, 0, sizeof (D3DPRESENT_PARAMETERS));

	if (mode->RefreshRate == 0)
	{
		pp->Windowed = TRUE;
		pp->BackBufferFormat = D3DFMT_UNKNOWN;
		pp->FullScreen_RefreshRateInHz = 0;
	}
	else
	{
		pp->Windowed = FALSE;
		pp->BackBufferFormat = mode->Format;
		pp->FullScreen_RefreshRateInHz = d3d_DesktopMode.RefreshRate;
	}

	// create it without a depth buffer to begin with
	d3d_GlobalCaps.DepthStencilFormat = D3DVid_GetDepthStencilFormat (mode);
	pp->AutoDepthStencilFormat = d3d_GlobalCaps.DepthStencilFormat;
	pp->EnableAutoDepthStencil = TRUE;
	pp->Flags = 0;

	pp->SwapEffect = D3DSWAPEFFECT_DISCARD;
	pp->PresentationInterval = D3DVid_GetPresentInterval ();
	pp->BackBufferCount = 1;
	pp->BackBufferWidth = mode->Width;
	pp->BackBufferHeight = mode->Height;
	pp->hDeviceWindow = d3d_Window;
	pp->MultiSampleQuality = 0;
	pp->MultiSampleType = D3DMULTISAMPLE_NONE;
}


void D3DVid_ModeDescription (d3d_ModeDesc_t *mode)
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


void D3DVid_DescribeModes_f (void)
{
	for (int i = 0; i < d3d_NumModes; i++)
	{
		d3d_ModeDesc_t *mode = d3d_ModeList + i;
		D3DVid_ModeDescription (mode);
	}
}


void D3DVid_NumModes_f (void)
{
	if (d3d_NumModes == 1)
		Con_Printf ("%d video mode is available\n", d3d_NumModes);
	else Con_Printf ("%d video modes are available\n", d3d_NumModes);
}


bool D3DVid_ModeIsCurrent (d3d_ModeDesc_t *mode)
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


void D3DVid_SetVidMode (void)
{
	// puts a correct number into the d3d_mode cvar
	for (int i = 0; i < d3d_NumModes; i++)
	{
		if (D3DVid_ModeIsCurrent (&d3d_ModeList[i]))
		{
			// set the correct value
			Cvar_Set (&d3d_mode, d3d_ModeList[i].ModeNum);
			return;
		}
	}
}


void D3DVid_DescribeCurrentMode_f (void)
{
	for (int i = 0; i < d3d_NumModes; i++)
	{
		d3d_ModeDesc_t *mode = d3d_ModeList + i;

		if (!D3DVid_ModeIsCurrent (mode)) continue;

		D3DVid_ModeDescription (mode);
		return;
	}
}


void D3DVid_DescribeMode_f (void)
{
	int modenum = atoi (Cmd_Argv (1));

	for (int i = 0; i < d3d_NumModes; i++)
	{
		d3d_ModeDesc_t *mode = d3d_ModeList + i;

		if (mode->ModeNum != modenum) continue;

		D3DVid_ModeDescription (mode);
		return;
	}

	Con_Printf ("Unknown video mode: %i\n", modenum);
}


#define MAX_HANDLERS	256
CD3DDeviceLossHandler *d3d_DeviceLossHandlers[MAX_HANDLERS];
int numhandlers = 0;


CD3DDeviceLossHandler::CD3DDeviceLossHandler (xcommand_t onloss, xcommand_t onrecover)
{
	this->OnLoseDevice = onloss;
	this->OnRecoverDevice = onrecover;

	if (numhandlers == MAX_HANDLERS)
		Sys_Error ("CD3DDeviceLossHandler::CD3DDeviceLossHandler - Too many handlers!");
	else
	{
		d3d_DeviceLossHandlers[numhandlers] = this;
		numhandlers++;
	}
}


void D3DVid_RecoverDeviceResources (void)
{
	// recreate anything that needs to be recreated
	for (int i = 0; i < MAX_HANDLERS; i++)
	{
		if (!d3d_DeviceLossHandlers[i]) continue;
		if (!d3d_DeviceLossHandlers[i]->OnRecoverDevice) continue;

		d3d_DeviceLossHandlers[i]->OnRecoverDevice ();
	}

	// recover all states back to what they should be
	D3D_SetAllStates ();

	// force a recalc of the refdef
	vid.recalc_refdef = true;
}


void D3DVid_LoseDeviceResources (void)
{
	// release anything that needs to be released
	for (int i = 0; i < MAX_HANDLERS; i++)
	{
		if (!d3d_DeviceLossHandlers[i]) continue;
		if (!d3d_DeviceLossHandlers[i]->OnLoseDevice) continue;

		d3d_DeviceLossHandlers[i]->OnLoseDevice ();
	}

	// ensure that present params are valid
	D3DVid_SetPresentParams (&d3d_PresentParams, &d3d_CurrentMode);
}


void Host_GetConsoleCommands (void);

bool vid_restarted = false;

void D3DVid_Restart_f (void)
{
	// if we're attempting to change a fullscreen mode we need to validate it first
	if (d3d_CurrentMode.Format != D3DFMT_UNKNOWN && d3d_CurrentMode.RefreshRate != 0)
	{
		DEVMODE dm;

		memset (&dm, 0, sizeof (DEVMODE));
		dm.dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY;

		switch (d3d_CurrentMode.Format)
		{
		case D3DFMT_R5G6B5:
		case D3DFMT_X1R5G5B5:
		case D3DFMT_A1R5G5B5:
			dm.dmBitsPerPel = 16;
			break;

		default:
			dm.dmBitsPerPel = 32;
			break;
		}

		dm.dmDisplayFrequency = d3d_CurrentMode.RefreshRate;
		dm.dmPelsWidth = d3d_CurrentMode.Width;
		dm.dmPelsHeight = d3d_CurrentMode.Height;

		dm.dmSize = sizeof (DEVMODE);

		// attempt to change to it
		if (ChangeDisplaySettings (&dm, CDS_TEST | CDS_FULLSCREEN) != DISP_CHANGE_SUCCESSFUL)
		{
			// to do - restore the old mode, cvars, etc instead
			Sys_Error ("Could not change to selected display mode");
			return;
		}
	}

	// to do  - validate vsync and depth buffer format for the device

	// make sure that we're ready to reset
	while (true)
	{
		Sys_SendKeyEvents ();
		Sleep (10);
		hr = d3d_Device->TestCooperativeLevel ();

		if (hr == D3D_OK) break;
	}

	if (key_dest == key_menu)
	{
		// wipe the screen before resetting the device
		d3d_Device->Clear (0, NULL, D3DCLEAR_TARGET, 0x00000000, 1.0f, 1);
		d3d_Device->Present (NULL, NULL, NULL, NULL);
		HUD_Changed ();
	}

	// release anything that needs to be released
	D3DVid_LoseDeviceResources ();

	// reset the device
	hr = d3d_Device->Reset (&d3d_PresentParams);

	// if we're going to a fullscreen mode we need to handle the mouse properly
	IN_SetMouseState (!d3d_PresentParams.Windowed);

	if (FAILED (hr))
	{
		// a failed reset causes hassle
		Sys_Error ("D3DVid_Restart_f: Unable to Reset Device");
		return;
	}

	// make sure that the reset has completed
	while (true)
	{
		Sys_SendKeyEvents ();
		Sleep (10);
		hr = d3d_Device->TestCooperativeLevel ();

		if (hr == D3D_OK) break;
	}

	if (key_dest == key_menu)
	{
		// wipe the screen again post-reset
		d3d_Device->Clear (0, NULL, D3DCLEAR_TARGET, 0x00000000, 1.0f, 1);
		d3d_Device->Present (NULL, NULL, NULL, NULL);
		HUD_Changed ();
	}

	// bring back anything that needs to be brought back
	D3DVid_RecoverDeviceResources ();

	// flag to skip this frame so that we update more robustly
	vid_restarted = true;

	Cbuf_InsertText ("\n");
	Cbuf_Execute ();
}


D3DFORMAT D3DVid_FindBPP (int bpp)
{
	// any matching bpp
	D3DFORMAT AnyBPP = D3DFMT_UNKNOWN;

	// attempt to find an exact match BPP in the mode list
	for (int i = 0; i < d3d_NumModes; i++)
	{
		d3d_ModeDesc_t *mode = d3d_ModeList + i;

		// bpp match
		if (mode->BPP == bpp) return mode->d3d_Mode.Format;

		// store any valid bpp
		AnyBPP = mode->d3d_Mode.Format;
	}

	// return any bpp we got
	if (AnyBPP != D3DFMT_UNKNOWN) return AnyBPP;

	Sys_Error ("D3DVid_FindBPP: Failed to find matching BPP");

	// shut up compiler
	return D3DFMT_UNKNOWN;
}


int RRCompFunc (const void *a, const void *b)
{
	int ia = ((int *) a)[0];
	int ib = ((int *) b)[0];

	return ia - ib;
}


void Menu_VideoDecodeVideoModes (d3d_ModeDesc_t *modes, int totalnummodes, int numwindowed);

bool ExistingMode (D3DDISPLAYMODE *mode)
{
	for (int i = 0; i < d3d_NumModes; i++)
	{
		if (d3d_ModeList[i].d3d_Mode.Format != mode->Format) continue;
		if (d3d_ModeList[i].d3d_Mode.Height != mode->Height) continue;
		if (d3d_ModeList[i].d3d_Mode.RefreshRate != mode->RefreshRate) continue;
		if (d3d_ModeList[i].d3d_Mode.Width != mode->Width) continue;

		// mode is already present
		return true;
	}

	// mode is not 
	return false;
}


void D3DVid_EnumerateVideoModes (void)
{
	// get the desktop mode for reference
	d3d_Object->GetAdapterDisplayMode (D3DADAPTER_DEFAULT, &d3d_DesktopMode);

	// get the size of the desktop working area.  this is used instead of the desktop resolution for
	// determining if a mode is valid for windowed operation, as the desktop size gives 768 as a
	// valid height in an 800 high display, meaning that the taskbar will hide parts of the DirectQ window.
	RECT WorkArea;

	SystemParametersInfo (SPI_GETWORKAREA, 0, &WorkArea, 0);

	int MaxWindowWidth = WorkArea.right - WorkArea.left;
	int MaxWindowHeight = WorkArea.bottom - WorkArea.top;

	// get a valid pointer for the first mode in the list
	d3d_ModeList = (d3d_ModeDesc_t *) MainHunk->Alloc (0);

	// enumerate the modes in the adapter
	for (int m = 0;; m++)
	{
		// end of the list
		if (d3d_AdapterModeDescs[m] == D3DFMT_UNKNOWN) break;

		// see can we support a fullscreen format with this mode format
		hr = d3d_Object->CheckDeviceType (D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, d3d_AdapterModeDescs[m], d3d_AdapterModeDescs[m], FALSE);

		if (FAILED (hr)) continue;

		// see can we create a standard texture on it
		hr = d3d_Object->CheckDeviceFormat (D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, d3d_AdapterModeDescs[m], 0, D3DRTYPE_TEXTURE, D3DFMT_X8R8G8B8);

		if (FAILED (hr)) continue;

		// see can we create an alpha texture on it
		hr = d3d_Object->CheckDeviceFormat (D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, d3d_AdapterModeDescs[m], 0, D3DRTYPE_TEXTURE, D3DFMT_A8R8G8B8);

		if (FAILED (hr)) continue;

		// get the count of modes for this format
		int ModeCount = d3d_Object->GetAdapterModeCount (D3DADAPTER_DEFAULT, d3d_AdapterModeDescs[m]);

		// no modes available for this format
		if (!ModeCount) continue;

		// enumerate them all
		for (int i = 0; i < ModeCount; i++)
		{
			D3DDISPLAYMODE mode;

			// get the mode description
			d3d_Object->EnumAdapterModes (D3DADAPTER_DEFAULT, d3d_AdapterModeDescs[m], i, &mode);

			DEVMODE dm;

			memset (&dm, 0, sizeof (DEVMODE));
			dm.dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY;

			switch (mode.Format)
			{
			case D3DFMT_R5G6B5:
			case D3DFMT_X1R5G5B5:
			case D3DFMT_A1R5G5B5:
				dm.dmBitsPerPel = 16;
				break;

			default:
				dm.dmBitsPerPel = 32;
				break;
			}

			dm.dmDisplayFrequency = mode.RefreshRate;
			dm.dmPelsWidth = mode.Width;
			dm.dmPelsHeight = mode.Height;

			dm.dmSize = sizeof (DEVMODE);

			// attempt to change to it
			// (fixme - this still breaks on vmwares svga driver which allows huge virtual resolutions)
			if (ChangeDisplaySettings (&dm, CDS_TEST | CDS_FULLSCREEN) != DISP_CHANGE_SUCCESSFUL) continue;

			// we're only interested in modes that match the desktop refresh rate
			if (mode.RefreshRate != d3d_DesktopMode.RefreshRate) continue;

			// don't allow modes < 640 x 480
			if (mode.Width < 640) continue;
			if (mode.Height < 480) continue;

			// if the mode width is < height we assume that we have a monitor capable of rotating it's desktop
			// and therefore we skip the mode
			if (mode.Width < mode.Height) continue;

			// if the mode is already present don't record it
			if (ExistingMode (&mode)) continue;

			// ensure that we have space to store it
			MainHunk->Alloc (sizeof (d3d_ModeDesc_t));

			// and store it in our master mode list
			d3d_ModeDesc_t *newmode = &d3d_ModeList[d3d_NumModes];
			newmode->ModeNum = d3d_NumModes;
			d3d_NumModes++;

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
			strcpy (newmode->ModeDesc, D3DTypeToString (d3d_AdapterModeDescs[m]));

			// see is it valid for windowed
			newmode->AllowWindowed = true;

			// valid windowed modes must be the same format as the desktop and less than it's resolution
			if (newmode->d3d_Mode.Width >= MaxWindowWidth) newmode->AllowWindowed = false;
			if (newmode->d3d_Mode.Height >= MaxWindowHeight) newmode->AllowWindowed = false;
			if (newmode->d3d_Mode.Format != d3d_DesktopMode.Format) newmode->AllowWindowed = false;
			if (newmode->AllowWindowed) d3d_NumWindowedModes++; else continue;
			d3d_NumWindowedModes = d3d_NumWindowedModes;
		}
	}

	if (!d3d_NumModes)
	{
		Sys_Error ("D3DVid_EnumerateVideoModes: No RGB display modes available");
		return;
	}

	// we must emulate WinQuake by putting the windowed modes at the start of the list
	if (d3d_NumWindowedModes)
	{
		// allocare extra space for the windowed modes
		MainHunk->Alloc (d3d_NumWindowedModes * sizeof (d3d_ModeDesc_t));

		// move the fullscreen modes to the end of the list
		for (int i = 0; i < d3d_NumModes; i++)
		{
			d3d_ModeDesc_t *src = &d3d_ModeList[d3d_NumModes - (i + 1)];
			d3d_ModeDesc_t *dst = &d3d_ModeList[d3d_NumModes + d3d_NumWindowedModes - (i + 1)];

			memcpy (dst, src, sizeof (d3d_ModeDesc_t));
		}

		// reset pointers and stuff so that we can start copying in the windowed modes
		d3d_ModeDesc_t *fullmode = &d3d_ModeList[d3d_NumWindowedModes];
		d3d_ModeDesc_t *winmode = d3d_ModeList;

		// put windowed modes at the start of the list
		for (int i = 0; i < d3d_NumModes; i++, fullmode++)
		{
			// not a valid windowed mode
			if (!fullmode->AllowWindowed) continue;

			// copy it in and update it
			memcpy (winmode, fullmode, sizeof (d3d_ModeDesc_t));
			strcpy (winmode->ModeDesc, "Windowed");

			// ensure that it's recognized as a windowed mode
			winmode->d3d_Mode.Format = D3DFMT_UNKNOWN;
			winmode->d3d_Mode.RefreshRate = 0;
			winmode->BPP = 0;

			// this is no longer a windowed mode
			fullmode->AllowWindowed = false;

			// next windowed mode
			winmode++;
		}

		// add the windowed modes to the total mode count
		// (fixme: maintain separate pointers and counters for each too???)
		d3d_NumModes += d3d_NumWindowedModes;

		// fix up the mode numbers
		for (int i = 0; i < d3d_NumModes; i++)
			d3d_ModeList[i].ModeNum = i;
	}

	// finally copy from the mainhunk to the zone at the correct size
	d3d_ModeDesc_t *modes = (d3d_ModeDesc_t *) MainZone->Alloc (d3d_NumModes * sizeof (d3d_ModeDesc_t));
	memcpy (modes, d3d_ModeList, d3d_NumModes * sizeof (d3d_ModeDesc_t));
	d3d_ModeList = modes;
	MainHunk->Free ();

	Menu_VideoDecodeVideoModes (d3d_ModeList, d3d_NumModes, d3d_NumWindowedModes);

	// here we protect the user against changes i might make to the video driver startup.  in the past
	// this may have started up in an invalid mode, so we make sure that the value of viddriver_version
	// retrieved from the config is the same as what this version of directq expects. and fall back to
	// a known good safe mode if it's not.  the string stored in VIDDRIVER_VERSION can be changed each
	// time i make updates to this code so that this should never happen again.
	if (!strcmp (viddriver_version.string, VIDDRIVER_VERSION))
	{
		// check for mode already set and return if it was a valid one already in our list
		if (d3d_mode.integer >= 0 && d3d_mode.integer < d3d_NumModes)
		{
			d3d_ModeDesc_t *mode = d3d_ModeList + d3d_mode.integer;
			return;
		}
	}

	// find a mode to start in; we start directq in a windowed mode at either 640x480 or 800x600, whichever is
	// higher.  windowed modes are safer - they don't have exclusive ownership of your screen so if things go
	// wrong on first run you can get out easier.
	int windowedmode800 = -1;
	int windowedmode640 = -1;

	// now find a good default windowed mode - try to find either 800x600 or 640x480
	for (int i = 0; i < d3d_NumModes; i++)
	{
		d3d_ModeDesc_t *mode = d3d_ModeList + i;

		// not a windowed mode
		if (!mode->AllowWindowed) continue;

		if (mode->BPP > 0) continue;
		if (mode->d3d_Mode.RefreshRate > 0) continue;
		if (mode->d3d_Mode.Width == 800 && mode->d3d_Mode.Height == 600) windowedmode800 = mode->ModeNum;
		if (mode->d3d_Mode.Width == 640 && mode->d3d_Mode.Height == 480) windowedmode640 = mode->ModeNum;
	}

	// use the best windowed mode we could find of 800x600 or 640x480, or mode 0 if none found
	// (this will be a fullscreen mode at some crazy low resolution, so it probably won't work anyway...)
	if (windowedmode800 >= 0)
	{
		d3d_CurrentMode.Width = 800;
		d3d_CurrentMode.Height = 600;
		d3d_CurrentMode.Format = D3DFMT_UNKNOWN;
		d3d_CurrentMode.RefreshRate = 0;
	}
	else if (windowedmode640 >= 0)
	{
		d3d_CurrentMode.Width = 640;
		d3d_CurrentMode.Height = 480;
		d3d_CurrentMode.Format = D3DFMT_UNKNOWN;
		d3d_CurrentMode.RefreshRate = 0;
	}
	else
	{
		// to do...
	}
}


void D3DVid_FindModeForVidMode (D3DDISPLAYMODE *mode)
{
	// catch unspecified modes
	if (d3d_mode.value < 0) return;

	for (int i = 0; i < d3d_NumModes; i++)
	{
		d3d_ModeDesc_t *findmode = d3d_ModeList + i;

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


void D3DVid_FindBestWindowedMode (D3DDISPLAYMODE *mode)
{
	D3DDISPLAYMODE *best = NULL;

	for (int i = 0; i < d3d_NumModes; i++)
	{
		d3d_ModeDesc_t *winmode = d3d_ModeList + i;

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
		Sys_Error ("D3DVid_FindBestWindowedMode: Failed to find a Windowed mode");
		return;
	}

	// store best back to mode
	mode->Height = best->Height;
	mode->Width = best->Width;
	mode->RefreshRate = 0;
}


void D3DVid_FindBestFullscreenMode (D3DDISPLAYMODE *mode)
{
	D3DDISPLAYMODE *best = NULL;

	for (int i = 0; i < d3d_NumModes; i++)
	{
		d3d_ModeDesc_t *fsmode = d3d_ModeList + i;

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
		Sys_Error ("D3DVid_FindBestFullscreenMode: Failed to find a Fullscreen mode");
		return;
	}

	// store best back to mode
	mode->Height = best->Height;
	mode->Width = best->Width;
}


void D3DVid_InfoDump_f (void)
{
	Con_Printf ("Getting driver info... ");
	FILE *f = fopen ("driver.bin", "wb");

	if (f)
	{
		// dump out info about the card and the currently running state so that we can analyze it later
		D3DADAPTER_IDENTIFIER9 adinfo;
		d3d_Object->GetAdapterIdentifier (D3DADAPTER_DEFAULT, 0, &adinfo);
		fwrite (&adinfo, sizeof (D3DADAPTER_IDENTIFIER9), 1, f);

		D3DCAPS9 dcaps;
		d3d_Device->GetDeviceCaps (&dcaps);
		fwrite (&dcaps, sizeof (D3DCAPS9), 1, f);

		fwrite (&d3d_PresentParams, sizeof (D3DPRESENT_PARAMETERS), 1, f);

		Con_Printf ("Done\n");
		return;
	}

	Con_Printf ("Failed to get driver info\n");
}


cmd_t d3d_InfoDump_Cmd ("d3d_infodump", D3DVid_InfoDump_f);


void D3DVid_TexMem_f (void)
{
	Con_Printf ("Available Texture Memory: %i MB\n", (d3d_Device->GetAvailableTextureMem ()) / (1024 * 1024));
}


cmd_t D3DVid_TexMem_Cmd ("gl_videoram", D3DVid_TexMem_f);

void D3DVid_ValidateTextureSizes (void)
{
	LPDIRECT3DTEXTURE9 tex = NULL;

	for (int s = d3d_DeviceCaps.MaxTextureWidth;; s >>= 1)
	{
		if (s < 256)
		{
			Sys_Error ("Could not create a 256x256 texture");
			return;
		}

		hr = d3d_Device->CreateTexture (s, 256, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, NULL);

		if (FAILED (hr))
		{
			tex = NULL;
			continue;
		}

		d3d_DeviceCaps.MaxTextureWidth = s;
		SAFE_RELEASE (tex);
		break;
	}

	for (int s = d3d_DeviceCaps.MaxTextureHeight;; s >>= 1)
	{
		if (s < 256)
		{
			Sys_Error ("Could not create a 256x256 texture");
			return;
		}

		hr = d3d_Device->CreateTexture (256, s, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, NULL);

		if (FAILED (hr))
		{
			tex = NULL;
			continue;
		}

		d3d_DeviceCaps.MaxTextureHeight = s;
		SAFE_RELEASE (tex);
		break;
	}

	d3d_GlobalCaps.supportDynTex = false;

	if (d3d_DeviceCaps.Caps2 & D3DCAPS2_DYNAMICTEXTURES)
	{
		hr = d3d_Device->CreateTexture (256, 256, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &tex, NULL);

		if (SUCCEEDED (hr))
		{
			d3d_GlobalCaps.supportDynTex = true;
			SAFE_RELEASE (tex);
		}
	}
}


void D3DVid_InitDirect3D (D3DDISPLAYMODE *mode)
{
	// get the kind of capabilities we can expect from a HAL device ("hello Dave")
	hr = d3d_Object->GetDeviceCaps (D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, &d3d_DeviceCaps);

	if (FAILED (hr)) Sys_Error ("D3DVid_InitDirect3D: Failed to retrieve object caps\n(No HAL D3D Device Available)");

	// check for basic required capabilities ("your name's not down, you're not coming in")
	if (!(d3d_DeviceCaps.DevCaps & D3DDEVCAPS_DRAWPRIMITIVES2EX)) Sys_Error ("You need at least a DirectX 7-compliant device to run DirectQ");
	if (!(d3d_DeviceCaps.DevCaps & D3DDEVCAPS_HWRASTERIZATION)) Sys_Error ("You need a hardware-accelerated device to run DirectQ");
	if (!(d3d_DeviceCaps.TextureCaps & D3DPTEXTURECAPS_MIPMAP)) Sys_Error ("You need a device that supports mipmapping to run DirectQ");

	// check for required texture op caps
	if (!(d3d_DeviceCaps.TextureOpCaps & D3DTEXOPCAPS_ADD)) Sys_Error ("You need a device that supports D3DTEXOPCAPS_ADD to run DirectQ");
	if (!(d3d_DeviceCaps.TextureOpCaps & D3DTEXOPCAPS_DISABLE)) Sys_Error ("You need a device that supports D3DTEXOPCAPS_DISABLE to run DirectQ");
	if (!(d3d_DeviceCaps.TextureOpCaps & D3DTEXOPCAPS_MODULATE)) Sys_Error ("You need a device that supports D3DTEXOPCAPS_MODULATE to run DirectQ");
	if (!(d3d_DeviceCaps.TextureOpCaps & D3DTEXOPCAPS_MODULATE2X)) Sys_Error ("You need a device that supports D3DTEXOPCAPS_MODULATE2X to run DirectQ");
	if (!(d3d_DeviceCaps.TextureOpCaps & D3DTEXOPCAPS_SELECTARG1)) Sys_Error ("You need a device that supports D3DTEXOPCAPS_SELECTARG1 to run DirectQ");
	if (!(d3d_DeviceCaps.TextureOpCaps & D3DTEXOPCAPS_SELECTARG2)) Sys_Error ("You need a device that supports D3DTEXOPCAPS_SELECTARG2 to run DirectQ");
	if (!(d3d_DeviceCaps.TextureOpCaps & D3DTEXOPCAPS_BLENDTEXTUREALPHA)) Sys_Error ("You need a device that supports D3DTEXOPCAPS_BLENDTEXTUREALPHA to run DirectQ");

	// check for texture addressing modes
	if (!(d3d_DeviceCaps.TextureAddressCaps & D3DPTADDRESSCAPS_CLAMP)) Sys_Error ("You need a device that supports D3DPTADDRESSCAPS_CLAMP to run DirectQ");
	if (!(d3d_DeviceCaps.TextureAddressCaps & D3DPTADDRESSCAPS_WRAP)) Sys_Error ("You need a device that supports D3DPTADDRESSCAPS_WRAP to run DirectQ");

	// check for TMU support - Anything less than 3 TMUs is not d3d9 hardware
	if (d3d_DeviceCaps.MaxTextureBlendStages < 3) Sys_Error ("You need a device with at least 3 TMUs to run DirectQ");
	if (d3d_DeviceCaps.MaxSimultaneousTextures < 3) Sys_Error ("You need a device with at least 3 TMUs to run DirectQ");
	if (d3d_DeviceCaps.MaxStreams < 3) Sys_Error ("You need a device with at least 3 Vertex Streams to run DirectQ");

	// check for z buffer support
	if (!(d3d_DeviceCaps.ZCmpCaps & D3DPCMPCAPS_ALWAYS)) Sys_Error ("You need a device that supports a proper Z buffer to run DirectQ");
	if (!(d3d_DeviceCaps.ZCmpCaps & D3DPCMPCAPS_EQUAL)) Sys_Error ("You need a device that supports a proper Z buffer to run DirectQ");
	if (!(d3d_DeviceCaps.ZCmpCaps & D3DPCMPCAPS_GREATER)) Sys_Error ("You need a device that supports a proper Z buffer to run DirectQ");
	if (!(d3d_DeviceCaps.ZCmpCaps & D3DPCMPCAPS_GREATEREQUAL)) Sys_Error ("You need a device that supports a proper Z buffer to run DirectQ");
	if (!(d3d_DeviceCaps.ZCmpCaps & D3DPCMPCAPS_LESS)) Sys_Error ("You need a device that supports a proper Z buffer to run DirectQ");
	if (!(d3d_DeviceCaps.ZCmpCaps & D3DPCMPCAPS_LESSEQUAL)) Sys_Error ("You need a device that supports a proper Z buffer to run DirectQ");
	if (!(d3d_DeviceCaps.ZCmpCaps & D3DPCMPCAPS_NEVER)) Sys_Error ("You need a device that supports a proper Z buffer to run DirectQ");
	if (!(d3d_DeviceCaps.ZCmpCaps & D3DPCMPCAPS_NOTEQUAL)) Sys_Error ("You need a device that supports a proper Z buffer to run DirectQ");

	// check for hardware T&L support
	if ((d3d_DeviceCaps.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT) && (d3d_DeviceCaps.DevCaps & D3DDEVCAPS_PUREDEVICE))
	{
		d3d_GlobalCaps.supportHardwareTandL = true;
		d3d_GlobalCaps.deviceCreateFlags = D3DCREATE_HARDWARE_VERTEXPROCESSING;
	}
	else
	{
		d3d_GlobalCaps.supportHardwareTandL = false;
		d3d_GlobalCaps.deviceCreateFlags = D3DCREATE_SOFTWARE_VERTEXPROCESSING;
	}

	// now reset the present params as they will have become messed up above
	D3DVid_SetPresentParams (&d3d_PresentParams, mode);

	// attempt to create the device - we can ditch all of the extra flags now :)
	// (Quark ETP needs D3DCREATE_FPU_PRESERVE - using _controlfp during texcoord gen doesn't work)
	// here as the generated coords will also lose precision when being applied
	hr = d3d_Object->CreateDevice
	(
		D3DADAPTER_DEFAULT,
		D3DDEVTYPE_HAL,
		d3d_Window,
		d3d_GlobalCaps.deviceCreateFlags | D3DCREATE_FPU_PRESERVE,
		&d3d_PresentParams,
		&d3d_Device
	);

	if (FAILED (hr))
	{
		Sys_Error ("D3DVid_InitDirect3D: IDirect3D9::CreateDevice failed");
		return;
	}

	if (d3d_GlobalCaps.supportHardwareTandL)
		Con_Printf ("Using Hardware Vertex Processing\n\n");
	else Con_Printf ("Using Software Vertex Processing\n\n");

	// report some caps
	Con_Printf ("Video mode %i (%ix%i) Initialized\n", d3d_mode.integer, mode->Width, mode->Height);
	Con_Printf ("Back Buffer Format: %s (created %i %s)\n", D3DTypeToString (mode->Format), d3d_PresentParams.BackBufferCount, d3d_PresentParams.BackBufferCount > 1 ? "backbuffers" : "backbuffer");
	Con_Printf ("Refresh Rate: %i Hz (%s)\n", mode->RefreshRate, mode->RefreshRate ? "Fullscreen" : "Windowed");
	Con_Printf ("\n");

	// clear to black immediately
	d3d_Device->Clear (0, NULL, D3DCLEAR_TARGET, 0x00000000, 1.0f, 1);
	d3d_Device->Present (NULL, NULL, NULL, NULL);
	HUD_Changed ();

	// get capabilities on the actual device
	hr = d3d_Device->GetDeviceCaps (&d3d_DeviceCaps);

	if (FAILED (hr))
	{
		Sys_Error ("D3DVid_InitDirect3D: Failed to retrieve device caps");
		return;
	}

	// ensure that the reported texture sizes are correct
	D3DVid_ValidateTextureSizes ();

	// report on selected ones
	Con_Printf ("Maximum Texture Blend Stages: %i\n", d3d_DeviceCaps.MaxTextureBlendStages);
	Con_Printf ("Maximum Simultaneous Textures: %i\n", d3d_DeviceCaps.MaxSimultaneousTextures);
	Con_Printf ("Maximum Texture Size: %i x %i\n", d3d_DeviceCaps.MaxTextureWidth, d3d_DeviceCaps.MaxTextureHeight);
	Con_Printf ("Maximum Anisotropic Filter: %i\n", d3d_DeviceCaps.MaxAnisotropy);
	Con_Printf ("\n");

	// no np2 support by default
	d3d_GlobalCaps.supportNonPow2 = false;

	// get the shader model because we don't want to support NP2 on SM2 hardware (both ATI and NVIDIA have problems with this in OpenGL, and
	// while D3D does have stricter hardware capabilities checking it's not beyond the bounds of possibility that the driver could lie
	// and/or not throw an error until the texture is used).
	int vsvermaj = D3DSHADER_VERSION_MAJOR (d3d_DeviceCaps.VertexShaderVersion);
	int psvermaj = D3DSHADER_VERSION_MAJOR (d3d_DeviceCaps.PixelShaderVersion);

	if (!(d3d_DeviceCaps.TextureCaps & D3DPTEXTURECAPS_POW2) && 
		!(d3d_DeviceCaps.TextureCaps & D3DPTEXTURECAPS_NONPOW2CONDITIONAL) && 
		(vsvermaj >= 3 && psvermaj >= 3))
	{
		// validate that the reported support actually does exist by creating a NP2 texture with a full mipchain
		LPDIRECT3DTEXTURE9 tex = NULL;

		hr = d3d_Device->CreateTexture (160, 192, 0, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &tex, NULL);

		if (SUCCEEDED (hr) && tex)
		{
			hr = tex->AddDirtyRect (NULL);

			if (SUCCEEDED (hr))
			{
				// ensure that all miplevels get updated correctly
				hr = D3DXFilterTexture (tex, NULL, 0, D3DX_FILTER_BOX);

				if (SUCCEEDED (hr))
				{
					Con_Printf ("Allowing non-power-of-2 textures\n");
					d3d_GlobalCaps.supportNonPow2 = true;
				}
			}

			tex->Release ();
			tex = NULL;
		}
	}

	// tmus; take the lower of what's available as we need to work with them all
	d3d_GlobalCaps.NumTMUs = 666;

	if (d3d_DeviceCaps.MaxTextureBlendStages < d3d_GlobalCaps.NumTMUs) d3d_GlobalCaps.NumTMUs = d3d_DeviceCaps.MaxTextureBlendStages;
	if (d3d_DeviceCaps.MaxSimultaneousTextures < d3d_GlobalCaps.NumTMUs) d3d_GlobalCaps.NumTMUs = d3d_DeviceCaps.MaxSimultaneousTextures;

	// check for availability of rgb texture formats (ARGB is non-negotiable required by Quake)
	d3d_GlobalCaps.supportARGB = D3D_CheckTextureFormat (D3DFMT_A8R8G8B8, TRUE);
	d3d_GlobalCaps.supportXRGB = D3D_CheckTextureFormat (D3DFMT_X8R8G8B8, FALSE);

	// check for availability of alpha/luminance formats
	d3d_GlobalCaps.supportL8 = D3D_CheckTextureFormat (D3DFMT_L8, TRUE);
	d3d_GlobalCaps.supportA8L8 = D3D_CheckTextureFormat (D3DFMT_A8L8, FALSE);

	// check for availability of compressed texture formats
	d3d_GlobalCaps.supportDXT1 = D3D_CheckTextureFormat (D3DFMT_DXT1, FALSE);
	d3d_GlobalCaps.supportDXT3 = D3D_CheckTextureFormat (D3DFMT_DXT3, FALSE);
	d3d_GlobalCaps.supportDXT5 = D3D_CheckTextureFormat (D3DFMT_DXT5, FALSE);

	d3d_GlobalCaps.supportInstancing = false;

	// basic requirements for instancing
	if (vsvermaj >= 3 && psvermaj >= 3 && (d3d_DeviceCaps.DevCaps2 & D3DDEVCAPS2_STREAMOFFSET))
	{
		hr = d3d_Device->SetStreamSourceFreq (0, D3DSTREAMSOURCE_INDEXEDDATA | 25);

		if (SUCCEEDED (hr))
		{
			hr = d3d_Device->SetStreamSourceFreq (1, D3DSTREAMSOURCE_INSTANCEDATA | 1);

			if (SUCCEEDED (hr))
			{
				Con_Printf ("Allowing geometry instancing\n");
				d3d_GlobalCaps.supportInstancing = true;
			}
		}

		// revert back to non-instanced geometry
		d3d_Device->SetStreamSourceFreq (0, 1);
		d3d_Device->SetStreamSourceFreq (1, 1);
	}

	// set up everything else
	// (fixme - run through the on-recover code for the loss handlers here instead)
	D3DHLSL_Init ();
	D3DRTT_CreateRTTTexture ();

	Con_Printf ("\n");

	// set the d3d_mode cvar correctly
	D3DVid_SetVidMode ();

	// build the rest of the video menu (deferred to here as it's dependent on video being up)
	Menu_VideoBuild ();

	// set initial state caches
	D3D_SetAllStates ();
}


void D3DVid_CreateWindow (D3DDISPLAYMODE *mode)
{
	D3DVid_SetWindowStyles (mode);

	// the window has already been created so all we need is to update it's properties
	RECT rect;

	rect.top = rect.left = 0;
	rect.right = mode->Width;
	rect.bottom = mode->Height;

	AdjustWindowRectEx (&rect, WindowStyle, FALSE, 0);

	int width = rect.right - rect.left;
	int height = rect.bottom - rect.top;

	// switch size and position (also hide the window here)
	SetWindowPos (d3d_Window, NULL, rect.left, rect.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS | SWP_FRAMECHANGED | SWP_HIDEWINDOW);
	SetWindowPos (d3d_Window, NULL, 0, 0, width, height, SWP_NOMOVE | SWP_NOZORDER | SWP_NOCOPYBITS | SWP_FRAMECHANGED | SWP_HIDEWINDOW);

	// switch window properties
	SetWindowLong (d3d_Window, GWL_EXSTYLE, ExWindowStyle);
	SetWindowLong (d3d_Window, GWL_STYLE, WindowStyle);
	SetWindowLong (d3d_Window, GWL_WNDPROC, (LONG) MainWndProc);

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
	}

	ShowWindow (d3d_Window, SW_SHOWDEFAULT);
	UpdateWindow (d3d_Window);

	HICON hIcon = LoadIcon (GetModuleHandle (NULL), MAKEINTRESOURCE (IDI_APPICON));
	SendMessage (d3d_Window, WM_SETICON, (WPARAM) TRUE, (LPARAM) hIcon);
	SendMessage (d3d_Window, WM_SETICON, (WPARAM) FALSE, (LPARAM) hIcon);

	// set internal active flags otherwise we'll get 40 FPS and no mouse!!!
	AppActivate (TRUE, FALSE);

	// set cursor clip region
	IN_UpdateClipCursor ();
}


void D3DVid_SetVideoMode (D3DDISPLAYMODE *mode)
{
	// suspend stuff that could mess us up while creating the window
	bool temp = scr_disabled_for_loading;
	scr_disabled_for_loading = true;
	CDAudio_Pause ();

	// if neither width nor height were specified we take it from the d3d_mode cvar
	// this then passes through to the find best... functions so that the rest of the mode is filled in
	if (mode->Width == 0 && mode->Height == 0) D3DVid_FindModeForVidMode (mode);

	// even if a mode is found we pass through here
	if (mode->RefreshRate == 0)
		D3DVid_FindBestWindowedMode (mode);
	else D3DVid_FindBestFullscreenMode (mode);

	// retrieve and store the gamma ramp for the desktop
	HDC hdc = GetDC (NULL);
	GetDeviceGammaRamp (hdc, &d3d_DefaultGamma);
	ReleaseDC (NULL, hdc);

	// create the mode and activate input
	D3DVid_CreateWindow (mode);
	IN_SetMouseState (mode->RefreshRate != 0);

	// now initialize direct 3d on the window
	D3DVid_InitDirect3D (mode);

	// now resume the messy-uppy stuff
	CDAudio_Resume ();
	scr_disabled_for_loading = temp;

	// now we force the window to the top of the z-order and the foreground
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


void PaletteFromColormap (byte *pal, byte *map)
{
	// set fullbright colour indexes
	for (int x = 0; x < 256; x++)
	{
		if (x < 224)
			vid.fullbright[x] = false;
		else if (x < 255)
			vid.fullbright[x] = true;
		else vid.fullbright[x] = false;
	}

	// for avoidance of the pink fringe effect
	pal[765] = pal[766] = pal[767] = 0;
}


cmd_t D3DVid_DescribeModes_f_Cmd ("vid_describemodes", D3DVid_DescribeModes_f);
cmd_t D3DVid_NumModes_f_Cmd ("vid_nummodes", D3DVid_NumModes_f);
cmd_t D3DVid_DescribeCurrentMode_f_Cmd ("vid_describecurrentmode", D3DVid_DescribeCurrentMode_f);
cmd_t D3DVid_DescribeMode_f_Cmd ("vid_describemode", D3DVid_DescribeMode_f);
cmd_t D3DVid_Restart_f_Cmd ("vid_restart", D3DVid_Restart_f);

void D3DVid_Init (void)
{
	// ensure
	memset (&d3d_RenderDef, 0, sizeof (d3d_renderdef_t));

	vid_initialized = true;
	vid_canalttab = true;

	// dump the colormap out to file so that we can have a look-see at what's in it
	// SCR_WriteDataToTGA ("colormap.tga", vid.colormap, 256, 64, 8, 24);

	// this is always 32 irrespective of which version of the sdk we use
	if (!(d3d_Object = Direct3DCreate9 (D3D_SDK_VERSION)))
	{
		Sys_Error ("D3DVid_InitDirect3D - failed to initialize Direct3D!");
		return;
	}

	// enumerate available modes
	D3DVid_EnumerateVideoModes ();

	d3d_Object->GetAdapterIdentifier (D3DADAPTER_DEFAULT, 0, &d3d_Adapter);

	Con_Printf ("\nInitialized Direct 3D on %s\n", d3d_Adapter.DeviceName);
	Con_Printf ("%s\nDriver: %s\n", d3d_Adapter.Description, d3d_Adapter.Driver);

	// print extended info
	Con_Printf
	(
		"Vendor %x Device %x SubSys %x Revision %x\n",
		d3d_Adapter.VendorId,
		d3d_Adapter.DeviceId,
		d3d_Adapter.SubSysId,
		d3d_Adapter.Revision
	);

	Con_Printf ("\n");

	// check command-line args for resolution specifiers
	if (COM_CheckParm ("-window"))
	{
		// ensure that we're in an RGB mode if running windowed!
		HDC hdc = GetDC (NULL);

		if (GetDeviceCaps (hdc, RASTERCAPS) & RC_PALETTE)
		{
			Sys_Error ("D3DVid_Init: Can't run Windowed Mode in non-RGB Display Mode");
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
				d3d_CurrentMode.Format = D3DVid_FindBPP (32);
			else d3d_CurrentMode.Format = D3DVid_FindBPP (16);
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

	// set the selected video mode
	D3DVid_SetVideoMode (&d3d_CurrentMode);
}


void Menu_PrintCenterWhite (int cy, char *str);

void D3DVid_ShutdownDirect3D (void)
{
	// clear the screen to black so that shutdown doesn't leave artefacts from the last SCR_UpdateScreen
	if (d3d_Device)
	{
		d3d_Device->Clear (0, NULL, D3DCLEAR_TARGET, 0x00000000, 1.0f, 1);
		d3d_Device->Present (NULL, NULL, NULL, NULL);
		HUD_Changed ();
	}

	// also need these... ;)
	D3DVid_LoseDeviceResources ();

	// release anything that needs to be released
	D3D_ReleaseTextures ();

	// destroy the device and object
	SAFE_RELEASE (d3d_Device);
	SAFE_RELEASE (d3d_Object);
}


void VID_DefaultMonitorGamma_f (void)
{
	// restore ramps to linear in case something fucks up
	for (int i = 0; i < 256; i++)
	{
		// this is correct in terms of the default linear GDI gamma
		d3d_DefaultGamma.r[i] = d3d_CurrentGamma.r[i] = i << 8;
		d3d_DefaultGamma.g[i] = d3d_CurrentGamma.g[i] = i << 8;
		d3d_DefaultGamma.b[i] = d3d_CurrentGamma.b[i] = i << 8;
	}

	VID_SetOSGamma ();
}

cmd_t VID_DefaultMonitorGamma_Cmd ("vid_defaultmonitorgamma", VID_DefaultMonitorGamma_f);

void VID_Shutdown (void)
{
	// always restore gamma correctly
	VID_SetOSGamma ();

	if (vid_initialized)
	{
		vid_canalttab = false;

		D3DVid_ShutdownDirect3D ();

		AppActivate (false, false);
	}
}


typedef struct d3d_filtermode_s
{
	char *name;
	D3DTEXTUREFILTERTYPE texfilter;
	D3DTEXTUREFILTERTYPE mipfilter;
} d3d_filtermode_t;

d3d_filtermode_t d3d_filtermodes[] =
{
	{"GL_NEAREST", D3DTEXF_POINT, D3DTEXF_NONE},
	{"GL_LINEAR", D3DTEXF_LINEAR, D3DTEXF_NONE},
	{"GL_NEAREST_MIPMAP_NEAREST", D3DTEXF_POINT, D3DTEXF_POINT},
	{"GL_LINEAR_MIPMAP_NEAREST", D3DTEXF_LINEAR, D3DTEXF_POINT},
	{"GL_NEAREST_MIPMAP_LINEAR", D3DTEXF_POINT, D3DTEXF_LINEAR},
	{"GL_LINEAR_MIPMAP_LINEAR", D3DTEXF_LINEAR, D3DTEXF_LINEAR}
};

D3DTEXTUREFILTERTYPE d3d_TexFilter = D3DTEXF_LINEAR;
D3DTEXTUREFILTERTYPE d3d_MipFilter = D3DTEXF_LINEAR;

void D3DVid_TextureMode_f (void)
{
	if (Cmd_Argc () == 1)
	{
		D3DTEXTUREFILTERTYPE texfilter = d3d_TexFilter;
		D3DTEXTUREFILTERTYPE mipfilter = d3d_MipFilter;

		Con_Printf ("Available Filters:\n");

		for (int i = 0; i < 6; i++)
			Con_Printf ("%i: %s\n", i, d3d_filtermodes[i].name);

		for (int i = 0; i < 6; i++)
		{
			if (texfilter == d3d_filtermodes[i].texfilter && mipfilter == d3d_filtermodes[i].mipfilter)
			{
				Con_Printf ("\nCurrent filter: %s\n", d3d_filtermodes[i].name);
				return;
			}
		}

		Con_Printf ("current filter is unknown???\n");
		Con_Printf ("Texture filter: %s\n", D3DTypeToString (d3d_TexFilter));
		Con_Printf ("Mipmap filter:  %s\n", D3DTypeToString (d3d_MipFilter));
		return;
	}

	char *desiredmode = Cmd_Argv (1);
	int modenum = desiredmode[0] - '0';

	for (int i = 0; i < 6; i++)
	{
		if (!stricmp (d3d_filtermodes[i].name, desiredmode) || i == modenum)
		{
			// reset filter
			d3d_TexFilter = d3d_filtermodes[i].texfilter;
			d3d_MipFilter = d3d_filtermodes[i].mipfilter;

			Con_Printf ("Texture filter: %s\n", D3DTypeToString (d3d_TexFilter));
			Con_Printf ("Mipmap filter:  %s\n", D3DTypeToString (d3d_MipFilter));
			return;
		}
	}

	Con_Printf ("bad filter name\n");
}


void D3DVid_SaveTextureMode (FILE *f)
{
	for (int i = 0; i < 6; i++)
	{
		if (d3d_TexFilter == d3d_filtermodes[i].texfilter && d3d_MipFilter == d3d_filtermodes[i].mipfilter)
		{
			fprintf (f, "gl_texturemode %s\n", d3d_filtermodes[i].name);
			return;
		}
	}
}


cmd_t D3DVid_TextureMode_Cmd ("gl_texturemode", D3DVid_TextureMode_f);

void D3DVid_CheckVidMode (void)
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
	for (int i = 0; i < d3d_NumModes; i++)
	{
		// this was also declared in loop scope; how did it ever work???
		findmode = d3d_ModeList + i;

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
		Con_Printf ("D3DVid_CheckVidMode: selected video mode not available\n");

		// restore the previous value
		Cvar_Set (&d3d_mode, old_vidmode);

		// get out
		return;
	}

	// store back (deferred to here so that we can restore the value if the mode isn't found)
	old_vidmode = d3d_mode.integer;

	// reset the window
	D3DVid_ResetWindow (&findmode->d3d_Mode);

	// store to current mode
	d3d_CurrentMode.Format = findmode->d3d_Mode.Format;
	d3d_CurrentMode.Height = findmode->d3d_Mode.Height;
	d3d_CurrentMode.RefreshRate = findmode->d3d_Mode.RefreshRate;
	d3d_CurrentMode.Width = findmode->d3d_Mode.Width;

	// restart video
	D3DVid_Restart_f ();

	// update the refdef
	vid.recalc_refdef = 1;
}


int D3DVid_AdjustGamma (float gammaval, int baseval)
{
	baseval >>= 8;

	// the same gamma calc as GLQuake had some "gamma creep" where DirectQ would gradually get brighter
	// the more it was run; this hopefully fixes it once and for all
	float f = pow ((float) baseval / 255.0f, (float) gammaval);
	float inf = f * 255 + 0.5;

	// return what we got
	return (BYTE_CLAMP ((int) inf)) << 8;
}


void D3DVid_SetActiveGamma (void)
{
	// apply v_gamma to all components
	for (int i = 0; i < 256; i++)
	{
		// adjust v_gamma to the same scale as glquake uses
		d3d_CurrentGamma.r[i] = D3DVid_AdjustGamma (v_gamma.value, d3d_DefaultGamma.r[i]);
		d3d_CurrentGamma.g[i] = D3DVid_AdjustGamma (v_gamma.value, d3d_DefaultGamma.g[i]);
		d3d_CurrentGamma.b[i] = D3DVid_AdjustGamma (v_gamma.value, d3d_DefaultGamma.b[i]);
	}

	// now apply r/g/b to the derived values
	for (int i = 0; i < 256; i++)
	{
		d3d_CurrentGamma.r[i] = D3DVid_AdjustGamma (r_gamma.value, d3d_CurrentGamma.r[i]);
		d3d_CurrentGamma.g[i] = D3DVid_AdjustGamma (g_gamma.value, d3d_CurrentGamma.g[i]);
		d3d_CurrentGamma.b[i] = D3DVid_AdjustGamma (b_gamma.value, d3d_CurrentGamma.b[i]);
	}

	VID_SetAppGamma ();
}


void D3DVid_CheckGamma (void)
{
	// these values are to force a change on first run
	static int oldvgamma = -1;
	static int oldrgamma = -1;
	static int oldggamma = -1;
	static int oldbgamma = -1;

	// didn't change - call me paranoid about floats!!!
	if ((int) (v_gamma.value * 100) == oldvgamma &&
			(int) (r_gamma.value * 100) == oldrgamma &&
			(int) (g_gamma.value * 100) == oldggamma &&
			(int) (b_gamma.value * 100) == oldbgamma)
	{
		// didn't change
		return;
	}

	// store back
	oldvgamma = (int) (v_gamma.value * 100);
	oldrgamma = (int) (r_gamma.value * 100);
	oldggamma = (int) (g_gamma.value * 100);
	oldbgamma = (int) (b_gamma.value * 100);

	D3DVid_SetActiveGamma ();
}


void D3DVid_CheckVSync (void)
{
	static int old_vsync = vid_vsync.integer;

	if (old_vsync != vid_vsync.integer)
	{
		old_vsync = vid_vsync.integer;
		D3DVid_Restart_f ();
	}
}


void Host_Frame (DWORD time);

void D3DVid_RecoverLostDevice (void)
{
	bool was_blocked = block_drawing;
	bool was_clpaused = cl.paused;
	bool was_svpaused = sv.paused;

	// pause everything and block drawing
	block_drawing = true;
	cl.paused = true;
	sv.paused = true;

	while (1)
	{
		// run a frame to keep everything ticking along
		Sys_SendKeyEvents ();
		Host_Frame (20);

		// yield CPU for a while
		Sleep (20);

		// see if the device can be recovered
		hr = d3d_Device->TestCooperativeLevel ();

		switch (hr)
		{
		case D3D_OK:
			// recover device resources and bring states back to defaults
			D3DVid_RecoverDeviceResources ();

			// the device is no longer lost
			Con_DPrintf ("recovered lost device\n");

			// force an update of areas that aren't normally updated in the main refresh
			HUD_Changed ();

			// restore states (this is ugly)
			block_drawing = was_blocked;
			cl.paused = was_clpaused;
			sv.paused = was_svpaused;

			// return to normal rendering
			return;

		case D3DERR_DEVICELOST:
			// the device cannot be recovered at this time
			break;

		case D3DERR_DEVICENOTRESET:
			// the device is ready to be reset
			D3DVid_LoseDeviceResources ();

			// reset the device
			d3d_Device->Reset (&d3d_PresentParams);
			break;

		case D3DERR_DRIVERINTERNALERROR:
		default:
			// something bad happened
			Sys_Quit (13);
			break;
		}
	}
}


void Fog_FrameCheck (void);

void D3DVid_BeginRendering (void)
{
	// video is not restarted by default
	vid_restarted = false;

	// check for any changes to any display properties
	// if we need to restart video we must skip drawing this frame
	D3DVid_CheckGamma ();
	D3DVid_CheckVidMode (); if (vid_restarted) return;
	D3DVid_CheckVSync (); if (vid_restarted) return;
	Fog_FrameCheck ();

	// force lighting calcs off; this is done every frame and it will be filtered if necessary
	D3D_SetRenderState (D3DRS_LIGHTING, FALSE);

	// this is called before d3d_Device->BeginScene so that the d3d_Device->BeginScene is called on the correct rendertarget
	D3DRTT_BeginScene ();

	// SCR_UpdateScreen assumes that this is the last call in this function
	hr = d3d_Device->BeginScene ();
}


void D3DVid_EndRendering (void)
{
	// unbind everything
	D3D_SetStreamSource (0, NULL, 0, 0);
	D3D_SetStreamSource (1, NULL, 0, 0);
	D3D_SetStreamSource (2, NULL, 0, 0);

	D3D_SetIndices (NULL);
	D3D_SetVertexDeclaration (NULL);

	d3d_Device->EndScene ();

	if ((hr = d3d_Device->Present (NULL, NULL, NULL, NULL)) == D3DERR_DEVICELOST)
		D3DVid_RecoverLostDevice ();
}


void D3DVid_Finish (void)
{
	// bollocks!
}


/*
=========================================================================================================================================

		VERTICAL SYNC DETECTION

	Here we attempt to detect any vsync settings in a driver control panel.  D3D doesn't seem to have a way of detecting this so
	instead we'll break into some OpenGL, run some frames in a double-buffered context, see how long they take and make a guess
	based on that.  This should be done as early as possible in the program (but after the timer is initialized) so that a D3D
	device isn't active while it's running.  We could potentially rework this as a splash screen at some point in time???

=========================================================================================================================================
*/

#include <gl/gl.h>
#pragma comment (lib, "opengl32.lib")

void D3DVid_DetectVSync (HWND hWnd)
{
	int pf = 0;
	HGLRC hRC = NULL;
	HDC hDC = GetDC (hWnd);

	PIXELFORMATDESCRIPTOR pfd =
	{
		sizeof (PIXELFORMATDESCRIPTOR),
		1,
		PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,
		PFD_TYPE_RGBA,
		32,
		0, 0, 0, 0, 0, 0,
		0,
		0,
		0,
		0, 0, 0, 0,
		24,
		0,
		0,
		PFD_MAIN_PLANE,
		0,
		0, 0, 0
	};

	if (!(pf = ChoosePixelFormat (hDC, &pfd))) return;
	if (!SetPixelFormat (hDC, pf, &pfd)) return;
	if (!(hRC = wglCreateContext (hDC))) return;

	if (!wglMakeCurrent (hDC, hRC))
	{
		wglDeleteContext (hRC);
		return;
	}

	// run some dummy frames to warm it up as otherwise the first frame or so might be unusually long
	for (int i = 0; i < 4; i++)
	{
		glClear (GL_DEPTH_BUFFER_BIT);
		glFinish ();
		SwapBuffers (hDC);
	}

	DWORD start = Sys_Milliseconds ();

	// now do it for real
	for (int i = 0; i < 4; i++)
	{
		glClear (GL_DEPTH_BUFFER_BIT);
		glFinish ();
		SwapBuffers (hDC);
	}

	DWORD end = Sys_Milliseconds ();

	wglMakeCurrent (NULL, NULL);
	wglDeleteContext (hRC);
	ReleaseDC (hWnd, hDC);

	// we're running 4 frames so if vsync is enabled in the driver control panel
	// a difference of > 20ms is good for detecting refresh rates up to 200 hz.
	if (end - start > 20)
		Cvar_Set (&vid_vsync, 1.0f);
	else Cvar_Set (&vid_vsync, 0.0f);
}




