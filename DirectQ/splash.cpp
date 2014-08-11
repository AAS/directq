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

#include "quakedef.h"
#include "resource.h"

#include <gdiplus.h>
#pragma comment (lib, "gdiplus.lib")
#include "CGdiPlusBitmap.h"

HWND SplashWindow;

#define SPLASH_CLASS_NAME "Splash Screen for DirectQ"

Gdiplus::GdiplusStartupInput gdiplusStartupInput;
ULONG_PTR gdiplusToken;
CGdiPlusBitmapResource *SplashBMP = NULL;

LRESULT CALLBACK SplashProc (HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	HDC          hdc;
	PAINTSTRUCT  ps;

	switch (message)
	{
	case WM_CREATE:
		{
			// Initialize GDI+.
			Gdiplus::GdiplusStartup (&gdiplusToken, &gdiplusStartupInput, NULL);

			// load our bitmap; basic GDI+ doesn't provide support for loading PNGs from resources so
			// we use the code found at http://www.codeproject.com/KB/GDI-plus/cgdiplusbitmap.aspx
			SplashBMP = new CGdiPlusBitmapResource ();
			SplashBMP->Load (IDR_SPLASH, RT_RCDATA, GetModuleHandle (NULL));

			int BMPWidth = SplashBMP->TheBitmap->GetWidth ();
			int BMPHeight = SplashBMP->TheBitmap->GetHeight ();

			// get the desktop resolution
			HDC DesktopDC = GetDC (NULL);
			int DeskWidth = GetDeviceCaps (DesktopDC, HORZRES);
			int DeskHeight = GetDeviceCaps (DesktopDC, VERTRES);
			ReleaseDC (NULL, DesktopDC);

			// center and show the splash window (resizing it to the bitmap size as we go)
			SetWindowPos (hWnd, NULL, (DeskWidth - BMPWidth) / 2, (DeskHeight - BMPHeight) / 2, BMPWidth, BMPHeight, SWP_SHOWWINDOW);
		}

		return 0;

	case WM_PAINT:
		if (SplashBMP)
		{
			hdc = BeginPaint (hWnd, &ps);
			Gdiplus::Graphics g (hdc);

			int w = SplashBMP->TheBitmap->GetWidth ();
			int h = SplashBMP->TheBitmap->GetHeight ();

			g.DrawImage (SplashBMP->TheBitmap, 0, 0, w, h);

			// this pretty much replicates the functionality of my splash screen maker app but brings it back into the engine
			// unicode sucks cocks in hell, by the way.
			Gdiplus::FontFamily   fontFamily (L"Arial");
			Gdiplus::Font         font (&fontFamily, 10, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
			Gdiplus::RectF        rectF (0, (h / 2) + 10, w, h);
			Gdiplus::StringFormat stringFormat;
			Gdiplus::SolidBrush   solidBrush (Gdiplus::Color (255, 192, 192, 192));

			stringFormat.SetAlignment (Gdiplus::StringAlignmentCenter);
			stringFormat.SetLineAlignment (Gdiplus::StringAlignmentNear);

			g.SetTextRenderingHint (Gdiplus::TextRenderingHintAntiAliasGridFit);
			g.DrawString (QASCIIToUnicode ("Release "DIRECTQ_VERSION), -1, &font, rectF, &stringFormat, &solidBrush);

			g.Flush (Gdiplus::FlushIntentionFlush);
			g.ReleaseHDC (hdc);
			EndPaint (hWnd, &ps);
		}

		// sleep a little to give the splash a chance to show
		Sleep (500);
		return 0;

	case WM_DESTROY:
		if (SplashBMP) delete SplashBMP;
		SplashBMP = NULL;
		Gdiplus::GdiplusShutdown (gdiplusToken);
		return 0;

	default:
		return DefWindowProc (hWnd, message, wParam, lParam);
	}
}


bool splashon = false;

void Splash_Init (void)
{
	// prevent the splash from being shown twice
	if (splashon) return;

	WNDCLASSEX wc;

	wc.cbClsExtra = 0;
	wc.cbSize = sizeof (WNDCLASSEX);
	wc.cbWndExtra = 0;
	wc.hbrBackground = (HBRUSH) GetStockObject (BLACK_BRUSH);
	wc.hCursor = LoadCursor (NULL, IDC_ARROW);
	wc.hIcon = LoadIcon (GetModuleHandle (NULL), MAKEINTRESOURCE (IDI_APPICON));
	wc.hIconSm = LoadIcon (GetModuleHandle (NULL), MAKEINTRESOURCE (IDI_APPICON));
	wc.hInstance = GetModuleHandle (NULL);
	wc.lpfnWndProc = (WNDPROC) SplashProc;
	wc.lpszClassName = SPLASH_CLASS_NAME;
	wc.lpszMenuName = NULL;
	wc.style = 0;

	// register the class
	RegisterClassEx (&wc);

	SplashWindow = CreateWindowEx
	(
		WS_EX_TOPMOST,
		SPLASH_CLASS_NAME,
		"Starting DirectQ...",
		WS_POPUP,
		0,
		0,
		10,
		10,
		GetDesktopWindow (),
		NULL,
		GetModuleHandle (NULL),
		NULL
	);

	// show and update the window
	ShowWindow (SplashWindow, SW_SHOWDEFAULT);
	UpdateWindow (SplashWindow);

	// give the window the focus
	SetForegroundWindow (SplashWindow);

	// prevent the splash from being shown twice
	splashon = true;
}


void Splash_Destroy (void)
{
	if (!splashon) return;

	DestroyWindow (SplashWindow);
	UnregisterClass (SPLASH_CLASS_NAME, GetModuleHandle (NULL));

	splashon = false;
}


