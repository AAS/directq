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

void D3DVid_CenterWindow (HWND hWnd);
void D3DVid_SendToForeground (HWND hWnd);
void D3DVid_SetWindowStyles (HWND hWnd, RECT *adjrect, D3DDISPLAYMODE *mode);

#define VIDDRIVER_VERSION "1.8.7-no-16-bpp"
cvar_t viddriver_version ("viddriver_version", "unknown", CVAR_ARCHIVE);

void D3DVid_UpdateDriver (void)
{
}


bool D3DVid_IsFullscreen (void)
{
	if (d3d_CurrentMode.RefreshRate)
		return true;
	else return false;
}


void D3D_SetAllStates (void);

// declarations that don't really fit in anywhere else
LPDIRECT3D9 d3d_Object = NULL;
LPDIRECT3DDEVICE9 d3d_Device = NULL;
LPDIRECT3DSWAPCHAIN9 d3d_SwapChain = NULL;
LPDIRECT3DQUERY9 d3d_FinishQuery = NULL;

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

// lightmaps
void D3D_LoseLightmapResources (void);
void D3D_RecoverLightmapResources (void);
void D3DRTT_CreateRTTTexture (void);

// fixme - merge these two
HWND d3d_Window;

void D3DVid_Restart_f (void);
bool vid_queuerestart = false;

void D3DVid_QueueRestart (cvar_t *var)
{
	// rather than restart immediately we notify the renderer that it will need to restart as soon as it comes up
	// this should hopefully fix a lot of crap during startup (and generally speed startup up a LOT)
	vid_queuerestart = true;
}


// video cvars
void D3DVid_SetActiveGamma (cvar_t *var);

// force an invalid mode on initial entry
cvar_t		vid_mode ("vid_mode", "-666", CVAR_ARCHIVE);
cvar_t		d3d_mode ("d3d_mode", "-1", CVAR_ARCHIVE, D3DVid_QueueRestart);
cvar_t		vid_wait ("vid_wait", "0");
cvar_t		v_gamma ("gamma", "1", CVAR_ARCHIVE, D3DVid_SetActiveGamma);
cvar_t		r_gamma ("r_gamma", "1", CVAR_ARCHIVE, D3DVid_SetActiveGamma);
cvar_t		g_gamma ("g_gamma", "1", CVAR_ARCHIVE, D3DVid_SetActiveGamma);
cvar_t		b_gamma ("b_gamma", "1", CVAR_ARCHIVE, D3DVid_SetActiveGamma);
cvar_t		vid_vsync ("vid_vsync", "0", CVAR_ARCHIVE, D3DVid_QueueRestart);
cvar_t		vid_contrast ("contrast", "1", CVAR_ARCHIVE, D3DVid_SetActiveGamma);
cvar_t		r_contrast ("r_contrast", "1", CVAR_ARCHIVE, D3DVid_SetActiveGamma);
cvar_t		g_contrast ("g_contrast", "1", CVAR_ARCHIVE, D3DVid_SetActiveGamma);
cvar_t		b_contrast ("b_contrast", "1", CVAR_ARCHIVE, D3DVid_SetActiveGamma);
cvar_t		d3d_multisample ("d3d_multisample", "0", CVAR_ARCHIVE, D3DVid_QueueRestart);
cvar_t		gl_finish ("gl_finish", 0.0f);

cvar_t d3d_usinginstancing ("r_instancing", "1", CVAR_ARCHIVE);

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
cvar_alias_t gl_anisotropic_filter_alias ("gl_anisotropic_filter", &r_anisotropicfilter);

cvar_t gl_triplebuffer ("gl_triplebuffer", 1, CVAR_ARCHIVE);

D3DDISPLAYMODE *d3d_ModeList = NULL;

int d3d_NumModes = 0;
int d3d_NumWindowedModes = 0;

RECT WorkArea;
D3DDISPLAYMODE d3d_DesktopMode;
D3DDISPLAYMODE d3d_CurrentMode;

void ClearAllStates (void);
void AppActivate (BOOL fActive, BOOL minimize);


void D3DVid_ClearScreen (void)
{
	if (d3d_Device)
	{
		// this represents another frame
		d3d_RenderDef.framecount++;
		d3d_Device->Clear (0, NULL, D3DCLEAR_TARGET, 0x00000000, 1.0f, 1);
		d3d_Device->Present (NULL, NULL, NULL, NULL);
	}
}


void D3DVid_ResetWindow (D3DDISPLAYMODE *mode);

// let's cohabit more peacefully with fitz...
// fixme - do this right (using vid_width and vid_height and mode 0 for a windowed mode)
cvar_t vid_width ("d3d_width", 0.0f, CVAR_ARCHIVE, D3DVid_QueueRestart);
cvar_t vid_height ("d3d_height", 0.0f, CVAR_ARCHIVE, D3DVid_QueueRestart);


void D3DVid_SyncDimensions (D3DDISPLAYMODE *mode)
{
	Cvar_Set (&vid_width, mode->Width);
	Cvar_Set (&vid_height, mode->Height);
}


void D3DVid_ResizeToDimension (int width, int height)
{
	// don't fire this if (a) we don't have a device or (b) we're not in a windowed mode
	if (!d3d_Device) return;
	if (d3d_CurrentMode.RefreshRate != 0) return;

	// default 0 is to use the current mode
	if (width < 1) width = d3d_CurrentMode.Width;
	if (height < 1) height = d3d_CurrentMode.Height;

	// upper bound
	if (width > d3d_DesktopMode.Width) width = d3d_DesktopMode.Width;
	if (height > d3d_DesktopMode.Height) height = d3d_DesktopMode.Height;

	// prevent insanity
	if (width < 640) width = 640;
	if (height < 480) height = 480;

	// no change
	if (width == d3d_CurrentMode.Width && height == d3d_CurrentMode.Height) return;

	d3d_PresentParams.BackBufferWidth = d3d_CurrentMode.Width = width;
	d3d_PresentParams.BackBufferHeight = d3d_CurrentMode.Height = height;

	vid.recalc_refdef = 1;
	IN_UpdateClipCursor ();
	D3DVid_ClearScreen ();
	//D3DVid_Restart_f ();
	vid_queuerestart = true;

	// note - this will recursively call this function but the checks above will catch it
	D3DVid_ResetWindow (&d3d_CurrentMode);
	D3DVid_SyncDimensions (&d3d_CurrentMode);
}


void D3DVid_ResizeWindow (HWND hWnd)
{
	if (!d3d_Device) return;
	if (d3d_CurrentMode.RefreshRate != 0) return;

	// reset the device to resize the backbuffer
	RECT clientrect;
	GetClientRect (hWnd, &clientrect);

	// check the client rect dimensions to make sure it's valid
	// (sometimes on minimize or show desktop we get a clientrect of 0/0)
	if (clientrect.right - clientrect.left < 1) return;
	if (clientrect.bottom - clientrect.top < 1) return;

	D3DVid_ResizeToDimension (clientrect.right - clientrect.left, clientrect.bottom - clientrect.top);
}


void D3DVid_ResetWindow (D3DDISPLAYMODE *mode)
{
	RECT rect;
	D3DVid_SetWindowStyles (d3d_Window, &rect, mode);

	int width = rect.right - rect.left;
	int height = rect.bottom - rect.top;

	// resize the window
	SetWindowPos (d3d_Window, HWND_TOP, 0, 0, width, height, SWP_NOMOVE | SWP_SHOWWINDOW);

	// the style reset requires a SWP to update cached info
	SetWindowPos (d3d_Window, HWND_TOP, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOSIZE | SWP_NOMOVE | SWP_SHOWWINDOW);

	// re-center it if windowed
	if (mode->RefreshRate == 0) D3DVid_CenterWindow (d3d_Window);

	// update cursor clip region
	IN_UpdateClipCursor ();
	D3DVid_SyncDimensions (mode);
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

	hr = d3d_Object->CheckDeviceMultiSampleType (D3DADAPTER_DEFAULT,
		D3DDEVTYPE_HAL,
		(mode->RefreshRate == 0) ? d3d_DesktopMode.Format : mode->Format,
		(mode->RefreshRate == 0),
		D3DMULTISAMPLE_NONMASKABLE,
		&d3d_GlobalCaps.MaxMultiSample);

	// per the spec, max multisample quality is between 0 and 1 less than this
	d3d_GlobalCaps.MaxMultiSample--;

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

	if (d3d_multisample.integer > 0 && d3d_GlobalCaps.MaxMultiSample > 0)
	{
		if (d3d_multisample.integer > d3d_GlobalCaps.MaxMultiSample)
			pp->MultiSampleQuality = d3d_GlobalCaps.MaxMultiSample;
		else pp->MultiSampleQuality = d3d_multisample.integer;

		pp->MultiSampleType = D3DMULTISAMPLE_NONMASKABLE;
	}
	else
	{
		pp->MultiSampleQuality = 0;
		pp->MultiSampleType = D3DMULTISAMPLE_NONE;
	}
}


void D3DVid_ModeDescription (int modenum, D3DDISPLAYMODE *mode)
{
	Con_Printf
	(
		"%3i: %ix%i (%s)\n",
		modenum,
		mode->Width,
		mode->Height,
		mode->RefreshRate == 0 ? "Windowed" : D3DTypeToString (mode->Format)
	);
}


void D3DVid_DescribeModes_f (void)
{
	for (int i = 0; i < d3d_NumModes; i++)
	{
		D3DDISPLAYMODE *mode = d3d_ModeList + i;
		D3DVid_ModeDescription (i, mode);
	}
}


void D3DVid_NumModes_f (void)
{
	if (d3d_NumModes == 1)
		Con_Printf ("%d video mode is available\n", d3d_NumModes);
	else Con_Printf ("%d video modes are available\n", d3d_NumModes);
}


bool D3DVid_ModeIsCurrent (D3DDISPLAYMODE *mode)
{
	if (d3d_CurrentMode.RefreshRate)
	{
		if (mode->Format != d3d_CurrentMode.Format) return false;
		if (mode->Height != d3d_CurrentMode.Height) return false;
		if (mode->RefreshRate != d3d_CurrentMode.RefreshRate) return false;
		if (mode->Width != d3d_CurrentMode.Width) return false;
	}
	else
	{
		if (mode->RefreshRate != 0) return false;
		if (mode->Height != d3d_CurrentMode.Height) return false;
		if (mode->Width != d3d_CurrentMode.Width) return false;
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
			Cvar_Set (&d3d_mode, i);
			return;
		}
	}
}


void D3DVid_DescribeCurrentMode_f (void)
{
	for (int i = 0; i < d3d_NumModes; i++)
	{
		D3DDISPLAYMODE *mode = d3d_ModeList + i;

		if (!D3DVid_ModeIsCurrent (mode)) continue;

		D3DVid_ModeDescription (i, mode);
		return;
	}
}


void D3DVid_DescribeMode_f (void)
{
	int modenum = atoi (Cmd_Argv (1));

	if (modenum < 0 || modenum >= d3d_NumModes)
		Con_Printf ("Unknown video mode: %i\n", modenum);
	else D3DVid_ModeDescription (modenum, &d3d_ModeList[modenum]);
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
	SAFE_RELEASE (d3d_FinishQuery);
	SAFE_RELEASE (d3d_SwapChain);

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
	// video can't be restarted yet
	if (!d3d_Device) return;

	// if we're attempting to change a fullscreen mode we need to validate it first
	if (d3d_CurrentMode.Format != D3DFMT_UNKNOWN && d3d_CurrentMode.RefreshRate != 0)
	{
		DEVMODE dm;

		memset (&dm, 0, sizeof (DEVMODE));
		dm.dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY;
		dm.dmBitsPerPel = 32;
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
		Sleep (1);
		hr = d3d_Device->TestCooperativeLevel ();

		if (hr == D3D_OK) break;
	}

	// wipe the screen before resetting the device
	if (key_dest == key_menu) D3DVid_ClearScreen ();

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
		Sleep (1);
		hr = d3d_Device->TestCooperativeLevel ();

		if (hr == D3D_OK) break;
	}

	// wipe the screen again post-reset
	if (key_dest == key_menu) D3DVid_ClearScreen ();

	// bring back anything that needs to be brought back
	D3DVid_RecoverDeviceResources ();

	// flag to skip this frame so that we update more robustly
	vid_restarted = true;

	D3DVid_SyncDimensions (&d3d_CurrentMode);

	Cbuf_InsertText ("\n");
	Cbuf_Execute ();

	Con_Printf ("reset video mode\n");
}


void Menu_VideoDecodeVideoModes (D3DDISPLAYMODE *modes, int totalnummodes, int numwindowed);

bool ExistingMode (D3DDISPLAYMODE *mode)
{
	for (int i = 0; i < d3d_NumModes; i++)
	{
		if (d3d_ModeList[i].Format != mode->Format) continue;
		if (d3d_ModeList[i].Height != mode->Height) continue;
		if (d3d_ModeList[i].RefreshRate != mode->RefreshRate) continue;
		if (d3d_ModeList[i].Width != mode->Width) continue;

		// mode is already present
		return true;
	}

	// mode is not 
	return false;
}


int D3DVid_ModeSortFunc (D3DDISPLAYMODE *m1, D3DDISPLAYMODE *m2)
{
	if (m1->RefreshRate == m2->RefreshRate)
	{
		if (m1->Width == m2->Width)
			return ((int) m1->Height - (int) m2->Height);
		else return ((int) m1->Width - (int) m2->Width);
	}
	else return ((int) m1->RefreshRate - (int) m2->RefreshRate);
}


void D3DVid_EnumerateVideoModes (void)
{
	// get the desktop mode for reference
	d3d_Object->GetAdapterDisplayMode (D3DADAPTER_DEFAULT, &d3d_DesktopMode);

	// get the size of the desktop working area.  this is used instead of the desktop resolution for
	// determining if a mode is valid for windowed operation, as the desktop size gives 768 as a
	// valid height in an 800 high display, meaning that the taskbar will hide parts of the DirectQ window.
	SystemParametersInfo (SPI_GETWORKAREA, 0, &WorkArea, 0);

	int MaxWindowWidth = WorkArea.right - WorkArea.left;
	int MaxWindowHeight = WorkArea.bottom - WorkArea.top;

	// get the count of modes for this format
	int ModeCount = d3d_Object->GetAdapterModeCount (D3DADAPTER_DEFAULT, d3d_DesktopMode.Format);

	// no modes available for this format
	if (!ModeCount)
	{
		Sys_Error ("D3DVid_EnumerateVideoModes: No 32 BPP display modes available");
		return;
	}

	d3d_ModeList = (D3DDISPLAYMODE *) scratchbuf;
	d3d_NumModes = 0;
	d3d_NumWindowedModes = 0;

	// enumerate them all
	for (int i = 0; i < ModeCount; i++)
	{
		// get the mode description
		d3d_Object->EnumAdapterModes (D3DADAPTER_DEFAULT, d3d_DesktopMode.Format, i, &d3d_ModeList[d3d_NumModes]);

		// we're only interested in modes that match the desktop refresh rate
		if (d3d_ModeList[d3d_NumModes].RefreshRate != d3d_DesktopMode.RefreshRate) continue;

		// don't allow modes < 640 x 480
		if (d3d_ModeList[d3d_NumModes].Width < 640) continue;
		if (d3d_ModeList[d3d_NumModes].Height < 480) continue;

		// if the mode width is < height we assume that we have a monitor capable of rotating it's desktop
		// and therefore we skip the mode
		if (d3d_ModeList[d3d_NumModes].Width < d3d_ModeList[d3d_NumModes].Height) continue;

		// see does the mode already exist
		bool existingmode = false;

		for (int j = 0; j < d3d_NumModes; j++)
		{
			if (d3d_ModeList[d3d_NumModes].Width != d3d_ModeList[j].Width) continue;
			if (d3d_ModeList[d3d_NumModes].Height != d3d_ModeList[j].Height) continue;

			existingmode = true;
			break;
		}

		// if it's not a previously existing mode we advance the counter
		if (!existingmode) d3d_NumModes++;
	}

	// now fill in windowed modes
	for (int i = 0; i < d3d_NumModes; i++)
	{
		if (d3d_ModeList[i].Width >= MaxWindowWidth) continue;
		if (d3d_ModeList[i].Height >= MaxWindowHeight) continue;

		// add it as a windowed mode
		d3d_ModeList[d3d_NumModes + d3d_NumWindowedModes].Width = d3d_ModeList[i].Width;
		d3d_ModeList[d3d_NumModes + d3d_NumWindowedModes].Height = d3d_ModeList[i].Height;
		d3d_ModeList[d3d_NumModes + d3d_NumWindowedModes].Format = D3DFMT_UNKNOWN;
		d3d_ModeList[d3d_NumModes + d3d_NumWindowedModes].RefreshRate = 0;
		d3d_NumWindowedModes++;
	}

	// add the windowed modes to the total mode count
	d3d_NumModes += d3d_NumWindowedModes;

	// sort the list to put them into proper order
	qsort (d3d_ModeList, d3d_NumModes, sizeof (D3DDISPLAYMODE), (sortfunc_t) D3DVid_ModeSortFunc);

	// copy them out to main memory
	d3d_ModeList = (D3DDISPLAYMODE *) MainZone->Alloc (d3d_NumModes * sizeof (D3DDISPLAYMODE));
	memcpy (d3d_ModeList, scratchbuf, d3d_NumModes * sizeof (D3DDISPLAYMODE));

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
			return;
		}
	}

	// find a mode to start in; we start directq in a windowed mode at either 640x480 or 800x600, whichever is
	// higher.  windowed modes are safer - they don't have exclusive ownership of your screen so if things go
	// wrong on first run you can get out easier.
	D3DDISPLAYMODE *windowedmode800 = NULL;
	D3DDISPLAYMODE *windowedmode640 = NULL;

	// now find a good default windowed mode - try to find either 800x600 or 640x480
	for (int i = 0; i < d3d_NumModes; i++)
	{
		D3DDISPLAYMODE *mode = d3d_ModeList + i;

		// not a windowed mode
		if (mode->Format != D3DFMT_UNKNOWN) continue;
		if (mode->RefreshRate > 0) continue;
		if (mode->Width == 800 && mode->Height == 600) windowedmode800 = mode;
		if (mode->Width == 640 && mode->Height == 480) windowedmode640 = mode;
	}

	// use the best windowed mode we could find of 800x600 or 640x480, or mode 0 if none found
	// (this will be a fullscreen mode at some crazy low resolution, so it probably won't work anyway...)
	if (windowedmode800)
	{
		d3d_CurrentMode.Width = 800;
		d3d_CurrentMode.Height = 600;
		d3d_CurrentMode.Format = D3DFMT_UNKNOWN;
		d3d_CurrentMode.RefreshRate = 0;
	}
	else if (windowedmode640)
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
	if (d3d_mode.integer < 0) return;
	if (d3d_mode.integer >= d3d_NumModes) return;

	D3DDISPLAYMODE *findmode = d3d_ModeList + d3d_mode.integer;

	// copy them out
	mode->Width = findmode->Width;
	mode->Height = findmode->Height;

	// be certain to copy these out too so that we know they're valid for windowed or fullscreen
	mode->Format = findmode->Format;
	mode->RefreshRate = findmode->RefreshRate;
}


void D3DVid_FindBestWindowedMode (D3DDISPLAYMODE *mode)
{
	D3DDISPLAYMODE *best = NULL;

	for (int i = 0; i < d3d_NumModes; i++)
	{
		D3DDISPLAYMODE *winmode = d3d_ModeList + i;

		// not valid for a windowed mode
		if (winmode->RefreshRate) continue;
		if (winmode->Format != D3DFMT_UNKNOWN) continue;

		if (!best)
		{
			// if we don't have a best mode set yet, the first one we find is it!
			best = winmode;
			continue;
		}

		if (winmode->Width == mode->Width && winmode->Height == mode->Height)
		{
			// exact match
			best = winmode;
			break;
		}

		// if either of width or height were unspecified, take one that matches
		if (winmode->Width == mode->Width && mode->Height == 0) best = winmode;
		if (winmode->Width == 0 && winmode->Height == mode->Height) best = winmode;

		// if both were unspecified we take the current windowed mode as the best
		if (mode->Height == 0 && mode->Width == 0) best = winmode;
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
		D3DDISPLAYMODE *fsmode = d3d_ModeList + i;

		// invalid format
		if (fsmode->Format != mode->Format) continue;

		if (!best)
		{
			// if we don't have a best mode set yet, the first one we find is it!
			best = fsmode;
			continue;
		}

		if (fsmode->Width == mode->Width && fsmode->Height == mode->Height)
		{
			// exact match
			best = fsmode;
			break;
		}

		// if either of width or height were unspecified, take one that matches
		if (fsmode->Width == mode->Width && mode->Height == 0) best = fsmode;
		if (fsmode->Width == 0 && fsmode->Height == mode->Height) best = fsmode;

		// if both were unspecified we take the current fullscreen mode as the best
		if (mode->Height == 0 && mode->Width == 0) best = fsmode;
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
		fclose (f);
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

	// D3D9 guarantees 16 streams and 8 TMUs - don't bother checking TMUs
	if (d3d_DeviceCaps.MaxStreams < 4) Sys_Error ("You need a device with at least 4 Vertex Streams to run DirectQ");

	// software t&l reports 0 for the max vertex shader constant registers
	if (d3d_DeviceCaps.MaxVertexShaderConst < 256 && d3d_DeviceCaps.MaxVertexShaderConst > 0)
		Sys_Error ("You need a device with at least 256 Vertex Shader Constant Registers to run DirectQ");

	// check for z buffer support
	if (!(d3d_DeviceCaps.ZCmpCaps & D3DPCMPCAPS_ALWAYS)) Sys_Error ("You need a device that supports a proper Z buffer to run DirectQ");
	if (!(d3d_DeviceCaps.ZCmpCaps & D3DPCMPCAPS_EQUAL)) Sys_Error ("You need a device that supports a proper Z buffer to run DirectQ");
	if (!(d3d_DeviceCaps.ZCmpCaps & D3DPCMPCAPS_GREATER)) Sys_Error ("You need a device that supports a proper Z buffer to run DirectQ");
	if (!(d3d_DeviceCaps.ZCmpCaps & D3DPCMPCAPS_GREATEREQUAL)) Sys_Error ("You need a device that supports a proper Z buffer to run DirectQ");
	if (!(d3d_DeviceCaps.ZCmpCaps & D3DPCMPCAPS_LESS)) Sys_Error ("You need a device that supports a proper Z buffer to run DirectQ");
	if (!(d3d_DeviceCaps.ZCmpCaps & D3DPCMPCAPS_LESSEQUAL)) Sys_Error ("You need a device that supports a proper Z buffer to run DirectQ");
	if (!(d3d_DeviceCaps.ZCmpCaps & D3DPCMPCAPS_NEVER)) Sys_Error ("You need a device that supports a proper Z buffer to run DirectQ");
	if (!(d3d_DeviceCaps.ZCmpCaps & D3DPCMPCAPS_NOTEQUAL)) Sys_Error ("You need a device that supports a proper Z buffer to run DirectQ");

	// now reset the present params as they will have become messed up above
	D3DVid_SetPresentParams (&d3d_PresentParams, mode);

	// attempt to create a hardware T&L device - we can ditch all of the extra flags now :)
	// (Quark ETP needs D3DCREATE_FPU_PRESERVE - using _controlfp during texcoord gen doesn't work)
	// here as the generated coords will also lose precision when being applied

	// if we don't support > 64k indexes we create as software VP
	if (d3d_DeviceCaps.MaxVertexIndex <= 0xffff)
	{
		d3d_GlobalCaps.supportHardwareTandL = false;
		d3d_GlobalCaps.deviceCreateFlags = D3DCREATE_SOFTWARE_VERTEXPROCESSING;
	}
	else
	{
		d3d_GlobalCaps.supportHardwareTandL = true;
		d3d_GlobalCaps.deviceCreateFlags = D3DCREATE_HARDWARE_VERTEXPROCESSING;
	}

	// ensure our device is validly gone
	SAFE_RELEASE (d3d_Device);

	extern HWND hwndSplash;
	if (hwndSplash) DestroyWindow (hwndSplash);

	hr = d3d_Object->CreateDevice
	(
		D3DADAPTER_DEFAULT,
		D3DDEVTYPE_HAL,
		d3d_Window,
		d3d_GlobalCaps.deviceCreateFlags | D3DCREATE_FPU_PRESERVE,
		&d3d_PresentParams,
		&d3d_Device
	);

	if (SUCCEEDED (hr))
	{
		// now we test for stream offset before finally accepting the device; the capability bit is unreliable
		// so we actually try it and see what happens (this is fun).  stream offset is always supported by a
		// software T&L device so we fall back on that if we can't get it in hardware.
		LPDIRECT3DVERTEXBUFFER9 sotest = NULL;
		int sotestsize = 4096 * 1024;

		hr = d3d_Device->CreateVertexBuffer (sotestsize, D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT, &sotest, NULL);
		if (FAILED (hr)) Sys_Error ("D3DVid_InitDirect3D : Failed to create a vertex buffer");

		// set up for drawing at multiple offsets to establish whether or not this will work
		for (int i = 0; i < 1024; i++)
		{
			hr = d3d_Device->SetStreamSource (0, sotest, i * 96, 32);

			if (FAILED (hr))
			{
				// destroy the device so that it will recreate with software t&l
				SAFE_RELEASE (sotest);
				SAFE_RELEASE (d3d_Device);
				break;
			}
		}

		// don't leak memory
		SAFE_RELEASE (sotest);
	}

	if (FAILED (hr) || !d3d_Device)
	{
		// we may still be able to create a software T&L device
		SAFE_RELEASE (d3d_Device);
		d3d_GlobalCaps.supportHardwareTandL = false;
		d3d_GlobalCaps.deviceCreateFlags = D3DCREATE_SOFTWARE_VERTEXPROCESSING;

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
	}

	if (d3d_GlobalCaps.supportHardwareTandL)
		Con_Printf ("Using Hardware Vertex Processing\n\n");
	else Con_Printf ("Using Software Vertex Processing\n\n");

	d3d_GlobalCaps.DefaultLock = D3DLOCK_NOSYSLOCK;
	d3d_GlobalCaps.DiscardLock = D3DLOCK_DISCARD | D3DLOCK_NOSYSLOCK;
	d3d_GlobalCaps.DynamicLock = D3DLOCK_NO_DIRTY_UPDATE | D3DLOCK_NOSYSLOCK;
	d3d_GlobalCaps.NoOverwriteLock = D3DLOCK_NOOVERWRITE | D3DLOCK_NOSYSLOCK;

	// report some caps
	Con_Printf ("Video mode %i (%ix%i) Initialized\n", d3d_mode.integer, mode->Width, mode->Height);
	Con_Printf ("Back Buffer Format: %s (created %i %s)\n", D3DTypeToString (mode->Format), d3d_PresentParams.BackBufferCount, d3d_PresentParams.BackBufferCount > 1 ? "backbuffers" : "backbuffer");
	Con_Printf ("Refresh Rate: %i Hz (%s)\n", mode->RefreshRate, mode->RefreshRate ? "Fullscreen" : "Windowed");
	Con_Printf ("\n");

	// clear to black immediately
	D3DVid_ClearScreen ();

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

	Con_Printf ("\n");

	// no np2 support by default
	d3d_GlobalCaps.supportNonPow2 = false;

	// get the shader model because we don't want to support NP2 on SM2 hardware (both ATI and NVIDIA have problems with this in OpenGL, and
	// while D3D does have stricter hardware capabilities checking it's not beyond the bounds of possibility that the driver could lie
	// and/or not throw an error until the texture is used).
	int vsvermaj = D3DSHADER_VERSION_MAJOR (d3d_DeviceCaps.VertexShaderVersion);
	int psvermaj = D3DSHADER_VERSION_MAJOR (d3d_DeviceCaps.PixelShaderVersion);

	// we only support np2 with ps3 or higher hardware as it's known-bad on certain ps2 hardware
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

	// check for ATI instancing hack
	if (!d3d_GlobalCaps.supportInstancing)
	{
		hr = d3d_Object->CheckDeviceFormat
		(
			D3DADAPTER_DEFAULT,
			D3DDEVTYPE_HAL,
			d3d_DesktopMode.Format,
			0,
			D3DRTYPE_SURFACE,
			(D3DFORMAT) MAKEFOURCC ('I','N','S','T')
		);

		if (SUCCEEDED (hr))
		{
			// enable instancing
			hr = d3d_Device->SetRenderState (D3DRS_POINTSIZE, MAKEFOURCC ('I','N','S','T'));

			if (SUCCEEDED (hr))
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
		}
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

	// begin at 1 so that any newly allocated model_t will be 0 and therefore must
	// be explicitly set to be valid
	d3d_RenderDef.RegistrationSequence = 1;

	D3DVid_SyncDimensions (mode);
}


void D3DVid_CreateWindow (D3DDISPLAYMODE *mode)
{
	RECT rect;
	D3DVid_SetWindowStyles (d3d_Window, &rect, mode);

	int width = rect.right - rect.left;
	int height = rect.bottom - rect.top;

	// switch size and position (also hide the window here)
	SetWindowPos (d3d_Window, NULL, rect.left, rect.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS | SWP_FRAMECHANGED | SWP_HIDEWINDOW);
	SetWindowPos (d3d_Window, NULL, 0, 0, width, height, SWP_NOMOVE | SWP_NOZORDER | SWP_NOCOPYBITS | SWP_FRAMECHANGED | SWP_HIDEWINDOW);

	// switch window properties
	SetWindowLong (d3d_Window, GWL_WNDPROC, (LONG) MainWndProc);

	if (mode->RefreshRate == 0) D3DVid_CenterWindow (d3d_Window);

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
	Host_DisableForLoading (true);
	CDAudio_Pause ();

	// much of this is now bullshit as we're now getting a 640x480 mode initially
	// if neither width nor height were specified we take it from the d3d_mode cvar
	// this then passes through to the find best... functions so that the rest of the mode is filled in
	if (mode->Width == 0 && mode->Height == 0) D3DVid_FindModeForVidMode (mode);

	// even if a mode is found we pass through here
	if (mode->RefreshRate == 0)
	{
		D3DVid_FindBestWindowedMode (mode);

		// override the selected mode with the width/height cvars
		if (vid_width.value > 0) mode->Width = vid_width.value;
		if (vid_height.value > 0) mode->Height = vid_height.value;

		// upper bound
		if (mode->Width > d3d_DesktopMode.Width) mode->Width = d3d_DesktopMode.Width;
		if (mode->Height > d3d_DesktopMode.Height) mode->Height = d3d_DesktopMode.Height;

		// prevent insanity
		if (mode->Width < 640) mode->Width = 640;
		if (mode->Height < 480) mode->Height = 480;
	}
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
	Host_DisableForLoading (temp);

	// now we force the window to the top of the z-order and the foreground
	D3DVid_SendToForeground (d3d_Window);

	// fix the leftover Alt from any Alt-Tab or the like that switched us away
	ClearAllStates ();

	// force an immediate recalc of the refdef
	vid.recalc_refdef = 1;
	D3DVid_SyncDimensions (mode);
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

		// windowed modes use the same format as the desktop and refresh rate 0
		d3d_CurrentMode.Format = D3DFMT_UNKNOWN;
		d3d_CurrentMode.RefreshRate = 0;
	}
	else
	{
		// use the desktop format and refresh rate
		d3d_CurrentMode.Format = d3d_DesktopMode.Format;
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
	D3DVid_ClearScreen ();

	// also need these... ;)
	D3DVid_LoseDeviceResources ();

	// release anything that needs to be released
	D3D_ReleaseTextures ();

	// take down our shaders
	D3DHLSL_Shutdown ();

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
		if (!_stricmp (d3d_filtermodes[i].name, desiredmode) || i == modenum)
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

	if (d3d_mode.integer < 0) return;
	if (d3d_mode.integer >= d3d_NumModes) return;
	if (old_vidmode == d3d_mode.integer) return;

	if (old_vidmode == -1)
	{
		// first time is not a mode change
		old_vidmode = d3d_mode.integer;
		return;
	}

	// testing
	Con_DPrintf ("mode is %i (was %i)...\n", d3d_mode.integer, old_vidmode);

	// attempt to find the mode
	D3DDISPLAYMODE *findmode = d3d_ModeList + d3d_mode.integer;

	do
	{
		// look for differences
		if (findmode->Format != d3d_CurrentMode.Format) break;
		if (findmode->Width != d3d_CurrentMode.Width) break;
		if (findmode->Height != d3d_CurrentMode.Height) break;
		if (findmode->RefreshRate != d3d_CurrentMode.RefreshRate) break;

		// no differences found so it hasn't changed, just get out
		Con_Printf ("Mode is unchanged\n");

		// store back to prevent this from repeatedly occurring
		old_vidmode = d3d_mode.integer;
		return;
	} while (0);

	// store back (deferred to here so that we can restore the value if the mode isn't found)
	old_vidmode = d3d_mode.integer;

	// reset the window
	D3DVid_ResetWindow (findmode);
	D3DVid_SyncDimensions (findmode);

	// store to current mode
	d3d_CurrentMode.Format = findmode->Format;
	d3d_CurrentMode.Height = findmode->Height;
	d3d_CurrentMode.RefreshRate = findmode->RefreshRate;
	d3d_CurrentMode.Width = findmode->Width;
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


int D3DVid_AdjustContrast (float contrastval, int baseval)
{
	int i = ((float) (baseval - 32767) * contrastval) + 32767;

	if (i < 0)
		return 0;
	else if (i > 65535)
		return 65535;
	else return i;
}


void D3DVid_SetActiveGamma (cvar_t *var)
{
	// create a valid baseline for everything to work from
	for (int i = 0; i < 256; i++)
	{
		d3d_CurrentGamma.r[i] = d3d_DefaultGamma.r[i];
		d3d_CurrentGamma.g[i] = d3d_DefaultGamma.g[i];
		d3d_CurrentGamma.b[i] = d3d_DefaultGamma.b[i];
	}

	// apply v_gamma to all components
	for (int i = 0; i < 256; i++)
	{
		d3d_CurrentGamma.r[i] = D3DVid_AdjustGamma (v_gamma.value, d3d_CurrentGamma.r[i]);
		d3d_CurrentGamma.g[i] = D3DVid_AdjustGamma (v_gamma.value, d3d_CurrentGamma.g[i]);
		d3d_CurrentGamma.b[i] = D3DVid_AdjustGamma (v_gamma.value, d3d_CurrentGamma.b[i]);
	}

	// now apply r/g/b to the derived values
	for (int i = 0; i < 256; i++)
	{
		d3d_CurrentGamma.r[i] = D3DVid_AdjustGamma (r_gamma.value, d3d_CurrentGamma.r[i]);
		d3d_CurrentGamma.g[i] = D3DVid_AdjustGamma (g_gamma.value, d3d_CurrentGamma.g[i]);
		d3d_CurrentGamma.b[i] = D3DVid_AdjustGamma (b_gamma.value, d3d_CurrentGamma.b[i]);
	}

	// apply global contrast
	for (int i = 0; i < 256; i++)
	{
		d3d_CurrentGamma.r[i] = D3DVid_AdjustContrast (vid_contrast.value, d3d_CurrentGamma.r[i]);
		d3d_CurrentGamma.g[i] = D3DVid_AdjustContrast (vid_contrast.value, d3d_CurrentGamma.g[i]);
		d3d_CurrentGamma.b[i] = D3DVid_AdjustContrast (vid_contrast.value, d3d_CurrentGamma.b[i]);
	}

	// and again with the r/g/b
	for (int i = 0; i < 256; i++)
	{
		d3d_CurrentGamma.r[i] = D3DVid_AdjustContrast (r_contrast.value, d3d_CurrentGamma.r[i]);
		d3d_CurrentGamma.g[i] = D3DVid_AdjustContrast (g_contrast.value, d3d_CurrentGamma.g[i]);
		d3d_CurrentGamma.b[i] = D3DVid_AdjustContrast (b_contrast.value, d3d_CurrentGamma.b[i]);
	}

	VID_SetAppGamma ();
}


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

		// yield CPU for a while
		Sleep (1);

		// see if the device can be recovered
		hr = d3d_Device->TestCooperativeLevel ();

		switch (hr)
		{
		case D3D_OK:
			// recover device resources and bring states back to defaults
			D3DVid_RecoverDeviceResources ();

			// the device is no longer lost
			Con_DPrintf ("recovered lost device\n");

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


void D3DVid_BeginRendering (void)
{
	// video is not restarted by default
	vid_restarted = false;

	// check if a restart request has been queued; run it if so, and optionally skip the rest of this frame
	if (vid_queuerestart)
	{
		// fixme - move device creation to first time through here...?
		vid.recalc_refdef = 1;
		D3DVid_ResizeToDimension (vid_width.integer, vid_height.integer);
		D3DVid_CheckVidMode ();
		D3DVid_Restart_f ();
		vid_queuerestart = false;
		return;
	}

	if (gl_finish.integer)
	{
		if (!d3d_FinishQuery)
		{
			if (FAILED (d3d_Device->CreateQuery (D3DQUERYTYPE_EVENT, &d3d_FinishQuery)))
			{
				// don't gl_finish if we couldn;t create a query to drain the command buffer
				Cvar_Set (&gl_finish, 0.0f);
			}
		}

		if (d3d_FinishQuery)
		{
			d3d_FinishQuery->Issue (D3DISSUE_END);

			while (d3d_FinishQuery->GetData (NULL, 0, D3DGETDATA_FLUSH) == S_FALSE);
		}
	}

	// get access to the swap chain if we don't have it
	if (!d3d_SwapChain) d3d_Device->GetSwapChain (0, &d3d_SwapChain);

	// force lighting calcs off; this is done every frame and it will be filtered if necessary
	D3D_SetRenderState (D3DRS_LIGHTING, FALSE);
	D3D_SetRenderState (D3DRS_CLIPPING, TRUE);

	// this is called before d3d_Device->BeginScene so that the d3d_Device->BeginScene is called on the correct rendertarget
	D3DRTT_BeginScene ();

	// SCR_UpdateScreen assumes that this is the last call in this function
	hr = d3d_Device->BeginScene ();
}


void D3DVid_EndRendering (void)
{
	// unbind everything
	D3D_UnbindStreams ();
	D3D_SetVertexDeclaration (NULL);

	// count frames here so that we match present calls with the actual real framerate
	d3d_Device->EndScene ();
	d3d_RenderDef.framecount++;

	// wtf?  presenting through the swap chain is faster?  ok, that's cool
	hr = d3d_SwapChain->Present (NULL, NULL, NULL, NULL, 0);

	if (hr == D3DERR_DEVICELOST)
		D3DVid_RecoverLostDevice ();
}


