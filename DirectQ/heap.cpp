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

void Cache_Invalidate (char *name)
{
	extern bool signal_cacheclear;

	for (cacheobject_t *cache = cachehead; cache; cache = cache->next)
	{
		// these should never happen
		if (!cache->name) continue;
		if (!cache->data) continue;

		if (!stricmp (cache->name, name))
		{
			// invalidate the cached object name
			cache->name[0] = 0;

			// clear the cache on the next map change to prevent it leaking
			signal_cacheclear = true;
			return;
		}
	}
}


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
	cacheobject_t *cache = (cacheobject_t *) Pool_Cache->Alloc (sizeof (cacheobject_t));

	// alloc on the cache
	cache->name = (char *) Pool_Cache->Alloc (strlen (name) + 1);
	cache->data = Pool_Cache->Alloc (size);

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

CSpaceBuffer *Pool_Game = NULL;
CSpaceBuffer *Pool_Permanent = NULL;
CSpaceBuffer *Pool_Map = NULL;
CSpaceBuffer *Pool_Cache = NULL;
CSpaceBuffer *Pool_FileLoad = NULL;
CSpaceBuffer *Pool_Temp = NULL;
CSpaceBuffer *Pool_PolyVerts = NULL;

void Pool_Init (void)
{
	static bool vpinit = false;

	// prevent being called twice (owing to maxmem conversion below)
	if (vpinit) Sys_Error ("Pool_Init: called twice");

	// init the pools we want to keep around all the time
	Pool_Permanent = new CSpaceBuffer ("Permanent", 32, POOL_PERMANENT);
	Pool_Game = new CSpaceBuffer ("This Game", 32, POOL_GAME);
	Pool_Map = new CSpaceBuffer ("This Map", 128, POOL_MAP);
	Pool_Cache = new CSpaceBuffer ("Cache", 256, POOL_CACHE);
	Pool_FileLoad = new CSpaceBuffer ("File Loads", 128, POOL_FILELOAD);
	Pool_Temp = new CSpaceBuffer ("Temp Allocs", 128, POOL_TEMP);
	Pool_PolyVerts = new CSpaceBuffer ("Poly Verts", 16, POOL_MAP);

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
	if (!zoneheap) zoneheap = HeapCreate (0, 0x20000, 0);

	size += sizeof (zblock_t);
	size = (size + 7) & ~7;

	zblock_t *zb = (zblock_t *) HeapAlloc (zoneheap, HEAP_ZERO_MEMORY, size);

	if (!zb)
	{
		Sys_Error ("Zone_Alloc failed on %i bytes", size);
		return NULL;
	}

	// force the zone block to be non-executable
	// (we'll allow this to fail as it's not operationally critical)
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
	if (!HeapFree (zoneheap, 0, zptr))
	{
		Sys_Error ("Zone_Free failed on %i bytes", zptr->size);
		return;
	}

	// compact ever few frees so as to keep the zone from getting too fragmented
	if (!(zoneblocks % 32)) HeapCompact (zoneheap, 0);
}


void Zone_Compact (void)
{
	// create an initial heap for use with the zone
	// this heap has 128K initially allocated and 32 MB reserved from the virtual address space
	if (!zoneheap) zoneheap = HeapCreate (0, 0x20000, 0x2000000);

	if (!zoneheap)
	{
		Sys_Error ("HeapCreate failed");
		return;
	}

	// merge contiguous free blocks
	// call this every map load; it may also be called on demand
	// (we'll allow this to fail as it's not operationally critical)
	HeapCompact (zoneheap, 0);
}


/*
========================================================================================================================

		DYNAMIC SPACE BUFFERS

	A new space buffer can be created on the fly and as required everytime memory space is needed for anything.
	DirectQ also maintains a number of it's own internal space buffers for use in the game.

========================================================================================================================
*/
#define MAX_REGISTERED_BUFFERS	4096

CSpaceBuffer *RegisteredBuffers[MAX_REGISTERED_BUFFERS] = {NULL};


int RegisterBuffer (CSpaceBuffer *buffer)
{
	for (int i = 0; i < MAX_REGISTERED_BUFFERS; i++)
	{
		if (!RegisteredBuffers[i])
		{
			RegisteredBuffers[i] = buffer;
			return i;
		}
	}

	Sys_Error ("Too many buffers!");
	return 0;
}


void UnRegisterBuffer (int buffernum)
{
	RegisteredBuffers[buffernum] = NULL;
}


void FreeSpaceBuffers (int usage)
{
	for (int i = 0; i < MAX_REGISTERED_BUFFERS; i++)
	{
		if (!RegisteredBuffers[i]) continue;
		if (!(usage & RegisteredBuffers[i]->GetUsage ())) continue;

		RegisteredBuffers[i]->Free ();
	}
}


CSpaceBuffer::CSpaceBuffer (char *name, int maxsizemb, int usage)
{
	// sizes in KB
	this->MaxSize = maxsizemb * 1024 * 1024;
	this->LowMark = 0;
	this->PeakMark = 0;
	this->HighMark = 0;

	// reserve the full block but do not commit it yet
	this->BasePtr = (byte *) VirtualAlloc (NULL, this->MaxSize, MEM_RESERVE, PAGE_NOACCESS);

	if (!this->BasePtr)
	{
		Sys_Error ("CSpaceBuffer::CSpaceBuffer - VirtualAlloc failed on \"%s\" memory pool", name);
		return;
	}

	// commit an initial block
	this->Initialize ();

	// register the buffer
	Q_strncpy (this->Name, name, 64);

	this->Usage = usage;
	this->Registration = RegisterBuffer (this);
}


CSpaceBuffer::~CSpaceBuffer (void)
{
	UnRegisterBuffer (this->Registration);

	VirtualFree (this->BasePtr, this->MaxSize, MEM_DECOMMIT);
	VirtualFree (this->BasePtr, 0, MEM_RELEASE);
}


void *CSpaceBuffer::Alloc (int size)
{
	if (this->LowMark + size >= this->MaxSize)
	{
		Sys_Error ("CSpaceBuffer::Alloc - overflow on \"%s\" memory pool", this->Name);
		return NULL;
	}

	// size might be > the extra alloc size
	if ((this->LowMark + size) > this->HighMark)
	{
		// round to 64K boundaries
		this->HighMark = (this->LowMark + size + 0xffff) & ~0xffff;

		// this will walk over a previously committed region.  i might fix it...
		if (!VirtualAlloc (this->BasePtr + this->LowMark, this->HighMark - this->LowMark, MEM_COMMIT, PAGE_READWRITE))
		{
			Sys_Error ("CSpaceBuffer::Alloc - VirtualAlloc failed for \"%s\" memory pool", this->Name);
			return NULL;
		}
	}

	// fix up pointers and return what we got
	byte *buf = this->BasePtr + this->LowMark;
	this->LowMark += size;

	// peakmark is for reporting only
	if (this->LowMark > this->PeakMark) this->PeakMark = this->LowMark;

	// ensure set to 0 memory (bug city otherwise)
	memset (buf, 0, size);

	return buf;
}

void CSpaceBuffer::Free (void)
{
	// decommit all memory
	VirtualFree (this->BasePtr, this->MaxSize, MEM_DECOMMIT);

	// recommit the initial block
	this->Initialize ();

	// reinit the cache if that was freed
	if (this->Usage & POOL_CACHE) Cache_Init ();
}


void CSpaceBuffer::Rewind (void)
{
	// just resets the lowmark
	this->LowMark = 0;
}


void CSpaceBuffer::Initialize (void)
{
	// commit an initial page of 64k
	VirtualAlloc (this->BasePtr, 0x10000, MEM_COMMIT, PAGE_READWRITE);

	this->LowMark = 0;
	this->HighMark = 0x10000;
}


/*
========================================================================================================================

		REPORTING

========================================================================================================================
*/

void Pool_Report (int usage, char *desc)
{
	int reservedmem = 0;
	int committedmem = 0;
	int peakmem = 0;
	int addrspace = 0;

	for (int i = 0; i < MAX_REGISTERED_BUFFERS; i++)
	{
		if (!RegisteredBuffers[i]) continue;
		if (RegisteredBuffers[i]->GetUsage () != usage) continue;

		addrspace += RegisteredBuffers[i]->GetMaxSize () / 1024;
		reservedmem += RegisteredBuffers[i]->GetHighMark ();
		committedmem += RegisteredBuffers[i]->GetLowMark ();
		peakmem += RegisteredBuffers[i]->GetPeakMark ();
	}

	Con_Printf
	(
		"%-13s  %7.2f MB  %7.2f MB  %7.2f MB\n",
		desc,
		((float) reservedmem / 1024.0f) / 1024.0f,
		((float) committedmem / 1024.0f) / 1024.0f,
		((float) peakmem / 1024.0f) / 1024.0f
	);
}


void Virtual_Report_f (void)
{
	Con_Printf ("\n-------------------------------------------------\n");
	Con_Printf ("Pool            Committed      In-Use  Peak Usage\n");
	Con_Printf ("-------------------------------------------------\n");

	Pool_Report (POOL_PERMANENT, "Permanent");
	Pool_Report (POOL_GAME, "This Game");
	Pool_Report (POOL_CACHE, "Cache");
	Pool_Report (POOL_MAP, "This Map");
	Pool_Report (POOL_FILELOAD, "File Loads");
	Pool_Report (POOL_TEMP, "Temp Buffer");

	Con_Printf ("-------------------------------------------------\n");

	int reservedmem = 0;
	int committedmem = 0;
	int peakmem = 0;
	int addrspace = 0;

	for (int i = 0; i < MAX_REGISTERED_BUFFERS; i++)
	{
		if (!RegisteredBuffers[i]) continue;

		addrspace += (RegisteredBuffers[i]->GetMaxSize () + 512) / 1024;
		reservedmem += RegisteredBuffers[i]->GetHighMark ();
		committedmem += RegisteredBuffers[i]->GetLowMark ();
		peakmem += RegisteredBuffers[i]->GetPeakMark ();
	}

	Con_Printf
	(
		"%-13s  %7.2f MB  %7.2f MB  %7.2f MB\n",
		"Total",
		((float) reservedmem / 1024.0f) / 1024.0f,
		((float) committedmem / 1024.0f) / 1024.0f,
		((float) peakmem / 1024.0f) / 1024.0f
	);

	Con_Printf ("-------------------------------------------------\n");
	Con_Printf ("%i MB Reserved address space\n", (addrspace + 512) / 1024);
	Con_Printf ("%i objects in cache\n", numcacheobjects);
	Con_Printf ("-------------------------------------------------\n");
	Con_Printf ("Zone current size: %i KB in %i blocks\n", (zonesize + 1023) / 1023, zoneblocks);
	Con_Printf ("Zone peak size: %i KB in %i blocks\n", (zonepeaksize + 1023) / 1023, zonepeakblocks);
}


cmd_t Heap_Report_Cmd ("heap_report", Virtual_Report_f);