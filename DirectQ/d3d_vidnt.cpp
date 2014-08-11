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
#include "d3d_vbo.h"

#include "winquake.h"
#include "resource.h"
#include <commctrl.h>

void D3D_SetDefaultStates (void);
void D3D_SetAllStates (void);

extern bool WinDWM;

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
D3DXDDECLARATORFROMFVFPROC QD3DXDeclaratorFromFVF = NULL;
D3DXOPTIMIZEFACEVERTPROC QD3DXOptimizeFaces = NULL;
D3DXOPTIMIZEFACEVERTPROC QD3DXOptimizeVertices = NULL;
D3DXCREATEMESHFVFPROC QD3DXCreateMeshFVF = NULL;
D3DXLOADSURFACEFROMFILEINMEMORYPROC QD3DXLoadSurfaceFromFileInMemory = NULL;
D3DXMATRIXINVERSEPROC QD3DXMatrixInverse = NULL;

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
			if (!(QD3DXDeclaratorFromFVF = (D3DXDDECLARATORFROMFVFPROC) D3DX_GetProcAddress ("D3DXDeclaratorFromFVF"))) continue;
			if (!(QD3DXOptimizeFaces = (D3DXOPTIMIZEFACEVERTPROC) D3DX_GetProcAddress ("D3DXOptimizeFaces"))) continue;
			if (!(QD3DXOptimizeVertices = (D3DXOPTIMIZEFACEVERTPROC) D3DX_GetProcAddress ("D3DXOptimizeVertices"))) continue;
			if (!(QD3DXCreateMeshFVF = (D3DXCREATEMESHFVFPROC) D3DX_GetProcAddress ("D3DXCreateMeshFVF"))) continue;
			if (!(QD3DXLoadSurfaceFromFileInMemory = (D3DXLOADSURFACEFROMFILEINMEMORYPROC) D3DX_GetProcAddress ("D3DXLoadSurfaceFromFileInMemory"))) continue;
			if (!(QD3DXMatrixInverse = (D3DXMATRIXINVERSEPROC) D3DX_GetProcAddress ("D3DXMatrixInverse"))) continue;

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

bool d3d_SceneBegun = false;

void D3D_Init3DSceneTexture (void);
void D3D_Kill3DSceneTexture (void);


// global video state
viddef_t	vid;

// window state management
bool d3d_DeviceLost = false;
bool vid_canalttab = false;
bool vid_initialized = false;
bool scr_skipupdate;

// 32/24 bit needs to come first because a 16 bit depth buffer is just not good enough for to prevent precision trouble in places
D3DFORMAT d3d_AllowedDepthFormats[] = {D3DFMT_D32, D3DFMT_D24X8, D3DFMT_D16, D3DFMT_D24S8, D3DFMT_D24X4S4, D3DFMT_D15S1, D3DFMT_UNKNOWN};
D3DFORMAT d3d_SupportedDepthFormats[32] = {D3DFMT_UNKNOWN};


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
cvar_t		d3d_mode ("d3d_mode", "-1", CVAR_ARCHIVE);
cvar_t		d3d_depthmode ("d3d_depthmode", "0", CVAR_ARCHIVE);
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

cvar_t vid_refreshrate ("vid_refreshrate", "-1", CVAR_ARCHIVE);
int d3d_AllowedRefreshRates[64] = {0};
int d3d_NumRefreshRates = 0;

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
		ExWindowStyle = WS_EX_TOPMOST;
	}
	else
	{
		// fullscreen mode
		WindowStyle = WS_POPUP;
		ExWindowStyle = WS_EX_TOPMOST;
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
	Q_MemSet (pp, 0, sizeof (D3DPRESENT_PARAMETERS));

	if (mode->RefreshRate == 0)
	{
		pp->Windowed = TRUE;
		pp->BackBufferFormat = D3DFMT_UNKNOWN;
		pp->FullScreen_RefreshRateInHz = 0;
	}
	else if (!WinDWM || d3d_NumRefreshRates < 2)
	{
		// only allow refresh rate changes on vista or higher as they seem unstable on XPDM drivers
		pp->Windowed = FALSE;
		pp->BackBufferFormat = mode->Format;
		pp->FullScreen_RefreshRateInHz = d3d_DesktopMode.RefreshRate;
	}
	else
	{
		pp->Windowed = FALSE;
		pp->BackBufferFormat = mode->Format;

		// pick a safe refresh rate
		int findrr = mode->RefreshRate;

		for (int i = 0; i < d3d_NumRefreshRates; i++)
		{
			if (d3d_AllowedRefreshRates[i] == vid_refreshrate.integer)
			{
				findrr = d3d_AllowedRefreshRates[i];
				break;
			}
		}

		// set back to the cvar
		Cvar_Set (&vid_refreshrate, findrr);

		// store to pp and mode
		pp->FullScreen_RefreshRateInHz = findrr;
		mode->RefreshRate = findrr;
	}

	// create it without a depth buffer to begin with
	d3d_GlobalCaps.DepthStencilFormat = D3DFMT_UNKNOWN;
	pp->AutoDepthStencilFormat = D3DFMT_UNKNOWN;
	pp->EnableAutoDepthStencil = FALSE;
	pp->Flags = 0;

	pp->SwapEffect = D3DSWAPEFFECT_DISCARD;
	pp->PresentationInterval = D3D_GetPresentInterval ();
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


void D3D_RecoverDeviceResources (void)
{
	// recreate anything that needs to be recreated
	D3D_Init3DSceneTexture ();
	D3D_InitHLSL ();
	D3D_RecoverLightmapResources ();

	// recover all states back to what they should be
	D3D_SetAllStates ();

	// force a recalc of the refdef
	vid.recalc_refdef = true;
}


void D3D_ClearOcclusionQueries (void);

void D3D_LoseDeviceResources (void)
{
	// release anything that needs to be released
	D3D_ClearOcclusionQueries ();
	D3D_Kill3DSceneTexture ();
	D3D_ShutdownHLSL ();
	D3D_LoseLightmapResources ();
	VBO_DestroyBuffers ();

	// destroy the old depth buffer
	// this was an auto surface so it doesn't need to be released
	d3d_Device->SetDepthStencilSurface (NULL);

	// ensure that present params are valid
	D3D_SetPresentParams (&d3d_PresentParams, &d3d_CurrentMode);

	// set up the correct depth format
	d3d_GlobalCaps.DepthStencilFormat = d3d_SupportedDepthFormats[d3d_depthmode.integer];
	d3d_PresentParams.AutoDepthStencilFormat = d3d_SupportedDepthFormats[d3d_depthmode.integer];
	d3d_PresentParams.EnableAutoDepthStencil = TRUE;
	d3d_PresentParams.Flags = D3DPRESENTFLAG_DISCARD_DEPTHSTENCIL;
}


void Host_GetConsoleCommands (void);

bool vid_restarted = false;

void D3D_VidRestart_f (void)
{
	// if we're attempting to change a fullscreen mode we need to validate it first
	if (d3d_CurrentMode.Format != D3DFMT_UNKNOWN && d3d_CurrentMode.RefreshRate != 0)
	{
		DEVMODE dm;

		Q_MemSet (&dm, 0, sizeof (DEVMODE));
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
		Sleep (10);
		hr = d3d_Device->TestCooperativeLevel ();

		if (hr == D3D_OK) break;
	}

	if (key_dest == key_menu)
	{
		// wipe the screen before resetting the device
		d3d_Device->Clear (0, NULL, D3DCLEAR_TARGET, 0x00000000, 1.0f, 1);
		d3d_Device->Present (NULL, NULL, NULL, NULL);
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

	if (key_dest == key_menu)
	{
		// wipe the screen again post-reset
		d3d_Device->Clear (0, NULL, D3DCLEAR_TARGET, 0x00000000, 1.0f, 1);
		d3d_Device->Present (NULL, NULL, NULL, NULL);
	}

	// bring back anything that needs to be brought back
	D3D_RecoverDeviceResources ();

	// flag to skip this frame so that we update more robustly
	vid_restarted = true;

	Cbuf_InsertText ("\n");
	Cbuf_Execute ();
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


int RRCompFunc (const void *a, const void *b)
{
	int ia = ((int *) a)[0];
	int ib = ((int *) b)[0];

	return ia - ib;
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

	d3d_NumRefreshRates = 0;

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

			Q_MemSet (&dm, 0, sizeof (DEVMODE));
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
			if (ChangeDisplaySettings (&dm, CDS_TEST | CDS_FULLSCREEN) != DISP_CHANGE_SUCCESSFUL) continue;

			// store out the list of allowed refresh rates
			bool allowrr = true;

			// see if this rate is already available
			for (int rr = 0; rr < d3d_NumRefreshRates; rr++)
			{
				if (d3d_AllowedRefreshRates[rr] == mode.RefreshRate)
				{
					allowrr = false;
					break;
				}
			}

			// we assume that monitors won't support more than this number of refresh rates...
			if (allowrr && d3d_NumRefreshRates < 64)
			{
				d3d_AllowedRefreshRates[d3d_NumRefreshRates] = mode.RefreshRate;
				d3d_NumRefreshRates++;
			}

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
				d3d_ModeList = (d3d_ModeDesc_t *) Zone_Alloc (sizeof (d3d_ModeDesc_t));
				newmode = d3d_ModeList;
			}
			else
			{
				// add it to the end of the list
				for (newmode = d3d_ModeList;; newmode = newmode->Next)
					if (!newmode->Next)
						break;

				newmode->Next = (d3d_ModeDesc_t *) Zone_Alloc (sizeof (d3d_ModeDesc_t));
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

	// sort the refresh rates list
	qsort (d3d_AllowedRefreshRates, d3d_NumRefreshRates, sizeof (int), RRCompFunc);

	// did we find any windowed modes?
	if (NumWindowedModes)
	{
		// now we emulate winquake by pushing windowed modes to the start of the list
		d3d_ModeDesc_t *WindowedModes = (d3d_ModeDesc_t *) Zone_Alloc (NumWindowedModes * sizeof (d3d_ModeDesc_t));
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

	// check for mode already set
	if (d3d_mode.integer >= 0)
	{
		// make sure it's a good one
		for (d3d_ModeDesc_t *mode = d3d_ModeList; mode; mode = mode->Next)
		{
			if (mode->ModeNum == d3d_mode.integer)
			{
				// we got a good 'un
				return;
			}
		}

		// the value of d3d_mode isn't in our mode list so pick a decent default mode
	}

	// find a mode to start in; we start directq in a windowed mode at either 640x480 or 800x600, whichever is
	// higher.  windowed modes are safer - they don't have exclusive ownership of your screen so if things go
	// wrong on first run you can get out easier.
	int windowedmode800 = -1;
	int windowedmode640 = -1;

	// now find a good default windowed mode - try to find either 800x600 or 640x480
	for (d3d_ModeDesc_t *mode = d3d_ModeList; mode; mode = mode->Next)
	{
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
}


void D3D_FindModeForVidMode (D3DDISPLAYMODE *mode)
{
	// catch unspecified modes
	if (d3d_mode.value < 0) return;

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


void D3D_TexMem_f (void)
{
	Con_Printf ("Available Texture Memory: %i MB\n", (d3d_Device->GetAvailableTextureMem ()) / (1024 * 1024));
}


cmd_t D3D_TexMem_Cmd ("gl_videoram", D3D_TexMem_f);

void D3D_InitDirect3D (D3DDISPLAYMODE *mode)
{
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
		d3d_GlobalCaps.deviceCreateFlags = D3DCREATE_HARDWARE_VERTEXPROCESSING;// | D3DCREATE_DISABLE_DRIVER_MANAGEMENT;
	}
	else
	{
		d3d_GlobalCaps.supportHardwareTandL = false;
		d3d_GlobalCaps.deviceCreateFlags = D3DCREATE_SOFTWARE_VERTEXPROCESSING;// | D3DCREATE_DISABLE_DRIVER_MANAGEMENT;
	}

	// now reset the present params as they will have become messed up above
	D3D_SetPresentParams (&d3d_PresentParams, mode);

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
		Sys_Error ("D3D_InitDirect3D: IDirect3D9::CreateDevice failed");
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
	Sbar_Changed ();
	d3d_Device->Present (NULL, NULL, NULL, NULL);

	// get capabilities on the actual device
	hr = d3d_Device->GetDeviceCaps (&d3d_DeviceCaps);

	if (FAILED (hr))
	{
		Sys_Error ("D3D_InitDirect3D: Failed to retrieve device caps");
		return;
	}

	// report on selected ones
	Con_Printf ("Maximum Texture Blend Stages: %i\n", d3d_DeviceCaps.MaxTextureBlendStages);
	Con_Printf ("Maximum Simultaneous Textures: %i\n", d3d_DeviceCaps.MaxSimultaneousTextures);
	Con_Printf ("Maximum Texture Size: %i x %i\n", d3d_DeviceCaps.MaxTextureWidth, d3d_DeviceCaps.MaxTextureHeight);
	Con_Printf ("Maximum Anisotropic Filter: %i\n", d3d_DeviceCaps.MaxAnisotropy);
	Con_Printf ("\n");

	// tmus; take the lower of what's available as we need to work with them all
	d3d_GlobalCaps.NumTMUs = 666;

	if (d3d_DeviceCaps.MaxTextureBlendStages < d3d_GlobalCaps.NumTMUs) d3d_GlobalCaps.NumTMUs = d3d_DeviceCaps.MaxTextureBlendStages;
	if (d3d_DeviceCaps.MaxSimultaneousTextures < d3d_GlobalCaps.NumTMUs) d3d_GlobalCaps.NumTMUs = d3d_DeviceCaps.MaxSimultaneousTextures;

	// check for availability of rgb texture formats (ARGB is non-negotiable required by Quake)
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

	// assume that vertex buffers are going to work...
	d3d_GlobalCaps.supportVertexBuffers = true;

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
	else D3D_FindBestFullscreenMode (mode);

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


void PaletteFromColormap (byte *pal, byte *map)
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
	Q_MemCpy (basepal, pal, 768);

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
	}

	// for avoidance of the pink fringe effect
	pal[765] = pal[766] = pal[767] = 0;

	// entry 255 is not a luma
	vid.fullbright[255] = false;
}


void Check_Gamma (unsigned char *pal)
{
#ifdef GLQUAKE_GAMMA_SCALE
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
#endif
}


cmd_t D3D_DescribeModes_f_Cmd ("vid_describemodes", D3D_DescribeModes_f);
cmd_t D3D_NumModes_f_Cmd ("vid_nummodes", D3D_NumModes_f);
cmd_t D3D_DescribeCurrentMode_f_Cmd ("vid_describecurrentmode", D3D_DescribeCurrentMode_f);
cmd_t D3D_DescribeMode_f_Cmd ("vid_describemode", D3D_DescribeMode_f);
cmd_t D3D_VidRestart_f_Cmd ("vid_restart", D3D_VidRestart_f);

void D3D_VidInit (void)
{
	// ensure
	Q_MemSet (&d3d_RenderDef, 0, sizeof (d3d_renderdef_t));

	vid_initialized = true;
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
		Sbar_Changed ();
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

void D3D_TextureMode_f (void)
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

	for (int i = 0; i < 6; i++)
	{
		if (!stricmp (d3d_filtermodes[i].name, Cmd_Argv (1)) || i == atoi (Cmd_Argv (1)))
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


cmd_t D3D_TextureMode_Cmd ("gl_texturemode", D3D_TextureMode_f);

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

	// unavailable so don't try to use them
	if (!d3d_GlobalCaps.supportPixelShaders) return;

	if (d3d_GlobalCaps.usingPixelShaders != oldUsingPixelShaders)
	{
		if (d3d_GlobalCaps.usingPixelShaders)
		{
			// Con_SafePrintf ("Enabled pixel shaders\n");
		}
		else
		{
			// switch shaders off
			d3d_Device->SetPixelShader (NULL);
			d3d_Device->SetVertexShader (NULL);
			// Con_SafePrintf ("Disabled pixel shaders\n");
		}

		// store back
		oldUsingPixelShaders = d3d_GlobalCaps.usingPixelShaders;
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


void D3D_CheckAnisotropic (void)
{
	// force an update first time through
	static int oldaniso = ~r_anisotropicfilter.integer;

	if (oldaniso != r_anisotropicfilter.integer)
	{
		int blah;
		for (blah = 1; blah < r_anisotropicfilter.integer; blah <<= 1);

		if (blah > d3d_DeviceCaps.MaxAnisotropy) blah = d3d_DeviceCaps.MaxAnisotropy;
		if (blah < 1) blah = 1;

		if (blah != r_anisotropicfilter.integer)
			Cvar_Set (&r_anisotropicfilter, (float) blah);

		for (int stage = 0; stage < d3d_GlobalCaps.NumTMUs; stage++)
			D3D_SetSamplerState (stage, D3DSAMP_MAXANISOTROPY, r_anisotropicfilter.integer);

		oldaniso = r_anisotropicfilter.integer;
	}
}


void D3D_CheckDepthFormat (void)
{
	static int oldmode = ~d3d_depthmode.integer;

	// check for first time or recreate the depth buffer
	if (d3d_GlobalCaps.DepthStencilFormat == D3DFMT_UNKNOWN)
	{
		// enumerate supported formats
		for (int i = 0, numd = 0; ; i++)
		{
			if (d3d_AllowedDepthFormats[i] == D3DFMT_UNKNOWN) break;

			// check that the format exists
			hr = d3d_Object->CheckDeviceFormat
			(
				D3DADAPTER_DEFAULT,
				D3DDEVTYPE_HAL,
				d3d_CurrentMode.Format,
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
				d3d_CurrentMode.Format,
				d3d_CurrentMode.Format,
				d3d_AllowedDepthFormats[i]
			);

			// format is not compatible
			if (FAILED (hr)) continue;

			// add it to the list of supported formats
			d3d_SupportedDepthFormats[numd] = d3d_AllowedDepthFormats[i];
			d3d_SupportedDepthFormats[numd + 1] = D3DFMT_UNKNOWN;
			numd++;
		}
	}

	// check for a change or recreate the depth buffer
	if (oldmode != d3d_depthmode.integer)
	{
		// ensure that the mode is a valid one
		int newmode = 0;

		// find the matching mode
		for (int i = 0; ; i++)
		{
			if (d3d_SupportedDepthFormats[i] == D3DFMT_UNKNOWN) break;

			if (i == d3d_depthmode.integer)
			{
				newmode = i;
				break;
			}
		}

		// update the selected mode
		Cvar_Set (&d3d_depthmode, newmode);

		// store back
		oldmode = d3d_depthmode.integer;

		// only restart if the mode actually changed
		if (d3d_SupportedDepthFormats[d3d_depthmode.integer] != d3d_GlobalCaps.DepthStencilFormat)
		{
			// restart video
			// note - using CreateDepthStencilSurface and SetDepthStencilSurface gives a depth buffer that's
			// considerably slower than creating an auto one in the present params.
			D3D_VidRestart_f ();

			static bool firsttime = true;

			if (!firsttime) Con_Printf ("Created depth buffer with format %s\n", D3DTypeToString (d3d_SupportedDepthFormats[d3d_depthmode.integer]));
			firsttime = false;
		}
	}
}


void D3D_CheckRefreshRate (void)
{
	static int oldrr = ~vid_refreshrate.integer;

	// only allow on vista or higher
	if (oldrr != vid_refreshrate.integer && WinDWM && d3d_NumRefreshRates > 1)
	{
		// pick a valid rate
		int findrr = d3d_DesktopMode.RefreshRate;

		// ensure that it's valid
		for (int i = 0; i < d3d_NumRefreshRates; i++)
		{
			if (d3d_AllowedRefreshRates[i] == vid_refreshrate.integer)
			{
				findrr = d3d_AllowedRefreshRates[i];
				break;
			}
		}

		// set back
		Cvar_Set (&vid_refreshrate, findrr);

		// only restart if it changed
		if (findrr != oldrr)
		{
			D3D_VidRestart_f ();

			static bool firsttime = true;

			if (!firsttime) Con_Printf ("Set refresh rate to %i Hz\n", findrr);
			firsttime = false;
		}

		oldrr = vid_refreshrate.integer;
	}
}


void D3D_SubdivideWater (void);
void D3D_VBOCheck (void);

void D3D_BeginRendering (void)
{
	// video is not restarted by default
	vid_restarted = false;

	// check for device recovery and recover it if needed
	if (!D3D_CheckRecoverDevice ()) return;

	// check for any changes to any display properties
	// if we need to restart video we must skip drawing this frame
	D3D_CheckGamma ();
	D3D_CheckVidMode (); if (vid_restarted) return;
	D3D_CheckVSync (); if (vid_restarted) return;
	D3D_CheckDepthFormat (); if (vid_restarted) return;
	D3D_CheckRefreshRate (); if (vid_restarted) return;
	D3D_CheckD3DXVersion (); if (vid_restarted) return;
	D3D_CheckAnisotropic ();

	// flag that we haven't begun the scene yet
	d3d_SceneBegun = false;

	// begin processing vertex buffers
	VBO_BeginFrame ();
}


void D3D_EndRendering (void)
{
	// end vertex buffers for the frame
	VBO_EndFrame ();

	// end the previous scene
	if (d3d_SceneBegun)
	{
		d3d_Device->EndScene ();
		hr = d3d_Device->Present (NULL, NULL, NULL, NULL);

		// force to false or it may be invalid for 2 consecutive calls to this
		d3d_SceneBegun = false;
	}
	else
	{
		// fake an OK hr from present if nothing was drawn
		hr = S_OK;
	}

	// defer the pixel shaders check to here to ensure that we're in the right mode before subdividing
	D3D_CheckPixelShaders ();

	// check for water subdivision changes
	// done here so that the scratch buffer will be valid for use
	D3D_SubdivideWater ();

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


void D3D_Finish (void)
{
	// bollocks!
}


#include <gdiplus.h>
#pragma comment (lib, "gdiplus.lib")
#include "CGdiPlusBitmap.h"

Gdiplus::GdiplusStartupInput gdiplusStartupInput;
ULONG_PTR gdiplusToken;
CGdiPlusBitmapResource *SplashBMP = NULL;

LRESULT CALLBACK SplashProc (HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	HDC          hdc;
	PAINTSTRUCT  ps;

	switch (message)
	{
	case WM_PAINT:
		if (SplashBMP)
		{
			hdc = BeginPaint (hWnd, &ps);
			Gdiplus::Graphics g (hdc);

			int w = SplashBMP->TheBitmap->GetWidth ();
			int h = SplashBMP->TheBitmap->GetHeight ();

			g.DrawImage (SplashBMP->TheBitmap, 0, 0, w, h);

			// this pretty much replicates the functionality of my splash screen maker app but brings it back into the engine
			// unicode sucks cocks in hell, by the way.
			Gdiplus::FontFamily   fontFamily (L"Arial");
			Gdiplus::Font         font (&fontFamily, 10, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
			Gdiplus::RectF        rectF (0, (h / 2) + 10, w, h);
			Gdiplus::StringFormat stringFormat;
			Gdiplus::SolidBrush   solidBrush (Gdiplus::Color (255, 192, 192, 192));

			stringFormat.SetAlignment (Gdiplus::StringAlignmentCenter);
			stringFormat.SetLineAlignment (Gdiplus::StringAlignmentNear);

			g.SetTextRenderingHint (Gdiplus::TextRenderingHintAntiAliasGridFit);
			g.DrawString (QASCIIToUnicode ("Release "DIRECTQ_VERSION), -1, &font, rectF, &stringFormat, &solidBrush);

			g.Flush (Gdiplus::FlushIntentionFlush);
			g.ReleaseHDC (hdc);
			EndPaint (hWnd, &ps);

			// sleep a little to give the splash a chance to show
			Sleep (500);
		}
		else
		{
			// this is to cover cases where this window proc may be triggered with no valid bitmap
			hdc = GetDC (d3d_Window);
			RECT cr;
			GetClientRect (d3d_Window, &cr);
			PatBlt (hdc, cr.left, cr.top, cr.right - cr.left, cr.bottom - cr.top, BLACKNESS);
			ReleaseDC (d3d_Window, hdc);
		}

		return 0;

	default:
		return DefWindowProc (hWnd, message, wParam, lParam);
	}
}


void Splash_Init (void)
{
	// switch window properties
	SetWindowLong (d3d_Window, GWL_WNDPROC, (LONG) SplashProc);
	SetWindowLong (d3d_Window, GWL_EXSTYLE, WS_EX_TOPMOST);
	SetWindowLong (d3d_Window, GWL_STYLE, WS_POPUP);

	// Initialize GDI+.
	Gdiplus::GdiplusStartup (&gdiplusToken, &gdiplusStartupInput, NULL);

	// load our bitmap; basic GDI+ doesn't provide support for loading PNGs from resources so
	// we use the code found at http://www.codeproject.com/KB/GDI-plus/cgdiplusbitmap.aspx
	SplashBMP = new CGdiPlusBitmapResource ();
	SplashBMP->Load (IDR_SPLASH, RT_RCDATA, GetModuleHandle (NULL));

	int BMPWidth = SplashBMP->TheBitmap->GetWidth ();
	int BMPHeight = SplashBMP->TheBitmap->GetHeight ();

	// get the desktop resolution
	HDC DesktopDC = GetDC (NULL);
	int DeskWidth = GetDeviceCaps (DesktopDC, HORZRES);
	int DeskHeight = GetDeviceCaps (DesktopDC, VERTRES);
	ReleaseDC (NULL, DesktopDC);

	// center and show the splash window (resizing it to the bitmap size as we go)
	SetWindowPos (d3d_Window, NULL, (DeskWidth - BMPWidth) / 2, (DeskHeight - BMPHeight) / 2, BMPWidth, BMPHeight, 0);

	ShowWindow (d3d_Window, SW_SHOW);
	UpdateWindow (d3d_Window);

	SetForegroundWindow (d3d_Window);

	// pump all messages
	MSG msg;

	while (PeekMessage (&msg, NULL, 0, 0, PM_REMOVE))
	{
      	TranslateMessage (&msg);
      	DispatchMessage (&msg);
	}
}


void Splash_Destroy (void)
{
	if (SplashBMP) delete SplashBMP;
	SplashBMP = NULL;
	Gdiplus::GdiplusShutdown (gdiplusToken);
	SetWindowLong (d3d_Window, GWL_WNDPROC, (LONG) DefWindowProc);

	// hide the window
	ShowWindow (d3d_Window, SW_HIDE);
}

