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

// ascii to unicode and back conversions; these are for functions that only exist in unicode versions (like in GDI+)
// of course nobody in Microsoft EVER thought that anybody would be porting a legacy application.  NEVER.

#include "versions.h"

#include <windows.h>

WCHAR *QASCIIToUnicode (char *strasc)
{
	static WCHAR struni[1024] = {0};

	if (!strasc) return NULL;

	int len = strlen (strasc) + 1;

	if (len > 1023) len = 1023;

	if (!MultiByteToWideChar (CP_ACP, 0, strasc, len, struni, len))
		return NULL;

	return struni;
}


char *QUnicodeToASCII (WCHAR *struni)
{
	static char strasc[1024] = {0};

	if (!struni) return NULL;

	int len = wcslen (struni) + 1;

	if (len > 1023) len = 1023;

	if (!WideCharToMultiByte (CP_ACP, 0, struni, len, strasc, len, NULL, NULL))
		return NULL;

	return strasc;
}
