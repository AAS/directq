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

int 	*snd_p, snd_linear_count;
short	*snd_out;


// made this inline as it will be called fairly regularly
__inline void Snd_InitPaintBuffer (void)
{
	if (!paintbuffer)
	{
		paintbuffer = (portable_samplepair_t *) Zone_Alloc (PAINTBUF_SIZE * sizeof (portable_samplepair_t));
		memset (paintbuffer, 0, PAINTBUF_SIZE * sizeof (portable_samplepair_t));
	}
}


void S_TransferPaintBuffer (int endtime)
{
	// init the paintbuffer if we need to
	Snd_InitPaintBuffer ();

	// only support 16-bit stereo sound
	int		lpos;
	int		lpaintedtime;
	DWORD	*pbuf;
	DWORD	dwSize, dwSize2;
	DWORD	*pbuf2;

	snd_p = (int *) paintbuffer;
	lpaintedtime = paintedtime;

	int snd_vol = volume.value * 256;
	int val[2];

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
		for (int l = 0; l < snd_linear_count; l += 2, snd_p += 2, snd_out += 2)
		{
			val[0] = (snd_p[0] * snd_vol) >> 8;
			val[1] = (snd_p[1] * snd_vol) >> 8;

			snd_out[0] = (val[0] > 32767) ? 32767 : ((val[0] < -32768) ? -32768 : val[0]);
			snd_out[1] = (val[1] > 32767) ? 32767 : ((val[1] < -32768) ? -32768 : val[1]);
		}

		lpaintedtime += (snd_linear_count >> 1);
	}

	ds_SecondaryBuffer8->Unlock (pbuf, dwSize, NULL, 0);
}


/*
===============================================================================

CHANNEL MIXING

===============================================================================
*/

void SND_PaintChannelFrom16 (channel_t *ch, sfxcache_t *sc, int count)
{
	// init the paintbuffer if we need to
	Snd_InitPaintBuffer ();

	int leftvol = ch->leftvol;
	int rightvol = ch->rightvol;

	signed short *sfx = (signed short *) sc->data + ch->pos;

	for (int i = 0; i < count; i++)
	{
		paintbuffer[i].left += (sfx[i] * leftvol) >> 8;
		paintbuffer[i].right += (sfx[i] * rightvol) >> 8;
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
					SND_PaintChannelFrom16 (ch, sc, count);
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
					{
						// channel just stopped
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

