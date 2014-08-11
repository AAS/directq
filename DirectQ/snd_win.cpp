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
#include "winquake.h"

// 64K is > 1 second at 16-bit, 22050 Hz
#define	WAV_BUFFERS				64
#define	WAV_MASK				0x3F
#define	WAV_BUFFER_SIZE			0x0400
#define SECONDARY_BUFFER_SIZE	0x100000

static bool	wavonly;
static bool	dsound_init;
static bool	primary_format_set;

static int	sample16;
static int	snd_sent, snd_completed;


/* 
 * Global variables. Must be visible to window-procedure function 
 *  so it can unlock and free the data block after it has been played. 
 */ 

HPSTR		lpData;

DWORD	ds_SoundBufferSize;

MMTIME		mmstarttime;

LPDIRECTSOUND8 ds_Device = NULL;
LPDIRECTSOUNDBUFFER8 ds_SecondaryBuffer8 = NULL;
DSCAPS ds_DeviceCaps;

sndinitstat SNDDMA_InitDirect (void);


/*
==================
FreeSound
==================
*/
void FreeSound (void)
{
	int	i;

	if (ds_SecondaryBuffer8)
	{
		ds_SecondaryBuffer8->Stop ();
		ds_SecondaryBuffer8->Release ();
	}

	if (ds_Device)
	{
		ds_Device->SetCooperativeLevel (d3d_Window, DSSCL_NORMAL);
		ds_Device->Release ();
	}

	ds_Device = NULL;
	ds_SecondaryBuffer8 = NULL;
	lpData = NULL;
	dsound_init = false;
}


char ds_BestDescription[128];
GUID ds_BestGuid;
char ds_BestModule[64];
int ds_BestBuffers = 0;
int ds_BestSampleRate = 0;

BOOL CALLBACK DSEnumCallback (LPGUID lpGuid, LPCSTR lpcstrDescription, LPCSTR lpcstrModule, LPVOID lpContext)
{
	// the primary sound device is enumerated twice, once with a NULL GUID and once with it's real GUID.
	// we skip over the NULL GUID as we want to report on the driver name.
	if (!lpGuid) return TRUE;

	LPDIRECTSOUND8 ds_FakeDevice;

	// create a fake device to test for caps
	hr = DirectSoundCreate8 (lpGuid, &ds_FakeDevice, NULL);

	// couldn't create so continue enumerating
	if (FAILED (hr)) return TRUE;

	DSCAPS ds_FakeCaps;
	ds_FakeCaps.dwSize = sizeof (DSCAPS);

	// get the caps
	ds_FakeDevice->GetCaps (&ds_FakeCaps);

	if (ds_FakeCaps.dwFlags & DSCAPS_EMULDRIVER)
	{
		// don't report emulated devices
		ds_FakeDevice->Release ();
		return TRUE;
	}

	if (ds_FakeCaps.dwFlags & DSCAPS_CERTIFIED)
	{
		// that's more like it, a certfied device; test some other stuff
		if (ds_FakeCaps.dwMaxSecondarySampleRate > ds_BestSampleRate && ds_FakeCaps.dwMaxHwMixingAllBuffers > ds_BestBuffers)
		{
			// if both buffers and sample rate are better than anything we've got so far, it's no contest
			strncpy (ds_BestDescription, lpcstrDescription, 127);
			strncpy (ds_BestModule, lpcstrModule, 63);
			memcpy (&ds_BestGuid, lpGuid, sizeof (GUID));
			ds_BestSampleRate = ds_FakeCaps.dwMaxSecondarySampleRate;
			ds_BestBuffers = ds_FakeCaps.dwMaxHwMixingAllBuffers;
		}
	}

	// release the fake device and continue some more
	ds_FakeDevice->Release ();
	return TRUE;
}


/*
==================
SNDDMA_InitDirect

Direct-Sound support
==================
*/
sndinitstat SNDDMA_InitDirect (void)
{
	DSBUFFERDESC	ds_BufferDesc;
	DSBCAPS			ds_BufferCaps;
	DWORD			dwSize, dwWrite;
	WAVEFORMATEX	format, pformat;
	int				reps;

	CoInitialize (NULL);

	memset ((void *) &sn, 0, sizeof (sn));

	shm = &sn;

	shm->channels = 2;
	shm->samplebits = 16;
	shm->speed = 11025;

	memset (&format, 0, sizeof(format));
	format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = shm->channels;
    format.wBitsPerSample = shm->samplebits;
    format.nSamplesPerSec = shm->speed;
    format.nBlockAlign = format.nChannels * format.wBitsPerSample / 8;
    format.cbSize = 0;
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign; 

	// enumerate the devices available - this is not so much a case of "pick the best"
	// as it is a case of "get the description"
	ds_BestDescription[0] = 0;
	ds_BestModule[0] = 0;
	memcpy (&ds_BestGuid, &DSDEVID_DefaultPlayback, sizeof (GUID));
	ds_BestBuffers = 0;
	ds_BestSampleRate = 0;

	// run the enumeration
	DirectSoundEnumerate (DSEnumCallback, NULL);

	// at this stage we've either got our primary sound device (or a better one, if installed)
	// in the variables, or we got nothing at all...
	if (!ds_BestDescription[0])
	{
		// ensure we're set up correctly
		strcpy (ds_BestDescription, "Primary Sound Driver");
		memcpy (&ds_BestGuid, &DSDEVID_DefaultPlayback, sizeof (GUID));
	}

	while (1)
	{
		// attempt to create it
		hr = DirectSoundCreate8 (&ds_BestGuid, &ds_Device, NULL);

		// success
		if (SUCCEEDED (hr)) break;

		if (hr != DSERR_ALLOCATED)
		{
			// something other than already allocated caused it to fail
			Con_SafePrintf ("DirectSound create failed\n");
			return SIS_FAILURE;
		}

		int MBReturn = MessageBox
		(
			NULL,
			"The sound hardware is in use by another Application.\n\n"
			"Select Retry to try to start sound again or Cancel to run Quake with no sound.",
			"Sound not available",
			MB_RETRYCANCEL | MB_SETFOREGROUND | MB_ICONEXCLAMATION
		);

		if (MBReturn != IDRETRY)
		{
			Con_SafePrintf ("DirectSoundCreate failure\n  hardware already in use\n");
			return SIS_NOTAVAIL;
		}
	}

	Con_SafePrintf ("DirectSound Device OK\n");
	Con_SafePrintf ("Using %s", ds_BestDescription);
	if (ds_BestModule[0]) Con_SafePrintf (" (%s)\n", ds_BestModule); else Con_SafePrintf ("\n");

	// get the caps for reporting
	ds_DeviceCaps.dwSize = sizeof (ds_DeviceCaps);

	if (DS_OK != ds_Device->GetCaps (&ds_DeviceCaps))
		Con_SafePrintf ("Couldn't get DS caps\n");
	else
	{
		if (ds_DeviceCaps.dwFlags & DSCAPS_CERTIFIED) Con_SafePrintf ("Using Certified Sound Device\n");
		if (ds_DeviceCaps.dwFlags & DSCAPS_EMULDRIVER) Con_SafePrintf ("Using Emulated Sound Device\n");
	}

	// DSSCL_EXCLUSIVE is deprecated in 8.0 or later
	if (DS_OK != ds_Device->SetCooperativeLevel (d3d_Window, DSSCL_PRIORITY))
	{
		Con_SafePrintf ("Set coop level failed\n");
		FreeSound ();
		return SIS_FAILURE;
	}

	Con_SafePrintf ("Priority Co-operative Level Set OK\n");

	// no need to create a primry buffer; directsound will do it automatically for us
	// create the secondary buffer we'll actually work with
	memset (&ds_BufferDesc, 0, sizeof(ds_BufferDesc));
	ds_BufferDesc.dwSize = sizeof(DSBUFFERDESC);
	ds_BufferDesc.dwFlags = DSBCAPS_CTRLFREQUENCY | DSBCAPS_LOCSOFTWARE | DSBCAPS_CTRLFX;
	ds_BufferDesc.dwBufferBytes = SECONDARY_BUFFER_SIZE;
	ds_BufferDesc.lpwfxFormat = &format;

	memset (&ds_BufferCaps, 0, sizeof(ds_BufferCaps));
	ds_BufferCaps.dwSize = sizeof(ds_BufferCaps);

	// we need to create a legacy buffer first so that we can obtain the LPDIRECTSOUNDBUFFER8 interface from it
	LPDIRECTSOUNDBUFFER ds_LegacyBuffer = NULL;

	// create it
	if (DS_OK != ds_Device->CreateSoundBuffer (&ds_BufferDesc, &ds_LegacyBuffer, NULL))
	{
		Con_SafePrintf ("DS:CreateSoundBuffer Failed");
		FreeSound ();
		return SIS_FAILURE;
	}

	// now obtain the directsound 8 interface from it
	if (FAILED (hr = ds_LegacyBuffer->QueryInterface (IID_IDirectSoundBuffer8, (void **) &ds_SecondaryBuffer8)))
	{
		// failed to create it
		ds_LegacyBuffer->Release ();
		Con_SafePrintf ("DS:CreateSoundBuffer Failed");
		FreeSound ();
		return SIS_FAILURE;
	}

	// don't need the legacy buffer any more
	ds_LegacyBuffer->Release ();

	shm->channels = format.nChannels;
	shm->samplebits = format.wBitsPerSample;
	shm->speed = format.nSamplesPerSec;

	if (DS_OK != ds_SecondaryBuffer8->GetCaps (&ds_BufferCaps))
	{
		Con_SafePrintf ("DS:GetCaps failed\n");
		FreeSound ();
		return SIS_FAILURE;
	}

	Con_SafePrintf ("Created Secondary Sound Buffer OK\n");

	// Make sure mixer is active
	ds_SecondaryBuffer8->Play (0, 0, DSBPLAY_LOOPING);

	Con_SafePrintf ("   %d channel(s)\n   %d bits/sample\n   %d bytes/sec\n", shm->channels, shm->samplebits, shm->speed);

	ds_SoundBufferSize = ds_BufferCaps.dwBufferBytes;

	// initialize the buffer
	reps = 0;

	// attempt to lock it
	while ((hr = ds_SecondaryBuffer8->Lock (0, ds_SoundBufferSize, (LPVOID *) &lpData, &dwSize, NULL, NULL, 0)) != DS_OK)
	{
		if (hr != DSERR_BUFFERLOST)
		{
			Con_SafePrintf ("SNDDMA_InitDirect: DS::Lock Sound Buffer Failed\n");
			FreeSound ();
			return SIS_FAILURE;
		}

		if (++reps > 10000)
		{
			Con_SafePrintf ("SNDDMA_InitDirect: DS: couldn't restore buffer\n");
			FreeSound ();
			return SIS_FAILURE;
		}
	}

	// DirectSound doesn't guarantee that a new buffer will be silent, so we make it silent
	memset (lpData, 0, dwSize);

	ds_SecondaryBuffer8->Unlock (lpData, dwSize, NULL, 0);

	// we don't want anyone to access the buffer directly w/o locking it first.
	lpData = NULL;

	ds_SecondaryBuffer8->Stop ();

	ds_SecondaryBuffer8->GetCurrentPosition (&mmstarttime.u.sample, &dwWrite);
	ds_SecondaryBuffer8->Play (0, 0, DSBPLAY_LOOPING);

	shm->soundalive = true;
	shm->splitbuffer = false;
	shm->samples = ds_SoundBufferSize/(shm->samplebits/8);
	shm->samplepos = 0;
	shm->submission_chunk = 1;
	shm->buffer = (unsigned char *) lpData;
	sample16 = (shm->samplebits/8) - 1;

	dsound_init = true;

	return SIS_SUCCESS;
}


/*
==================
SNDDMA_Init

Try to find a sound device to mix for.
Returns false if nothing is found.
==================
*/
bool SNDDMA_Init(void)
{
	sndinitstat	stat;

	dsound_init = false;

	// assume DirectSound won't initialize
	// (which may have been reasonable in 1996...)
	stat = SIS_FAILURE;

	stat = SNDDMA_InitDirect ();;

	if (stat == SIS_SUCCESS)
		Con_SafePrintf ("DirectSound Initialization Complete\n");
	else
		Con_SafePrintf ("DirectSound failed to init\n");

	if (!dsound_init)
	{
		Con_SafePrintf ("No sound device initialized\n");
		return false;
	}

	return true;
}


/*
==============
SNDDMA_GetDMAPos

return the current sample position (in mono samples read)
inside the recirculating dma buffer, so the mixing code will know
how many sample are required to fill it up.
===============
*/
int SNDDMA_GetDMAPos (void)
{
	MMTIME	mmtime;
	int		s;
	DWORD	dwWrite;

	if (dsound_init) 
	{
		mmtime.wType = TIME_SAMPLES;
		ds_SecondaryBuffer8->GetCurrentPosition (&mmtime.u.sample, &dwWrite);
		s = mmtime.u.sample - mmstarttime.u.sample;
	}
	else return 0;

	s >>= sample16;

	s &= (shm->samples-1);

	return s;
}


/*
==============
SNDDMA_Shutdown

Reset the sound device for exiting
===============
*/
void SNDDMA_Shutdown(void)
{
	FreeSound ();
}

