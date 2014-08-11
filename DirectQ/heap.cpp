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

// directq memory functions.  funny how things come full-circle, isn't it?
#include "quakedef.h"

/*
========================================================================================================================

		UTILITY FUNCTIONS

========================================================================================================================
*/

int MemoryRoundSizeToKB (int size, int roundtok)
{
	// kilobytes
	roundtok *= 1024;

	for (int newsize = 0;; newsize += roundtok)
		if (newsize >= size)
			return newsize;

	// never reached
	return size;
}


/*
========================================================================================================================

		CACHE MEMORY

	Certain objects which are loaded per map can be cached per game as they are reusable.  The cache should
	always be thrown out when the game changes, and may be discardable at any other time.  The cache is just a
	wrapper around the main virtual memory system, so use Pool_Free to discard it.

========================================================================================================================
*/


typedef struct cacheobject_s
{
	struct cacheobject_s *next;
	void *data;
	char *name;
} cacheobject_t;


cacheobject_t *cachehead = NULL;
int numcacheobjects = 0;

void *Cache_Check (char *name)
{
	for (cacheobject_t *cache = cachehead; cache; cache = cache->next)
	{
		// these should never happen
		if (!cache->name) continue;
		if (!cache->data) continue;

		if (!stricmp (cache->name, name))
		{
			Con_DPrintf ("Reusing %s from cache\n", cache->name);
			return cache->data;
		}
	}

	// not found in cache
	return NULL;
}


void Cache_Report_f (void)
{
	for (cacheobject_t *cache = cachehead; cache; cache = cache->next)
	{
		// these should never happen
		if (!cache->name) continue;
		if (!cache->data) continue;

		Con_Printf ("%s\n", cache->name);
	}
}


cmd_t Cache_Report_Cmd ("cache_report", Cache_Report_f);

void *Cache_Alloc (char *name, void *data, int size)
{
	cacheobject_t *cache = (cacheobject_t *) Pool_Alloc (POOL_CACHE, sizeof (cacheobject_t));

	// alloc on the cache
	cache->name = (char *) Pool_Alloc (POOL_CACHE, strlen (name) + 1);
	cache->data = Pool_Alloc (POOL_CACHE, size);

	// copy in the name
	strcpy (cache->name, name);

	// count objects for reporting
	numcacheobjects++;

	// copy to the cache buffer
	if (data) memcpy (cache->data, data, size);

	// link it in
	cache->next = cachehead;
	cachehead = cache;

	// return from the cache
	return cache->data;
}


void Cache_Init (void)
{
	cachehead = NULL;
	numcacheobjects = 0;
}


/*
========================================================================================================================

		VIRTUAL POOL BASED MEMORY SYSTEM

	This is officially the future of DirectQ memory allocation.  Instead of using lots of small itty bitty memory
	chunks we instead use a number of large "pools", each of which is reserved but not yet committed in virtual
	memory.  We can then commit as we go, thus giving us the flexibility of (almost) unrestricted memory, but the
	convenience of the old Hunk system (with everything consecutive in memory).

========================================================================================================================
*/

int mapallocs = 0;

typedef struct vpool_s
{
	// name for display
	char name[24];

	// max memory in this pool in mb (converted to bytes at startup)
	int maxmem;

	// new allocations will start here
	int lowmark;

	// currently allocated and committed memory
	int highmark;

	// initial allocation (sized for peaks of ID1)
	int initialkb;

	// memory is allocated in chunks of this amount of bytes
	int chunksizekb;

	// max size of highmark
	int peaksize;

	// base pointer
	byte *membase;
} vpool_t;

// these should be declared in the same order as the #defines in heap.h
vpool_t virtualpools[NUM_VIRTUAL_POOLS] =
{
	// stuff in this pool is never freed while DirectQ is running
	{"Permanent", 32, 0, 0, 10240, 512, 0, NULL},

	// stuff in these pools persists for the duration of the game
	{"This Game", 32, 0, 0, 512, 256, 0, NULL},
	{"Cache", 256, 0, 0, 10240, 1024, 0, NULL},

	// stuff in these pools persists for the duration of the map
	// warpc only uses ~48 MB
	{"This Map", 128, 0, 0, 10240, 2048, 0, NULL},
	{"Edicts", 128, 0, 0, 512, 256, 0, NULL},

	// used for temp allocs where we don't want to worry about freeing them
	{"Temp Allocs", 128, 0, 0, 3072, 1024, 0, NULL},

	// for COM_LoadTempFile Loading
	{"File Loads", 128, 0, 0, 2048, 2048, 0, NULL},

	// spare slots
	{"Unused", 1, 0, 0, 64, 64, 0, NULL},
	{"Unused", 1, 0, 0, 64, 64, 0, NULL},
	{"Unused", 1, 0, 0, 64, 64, 0, NULL}
};


void *Pool_Alloc (int pool, int size)
{
	if (pool < 0 || pool >= NUM_VIRTUAL_POOLS)
		Sys_Error ("Pool_Alloc: Invalid Pool");

	vpool_t *vp = &virtualpools[pool];

	// if temp file loading overflows we just reset it
	if (pool == POOL_TEMP && (vp->lowmark + size) >= vp->maxmem)
	{
		// if the temp pool is too small to hold this allocation we just reset it
		if (size > vp->maxmem)
			Pool_Reset (pool, size);
		else vp->lowmark = 0;
	}

	// not enough pool space
	if ((vp->lowmark + size) >= vp->maxmem)
		Sys_Error ("Pool_Alloc: Overflow");

	// only pass over the commit region otherwise lots of small allocations will pass over
	// the *entire* *buffer* every time (slooooowwwwww)
	if ((vp->lowmark + size) >= vp->highmark)
	{
		if (pool == POOL_MAP) mapallocs++;

		// alloc in batches
		int newsize = MemoryRoundSizeToKB (vp->lowmark + size, vp->chunksizekb);

		// this will also set the committed memory to 0 for us
		if (!VirtualAlloc (vp->membase + vp->lowmark, newsize - vp->lowmark, MEM_COMMIT, PAGE_READWRITE))
			Sys_Error ("Pool_Alloc: VirtualAlloc Failed");

		vp->highmark = newsize;

		// track peak - only used for reporting
		if (vp->highmark > vp->peaksize) vp->peaksize = vp->highmark;
	}

	// set up
	void *buf = (vp->membase + vp->lowmark);
	vp->lowmark += size;

	return buf;
}


void Pool_Reset (int pool, int newsizebytes)
{
	// graceful failure
	if (pool < 0 || pool >= NUM_VIRTUAL_POOLS) return;

	// easier access
	vpool_t *vp = &virtualpools[pool];

	if (newsizebytes < 1)
	{
		// do a "fast reset", i.e. just switch the lowmark back to 0
		// this is used for stuff like file loading where we want to avoid a full reset/realloc
		vp->lowmark = 0;

		// reset to 0 as much of the loading code will expect this
		memset (vp->membase, 0, vp->highmark);
		return;
	}

	// fully release the memory
	if (vp->membase) VirtualFree (vp->membase, 0, MEM_RELEASE);

	// fill in
	vp->lowmark = 0;
	vp->highmark = 0;
	vp->maxmem = MemoryRoundSizeToKB (newsizebytes, 1024);

	// reserve the memory for use by this pool
	vp->membase = (byte *) VirtualAlloc (NULL, vp->maxmem, MEM_RESERVE, PAGE_NOACCESS);

	// commit an initial chunk
	Pool_Alloc (pool, vp->initialkb * 1024);
	vp->lowmark = 0;
}


void Pool_Free (int pool)
{
	// graceful failure
	if (pool < 0 || pool >= NUM_VIRTUAL_POOLS) return;

	// easier access
	vpool_t *vp = &virtualpools[pool];

	// already free
	if (!vp->highmark) return;
	if (!vp->membase) return;

	// decommit all of the allocated pool aside from the first chunk in it
	VirtualFree (vp->membase + (vp->initialkb * 1024), vp->highmark - (vp->initialkb * 1024), MEM_DECOMMIT);

	// wipe the retained chunk
	memset (vp->membase, 0, (vp->initialkb * 1024));

	// reset lowmark and highmark
	vp->lowmark = 0;
	vp->highmark = vp->initialkb * 1024;

	if (pool == POOL_MAP)
	{
		Con_DPrintf ("%i map allocations\n", mapallocs);
		mapallocs = 0;
	}

	// reinit the cache if that was freed
	if (pool == POOL_CACHE) Cache_Init ();
}


void Pool_Init (void)
{
	static bool vpinit = false;

	// prevent being called twice (owing to maxmem conversion below)
	if (vpinit) Sys_Error ("Pool_Init: called twice");

	for (int i = 0; i < NUM_VIRTUAL_POOLS; i++)
	{
		// convert maxmem to bytes
		virtualpools[i].maxmem *= (1024 * 1024);

		// reserve the memory for use by this pool
		virtualpools[i].membase = (byte *) VirtualAlloc (NULL, virtualpools[i].maxmem, MEM_RESERVE, PAGE_NOACCESS);

		// check
		if (!virtualpools[i].membase) Sys_Error ("Pool_Init: VirtualAlloc failed");

		// commit one initial chunk
		Pool_Alloc (i, virtualpools[i].initialkb * 1024);
		virtualpools[i].lowmark = 0;
	}

	// init the cache
	Cache_Init ();

	// memory is up now
	vpinit = true;
}


/*
========================================================================================================================

		ZONE MEMORY

	The zone is used for small strings and other stuff that's dynamic in nature and would normally be handled by
	malloc and free.  It primarily exists so that we can report on zone usage, but also so that we can avoid using
	malloc and free, as their behaviour is runtime dependent.

	The win32 Heap* functions basically operate identically to the old zone functions except they let use reserve
	virtual memory and also do all of the tracking and other heavy lifting for us.

========================================================================================================================
*/

typedef struct zblock_s
{
	int size;
	void *data;
} zblock_t;

int zonesize = 0;
int zoneblocks = 0;
int zonepeaksize = 0;
int zonepeakblocks = 0;
HANDLE zoneheap = NULL;

void *Zone_Alloc (int size)
{
	// create an initial heap for use with the zone
	// this heap has 128K initially allocated and 32 MB reserved from the virtual address space
	if (!zoneheap) zoneheap = HeapCreate (0, 0x20000, 0x2000000);

	size += sizeof (zblock_t);
	size = (size + 7) & ~7;

	zblock_t *zb = (zblock_t *) HeapAlloc (zoneheap, HEAP_ZERO_MEMORY, size);

	if (!zb)
	{
		Sys_Error ("Zone_Alloc failed on %i bytes", size);
		return NULL;
	}

	// force the zone block to be non-executable
	DWORD dwdummy;
	BOOL ret = VirtualProtect (zb, size, PAGE_READWRITE, &dwdummy);

	zb->size = size;
	zb->data = (void *) (zb + 1);

	zonesize += size;
	zoneblocks++;

	if (zonesize > zonepeaksize) zonepeaksize = zonesize;
	if (zoneblocks > zonepeakblocks) zonepeakblocks = zoneblocks;

	return zb->data;
}


void Zone_Free (void *ptr)
{
	// attempt to free a NULL pointer
	if (!ptr) return;
	if (!zoneheap) return;

	// retrieve zone block pointer
	zblock_t *zptr = ((zblock_t *) ptr) - 1;

	zonesize -= zptr->size;
	zoneblocks--;

	// release this back to the OS
	HeapFree (zoneheap, 0, zptr);

	// compact ever few frees so as to keep the zone from getting too fragmented
	if (!(zoneblocks % 32)) HeapCompact (zoneheap, 0);
}


void Zone_Compact (void)
{
	// create an initial heap for use with the zone
	// this heap has 128K initially allocated and 32 MB reserved from the virtual address space
	if (!zoneheap) zoneheap = HeapCreate (0, 0x20000, 0x2000000);

	// merge contiguous free blocks
	// call this every map load; it may also be called on demand
	HeapCompact (zoneheap, 0);
}


/*
========================================================================================================================

		REPORTING

========================================================================================================================
*/
void Virtual_Report_f (void)
{
	int reservedmem = 0;
	int committedmem = 0;
	int peakmem = 0;
	int addrspace = 0;

	Con_Printf ("\n-----------------------------------------------\n");
	Con_Printf ("Pool           Highmark     Lowmark  Peak Usage\n");
	Con_Printf ("-----------------------------------------------\n");

	for (int i = 0; i < NUM_VIRTUAL_POOLS; i++)
	{
		addrspace += (virtualpools[i].maxmem / 1024) / 1024;

		// don't report on pools which were never used
		if (!virtualpools[i].peaksize) continue;

		Con_Printf
		(
			"%-11s  %7.2f MB  %7.2f MB  %7.2f MB\n",
			virtualpools[i].name,
			((float) virtualpools[i].highmark / 1024.0f) / 1024.0f,
			((float) virtualpools[i].lowmark / 1024.0f) / 1024.0f,
			((float) virtualpools[i].peaksize / 1024.0f) / 1024.0f
		);

		reservedmem += virtualpools[i].highmark;
		committedmem += virtualpools[i].lowmark;
		peakmem += virtualpools[i].peaksize;
	}

	Con_Printf ("-----------------------------------------------\n");

	Con_Printf
	(
		"%-11s  %7.2f MB  %7.2f MB  %7.2f MB\n",
		"Total",
		((float) reservedmem / 1024.0f) / 1024.0f,
		((float) committedmem / 1024.0f) / 1024.0f,
		((float) peakmem / 1024.0f) / 1024.0f
	);

	Con_Printf ("-----------------------------------------------\n");
	Con_DPrintf ("Reserved address space: %i MB\n", addrspace);
	Con_Printf ("%i objects in cache\n", numcacheobjects);
	Con_Printf ("\nZone current size: %i KB in %i blocks\n", (zonesize + 1023) / 1023, zoneblocks);
	Con_DPrintf ("Zone peak size: %i KB in %i blocks\n", (zonepeaksize + 1023) / 1023, zonepeakblocks);
	Con_DPrintf ("%i allocations for POOL_MAP\n", mapallocs);
}


cmd_t heap_report_cmd ("heap_report", Virtual_Report_f);



