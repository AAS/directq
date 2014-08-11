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
// snd_dma.c -- main control for any streaming sound output device

#include "quakedef.h"
#include "d3d_model.h"
#include "winquake.h"
#include <vector>

extern LPDIRECTSOUNDBUFFER8 ds_SecondaryBuffer8;
extern DWORD ds_SoundBufferSize;


void S_Play (void);
void S_Play2 (void);
void S_PlayVol (void);
void S_SoundList (void);
void S_Update_();
void S_StopAllSounds (bool clear);
void S_StopAllSoundsC (void);

// =======================================================================
// Internal sound data & structures
// =======================================================================

channel_t   channels[MAX_CHANNELS];
int			total_channels;

int				snd_blocked = 0;
static bool	snd_ambient = 1;
bool		snd_initialized = false;

// pointer should go away
volatile dma_t  *shm = 0;
volatile dma_t sn;

vec3_t		listener_origin;
vec3_t		listener_forward;
vec3_t		listener_right;
vec3_t		listener_up;

cvar_t sound_nominal_clip_dist ("snd_clipdist", 1500, CVAR_ARCHIVE);

int			soundtime;		// sample PAIRS
int   		paintedtime; 	// sample PAIRS


std::vector<sfx_t *> known_sfx;

sfx_t		*ambient_sfx[NUM_AMBIENTS];

int 		desired_speed = 11025;
int 		desired_bits = 16;

int sound_started = 0;

cvar_t bgmvolume ("bgmvolume", "1", CVAR_ARCHIVE);
cvar_t volume ("volume", "0.7", CVAR_ARCHIVE);

cvar_t nosound ("nosound", "0");
cvar_t precache ("precache", "1");
cvar_t bgmbuffer ("bgmbuffer", "4096");
cvar_t ambient_level ("ambient_level", "0.3", CVAR_ARCHIVE);
cvar_t ambient_fade ("ambient_fade", "100", CVAR_ARCHIVE);
cvar_t snd_noextraupdate ("snd_noextraupdate", "0");
cvar_t snd_show ("snd_show", "0");
cvar_t _snd_mixahead ("_snd_mixahead", "0.1", CVAR_ARCHIVE);


// ====================================================================
// User-setable variables
// ====================================================================


void S_AmbientOff (void)
{
	snd_ambient = false;
}


void S_AmbientOn (void)
{
	snd_ambient = true;
}


void S_SoundInfo_f (void)
{
	if (!sound_started || !shm)
	{
		Con_Printf ("sound system not started\n");
		return;
	}

	Con_Printf ("%5d stereo\n", shm->channels - 1);
	Con_Printf ("%5d samples\n", shm->samples);
	Con_Printf ("%5d samplepos\n", shm->samplepos);
	Con_Printf ("%5d samplebits\n", shm->samplebits);
	Con_Printf ("%5d submission_chunk\n", shm->submission_chunk);
	Con_Printf ("%5d speed\n", shm->speed);
	Con_Printf ("0x%x dma buffer\n", shm->buffer);
	Con_Printf ("%5d total_channels\n", total_channels);
}


/*
================
S_Startup
================
*/

void S_Startup (void)
{
	int		rc;

	if (!snd_initialized)
		return;

	rc = SNDDMA_Init ();

	if (!rc)
	{
		sound_started = 0;
		return;
	}

	sound_started = 1;
}


/*
================
S_Init
================
*/
cmd_t S_Play_Cmd ("play", S_Play);
cmd_t S_Play2_Cmd ("play2", S_Play2);
cmd_t S_PlayVol_Cmd ("playvol", S_PlayVol);
cmd_t S_StopAllSoundsC_Cmd ("stopsound", S_StopAllSoundsC);
cmd_t S_SoundList_Cmd ("soundlist", S_SoundList);
cmd_t S_SoundInfo_f_Cmd ("soundinfo", S_SoundInfo_f);

void S_Init (void)
{
	// alloc the cache that we'll use for the rest of the game
	SoundCache = new CQuakeCache ();
	SoundHeap = new CQuakeZone ();

	// always init this otherwise we'll crash during sound clearing
	S_ClearSounds ();

	if (COM_CheckParm ("-nosound"))
		return;

	Con_Printf ("Sound Initialization\n");
	snd_initialized = true;
	S_Startup ();
	Con_Printf ("Sound sampling rate: %i\n", shm->speed);
	S_StopAllSounds (true);
}


void S_InitAmbients (void)
{
	ambient_sfx[AMBIENT_WATER] = S_PrecacheSound ("ambience/water1.wav");
	ambient_sfx[AMBIENT_SKY] = S_PrecacheSound ("ambience/wind2.wav");
}



// =======================================================================
// Shutdown sound engine
// =======================================================================

void S_Shutdown (void)
{
	if (!sound_started) return;

	if (shm) shm->gamealive = 0;

	shm = 0;
	sound_started = 0;

	SNDDMA_Shutdown();
}


// =======================================================================
// Load a sound
// =======================================================================

/*
==================
S_FindName

==================
*/
sfx_t *S_FindName (char *name)
{
	int		i;
	sfx_t	*sfx;
	int		slot = -1;

	if (!name) Sys_Error ("S_FindName: NULL\n");

	if (strlen (name) >= MAX_QPATH)
		Sys_Error ("Sound name too long: %s", name);

	// see if already loaded
	for (i = 0; i < known_sfx.size (); i++)
	{
		if (!known_sfx[i])
		{
			// this is a reusable slot
			slot = i;
			continue;
		}

		if (!strcmp (known_sfx[i]->name, name))
		{
			known_sfx[i]->sndcache = NULL;
			return known_sfx[i];
		}
	}

	// alloc a new SFX
	sfx = (sfx_t *) SoundHeap->Alloc (sizeof (sfx_t));

	if (slot >= 0)
		known_sfx[slot] = sfx;
	else known_sfx.push_back (sfx);

	strcpy (sfx->name, name);
	sfx->sndcache = NULL;

	return sfx;
}


void S_ClearSounds (void)
{
	for (int i = 0; i < known_sfx.size (); i++)
	{
		if (known_sfx[i])
		{
			if (known_sfx[i]->Buffer)
			{
				known_sfx[i]->Buffer->Release ();
				known_sfx[i]->Buffer = NULL;
			}

			SoundHeap->Free (known_sfx[i]);
			known_sfx[i] = NULL;
		}
	}

	known_sfx.clear ();
}


/*
==================
S_TouchSound

==================
*/
void S_TouchSound (char *name)
{
	sfx_t	*sfx;

	if (!sound_started)
		return;

	sfx = S_FindName (name);
	sfx->sndcache = NULL;
}


/*
==================
S_PrecacheSound

==================
*/
sfx_t *S_PrecacheSound (char *name)
{
	sfx_t	*sfx;

	if (!sound_started || nosound.value)
		return NULL;

	// find the name for it and set it's initial cache to null
	sfx = S_FindName (name);
	sfx->sndcache = NULL;

	// cache it in
	if (precache.value)
		S_LoadSound (sfx);

	return sfx;
}


//=============================================================================

/*
=================
SND_PickChannel
=================
*/
channel_t *SND_PickChannel (int entnum, int entchannel)
{
	int ch_idx;
	int first_to_die;
	int life_left;

	// Check for replacement sound, or find the best one to replace
	first_to_die = -1;
	life_left = 0x7fffffff;

	for (ch_idx = NUM_AMBIENTS; ch_idx < NUM_AMBIENTS + MAX_DYNAMIC_CHANNELS; ch_idx++)
	{
		if (entchannel != 0 &&		// channel 0 never overrides
			channels[ch_idx].entnum == entnum &&
			(channels[ch_idx].entchannel == entchannel || entchannel == -1))
		{
			// allways override sound from same entity
			first_to_die = ch_idx;
			break;
		}

		// don't let monster sounds override player sounds
		if (channels[ch_idx].entnum == cl.viewentity && entnum != cl.viewentity && channels[ch_idx].sfx)
			continue;

		if (channels[ch_idx].end - paintedtime < life_left)
		{
			life_left = channels[ch_idx].end - paintedtime;
			first_to_die = ch_idx;
		}
	}

	if (first_to_die == -1)
		return NULL;

	if (channels[first_to_die].sfx)
		channels[first_to_die].sfx = NULL;

	return &channels[first_to_die];
}

/*
=================
SND_Spatialize
=================
*/
void SND_Spatialize (channel_t *ch)
{
	vec_t dot;
	vec_t dist;
	vec_t lscale, rscale, scale;
	vec3_t source_vec;

	// anything coming from the view entity will allways be full volume
	if (ch->entnum == cl.viewentity)
	{
		ch->leftvol = ch->master_vol;
		ch->rightvol = ch->master_vol;
		return;
	}

	// fixme - do we actually need to update ch->origin here or can we just use a different origin vec (a local?) purely for spatialization?
	if (ch->entnum > 0 && cls.state == ca_connected) // && cl_gameplayfix_soundsmovewithentities.integer)
	{
		if (cl_entities[ch->entnum] && cl_entities[ch->entnum]->model)
		{
			// brush model entities have their origins at 0|0|0 and move relative to that so we need to use trueorigin instead
			// update sound origin
			VectorCopy2 (ch->origin, cl_entities[ch->entnum]->trueorigin);
		}
	}

	// calculate stereo seperation and distance attenuation
	VectorSubtract (ch->origin, listener_origin, source_vec);
	dist = VectorNormalize (source_vec) * ch->dist_mult;
	dot = DotProduct (listener_right, source_vec);

	if (shm->channels == 1)
	{
		rscale = 1.0;
		lscale = 1.0;
	}
	else
	{
		rscale = 1.0 + dot;
		lscale = 1.0 - dot;
	}

	// add in distance effect
	scale = (1.0 - dist) * rscale;
	ch->rightvol = (int) (ch->master_vol * scale);

	if (ch->rightvol < 0)
		ch->rightvol = 0;

	scale = (1.0 - dist) * lscale;
	ch->leftvol = (int) (ch->master_vol * scale);

	if (ch->leftvol < 0)
		ch->leftvol = 0;
}


HRESULT CreateBasicBuffer (LPDIRECTSOUNDBUFFER8 *ppDsb8, wavinfo_t *info, void *data)
{
	extern LPDIRECTSOUND8 ds_Device;
	WAVEFORMATEX wfx;
	DSBUFFERDESC dsbdesc;
	LPDIRECTSOUNDBUFFER pDsb = NULL;

	// Set up WAV format structure.
	memset (&wfx, 0, sizeof (WAVEFORMATEX));
	wfx.wFormatTag = WAVE_FORMAT_PCM;
	wfx.nChannels = info->channels;
	wfx.nSamplesPerSec = info->rate;
	wfx.nBlockAlign = info->blockalign;
	wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
	wfx.wBitsPerSample = info->width * 8;

	// Set up DSBUFFERDESC structure.
	memset (&dsbdesc, 0, sizeof (DSBUFFERDESC));
	dsbdesc.dwSize = sizeof (DSBUFFERDESC);
	dsbdesc.dwFlags = DSBCAPS_CTRLPAN | DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLFREQUENCY | DSBCAPS_GLOBALFOCUS;
	dsbdesc.dwBufferBytes = info->samples;
	dsbdesc.lpwfxFormat = &wfx;

	// Create buffer.
	hr = ds_Device->CreateSoundBuffer (&dsbdesc, &pDsb, NULL);

	if (SUCCEEDED (hr))
	{
		hr = pDsb->QueryInterface (IID_IDirectSoundBuffer8, (LPVOID *) ppDsb8);
		pDsb->Release ();

		void *audioptr[2] = {NULL, NULL};
		DWORD lockbytes[2] = {0, 0};

		ppDsb8[0]->Lock (0, 0, (void **) &audioptr[0], &lockbytes[0], (void **) &audioptr[1], &lockbytes[1], DSBLOCK_ENTIREBUFFER);
		memcpy (audioptr[0], data, info->samples);
		ppDsb8[0]->Unlock ((void *) audioptr[0], lockbytes[0], (void *) audioptr[1], lockbytes[1]);
	}

	return hr;
}


LONG LinearToDB (float linear)
{
	// fixme - do a lookup for this...
	LONG db;

	// allow players to scale it down
	linear *= volume.value;

	// whoever in ms designed this must have really prided themselves on being so technically correct.  bastards.
	// decibels are great if you're an audio engineer, but if you're just writing a simple sliding volume control...
	if (linear <= 0)
		db = -10000;
	else if (linear >= 1)
		db = 0;
	else db = log10 (linear) * 2000;

	if (db < DSBVOLUME_MIN) db = DSBVOLUME_MIN;
	if (db > DSBVOLUME_MAX) db = DSBVOLUME_MAX;

	return db;
}


void S_PlaySoundFromBuffer (int entnum, int entchannel, sfx_t *sfx, vec3_t origin, float fvol, float attenuation)
{
	if (sfx->Buffer)
	{
		// fixme - do looping sounds too...
		// fixme - what happens if we need to play the same sound twice?  Clone the buffer?
		sfx->Buffer->Stop ();
		sfx->Buffer->SetCurrentPosition (0);
		sfx->Buffer->SetVolume (LinearToDB (fvol));
		sfx->Buffer->Play (0, 0, 0);
	}
	else
	{
		S_StartSound (entnum, entchannel, sfx, origin, fvol, attenuation);
	}
}


// =======================================================================
// Start a sound effect
// =======================================================================

void S_StartSound (int entnum, int entchannel, sfx_t *sfx, vec3_t origin, float fvol, float attenuation)
{
	channel_t *target_chan, *check;
	sfxcache_t	*sc;
	int		vol;
	int		ch_idx;
	int		skip;

	if (!sound_started) return;
	if (!sfx) return;
	if (nosound.value) return;

	vol = fvol * 255;

	// pick a channel to play on
	target_chan = SND_PickChannel (entnum, entchannel);

	if (!target_chan)
		return;

	// spatialize
	memset (target_chan, 0, sizeof (*target_chan));
	VectorCopy2 (target_chan->origin, origin);
	target_chan->dist_mult = attenuation / sound_nominal_clip_dist.value;
	target_chan->master_vol = vol;
	target_chan->entnum = entnum;
	target_chan->entchannel = entchannel;
	SND_Spatialize (target_chan);

	if (!target_chan->leftvol && !target_chan->rightvol)
		return;		// not audible at all

	// new channel
	sc = S_LoadSound (sfx);

	if (!sc)
	{
		target_chan->sfx = NULL;
		return;		// couldn't load the sound's data
	}

	target_chan->sfx = sfx;
	target_chan->pos = 0.0;
	target_chan->end = paintedtime + sc->length;
	sc->loopstart = -1;

	// if an identical sound has also been started this frame, offset the pos
	// a bit to keep it from just making the first one louder
	check = &channels[NUM_AMBIENTS];

	for (ch_idx = NUM_AMBIENTS; ch_idx < NUM_AMBIENTS + MAX_DYNAMIC_CHANNELS; ch_idx++, check++)
	{
		if (check == target_chan)
			continue;

		if (check->sfx == sfx && !check->pos)
		{
			skip = rand () % (int) (0.1 * shm->speed);

			if (skip >= target_chan->end)
				skip = target_chan->end - 1;

			target_chan->pos += skip;
			target_chan->end -= skip;
			break;
		}
	}
}

void S_StopSound (int entnum, int entchannel)
{
	int i;

	for (i = 0; i < MAX_DYNAMIC_CHANNELS; i++)
	{
		if (channels[i].entnum == entnum &&
			channels[i].entchannel == entchannel)
		{
			channels[i].end = 0;
			channels[i].sfx = NULL;
			return;
		}
	}
}


void CDAudio_Stop (void);

void S_StopAllSounds (bool clear)
{
	// stop these as well
	CDAudio_Stop ();

	if (!sound_started)
		return;

	total_channels = MAX_DYNAMIC_CHANNELS + NUM_AMBIENTS;	// no statics

	for (int i = 0; i < MAX_CHANNELS; i++)
	{
		channels[i].end = 0;
		channels[i].sfx = NULL;
	}

	memset (channels, 0, MAX_CHANNELS * sizeof (channel_t));

	if (clear) S_ClearBuffer ();
}

void S_StopAllSoundsC (void)
{
	S_StopAllSounds (true);
}


void S_ClearBuffer (void)
{
	int		clear;

	if (!sound_started || !shm || (!shm->buffer && !ds_SecondaryBuffer8))
		return;

	if (shm->samplebits == 8)
		clear = 0x80;
	else clear = 0;

	DWORD	dwSize;
	DWORD	*pData;

	if (!S_GetBufferLock (0, ds_SoundBufferSize, (LPVOID *) &pData, &dwSize, NULL, NULL, 0)) return;

	memset (pData, clear, shm->samples * shm->samplebits / 8);
	ds_SecondaryBuffer8->Unlock (pData, dwSize, NULL, 0);
}


/*
=================
S_StaticSound
=================
*/
void S_StaticSound (sfx_t *sfx, vec3_t origin, float vol, float attenuation)
{
	channel_t	*ss;
	sfxcache_t		*sc;

	if (!sfx)
		return;

	if (total_channels == MAX_CHANNELS)
	{
		Con_Printf ("total_channels == MAX_CHANNELS\n");
		return;
	}

	ss = &channels[total_channels];
	total_channels++;

	sc = S_LoadSound (sfx);

	if (!sc)
		return;

	if (sc->loopstart == -1)
	{
		Con_DPrintf ("Sound %s not looped\n", sfx->name);

		// most static sounds in Quake loop from 0, so if this happens we'll just devprint it and take a wild guess!
		sc->loopstart = 0;
	}

	ss->sfx = sfx;
	VectorCopy2 (ss->origin, origin);
	ss->master_vol = vol;
	ss->dist_mult = (attenuation / 64) / sound_nominal_clip_dist.value;
	ss->end = paintedtime + sc->length;

	SND_Spatialize (ss);
}


//=============================================================================

/*
===================
S_UpdateAmbientSounds
===================
*/
void S_UpdateAmbientSounds (double frametime)
{
	float		vol;
	int			ambient_channel;
	channel_t	*chan;

	// calc ambient sound levels
	if (!snd_ambient) return;
	if (!cl.worldmodel) return;
	if (!cls.maprunning) return;

	mleaf_t *l = Mod_PointInLeaf (listener_origin, cl.worldmodel);

	if (!l || !ambient_level.value)
	{
		for (ambient_channel = 0; ambient_channel < NUM_AMBIENTS; ambient_channel++)
			channels[ambient_channel].sfx = NULL;

		return;
	}

	for (ambient_channel = 0; ambient_channel < NUM_AMBIENTS; ambient_channel++)
	{
		chan = &channels[ambient_channel];
		chan->sfx = ambient_sfx[ambient_channel];

		if ((vol = ambient_level.value * l->ambient_sound_level[ambient_channel]) < 8)
			vol = 0;

		// don't adjust volume too fast
		if (chan->master_vol < vol)
		{
			chan->master_vol += frametime * ambient_fade.value;

			if (chan->master_vol > vol)
				chan->master_vol = vol;
		}
		else if (chan->master_vol > vol)
		{
			chan->master_vol -= frametime * ambient_fade.value;

			if (chan->master_vol < vol)
				chan->master_vol = vol;
		}

		chan->leftvol = chan->rightvol = chan->master_vol;
	}
}


/*
============
S_Update

Called once each time through the main loop
============
*/
void S_Update (double frametime, vec3_t origin, vec3_t forward, vec3_t right, vec3_t up)
{
	int			i, j;
	int			total;
	channel_t	*ch;
	channel_t	*combine;

	if (!sound_started || (snd_blocked > 0)) return;

	extern int snd_NumSounds;

	// Con_Printf ("Loaded %i sounds\n", snd_NumSounds);

	VectorCopy2 (listener_origin, origin);
	VectorCopy2 (listener_forward, forward);
	VectorCopy2 (listener_right, right);
	VectorCopy2 (listener_up, up);

	// update general area ambient sound sources
	S_UpdateAmbientSounds (frametime);

	combine = NULL;

	// update spatialization for static and dynamic sounds
	ch = channels + NUM_AMBIENTS;

	for (i = NUM_AMBIENTS; i < total_channels; i++, ch++)
	{
		// no sound in this channel
		if (!ch->sfx) continue;

		// respatialize channel
		SND_Spatialize (ch);

		// no volume
		if (ch->leftvol < 1 && ch->rightvol < 1) continue;

		// try to combine static sounds with a previous channel of the same
		// sound effect so we don't mix five torches every frame
		if (i >= MAX_DYNAMIC_CHANNELS + NUM_AMBIENTS)
		{
			// see if it can just use the last one
			if (combine && combine->sfx == ch->sfx)
			{
				combine->leftvol += ch->leftvol;
				combine->rightvol += ch->rightvol;
				ch->leftvol = ch->rightvol = 0;
				continue;
			}

			// search for one
			combine = channels + MAX_DYNAMIC_CHANNELS + NUM_AMBIENTS;

			for (j = MAX_DYNAMIC_CHANNELS + NUM_AMBIENTS; j < i; j++, combine++)
				if (combine->sfx == ch->sfx)
					break;

			if (j == total_channels)
				combine = NULL;
			else
			{
				if (combine != ch)
				{
					combine->leftvol += ch->leftvol;
					combine->rightvol += ch->rightvol;
					ch->leftvol = ch->rightvol = 0;
				}

				continue;
			}
		}
	}

	// debugging output
	if (snd_show.value)
	{
		total = 0;
		ch = channels;

		for (i = 0; i < total_channels; i++, ch++)
		{
			if (ch->sfx && (ch->leftvol > 0 || ch->rightvol > 0))
			{
				Con_Printf ("%3i %3i %s\n", ch->leftvol, ch->rightvol, ch->sfx->name);
				total++;
			}
		}

		Con_Printf ("----(%i)----\n", total);
	}

	// mix some sound
	S_Update_ ();
}

void GetSoundtime (void)
{
	int		samplepos;
	static	int		buffers;
	static	int		oldsamplepos;
	int		fullsamples;

	fullsamples = shm->samples / shm->channels;

	// it is possible to miscount buffers if it has wrapped twice between
	// calls to S_Update.  Oh well.
	samplepos = SNDDMA_GetDMAPos ();

	if (samplepos < oldsamplepos)
	{
		buffers++;					// buffer wrapped

		if (paintedtime > 0x40000000)
		{
			// time to chop things off to avoid 32 bit limits
			buffers = 0;
			paintedtime = fullsamples;
			S_StopAllSounds (true);
		}
	}

	oldsamplepos = samplepos;

	soundtime = buffers * fullsamples + samplepos / shm->channels;
}


void S_ExtraUpdate (void)
{
}


void S_Update_ (void)
{
	unsigned        endtime;
	int				samps;

	if (!sound_started || (snd_blocked > 0)) return;

	// Updates DMA time
	GetSoundtime ();

	// check to make sure that we haven't overshot
	if (paintedtime < soundtime)
	{
		//Con_Printf ("S_Update_ : overflow\n");
		paintedtime = soundtime;
	}

	// mix ahead of current position
	endtime = soundtime + _snd_mixahead.value * shm->speed;
	samps = shm->samples >> (shm->channels - 1);

	if (endtime - soundtime > samps)
		endtime = soundtime + samps;

	// if the buffer was lost or stopped, restore it and/or restart it
	DWORD dwStatus;

	if (ds_SecondaryBuffer8)
	{
		if (ds_SecondaryBuffer8->GetStatus (&dwStatus) != DD_OK) Con_Printf ("Couldn't get sound buffer status\n");
		if (dwStatus & DSBSTATUS_BUFFERLOST) ds_SecondaryBuffer8->Restore ();
		if (!(dwStatus & DSBSTATUS_PLAYING)) ds_SecondaryBuffer8->Play (0, 0, DSBPLAY_LOOPING);
	}

	S_PaintChannels (endtime);
}

/*
===============================================================================

console functions

===============================================================================
*/

static void S_PlayGen (int *hash, float attn)
{
	int 	i;
	char	name[256];
	sfx_t	*sfx;

	i = 1;

	while (i < Cmd_Argc ())
	{
		if (!strrchr (Cmd_Argv (i), '.'))
		{
			strcpy (name, Cmd_Argv (i));
			strcat (name, ".wav");
		}
		else
			strcpy (name, Cmd_Argv (i));

		sfx = S_PrecacheSound (name);
		S_StartSound ((*hash)++, 0, sfx, listener_origin, 1.0, attn);
		i++;
	}
}

void S_Play (void)
{
	static int hash = 345;

	S_PlayGen (&hash, 1);
}

void S_Play2 (void)
{
	static int hash = 345;

	S_PlayGen (&hash, 0);
}

void S_PlayVol (void)
{
	static int hash = 543;
	int i;
	float vol;
	char name[256];
	sfx_t	*sfx;

	i = 1;

	while (i < Cmd_Argc())
	{
		if (!strrchr (Cmd_Argv (i), '.'))
		{
			strcpy (name, Cmd_Argv (i));
			strcat (name, ".wav");
		}
		else
			strcpy (name, Cmd_Argv (i));

		sfx = S_PrecacheSound (name);
		vol = atof (Cmd_Argv (i + 1));
		S_StartSound (hash++, 0, sfx, listener_origin, vol, 1.0);
		i += 2;
	}
}

void S_SoundList (void)
{
	int		i;
	sfx_t	*sfx;
	sfxcache_t	*sc;
	int		size, total;

	total = 0;

	for (i = 0; i < known_sfx.size (); i++)
	{
		if (!(sfx = known_sfx[i])) continue;
		if (!(sc = sfx->sndcache)) continue;

		size = sc->length * 2 * (sc->stereo + 1);
		total += size;

		if (sc->loopstart >= 0)
			Con_Printf ("L");
		else Con_Printf (" ");

		Con_Printf ("(%2db) %6i : %s\n", 16, size, sfx->name);
	}

	Con_Printf ("Total resident: %i\n", total);
}


void S_LocalSound (char *sound)
{
	int		i;
	sfx_t	*sfx;

	if (nosound.value) return;
	if (!sound_started) return;

	// look for a cached version
	for (i = 0; i < known_sfx.size (); i++)
	{
		if (!(sfx = known_sfx[i])) continue;
		if (!sfx->sndcache) continue;

		// matching name
		if (!_stricmp (sound, sfx->name))
		{
			// play the sound we got
			S_PlaySoundFromBuffer (cl.viewentity, -1, sfx, vec3_origin, 1, 1);
			return;
		}
	}

	// not cached
	if ((sfx = S_PrecacheSound (sound)) != NULL)
	{
		// play the sound we got
		S_PlaySoundFromBuffer (cl.viewentity, -1, sfx, vec3_origin, 1, 1);
	}
	else Con_Printf ("S_LocalSound: can't cache %s\n", sound);
}


void S_ClearPrecache (void)
{
}


void S_BeginPrecaching (void)
{
}


void S_EndPrecaching (void)
{
}

