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

#include "quakedef.h"

byte *scratchbuf = NULL;

int TotalSize = 0;
int TotalPeak = 0;
int TotalReserved = 0;


/*
========================================================================================================================

		LIBRARY REPLACEMENT FUNCTIONS

	Fast versions of memcpy and memset that operate on WORD or DWORD boundarys (MS CRT only operates on BYTEs).
	The Q3A replacements corrupt the command buffer so they're not viable for use here.

	memcmp is only used twice in DQ so it remains CRT.

========================================================================================================================
*/

void *Q_MemCpy (void *dst, void *src, int size)
{
	void *ret = dst;

	if (size >= 4)
	{
		int *d = (int *) dst;
		int *s = (int *) src;

		for (int i = 0, sz = size >> 2; i < sz; i++)
			*d++ = *s++;

		dst = d;
		src = s;
		size -= (size >> 2) << 2;
	}

	if (size >= 2)
	{
		short *d = (short *) dst;
		short *s = (short *) src;

		for (int i = 0, sz = size >> 1; i < sz; i++)
			*d++ = *s++;

		dst = d;
		src = s;
		size -= (size >> 1) << 1;
	}

	if (size)
	{
		byte *d = (byte *) dst;
		byte *s = (byte *) src;

		for (int i = 0; i < size; i++)
			*d++ = *s++;
	}

	return ret;
}


void *Q_MemSet (void *dst, int val, int size)
{
	void *ret = dst;

	union
	{
		int _int;
		short _short;
		byte _byte[4];
	} fill4;

	fill4._byte[0] = fill4._byte[1] = fill4._byte[2] = fill4._byte[3] = val;

	if (size >= 4)
	{
		int *d = (int *) dst;

		for (int i = 0, sz = size >> 2; i < sz; i++)
			*d++ = fill4._int;

		dst = d;
		size -= (size >> 2) << 2;
	}

	if (size >= 2)
	{
		short *d = (short *) dst;

		for (int i = 0, sz = size >> 1; i < sz; i++)
			*d++ = fill4._short;

		dst = d;
		size -= (size >> 1) << 1;
	}

	if (size)
	{
		byte *d = (byte *) dst;

		for (int i = 0; i < size; i++)
			*d++ = fill4._byte[0];
	}

	return ret;
}

/*
========================================================================================================================

		ZONE MEMORY

========================================================================================================================
*/

#define HEAP_MAGIC 0x35012560


CQuakeZone::CQuakeZone (void)
{
	// prevent this->EnsureHeap from exploding
	this->hHeap = NULL;

	// create it
	this->EnsureHeap ();
}


void *CQuakeZone::Alloc (int size)
{
	this->EnsureHeap ();
	assert (size > 0);

	int *buf = (int *) HeapAlloc (this->hHeap, 0, size + sizeof (int) * 2);

	assert (buf);
	Q_MemSet (buf, 0, size + sizeof (int) * 2);

	// mark as no-execute; not critical so fail it silently
	DWORD dwdummy;
	BOOL ret = VirtualProtect (buf, size, PAGE_READWRITE, &dwdummy);

	buf[0] = HEAP_MAGIC;
	buf[1] = size;

	this->Size += size;
	if (this->Size > this->Peak) this->Peak = this->Size;

	TotalSize += size;
	if (TotalSize > TotalPeak) TotalPeak = TotalSize;

	return (buf + 2);
}


void CQuakeZone::Free (void *data)
{
	if (!this->hHeap) return;
	if (!data) return;

	int *buf = (int *) data;
	buf -= 2;

	assert (buf[0] == HEAP_MAGIC);

	this->Size -= buf[1];
	TotalSize -= buf[1];

	BOOL blah = HeapFree (this->hHeap, 0, buf);
	assert (blah);
}


void CQuakeZone::Compact (void)
{
	HeapCompact (this->hHeap, 0);
}


void CQuakeZone::EnsureHeap (void)
{
	if (!this->hHeap)
	{
		this->hHeap = HeapCreate (0, 0x10000, 0);
		assert (this->hHeap);

		this->Size = 0;
		this->Peak = 0;
	}
}


void CQuakeZone::Discard (void)
{
	if (this->hHeap)
	{
		TotalSize -= this->Size;
		HeapDestroy (this->hHeap);
		this->hHeap = NULL;
	}
}


CQuakeZone::~CQuakeZone (void)
{
	this->Discard ();
}


/*
========================================================================================================================

		CACHE MEMORY

	Certain objects which are loaded per map can be cached per game as they are reusable.  The cache should
	always be thrown out when the game changes, and may be discardable at any other time.  The cache is just a
	wrapper around the Zone API.

	The cache only grows, never shrinks, so it does not fragment.

========================================================================================================================
*/

typedef struct cacheobject_s
{
	struct cacheobject_s *next;
	void *data;
	char *name;
} cacheobject_t;


CQuakeCache::CQuakeCache (void)
{
	// so that the check in Init is valid
	this->Heap = NULL;

	this->Init ();
}


CQuakeCache::~CQuakeCache (void)
{
	SAFE_DELETE (this->Heap);
}


void CQuakeCache::Init (void)
{
	if (!this->Heap)
		this->Heap = new CQuakeZone ();

	this->Head = NULL;
}


void *CQuakeCache::Alloc (int size)
{
	return this->Heap->Alloc (size);
}


void *CQuakeCache::Alloc (void *data, int size)
{
	void *buf = this->Heap->Alloc (size);
	Q_MemCpy (buf, data, size);
	return buf;
}


void *CQuakeCache::Alloc (char *name, void *data, int size)
{
	cacheobject_t *cache = (cacheobject_t *) this->Heap->Alloc (sizeof (cacheobject_t));

	// alloc on the cache
	cache->name = (char *) this->Heap->Alloc (strlen (name) + 1);
	cache->data = this->Heap->Alloc (size);

	// copy in the name
	strcpy (cache->name, name);

	// copy to the cache buffer
	if (data) Q_MemCpy (cache->data, data, size);

	// link it in
	cache->next = this->Head;
	this->Head = cache;

	// return from the cache
	return cache->data;
}


void *CQuakeCache::Check (char *name)
{
	for (cacheobject_t *cache = this->Head; cache; cache = cache->next)
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


void CQuakeCache::Flush (void)
{
	// reinitialize the cache
	SAFE_DELETE (this->Heap);
	this->Init ();
}


/*
========================================================================================================================

		ZONE MEMORY

	The Zone is now just a wrapper around the new CQuakeZone class and only exists outside the class so that it
	may be called from cvar constructors.

========================================================================================================================
*/

void *Zone_Alloc (int size)
{
	if (!MainZone) MainZone = new CQuakeZone ();
	return MainZone->Alloc (size);
}


void Zone_FreeMemory (void *ptr)
{
	// release this back to the OS
	if (MainZone) MainZone->Free (ptr);
}


void Zone_Compact (void)
{
	if (MainZone) MainZone->Compact ();
}


/*
========================================================================================================================

		HUNK MEMORY

========================================================================================================================
*/

CQuakeHunk::CQuakeHunk (int maxsizemb)
{
	// sizes in KB
	this->MaxSize = maxsizemb * 1024 * 1024;
	this->LowMark = 0;
	this->HighMark = 0;

	TotalReserved += this->MaxSize;

	// reserve the full block but do not commit it yet
	this->BasePtr = (byte *) VirtualAlloc (NULL, this->MaxSize, MEM_RESERVE, PAGE_NOACCESS);

	if (!this->BasePtr)
		Sys_Error ("CQuakeHunk::CQuakeHunk - VirtualAlloc failed on memory pool");

	// commit an initial block
	this->Initialize ();
}


CQuakeHunk::~CQuakeHunk (void)
{
	VirtualFree (this->BasePtr, this->MaxSize, MEM_DECOMMIT);
	VirtualFree (this->BasePtr, 0, MEM_RELEASE);
	TotalSize -= this->LowMark;
	TotalReserved -= this->MaxSize;
}


int CQuakeHunk::GetLowMark (void)
{
	return this->LowMark;
}

void CQuakeHunk::FreeToLowMark (int mark)
{
	TotalSize -= (this->LowMark - mark);
	this->LowMark = mark;
}


void *CQuakeHunk::Alloc (int size)
{
	if (this->LowMark + size >= this->MaxSize)
	{
		Sys_Error ("CQuakeHunk::Alloc - overflow on \"%s\" memory pool", this->Name);
		return NULL;
	}

	// size might be > the extra alloc size
	if ((this->LowMark + size) > this->HighMark)
	{
		// round to 1MB boundaries
		this->HighMark = (this->LowMark + size + 0xfffff) & ~0xfffff;

		// this will walk over a previously committed region.  i might fix it...
		if (!VirtualAlloc (this->BasePtr + this->LowMark, this->HighMark - this->LowMark, MEM_COMMIT, PAGE_READWRITE))
		{
			Sys_Error ("CQuakeHunk::Alloc - VirtualAlloc failed for \"%s\" memory pool", this->Name);
			return NULL;
		}
	}

	// fix up pointers and return what we got
	byte *buf = this->BasePtr + this->LowMark;
	this->LowMark += size;

	// ensure set to 0 memory (bug city otherwise)
	Q_MemSet (buf, 0, size);

	TotalSize += size;
	if (TotalSize > TotalPeak) TotalPeak = TotalSize;

	return buf;
}

void CQuakeHunk::Free (void)
{
	// decommit all memory
	VirtualFree (this->BasePtr, this->MaxSize, MEM_DECOMMIT);
	TotalSize -= this->LowMark;

	// recommit the initial block
	this->Initialize ();
}


void CQuakeHunk::Initialize (void)
{
	// commit an initial page of 64k
	VirtualAlloc (this->BasePtr, 0x10000, MEM_COMMIT, PAGE_READWRITE);

	this->LowMark = 0;
	this->HighMark = 0x10000;
}


/*
========================================================================================================================

		INITIALIZATION

========================================================================================================================
*/

CQuakeHunk *MainHunk = NULL;
CQuakeZone *MapZone = NULL;
CQuakeCache *MainCache = NULL;
CQuakeZone *MainZone = NULL;


void Pool_Init (void)
{
	// init the pools we want to keep around all the time
	if (!MainHunk) MainHunk = new CQuakeHunk (128);
	if (!MainCache) MainCache = new CQuakeCache ();
	if (!MainZone) MainZone = new CQuakeZone ();
	if (!MapZone) MapZone = new CQuakeZone ();

	// take a chunk of memory for use by temporary loading functions and other doo-dahs
	scratchbuf = (byte *) Zone_Alloc (SCRATCHBUF_SIZE);
}


/*
========================================================================================================================

		REPORTING

========================================================================================================================
*/

void Virtual_Report_f (void)
{
	Con_Printf ("Memory Usage:\n");
	Con_Printf (" Allocated %6.2f MB\n", ((((float) TotalSize) / 1024.0f) / 1024.0f));
	Con_Printf (" Reserved  %6.2f MB\n", ((((float) TotalReserved) / 1024.0f) / 1024.0f));
	Con_Printf ("\n");
	//int pmsize = MainHunk->GetLowMark ();
	//Con_Printf ("MainHunk occupies %6.2f MB\n", ((((float) pmsize) / 1024.0f) / 1024.0f));
}


cmd_t Heap_Report_Cmd ("heap_report", Virtual_Report_f);

