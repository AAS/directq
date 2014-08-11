

#include "quakedef.h"
#include "d3d_quake.h"
#include "winquake.h"
#include "resource.h"
#include <commctrl.h>

// declarations that don't really fit in anywhere else
LPDIRECT3D9 d3d_Object = NULL;
LPDIRECT3DDEVICE9 d3d_Device = NULL;
D3DADAPTER_IDENTIFIER9 d3d_Adapter;
D3DCAPS9 d3d_DeviceCaps;
d3d_global_caps_t d3d_GlobalCaps;
D3DPRESENT_PARAMETERS d3d_PresentParams;
extern LPDIRECT3DVERTEXBUFFER9 d3d_SkySphereVerts;

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

static float vid_gamma = 1.0;

LPD3DXMATRIXSTACK d3d_WorldMatrixStack = NULL;
D3DXMATRIX d3d_ViewMatrix;
D3DXMATRIX *d3d_WorldMatrix;
D3DXMATRIX d3d_ProjectionMatrix;

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
HWND mainwindow;

// fixme - get rid of these
int DIBWidth, DIBHeight;

modestate_t	modestate = MS_UNINIT;

void Splash_Destroy (void);

// video cvars
cvar_t		vid_mode = {"vid_mode","0", false};
cvar_t		_vid_default_mode = {"_vid_default_mode","0", true};
cvar_t		_vid_default_mode_win = {"_vid_default_mode_win","3", true};
cvar_t		vid_wait = {"vid_wait","0"};
cvar_t		vid_nopageflip = {"vid_nopageflip","0", true};
cvar_t		_vid_wait_override = {"_vid_wait_override", "0", true};
cvar_t		vid_config_x = {"vid_config_x","800", true};
cvar_t		vid_config_y = {"vid_config_y","600", true};
cvar_t		vid_stretch_by_2 = {"vid_stretch_by_2","1", true};
cvar_t		gl_ztrick = {"gl_ztrick","1"};


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


RECT		WindowRect;
DWORD		WindowStyle, ExWindowStyle;

int			window_center_x, window_center_y, window_x, window_y, window_width, window_height;
RECT		window_rect;


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
	int modenum = Q_atoi (Cmd_Argv (1));

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
	D3D_ReleaseStateBlocks ();

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

	// this won't be displayed until after the reset completes
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

	// enumerate the modes in the adapter
	for (int m = 0; ; m++)
	{
		// end of the list
		if (AdapterModeDescs[m] == D3DFMT_UNKNOWN) break;

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

			// store it in our master mode list
			d3d_ModeDesc_t *newmode;

			// we need to keep our own list because d3d makes it awkward for us by maintaining a separate list for each format
			if (!d3d_ModeList)
			{
				d3d_ModeList = (d3d_ModeDesc_t *) malloc (sizeof (d3d_ModeDesc_t));
				newmode = d3d_ModeList;
			}
			else
			{
				// add it to the end of the list
				for (newmode = d3d_ModeList; ; newmode = newmode->Next)
					if (!newmode->Next)
						break;

				newmode->Next = (d3d_ModeDesc_t *) malloc (sizeof (d3d_ModeDesc_t));
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
			if (newmode->d3d_Mode.Width >= d3d_DesktopMode.Width) newmode->AllowWindowed = false;
			if (newmode->d3d_Mode.Height >= d3d_DesktopMode.Height) newmode->AllowWindowed = false;
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
	d3d_ModeDesc_t *WindowedModes = (d3d_ModeDesc_t *) malloc (NumWindowedModes * sizeof (d3d_ModeDesc_t));
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

		// if both were unspecified we take the current windowed mode as the best
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
		/*
		hardware t&l fucks up on some nvidia (only?) drivers, so it's been removed for now
		D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_PUREDEVICE,
		D3DCREATE_MIXED_VERTEXPROCESSING | D3DCREATE_PUREDEVICE,
		D3DCREATE_HARDWARE_VERTEXPROCESSING,
		D3DCREATE_MIXED_VERTEXPROCESSING,
		*/
		D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_PUREDEVICE,
		D3DCREATE_SOFTWARE_VERTEXPROCESSING,
		-1
	};

	// retrieve the OS version
	OSVERSIONINFO os_Version;
	os_Version.dwOSVersionInfoSize = sizeof (OSVERSIONINFO);
	GetVersionEx (&os_Version);

	for (int i = 0; ; i++)
	{
		if (DesiredFlags[i] == -1)
		{
			// failed
			Sys_Error ("D3D_InitDirect3D: d3d_Object->CreateDevice failed");
			return;
		}

		// on vista we need to restrict computation to the main thread only
		if (os_Version.dwMajorVersion >= 6) DesiredFlags[i] |= D3DCREATE_DISABLE_PSGP_THREADING;

		// attempt to create the device
		// Quake requires D3DCREATE_FPU_PRESERVE or it's internal timers go totally out of whack
		// we also create it to be multithread-safe as the CRT is no longer single threaded in VS 2005 or above
		HRESULT hr = d3d_Object->CreateDevice
		(
			D3DADAPTER_DEFAULT,
			D3DDEVTYPE_HAL,
			d3d_Window,
			DesiredFlags[i] | D3DCREATE_FPU_PRESERVE | D3DCREATE_MULTITHREADED,
			&d3d_PresentParams,
			&d3d_Device
		);

		if (SUCCEEDED (hr))
		{
			Con_Printf ("Video mode %i x %i Initialized\n", mode->Width, mode->Height);

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
				Con_Printf ("Using Hardware Vertex Processing\n\n");
			else if (DesiredFlags[i] & D3DCREATE_MIXED_VERTEXPROCESSING)
				Con_Printf ("Using Mixed Vertex Processing\n\n");
			else Con_Printf ("Using Software Vertex Processing\n\n");

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

	// get capabilities
	d3d_Device->GetDeviceCaps (&d3d_DeviceCaps);

	// report
	Con_Printf ("Available Texture Memory: %i MB\n", (d3d_Device->GetAvailableTextureMem ()) / (1024 * 1024));
	Con_Printf ("Maximum Simultaneous Textures: %i\n", d3d_DeviceCaps.MaxSimultaneousTextures);
	Con_Printf ("Maximum Texture Size: %i x %i\n", d3d_DeviceCaps.MaxTextureWidth, d3d_DeviceCaps.MaxTextureHeight);

	// attempt to create a texture in D3DFMT_A16B16G16R16 format
	LPDIRECT3DTEXTURE9 tex;

	HRESULT hr = d3d_Device->CreateTexture
	(
		128,
		128,
		1,
		0,
		D3DFMT_A16B16G16R16,
		D3DPOOL_MANAGED,
		&tex,
		NULL
	);

	if (SUCCEEDED (hr))
	{
		// done with the texture
		tex->Release ();

		// flag it
		Con_Printf ("Allowing A16B16G16R16 Lightmap Format!\n");
		d3d_GlobalCaps.AllowA16B16G16R16 = true;
	}
	else
	{
		// sucks
		Con_Printf ("Using A8B8G8R8 Lightmap Format\n");
		d3d_GlobalCaps.AllowA16B16G16R16 = false;
	}

	Con_Printf ("\n");

	// create our state blocks
	D3D_CreateStateBlocks ();

	// set default states
	D3D_SetDefaultStates ();
}


void D3D_CreateWindowClass (void)
{
	HINSTANCE hInstance = GetModuleHandle (NULL);
	WNDCLASS wc;

	// set up and register the window class
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
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
	extern DWORD WindowStyle;
	extern DWORD ExWindowStyle;
	extern RECT WindowRect;
	extern int DIBWidth;
	extern int DIBHeight;
	extern HINSTANCE global_hInstance;
	extern int window_x;
	extern int window_y;
	extern int window_width;
	extern int window_height;

	WindowRect.top = WindowRect.left = 0;
	DIBWidth = WindowRect.right = mode->Width;
	DIBHeight = WindowRect.bottom = mode->Height;

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

	RECT rect = WindowRect;
	AdjustWindowRectEx (&rect, WindowStyle, FALSE, 0);

	int width = rect.right - rect.left;
	int height = rect.bottom - rect.top;

	d3d_Window = CreateWindowEx
	(
		ExWindowStyle,
		D3D_WINDOW_CLASS_NAME,
		"DirectQ Version 1.1",
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
	// empty while Quake starts up.
	HDC hdc = GetDC (d3d_Window);
	PatBlt (hdc, 0, 0, WindowRect.right, WindowRect.bottom, BLACKNESS);
	ReleaseDC (d3d_Window, hdc);

	D3D_SetConSize (mode->Width, mode->Height);

	vid.numpages = 1;

	mainwindow = d3d_Window;

	HICON hIcon = LoadIcon (global_hInstance, MAKEINTRESOURCE (IDI_ICON1));

	SendMessage (mainwindow, WM_SETICON, (WPARAM) TRUE, (LPARAM) hIcon);
	SendMessage (mainwindow, WM_SETICON, (WPARAM) FALSE, (LPARAM) hIcon);

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
	SetForegroundWindow (mainwindow);

	MSG msg;

	while (PeekMessage (&msg, NULL, 0, 0, PM_REMOVE))
	{
      	TranslateMessage (&msg);
      	DispatchMessage (&msg);
	}

	Sleep (100);

	SetWindowPos
	(
		mainwindow,
		HWND_TOP,
		0,
		0,
		0,
		0,
		SWP_DRAWFRAME | SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOCOPYBITS
	);

	SetForegroundWindow (mainwindow);

	// fix the leftover Alt from any Alt-Tab or the like that switched us away
	ClearAllStates ();

	// force an immediate recalc of the refdef
	vid.recalc_refdef = 1;
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
		r = pal[0];
		g = pal[1];
		b = pal[2];
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
	float f, inf;
	byte palette[768];

	// default to 0.7 on non-3dfx hardware
	// (nobody has a 3dfx any more...)
	if ((i = COM_CheckParm ("-gamma")) == 0)
		vid_gamma = 0.7;
	else
		vid_gamma = Q_atof (com_argv[i + 1]);

	for (i = 0; i < 768; i++)
	{
		f = pow ((float) ((pal[i] + 1) / 256.0), (float) vid_gamma);
		inf = f * 255 + 0.5;

		if (inf < 0) inf = 0;
		if (inf > 255) inf = 255;

		palette[i] = inf;
	}

	memcpy (pal, palette, sizeof (palette));
}


void D3D_VidInit (byte *palette)
{
	Cvar_RegisterVariable (&vid_mode);
	Cvar_RegisterVariable (&vid_wait);
	Cvar_RegisterVariable (&vid_nopageflip);
	Cvar_RegisterVariable (&_vid_wait_override);
	Cvar_RegisterVariable (&_vid_default_mode);
	Cvar_RegisterVariable (&_vid_default_mode_win);
	Cvar_RegisterVariable (&vid_config_x);
	Cvar_RegisterVariable (&vid_config_y);
	Cvar_RegisterVariable (&vid_stretch_by_2);
	Cvar_RegisterVariable (&gl_ztrick);

	InitCommonControls ();

	vid_initialized = true;

	vid.colormap = host_colormap;
	vid.fullbright = 256 - LittleLong (*((int *) vid.colormap + 2048));

	Check_Gamma (palette);
	VID_SetPalette (palette);

	vid_menudrawfn = VID_MenuDraw;
	vid_menukeyfn = VID_MenuKey;

	vid_canalttab = true;

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

	// add our commands
	Cmd_AddCommand ("vid_describemodes", D3D_DescribeModes_f);
	Cmd_AddCommand ("vid_nummodes", D3D_NumModes_f);
	Cmd_AddCommand ("vid_describecurrentmode", D3D_DescribeCurrentMode_f);
	Cmd_AddCommand ("vid_describemode", D3D_DescribeMode_f);
	Cmd_AddCommand ("vid_restart", D3D_VidRestart_f);

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
			int bpp = Q_atoi (com_argv[COM_CheckParm ("-bpp") + 1]);

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
		d3d_CurrentMode.Width = Q_atoi (com_argv[COM_CheckParm ("-width") + 1]);
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
		d3d_CurrentMode.Height = Q_atoi (com_argv[COM_CheckParm ("-height") + 1]);
	}
	else
	{
		// unspecified - set it to 0
		d3d_CurrentMode.Height = 0;
	}

	// set up the window class
	D3D_CreateWindowClass ();

	if (!isDedicated) Splash_Destroy ();

	// set the selected video mode
	D3D_SetVideoMode (&d3d_CurrentMode);

	// finally set the palette
	VID_SetPalette (palette);
}


void D3D_ShutdownDirect3D (void)
{
	D3D_ReleaseStateBlocks ();
	D3D_ReleaseTextures (D3D_RELEASE_ALL);

	SAFE_RELEASE (d3d_WorldMatrixStack);

	SAFE_RELEASE (d3d_BrushModelVerts);
	SAFE_RELEASE (d3d_SkySphereVerts);

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
			D3D_CreateStateBlocks ();

			// set default states
			D3D_SetDefaultStates ();

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
			D3D_ReleaseStateBlocks ();

			// ensure that present params are valid
			D3D_SetPresentParams (&d3d_PresentParams, &d3d_CurrentMode);

			// reset the device
			d3d_Device->Reset (&d3d_PresentParams);
			break;

		case D3DERR_DRIVERINTERNALERROR:
		default:
			// something bad happened
			// note: this isn't really a proper clean-up path as it doesn't do input/sound/etc cleanup either...
			DestroyWindow (mainwindow);
			exit (0);
			break;
		}

		// not quite ready yet
		return false;
	}

	// device is not lost
	return true;
}


void D3D_BeginRendering (int *x, int *y, int *width, int *height)
{
	// check for device recovery and recover it if needed
	if (!D3D_CheckRecoverDevice ()) return;

	// always clear the zbuffer
	DWORD d3d_ClearFlags = D3DCLEAR_ZBUFFER;

	// accumulate everything else we want to clear
	if (gl_clear.value) d3d_ClearFlags |= D3DCLEAR_TARGET;
	if (d3d_GlobalCaps.DepthStencilFormat == D3DFMT_D24S8) d3d_ClearFlags |= D3DCLEAR_STENCIL;

	d3d_Device->Clear (0, NULL, d3d_ClearFlags, D3DCOLOR_XRGB (255, 128, 0), 1.0f, 0);

	d3d_Device->BeginScene ();

	extern RECT WindowRect;

	*x = *y = 0;
	*width = WindowRect.right - WindowRect.left;
	*height = WindowRect.bottom - WindowRect.top;

	// load identity onto the view matrix (it seems as though some cards don't like it being set one time only)
	D3DXMatrixIdentity (&d3d_ViewMatrix);
	d3d_Device->SetTransform (D3DTS_VIEW, &d3d_ViewMatrix);

	// set up the default viewport
	d3d_DefaultViewport->Apply ();
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
	{
		S_BlockSound ();
		sound_active = false;
	}
	else if (ActiveApp && !sound_active)
	{
		S_UnblockSound ();
		sound_active = true;
	}

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
				ShowWindow (mainwindow, SW_SHOWNORMAL);
				SetForegroundWindow (mainwindow);
			}
		}
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
	case WM_KILLFOCUS:
		if (modestate == MS_FULLDIB)
			ShowWindow (mainwindow, SW_SHOWMINNOACTIVE);
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
		if (MessageBox (mainwindow, "Are you sure you want to quit?", "Confirm Exit", MB_YESNO | MB_SETFOREGROUND | MB_ICONQUESTION) == IDYES)
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

extern void M_Menu_Options_f (void);
extern void M_Print (int cx, int cy, char *str);
extern void M_PrintWhite (int cx, int cy, char *str);
extern void M_DrawCharacter (int cx, int line, int num);
extern void M_DrawTransPic (int x, int y, qpic_t *pic);
extern void M_DrawPic (int x, int y, qpic_t *pic);

static int	vid_line, vid_wmodes;

typedef struct
{
	int		modenum;
	char	desc[64];
	int		iscur;
} modedesc_t;

#define MAX_COLUMN_SIZE		9
#define MODE_AREA_HEIGHT	(MAX_COLUMN_SIZE + 2)
#define MAX_MODEDESCS		(MAX_COLUMN_SIZE * 3)
#define VID_ROW_SIZE	3

static modedesc_t	modedescs[MAX_MODEDESCS];

/*
================
VID_MenuDraw
================
*/
void VID_MenuDraw (void)
{
	qpic_t		*p;
	int			i, k, column, row;

	p = Draw_CachePic ("gfx/vidmodes.lmp");
	M_DrawPic ((320 - p->width) / 2, 4, p);

	vid_wmodes = 0;
	i = 0;

	for (d3d_ModeDesc_t *mode = d3d_ModeList; mode; mode = mode->Next)
	{
		k = vid_wmodes;

		// 2008-12-18 - fix vid menu crash when > 27 modes available
		if (k == MAX_MODEDESCS) break;

		modedescs[k].modenum = i;
		modedescs[k].iscur = 0;

		sprintf (modedescs[k].desc, "%ix%ix%i", mode->d3d_Mode.Width, mode->d3d_Mode.Height, mode->BPP);

		if (D3D_ModeIsCurrent (mode)) modedescs[k].iscur = 1;

		vid_wmodes++;
		i++;
	}

	if (vid_wmodes > 0)
	{
		M_Print (2*8, 36+0*8, "Fullscreen Modes (WIDTHxHEIGHTxBPP)");

		column = 8;
		row = 36+2*8;

		for (i=0 ; i<vid_wmodes ; i++)
		{
			if (modedescs[i].iscur)
				M_PrintWhite (column, row, modedescs[i].desc);
			else
				M_Print (column, row, modedescs[i].desc);

			column += 13*8;

			if ((i % VID_ROW_SIZE) == (VID_ROW_SIZE - 1))
			{
				column = 8;
				row += 8;
			}
		}
	}

	M_Print (3*8, 36 + MODE_AREA_HEIGHT * 8 + 8*2,
			 "Video modes must be set from the");
	M_Print (3*8, 36 + MODE_AREA_HEIGHT * 8 + 8*3,
			 "command line with -width <width>");
	M_Print (3*8, 36 + MODE_AREA_HEIGHT * 8 + 8*4,
			 "and -bpp <bits-per-pixel>");
	M_Print (3*8, 36 + MODE_AREA_HEIGHT * 8 + 8*6,
			 "Select windowed mode with -window");
}


/*
================
VID_MenuKey
================
*/
void VID_MenuKey (int key)
{
	switch (key)
	{
	case K_ESCAPE:
		S_LocalSound ("misc/menu1.wav");
		M_Menu_Options_f ();
		break;

	default:
		break;
	}
}



