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

/*
================================================================================================================================

DIRECTSHOW INTERFACE

This runs supplementary to, and not instead of, the stock Q1 CD interface.  CD Audio is preferred but - if not initialized,
DirectShow will take over in it's place.

It's not restricted to MP3 files - pretty much any file type which you have a codec installed for can be played through this.
I've deliberately written it in such a way so as to ensure that there are as few restrictions as possible.

When I say "MP3 Files" in anything here, you should read "any audio format for which a codec is installed".

MP3 files must be in the "Music" directory under your game.  You can call them anything you like, but they will be played
in standard filename order, so be certain you have that correct.

The current gamedir is searched first, if nothing is found it falls back on ID1.  This causes ID1 to be searched twice
on some systems but it's no real great shakes.

BIG THANKS to http://www.flipcode.com for one of the best writeups I've ever seen on this, with loads of practical real
world advice for EXACTLY the type of thing someone writing a DirectShow app would want to do.

NO THANKS AT ALL to http://msdn.microsoft.com for one of the worst writeups I've ever seen on anything at all.  Ever.

Note: DS_FindTrack is a bit messy right now; I should go back and clean it up.

================================================================================================================================
*/


#include <DShow.h>
#include <windows.h>
#include "quakedef.h"
#include "winquake.h"

#pragma comment (lib, "Strmiids.lib")


// a class!!!  it does help to keep the code a lot tidier in here; DirectShow is MESSY!
class CDSClass
{
private:
	bool Initialized;
	bool Playing;
	bool Looping;
	bool Paused;

	IGraphBuilder *ds_Graph;
	IMediaControl *ds_Control;
	IBasicAudio *ds_Audio;
	IMediaEventEx *ds_Event;
	IMediaPosition *ds_Position;

public:
	void PauseTrack (void)
	{
		// ensure that we can pause
		if (!this->ds_Control) return;

		// ensure that we're not already paused
		if (this->Paused) return;

		// we're paused now
		this->ds_Control->Stop ();
		this->Paused = true;
	}

	void ResumeTrack (void)
	{
		// ensure that we can resume
		if (!this->ds_Control) return;

		// ensure that we're paused before we resume
		if (this->Paused) return;

		// we're not paused any more now
		this->ds_Control->Run ();
		this->Paused = false;
	}

	void AdjustVolume (void)
	{
		// ensure that we can change volume
		if (!this->ds_Audio) return;

		long db;

		// whoever in ms designed this must have really prided themselves on being so technically correct.  bastards.
		// decibels are great if you're an audio engineer, but if you're just writing a simple sliding volume control...
		if (bgmvolume.value <= 0)
			db = -10000;
		else if (bgmvolume.value >= 1)
			db = 0;
		else
			db = log10 (bgmvolume.value) * 2000;

		// set the volume
		this->ds_Audio->put_Volume (db);
	}

	CDSClass (wchar_t *FileName, bool looping)
	{
		this->Initialized = false;
		this->Playing = false;
		this->Looping = false;
		this->Paused = false;

		// init all to NULL
		this->ds_Audio = NULL;
		this->ds_Control = NULL;
		this->ds_Event = NULL;
		this->ds_Graph = NULL;
		this->ds_Position = NULL;

		HRESULT hr;

		// set up everything
		if (FAILED (hr = CoCreateInstance (CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER, IID_IGraphBuilder, (void **) &this->ds_Graph))) return;
		if (FAILED (hr = this->ds_Graph->QueryInterface (IID_IMediaControl, (void **) &this->ds_Control))) return;
		if (FAILED (hr = this->ds_Graph->QueryInterface (IID_IBasicAudio, (void **) &this->ds_Audio))) return;
		if (FAILED (hr = this->ds_Graph->QueryInterface (IID_IMediaEvent, (void **) &this->ds_Event))) return;
		if (FAILED (hr = this->ds_Graph->QueryInterface (IID_IMediaPosition, (void **) &this->ds_Position))) return;

		// set up notification on the event window - see WM_GRAPHEVENT in MainWndProc
		this->ds_Event->SetNotifyWindow ((OAHWND) d3d_Window, WM_GRAPHEVENT, 0);
		this->ds_Event->SetNotifyFlags (0);

		// it's initialized now
		this->Initialized = true;

		// flag if we're looping
		this->Looping = looping;

		// attempt to play it
		if (FAILED (hr = this->ds_Graph->RenderFile (FileName, NULL))) return;
		if (FAILED (hr = this->ds_Control->Run ())) return;

		// finish up by adjusting volume
		this->AdjustVolume ();

		// we're playing now
		this->Playing = true;
		this->Paused = false;
	}

	void DoEvents (void)
	{
		// ensure that we can process events
		if (!this->ds_Event) return;

		long EventCode, Param1, Param2;

		// read all events
		while (this->ds_Event->GetEvent (&EventCode, &Param1, &Param2, 0) != E_ABORT)
		{
			switch (EventCode)
			{
			case EC_COMPLETE:
				// if we're not looping we stop it
				if (!this->Looping) this->ds_Control->Stop ();

				// reset to start either way
				this->ds_Position->put_CurrentPosition (0);
				break;

			default:
				// we're not interested in these events
				break;
			}

			// DirectShow stores some data for events internally so we must be sure to free it
			this->ds_Event->FreeEventParams (EventCode, Param1, Param2);
		}
	}

	~CDSClass (void)
	{
		// stop playing and abort whatever we were doing
		if (this->ds_Control) this->ds_Control->Stop ();
		if (this->ds_Graph) this->ds_Graph->Abort ();

		// release them all
		SAFE_RELEASE (this->ds_Audio);
		SAFE_RELEASE (this->ds_Control);
		SAFE_RELEASE (this->ds_Event);
		SAFE_RELEASE (this->ds_Position);
		SAFE_RELEASE (this->ds_Graph);
	}
};


// this must be created before playing each track
CDSClass *DSManager = NULL;

cvar_t ds_musicdir ("ds_musicdir", "music", CVAR_ARCHIVE);


void DS_Event (void)
{
	// check that DS is up
	if (!DSManager) return;

	// run all events
	DSManager->DoEvents ();
}


void DS_Init (void)
{
	// just attempt to initialize COM
	HRESULT hr = CoInitialize (NULL);

	if (FAILED (hr))
	{
		// if this fails we likely have a broken OS...
		Sys_Error ("DS_Init: FAILED (hr) on CoInitialize\n");
		return;
	}
}


void DS_Shutdown (void)
{
	// stop everything
	DS_Stop ();

	// unload COM
	CoUninitialize ();
}


bool DS_FindTrack (const char *trackdir, const char *musicdir, int track, bool looping)
{
	char MusicPath[256];

	// build the path to search
	// we don't need this for FindFirstFile/FindNextFile, but we do for DirectShow
	sprintf (MusicPath, "%s\\%s", trackdir, musicdir);

	WIN32_FIND_DATA FindFileData;
	HANDLE hFind = INVALID_HANDLE_VALUE;

	// Quake track numbers start at 2 as they are CD track numbers (1 based, #1 is data on the Quake CD)
	// so here we convert it into what we expect it to be.
	track -= 2;

	// look for a file
	hFind = FindFirstFile (va ("%s\\*.*", MusicPath), &FindFileData);

	if (hFind == INVALID_HANDLE_VALUE)
	{
		// found no files
		FindClose (hFind);

		// false as there was nothing to check against
		return false;
	}

	int seektrack = 0;

	do
	{
		// not interested in these types
		if (FindFileData.dwFileAttributes & FILE_ATTRIBUTE_COMPRESSED) continue;
		if (FindFileData.dwFileAttributes & FILE_ATTRIBUTE_ENCRYPTED) continue;
		if (FindFileData.dwFileAttributes & FILE_ATTRIBUTE_OFFLINE) continue;
		if (FindFileData.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) continue;
		if (FindFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;

		if (track == seektrack)
		{
			wchar_t WCFileName[256];
			HRESULT hr;

			// convert to wide char cos DirectShow requires Wide Chars (bastards!)
			mbstowcs (WCFileName, va ("%s\\%s", MusicPath, FindFileData.cFileName), 256);

			// attempt to play it
			DSManager = new CDSClass (WCFileName, looping);

			// no need to find any more
			FindClose (hFind);
			return true;
		}

		// go to the next track
		seektrack++;
	} while (FindNextFile (hFind, &FindFileData));

	// close the finder
	FindClose (hFind);

	// didn't find anything
	return false;
}


bool DS_Play (int track, bool looping)
{
	// stop any previous tracks
	DS_Stop ();

	// ensure
	COM_CheckContentDirectory (&ds_musicdir, false);

	// look for the track in the current gamedir, then fall back on ID1
	// note - this will actually search ID1 twice if the current game is ID1
	if (DS_FindTrack (com_gamedir, ds_musicdir.string, track, looping)) return true;
	if (DS_FindTrack (GAMENAME, ds_musicdir.string, track, looping)) return true;

	// because ONE engine has gotten popular enough to insist on doing things it's own way and get away with it
	if (DS_FindTrack (com_gamedir, "sound/cdtracks", track, looping)) return true;
	if (DS_FindTrack (GAMENAME, "sound/cdtracks", track, looping)) return true;

	// if things get really screwy we fall back on /music as a final fallback
	if (DS_FindTrack (com_gamedir, "music", track, looping)) return true;
	if (DS_FindTrack (GAMENAME, "music", track, looping)) return true;

	return false;
}


void DS_Stop (void)
{
	if (DSManager)
	{
		// just delete the manager object to force a stop
		delete DSManager;
		DSManager = NULL;
	}
}


void DS_Pause (void)
{
	if (!DSManager) return;

	DSManager->PauseTrack ();
}


void DS_Resume (void)
{
	if (!DSManager) return;

}


void DS_ChangeVolume (void)
{
	if (!DSManager) return;

	DSManager->AdjustVolume ();
}


