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
// don't use this cos it cuts perf in half!!!!  not very helpful for when you're profiling
//#pragma comment (lib, "d3dx9d.lib")
#pragma comment (lib, "d3dx9.lib")
#else
#pragma comment (lib, "d3d9.lib")
#pragma comment (lib, "d3dx9.lib")
#endif

#include <vector>

std::vector<D3DDISPLAYMODE> d3d_DisplayModes;

void VIDWin32_CenterWindow (D3DDISPLAYMODE *mode);
void VIDWin32_SetWindowFrame (D3DDISPLAYMODE *mode);

#define VIDDRIVER_VERSION		3
cvar_t viddriver_version ("viddriver_version", 0.0f, CVAR_ARCHIVE);

void VID_GetCurrentGamma (void);
void VIDWin32_SetActiveGamma (cvar_t *var);
void VID_SetOSGamma (void);

void VIDD3D_EnumerateModes (void);

bool D3DVid_IsFullscreen (void) {return d3d_CurrentMode.RefreshRate ? true : false;}

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

void D3DRTT_CreateRTTTexture (void);

// fixme - merge these two
HWND d3d_Window;

bool vid_queuerestart = false;

void D3DVid_QueueRestart (cvar_t *var)
{
	// rather than restart immediately we notify the renderer that it will need to restart as soon as it comes up
	// this should hopefully fix a lot of crap during startup (and generally speed startup up a LOT)
	vid_queuerestart = true;
}


// video cvars
// force an invalid mode on initial entry
cvar_t		vid_mode ("vid_mode", "0", CVAR_ARCHIVE, D3DVid_QueueRestart);
cvar_t		vid_wait ("vid_wait", "0");
cvar_t		vid_vsync ("vid_vsync", "0", CVAR_ARCHIVE, D3DVid_QueueRestart);
cvar_t		vid_multisample ("vid_multisample", "0", CVAR_ARCHIVE, D3DVid_QueueRestart);
cvar_t		gl_finish ("gl_finish", 0.0f);
cvar_t		vid_fullscreen ("vid_fullscreen", 0.0f, CVAR_ARCHIVE, D3DVid_QueueRestart);
cvar_t		vid_windowborders ("vid_windowborders", 1.0f, CVAR_ARCHIVE, D3DVid_QueueRestart);

cvar_t d3d_usinginstancing ("r_instancing", "1", CVAR_ARCHIVE);


void VIDD3D_CheckVidDriver (void)
{
	// check if a restart request has been queued; run it if so, and optionally skip the rest of this frame
	if (viddriver_version.integer != VIDDRIVER_VERSION)
	{
		// if the video driver changed then we must force the user to 640x480 windowed
		Cvar_Set (&vid_mode, 0.0f);
		Cvar_Set (&vid_fullscreen, 0.0f);
		Cvar_Set (&viddriver_version, VIDDRIVER_VERSION);
		Cvar_Set (&vid_multisample, 0.0f);
		Cvar_Set (&vid_windowborders, 1.0f);
	}
}


// consistency with DP and FQ
cvar_t r_anisotropicfilter ("gl_texture_anisotropy", "1", CVAR_ARCHIVE);
cvar_alias_t r_anisotropicfilter_alias ("r_anisotropicfilter", &r_anisotropicfilter);
cvar_alias_t gl_anisotropic_filter_alias ("gl_anisotropic_filter", &r_anisotropicfilter);

cvar_t gl_triplebuffer ("gl_triplebuffer", 1, CVAR_ARCHIVE);

D3DDISPLAYMODE d3d_DesktopMode;
D3DDISPLAYMODE d3d_CurrentMode;

void ClearAllStates (void);
void AppActivate (BOOL fActive, BOOL minimize);


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
		if (FAILED (d3d_Object->CheckDeviceFormat (D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, ModeFormat, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_SURFACE, d3d_AllowedDepthFormats[i]))) continue;

		// check that the format is compatible
		if (FAILED (d3d_Object->CheckDepthStencilMatch (D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, ModeFormat, ModeFormat, d3d_AllowedDepthFormats[i]))) continue;

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

	// these are set according to the mode (windowed modes will have their stuff correct coming in here)
	pp->Windowed = (mode->RefreshRate == 0);
	pp->BackBufferFormat = mode->Format;
	pp->FullScreen_RefreshRateInHz = mode->RefreshRate;

	// create it without a depth buffer to begin with
	d3d_GlobalCaps.DepthStencilFormat = D3DVid_GetDepthStencilFormat (mode);
	pp->AutoDepthStencilFormat = d3d_GlobalCaps.DepthStencilFormat;
	pp->EnableAutoDepthStencil = TRUE;
	pp->Flags = 0;

	pp->SwapEffect = D3DSWAPEFFECT_DISCARD;
	pp->PresentationInterval = vid_vsync.integer ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE;	// roll on d3d11
	pp->BackBufferCount = 1;
	pp->BackBufferWidth = mode->Width;
	pp->BackBufferHeight = mode->Height;
	pp->hDeviceWindow = d3d_Window;

	if (vid_multisample.integer > 0 && d3d_GlobalCaps.MaxMultiSample > 0)
	{
		if (vid_multisample.integer > d3d_GlobalCaps.MaxMultiSample)
			pp->MultiSampleQuality = d3d_GlobalCaps.MaxMultiSample;
		else pp->MultiSampleQuality = vid_multisample.integer;

		pp->MultiSampleType = D3DMULTISAMPLE_NONMASKABLE;
	}
	else
	{
		pp->MultiSampleQuality = 0;
		pp->MultiSampleType = D3DMULTISAMPLE_NONE;
	}
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


bool vid_restarted = false;
bool vid_recoveryrequired = false;

void D3DVid_Restart (void)
{
	// video can't be restarted yet
	if (!d3d_Device) return;

	// to do  - validate vsync and depth buffer format for the device

	// make sure that we're ready to reset
	while ((hr = d3d_Device->TestCooperativeLevel ()) != D3D_OK)
	{
		Sys_SendKeyEvents ();
		Sleep (1);
	}

	// release anything that needs to be released
	D3DVid_LoseDeviceResources ();

	// this is a good time to resize the window frame
	VIDWin32_SetWindowFrame (&d3d_CurrentMode);

	// reset the device
	hr = d3d_Device->Reset (&d3d_PresentParams);

	// if we're going to a fullscreen mode we need to handle the mouse properly
	IN_SetMouseState (!d3d_PresentParams.Windowed);

	if (FAILED (hr))
	{
		// a failed reset causes hassle
		Sys_Error ("D3DVid_Restart: Unable to Reset Device");
		return;
	}

	// make sure that the reset has completed
	while ((hr = d3d_Device->TestCooperativeLevel ()) != D3D_OK)
	{
		Sys_SendKeyEvents ();
		Sleep (1);
	}

	// bring back anything that needs to be brought back
	vid_recoveryrequired = true;

	// flag to skip this frame so that we update more robustly
	vid_restarted = true;

	Cbuf_InsertText ("\n");
	Cbuf_Execute ();
}


void D3DVid_ValidateTextureSizes (void);

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

	// if i use these then i'll never forget to add the appropriate locking flags and will only have one place to change
	// them in when and if i ever need to change them
	d3d_GlobalCaps.DefaultLock = D3DLOCK_NOSYSLOCK;
	d3d_GlobalCaps.DiscardLock = D3DLOCK_DISCARD | D3DLOCK_NOSYSLOCK;
	d3d_GlobalCaps.DynamicLock = D3DLOCK_NO_DIRTY_UPDATE | D3DLOCK_NOSYSLOCK;
	d3d_GlobalCaps.NoOverwriteLock = D3DLOCK_NOOVERWRITE | D3DLOCK_NOSYSLOCK;

	// set up the counts for particle batching; we keep 16 registers spare for anything else we may wish to use
	d3d_GlobalCaps.MaxParticleBatch = (d3d_DeviceCaps.MaxVertexShaderConst - 16) / 2;
	d3d_GlobalCaps.MaxIQMJoints = (d3d_DeviceCaps.MaxVertexShaderConst - 16) / 3;

	// report some caps
	Con_Printf ("Video mode %i (%ix%i) Initialized\n", vid_mode.integer, mode->Width, mode->Height);
	Con_Printf ("Back Buffer Format: %s (created %i %s)\n", D3DTypeToString (mode->Format), d3d_PresentParams.BackBufferCount, d3d_PresentParams.BackBufferCount > 1 ? "backbuffers" : "backbuffer");
	Con_Printf ("Refresh Rate: %i Hz (%s)\n", mode->RefreshRate, mode->RefreshRate ? "Fullscreen" : "Windowed");
	Con_Printf ("\n");

	// clear to black immediately
	d3d_Device->Clear (0, NULL, D3DCLEAR_TARGET, 0x00000000, 1.0f, 1);
	d3d_Device->Present (NULL, NULL, NULL, NULL);
	d3d_RenderDef.framecount++;

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

	// eval max allowable extents
	d3d_GlobalCaps.MaxExtents = d3d_DeviceCaps.MaxTextureWidth;

	// note - some devices have a max height < max width (my ATI does)
	if (d3d_DeviceCaps.MaxTextureHeight < d3d_GlobalCaps.MaxExtents) d3d_GlobalCaps.MaxExtents = d3d_DeviceCaps.MaxTextureHeight;
	if (LIGHTMAP_SIZE < d3d_GlobalCaps.MaxExtents) d3d_GlobalCaps.MaxExtents = LIGHTMAP_SIZE;
	if (LIGHTMAP_SIZE < d3d_GlobalCaps.MaxExtents) d3d_GlobalCaps.MaxExtents = LIGHTMAP_SIZE;

	d3d_GlobalCaps.MaxExtents <<= 4;
	d3d_GlobalCaps.MaxExtents -= 16;

	Con_DPrintf ("max extents: %i\n", d3d_GlobalCaps.MaxExtents);

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

	// build the rest of the video menu (deferred to here as it's dependent on video being up)
	Menu_VideoBuild ();

	// set initial state caches
	D3D_SetAllStates ();

	// begin at 1 so that any newly allocated model_t will be 0 and therefore must
	// be explicitly set to be valid
	d3d_RenderDef.RegistrationSequence = 1;
}


/*
==============================================================================================================================

	VIDEO STARTUP

==============================================================================================================================
*/

void VIDD3D_EnumerateModes (void)
{
	// get the desktop mode for reference
	d3d_Object->GetAdapterDisplayMode (D3DADAPTER_DEFAULT, &d3d_DesktopMode);

	// get the count of modes for this format
	int ModeCount = d3d_Object->GetAdapterModeCount (D3DADAPTER_DEFAULT, d3d_DesktopMode.Format);
	D3DDISPLAYMODE mode;

	// no modes available for this format
	if (!ModeCount)
	{
		Sys_Error ("D3DVid_EnumerateVideoModes: No 32 BPP display modes available");
		return;
	}

	// enumerate them all
	for (int i = 0; i < ModeCount; i++)
	{
		// get the mode description
		d3d_Object->EnumAdapterModes (D3DADAPTER_DEFAULT, d3d_DesktopMode.Format, i, &mode);

		// don't allow modes < 640 x 480 or where width < height (assume a rotated laptop screen)
		if (mode.Width < 640 || mode.Height < 480 || mode.Width < mode.Height) continue;

		// and save it out
		d3d_DisplayModes.push_back (mode);
	}
}


// this can also be used for cheking for mode changes by feeding a new mode into it and checking for differences
void VIDD3D_SetupMode (D3DDISPLAYMODE *mode)
{
	// sanity-check the modenum (this also forces the user to 640x480 windowed when they start up first if i change the mode list)
	if (vid_mode.integer < 0 || vid_mode.integer >= d3d_DisplayModes.size ())
	{
		// go back to safe mode
		Cvar_Set (&vid_mode, 0.0f);
		Cvar_Set (&vid_fullscreen, 0.0f);
		Cvar_Set (&vid_multisample, 0.0f);
		Cvar_Set (&vid_windowborders, 1.0f);
	}

	// the current mode dimensions are initially taken from the cvars
	mode->Width = d3d_DisplayModes[vid_mode.integer].Width;
	mode->Height = d3d_DisplayModes[vid_mode.integer].Height;

	// enable fullscreen toggle
	mode->RefreshRate = vid_fullscreen.value ? d3d_DisplayModes[vid_mode.integer].RefreshRate : 0;
	mode->Format = vid_fullscreen.value ? d3d_DisplayModes[vid_mode.integer].Format : D3DFMT_UNKNOWN;
}


void VIDD3D_Init (void)
{
	// this needs to be done every frame as well as on startup in case the game changes or a new cfg is execed
	VIDD3D_CheckVidDriver ();

	// ensure
	memset (&d3d_RenderDef, 0, sizeof (d3d_renderdef_t));

	vid_initialized = true;
	vid_canalttab = true;

	// this is always 32 irrespective of which version of the sdk we use
	if (!(d3d_Object = Direct3DCreate9 (D3D_SDK_VERSION)))
	{
		Sys_Error ("VIDD3D_Init - failed to initialize Direct3D!");
		return;
	}

	// enumerate available modes
	VIDD3D_EnumerateModes ();

	// set up the mode we'll start in
	VIDD3D_SetupMode (&d3d_CurrentMode);

	// if these are specified on the cmdline then we set them to non-zero and look for a matching mode
	int findwidth = (COM_CheckParm ("-width")) ? atoi (com_argv[COM_CheckParm ("-width") + 1]) : 0;
	int findheight = (COM_CheckParm ("-height")) ? atoi (com_argv[COM_CheckParm ("-height") + 1]) : 0;

	if (findwidth || findheight)
	{
		for (int i = 0; i < d3d_DisplayModes.size (); i++)
		{
			// if we're looking for the mode and we don't match then this isn't it
			if (findwidth && d3d_DisplayModes[i].Width != findwidth) continue;
			if (findheight && d3d_DisplayModes[i].Height != findheight) continue;

			// allow the override
			d3d_CurrentMode.Width = d3d_DisplayModes[i].Width;
			d3d_CurrentMode.Height = d3d_DisplayModes[i].Height;

			// we must also override refresh rate as the originally selected rate may be invalid for the new mode
			d3d_CurrentMode.RefreshRate = vid_fullscreen.value ? d3d_DisplayModes[i].RefreshRate : 0;
			d3d_CurrentMode.Format = vid_fullscreen.value ? d3d_DisplayModes[i].Format : D3DFMT_UNKNOWN;

			// and this is the mode we want
			Cvar_Set (&vid_mode, i);
			break;
		}
	}

	// enable command-line override - this must happen after the mode find so that it can override any overridden refresh rate properly
	if (COM_CheckParm ("-window"))
	{
		d3d_CurrentMode.RefreshRate = 0;
		d3d_CurrentMode.Format = D3DFMT_UNKNOWN;
		Cvar_Set (&vid_fullscreen, 0.0f);
	}

	if (COM_CheckParm ("-safe"))
	{
		// reset all cvars back to safe defaults and bring up in minimal windowed mode
		Cvar_Set (&vid_mode, 0.0f);
		Cvar_Set (&vid_fullscreen, 0.0f);
		Cvar_Set (&vid_multisample, 0.0f);
		Cvar_Set (&vid_windowborders, 1.0f);

		d3d_CurrentMode.Width = d3d_DisplayModes[0].Width;
		d3d_CurrentMode.Height = d3d_DisplayModes[0].Height;
		d3d_CurrentMode.RefreshRate = 0;
		d3d_CurrentMode.Format = D3DFMT_UNKNOWN;
	}

	d3d_Object->GetAdapterIdentifier (D3DADAPTER_DEFAULT, 0, &d3d_Adapter);

	Con_SafePrintf ("\nInitialized Direct 3D on %s\n", d3d_Adapter.DeviceName);
	Con_SafePrintf ("%s\nDriver: %s\n", d3d_Adapter.Description, d3d_Adapter.Driver);

	// print extended info
	Con_SafePrintf
	(
		"Vendor %x Device %x SubSys %x Revision %x\n",
		d3d_Adapter.VendorId,
		d3d_Adapter.DeviceId,
		d3d_Adapter.SubSysId,
		d3d_Adapter.Revision
	);

	Con_SafePrintf ("\n");

	// set the selected video mode
	// suspend stuff that could mess us up while creating the window
	bool temp = scr_disabled_for_loading;

	Host_DisableForLoading (true);
	CDAudio_Pause ();

	// switch window properties
	SetWindowLong (d3d_Window, GWL_WNDPROC, (LONG) MainWndProc);

	// retrieve and store the gamma ramp for the desktop
	VID_GetCurrentGamma ();

	// create the mode and activate input
	VIDWin32_SetWindowFrame (&d3d_CurrentMode);

	HICON hIcon = LoadIcon (GetModuleHandle (NULL), MAKEINTRESOURCE (IDI_APPICON));

	SendMessage (d3d_Window, WM_SETICON, (WPARAM) TRUE, (LPARAM) hIcon);
	SendMessage (d3d_Window, WM_SETICON, (WPARAM) FALSE, (LPARAM) hIcon);

	// set internal active flags otherwise we'll get 40 FPS and no mouse!!!
	AppActivate (TRUE, FALSE);

	// set cursor clip region
	IN_UpdateClipCursor ();
	IN_SetMouseState (d3d_CurrentMode.RefreshRate != 0);

	// now initialize direct 3d on the window
	D3DVid_InitDirect3D (&d3d_CurrentMode);

	// now resume the messy-uppy stuff
	CDAudio_Resume ();
	Host_DisableForLoading (temp);

	// fix the leftover Alt from any Alt-Tab or the like that switched us away
	ClearAllStates ();

	// force an immediate recalc of the refdef
	vid.recalc_refdef = 1;
}


void VID_Shutdown (void)
{
	// always restore gamma correctly
	VID_SetOSGamma ();

	if (vid_initialized)
	{
		vid_canalttab = false;

		// clear the screen to black so that shutdown doesn't leave artefacts from the last SCR_UpdateScreen
		if (d3d_Device)
		{
			// to do - switch back to windowed mode...
			if (d3d_CurrentMode.RefreshRate)
			{
				d3d_CurrentMode.Width = d3d_DisplayModes[0].Width;
				d3d_CurrentMode.Height = d3d_DisplayModes[0].Height;
				d3d_CurrentMode.RefreshRate = 0;
				d3d_CurrentMode.Format = D3DFMT_UNKNOWN;
				D3DVid_Restart ();
			}

			d3d_Device->Clear (0, NULL, D3DCLEAR_TARGET, 0x00000000, 1.0f, 1);
			d3d_Device->Present (NULL, NULL, NULL, NULL);
			d3d_RenderDef.framecount++;
		}

		// also need these... ;)
		D3DVid_LoseDeviceResources ();

		// release anything that needs to be released
		D3DTexture_Release ();

		// take down our shaders
		D3DHLSL_Shutdown ();

		// destroy the device and object
		SAFE_RELEASE (d3d_Device);
		SAFE_RELEASE (d3d_Object);

		AppActivate (false, false);
	}
}


/*
==============================================================================================================================

	MAIN SCENE

==============================================================================================================================
*/


void D3DVid_BeginRendering (void)
{
	// video is not restarted by default
	vid_restarted = false;

	if (!d3d_Device)
	{
		// if we don't have the device we don't bother
		vid_restarted = true;
		return;
	}

	// this needs to be done every frame as well as on startup in case the game changes or a new cfg is execed
	VIDD3D_CheckVidDriver ();

	bool vid_needrestart = false;
	D3DDISPLAYMODE newmode;

	VIDD3D_SetupMode (&newmode);

	if (vid_queuerestart) vid_needrestart = true;
	if (newmode.Width != d3d_CurrentMode.Width) vid_needrestart = true;
	if (newmode.Height != d3d_CurrentMode.Height) vid_needrestart = true;
	if (newmode.RefreshRate != d3d_CurrentMode.RefreshRate) vid_needrestart = true;
	if (newmode.Format != d3d_CurrentMode.Format) vid_needrestart = true;

	if (vid_needrestart)
	{
		VIDD3D_SetupMode (&d3d_CurrentMode);
		D3DVid_Restart ();
		vid_restarted = true;
		vid_queuerestart = false;
		return;
	}

	if (vid_recoveryrequired)
	{
		// resource recover is deferred to here so that it doesn't happen unnecessarily
		D3DVid_RecoverDeviceResources ();
		vid_restarted = true;
		vid_recoveryrequired = false;
		return;
	}

	// sigh - roll on d3d11
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
	// (is this even necessary any more since we're on shaders-only now?)
	D3D_SetRenderState (D3DRS_LIGHTING, FALSE);
	D3D_SetRenderState (D3DRS_CLIPPING, TRUE);

	// this is called before d3d_Device->BeginScene so that the d3d_Device->BeginScene is called on the correct rendertarget
	D3DRTT_BeginScene ();

	// SCR_UpdateScreen assumes that this is the last call in this function
	hr = d3d_Device->BeginScene ();
}


// set to true to suppress present for one frame
bool vid_nopresent = false;

void D3DVid_EndRendering (void)
{
	if (!d3d_Device) return;

	// get access to the swap chain if we don't have it
	if (!d3d_SwapChain) d3d_Device->GetSwapChain (0, &d3d_SwapChain);

	// unbind everything
	D3D_UnbindStreams ();
	D3D_SetVertexDeclaration (NULL);

	// count frames here so that we match present calls with the actual real framerate
	d3d_Device->EndScene ();

	if (!vid_nopresent)
	{
		// wtf?  presenting through the swap chain is faster?  ok, that's cool
		if ((d3d_SwapChain->Present (NULL, NULL, NULL, NULL, 0)) == D3DERR_DEVICELOST)
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
					vid_recoveryrequired = true;

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

		// the frame count should track present calls
		d3d_RenderDef.framecount++;
	}
	else vid_nopresent = false;
}

