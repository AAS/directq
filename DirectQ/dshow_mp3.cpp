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

#include "quakedef.h"
#include <DShow.h>
#include "winquake.h"

#pragma comment (lib, "Strmiids.lib")

char *IsURLContainer (char *filename)
{
	static char urlname[1024];
	FILE *f = fopen (filename, "r");

	if (!f) return NULL;

	fscanf (f, "%1023s", urlname);
	fclose (f);

	if (!strnicmp (urlname, "http://", 7)) return urlname;
	if (!strnicmp (urlname, "https://", 8)) return urlname;

	return NULL;
}


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

	REFTIME Duration;

public:
	void PauseTrack (void)
	{
		// ensure that we can pause
		if (!this->Initialized) return;

		// ensure that we're not already paused
		if (this->Paused) return;

		// we're paused now
		this->ds_Control->Stop ();
		this->Paused = true;
	}

	void ResumeTrack (void)
	{
		// ensure that we can resume
		if (!this->Initialized) return;

		// ensure that we're paused before we resume
		if (!this->Paused) return;

		// we're not paused any more now
		this->ds_Control->Run ();
		this->Paused = false;
	}

	void AdjustVolume (void)
	{
		// ensure that we can change volume
		if (!this->Initialized) return;

		long db;

		// whoever in ms designed this must have really prided themselves on being so technically correct.  bastards.
		// decibels are great if you're an audio engineer, but if you're just writing a simple sliding volume control...
		if (bgmvolume.value <= 0)
			db = -10000;
		else if (bgmvolume.value >= 1)
			db = 0;
		else db = log10 (bgmvolume.value) * 2000;

		// set the volume
		this->ds_Audio->put_Volume (db);
	}

	CDSClass (char *FileName, bool looping)
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

		// set up everything
		if (FAILED (hr = CoCreateInstance (CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER, IID_IGraphBuilder, (void **) &this->ds_Graph))) return;
		if (FAILED (hr = this->ds_Graph->QueryInterface (IID_IMediaControl, (void **) &this->ds_Control))) return;
		if (FAILED (hr = this->ds_Graph->QueryInterface (IID_IBasicAudio, (void **) &this->ds_Audio))) return;
		if (FAILED (hr = this->ds_Graph->QueryInterface (IID_IMediaEvent, (void **) &this->ds_Event))) return;
		if (FAILED (hr = this->ds_Graph->QueryInterface (IID_IMediaPosition, (void **) &this->ds_Position))) return;

		// it's initialized now
		this->Initialized = true;

		// flag if we're looping
		this->Looping = looping;

		wchar_t WCFileName[1024];
		char *urlname = IsURLContainer (FileName);

		if (urlname)
		{
			// convert to wide char cos DirectShow requires Wide Chars (bastards!)
			mbstowcs (WCFileName, urlname, 1023);
			if (FAILED (hr = this->ds_Graph->RenderFile (WCFileName, NULL))) return;
		}
		else
		{
			// convert to wide char cos DirectShow requires Wide Chars (bastards!)
			mbstowcs (WCFileName, FileName, 256);
			if (FAILED (hr = this->ds_Graph->RenderFile (WCFileName, NULL))) return;
		}

		// attempt to play it
		if (FAILED (hr = this->ds_Control->Run ())) return;

		// we can't get the duration until it's actually running
		// the event notification sends spurious EC_COMPLETE messages on some machines
		// (possibly because the k-lite codec pack hijacks directshow) so we poll for stopping each frame instead
		this->ds_Position->get_Duration (&this->Duration);

		// finish up by adjusting volume
		this->AdjustVolume ();

		// we're playing now
		this->Playing = true;
		this->Paused = false;
	}

	void CheckStopTime (void)
	{
		static int checkframe = 0;

		// ensure that we can process events
		if (!this->Initialized) return;
		if (!this->Playing) return;
		if ((++checkframe) & 7) return;

		// we could be streaming over the web or paused so we need to check position
		REFTIME currpos;
		ds_Position->get_CurrentPosition (&currpos);

		// currpos will be == when stopped so we need a little fuzziness and >=
		if ((currpos + 0.001) >= this->Duration)
		{
			if (!this->Looping)
			{
				// if we're not looping we stop it
				this->ds_Control->Stop ();

				// not playing now
				this->Playing = false;
			}

			// reset to start either way
			this->ds_Position->put_CurrentPosition (0);
		}
	}

	~CDSClass (void)
	{
		// stop playing and abort whatever we were doing
		if (this->ds_Control) this->ds_Control->Stop ();
		if (this->ds_Graph) this->ds_Graph->Abort ();

		// release them all
		SAFE_RELEASE (this->ds_Position);
		SAFE_RELEASE (this->ds_Event);
		SAFE_RELEASE (this->ds_Audio);
		SAFE_RELEASE (this->ds_Control);
		SAFE_RELEASE (this->ds_Graph);
	}
};


// this must be created before playing each track
CDSClass *DSManager = NULL;


void MediaPlayer_Init (void)
{
	// just attempt to initialize COM
	hr = CoInitialize (NULL);

	if (FAILED (hr))
	{
		// if this fails we likely have a broken OS...
		Sys_Error ("MediaPlayer_Init: FAILED (hr) on CoInitialize\n");
		return;
	}
}


void MediaPlayer_Shutdown (void)
{
	// stop everything
	MediaPlayer_Stop ();

	// unload COM
	CoUninitialize ();
}


bool FindMediaFile (char *subdir, int track, bool looping)
{
	char **foundtracks = NULL;
	bool mediaplaying = false;

	// welcome to the world of char ***!  we want to be able to play all types of tracks here
	// don't sort the result so that the tracks will appear in the order of the specified gamedirs
	int listlen = COM_BuildContentList (&foundtracks, subdir, ".*", NO_PAK_CONTENT | PREPEND_PATH | NO_SORT_RESULT);

	if (listlen)
	{
		// we need to walk the entire list anyway to free memory so may as well search for the specified file here too
		// COM_BuildContentList returns files in alphabetical order so we just take the correct number
		for (int i = 0; i < listlen; i++)
		{
			// quake tracks are 2-based because the first track on the CD is the data track and CD tracks are 1-based
			if (i == (track - 2) && !mediaplaying)
			{
				// attempt to play it
				DSManager = new CDSClass (foundtracks[i], looping);
				mediaplaying = true;
			}

			// it's our responsibility to free memory allocated by COM_BuildContentList
			Zone_Free (foundtracks[i]);
		}
	}

	// return if the media is playing
	return mediaplaying;
}


bool MediaPlayer_Play (int track, bool looping)
{
	// stop any previous tracks
	MediaPlayer_Stop ();

	// every other ID game that uses music tracks in the filesystem has them in music, so it's
	// a reasonable assumption that that's what users will be expecting...
	if (FindMediaFile ("music/", track, looping)) return true;

	// darkplaces on the other hand does something *completely* different.  standards?  who needs 'em!
	if (FindMediaFile ("sound/cdtracks/", track, looping)) return true;

	return false;
}


void MediaPlayer_Update (void)
{
	// check that DS is up
	if (!DSManager) return;

	// run all events
	DSManager->CheckStopTime ();
}


void MediaPlayer_Stop (void)
{
	if (DSManager)
	{
		// just delete the manager object to force a stop
		delete DSManager;
		DSManager = NULL;
	}
}


void MediaPlayer_Pause (void)
{
	if (!DSManager) return;

	DSManager->PauseTrack ();
}


void MediaPlayer_Resume (void)
{
	if (!DSManager) return;

	DSManager->ResumeTrack ();
}


void MediaPlayer_ChangeVolume (void)
{
	if (!DSManager) return;

	DSManager->AdjustVolume ();
}

