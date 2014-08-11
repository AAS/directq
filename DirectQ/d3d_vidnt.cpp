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
#include "d3d_model.h"
#include "d3d_quake.h"

#include "winquake.h"
#include "resource.h"
#include <commctrl.h>

void D3D_SetDefaultStates (void);
void D3D_SetAllStates (void);

D3DTEXTUREFILTERTYPE d3d_3DFilterMin = D3DTEXF_LINEAR;
D3DTEXTUREFILTERTYPE d3d_3DFilterMag = D3DTEXF_LINEAR;
D3DTEXTUREFILTERTYPE d3d_3DFilterMip = D3DTEXF_LINEAR;


// declarations that don't really fit in anywhere else
LPDIRECT3D9 d3d_Object = NULL;
LPDIRECT3DDEVICE9 d3d_Device = NULL;

// entrypoints for d3d
// because we don't know which version of d3d9 is going to be on a target machine we load dynamically
HINSTANCE hInstD3D9 = NULL;
HINSTANCE hInstD3DX = NULL;

FARPROC D3DX_GetProcAddress (char *procname)
{
	FARPROC proc = NULL;

	if (hInstD3DX && procname)
		proc = GetProcAddress (hInstD3DX, procname);

	return proc;
}


DIRECT3DCREATE9PROC QDirect3DCreate9 = NULL;
D3DXMATRIXPERSPECTIVEFOVRHPROC QD3DXMatrixPerspectiveFovRH = NULL;
D3DXCREATEEFFECTPROC QD3DXCreateEffect = NULL;
D3DXLOADSURFACEFROMSURFACEPROC QD3DXLoadSurfaceFromSurface = NULL;
D3DXMATRIXPERSPECTIVEOFFCENTERRHPROC QD3DXMatrixPerspectiveOffCenterRH = NULL;
D3DXMATRIXORTHOOFFCENTERPROC QD3DXMatrixOrthoOffCenterRH = NULL;
D3DXMATRIXMULTIPLYPROC QD3DXMatrixMultiply = NULL;
D3DXMATRIXSCALINGPROC QD3DXMatrixScaling = NULL;
D3DXMATRIXTRANSLATIONPROC QD3DXMatrixTranslation = NULL;
D3DXMATRIXROTATIONXPROC QD3DXMatrixRotationX = NULL;
D3DXMATRIXROTATIONYPROC QD3DXMatrixRotationY = NULL;
D3DXMATRIXROTATIONZPROC QD3DXMatrixRotationZ = NULL;
D3DXLOADSURFACEFROMMEMORYPROC QD3DXLoadSurfaceFromMemory = NULL;
D3DXFILTERTEXTUREPROC QD3DXFilterTexture = NULL;
D3DXGETPIXELSHADERPROFILEPROC QD3DXGetPixelShaderProfile = NULL;
D3DXGETVERTEXSHADERPROFILEPROC QD3DXGetVertexShaderProfile = NULL;
D3DXSAVESURFACETOFILEPROC QD3DXSaveSurfaceToFileA = NULL;
D3DXCREATETEXTUREFROMFILEINMEMORYEXPROC QD3DXCreateTextureFromFileInMemoryEx = NULL;
D3DXCREATETEXTUREFROMRESOURCEEXAPROC QD3DXCreateTextureFromResourceExA = NULL;
D3DXCREATERENDERTOSURFACEPROC QD3DXCreateRenderToSurface = NULL;

// this needs to default to 42 and be capped at that level as using d3dx versions later than the SDK we compile with it problematical
cvar_t d3dx_version ("d3dx_version", 42, CVAR_ARCHIVE);

void D3D_LoadD3DXVersion (int ver)
{
	// no versions of the dll below 24 exist, but DirectQ requires 32 to compile it's shaders so prevent attempts to use anything lower.
	// a version of -1 (or any other negative number) can be used for a "best available" 
	if (ver < 0) ver = 42;
	if (ver < 32) ver = 32;

	// never go higher than 42 - see above
	if (ver > 42) ver = 42;

	// select the best version of d3dx available to us
	for (int i = ver; i; i--)
	{
		UNLOAD_LIBRARY (hInstD3DX);
		hInstD3DX = LoadLibrary (va ("d3dx9_%i.dll", i));

		if (hInstD3DX)
		{
			// it's known-good that these all have entrypoints in d3dx9 up to version 42, but who knows about potential future versions?
			if (!(QD3DXMatrixPerspectiveFovRH = (D3DXMATRIXPERSPECTIVEFOVRHPROC) D3DX_GetProcAddress ("D3DXMatrixPerspectiveFovRH"))) continue;
			if (!(QD3DXCreateEffect = (D3DXCREATEEFFECTPROC) D3DX_GetProcAddress ("D3DXCreateEffect"))) continue;
			if (!(QD3DXLoadSurfaceFromSurface = (D3DXLOADSURFACEFROMSURFACEPROC) D3DX_GetProcAddress ("D3DXLoadSurfaceFromSurface"))) continue;
			if (!(QD3DXMatrixPerspectiveOffCenterRH = (D3DXMATRIXPERSPECTIVEOFFCENTERRHPROC) D3DX_GetProcAddress ("D3DXMatrixPerspectiveOffCenterRH"))) continue;
			if (!(QD3DXMatrixOrthoOffCenterRH = (D3DXMATRIXORTHOOFFCENTERPROC) D3DX_GetProcAddress ("D3DXMatrixOrthoOffCenterRH"))) continue;
			if (!(QD3DXMatrixMultiply = (D3DXMATRIXMULTIPLYPROC) D3DX_GetProcAddress ("D3DXMatrixMultiply"))) continue;
			if (!(QD3DXMatrixScaling = (D3DXMATRIXSCALINGPROC) D3DX_GetProcAddress ("D3DXMatrixScaling"))) continue;
			if (!(QD3DXMatrixTranslation = (D3DXMATRIXTRANSLATIONPROC) D3DX_GetProcAddress ("D3DXMatrixTranslation"))) continue;
			if (!(QD3DXMatrixRotationX = (D3DXMATRIXROTATIONXPROC) D3DX_GetProcAddress ("D3DXMatrixRotationX"))) continue;
			if (!(QD3DXMatrixRotationY = (D3DXMATRIXROTATIONYPROC) D3DX_GetProcAddress ("D3DXMatrixRotationY"))) continue;
			if (!(QD3DXMatrixRotationZ = (D3DXMATRIXROTATIONZPROC) D3DX_GetProcAddress ("D3DXMatrixRotationZ"))) continue;
			if (!(QD3DXLoadSurfaceFromMemory = (D3DXLOADSURFACEFROMMEMORYPROC) D3DX_GetProcAddress ("D3DXLoadSurfaceFromMemory"))) continue;
			if (!(QD3DXFilterTexture = (D3DXFILTERTEXTUREPROC) D3DX_GetProcAddress ("D3DXFilterTexture"))) continue;
			if (!(QD3DXGetPixelShaderProfile = (D3DXGETPIXELSHADERPROFILEPROC) D3DX_GetProcAddress ("D3DXGetPixelShaderProfile"))) continue;
			if (!(QD3DXGetVertexShaderProfile = (D3DXGETVERTEXSHADERPROFILEPROC) D3DX_GetProcAddress ("D3DXGetVertexShaderProfile"))) continue;
			if (!(QD3DXSaveSurfaceToFileA = (D3DXSAVESURFACETOFILEPROC) D3DX_GetProcAddress ("D3DXSaveSurfaceToFileA"))) continue;
			if (!(QD3DXCreateTextureFromFileInMemoryEx = (D3DXCREATETEXTUREFROMFILEINMEMORYEXPROC) D3DX_GetProcAddress ("D3DXCreateTextureFromFileInMemoryEx"))) continue;
			if (!(QD3DXCreateTextureFromResourceExA = (D3DXCREATETEXTUREFROMRESOURCEEXAPROC) D3DX_GetProcAddress ("D3DXCreateTextureFromResourceExA"))) continue;
			if (!(QD3DXCreateRenderToSurface = (D3DXCREATERENDERTOSURFACEPROC) D3DX_GetProcAddress ("D3DXCreateRenderToSurface"))) continue;

			// done
			Con_SafePrintf ("Loaded D3DX version %i (d3dx9_%i.dll)\n", i, i);
			Cvar_Set (&d3dx_version, i);
			return;
		}
	}

	// no d3dx9 available (this should never happen)
	UNLOAD_LIBRARY (hInstD3DX);
	Sys_Error ("Couldn't load D3DX version %i (d3dx9_%i.dll)\n\nConsider upgrading or installing/reinstalling DirectX 9.", ver, ver);
}

D3DADAPTER_IDENTIFIER9 d3d_Adapter;
D3DCAPS9 d3d_DeviceCaps;
d3d_global_caps_t d3d_GlobalCaps;
D3DPRESENT_PARAMETERS d3d_PresentParams;

// for various render-to-texture and render-to-surface stuff
LPDIRECT3DSURFACE9 d3d_BackBuffer = NULL;

bool d3d_SceneBegun = false;

void D3D_Init3DSceneTexture (void);
void D3D_Kill3DSceneTexture (void);


// global video state
viddef_t	vid;

// quake palette
unsigned	d_8to24table[256];

// luma map palette
unsigned	lumatable[256];

// window state management
bool d3d_DeviceLost = false;
bool vid_canalttab = false;
bool vid_initialized = false;
bool scr_skipupdate;


// RotateAxisLocal requires instantiating a class and filling it's members just to pass
// 3 floats to it!  These little babies avoid that silly nonsense.  SORT IT OUT MICROSOFT!!!
D3DXVECTOR3 XVECTOR (1, 0, 0);
D3DXVECTOR3 YVECTOR (0, 1, 0);
D3DXVECTOR3 ZVECTOR (0, 0, 1);

// forward declarations of video menu functions
void VID_MenuDraw (void);
void VID_MenuKey (int key);

// for building the menu after video comes up
void Menu_VideoBuild (void);

// hlsl
void D3D_InitHLSL (void);
void D3D_ShutdownHLSL (void);

// lightmaps
void D3D_LoseLightmapResources (void);
void D3D_RecoverLightmapResources (void);

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

cvar_t gl_triplebuffer ("gl_triplebuffer", 1, CVAR_ARCHIVE);

d3d_ModeDesc_t *d3d_ModeList = NULL;

D3DDISPLAYMODE d3d_DesktopMode;
D3DDISPLAYMODE d3d_CurrentMode;

d3d_ModeDesc_t d3d_BadMode = {{666, 666, 666, D3DFMT_UNKNOWN}, false, 0, -1, "Bad Mode", NULL};


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



DWORD D3D_RenderThread (LPVOID lpParam)
{
	while (1)
	{
	}

	return 0;
}


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
	}

	// update cursor clip region
	IN_UpdateClipCursor ();
}


D3DFORMAT D3D_GetDepthStencilFormat (D3DDISPLAYMODE *mode)
{
	D3DFORMAT ModeFormat = mode->Format;

	if (ModeFormat == D3DFMT_UNKNOWN) ModeFormat = d3d_DesktopMode.Format;

	// prefer faster formats (but prefer to get a stencil buffer)
	D3DFORMAT DepthStencilFormats[] = {D3DFMT_D24S8, D3DFMT_D24X8, D3DFMT_D16, D3DFMT_UNKNOWN};

	for (int i = 0;; i++)
	{
		// ran out of formats
		if (DepthStencilFormats[i] == D3DFMT_UNKNOWN) break;

		// check that the format exists
		hr = d3d_Object->CheckDeviceFormat
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


DWORD D3D_GetPresentInterval (void)
{
	if (vid_vsync.integer)
	{
		if (d3d_DeviceCaps.PresentationIntervals & D3DPRESENT_INTERVAL_ONE)
			return D3DPRESENT_INTERVAL_ONE;
		else D3DPRESENT_INTERVAL_IMMEDIATE;
	}
	else
	{
		if (d3d_DeviceCaps.PresentationIntervals & D3DPRESENT_INTERVAL_IMMEDIATE)
			return D3DPRESENT_INTERVAL_IMMEDIATE;
		else D3DPRESENT_INTERVAL_ONE;
	}

	// shut up compiler
	return D3DPRESENT_INTERVAL_IMMEDIATE;
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
	pp->PresentationInterval = D3D_GetPresentInterval ();
	pp->BackBufferCount = (gl_triplebuffer.integer && d3d_GlobalCaps.supportTripleBuffer) ? 2 : 1;
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
	else Con_Printf ("%d video modes are available\n", nummodes);
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


void D3D_PreloadTextures (void);

void D3D_RecoverDeviceResources (void)
{
	// recreate anything that needs to be recreated
	D3D_Init3DSceneTexture ();
	D3D_InitHLSL ();
	D3D_RecoverLightmapResources ();

	// recover all states back to what they should be
	D3D_SetAllStates ();

	// pull textures back to vram
	D3D_PreloadTextures ();

	// force a recalc of the refdef
	vid.recalc_refdef = true;
}


void D3D_ClearOcclusionQueries (void);

void D3D_LoseDeviceResources (void)
{
	// release anything that needs to be released
	D3D_ClearOcclusionQueries ();
	D3D_Kill3DSceneTexture ();
	D3D_VBOReleaseBuffers ();
	D3D_ShutdownHLSL ();
	D3D_LoseLightmapResources ();

	// ensure that present params are valid
	D3D_SetPresentParams (&d3d_PresentParams, &d3d_CurrentMode);
}


void Host_GetConsoleCommands (void);

void D3D_VidRestart_f (void)
{
	// make sure that we're ready to reset
	while (true)
	{
		Sleep (10);
		hr = d3d_Device->TestCooperativeLevel ();

		if (hr == D3D_OK) break;
	}

	// release anything that needs to be released
	D3D_LoseDeviceResources ();

	// reset the device
	hr = d3d_Device->Reset (&d3d_PresentParams);

	// if we're going to a fullscreen mode we need to handle the mouse properly
	if (!d3d_PresentParams.Windowed)
	{
		IN_ActivateMouse ();
		IN_ShowMouse (FALSE);
	}

	if (FAILED (hr))
	{
		// a failed reset causes a lost device
		d3d_DeviceLost = true;
		Con_Printf ("D3D_VidRestart_f: Unable to Reset Device.\n");
		return;
	}

	// make sure that the reset has completed
	while (true)
	{
		Sleep (10);
		hr = d3d_Device->TestCooperativeLevel ();

		if (hr == D3D_OK) break;
	}

	// bring back anything that needs to be brought back
	D3D_RecoverDeviceResources ();
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
				d3d_ModeList = (d3d_ModeDesc_t *) Pool_Permanent->Alloc (sizeof (d3d_ModeDesc_t));
				newmode = d3d_ModeList;
			}
			else
			{
				// add it to the end of the list
				for (newmode = d3d_ModeList;; newmode = newmode->Next)
					if (!newmode->Next)
						break;

				newmode->Next = (d3d_ModeDesc_t *) Pool_Permanent->Alloc (sizeof (d3d_ModeDesc_t));
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
			strcpy (newmode->ModeDesc, D3DTypeToString (d3d_AdapterModeDescs[m]));

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
	d3d_ModeDesc_t *WindowedModes = (d3d_ModeDesc_t *) Pool_Permanent->Alloc (NumWindowedModes * sizeof (d3d_ModeDesc_t));
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


void D3D_InfoDump_f (void)
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


cmd_t d3d_InfoDump_Cmd ("d3d_infodump", D3D_InfoDump_f);


void D3D_GetMaxBackBuffers (D3DDISPLAYMODE *mode)
{
	// fill in with valid values
	D3D_SetPresentParams (&d3d_PresentParams, mode);

	// D3D allows 1, 2 or 3 backbuffers so try them all
	// (the documentation claims that the count will be filled in with a valid value after one failure.  it's lying)
	for (int i = 3; i; i--)
	{
		d3d_PresentParams.BackBufferCount = i;

		// attempt to create with this number of backbuffers
		// (Quark ETP needs D3DCREATE_FPU_PRESERVE)
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
			// clear to black immediately so that we don't start up with a blank window
			d3d_Device->Clear (0, NULL, D3DCLEAR_TARGET, 0x00000000, 1.0f, 1);
			d3d_Device->Present (NULL, NULL, NULL, NULL);

			// this is the max number of backbuffers the device supports
			// release the device, set a global cap, and get out
			SAFE_RELEASE (d3d_Device);

			if (i == 1)
				d3d_GlobalCaps.supportTripleBuffer = false;
			else d3d_GlobalCaps.supportTripleBuffer = true;
			return;
		}
	}

	// if we get to here we have a bad device
	Sys_Error ("Could not create device");
}


void D3D_GetVideoRAM (D3DDISPLAYMODE *mode)
{
	D3D_SetPresentParams (&d3d_PresentParams, mode);

	d3d_PresentParams.AutoDepthStencilFormat = D3DFMT_UNKNOWN;
	d3d_PresentParams.BackBufferCount = 0;
	d3d_PresentParams.BackBufferFormat = D3DFMT_UNKNOWN;
	d3d_PresentParams.BackBufferHeight = 10;
	d3d_PresentParams.BackBufferWidth = 10;
	d3d_PresentParams.EnableAutoDepthStencil = FALSE;
	d3d_PresentParams.Flags = 0;
	d3d_PresentParams.FullScreen_RefreshRateInHz = 0;
	d3d_PresentParams.Windowed = TRUE;

	// (Quark ETP needs D3DCREATE_FPU_PRESERVE)
	hr = d3d_Object->CreateDevice
	(
		D3DADAPTER_DEFAULT,
		D3DDEVTYPE_HAL,
		d3d_Window,
		D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE,
		&d3d_PresentParams,
		&d3d_Device
	);

	if (FAILED (hr))
	{
		d3d_GlobalCaps.videoRAMMB = -1;
		return;
	}

	d3d_GlobalCaps.videoRAMMB = (d3d_Device->GetAvailableTextureMem ()) / (1024 * 1024);
	SAFE_RELEASE (d3d_Device);
}


void D3D_TexMem_f (void)
{
	Con_Printf ("Available Texture Memory: %i MB\n", (d3d_Device->GetAvailableTextureMem ()) / (1024 * 1024));
}


cmd_t D3D_TexMem_Cmd ("gl_videoram", D3D_TexMem_f);

void D3D_InitDirect3D (D3DDISPLAYMODE *mode)
{
	// get a more accurate count of video ram
	D3D_GetVideoRAM (mode);

	// get the kind of capabilities we can expect from a HAL device ("hello Dave")
	hr = d3d_Object->GetDeviceCaps (D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, &d3d_DeviceCaps);

	if (FAILED (hr)) Sys_Error ("D3D_InitDirect3D: Failed to retrieve object caps\n(No HAL D3D Device Available)");

	// check for basic required capabilities ("your name's not down, you're not coming in")
	// if (d3d_DeviceCaps.MaxStreams < 1) Sys_Error ("You must have a Direct3D 9 driver to run DirectQ");	// not a factor any more
	if (!(d3d_DeviceCaps.DevCaps & D3DDEVCAPS_DRAWPRIMITIVES2EX)) Sys_Error ("You need at least a DirectX 7-compliant device to run DirectQ");
	if (!(d3d_DeviceCaps.DevCaps & D3DDEVCAPS_HWRASTERIZATION)) Sys_Error ("You need a hardware-accelerated device to run DirectQ");
	if (!(d3d_DeviceCaps.TextureCaps & D3DPTEXTURECAPS_MIPMAP)) Sys_Error ("You need a device that supports mipmapping to run DirectQ");

	// check for required texture op caps
	if (!(d3d_DeviceCaps.TextureOpCaps & D3DTEXOPCAPS_ADD)) Sys_Error ("You need a device that supports D3DTEXOPCAPS_ADD to run DirectQ");
	if (!(d3d_DeviceCaps.TextureOpCaps & D3DTEXOPCAPS_DISABLE)) Sys_Error ("You need a device that supports D3DTEXOPCAPS_DISABLE to run DirectQ");
	if (!(d3d_DeviceCaps.TextureOpCaps & D3DTEXOPCAPS_MODULATE)) Sys_Error ("You need a device that supports D3DTEXOPCAPS_MODULATE to run DirectQ");
	if (!(d3d_DeviceCaps.TextureOpCaps & D3DTEXOPCAPS_MODULATE2X)) Sys_Error ("You need a device that supports D3DTEXOPCAPS_MODULATE2X to run DirectQ");
	if (!(d3d_DeviceCaps.TextureOpCaps & D3DTEXOPCAPS_MODULATE4X)) Sys_Error ("You need a device that supports D3DTEXOPCAPS_MODULATE4X to run DirectQ");
	if (!(d3d_DeviceCaps.TextureOpCaps & D3DTEXOPCAPS_SELECTARG1)) Sys_Error ("You need a device that supports D3DTEXOPCAPS_SELECTARG1 to run DirectQ");
	if (!(d3d_DeviceCaps.TextureOpCaps & D3DTEXOPCAPS_SELECTARG2)) Sys_Error ("You need a device that supports D3DTEXOPCAPS_SELECTARG2 to run DirectQ");
	if (!(d3d_DeviceCaps.TextureOpCaps & D3DTEXOPCAPS_BLENDTEXTUREALPHA)) Sys_Error ("You need a device that supports D3DTEXOPCAPS_BLENDTEXTUREALPHA to run DirectQ");

	// check for texture addressing modes
	if (!(d3d_DeviceCaps.TextureAddressCaps & D3DPTADDRESSCAPS_CLAMP)) Sys_Error ("You need a device that supports D3DPTADDRESSCAPS_CLAMP to run DirectQ");
	if (!(d3d_DeviceCaps.TextureAddressCaps & D3DPTADDRESSCAPS_WRAP)) Sys_Error ("You need a device that supports D3DPTADDRESSCAPS_WRAP to run DirectQ");

	// check for TMU support - Anything less than 2 TMUs is a 1st gen 3D card and doesn't stand a hope anyway...
	if (d3d_DeviceCaps.MaxTextureBlendStages < 2) Sys_Error ("You need a device with at least 2 TMUs to run DirectQ");
	if (d3d_DeviceCaps.MaxSimultaneousTextures < 2) Sys_Error ("You need a device with at least 2 TMUs to run DirectQ");

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
		d3d_GlobalCaps.deviceCreateFlags = D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_DISABLE_DRIVER_MANAGEMENT;
	}
	else
	{
		d3d_GlobalCaps.supportHardwareTandL = false;
		d3d_GlobalCaps.deviceCreateFlags = D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_DISABLE_DRIVER_MANAGEMENT;
	}

	// first of all we decide what formats and counts we can support for the raw device
	D3D_GetMaxBackBuffers (mode);

	// now reset the present params as they will have become messed up above
	D3D_SetPresentParams (&d3d_PresentParams, mode);

	// attempt to create the device - we can ditch all of the extra flags now :)
	// (Quark ETP needs D3DCREATE_FPU_PRESERVE - using _controlfp during texcoord gen doesn't work)
	// here as the generated coords will also lost precision when being applied
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
		Sys_Error ("D3D_InitDirect3D: IDirect3D9::CreateDevice failed");
		return;
	}

	if (d3d_GlobalCaps.supportHardwareTandL)
		Con_Printf ("Using Hardware Vertex Processing\n\n");
	else Con_Printf ("Using Software Vertex Processing\n\n");

	// report some caps
	Con_Printf ("Video mode %i (%ix%i) Initialized\n", d3d_mode.integer, mode->Width, mode->Height);
	Con_Printf ("Back Buffer Format: %s (created %i %s)\n", D3DTypeToString (mode->Format), d3d_PresentParams.BackBufferCount, d3d_PresentParams.BackBufferCount > 1 ? "backbuffers" : "backbuffer");
	Con_Printf ("Depth/Stencil format: %s\n", D3DTypeToString (d3d_PresentParams.AutoDepthStencilFormat));
	Con_Printf ("Refresh Rate: %i Hz (%s)\n", mode->RefreshRate, mode->RefreshRate ? "Fullscreen" : "Windowed");
	Con_Printf ("\n");

	// clear to black immediately
	d3d_Device->Clear (0, NULL, D3DCLEAR_TARGET, 0x00000000, 1.0f, 1);
	d3d_Device->Present (NULL, NULL, NULL, NULL);

	// get capabilities on the actual device
	hr = d3d_Device->GetDeviceCaps (&d3d_DeviceCaps);

	if (FAILED (hr))
	{
		Sys_Error ("D3D_InitDirect3D: Failed to retrieve device caps");
		return;
	}

	if (d3d_GlobalCaps.videoRAMMB < 0)
		d3d_GlobalCaps.videoRAMMB = (d3d_Device->GetAvailableTextureMem ()) / (1024 * 1024);

	// report on selected ones
	Con_Printf ("Available Texture Memory: %i MB\n", d3d_GlobalCaps.videoRAMMB);
	Con_Printf ("Maximum Texture Blend Stages: %i\n", d3d_DeviceCaps.MaxTextureBlendStages);
	Con_Printf ("Maximum Simultaneous Textures: %i\n", d3d_DeviceCaps.MaxSimultaneousTextures);
	Con_Printf ("Maximum Texture Size: %i x %i\n", d3d_DeviceCaps.MaxTextureWidth, d3d_DeviceCaps.MaxTextureHeight);
	Con_Printf ("Maximum Anisotropic Filter: %i\n", d3d_DeviceCaps.MaxAnisotropy);
	Con_Printf ("\n");

	// tmus; take the lower of what's available as we need to work with them all
	d3d_GlobalCaps.NumTMUs = 666;

	if (d3d_DeviceCaps.MaxTextureBlendStages < d3d_GlobalCaps.NumTMUs) d3d_GlobalCaps.NumTMUs = d3d_DeviceCaps.MaxTextureBlendStages;
	if (d3d_DeviceCaps.MaxSimultaneousTextures < d3d_GlobalCaps.NumTMUs) d3d_GlobalCaps.NumTMUs = d3d_DeviceCaps.MaxSimultaneousTextures;

	// check for availability of rgb texture formats (ARGB is required by Quake)
	d3d_GlobalCaps.supportARGB = D3D_CheckTextureFormat (D3DFMT_A8R8G8B8, TRUE);
	d3d_GlobalCaps.supportXRGB = D3D_CheckTextureFormat (D3DFMT_X8R8G8B8, FALSE);

	// check for availability of alpha/luminance formats
	// unused d3d_GlobalCaps.supportL8 = D3D_CheckTextureFormat (D3DFMT_L8, FALSE);
	d3d_GlobalCaps.supportA8L8 = D3D_CheckTextureFormat (D3DFMT_A8L8, FALSE);

	// check for availability of compressed texture formats
	d3d_GlobalCaps.supportDXT1 = D3D_CheckTextureFormat (D3DFMT_DXT1, FALSE);
	d3d_GlobalCaps.supportDXT3 = D3D_CheckTextureFormat (D3DFMT_DXT3, FALSE);
	d3d_GlobalCaps.supportDXT5 = D3D_CheckTextureFormat (D3DFMT_DXT5, FALSE);

	// check for availability of occlusion queries
	LPDIRECT3DQUERY9 testOcclusion = NULL;

	if (SUCCEEDED (d3d_Device->CreateQuery (D3DQUERYTYPE_OCCLUSION, &testOcclusion)))
	{
		Con_Printf ("Using Occlusion Queries\n");
		d3d_GlobalCaps.supportOcclusion = true;
		SAFE_RELEASE (testOcclusion);
	}
	else d3d_GlobalCaps.supportOcclusion = false;

	// set up everything else
	D3D_Init3DSceneTexture ();
	D3D_InitHLSL ();

	Con_Printf ("\n");

	// set default states
	D3D_SetDefaultStates ();

	// set the d3d_mode cvar correctly
	D3D_SetVidMode ();

	// build the rest of the video menu (deferred to here as it's dependent on video being up)
	Menu_VideoBuild ();
}


void D3D_CreateWindow (D3DDISPLAYMODE *mode)
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

	rect.top = rect.left = 0;
	rect.right = mode->Width;
	rect.bottom = mode->Height;

	AdjustWindowRectEx (&rect, WindowStyle, FALSE, 0);

	int width = rect.right - rect.left;
	int height = rect.bottom - rect.top;

	d3d_Window = CreateWindowEx
	(
		ExWindowStyle,
		D3D_WINDOW_CLASS_NAME,
		va ("DirectQ Release %s", DIRECTQ_VERSION),
		WindowStyle,
		rect.left, rect.top,
		width,
		height,
		GetDesktopWindow (),
		NULL,
		GetModuleHandle (NULL),
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
	}

	ShowWindow (d3d_Window, SW_SHOWDEFAULT);
	UpdateWindow (d3d_Window);

	HICON hIcon = LoadIcon (GetModuleHandle (NULL), MAKEINTRESOURCE (IDI_APPICON));
	SendMessage (d3d_Window, WM_SETICON, (WPARAM) TRUE, (LPARAM) hIcon);
	SendMessage (d3d_Window, WM_SETICON, (WPARAM) FALSE, (LPARAM) hIcon);

	// set cursor clip region
	IN_UpdateClipCursor ();
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

	// retrieve and store the gamma ramp for the desktop
	HDC hdc = GetDC (NULL);
	GetDeviceGammaRamp (hdc, &d3d_DefaultGamma);
	ReleaseDC (NULL, hdc);

	// destroy our splash screen
	Splash_Destroy ();

	// create the mode and activate input
	D3D_CreateWindow (mode);
	IN_ActivateMouse ();
	IN_ShowMouse (FALSE);

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


static void PaletteFromColormap (byte *pal, byte *map)
{
	// set fullbright colour indexes
	for (int x = 0; x < 256; x++)
	{
		vid.fullbright[x] = true;

		for (int y = 1; y < VID_GRADES; y++)
		{
			int y1 = y - 1;

			if (map[y * 256 + x] != map[y1 * 256 + x])
			{
				vid.fullbright[x] = false;
				break;
			}
		}
	}

	// colour 255 (alpha) is never fullbright
	vid.fullbright[255] = false;

	float lmgrades[256];
	float maxgrade = 0;

	// average the colormap intensities to obtain a lightmap table
	// also do a reversed order (see below)
	for (int y = 0, rev = VID_GRADES - 1; y < VID_GRADES; y++, rev--)
	{
		int numcolors = 0;
		int avgr = 0;
		int avgg = 0;
		int avgb = 0;

		for (int x = 0; x < 256; x++)
		{
			// exclude columns with the same intensity all the way down
			if (vid.fullbright[x]) continue;

			// accumulate a new colour
			numcolors++;

			int thispal = map[y * 256 + x] * 3;
			int avgpal = map[31 * 256 + x] * 3;

#if 1
			// the colormap is premultiplied colour * intensity, so recover the original intensity used
			if (pal[avgpal + 0] > 0) avgr += (pal[thispal + 0] * 255) / pal[avgpal + 0];
			if (pal[avgpal + 1] > 0) avgg += (pal[thispal + 1] * 255) / pal[avgpal + 1];
			if (pal[avgpal + 2] > 0) avgb += (pal[thispal + 2] * 255) / pal[avgpal + 2];
#else
			avgr += pal[thispal + 0];
			avgg += pal[thispal + 1];
			avgb += pal[thispal + 2];
#endif
		}

		if (!numcolors) continue;

		// store this grade; right now we're just storing temp data so we don't need to worry
		// about doing it right just yet.
		// the colormap goes from light to dark so reverse the order
		lmgrades[rev * 4] = (float) (avgr * 30 + avgg * 59 + avgb * 11) / (float) (10 * numcolors);
	}

	// interpolate into vid.lightmap
	for (int i = 2; i < 255; i += 4) lmgrades[i] = (lmgrades[i - 2] + lmgrades[i + 2]) / 2.0f;
	for (int i = 1; i < 255; i += 2) lmgrades[i] = (lmgrades[i - 1] + lmgrades[i + 1]) / 2.0f;
	for (int i = 253; i < 256; i++) lmgrades[i] = (lmgrades[i - 1] * lmgrades[i - 2]) / lmgrades[i - 3];

	for (int i = 0; i < 256; i++)
	{
		int grade = (lmgrades[i] / lmgrades[255]) * 255.0f;
		vid.lightmap[i] = BYTE_CLAMP (grade);
	}

	// now reset the palette
	byte basepal[768];
	memcpy (basepal, pal, 768);

	// take the midline of the colormap for the palette; we *could* average the
	// entire column for each colour, but we lose some palette colours that way
	for (int i = 0; i < 256; i++)
	{
		int cmap1 = map[31 * 256 + i];
		int cmap2 = map[32 * 256 + i];
		int paltotal = 0;

		for (int c = 0; c < 3; c++)
		{
			float f = ((float) basepal[cmap1 * 3 + c] + (float) basepal[cmap2 * 3 + c]) / 2.0f;
			pal[i * 3 + c] = BYTE_CLAMP ((int) f);
			paltotal += pal[i * 3 + c];
		}

		// don't set dark colours to fullbright
		if (paltotal < 21) vid.fullbright[i] = false;

		// set correct luma table
		if (vid.fullbright[i])
			lumatable[i] = 0x80808080;
		else lumatable[i] = 0;
	}

	// for avoidance of the pink fringe effect
	pal[765] = pal[766] = pal[767] = 0;

	// entry 255 is not a luma
	vid.fullbright[255] = false;
	lumatable[255] = 0;
}


static void Check_Gamma (unsigned char *pal)
{
	int i;

	// set -gamma properly (the config will be up by now so this will override whatever was in it)
	if ((i = COM_CheckParm ("-gamma"))) Cvar_Set (&v_gamma, atof (com_argv[i + 1]));

	// apply default Quake gamma to texture palette
	for (i = 0; i < 768; i++)
	{
		// this calculation is IMPORTANT for retaining the full colour range of a LOT of
		// Quake 1 textures which gets LOST otherwise.
		float f = pow ((float) ((pal[i] + 1) / 256.0), 0.7f);

		// note: + 0.5f is IMPORTANT here for retaining a LOT of the visual look of Quake
		int inf = (f * 255.0f + 0.5f);

		// store back
		pal[i] = BYTE_CLAMP (inf);
	}
}


void VID_SetPalette (unsigned char *palette, byte *dst)
{
	// entry 255 is alpha, set to black to avoid pink fringes around stuff
	((unsigned *) dst)[255] = 0;

	// only go 0 to 254 here
	for (int i = 0; i < 255; i++)
	{
		// BGRA format for D3D
		dst[2] = palette[i * 3 + 0];
		dst[1] = palette[i * 3 + 1];
		dst[0] = palette[i * 3 + 2];
		dst[3] = 255;
		dst += 4;
	}
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
	PaletteFromColormap (palette, vid.colormap);
	Check_Gamma (palette);
	VID_SetPalette (palette, (byte *) d_8to24table);

	vid_canalttab = true;

	// dump the colormap out to file so that we can have a look-see at what's in it
	// SCR_WriteDataToTGA ("colormap.tga", vid.colormap, 256, 64, 8, 24);

	hInstD3D9 = LoadLibrary ("d3d9.dll");

	if (!hInstD3D9)
	{
		Sys_Error ("D3D_InitDirect3D - failed to load Direct3D!");
		return;
	}

	// create a regular XPDM object
	QDirect3DCreate9 = (DIRECT3DCREATE9PROC) GetProcAddress (hInstD3D9, "Direct3DCreate9");

	if (!QDirect3DCreate9)
	{
		Sys_Error ("D3D_InitDirect3D - GetProcAddress failed for Direct3DCreate9");
		return;
	}

	// this is always 32 irrespective of which version of the sdk we use
	if (!(d3d_Object = QDirect3DCreate9 (D3D_SDK_VERSION)))
	{
		Sys_Error ("D3D_InitDirect3D - failed to initialize Direct3D!");
		return;
	}

	// enumerate available modes
	D3D_EnumerateVideoModes ();

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

	// load d3dx library - directq has been tested with versions up to 42
	D3D_LoadD3DXVersion (d3dx_version.integer);

	Con_Printf ("\n");

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

	// set the selected video mode
	D3D_SetVideoMode (&d3d_CurrentMode);
}


void Menu_PrintCenterWhite (int cy, char *str);

void D3D_ShutdownDirect3D (void)
{
	// clear the screen to black so that shutdown doesn't leave artefacts from the last SCR_UpdateScreen
	if (d3d_Device)
	{
		d3d_Device->Clear (0, NULL, D3DCLEAR_TARGET, 0x00000000, 1.0f, 1);
		d3d_Device->Present (NULL, NULL, NULL, NULL);
	}

	// also need these... ;)
	D3D_LoseDeviceResources ();

	// release anything that needs to be released
	D3D_ReleaseTextures ();

	// destroy the device and object
	SAFE_RELEASE (d3d_Device);
	SAFE_RELEASE (d3d_Object);

	// unload our libraries
	UNLOAD_LIBRARY (hInstD3DX);
	UNLOAD_LIBRARY (hInstD3D9);
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
		hr = d3d_Device->TestCooperativeLevel ();

		switch (hr)
		{
		case D3D_OK:
			// recover device resources and bring states back to defaults
			D3D_RecoverDeviceResources ();

			// the device is no longer lost
			d3d_DeviceLost = false;

			// return to normal rendering
			return true;

		case D3DERR_DEVICELOST:
			// the device cannot be recovered at this time
			break;

		case D3DERR_DEVICENOTRESET:
			// the device is ready to be reset
			D3D_LoseDeviceResources ();

			// reset the device
			d3d_Device->Reset (&d3d_PresentParams);
			break;

		case D3DERR_DRIVERINTERNALERROR:
		default:
			// something bad happened
			Sys_Quit ();
			break;
		}

		// not quite ready yet
		return false;
	}

	// device is not lost
	return true;
}


typedef struct d3d_filtermode_s
{
	char *name;
	D3DTEXTUREFILTERTYPE minfilter;
	D3DTEXTUREFILTERTYPE magfilter;
	D3DTEXTUREFILTERTYPE mipfilter;
} d3d_filtermode_t;

d3d_filtermode_t d3d_filtermodes[] =
{
	{"GL_NEAREST", D3DTEXF_POINT, D3DTEXF_POINT, D3DTEXF_NONE},
	{"GL_LINEAR", D3DTEXF_LINEAR, D3DTEXF_LINEAR, D3DTEXF_NONE},
	{"GL_NEAREST_MIPMAP_NEAREST", D3DTEXF_POINT, D3DTEXF_POINT, D3DTEXF_POINT},
	{"GL_LINEAR_MIPMAP_NEAREST", D3DTEXF_LINEAR, D3DTEXF_LINEAR, D3DTEXF_POINT},
	{"GL_NEAREST_MIPMAP_LINEAR", D3DTEXF_POINT, D3DTEXF_POINT, D3DTEXF_LINEAR},
	{"GL_LINEAR_MIPMAP_LINEAR", D3DTEXF_LINEAR, D3DTEXF_LINEAR, D3DTEXF_LINEAR},
};

void D3D_TextureMode_f (void)
{
	if (Cmd_Argc () == 1)
	{
		D3DTEXTUREFILTERTYPE minfilter = d3d_3DFilterMin;
		D3DTEXTUREFILTERTYPE magfilter = d3d_3DFilterMag;
		D3DTEXTUREFILTERTYPE mipfilter = d3d_3DFilterMip;

		if (minfilter == D3DTEXF_ANISOTROPIC) minfilter = D3DTEXF_LINEAR;
		if (magfilter == D3DTEXF_ANISOTROPIC) magfilter = D3DTEXF_LINEAR;

		for (int i = 0; i < 6; i++)
		{
			if (magfilter == d3d_filtermodes[i].magfilter && minfilter == d3d_filtermodes[i].minfilter && mipfilter == d3d_filtermodes[i].mipfilter)
			{
				Con_Printf ("%s\n", d3d_filtermodes[i].name);
				return;
			}
		}

		Con_Printf ("current filter is unknown???\n");
		return;
	}

	for (int i = 0; i < 6; i++)
	{
		if (!stricmp (d3d_filtermodes[i].name, Cmd_Argv (1)))
		{
			// reset filter
			d3d_3DFilterMin = d3d_filtermodes[i].minfilter;
			d3d_3DFilterMag = d3d_filtermodes[i].magfilter;
			d3d_3DFilterMip = d3d_filtermodes[i].mipfilter;

			if (r_anisotropicfilter.integer > 1)
			{
				if (d3d_3DFilterMin == D3DTEXF_LINEAR) d3d_3DFilterMin = D3DTEXF_ANISOTROPIC;
				if (d3d_3DFilterMag == D3DTEXF_LINEAR) d3d_3DFilterMag = D3DTEXF_ANISOTROPIC;
			}

			for (int s = 0; s < d3d_DeviceCaps.MaxTextureBlendStages; s++)
				D3D_SetTextureMipmap (s, d3d_3DFilterMag, d3d_3DFilterMin, d3d_3DFilterMip);

			Con_Printf ("Minification:  %s\n", D3DTypeToString (d3d_3DFilterMin));
			Con_Printf ("Magnification: %s\n", D3DTypeToString (d3d_3DFilterMag));
			Con_Printf ("Mipmap filter: %s\n", D3DTypeToString (d3d_3DFilterMip));
			return;
		}
	}

	Con_Printf ("bad filter name\n");
}


cmd_t D3D_TextureMode_Cmd ("gl_texturemode", D3D_TextureMode_f);

void D3D_CheckTextureFiltering (void)
{
	static int old_aniso = -1;
	int real_aniso;

	// check the cvar first for an early-out
	if (r_anisotropicfilter.integer == old_aniso) return;

	// get the real value from the cvar - users may enter any old crap manually!!!
	for (real_aniso = 1; real_aniso < r_anisotropicfilter.value; real_aniso <<= 1);

	// clamp it
	if (real_aniso < 1) real_aniso = 1;
	if (real_aniso > d3d_DeviceCaps.MaxAnisotropy) real_aniso = d3d_DeviceCaps.MaxAnisotropy;

	// store it back
	Cvar_Set (&r_anisotropicfilter, real_aniso);

	// no change
	if (real_aniso == old_aniso) return;

	// store out
	old_aniso = real_aniso;

	if (real_aniso == 1)
	{
		// regular linear filtering
		d3d_3DFilterMag = D3DTEXF_LINEAR;
		d3d_3DFilterMin = D3DTEXF_LINEAR;
		d3d_3DFilterMip = D3DTEXF_LINEAR;

		if (key_dest == key_console) Con_Printf ("Set Linear Filtering\n");
	}
	else
	{
		// anisotropic filtering (not enabled on the mip filter)
		d3d_3DFilterMag = D3DTEXF_ANISOTROPIC;
		d3d_3DFilterMin = D3DTEXF_ANISOTROPIC;
		d3d_3DFilterMip = D3DTEXF_LINEAR;

		if (key_dest == key_console) Con_Printf ("Set %i x Anisotropic Filtering\n", real_aniso);
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
	baseval >>= 8;

	// the same gamma calc as GLQuake had some "gamma creep" where DirectQ would gradually get brighter
	// the more it was run; this hopefully fixes it once and for all
	float f = pow ((float) baseval / 255.0f, (float) gammaval);
	float inf = f * 255 + 0.5;

	// return what we got
	return (BYTE_CLAMP ((int) inf)) << 8;
}


void D3D_SetActiveGamma (void)
{
	// apply v_gamma to all components
	for (int i = 0; i < 256; i++)
	{
		d3d_CurrentGamma.r[i] = D3D_AdjustGamma (v_gamma.value, d3d_DefaultGamma.r[i]);
		d3d_CurrentGamma.g[i] = D3D_AdjustGamma (v_gamma.value, d3d_DefaultGamma.g[i]);
		d3d_CurrentGamma.b[i] = D3D_AdjustGamma (v_gamma.value, d3d_DefaultGamma.b[i]);
	}

	// now apply r/g/b to the derived values
	for (int i = 0; i < 256; i++)
	{
		d3d_CurrentGamma.r[i] = D3D_AdjustGamma (r_gamma.value, d3d_CurrentGamma.r[i]);
		d3d_CurrentGamma.g[i] = D3D_AdjustGamma (g_gamma.value, d3d_CurrentGamma.g[i]);
		d3d_CurrentGamma.b[i] = D3D_AdjustGamma (b_gamma.value, d3d_CurrentGamma.b[i]);
	}

	VID_SetAppGamma ();
}


void D3D_CheckGamma (void)
{
	static int oldvgamma = 100;
	static int oldrgamma = 100;
	static int oldggamma = 100;
	static int oldbgamma = 100;

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

	D3D_SetActiveGamma ();
}


void D3D_CheckPixelShaders (void)
{
	// init this to force a check first time in
	static bool oldUsingPixelShaders = !d3d_GlobalCaps.usingPixelShaders;

	if (d3d_GlobalCaps.usingPixelShaders != oldUsingPixelShaders)
	{
		if (d3d_GlobalCaps.usingPixelShaders)
		{
			// Con_SafePrintf ("Enabled pixel shaders\n");
		}
		else
		{
			// switch shaders off and set fvf to something invalid to force an update
			d3d_Device->SetPixelShader (NULL);
			d3d_Device->SetVertexShader (NULL);
			D3D_SetFVF (D3DFVF_XYZ | D3DFVF_XYZRHW);
			// Con_SafePrintf ("Disabled pixel shaders\n");
		}

		// store back
		oldUsingPixelShaders = d3d_GlobalCaps.usingPixelShaders;
	}
}


void D3D_CheckTripleBuffer (void)
{
	if (!d3d_GlobalCaps.supportTripleBuffer) return;

	int oldtriplebuffer = gl_triplebuffer.integer;

	if (oldtriplebuffer != gl_triplebuffer.integer)
	{
		oldtriplebuffer = gl_triplebuffer.integer;
		D3D_VidRestart_f ();
	}
}


void D3D_CheckVSync (void)
{
	static int old_vsync = vid_vsync.integer;

	if (old_vsync != vid_vsync.integer)
	{
		old_vsync = vid_vsync.integer;
		D3D_VidRestart_f ();
	}
}


void D3D_CheckD3DXVersion (void)
{
	static int oldversion = d3dx_version.integer;

	if (oldversion != d3dx_version.integer)
	{
		// this needs a resource reload as otherwise the shaders will potentially be new versions
		D3D_LoseDeviceResources ();
		D3D_LoadD3DXVersion (d3dx_version.integer);
		D3D_RecoverDeviceResources ();
		oldversion = d3dx_version.integer;
	}
}


void D3D_BeginRendering (void)
{
	// check for device recovery and recover it if needed
	if (!D3D_CheckRecoverDevice ()) return;

	// check for any changes to any display properties
	D3D_CheckGamma ();
	D3D_CheckVidMode ();
	D3D_CheckTextureFiltering ();
	D3D_CheckPixelShaders ();
	D3D_CheckTripleBuffer ();
	D3D_CheckVSync ();
	D3D_CheckD3DXVersion ();

	// flag that we haven't begun the scene yet
	d3d_SceneBegun = false;
}


void D3D_EndRendering (void)
{
	// we might have some 2d stuff left over so draw that now
	D3D_EndFlatDraw ();

	// unbind vertex streams
	D3D_VBOSetVBOStream (NULL);

	// end the previous scene
	d3d_Device->EndScene ();

	hr = d3d_Device->Present (NULL, NULL, NULL, NULL);

	// check for a lost device
	if (hr == D3DERR_DEVICELOST)
	{
		d3d_DeviceLost = true;
		return;
	}

	// handle the mouse state for both fullscreen and windowed modes.
	// this ensures that it's always inactive and not buffering commands when
	// not being used in fullscreen modes.
	// the only difference for windowed modes is that the cursor is shown
	extern bool mouseactive;
	extern bool keybind_grab;
	RECT cliprect;

	if ((key_dest == key_menu || key_dest == key_console || cls.state != ca_connected) && !keybind_grab)
	{
		if (mouseactive)
		{
			IN_DeactivateMouse ();
			if (d3d_CurrentMode.RefreshRate == 0) IN_ShowMouse (TRUE);

			// recenter the cursor here so that it will be visible if windowed
			GetWindowRect (d3d_Window, &cliprect);
			SetCursorPos (cliprect.left + (cliprect.right - cliprect.left) / 2, cliprect.top + (cliprect.bottom - cliprect.top) / 2);
			mouseactive = false;
		}
	}
	else
	{
		if (!mouseactive)
		{
			// recenter the cursor here so that the next press will be valid
			GetWindowRect (d3d_Window, &cliprect);
			SetCursorPos (cliprect.left + (cliprect.right - cliprect.left) / 2, cliprect.top + (cliprect.bottom - cliprect.top) / 2);

			IN_ActivateMouse ();
			IN_ShowMouse (FALSE);
			mouseactive = true;
		}
	}
}

