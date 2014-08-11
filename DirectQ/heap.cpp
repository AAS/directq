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

void Sys_PageIn (void *ptr, int size);


/*
====================================================================================================================================

		QUAKE MEMORY ALLOCATION

	Memory allocation is fully dynamic with no upper bounds (aside from the amount of physical memory in your machine).

	Each memory allocation is assigned a "tag", which must be a positive integer (for simplicity, no technical reason).
	Tags 0 to 100 are reserved for system startup allocations (filesystem, palette, colormap, etc).
	Tags 101 to 500 are reserved for server and maps/models/etc.
	Tag 666 is reserved for the sv.edicts array, as this needs special handling for expansion.

	Don't use the standard library functions, as debug versions are different to release versions.

====================================================================================================================================
*/

typedef struct heapblock_s
{
	void *data;
	int tag;
	int lowmark;
	int size;
	struct heapblock_s *next;
} heapblock_t;

// ahhh - hungarian notation, dr Hfuhruhurr would be proud...
HANDLE hHeap = NULL;

// handle for generic memory allocations just to keep them separate from the Q Heap
HANDLE QGlobalHeap = NULL;

heapblock_t *heapblocks = NULL;

#define HEAP_MIN_BLOCK_SIZE		0x100000


void Heap_Report_f (void)
{
	heapblock_t *hb = NULL;
	int highesttag = 0;

	// get the highest tag in use
	for (hb = heapblocks; hb; hb = hb->next)
		if (hb->tag > highesttag)
			highesttag = hb->tag;

	// track total used as well
	int totalram = 0;

	// check allocations for all tags
	for (int tag = 0; tag <= highesttag; tag++)
	{
		// amount of RAM allocated
		int tagram = 0;
		int tagblocks = 0;

		// accumulate
		for (hb = heapblocks; hb; hb = hb->next)
		{
			if (hb->tag == tag && hb->data)
			{
				tagram += hb->size;
				tagblocks++;
			}
		}

		totalram += tagram;

		if (tagram)
		{
			if (tagram < 1024)
				Con_Printf ("Tag %04i uses %0.2f KB RAM ", tag, (float) tagram / 1024.0f);
			else Con_Printf ("Tag %04i uses %0.2f MB RAM ", tag, ((float) tagram / 1024.0f) / 1024.0f);

			Con_Printf ("in %i blocks ", tagblocks);

			// check against the well-known tags
			if (tag == TAG_STARTUP)
				Con_Printf ("(generic startup)");
			else if (tag == TAG_CLIENTSTARTUP)
				Con_Printf ("(client startup)");
			else if (tag == TAG_FILESYSTEM)
				Con_Printf ("(filesystem)");
			else if (tag == TAG_BRUSHMODELS)
				Con_Printf ("(brush models)");
			else if (tag == TAG_ALIASMODELS)
				Con_Printf ("(alias models)");
			else if (tag == TAG_SPRITEMODELS)
				Con_Printf ("(sprite models)");
			else if (tag == TAG_CLIENTSTRUCT)
				Con_Printf ("(client structures)");
			else if (tag == TAG_PARTICLES)
				Con_Printf ("(particles)");
			else if (tag == TAG_SV_EDICTS)
				Con_Printf ("(sv.edicts structure)");
			else if (tag == TAG_SOUND)
				Con_Printf ("(sounds in-game)");
			else if (tag == TAG_SOUNDSTARTUP)
				Con_Printf ("(sound startup)");
			else if (tag == TAG_PROGS)
				Con_Printf ("(Progs/QC)");
			else if (tag == TAG_LOADMODELS)
				Con_Printf ("(Loaded models)");
			else if (tag == TAG_HUNKFILE)
				Con_Printf ("(Hunkfile)");
			else if (tag == TAG_SIZEBUF)
				Con_Printf ("(size buffer)");
			else if (tag > 999)
				Con_Printf ("(temporary allocations)");
			else if (tag > 100)
				Con_Printf ("(per-map allocations)");
			else Con_Printf ("(permanent allocations)");

			Con_Printf ("\n");
		}
	}

	if (totalram) Con_Printf ("Total RAM used: %0.2f MB\n", ((float) totalram / 1024.0f) / 1024.0f);

	// check number of blocks unused
	int unusedblocks = 0;
	int totalblocks = 0;

	for (hb = heapblocks; hb; hb = hb->next)
	{
		if (!hb->data) unusedblocks++;
		totalblocks++;
	}

	Con_Printf ("%i unused heap blocks (%i total)\n", unusedblocks, totalblocks);
}


static void Heap_CheckTempTag (int tag)
{
	if (tag >= TAG_TEMPORARY1)
	{
		// verboten in Heap_TagAlloc
		Sys_Error ("Heap_TagAlloc: tag >= TAG_TEMPORARY1");
		return;
	}
}


static void *Heap_PerformTagAlloc (int tag, int size)
{
	if (tag < 0)
	{
		Sys_Error ("Heap_TagAlloc: tag < 0");
		return NULL;
	}

	if (size <= 0)
	{
		Sys_Error ("Heap_TagAlloc: size <= 0");
		return NULL;
	}

	heapblock_t *hb = NULL;

	// allocate on DWORD boundaries
	size = ((size + 3) & ~3);

	if (tag > 100 && tag < 1000)
	{
		// attempt to stuff multiple allocations into a single block
		for (hb = heapblocks; hb; hb = hb->next)
		{
			// ensure that the block has data
			if (!hb->data) continue;

			// look for a matching tag
			if (hb->tag != tag) continue;

			// look for a block with sufficient free space
			if ((hb->size - hb->lowmark) < size) continue;

			// reuse space in this block
			byte *found = &(((byte *) hb->data)[hb->lowmark]);
			memset (found, 0, size);
			hb->lowmark += size;
			return found;
		}
	}

	// look for an available block
	for (hb = heapblocks; hb; hb = hb->next)
	{
		if (!hb->data)
		{
			hb->lowmark = 0;
			break;
		}
	}

	// didn't get one
	if (!hb)
	{
		// allocate a new block
		hb = (heapblock_t *) HeapAlloc (hHeap, HEAP_ZERO_MEMORY, sizeof (heapblock_t));

		// link it in
		hb->next = heapblocks;
		hb->lowmark = 0;
		heapblocks = hb;
	}

	// fill it in
	hb->lowmark = size;

	// never take < HEAP_MIN_BLOCK_SIZE to help optimize allocation speeds
	if (size < HEAP_MIN_BLOCK_SIZE && tag > 100 && tag < 1000)
		size = HEAP_MIN_BLOCK_SIZE;

	hb->size = size;
	hb->tag = tag;

	// allocate data and clear it to 0
	// (quake expects this and assumes it in many cases)
	hb->data = HeapAlloc (hHeap, HEAP_ZERO_MEMORY, size);

	if (!hb->data)
		Sys_Error ("Heap_TagAlloc: failed to allocate %i bytes", size);

	// return what we got
	return hb->data;
}


void *Heap_TagAlloc (int tag, int size)
{
	Heap_CheckTempTag (tag);
	return Heap_PerformTagAlloc (tag, size);
}


void *Heap_TempAlloc (int size)
{
	// so that the first call will go to 0
	static int heap_temptag = -1;

	// next tag in cycle
	heap_temptag++;

	// cycle 0 to 7
	// heap_temptag &= 7;
	if (heap_temptag > 7) heap_temptag = 0;

	// free anything that might be using this tag
	// (protection against two consecutive calls inadvertently freeing anything that might be currently in use)
	Heap_TagFree (TAG_TEMPORARY1 + heap_temptag);

	// alloc from current cycle
	return Heap_PerformTagAlloc (TAG_TEMPORARY1 + heap_temptag, size);
}


void Heap_Free101Plus (void)
{
	int freedata = 0;

	for (heapblock_t *hb = heapblocks; hb; hb = hb->next)
	{
		if (hb->tag > 100 && hb->data)
		{
			freedata += hb->size;

			HeapFree (hHeap, 0, hb->data);
			hb->data = NULL;
			hb->size = 0;
			hb->lowmark = 0;
		}
	}

	// do something interesting with the freedata counter here...
	Con_DPrintf ("Released %0.3f MB Heap Memory\n", (float) freedata / 1024 / 1024);

	// compress unused blocks
	freedata = HeapCompact (hHeap, 0);

	// free heap is free
	Con_DPrintf ("Largest contiguous free heap: %0.3f MB\n", (float) freedata / 1024 / 1024);

	// also compress the global heap
	HeapCompact (QGlobalHeap, 0);
}


void Heap_TagFree (int tag)
{
	int freedata = 0;

	for (heapblock_t *hb = heapblocks; hb; hb = hb->next)
	{
		if (hb->tag == tag && hb->data)
		{
			freedata += hb->size;

			HeapFree (hHeap, 0, hb->data);
			hb->data = NULL;
			hb->size = 0;
			hb->lowmark = 0;
		}
	}

	// because this can happen at runtime we don't compress or report
}


cmd_t Heap_Report_Cmd ("heap_report", Heap_Report_f);


void Heap_Check (void)
{
	BOOL validheap = HeapValidate (hHeap, 0, NULL);

	if (!validheap) Sys_Error ("Heap_Check: Trashed Heap");
}


void Heap_Init (void)
{
	hHeap = HeapCreate (0, 0, 0);

	if (!hHeap) Sys_Error ("Heap_Init: Failed to create memory heap");

	QGlobalHeap = HeapCreate (0, 0, 0);

	if (!QGlobalHeap) Sys_Error ("Heap_Init: Failed to create global heap");
}


void *Heap_QMalloc (int size)
{
	byte *memptr = (byte *) malloc (size);

	if (!memptr)
	{
		Sys_Error ("Heap_ZMalloc: failed to allocate %i bytes", size);
		return NULL;
	}

	memset (memptr, size, 0);
	return memptr;
}


