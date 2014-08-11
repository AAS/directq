
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

// 1 MB buffer for general short-lived allocations
extern byte *scratchbuf;
#define SCRATCHBUF_SIZE 0x100000

// interface
void Heap_Init (void);

void *Zone_Alloc (int size);
void Zone_FreeMemory (void *ptr);
void Zone_Compact (void);

// wrapper to ensure that the pointer is NULL after a free op
#define Zone_Free(ptr) {Zone_FreeMemory (ptr); (ptr) = NULL;}


class CQuakeHunk
{
public:
	CQuakeHunk (int maxsizemb);
	~CQuakeHunk (void);
	void *Alloc (int size);
	void Free (void);
	float GetSizeMB (void);

	int GetLowMark (void);
	void FreeToLowMark (int mark);

private:
	void Initialize (void);
	int MaxSize;	// maximum memory reserved by this buffer (converted to bytes in constructor)
	int LowMark;	// current memory pointer position
	int HighMark;	// size of all committed memory so far

	char Name[64];

	byte *BasePtr;
};


class CQuakeZone
{
public:
	CQuakeZone (void);
	~CQuakeZone (void);
	void *Alloc (int size);
	void Free (void *data);
	void Compact (void);
	void Discard (void);
	float GetSizeMB (void);

private:
	void EnsureHeap (void);
	HANDLE hHeap;
	int Size;
	int Peak;
};


class CQuakeCache
{
public:
	CQuakeCache (void);
	~CQuakeCache (void);
	void *Alloc (int size);
	void *Alloc (void *data, int size);
	void *Alloc (char *name, void *data, int size);
	void *Check (char *name);
	void Flush (void);
	float GetSizeMB (void);

private:
	void Init (void);
	CQuakeZone *Heap;
	struct cacheobject_s *Head;
};


// space buffers
extern CQuakeHunk *MainHunk;
extern CQuakeZone *GameZone;
extern CQuakeCache *MainCache;
extern CQuakeZone *MainZone;

extern CQuakeZone *ServerZone;
extern CQuakeZone *ClientZone;
extern CQuakeZone *RenderZone;
extern CQuakeZone *ModelZone;

