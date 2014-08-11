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


// pools
#define POOL_PERMANENT		1
#define POOL_GAME			2
#define POOL_CACHE			4
#define POOL_MAP			8
#define POOL_FILELOAD		16
#define POOL_TEMP			32


// interface
void Pool_Init (void);
void *Cache_Check (char *name);
void Cache_Invalidate (char *name);
void *Cache_Alloc (char *name, void *data, int size);
void *Zone_Alloc (int size);
void Zone_Free (void *ptr);
void Zone_Compact (void);


class CSpaceBuffer
{
public:
	CSpaceBuffer (char *name, int maxsizemb, int usage);
	~CSpaceBuffer (void);
	void *Alloc (int size);
	void Free (void);
	void Rewind (void);

	int GetMaxSize (void) {return this->MaxSize;}
	int GetLowMark (void) {return this->LowMark;}
	int GetHighMark (void) {return this->HighMark;}
	int GetPeakMark (void) {return this->PeakMark;}
	int GetUsage (void) {return this->Usage;}

private:
	void Initialize (void);
	int MaxSize;	// maximum memory reserved by this buffer (converted to bytes in constructor)
	int LowMark;	// current memory pointer position
	int HighMark;	// size of all committed memory so far
	int PeakMark;	// maximum memory ever used in this pool

	char Name[64];
	int Registration;
	int Usage;

	byte *BasePtr;
};


// space buffers
extern CSpaceBuffer *Pool_Game;
extern CSpaceBuffer *Pool_Permanent;
extern CSpaceBuffer *Pool_Map;
extern CSpaceBuffer *Pool_Cache;
extern CSpaceBuffer *Pool_FileLoad;
extern CSpaceBuffer *Pool_Temp;
extern CSpaceBuffer *Pool_PolyVerts;

void FreeSpaceBuffers (int usage);

