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


// pool memory
#define NUM_VIRTUAL_POOLS	10

// pools
#define POOL_PERMANENT		0
#define POOL_GAME			1
#define POOL_CACHE			2
#define POOL_MAP			3
#define POOL_EDICTS			4
#define POOL_TEMP			5
#define POOL_LOADFILE		6

// interface
void *Pool_Alloc (int pool, int size);
void Pool_Init (void);
void Pool_Free (int pool);
void Pool_Reset (int pool, int newsizebytes);
void *Cache_Check (char *name);
void *Cache_Alloc (char *name, void *data, int size);
void *Zone_Alloc (int size);
void Zone_Free (void *ptr);
void Zone_Compact (void);

