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

// hungarian notation must burn in HELL!
#pragma once

class CGdiPlusBitmap
{
public:
	Gdiplus::Bitmap *TheBitmap;

public:
	CGdiPlusBitmap (void)
	{
		this->TheBitmap = NULL;
	}

	CGdiPlusBitmap (LPCWSTR TheFile)
	{
		this->TheBitmap = NULL;
		this->Load (TheFile);
	}

	virtual ~CGdiPlusBitmap (void)
	{
		this->Empty ();
	}

	void Empty (void)
	{
		delete this->TheBitmap;
		this->TheBitmap = NULL;
	}

	bool Load (LPCWSTR TheFile)
	{
		this->Empty ();
		this->TheBitmap = Gdiplus::Bitmap::FromFile (TheFile);
		return this->TheBitmap->GetLastStatus () == Gdiplus::Ok;
	}

	operator Gdiplus::Bitmap *() const
	{
		return this->TheBitmap;
	}
};


class CGdiPlusBitmapResource : public CGdiPlusBitmap
{
protected:
	HGLOBAL TheBuffer;

public:
	CGdiPlusBitmapResource (void)
	{
		this->TheBuffer = NULL;
	}

	CGdiPlusBitmapResource (LPCTSTR Name, LPCTSTR Type = RT_RCDATA, HMODULE hInst = NULL)
	{
		this->TheBuffer = NULL;
		this->Load (Name, Type, hInst);
	}

	CGdiPlusBitmapResource (UINT id, LPCTSTR Type = RT_RCDATA, HMODULE hInst = NULL)
	{
		this->TheBuffer = NULL;
		this->Load (id, Type, hInst);
	}

	CGdiPlusBitmapResource (UINT id, UINT type, HMODULE hInst = NULL)
	{
		this->TheBuffer = NULL;
		this->Load (id, type, hInst);
	}

	virtual ~CGdiPlusBitmapResource (void)
	{
		this->Empty ();
	}

	void Empty (void);

	bool Load (LPCTSTR Name, LPCTSTR Type = RT_RCDATA, HMODULE hInst = NULL);

	bool Load (UINT id, LPCTSTR Type = RT_RCDATA, HMODULE hInst = NULL)
	{
		return this->Load (MAKEINTRESOURCE (id), Type, hInst);
	}

	bool Load (UINT id, UINT type, HMODULE hInst = NULL)
	{
		return this->Load (MAKEINTRESOURCE (id), MAKEINTRESOURCE (type), hInst);
	}
};


inline void CGdiPlusBitmapResource::Empty (void)
{
	CGdiPlusBitmap::Empty ();

	if (this->TheBuffer)
	{
		GlobalUnlock (this->TheBuffer);
		GlobalFree (this->TheBuffer);
		this->TheBuffer = NULL;
	} 
}

inline bool CGdiPlusBitmapResource::Load (LPCTSTR Name, LPCTSTR Type, HMODULE hInst)
{
	this->Empty ();

	HRSRC hResource = FindResource (hInst, Name, Type);

	if (!hResource) return false;

	DWORD imageSize = SizeofResource (hInst, hResource);

	if (!imageSize) return false;

	const void *ResourceData = LockResource (LoadResource (hInst, hResource));

	if (!ResourceData) return false;

	this->TheBuffer = GlobalAlloc (GMEM_MOVEABLE, imageSize);

	if (this->TheBuffer)
	{
		void *TempBuffer = GlobalLock (this->TheBuffer);

		if (TempBuffer)
		{
			CopyMemory (TempBuffer, ResourceData, imageSize);

			IStream *TheStream = NULL;

			if (CreateStreamOnHGlobal (this->TheBuffer, FALSE, &TheStream) == S_OK)
			{
				this->TheBitmap = Gdiplus::Bitmap::FromStream (TheStream);
				TheStream->Release ();

				if (this->TheBitmap)
				{ 
					if (this->TheBitmap->GetLastStatus () == Gdiplus::Ok)
						return true;

					delete this->TheBitmap;
					this->TheBitmap = NULL;
				}
			}

			GlobalUnlock (this->TheBuffer);
		}

		GlobalFree (this->TheBuffer);
		this->TheBuffer = NULL;
	}

	return false;
}
