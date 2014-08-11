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

#include "quakedef.h"
#include "unzip.h"
#include "modelgen.h"
#include <shlwapi.h>
#pragma comment (lib, "shlwapi.lib")

int COM_ListSortFunc (const void *a, const void *b);

void Host_WriteConfiguration (void);
void COM_UnloadAllStuff (void);
void COM_LoadAllStuff (void);
void COM_LoadGame (char *gamename);

/*
============
COM_SkipPath
============
*/
char *COM_SkipPath (char *pathname)
{
	char    *last;

	last = pathname;

	while (*pathname)
	{
		if (*pathname == '/')
			last = pathname + 1;

		pathname++;
	}

	return last;
}

/*
============
COM_StripExtension
============
*/
void COM_StripExtension (char *in, char *out)
{
	while (*in && *in != '.')
		*out++ = *in++;

	*out = 0;
}

/*
============
COM_FileExtension
============
*/
char *COM_FileExtension (char *in)
{
	static char exten[8];
	int             i;

	while (*in && *in != '.')
		in++;

	if (!*in)
		return "";

	in++;

	for (i = 0; i < 7 && *in; i++, in++)
		exten[i] = *in;

	exten[i] = 0;
	return exten;
}

/*
============
COM_FileBase
============
*/
void COM_FileBase (char *in, char *out)
{
	char *s, *s2;

	s = in + strlen (in) - 1;

	while (s != in && *s != '.')
		s--;

	for (s2 = s; s2 != in && *s2 && *s2 != '/'; s2--);

	if (s - s2 < 2)
		strcpy (out, "?model?");
	else
	{
		s--;
		Q_strncpy (out, s2 + 1, s - s2);
		out[s-s2] = 0;
	}
}


/*
==================
COM_DefaultExtension
==================
*/
void COM_DefaultExtension (char *path, char *extension)
{
	char    *src;
	
	// if path doesn't have a .EXT, append extension
	// (extension should include the .)
	src = path + strlen (path) - 1;

	while (*src != '/' && src != path)
	{
		if (*src == '.')
			return;                 // it has an extension

		src--;
	}

	strcat (path, extension);
}


/*
=============================================================================

QUAKE FILESYSTEM

=============================================================================
*/

int     com_filesize;

char    com_gamedir[MAX_PATH];
char	com_gamename[MAX_PATH];

searchpath_t    *com_searchpaths = NULL;

/*
============
COM_Path_f

============
*/
void COM_Path_f (void)
{
	searchpath_t    *s;

	Con_Printf ("Current search path:\n");

	for (s = com_searchpaths; s; s = s->next)
	{
		if (s->pack)
			Con_Printf ("%s (%i files)\n", s->pack->filename, s->pack->numfiles);
		else Con_Printf ("%s\n", s->filename);
	}
}


/*
============
COM_CreatePath

Only used for CopyFile
============
*/
void COM_CreatePath (char *path)
{
	for (char *ofs = path + 1; *ofs; ofs++)
	{
		if (*ofs == '/')
		{
			// create the directory
			*ofs = 0;
			Sys_mkdir (path);
			*ofs = '/';
		}
	}
}


bool SortCompare (char *left, char *right)
{
	if (stricmp (left, right) < 0)
		return true;
	else return false;
}


bool CheckExists (char **fl, char *mapname)
{
	for (int i = 0;; i++)
	{
		// end of list
		if (!fl[i]) return false;

		if (!stricmp (fl[i], mapname)) return true;
	}

	// never reached
	return false;
}


int COM_BuildContentList (char ***FileList, char *basedir, char *filetype, int flags)
{
	char **fl = FileList[0];
	int len = 0;

	if (!fl)
	{
		// we never know how much we need, so alloc enough for 256k items
		// at this stage they're only pointers so we can afford to do this.  if it becomes a problem
		// we might make a linked list then copy from that into an array and do it all in the Zone.
		FileList[0] = (char **) scratchbuf;

		// need to reset the pointer as it will have changed (fl is no longer NULL)
		fl = FileList[0];
		fl[0] = NULL;
	}
	else
	{
		// appending to a list so find the current length and build from there
		for (int i = 0;; i++)
		{
			if (!fl[i]) break;

			len++;
		}
	}

	int dirlen = strlen (basedir);
	int typelen = strlen (filetype);

	for (searchpath_t *search = com_searchpaths; search; search = search->next)
	{
		// prevent overflow
		if ((len + 1) == 0x40000) break;

		if (search->pack && !(flags & NO_PAK_CONTENT))
		{
			pack_t *pak = search->pack;

			for (int i = 0; i < pak->numfiles; i++)
			{
				int filelen = strlen (pak->files[i].name);
				if (filelen < typelen + dirlen) continue;
				if (strnicmp (pak->files[i].name, basedir, dirlen)) continue;
				if (stricmp (&pak->files[i].name[filelen - typelen], filetype)) continue;
				if (CheckExists (fl, &pak->files[i].name[dirlen])) continue;

				fl[len] = (char *) Zone_Alloc (strlen (&pak->files[i].name[dirlen]) + 1);
				strcpy (fl[len++], &pak->files[i].name[dirlen]);
				fl[len] = NULL;
			}
		}
		else if (search->pk3 && !(flags & NO_PAK_CONTENT))
		{
			pk3_t *pak = search->pk3;

			for (int i = 0; i < pak->numfiles; i++)
			{
				int filelen = strlen (pak->files[i].name);

				if (filelen < typelen + dirlen) continue;
				if (strnicmp (pak->files[i].name, basedir, dirlen)) continue;
				if (stricmp (&pak->files[i].name[filelen - typelen], filetype)) continue;
				if (CheckExists (fl, &pak->files[i].name[dirlen])) continue;

				fl[len] = (char *) Zone_Alloc (strlen (&pak->files[i].name[dirlen]) + 1);
				strcpy (fl[len++], &pak->files[i].name[dirlen]);
				fl[len] = NULL;
			}
		}
		else if (!(flags & NO_FS_CONTENT))
		{
			WIN32_FIND_DATA FindFileData;
			HANDLE hFind = INVALID_HANDLE_VALUE;
			char find_filter[MAX_PATH];

			_snprintf (find_filter, 260, "%s/%s*%s", search->filename, basedir, filetype);

			for (int i = 0;; i++)
			{
				if (find_filter[i] == 0) break;
				if (find_filter[i] == '/') find_filter[i] = '\\';
			}

			hFind = FindFirstFile (find_filter, &FindFileData);

			if (hFind == INVALID_HANDLE_VALUE)
			{
				// found no files
				FindClose (hFind);
				continue;
			}

			do
			{
				// not interested
				if (FindFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
				if (FindFileData.dwFileAttributes & FILE_ATTRIBUTE_COMPRESSED) continue;
				if (FindFileData.dwFileAttributes & FILE_ATTRIBUTE_ENCRYPTED) continue;
				if (FindFileData.dwFileAttributes & FILE_ATTRIBUTE_OFFLINE) continue;
				if (FindFileData.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) continue;
				if (FindFileData.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) continue;
				if (CheckExists (fl, FindFileData.cFileName)) continue;

				if (flags & PREPEND_PATH)
				{
					int itemlen = strlen (FindFileData.cFileName) + strlen (search->filename) + strlen (basedir) + 3;
					fl[len] = (char *) Zone_Alloc (itemlen);
					sprintf (fl[len++], "%s\\%s%s", search->filename, basedir, FindFileData.cFileName);
				}
				else
				{
					fl[len] = (char *) Zone_Alloc (strlen (FindFileData.cFileName) + 1);
					strcpy (fl[len++], FindFileData.cFileName);
				}

				fl[len] = NULL;
			} while (FindNextFile (hFind, &FindFileData));

			// done
			FindClose (hFind);
		}
	}

	// sort the list unless there is no list or we've specified not to sort it
	if (len && !(flags & NO_SORT_RESULT)) qsort (fl, len, sizeof (char *), COM_ListSortFunc);

	// return how many we got
	return len;
}


HANDLE COM_MakeTempFile (char *tmpfile)
{
	char fpath1[MAX_PATH];
	char fpath2[MAX_PATH];

	// get the path to the user's temp folder; normally %USERPROFILE%\Local Settings\temp
	if (!GetTempPath (MAX_PATH, fpath1))
	{
		// oh crap
		return INVALID_HANDLE_VALUE;
	}

	// ensure it exists
	CreateDirectory (fpath1, NULL);

	// build the second part of the path
	_snprintf (fpath2, MAX_PATH, "\\DirectQ\\%s", tmpfile);

	// replace path delims with _ so that files are created directly under %USERPROFILE%\Local Settings\temp
	// skip the first cos we wanna keep that one
	for (int i = 1;; i++)
	{
		if (fpath2[i] == 0) break;
		if (fpath2[i] == '/') fpath2[i] = '_';
		if (fpath2[i] == '\\') fpath2[i] = '_';
	}

	// now build the final name
	strcat (fpath1, fpath2);

	// create the file - see http://blogs.msdn.com/larryosterman/archive/2004/04/19/116084.aspx for
	// further info on the flags chosen here.
	HANDLE hf = CreateFile
	(
		fpath1,
		FILE_WRITE_DATA | FILE_READ_DATA,
		FILE_SHARE_READ,
		NULL,
		CREATE_ALWAYS,
		FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
		NULL
	);

	// either good or INVALID_HANDLE_VALUE
	return hf;
}


void COM_DecompressFile (char *filename, byte **decompressbuf, int *decompresslen)
{
	byte *buf = NULL;
	decompresslen[0] = 0;

	unzFile			uf = NULL;
	int				err;
	unz_global_info gi;
	unz_file_info	file_info;

	uf = unzOpen (filename);
	err = unzGetGlobalInfo (uf, &gi);

	if (err == UNZ_OK)
	{
		char filename_inzip[64];

		unzGoToFirstFile (uf);

		for (int i = 0; i < gi.number_entry; i++)
		{
			err = unzOpenCurrentFile (uf);

			if (err != UNZ_OK)
			{
				// something bad happened
				buf = NULL;
				goto done;
			}

			// the first file is the only one we want... ;)
			err = unzGetCurrentFileInfo (uf, &file_info, filename_inzip, sizeof (filename_inzip), NULL, 0, NULL, 0);

			if (err == UNZ_OK)
			{
				buf = (byte *) MainZone->Alloc (file_info.uncompressed_size);

				int bytesread = unzReadCurrentFile (uf, buf, file_info.uncompressed_size);

				if (bytesread != file_info.uncompressed_size)
				{
					unzCloseCurrentFile (uf);
					buf = NULL;
					goto done;
				}

				// done!
				decompresslen[0] = file_info.uncompressed_size;
				unzCloseCurrentFile (uf);
				goto done;
			}
			else
			{
				// something bad happened
				buf = NULL;
				unzCloseCurrentFile (uf);
				goto done;
			}
		}
	}

done:;
	unzClose (uf);
	decompressbuf[0] = buf;
}


HANDLE COM_UnzipPK3FileToTemp (pk3_t *pk3, char *filename)
{
	// initial scan ensures the file is present before opening the zip (perf)
	for (int i = 0; i < pk3->numfiles; i++)
	{
		if (!stricmp (pk3->files[i].name, filename))
		{
			unzFile			uf = NULL;
			int				err;
			unz_global_info gi;
			unz_file_info	file_info;

			uf = unzOpen (pk3->filename);
			err = unzGetGlobalInfo (uf, &gi);

			if (err == UNZ_OK)
			{
				char filename_inzip[64];

				unzGoToFirstFile (uf);

				for (int i = 0; i < gi.number_entry; i++)
				{
					err = unzOpenCurrentFile (uf);

					if (err != UNZ_OK)
					{
						// something bad happened
						unzClose (uf);
						return INVALID_HANDLE_VALUE;
					}

					err = unzGetCurrentFileInfo (uf, &file_info, filename_inzip, sizeof (filename_inzip), NULL, 0, NULL, 0);

					if (err == UNZ_OK)
					{
						if (!stricmp (filename_inzip, filename))
						{
							// got it, so unzip it to the temp folder
							byte *unztemp = (byte *) scratchbuf;
							DWORD byteswritten;

							HANDLE pk3handle = COM_MakeTempFile (filename);

							// didn't create it successfully
							if (pk3handle == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;

							for (;;)
							{
								// read in SCRATCHBUF_SIZE blocks
								int bytesread = unzReadCurrentFile (uf, unztemp, SCRATCHBUF_SIZE);

								if (bytesread < 0)
								{
									// something bad happened
									unzCloseCurrentFile (uf);
									COM_FCloseFile (&pk3handle);
									unzClose (uf);
									return INVALID_HANDLE_VALUE;
								}

								if (bytesread == 0) break;

								if (!COM_FWriteFile (pk3handle, unztemp, bytesread))
								{
									COM_FCloseFile (&pk3handle);
									pk3handle = INVALID_HANDLE_VALUE;
									break;
								}
							}

							unzCloseCurrentFile (uf);
							unzClose (uf);
							return pk3handle;
						}
					}

					unzGoToNextFile (uf);
				}
			}

			// didn't find it
			unzClose (uf);
			return INVALID_HANDLE_VALUE;
		}
	}

	// not present
	return INVALID_HANDLE_VALUE;
}


int COM_FWriteFile (void *fh, void *buf, int len)
{
	DWORD byteswritten;

	BOOL ret = WriteFile ((HANDLE) fh, buf, len, &byteswritten, NULL);

	if (ret && byteswritten == len)
		return 1;
	else return 0;
}


int COM_FReadFile (void *fh, void *buf, int len)
{
	DWORD bytesread;

	BOOL ret = ReadFile ((HANDLE) fh, buf, len, &bytesread, NULL);

	if (ret)
		return (int) bytesread;
	else return -1;
}


int COM_FReadChar (void *fh)
{
	char rc;
	DWORD bytesread;

	BOOL ret = ReadFile ((HANDLE) fh, &rc, 1, &bytesread, NULL);

	if (ret && bytesread == 1)
		return (int) (byte) rc;
	else return -1;
}


bool COM_ValidateMDLFile (HANDLE fh)
{
	unsigned int mdlid = 0;

	// make sure that we can read it all in
	if (COM_FReadFile (fh, &mdlid, 4) != 4)
		return false;

	// check for MD3 masquerading as mdl
	if (mdlid != IDPOLYHEADER)
		return false;

	// OK, it's an MDL file now; put the file pointer back
	SetFilePointer (fh, -4, NULL, FILE_CURRENT);
	// Con_Printf ("Validated MDL file\n");
	return true;
}


int COM_FOpenFile (char *filename, void *hf)
{
	// don't error out here
	if (!hf)
	{
		Con_SafePrintf ("COM_FOpenFile: hFile not set");
		com_filesize = -1;
		return -1;
	}

	// silliness here and above is needed because common.h doesn't know what a HANDLE is
	HANDLE *hFile = (HANDLE *) hf;

	// darkplaces does something evil; it allows MD3 files to be loaded with a .mdl extension.  Here we need to flag if we're trying to load an MDL
	// so that we can confirm if it's REALLY an MDL... grrrr...
	bool checkmdl = false;

	for (int i = strlen (filename); i; i--)
	{
		if (!stricmp (&filename[i], ".mdl"))
		{
			checkmdl = true;
			break;
		}

		if (filename[i] == '.' || filename[i] == '/' || filename[i] == '\\') break;
	}

	// ensure for the close test below
	*hFile = INVALID_HANDLE_VALUE;

	// search pak files second
	for (searchpath_t *search = com_searchpaths; search; search = search->next)
	{
		// ensure...
		if (*hFile != INVALID_HANDLE_VALUE) COM_FCloseFile (hFile);

		*hFile = INVALID_HANDLE_VALUE;
		com_filesize = -1;

		if (search->pack)
		{
			// look through all the pak file elements
			pack_t *pak = search->pack;

			for (int i = 0; i < pak->numfiles; i++)
			{
				if (!stricmp (pak->files[i].name, filename))
				{
					// note - we need to share read access because e.g. a demo could result in 2 simultaneous
					// reads, one for the .dem file and one for a .bsp file
					*hFile = CreateFile
					(
						pak->filename,
						FILE_READ_DATA,
						FILE_SHARE_READ,
						NULL,
						OPEN_EXISTING,
						FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_NO_RECALL | FILE_FLAG_SEQUENTIAL_SCAN,
						NULL
					);

					// this can happen if a PAK file was enumerated on startup but deleted while running
					if (*hFile == INVALID_HANDLE_VALUE)
					{
						DWORD dwerr = GetLastError ();
						continue;
					}

					SetFilePointer (*hFile, pak->files[i].filepos, NULL, FILE_BEGIN);
					com_filesize = pak->files[i].filelen;

					if (checkmdl && !COM_ValidateMDLFile (*hFile))
					{
						CloseHandle (*hFile);
						*hFile = INVALID_HANDLE_VALUE;
					}
					else return com_filesize;
				}
			}
		}
		else if (search->pk3)
		{
			HANDLE pk3handle = COM_UnzipPK3FileToTemp (search->pk3, filename);

			if (pk3handle != INVALID_HANDLE_VALUE)
			{
				*hFile = pk3handle;

				// need to reset the file pointer as it will be at eof owing to the file just having been created
				com_filesize = GetFileSize (*hFile, NULL);
				SetFilePointer (*hFile, 0, NULL, FILE_BEGIN);

				if (checkmdl && !COM_ValidateMDLFile (*hFile))
				{
					CloseHandle (*hFile);
					*hFile = INVALID_HANDLE_VALUE;
				}
				else return com_filesize;
			}
		}
		else
		{
			char netpath[MAX_PATH];

			// check for a file in the directory tree
			_snprintf (netpath, 128, "%s/%s", search->filename, filename);

			// note - we need to share read access because e.g. a demo could result in 2 simultaneous
			// reads, one for the .dem file and one for a .bsp file
			*hFile = CreateFile
			(
				netpath,
				FILE_READ_DATA,
				FILE_SHARE_READ,
				NULL,
				OPEN_EXISTING,
				FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_NO_RECALL | FILE_FLAG_SEQUENTIAL_SCAN,
				NULL
			);

			if (*hFile == INVALID_HANDLE_VALUE) continue;

			com_filesize = GetFileSize (*hFile, NULL);

			if (checkmdl && !COM_ValidateMDLFile (*hFile))
			{
				CloseHandle (*hFile);
				*hFile = INVALID_HANDLE_VALUE;
			}
			else return com_filesize;
		}
	}

	// not found
	*hFile = INVALID_HANDLE_VALUE;
	com_filesize = -1;
	return -1;
}


void COM_FCloseFile (void *fh)
{
	CloseHandle (*((HANDLE *) fh));
	*((HANDLE *) fh) = INVALID_HANDLE_VALUE;
}


/*
============
COM_LoadFile

Filename are reletive to the quake directory.
Always appends a 0 byte.

Loads a file into the specified buffer or the zone.
============
*/
static byte *COM_LoadFile (char *path, class CQuakeHunk *spacebuf, class CQuakeZone *heapbuf)
{
	HANDLE	fh = INVALID_HANDLE_VALUE;
	byte    *buf = NULL;

	// look for it in the filesystem or pack files
	int len = COM_FOpenFile (path, &fh);

	if (fh == INVALID_HANDLE_VALUE) return NULL;

	if (spacebuf)
		buf = (byte *) spacebuf->Alloc (len + 1);
	else if (heapbuf)
		buf = (byte *) heapbuf->Alloc (len + 1);
	else buf = (byte *) Zone_Alloc (len + 1);

	if (!buf)
	{
		Con_DPrintf ("COM_LoadFile: not enough space for %s", path);
		COM_FCloseFile (&fh);
		return NULL;
	}

	((byte *) buf)[len] = 0;
	int Success = COM_FReadFile (fh, buf, len);
	COM_FCloseFile (&fh);

	if (Success == -1) return NULL;

	return buf;
}


byte *COM_LoadFile (char *path, class CQuakeHunk *spacebuf)
{
	return COM_LoadFile (path, spacebuf, NULL);
}


byte *COM_LoadFile (char *path, class CQuakeZone *heapbuf)
{
	return COM_LoadFile (path, NULL, heapbuf);
}


byte *COM_LoadFile (char *path)
{
	return COM_LoadFile (path, NULL, NULL);
}


/*
=================
COM_LoadPackFile

Takes an explicit (not game tree related) path to a pak file.

Loads the header and directory, adding the files at the beginning
of the list so they override previous pack files.
=================
*/
pack_t *COM_LoadPackFile (char *packfile)
{
	dpackheader_t header;
	int i;
	int numpackfiles;
	pack_t *pack;
	FILE *packfp;
	packfile_t *info;
	unsigned short crc;

	// read and validate the header
	if (!(packfp = fopen (packfile, "rb"))) return NULL;

	fread (&header, sizeof (header), 1, packfp);

	if (header.id[0] != 'P' || header.id[1] != 'A' || header.id[2] != 'C' || header.id[3] != 'K')
	{
		Con_SafePrintf ("%s is not a packfile", packfile);
		fclose (packfp);
		return NULL;
	}

	header.dirofs = header.dirofs;
	header.dirlen = header.dirlen;

	numpackfiles = header.dirlen / sizeof (packfile_t);

	info = (packfile_t *) GameZone->Alloc (numpackfiles * sizeof (packfile_t));

	fseek (packfp, header.dirofs, SEEK_SET);
	fread (info, header.dirlen, 1, packfp);
	fclose (packfp);

	// parse the directory
	for (i = 0; i < numpackfiles; i++)
	{
		info[i].filepos = info[i].filepos;
		info[i].filelen = info[i].filelen;
	}

	pack = (pack_t *) GameZone->Alloc (sizeof (pack_t));
	Q_strncpy (pack->filename, packfile, 127);
	pack->numfiles = numpackfiles;
	pack->files = info;

	Con_SafePrintf ("Added packfile %s (%i files)\n", packfile, numpackfiles);
	return pack;
}


void COM_InitFilesystem (void)
{
	// check for expansion packs
	// (these are only checked at startup as the player might want to switch them off during gameplay; otherwise
	// they would be enforced on always)
	if (COM_CheckParm ("-rogue")) Cvar_Set ("com_rogue", 1);
	if (COM_CheckParm ("-hipnotic")) Cvar_Set ("com_hipnotic", 1);
	if (COM_CheckParm ("-quoth")) Cvar_Set ("com_quoth", 1);
	if (COM_CheckParm ("-nehahra")) Cvar_Set ("com_nehahra", 1);

	// -game <gamedir>
	// adds gamedir as an override game
	int i = COM_CheckParm ("-game");

	// load the specified game
	if (i && i < com_argc - 1)
		COM_LoadGame (com_argv[i + 1]);
	else COM_LoadGame (NULL);
}


// if we're going to allow players to set content locations we really should also validate them
bool COM_ValidateContentFolderCvar (cvar_t *var)
{
	if (!var->string)
	{
		Con_Printf ("%s is invalid : name does not exist\n", var->name);
		return false;
	}

	// this is a valid path as it means we're in the root of our game folder
	if (!var->string[0]) return true;

	if ((var->string[0] == '/' || var->string[0] == '\\') && !var->string[1])
	{
		// this is a valid path and is replaced by ''
		Cvar_Set (var, "");
		return true;
	}

	if (strlen (var->string) > 64)
	{
		Con_Printf ("%s is invalid : name is too long\n", var->name);
		return false;
	}

	if (var->string[0] == '/' || var->string[0] == '\\')
	{
		Con_Printf ("%s is invalid : cannot back up beyond %s\n", var->name, com_gamedir);
		return false;
	}

	// copy it off so that we can safely modify it if need be
	char tempname[256];

	strcpy (tempname, var->string);

	// remove trailing /
	for (int i = 0;; i++)
	{
		// end of path
		if (!tempname[i]) break;

		if ((tempname[i] == '/' || tempname[i] == '\\') && !tempname[i + 1])
		{
			tempname[i] = 0;
			break;
		}
	}

	// \ / : * ? " < > | are all invalid in a name
	for (int i = 0;; i++)
	{
		// end of path
		if (!tempname[i]) break;

		// a folder separator is allowed at the end of the path
		if ((tempname[i] == '/' || tempname[i] == '\\') && !tempname[i + 1]) break;

		if (tempname[i] == '.' && tempname[i + 1] == '.')
		{
			Con_Printf ("%s is invalid : relative paths are not allowed\n", var->name);
			return false;
		}

		switch (tempname[i])
		{
		case ' ':
			Con_Printf ("%s is invalid : paths with spaces are not allowed\n", var->name);
			return false;

		case '\\':
		case '/':
		case ':':
		case '*':
		case '?':
		case '"':
		case '<':
		case '>':
		case '|':
			Con_Printf ("%s is invalid : contains \\ / : * ? \" < > or | \n", var->name);
			return false;

		default: break;
		}
	}

	// attempt to create the directory - CreateDirectory will fail if the directory already exists
	if (!PathIsDirectory (va ("%s/%s", com_gamedir, tempname)))
	{
		if (!CreateDirectory (va ("%s/%s", com_gamedir, tempname), NULL))
		{
			Con_Printf ("%s is invalid : failed to create directory\n", var->name);
			return false;
		}
	}

	// attempt to create a file in it; the user must have rw access to the directory
	HANDLE hf = CreateFile
	(
		va ("%s/%s/tempfile.tmp", com_gamedir, tempname),
		FILE_WRITE_DATA | FILE_READ_DATA,
		0,
		NULL,
		CREATE_ALWAYS,
		FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
		NULL
	);

	if (hf == INVALID_HANDLE_VALUE)
	{
		Con_Printf ("%s is invalid : failed to create file\n", var->name);
		return false;
	}

	CloseHandle (hf);

	// path is valid now; we need a trailing / so add one
	Cvar_Set (var, va ("%s/", tempname));
	return true;
}


void COM_ValidateUserSettableDir (cvar_t *var)
{
	if (!COM_ValidateContentFolderCvar (var))
	{
		Con_Printf ("Resetting to default \"%s\"\n", var->defaultvalue);
		Cvar_Set (var, var->defaultvalue);
	}
}


