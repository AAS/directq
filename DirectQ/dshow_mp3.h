
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


// direct show player interface
#define WM_GRAPHEVENT	WM_USER

void MediaPlayer_Init (void);
void MediaPlayer_Shutdown (void);
bool MediaPlayer_Play (int track, bool looping);
void MediaPlayer_Stop (void);
void MediaPlayer_Pause (void);
void MediaPlayer_Resume (void);
void MediaPlayer_ChangeVolume (void);
void MediaPlayer_Update (void);
