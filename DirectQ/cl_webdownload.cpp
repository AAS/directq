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

#include "versions.h"

#include <windows.h>
#include <wininet.h>
#include <urlmon.h>
#include <stdio.h>
#include "quakedef.h"
#include "webdownload.h"

typedef HINTERNET (__stdcall *QINTERNETOPENPROC) (LPCSTR, DWORD, LPCSTR, LPCSTR, DWORD);
typedef HINTERNET (__stdcall *QINTERNETOPENURLPROC) (HINTERNET, LPCSTR, LPCSTR, DWORD, DWORD, DWORD_PTR);
typedef BOOL (__stdcall *QHTTPQUERYINFOPROC) (HINTERNET, DWORD, LPVOID, LPDWORD, LPDWORD);
typedef BOOL (__stdcall *QINTERNETREADFILEPROC) (HINTERNET, LPVOID, DWORD, LPDWORD);
typedef BOOL (__stdcall *QINTERNETCLOSEHANDLEPROC) (HINTERNET);

// this is a good size for the read buffer, anything higher makes no difference whereas anything
// lower will totally kill performance by doing lots of small reads and writes
#define MAX_READ_BUFFER 1024
unsigned char ReadBuffer[MAX_READ_BUFFER];

DWORD Web_DoDownload (char *url, char *file, DOWNLOADPROGRESSPROC progress)
{
	// need to set up a correct dest path because the incoming one may be stomped during the progress callback if it was a va
	// this should be the first thing done as we'll run some callbacks even before we start downloading
	char DestPath[256];
	strcpy (DestPath, file);

	// because we have no way of knowing the integrity of the client machine we load everything dynamically
	// instead of using the library versions (which just do this behind the scenes anyway).  this also
	// helps to prevent any theoretical library version conflict errors by not relying on the user to have
	// the same versions as the app was compiled with.
	HINSTANCE hInstWinInet = LoadLibrary ("wininet.dll");

	if (!hInstWinInet) return DL_ERR_NO_DLL;

	// progress (0, 0) just initializes some counters and sends a keepalive in case the dynamic load takes too long
	if (progress) progress (0, 0);

	// assume that the download is in an unknown error condition until we know otherwise
	DWORD errcode = DL_ERR_UNKNOWN;

	// more dynamic loading here
	QINTERNETOPENPROC QInternetOpen = (QINTERNETOPENPROC) GetProcAddress (hInstWinInet, "InternetOpenA");
	QINTERNETOPENURLPROC QInternetOpenUrl = (QINTERNETOPENURLPROC) GetProcAddress (hInstWinInet, "InternetOpenUrlA");
	QHTTPQUERYINFOPROC QHttpQueryInfo = (QHTTPQUERYINFOPROC) GetProcAddress (hInstWinInet, "HttpQueryInfoA");
	QINTERNETREADFILEPROC QInternetReadFile = (QINTERNETREADFILEPROC) GetProcAddress (hInstWinInet, "InternetReadFile");
	QINTERNETCLOSEHANDLEPROC QInternetCloseHandle = (QINTERNETCLOSEHANDLEPROC) GetProcAddress (hInstWinInet, "InternetCloseHandle");

	// progress (0, 0) just initializes some counters and sends a keepalive in case the dynamic load takes too long
	if (progress) progress (0, 0);

	// here we ensure that all the required entrypoints were loaded OK in case any user has done anything
	// silly with their OS.
	if (!QInternetOpen || !QInternetOpenUrl || !QHttpQueryInfo || !QInternetReadFile || !QInternetCloseHandle)
	{
		errcode = DL_ERR_NO_ENTRYPOINT;
		goto cleanup_fail;
	}

	FILE *fDownload = NULL;
	HINTERNET hInternet = NULL;
	HINTERNET hURL = NULL;
	DWORD DownloadSize = -1;

	// this just opens an internet connection assuming that the configuration is already set for you
	// we could set it to work through a user supplied proxy server, and we might extend the multiplayer
	// menu to allow for that at some point in time.
	if (!(hInternet = QInternetOpen ("DirectQ", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0)))
	{
		errcode = DL_ERR_OPENFAIL;
		goto cleanup_fail;
	}

	// this one opens a specified URL; we don't use the cache or any UI here
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
		errcode = DL_ERR_OPENURLFAIL;
		goto cleanup_fail;
	}

	// now we get the size of the file to be downloaded in bytes, reusing the standard read buffer for
	// temporary storage until we can convert the returned value to an integer result.  we assume that
	// there won't be any Quake maps > 2 GB in size here...
	DownloadSize = MAX_READ_BUFFER;

	if (!QHttpQueryInfo (hURL, HTTP_QUERY_CONTENT_LENGTH, ReadBuffer, &DownloadSize, NULL))
	{
		errcode = DL_ERR_QUERYINFOFAIL;
		goto cleanup_fail;
	}

	DownloadSize = atoi ((char *) ReadBuffer);

	// also ensure that we got a value here
	if (!DownloadSize)
	{
		errcode = DL_ERR_QUERYINFOFAIL;
		goto cleanup_fail;
	}

	// make a temporary name for it to go to
	// this will go into the user profile's "TEMP" folder so as to avoid polluting the Quake folder itself
	char TempPath[256];

	// if we don't get it we'll just pollute the Quake folder.  this is only temporary anyway as
	// the file will be either deleted or moved once the download process completes.  note that we could
	// cache this value.
	if (!GetTempPath (256, TempPath))
	{
		errcode = DL_ERR_TEMPPATHFAIL;
		goto cleanup_fail;
	}

	// construct a unique name for the temp file
	if (!GetTempFileName (TempPath, "qdl", 0, (char *) ReadBuffer))
	{
		errcode = DL_ERR_GETTEMPFILEFAIL;
		goto cleanup_fail;
	}

	// copy out the final version of the unique name
	strcpy (TempPath, (char *) ReadBuffer);

	// attempt to remove the temp file if it already exists;
	// we'll just let this fail silently if it didn't exist or if we can't remove it
	DeleteFile (TempPath);

	// attempt to open it - this should only fail if the user has done something silly to their OS
	if (!(fDownload = fopen (TempPath, "wb")))
	{
		errcode = DL_ERR_CREATETEMPFILEFAIL;
		goto cleanup_fail;
	}

	// total number of bytes downloaded; this must be equal to the number of bytes expected
	// for the download to be considered a success
	DWORD TotalBytes = 0;

	while (1)
	{
		DWORD BytesRead;

		// here we read from the URL into our temp buffer; we allow up to 10 retries if a read fails
		// so that we can cope a little more robustly with transient connection dropouts
		for (int i = 0; i <= 10; i++)
		{
			// if we read OK we don't need any retries
			if (QInternetReadFile (hURL, ReadBuffer, MAX_READ_BUFFER, &BytesRead))
				break;

			if (i == 9)
			{
				errcode = DL_ERR_READFAIL;
				goto cleanup_fail;
			}

			// sleep for a little to give the connection a chance to come back up
			Sleep (i * 100);

			// run a progress update so that we get a keepalive sent
			if (progress)
			{
				// we also need to pass in the download size so that we can calculate a percentage
				// and display an initial message indicating how many bytes in total we must download.
				// we ensured that we got a > 0 value for download size above so this is safe
				if (!progress (DownloadSize, (int) (((float) TotalBytes / (float) DownloadSize) * 100.0f)))
				{
					errcode = DL_ERR_PROGRESSABORT;
					goto cleanup_fail;
				}
			}
		}

		TotalBytes += BytesRead;

		// if we read nothing we have an end of download
		if (BytesRead == 0)
		{
			// end of file
			if (TotalBytes == DownloadSize)
				errcode = DL_ERR_NO_ERROR;
			else errcode = DL_ERR_MALFORMEDDOWN;

			break;
		}

		// update progress
		if (progress)
		{
			// we also need to pass in the download size so that we can calculate a percentage
			// and display an initial message indicating how many bytes in total we must download.
			// we ensured that we got a > 0 value for download size above so this is safe
			if (!progress (DownloadSize, (int) (((float) TotalBytes / (float) DownloadSize) * 100.0f)))
			{
				errcode = DL_ERR_PROGRESSABORT;
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
	if (errcode == DL_ERR_NO_ERROR)
	{
		// attempt to delete any occurances of the dest file that may have been copied in by the
		// user during the download process so that we can successfully move the downloaded file to there
		DeleteFile (DestPath);

		if (TempPath[0] == DestPath[0] && TempPath[1] == ':' && DestPath[1] == ':')
		{
			// we can use MoveFile on the same volume
			if (!MoveFile (TempPath, DestPath))
				errcode = DL_ERR_MOVEFILEFAIL;
			else errcode = DL_ERR_NO_ERROR;
		}
		else
		{
			// if we;re going between different volumes we need to copy it
			if (!CopyFile (TempPath, DestPath, FALSE))
				errcode = DL_ERR_MOVEFILEFAIL;
			else errcode = DL_ERR_NO_ERROR;

			// remove the old file
			DeleteFile (TempPath);
		}
	}
	else DeleteFile (TempPath);

	// because QInternetOpen and QInternetOpenUrl must have values for the handles to have values,
	// QInternetCloseHandle is also guaranteed to have a value here.
	if (hURL) QInternetCloseHandle (hURL);
	if (hInternet) QInternetCloseHandle (hInternet);

	QInternetOpen = NULL;
	QInternetOpenUrl = NULL;
	QHttpQueryInfo = NULL;
	QInternetReadFile = NULL;
	QInternetCloseHandle = NULL;

	UNLOAD_LIBRARY (hInstWinInet);

	// phew!
	return errcode;
}


char *dlResultStrings[] =
{
	"Download Successful",
	"Could not open \"wininet.dll\"",
	"Unknown error",
	"GetProcAddress failed on \"wininet.dll\"",
	"Failed to open internet connection",
	"Failed to open URL",
	"Query for URL info failed",
	"Failed to create temp file for download",
	"Failed to read from URL",
	"Malformed download size",
	"Download aborted by user",
	"Failed to create download target file",
	"Could not get temp directory",
	"Could not create a unique temp file name",
	NULL
};


char *Web_GetErrorString (int errcode)
{
	for (int i = 0;; i++)
	{
		// invalid code
		if (!dlResultStrings[i]) return dlResultStrings[2];

		// actual correct code
		if (i == errcode) return dlResultStrings[i];
	}

	// never gets to here
	return dlResultStrings[2];
}
