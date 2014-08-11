
#include <windows.h>
#include "resource.h"

HINSTANCE SplashInstance;
HWND SplashWindow;
#define SPLASH_CLASS_NAME "Splash Screen for DirectQ 1.1"



void Splash_Init (HINSTANCE hInstance)
{
	SplashInstance = hInstance;

	WNDCLASSEX wc;

	wc.cbClsExtra = 0;
	wc.cbSize = sizeof (WNDCLASSEX);
	wc.cbWndExtra = 0;
	wc.hbrBackground = (HBRUSH) GetStockObject (BLACK_BRUSH);
	wc.hCursor = LoadCursor (NULL, IDC_ARROW);
	wc.hIcon = LoadIcon (SplashInstance, MAKEINTRESOURCE (IDI_ICON1));
	wc.hIconSm = LoadIcon (SplashInstance, MAKEINTRESOURCE (IDI_ICON1));
	wc.hInstance = SplashInstance;
	wc.lpfnWndProc = (WNDPROC) DefWindowProc;
	wc.lpszClassName = SPLASH_CLASS_NAME;
	wc.lpszMenuName = NULL;
	wc.style = 0;

	// register the class
	RegisterClassEx (&wc);

	// load the GDI image from the resource
	// do this before even creating the window so that we can set it's dimensions to the same as the bitmap ;)
	HBITMAP hBmp = (HBITMAP) LoadImage (GetModuleHandle (NULL), MAKEINTRESOURCE (IDB_BITMAP1), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);

	BITMAP BM;

	// get info about the bitmap
	GetObject (hBmp, sizeof (BM), &BM);

	int bmpWidth = BM.bmWidth;
	int bmpHeight = BM.bmHeight;

	// done with the bitmap object
	DeleteObject (&BM);

	SplashWindow = CreateWindowEx
	(
		WS_EX_TOPMOST,
		SPLASH_CLASS_NAME,
		"Starting MHQuake...",
		WS_POPUP,
		0,
		0,
		bmpWidth,
		bmpHeight,
		NULL,
		NULL,
		SplashInstance,
		NULL
	);

	// get the desktop resolution
	HDC DesktopDC = GetDC (NULL);
	int DeskWidth = GetDeviceCaps (DesktopDC, HORZRES);
	int DeskHeight = GetDeviceCaps (DesktopDC, VERTRES);
	ReleaseDC (NULL, DesktopDC);

	// center the splash window
	SetWindowPos (SplashWindow, NULL, (DeskWidth - bmpWidth) / 2, (DeskHeight - bmpHeight) / 2, bmpWidth, bmpHeight, 0);

	// show and update the window
	ShowWindow (SplashWindow, SW_SHOWDEFAULT);
	UpdateWindow (SplashWindow);

	// give the window the focus
	SetForegroundWindow (SplashWindow);

	// get the DC for the window and create a DC for the bitmap that's compatible with it
	HDC hDC = GetDC (SplashWindow);
	HDC hBmpDC = CreateCompatibleDC (hDC);

	// select the bitmap into the compatible DC and blit it to the window
	SelectObject (hBmpDC, hBmp);
	BitBlt (hDC, 0, 0, bmpWidth, bmpHeight, hBmpDC, 0, 0, SRCCOPY);

	// free GDI Resources
	DeleteDC (hBmpDC);
	ReleaseDC (SplashWindow, hDC);
	DeleteObject ((HGDIOBJ) hBmp);

	// sleep for a bit to let the splash show
	Sleep (500);
}


void Splash_Destroy (void)
{
	DestroyWindow (SplashWindow);
	UnregisterClass (SPLASH_CLASS_NAME, SplashInstance);
}



