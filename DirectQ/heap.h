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



// global heap allocation (let's avoid malloc like the plague as it's behaviour differs between debug and release versions)
#define Heap_QMalloc(size) HeapAlloc (QGlobalHeap, HEAP_ZERO_MEMORY, (size))
#define Heap_QFreeFull(memptr) {HeapFree (QGlobalHeap, 0, (memptr)); HeapCompact (QGlobalHeap, 0);}
#define Heap_QFreeFast(memptr) HeapFree (QGlobalHeap, 0, (memptr))

void *Heap_TagAlloc (int tag, int size);
void Heap_TagFree (int tag);
void Heap_Free101Plus (void);
void *Heap_TempAlloc (int size);
void Heap_Init (void);
void Heap_Check (void);

// some custom defined tags to make it easier to alloc properly
#define TAG_STARTUP			0
#define TAG_CLIENTSTARTUP	1
#define TAG_FILESYSTEM		2
#define TAG_SOUNDSTARTUP	3
#define TAG_SIZEBUF			4
#define TAG_NETWORK			5
#define TAG_CONSOLE			6
#define TAG_HUNKFILE		7
#define TAG_BRUSHMODELS		101
#define TAG_ALIASMODELS		102
#define TAG_SPRITEMODELS	103
#define TAG_CLIENTSTRUCT	104
#define TAG_PARTICLES		105
#define TAG_SOUND			106
#define TAG_PROGS			107
#define TAG_LIGHTSTYLES		108
#define TAG_LOADMODELS		109
#define TAG_SV_EDICTS		666

// define 4 temporary tags (we're not restricted to these, of course
#define TAG_TEMPORARY1		1000
#define TAG_TEMPORARY2		1001
#define TAG_TEMPORARY3		1002
#define TAG_TEMPORARY4		1003

