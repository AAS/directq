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
// snd_mix.c -- portable code to mix sounds for snd_dma.c

#include "quakedef.h"
#include "winquake.h"

extern LPDIRECTSOUNDBUFFER8 ds_SecondaryBuffer8;
extern DWORD ds_SoundBufferSize;

// use a larger paintbuffer to prevent sound stuttering/etc
#define	PAINTBUF_SIZE	8192

portable_samplepair_t *paintbuffer = NULL;

int		**snd_scaletable = NULL;
int 	*snd_p, snd_linear_count, snd_vol;
short	*snd_out;


// made this inline as it will be called fairly regularly
__inline void Snd_InitPaintBuffer (void)
{
	if (!paintbuffer)
	{
		paintbuffer = (portable_samplepair_t *) Pool_Permanent->Alloc (PAINTBUF_SIZE * sizeof (portable_samplepair_t));
		memset (paintbuffer, 0, PAINTBUF_SIZE * sizeof (portable_samplepair_t));
	}
}


void Snd_WriteLinearBlastStereo16 (void)
{
	for (int l = 0, r = 1; l < snd_linear_count; l += 2, r += 2)
	{
		int val = (snd_p[l] * snd_vol) >> 8;
		snd_out[l] = (val > 32767) ? 32767 : ((val < -32768) ? -32768 : val);

		val = (snd_p[r] * snd_vol) >> 8;
		snd_out[r] = (val > 32767) ? 32767 : ((val < -32768) ? -32768 : val);
	}
}


bool S_GetBufferLock (DWORD dwOffset, DWORD dwBytes, void **pbuf, DWORD *dwSize, void **pbuf2, DWORD *dwSize2, DWORD dwFlags);

void S_TransferStereo16 (int endtime)
{
	if (!ds_SecondaryBuffer8) return;

	// init the paintbuffer if we need to
	Snd_InitPaintBuffer ();

	int		lpos;
	int		lpaintedtime;
	DWORD	*pbuf;
	DWORD	dwSize, dwSize2;
	DWORD	*pbuf2;

	snd_vol = volume.value * 256;

	snd_p = (int *) paintbuffer;
	lpaintedtime = paintedtime;

	// attempt to get a lock on the sound buffer
	if (!S_GetBufferLock (0, ds_SoundBufferSize, (LPVOID *) &pbuf, &dwSize, (LPVOID *) &pbuf2, &dwSize2, 0)) return;

	while (lpaintedtime < endtime)
	{
		// handle recirculating buffer issues
		lpos = lpaintedtime & ((shm->samples >> 1) - 1);

		snd_out = (short *) pbuf + (lpos << 1);
		snd_linear_count = (shm->samples >> 1) - lpos;

		if (lpaintedtime + snd_linear_count > endtime) snd_linear_count = endtime - lpaintedtime;

		snd_linear_count <<= 1;

		// write a linear blast of samples
		Snd_WriteLinearBlastStereo16 ();

		snd_p += snd_linear_count;
		lpaintedtime += (snd_linear_count >> 1);
	}

	ds_SecondaryBuffer8->Unlock (pbuf, dwSize, NULL, 0);
}


void S_TransferPaintBuffer (int endtime)
{
	// init the paintbuffer if we need to
	Snd_InitPaintBuffer ();

	if (shm->samplebits == 16 && shm->channels == 2)
	{
		S_TransferStereo16 (endtime);
		return;
	}

	DWORD	*pbuf;
	DWORD	dwSize, dwSize2;
	DWORD	*pbuf2;

	int *p = (int *) paintbuffer;
	int count = (endtime - paintedtime) * shm->channels;
	int out_mask = shm->samples - 1; 
	int out_idx = paintedtime * shm->channels & (shm->samples - 1);
	int step = 3 - shm->channels;
	int snd_vol = volume.value * 256;

	// attempt to get a lock on the sound buffer
	if (!S_GetBufferLock (0, ds_SoundBufferSize, (LPVOID *) &pbuf, &dwSize, (LPVOID *) &pbuf2, &dwSize2, 0)) return;

	if (shm->samplebits == 16)
	{
		short *out = (short *) pbuf;

		while (count--)
		{
			int val = ((*p) * snd_vol) >> 8;
			p += step;

			if (val > 32767) val = 32767; else if (val < -32768) val = -32768;

			out[out_idx] = val;
			out_idx = (out_idx + 1) & out_mask;
		}
	}
	else if (shm->samplebits == 8)
	{
		unsigned char *out = (unsigned char *) pbuf;

		while (count--)
		{
			int val = ((*p) * snd_vol) >> 8;
			p += step;

			if (val > 32767) val = 32767; else if (val < -32768) val = -32768;

			out[out_idx] = (val >> 8) + 128;
			out_idx = (out_idx + 1) & out_mask;
		}
	}

	ds_SecondaryBuffer8->Unlock (pbuf, dwSize, NULL, 0);
}


/*
===============================================================================

CHANNEL MIXING

===============================================================================
*/

void SND_InitScaletable (void)
{
	// set up the scale table first time
	if (!snd_scaletable)
	{
		// use a higher quality scale table than the Quake default
		// (this is just an array lookup so the only real penalty comes from memory overhead)
		snd_scaletable = (int **) Pool_Permanent->Alloc (256 * sizeof (int *));
		for (int i = 0; i < 256; i++) snd_scaletable[i] = NULL;
	}

	for (int i = 0; i < 256; i++)
	{
		if (!snd_scaletable[i]) snd_scaletable[i] = (int *) Pool_Permanent->Alloc (256 * sizeof (int));
		for (int j = 0; j < 256; j++) snd_scaletable[i][j] = ((signed char) j) * i;
	}
}


void SND_PaintChannelFrom8 (channel_t *ch, sfxcache_t *sc, int count)
{
	// init the paintbuffer if we need to
	Snd_InitPaintBuffer ();

	if (ch->leftvol > 255) ch->leftvol = 255;
	if (ch->rightvol > 255) ch->rightvol = 255;

	int *lscale = snd_scaletable[ch->leftvol];
	int *rscale = snd_scaletable[ch->rightvol];

	unsigned char *sfx = (unsigned char *) sc->data + ch->pos;

	for (int i = 0; i < count; i++)
	{
		int data = sfx[i];

		paintbuffer[i].left += lscale[data];
		paintbuffer[i].right += rscale[data];
	}

	ch->pos += count;
}


void SND_PaintChannelFrom16 (channel_t *ch, sfxcache_t *sc, int count)
{
	// init the paintbuffer if we need to
	Snd_InitPaintBuffer ();

	int data;
	int left, right;
	int leftvol, rightvol;
	signed short *sfx;
	int	i;

	leftvol = ch->leftvol;
	rightvol = ch->rightvol;
	sfx = (signed short *) sc->data + ch->pos;

	for (i = 0; i < count; i++)
	{
		data = sfx[i];
		left = (data * leftvol) >> 8;
		right = (data * rightvol) >> 8;
		paintbuffer[i].left += left;
		paintbuffer[i].right += right;
	}

	ch->pos += count;
}


void S_PaintChannels (int endtime)
{
	// init the paintbuffer if we need to
	Snd_InitPaintBuffer ();

	int 	i;
	int 	end;
	channel_t *ch;
	sfxcache_t	*sc;
	int		ltime, count;

	while (paintedtime < endtime)
	{
		// if paintbuffer is smaller than DMA buffer
		end = endtime;

		if (endtime - paintedtime > PAINTBUF_SIZE) end = paintedtime + PAINTBUF_SIZE;

		// clear the paint buffer
		memset (paintbuffer, 0, (end - paintedtime) * sizeof (portable_samplepair_t));

		// paint in the channels.
		ch = channels;

		for (i = 0; i < total_channels; i++, ch++)
		{
			if (!ch->sfx) continue;
			if (!ch->leftvol && !ch->rightvol) continue;
			if (!(sc = S_LoadSound (ch->sfx))) continue;

			ltime = paintedtime;

			while (ltime < end)
			{
				// paint up to end
				if (ch->end < end)
					count = ch->end - ltime;
				else count = end - ltime;

				if (count > 0)
				{	
					if (sc->width == 1)
						SND_PaintChannelFrom8 (ch, sc, count);
					else SND_PaintChannelFrom16 (ch, sc, count);
	
					ltime += count;
				}

				// if at end of loop, restart
				if (ltime >= ch->end)
				{
					if (sc->loopstart >= 0)
					{
						ch->pos = sc->loopstart;
						ch->end = ltime + sc->length - ch->pos;
					}
					else				
					{	// channel just stopped
						ch->sfx = NULL;
						break;
					}
				}
			}
		}

		// transfer out according to DMA format
		S_TransferPaintBuffer (end);
		paintedtime = end;
	}
}

