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

#include <windows.h>
#include <wininet.h>
#include <urlmon.h>
#include <stdio.h>
#include "webdownload.h"

typedef HINTERNET (__stdcall *QINTERNETOPENPROC) (LPCSTR, DWORD, LPCSTR, LPCSTR, DWORD);
typedef HINTERNET (__stdcall *QINTERNETOPENURLPROC) (HINTERNET, LPCSTR, LPCSTR, DWORD, DWORD, DWORD_PTR);
typedef BOOL (__stdcall *QHTTPQUERYINFOPROC) (HINTERNET, DWORD, LPVOID, LPDWORD, LPDWORD);
typedef BOOL (__stdcall *QINTERNETREADFILEPROC) (HINTERNET, LPVOID, DWORD, LPDWORD);
typedef BOOL (__stdcall *QINTERNETCLOSEHANDLEPROC) (HINTERNET);

#define MAX_READ_BUFFER 1024
unsigned char ReadBuffer[MAX_READ_BUFFER];

DWORD Web_DoDownload (char *url, char *file, DOWNLOADPROGRESSPROC progress)
{
	HINSTANCE hInstWinInet = LoadLibrary ("wininet.dll");

	if (!hInstWinInet) return 6;

	if (progress) progress (0, 0);

	DWORD errcode = 666;

	QINTERNETOPENPROC QInternetOpen = (QINTERNETOPENPROC) GetProcAddress (hInstWinInet, "InternetOpenA");
	QINTERNETOPENURLPROC QInternetOpenUrl = (QINTERNETOPENURLPROC) GetProcAddress (hInstWinInet, "InternetOpenUrlA");
	QHTTPQUERYINFOPROC QHttpQueryInfo = (QHTTPQUERYINFOPROC) GetProcAddress (hInstWinInet, "HttpQueryInfoA");
	QINTERNETREADFILEPROC QInternetReadFile = (QINTERNETREADFILEPROC) GetProcAddress (hInstWinInet, "InternetReadFile");
	QINTERNETCLOSEHANDLEPROC QInternetCloseHandle = (QINTERNETCLOSEHANDLEPROC) GetProcAddress (hInstWinInet, "InternetCloseHandle");

	if (progress) progress (0, 0);

	if (!QInternetOpen || !QInternetOpenUrl || !QHttpQueryInfo || !QInternetReadFile || !QInternetCloseHandle)
	{
		errcode = 8;
		goto cleanup_fail;
	}

	FILE *fDownload = NULL;
	HINTERNET hInternet = NULL;
	HINTERNET hURL = NULL;
	DWORD DownloadSize = -1;

	if (!(hInternet = QInternetOpen ("DirectQ", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0)))
	{
		errcode = 1;
		goto cleanup_fail;
	}

	if (!(hURL = QInternetOpenUrl
	(
		hInternet,
		url,
		NULL,
		0,
		INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_UI | INTERNET_FLAG_PRAGMA_NOCACHE | INTERNET_FLAG_RELOAD,
		0
	)))
	{
		errcode = 2;
		goto cleanup_fail;
	}

	DownloadSize = MAX_READ_BUFFER;

	if (!QHttpQueryInfo (hURL, HTTP_QUERY_CONTENT_LENGTH, ReadBuffer, &DownloadSize, NULL))
	{
		errcode = 3;
		goto cleanup_fail;
	}

	DownloadSize = atoi ((char *) ReadBuffer);

	// make a temporary name for it to go to
	char TempPath[256];

	if (!GetTempPath (256, TempPath)) TempPath[0] = 0;
	strcat (TempPath, "qdownload.tmp");

	// attempt to open it
	if (!(fDownload = fopen (TempPath, "wb")))
	{
		errcode = 4;
		goto cleanup_fail;
	}

	DWORD TotalBytes = 0;

	// need to set up a correct dest path because the incoming one may be stomped during the progress callback if it was a va
	char DestPath[256];
	strcpy (DestPath, file);

	while (1)
	{
		DWORD BytesRead;

		if (!QInternetReadFile (hURL, ReadBuffer, MAX_READ_BUFFER, &BytesRead))
		{
			errcode = 5;
			break;
		}

		TotalBytes += BytesRead;

		if (BytesRead == 0)
		{
			// end of file
			if (TotalBytes == DownloadSize)
				errcode = 0;
			else errcode = 9;

			break;
		}

		// update progress
		if (DownloadSize && progress)
		{
			if (!progress (DownloadSize, (int) (((float) TotalBytes / (float) DownloadSize) * 100.0f)))
			{
				errcode = 7;
				goto cleanup_fail;
			}
		}

		// write out
		fwrite (ReadBuffer, BytesRead, 1, fDownload);
	}

	// success also falls through here
cleanup_fail:;
	if (fDownload) fclose (fDownload);

	// if we succeeded we move the temp file to it's final location, otherwise we delete it
	if (!errcode)
		MoveFile (TempPath, DestPath);
	else DeleteFile (TempPath);

	if (hURL) QInternetCloseHandle (hURL);
	if (hInternet) QInternetCloseHandle (hInternet);

	if (hInstWinInet) FreeLibrary (hInstWinInet);

	return errcode;
}
