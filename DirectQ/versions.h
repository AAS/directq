
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

// common version definitions
// this should be the first thing included in quakedef.h
// if in any source file another file is included before quakedef.h this should also be included first there

#ifndef __VERSIONS_H_INCLUDED__
#define __VERSIONS_H_INCLUDED__

// DirectSound version used - failing to specify this will #ifdef out important stuff in newer SDKs
#define DIRECTSOUND_VERSION 0x0800

// leaving this out just throws a warning but we'll define it to avoid that
#define DIRECTINPUT_VERSION	0x0800

// here we define a windows version to ensure that we'll always compile OK for the minimum supported platform
#define WINVER 0x0500
#define _WIN32_WINNT 0x0500

// this is the current version of DirectQ
#define DIRECTQ_VERSION "1.8.666"

#endif
