
#include <windows.h>
#include <assert.h>
#include <Dbghelp.h>
#include <stdio.h>

#pragma comment (lib, "Dbghelp.lib")

#ifdef _DEBUG

bool symbolsinit = false;


void InitSymbols (void)
{
	if (!symbolsinit)
	{
		// get the current symbols options
		DWORD dwOptions = SymGetOptions ();

		// load lines
		SymSetOptions (dwOptions | SYMOPT_LOAD_LINES);

		BOOL blah = SymInitialize (GetCurrentProcess (), NULL, TRUE);
		assert (blah);

		symbolsinit = true;
	}
}


void KillSymbols (void)
{
	if (symbolsinit)
	{
		SymCleanup (GetCurrentProcess ());
		symbolsinit = false;
	}
}


void GetCrashReason (LPEXCEPTION_POINTERS ep)
{
	// ensure that the exception pointers weren't stomped
	if (!ep) return;

	if (IsBadReadPtr (ep, sizeof (EXCEPTION_POINTERS))) return;

	// turn on the symbols engine
	InitSymbols ();

	DWORD dwLineDisp = 0;
	IMAGEHLP_LINE64 crashline = {sizeof (IMAGEHLP_LINE64), NULL, 0, NULL, 0};

	if (SymGetLineFromAddr64 (GetCurrentProcess (), (DWORD64) ep->ExceptionRecord->ExceptionAddress, &dwLineDisp, &crashline))
	{
		char msg[2048];

		sprintf (msg, "file: %s  line: %i", crashline.FileName, (int) crashline.LineNumber);
		MessageBox (NULL, msg, "An error has occurred", MB_OK | MB_ICONSTOP);
	}
	else MessageBox (NULL, "Unknown error", "An error has occurred", MB_OK | MB_ICONSTOP);

	// and turn it off again
	KillSymbols ();
}
#else
void GetCrashReason (LPEXCEPTION_POINTERS ep)
{
	// if we're not using a debug build all that we can do is display an error
	MessageBox (NULL, "Something bad happened and DirectQ is now toast.\n"
				"Please visit http://mhquake.blogspot.com and report this crash.",
				"An error has occurred",
				MB_OK | MB_ICONSTOP);
}
#endif
