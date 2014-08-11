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

typedef bool (*DOWNLOADPROGRESSPROC) (int, int);
DWORD Web_DoDownload (char *url, char *file, DOWNLOADPROGRESSPROC progress);
char *Web_GetErrorString (int errcode);

#define DL_ERR_NO_ERROR				0
#define DL_ERR_NO_DLL				1
#define DL_ERR_UNKNOWN				2
#define DL_ERR_NO_ENTRYPOINT		3
#define DL_ERR_OPENFAIL				4
#define DL_ERR_OPENURLFAIL			5
#define DL_ERR_QUERYINFOFAIL		6
#define DL_ERR_CREATETEMPFILEFAIL	7
#define DL_ERR_READFAIL				8
#define DL_ERR_MALFORMEDDOWN		9
#define DL_ERR_PROGRESSABORT		10
#define DL_ERR_MOVEFILEFAIL			11
#define DL_ERR_TEMPPATHFAIL			12
#define DL_ERR_GETTEMPFILEFAIL		13
