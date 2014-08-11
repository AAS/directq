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
#include "menu_common.h"


// our current menu
CQMenu *menu_Current = NULL;

// sounds to play in menus
menu_soundlevel_t menu_soundlevel = m_sound_none;

// current menu state (largely unused now)
m_state_t m_state;

// EnterMenu wrappers for old functions; required for command support
// and for any other external calls
void M_Menu_Main_f (void) {menu_Main.EnterMenu ();}
void M_Menu_SinglePlayer_f (void) {menu_Singleplayer.EnterMenu ();}
void M_Menu_Load_f (void) {menu_Load.EnterMenu ();}
void M_Menu_Save_f (void) {menu_Save.EnterMenu ();}
void M_Menu_Options_f (void) {menu_Options.EnterMenu ();}
void M_Menu_Keys_f (void) {menu_Keybindings.EnterMenu ();}
void M_Menu_Video_f (void) {menu_Video.EnterMenu ();}
void M_Menu_Help_f (void) {menu_Help.EnterMenu ();}
void M_Menu_MultiPlayer_f (void) {menu_Multiplayer.EnterMenu ();}
void M_Menu_Setup_f (void) {menu_Setup.EnterMenu ();}

cvar_t menu_fillcolor ("menu_fillcolor", 4, CVAR_ARCHIVE);

bool IsFileNameChar (char c)
{
	// true if the char is valid for a file name (excluding alpha numeric)
	// standard windows disallowed characters
	if (c == '\\') return false;
	if (c == '/') return false;
	if (c == ':') return false;
	if (c == '*') return false;
	if (c == '?') return false;
	if (c == '"') return false;
	if (c == '<') return false;
	if (c == '>') return false;
	if (c == '|') return false;

	// these are ones we want to disallow in the engine for various reasons
	// (keeping code simpler, prevention of relative paths, etc etc etc)
	if (c == ' ') return false;
	if (c == '.') return false;

	return true;
}


/*
=====================================================================================================================================

				MENU DRAWING ROUTINES

		Text is centered horizontally but not vertically

=====================================================================================================================================
*/


void Menu_DrawCharacter (int cx, int line, int num)
{
	Draw_Character (cx + ((vid.width - 320) >> 1), line, num);
}


void Menu_DrawBackwardsCharacter (int cx, int line, int num)
{
	Draw_BackwardsCharacter (cx + ((vid.width - 320) >> 1), line, num);
}


void Menu_Print (int cx, int cy, char *str)
{
	while (*str)
	{
		Menu_DrawCharacter (cx, cy, (*str)+128);
		str++;
		cx += 8;
	}
}


void Menu_PrintWhite (int cx, int cy, char *str)
{
	while (*str)
	{
		Menu_DrawCharacter (cx, cy, *str);
		str++;
		cx += 8;
	}
}


void Menu_PrintCenter (int cx, int cy, char *str)
{
	cx -= strlen (str) * 4;

	while (*str)
	{
		Menu_DrawCharacter (cx, cy, (*str) + 128);
		str++;
		cx += 8;
	}
}


void Menu_PrintCenter (int cy, char *str)
{
	Menu_PrintCenter (160, cy, str);
}


void Menu_PrintCenterWhite (int cx, int cy, char *str)
{
	cx -= strlen (str) * 4;

	while (*str)
	{
		Menu_DrawCharacter (cx, cy, *str);
		str++;
		cx += 8;
	}
}


void Menu_PrintCenterWhite (int cy, char *str)
{
	Menu_PrintCenterWhite (160, cy, str);
}


/*
=====================================================================================================================================

				CONSOLE TEXT BUFFER

		Because console output can happen while in the menus we replicate the last line of console text at the bottom of the screen

=====================================================================================================================================
*/

char menu_ConBuffer[1024];
float menu_ConBufferTime = 0;
float old_ConBufferTime = 0;

void Menu_PutConsolePrintInbuffer (char *text)
{
	extern cvar_t scr_centertime;

	// copy to console buffer
	strncpy (menu_ConBuffer, text, 1023);

	// display time
	old_ConBufferTime = realtime;

	// give it an extra few seconds so the user has a chance to read it
	menu_ConBufferTime = realtime + 3 + scr_centertime.value;
}


bool GetToggleState (float tmin, float tmax, float tvalue)
{
	float deltamin = fabs (tvalue - tmin);
	float deltamax = fabs (tvalue - tmax);

	return (deltamax < deltamin);
}


/*
=====================================================================================================================================

				SPIN CONTROL

		Specify NULL for zerotext and units for them to not be displayed if you don't want them.

		If you don't want editing of cvars in-place, make like a textbox and set up a dummy one.

		Two types - one based on a cvar and the other based on a string buffer.  See the sample implementations in the
		multiplayer menu.  The stringbuf should always be NULL terminated.  Look at "SkillNames" in menu_Options.cpp

		A char *** version is also available for controls who's contents need to change after the menus are initialized.
		See skyboxes in the warp menu for an example.  Generally you shouldn't need to do this...

		Put NULL in command text to center the control options (you're responsible for adding any command text you may
		want yourself...!)

		More logically this should have been 2 separate classes.  Oh well...

=====================================================================================================================================
*/

CQMenuSpinControl::CQMenuSpinControl (char *commandtext, int *menuval, char ***stringbuf)
{
	if (commandtext)
		strcpy (this->CommandText, commandtext);
	else this->CommandText[0] = 0;

	// this->StringBuf will be set to this->StringBufPtr[0] at runtime each time it's encountered
	// this is to prevent creating a mess of triple indirections.
	this->StringBuf = NULL;
	this->StringBufPtr = stringbuf;
	this->MenuVal = menuval;

	// ensure that we're good to begin with
	this->OutputText[0] = 0;

	// value for alternate type
	this->MenuCvar = NULL;

	this->AcceptsInput = true;
}


CQMenuSpinControl::CQMenuSpinControl (char *commandtext, int *menuval, char **stringbuf)
{
	if (commandtext)
		strcpy (this->CommandText, commandtext);
	else this->CommandText[0] = 0;

	this->StringBuf = stringbuf;
	this->MenuVal = menuval;
	this->StringBufPtr = NULL;

	// ensure that we're good to begin with
	this->OutputText[0] = 0;

	// value for alternate type
	this->MenuCvar = NULL;

	this->AcceptsInput = true;
}


CQMenuSpinControl::CQMenuSpinControl (char *commandtext, cvar_t *menucvar, float minval, float maxval, float increment, char *zerotext, char *units)
{
	if (commandtext)
		strcpy (this->CommandText, commandtext);
	else this->CommandText[0] = 0;

	this->MenuCvar = menucvar;
	this->MinVal = minval;
	this->MaxVal = maxval;
	this->Increment = increment;

	if (zerotext)
		strcpy (this->ZeroText, zerotext);
	else this->ZeroText[0] = 0;

	if (units)
		strcpy (this->Units, units);
	else this->Units[0] = 0;

	// ensure that we're good to begin with
	this->OutputText[0] = 0;

	// values for alternate type
	this->MenuVal = NULL;
	this->StringBuf = NULL;
	this->StringBufPtr = NULL;

	this->AcceptsInput = true;
}


void CQMenuSpinControl::DrawCurrentOptionHighlight (int y)
{
	// this is a temporary hack until we fix a global width for a menu
	Menu_HighlightBar (-175, y, 350);

	if (this->StringBufPtr) this->StringBuf = this->StringBufPtr[0];

	bool drawleft = false;
	bool drawright = false;

	if (this->MenuCvar)
	{
		if (this->MenuCvar->value > this->MinVal) drawleft = true;
		if (this->MenuCvar->value < this->MaxVal) drawright = true;
	}
	else if (this->StringBuf)
	{
		if (*(this->MenuVal) > 0) drawleft = true;
		if (this->StringBuf[*(this->MenuVal) + 1]) drawright = true;
	}

	int lpos = 160;

	if (!this->CommandText[0])
	{
		// adjust lpos
		lpos = 148 - strlen (this->OutputText) * 4;
	}

	int rpos = lpos + 16 + strlen (this->OutputText) * 8;

	if (drawleft) Menu_DrawBackwardsCharacter (lpos, y, 12 + ((int) (realtime * 2) & 1));
	if (drawright) Menu_DrawCharacter (rpos, y, 12 + ((int) (realtime * 2) & 1));
}


void CQMenuSpinControl::Draw (int y)
{
	if (this->StringBufPtr) this->StringBuf = this->StringBufPtr[0];

	// text
	if (this->CommandText[0])
		Menu_Print (148 - strlen (this->CommandText) * 8, y, this->CommandText);

	if (this->MenuCvar)
	{
		// build the output text
		if (!this->MenuCvar->value && this->ZeroText[0])
			strcpy (this->OutputText, this->ZeroText);
		else
		{
			sprintf (this->OutputText, "%g", this->MenuCvar->value);

			if (this->Units[0] != 0)
			{
				strcat (this->OutputText, " ");
				strcat (this->OutputText, this->Units);
			}
		}
	}
	else if (this->StringBuf)
	{
		strcpy (this->OutputText, this->StringBuf[*(this->MenuVal)]);
	}

	if (!this->CommandText[0])
		Menu_PrintCenterWhite (y, this->OutputText);
	else Menu_PrintWhite (172, y, this->OutputText);
}


void CQMenuSpinControl::Key (int k)
{
	if (this->StringBufPtr) this->StringBuf = this->StringBufPtr[0];

	if (this->MenuCvar)
	{
		switch (k)
		{
		case K_LEFTARROW:
			menu_soundlevel = m_sound_option;

			this->MenuCvar->value -= this->Increment;

			if (this->MenuCvar->value < this->MinVal)
			{
				this->MenuCvar->value = this->MinVal;
				menu_soundlevel = m_sound_deny;
			}

			break;

		case K_RIGHTARROW:
			menu_soundlevel = m_sound_option;

			this->MenuCvar->value += this->Increment;

			if (this->MenuCvar->value > this->MaxVal)
			{
				this->MenuCvar->value = this->MaxVal;
				menu_soundlevel = m_sound_deny;
			}

			break;
		}

		// update the cvar
		Cvar_Set (this->MenuCvar, this->MenuCvar->value);
	}
	else if (this->StringBuf)
	{
		switch (k)
		{
		case K_LEFTARROW:
			menu_soundlevel = m_sound_option;

			(*(this->MenuVal))--;

			if (*(this->MenuVal) < 0)
			{
				*(this->MenuVal) = 0;
				menu_soundlevel = m_sound_deny;
			}

			break;

		case K_RIGHTARROW:
			menu_soundlevel = m_sound_option;

			(*(this->MenuVal))++;

			if (!this->StringBuf[*(this->MenuVal)])
			{
				(*(this->MenuVal))--;
				menu_soundlevel = m_sound_deny;
			}

			break;
		}
	}
}


/*
=====================================================================================================================================

				COLOUR BAR

	Because the colour can be a derived value, it takes a pointer rather than a cvar/etc so that it can modify the base value
	directly.  Be certain to either have a base value that you are happy to have modified directly or to set this up correctly!

	(See multiplayer setup menu for an example)

=====================================================================================================================================
*/

CQMenuColourBar::CQMenuColourBar (char *cmdtext, int *colour)
{
	strcpy (this->CommandText, cmdtext);
	this->Colour = colour;
	this->Initial = *(this->Colour);
	this->AcceptsInput = true;
}


void CQMenuColourBar::PerformEntryFunction (void)
{
	this->Initial = *(this->Colour);
}


void CQMenuColourBar::Draw (int y)
{
	// text
	Menu_Print (148 - strlen (this->CommandText) * 8, y, this->CommandText);

	int colour = *(this->Colour);
	int intense = colour * 16 + (colour < 8 ? 11 : 4);

	// colour bar
	for (int i = 0; i < 14; i++)
	{
		// take the approximate midpoint colour (handle backward ranges)
		int c = i * 16 + (i < 8 ? 8 : 7);

		// braw baseline colour (offset downwards a little so that it fits correctly
		Draw_Fill (vid.width / 2 + 12 + i * 8, y + 1, 8, 8, c, 255);
	}

	// draw the highlight rectangle
	Draw_Fill (vid.width / 2 + 11 + colour * 8, y, 10, 10, 15, 255);

	// redraw the highlighted color at brighter intensity (handle backward ranges)
	Draw_Fill (vid.width / 2 + 12 + colour * 8, y + 1, 8, 8, intense, 255);
}


void CQMenuColourBar::Key (int k)
{
	switch (k)
	{
	case K_LEFTARROW:
		menu_soundlevel = m_sound_option;

		(*(this->Colour))--;

		if (*(this->Colour) < 0)
		{
			*(this->Colour) = 0;
			menu_soundlevel = m_sound_deny;
		}

		break;

	case K_RIGHTARROW:
		menu_soundlevel = m_sound_option;

		(*(this->Colour))++;

		if (*(this->Colour) > 13)
		{
			*(this->Colour) = 13;
			menu_soundlevel = m_sound_deny;
		}

		break;
	}
}


/*
=====================================================================================================================================

				CVAR TEXTBOX

		This got real ugly real quick last time I tried to do it.  Let's hope I get it better this time...

		This can either access a string cvar directly (unrecommended) or you can declare but not register a "dummy" version of the
		cvar, create the textbox using that, and set up custom enter and custom key functions to handle changes, or you can do an
		apply menu that also handles the changes... your choice

=====================================================================================================================================
*/

CQMenuCvarTextbox::CQMenuCvarTextbox (char *commandtext, cvar_t *menucvar, int flags)
{
	strncpy (this->CommandText, commandtext, 127);

	this->MenuCvar = menucvar;
	this->AcceptsInput = (flags & TBFLAG_READONLY) ? false : true;
	this->InsertMode = true;
	this->IsCurrent = false;
	this->Flags = flags;
}


void CQMenuCvarTextbox::DrawCurrentOptionHighlight (int y)
{
	// mark as current
	this->IsCurrent = true;

	// highlight bar
	Menu_HighlightBar (-175, y, 350, 14);
}


#define MAX_TBLENGTH	20
#define MAX_TBPOS		(MAX_TBLENGTH - 1)

void CQMenuCvarTextbox::Draw (int y)
{
	// give some space above as well as below
	y += 2;

	// text
	Menu_Print (148 - strlen (this->CommandText) * 8, y, this->CommandText);

	// textbox
	Draw_Fill (vid.width / 2 - 160 + 168, y - 1, MAX_TBLENGTH * 8 + 4, 10, 20, 255);
	Draw_Fill (vid.width / 2 - 160 + 169, y, MAX_TBLENGTH * 8 + 2, 8, 0, 192);

	// current text
	for (int i = 0; i < MAX_TBLENGTH; i++)
	{
		if (!this->MenuCvar->string[this->TextStart + i]) break;
		Menu_DrawCharacter (170 + i * 8, y, this->MenuCvar->string[this->TextStart + i]);
	}

	if (this->IsCurrent)
	{
		// edit cursor
		Menu_DrawCharacter (170 + 8 * this->TextPos, y, 10 + ((int) (realtime * 4) & 1));

		// insert mode indicator
		Menu_Print (340, y, this->InsertMode ? "[INS]" : "[OVR]");
	}

	// reset as it may change next frame
	this->IsCurrent = false;
}


void CQMenuCvarTextbox::Key (int k)
{
	// get the real position of the cursor in the string
	int RealTextPos = this->TextPos + this->TextStart;

	switch (k)
	{
	case K_INS:
		// toggle insert mode
		this->InsertMode = !this->InsertMode;
		break;

	case K_LEFTARROW:
		this->TextPos--;
		menu_soundlevel = m_sound_option;

		if (this->TextPos < 0)
		{
			this->TextPos = 0;
			this->TextStart--;
		}

		if (this->TextStart < 0)
		{
			this->TextStart = 0;
			menu_soundlevel = m_sound_deny;
		}
		break;

	case K_RIGHTARROW:
		this->TextPos++;
		menu_soundlevel = m_sound_option;

		if (this->TextPos > strlen (this->MenuCvar->string))
		{
			menu_soundlevel = m_sound_deny;
			this->TextPos = strlen (this->MenuCvar->string);
		}

		if (this->TextPos > MAX_TBPOS)
		{
			this->TextPos = MAX_TBPOS;
			this->TextStart++;
		}

		if (this->TextStart > strlen (this->MenuCvar->string) - MAX_TBPOS)
		{
			this->TextStart = strlen (this->MenuCvar->string) - MAX_TBPOS;
			menu_soundlevel = m_sound_deny;
		}
		break;

	case K_HOME:
		// return to start
		menu_soundlevel = m_sound_option;
		this->TextPos = 0;
		this->TextStart = 0;
		break;

	case K_END:
		// go to end
		menu_soundlevel = m_sound_option;
		this->TextPos = MAX_TBPOS;
		if (this->TextPos > strlen (this->MenuCvar->string)) this->TextPos = strlen (this->MenuCvar->string);

		this->TextStart = strlen (this->MenuCvar->string) - MAX_TBPOS;
		if (this->TextStart < 0) this->TextStart = 0;
		break;

	case K_DEL:
		// prevent deletion if at end of string
		if (RealTextPos >= strlen (this->MenuCvar->string))
		{
			menu_soundlevel = m_sound_deny;
			break;
		}

		// simulate the deletion by moving right then deleting before the cursor
		this->Key (K_RIGHTARROW);
		this->Key (K_BACKSPACE);
		menu_soundlevel = m_sound_option;

		break;

	case K_SHIFT:
		// do nothing
		// this is to prevent holding down shift to type a cap from sounding "deny"
		break;

	case K_BACKSPACE:
		// prevent deletion at start of string
		if (!RealTextPos)
		{
			menu_soundlevel = m_sound_deny;
			break;
		}

		// delete character before cursor
		menu_soundlevel = m_sound_option;
		strcpy (this->ScratchPad, &this->MenuCvar->string[RealTextPos]);
		strcpy (&this->MenuCvar->string[RealTextPos - 1], this->ScratchPad);

		// fix up positioning
		this->TextStart--;

		if (this->TextStart < 0)
		{
			this->TextStart = 0;
			this->TextPos--;
		}

		if (this->TextPos < 0) this->TextPos = 0;
		break;

	default:
		// non alphanumeric
		if (k < 32 || k > 127)
		{
			menu_soundlevel = m_sound_deny;
			break;
		}

		// conservative overflow prevent
		if (strlen (this->MenuCvar->string) > 1020)
		{
			menu_soundlevel = m_sound_deny;
			break;
		}

		bool validinput = false;

		if (!this->Flags)
		{
			// all input is valid
			validinput = true;
		}
		else
		{
			// check individual flags
			if ((this->Flags & TBFLAG_ALLOWNUMBERS) && (k >= '0' && k <= '9')) validinput = true;
			if ((this->Flags & TBFLAG_ALLOWLETTERS) && ((k >= 'a' && k <= 'z') || (k >= 'A' && k <= 'Z'))) validinput = true;
			if ((this->Flags & TBFLAG_FILENAMECHARS) && IsFileNameChar (k)) validinput = true;
			if ((this->Flags & TBFLAG_ALLOWSPACE) && k == 32) validinput = true;
			if ((this->Flags & TBFLAG_FOLDERPATH) && (k == '/' || k == '\\') && RealTextPos > 0) validinput = true;
		}

		if (!validinput)
		{
			menu_soundlevel = m_sound_deny;
			break;
		}

		menu_soundlevel = m_sound_option;

		if (this->InsertMode)
		{
			// insert mode
			strcpy (this->ScratchPad, &this->MenuCvar->string[RealTextPos]);
			strcpy (&this->MenuCvar->string[RealTextPos + 1], this->ScratchPad);
			this->MenuCvar->string[RealTextPos] = k;

			// move right
			this->Key (K_RIGHTARROW);
		}
		else
		{
			// overwrite mode
			this->MenuCvar->string[RealTextPos] = k;

			// move right
			this->Key (K_RIGHTARROW);
		}

		break;
	}
}


void CQMenuCvarTextbox::PerformEntryFunction (void)
{
	// copy cvar string to temp storage
	strcpy (this->InitialValue, this->MenuCvar->string);

	// set positions
	this->TextStart = 0;
	this->TextPos = strlen (this->MenuCvar->string);

	// bound textpos as the string may be longer than the max visible
	// this just runs a K_END on it to get things right
	this->Key (K_END);
}


int CQMenuCvarTextbox::GetYAdvance (void)
{
	return 20;
}


/*
=====================================================================================================================================

				SUBMENU

=====================================================================================================================================
*/

CQMenuSubMenu::CQMenuSubMenu (char *cmdtext, CQMenu *submenu)
{
	strncpy (this->CommandText, cmdtext, 127);

	this->SubMenu = submenu;
	this->AcceptsInput = true;
}


void CQMenuSubMenu::Draw (int y)
{
	Menu_PrintCenter (y, this->CommandText);
}


void CQMenuSubMenu::Key (int k)
{
	if (k == K_ENTER) this->SubMenu->EnterMenu ();
}


/*
=====================================================================================================================================

				COMMAND

=====================================================================================================================================
*/

CQMenuCommand::CQMenuCommand (char *cmdtext, menucommand_t command)
{
	strncpy (this->CommandText, cmdtext, 127);

	if (command)
		this->Command = command;
	else this->Command = Menu_NullCommand;

	this->AcceptsInput = true;
}


void CQMenuCommand::Draw (int y)
{
	Menu_PrintCenter (y, this->CommandText);
}


void CQMenuCommand::Key (int k)
{
	if (k == K_ENTER)
	{
		menu_soundlevel = m_sound_option;
		this->Command ();
	}
}


/*
=====================================================================================================================================

				SPACER

		Either a blank space or some text.

		Set to NULL for space.

=====================================================================================================================================
*/

CQMenuSpacer::CQMenuSpacer (char *spacertext)
{
	if (spacertext)
		strcpy (this->CommandText, spacertext);
	else this->CommandText[0] = 0;

	this->AcceptsInput = false;
}


void CQMenuSpacer::Draw (int y)
{
	if (this->CommandText[0])
	{
		Menu_PrintCenter (y, this->CommandText);
	}
}


int CQMenuSpacer::GetYAdvance ()
{
	if (this->CommandText[0])
		return 15;

	return 5;
}


/*
=====================================================================================================================================

				CUSTOM ENTER

		Allows menus to define their own enter functions outside of the common framework

=====================================================================================================================================
*/

CQMenuCustomEnter::CQMenuCustomEnter (menucommand_t enterfunc)
{
	this->EnterFunc = enterfunc;
	this->AcceptsInput = false;
}


void CQMenuCustomEnter::PerformEntryFunction (void)
{
	this->EnterFunc ();
}


int CQMenuCustomEnter::GetYAdvance (void)
{
	return 0;
}


/*
=====================================================================================================================================

				CUSTOM DRAW

		Allows menus to define their own rendering outside of the common framework

		Note: the custom draw func should return the amount by which to advance Y rather than Y itself, a bit confusing
		so I'll probably change it...

=====================================================================================================================================
*/

CQMenuCustomDraw::CQMenuCustomDraw (imenucommandi_t customdrawfunc)
{
	this->CustomDrawFunc = customdrawfunc;
	this->AcceptsInput = false;
}


void CQMenuCustomDraw::Draw (int y)
{
	this->Y = this->CustomDrawFunc (y) - y;
}


int CQMenuCustomDraw::GetYAdvance (void)
{
	return this->Y;
}


/*
=====================================================================================================================================

				CUSTOM KEY

		Allows menus to define their own key capture events outside of the common framework

=====================================================================================================================================
*/

CQMenuCustomKey::CQMenuCustomKey (int keycapture, menucommand_t capturefunc, bool fwoverride)
{
	// single key handler function
	this->KeyCapture = keycapture;
	this->CaptureFuncNoKeyArg = capturefunc;
	this->CaptureFuncKeyArg = NULL;
	this->AcceptsInput = false;

	// allow to override or combine with the standard key func
	this->OverRideFramework = fwoverride;
}


CQMenuCustomKey::CQMenuCustomKey (int keycapture, menucommandi_t capturefunc, bool fwoverride)
{
	// shared key handler function
	this->KeyCapture = keycapture;
	this->CaptureFuncKeyArg = capturefunc;
	this->CaptureFuncNoKeyArg = NULL;
	this->AcceptsInput = false;

	// allow to override or combine with the standard key func
	this->OverRideFramework = fwoverride;
}


bool CQMenuCustomKey::CheckStatus (void *stuff)
{
	// retrieve the key
	int k = *((int *) stuff);

	// execute the capture function
	if (k == this->KeyCapture)
	{
		// execute the custom key func and notify that caller that we got it
		// the key arg version allows a shared handler
		if (this->CaptureFuncKeyArg)
			this->CaptureFuncKeyArg (k);
		else this->CaptureFuncNoKeyArg ();

		// return
		return this->OverRideFramework;
	}

	// didn't get it
	return false;
}


int CQMenuCustomKey::GetYAdvance ()
{
	return 0;
}


/*
=====================================================================================================================================

				TITLE

=====================================================================================================================================
*/

CQMenuTitle::CQMenuTitle (char *title)
{
	strncpy (this->CommandText, title, 127);
	this->AcceptsInput = false;
}


void CQMenuTitle::Draw (int y)
{
	// everywhere we had a title we put a spacer before it, so let's get rid of the need for that
	Menu_PrintCenterWhite (y + 5, this->CommandText);
	Menu_PrintCenter (y + 20, DIVIDER_LINE);
}


int CQMenuTitle::GetYAdvance (void)
{
	return 35;
}


/*
=====================================================================================================================================

				BANNER

=====================================================================================================================================
*/

CQMenuBanner::CQMenuBanner (char *pic)
{
	this->Pic = (char *) Heap_QMalloc (strlen (pic) + 1);
	strcpy (this->Pic, pic);
	this->AcceptsInput = false;
}


void CQMenuBanner::Draw (int y)
{
	// we can't load this on init as the filesystem may not be up yet
	qpic_t *pic = Draw_CachePic (this->Pic);

	// draw centered on screen and offset down
	Draw_Pic ((vid.width - pic->width) >> 1, y, pic);
	this->Y = pic->height + 10;
}


int CQMenuBanner::GetYAdvance (void)
{
	return this->Y;
}


/*
=====================================================================================================================================

				CVAR SLIDER

=====================================================================================================================================
*/

CQMenuCvarSlider::CQMenuCvarSlider (char *commandtext, cvar_t *menucvar, float minval, float maxval, float stepsize)
{
	strncpy (this->CommandText, commandtext, 127);

	this->AcceptsInput = true;

	this->MenuCvar = menucvar;
	this->SliderMin = minval;
	this->SliderMax = maxval;
	this->StepSize = stepsize;

	// check for inverse range
	if (this->SliderMin > this->SliderMax) this->StepSize *= -1;
}


void CQMenuCvarSlider::Draw (int y)
{
	// text
	Menu_Print (148 - strlen (this->CommandText) * 8, y, this->CommandText);

	// slider left
	Menu_DrawCharacter (168, y + 1, 128);

	// slider body
	for (int i = 0; i < 10; i++)
		Menu_DrawCharacter (176 + i * 8, y + 1, 129);

	// slider right
	Menu_DrawCharacter (256, y + 1, 130);

	// evaluate point on the range
	float point = (this->MenuCvar->value - this->SliderMin) / (this->SliderMax - this->SliderMin);

	// don't go beyond the bounds
	if (point < 0) point = 0;
	if (point > 1) point = 1;

	// draw indicator
	Menu_DrawCharacter (172 + point * 80, y + 1, 131);
}


void CQMenuCvarSlider::Key (int k)
{
	// store value out
	float val = this->MenuCvar->value;

	switch (k)
	{
	case K_LEFTARROW:
		val -= this->StepSize;
		menu_soundlevel = m_sound_option;

		if (val < this->SliderMin && this->SliderMin < this->SliderMax)
		{
			// normal range
			menu_soundlevel = m_sound_deny;
			val = this->SliderMin;
		}
		else if (val > this->SliderMin && this->SliderMin > this->SliderMax)
		{
			// normal range
			menu_soundlevel = m_sound_deny;
			val = this->SliderMin;
		}

		Cvar_Set (this->MenuCvar, val);
		break;

	case K_ENTER:
	case K_RIGHTARROW:
		val += this->StepSize;
		menu_soundlevel = m_sound_option;

		if (val > this->SliderMax && this->SliderMin < this->SliderMax)
		{
			// normal range
			menu_soundlevel = m_sound_deny;
			val = this->SliderMax;
		}
		else if (val < this->SliderMax && this->SliderMin > this->SliderMax)
		{
			// inverse range
			menu_soundlevel = m_sound_deny;
			val = this->SliderMax;
		}

		Cvar_Set (this->MenuCvar, val);
		break;

	default:
		break;
	}
}


CQMenuCvarExpSlider::CQMenuCvarExpSlider (char *commandtext, cvar_t *menucvar, int initial, int exponent, int numsteps, bool invert)
{
	int i;

	strncpy (this->CommandText, commandtext, 127);

	this->AcceptsInput = true;

	this->MenuCvar = menucvar;
	this->Initial = initial;
	this->Exponent = exponent;
	this->NumSteps = numsteps;
	this->Invert = invert;

	// figure out the max val
	for (i = 1, this->MaxVal = this->Initial; i < this->NumSteps; this->MaxVal *= this->Exponent, i++);
}


void CQMenuCvarExpSlider::Draw (int y)
{
	// text
	Menu_Print (148 - strlen (this->CommandText) * 8, y, this->CommandText);

	// slider left
	Menu_DrawCharacter (168, y + 1, 128);

	// slider body
	for (int i = 0; i < 10; i++)
		Menu_DrawCharacter (176 + i * 8, y + 1, 129);

	// slider right
	Menu_DrawCharacter (256, y + 1, 130);

	int realvalue, step;

	// figure the real value of the cvar (and the step - note - because we ++ it we need to -- it when done)
	for (step = 0, realvalue = 1; realvalue < this->MenuCvar->value; realvalue *= this->Exponent, step++);

	// -- it to compensate for loop overshoot
	if (--step < 0) step = 0;

	// in case some clever clogs sets an invalid value in the console
	// (note - check all these cvars every frame to ensure this doesn't happen)
	if (realvalue > this->MaxVal) realvalue = this->MaxVal;

	// lock the cvar into the scale
	Cvar_Set (this->MenuCvar, realvalue);

	// we have enough info now to draw the indicator
	float point = (float) step / (float) (this->NumSteps - 1);

	// don't go beyond the bounds
	if (point < 0) point = 0;
	if (point > 1) point = 1;

	// check for inverse scale
	if (this->Invert) point = 1.0f - point;

	// draw indicator
	Menu_DrawCharacter (172 + point * 80, y + 1, 131);

	/*
	Menu_Print (-100, 380, va ("Initial:    %i", this->Initial));
	Menu_Print (-100, 390, va ("Max value:  %i", this->MaxVal));
	Menu_Print (-100, 400, va ("Real value: %i", realvalue));
	Menu_Print (-100, 410, va ("Step:       %i", step));
	*/
}


void CQMenuCvarExpSlider::Key (int k)
{
	// the cvar will already have been converted to the proper step scale by the draw func
	// so here we just step it
	switch (k)
	{
	case K_LEFTARROW:
		menu_soundlevel = m_sound_option;

		if (this->Invert)
			this->MenuCvar->value *= this->Exponent;
		else this->MenuCvar->value /= this->Exponent;

		break;

	case K_RIGHTARROW:
		menu_soundlevel = m_sound_option;

		if (this->Invert)
			this->MenuCvar->value /= this->Exponent;
		else this->MenuCvar->value *= this->Exponent;

		break;
	}

	// "initial" is a bit of a misnomer here; it's really a minimum value
	if (this->MenuCvar->value < this->Initial)
	{
		menu_soundlevel = m_sound_deny;
		this->MenuCvar->value = this->Initial;
	}

	if (this->MenuCvar->value > this->MaxVal)
	{
		menu_soundlevel = m_sound_deny;
		this->MenuCvar->value = this->MaxVal;
	}

	Cvar_Set (this->MenuCvar, this->MenuCvar->value);
}


/*
=====================================================================================================================================

				CVAR ON/OFF TOGGLE

=====================================================================================================================================
*/

CQMenuCvarToggle::CQMenuCvarToggle (char *commandtext, cvar_t *menucvar, float toggleoffvalue, float toggleonvalue)
{
	strncpy (this->CommandText, commandtext, 127);

	this->AcceptsInput = true;
	this->MenuCvar = menucvar;
	this->ToggleOffValue = toggleoffvalue;
	this->ToggleOnValue = toggleonvalue;
}


void CQMenuCvarToggle::DrawCurrentOptionHighlight (int y)
{
	Menu_HighlightBar (y);

	// this is a hack to get the l/r indicators working correctly here
	Menu_DrawOption (172, y, this->ToggleState ? "  " : "   ", true, true);
}


void CQMenuCvarToggle::Draw (int y)
{
	Menu_Print (148 - strlen (this->CommandText) * 8, y, this->CommandText);

	// update togglestate otherwise it will only reflect the initial cvar value
	// set togglestate depending on which of on/off the value is closest to
	this->ToggleState = GetToggleState (this->ToggleOffValue, this->ToggleOnValue, this->MenuCvar->value);

	// this is a hack to get the l/r indicators working correctly here
	Menu_DrawOption (172, y, this->ToggleState ? "On" : "Off", false, false);
}


void CQMenuCvarToggle::Key (int k)
{
	switch (k)
	{
	case K_LEFTARROW:
	case K_RIGHTARROW:
	case K_ENTER:
		// flip the toggle state
		this->ToggleState = !this->ToggleState;
		menu_soundlevel = m_sound_option;

		// set the cvar
		Cvar_Set (this->MenuCvar, this->ToggleState ? this->ToggleOnValue : this->ToggleOffValue);
		break;

	default:
		break;
	}
}


/*
=====================================================================================================================================

				EMPTY OPTION

=====================================================================================================================================
*/

// these are called if none of the virtual methods are overloaded
// standard y advance
int CQMenuOption::GetYAdvance (void) {return 15;}

// draw nothing
void CQMenuOption::Draw (int y) {}

// no key events
void CQMenuOption::Key (int k) {}

// whether it can accept input or not
bool CQMenuOption::CanAcceptInput (void)
{
	// never accepts input
	if (!this->Enabled) return false;
	if (!this->Visible) return false;

	// depends on the option type
	return this->AcceptsInput;
}

bool CQMenuOption::IsEnabled (void) {return this->Enabled;}
bool CQMenuOption::IsVisible (void) {return this->Visible;}

// used for checking status of something
bool CQMenuOption::CheckStatus (void *stuff) {return false;}

// option number - never overridden
void CQMenuOption::SetOptionNumber (int optionnum) {this->OptionNum = optionnum;}
int CQMenuOption::GetOptionNumber (void) {return this->OptionNum;}

// this one lets each type of option perform a custom function when the menu is entered
// it might be it's own internal validation of cvar constraints or something entirely different
void CQMenuOption::PerformEntryFunction (void) {}

// this one lets options call public members from their parent menu
void CQMenuOption::SetParentMenu (CQMenu *parent) {this->Parent = parent;}

// this one lets options position their own highlight bars in cases where they draw anything nonstandard
void CQMenuOption::DrawCurrentOptionHighlight (int y) {Menu_HighlightBar (y);}

// sets a tag number on the option
void CQMenuOption::SetTag (int Tag) {this->OptionTag = Tag;}

// enable/disable on a tag match
void CQMenuOption::CheckEnable (int Tag, bool enable) {if (this->OptionTag == Tag) this->Enabled = enable;}

// show/hide on a tag match
void CQMenuOption::CheckVisible (int Tag, bool shown) {if (this->OptionTag == Tag) this->Visible = shown;}

/*
=====================================================================================================================================

				THE MENU ITSELF

=====================================================================================================================================
*/

CQMenu::CQMenu (CQMenu *previous, m_state_t menustate)
{
	// defaults
	this->PreviousMenu = previous;
	this->MenuOptions = NULL;
	this->CurrentOption = NULL;
	this->NumOptions = 0;
	this->MenuState = menustate;
	this->InsertedItems = NULL;
	this->InsertPosition = NULL;
	this->AfterInsert = NULL;
}


void CQMenu::EnableOptions (int Tag)
{
	for (CQMenuOption *opt = this->MenuOptions; opt; opt = opt->NextOption)
	{
		opt->CheckEnable (Tag, true);
	}
}


void CQMenu::DisableOptions (int Tag)
{
	for (CQMenuOption *opt = this->MenuOptions; opt; opt = opt->NextOption)
	{
		opt->CheckEnable (Tag, false);
	}
}


void CQMenu::ShowOptions (int Tag)
{
	for (CQMenuOption *opt = this->MenuOptions; opt; opt = opt->NextOption)
	{
		opt->CheckVisible (Tag, true);
	}
}


void CQMenu::HideOptions (int Tag)
{
	for (CQMenuOption *opt = this->MenuOptions; opt; opt = opt->NextOption)
	{
		opt->CheckVisible (Tag, false);
	}
}


// option insertion - note - this is not as robust as it should be.  i haven't bothered to add validation for inserting
// at the start or the end of the list, so don't do it.
void CQMenu::InsertNewItems (CQMenuOption *optlist)
{
	// validate the positions
	if (!this->InsertPosition) return;
	if (!this->AfterInsert) return;
	if (this->InsertedItems) return;
	if (!optlist) return;

	// recalc the number of options
	this->NumOptions = 0;

	// link the new options in to the before position
	this->InsertPosition->NextOption = optlist;
	optlist->PrevOption = this->InsertPosition;

	// find the last item in the optlist
	for (CQMenuOption *opt = optlist; opt; opt = opt->NextOption)
	{
		// check it
		if (!opt->NextOption)
		{
			// got it
			opt->NextOption = this->AfterInsert;
			this->AfterInsert->PrevOption = opt;

			// done
			break;
		}
	}

	// also reset the option number for each option
	for (CQMenuOption *opt = this->MenuOptions; opt; opt = opt->NextOption)
		opt->SetOptionNumber (this->NumOptions++);

	// store the inserted items
	this->InsertedItems = optlist;
}


void CQMenu::RemoveLastInsert (void)
{
	// nothing to remove
	if (!this->InsertedItems) return;

	// validate the positions
	if (!this->InsertPosition) return;
	if (!this->AfterInsert) return;

	// terminate the removed list
	this->AfterInsert->PrevOption->NextOption = NULL;

	// relink the before and after positions
	this->InsertPosition->NextOption = this->AfterInsert;
	this->AfterInsert->PrevOption = this->InsertPosition;

	// recalc the number of options
	this->NumOptions = 0;

	// also reset the option number for each option
	for (CQMenuOption *opt = this->MenuOptions; opt; opt = opt->NextOption)
		opt->SetOptionNumber (this->NumOptions++);

	// clear the inserted items
	this->InsertedItems = NULL;
}


void CQMenu::AddOption (CQMenuOption *opt, bool setinsertposafter)
{
	this->AddOption (0, opt, setinsertposafter);
}


void CQMenu::AddOption (int OptionTag, CQMenuOption *opt, bool setinsertposafter)
{
	// next points to NULL always so that we can identify the end of the list
	opt->NextOption = NULL;

	// set option number and increment options counter
	opt->SetOptionNumber (this->NumOptions++);

	// set tag, set visible and enabled
	opt->SetTag (OptionTag);
	opt->CheckEnable (OptionTag, true);
	opt->CheckVisible (OptionTag, true);

	// set parent menu
	opt->SetParentMenu (this);

	// check insert positions - if we had an insert position just set, this option
	// is the after insert position
	if (this->InsertPosition && !this->AfterInsert)
		this->AfterInsert = opt;
	else if (!this->InsertPosition && setinsertposafter)
		this->InsertPosition = opt;

	// the first option that can accept input is the initial current option
	if (!this->CurrentOption && opt->CanAcceptInput ())
		this->CurrentOption = opt;

	if (!this->MenuOptions)
	{
		// set at first in the list
		this->MenuOptions = opt;

		// previous points to itself as there is only one item in the list
		opt->PrevOption = this->MenuOptions;
		return;
	}

	// find the end of the list
	for (CQMenuOption *opt2 = this->MenuOptions; opt2; opt2 = opt2->NextOption)
	{
		if (!opt2->NextOption)
		{
			// add to the end
			opt2->NextOption = opt;
			opt->PrevOption = opt2;

			// update first item previous pointer
			this->MenuOptions->PrevOption = opt;
			break;
		}
	}
}


void CQMenu::Draw (void)
{
	int y = 25;

	// no options
	if (!this->MenuOptions) return;

	// check for NULL current option and set it to the first option
	if (!this->CurrentOption) this->CurrentOption = this->MenuOptions;

	// ensure that current option can accept input
	if (!this->CurrentOption->CanAcceptInput ())
	{
		int scrollcount = 0;

		// move current option to one that can accept input, unless we can't find one, in which case it's
		// likely custom handled...
		while (1)
		{
			// go to the next option
			this->CurrentOption = this->CurrentOption->NextOption;

			// check for null and reset to start of list if so
			if (!this->CurrentOption) this->CurrentOption = this->MenuOptions;

			// break if it accepts input
			if (this->CurrentOption->CanAcceptInput ()) break;

			// check for a menu with no options that accept input
			if (++scrollcount > this->NumOptions) break;
		}
	}

	// draw all of the options
	for (CQMenuOption *opt = this->MenuOptions; opt; opt = opt->NextOption)
	{
		if (!opt->IsVisible ())
			continue;
		else if (!opt->IsEnabled ())
		{
			// fade out
			D3D_Set2DShade (0.666f);

			// never has a highlight bar
			opt->Draw (y);

			// back to normal
			D3D_Set2DShade (1.0f);
		}
		else if (opt->CanAcceptInput () && (opt->GetOptionNumber () == this->CurrentOption->GetOptionNumber ()))
		{
			// draw the highlight bar under it
			opt->DrawCurrentOptionHighlight (y);

			// the fact that it's current needs special handling in come draw funcs
			opt->Draw (y);
		}
		else opt->Draw (y);

		// get the next y position
		y += opt->GetYAdvance ();
	}

	// draw the console buffer
	if (menu_ConBufferTime > realtime)
	{
		menu_ConBufferTime -= (realtime - old_ConBufferTime);
		old_ConBufferTime = realtime;

		Menu_PrintCenter (vid.height - 32, menu_ConBuffer);
	}
}


void CQMenu::Key (int k)
{
	if (!this->MenuOptions)
	{
		switch (k)
		{
		case K_ESCAPE:
			// to do - back out to previous menu or quit menus if no previous defined
			if (this->PreviousMenu)
				this->PreviousMenu->EnterMenu ();
			else
			{
				// exit the menus
			}
			break;

		default:
			break;
		}

		return;
	}

	// look for any custom key functions that match this key and override the framework if so
	for (CQMenuOption *opt = this->MenuOptions; opt; opt = opt->NextOption)
		if (opt->CheckStatus (&k))
			return;

	// depending on how fast (or clumsy!) we are with the keyboard, this function can be triggered before
	// the draw function, so we need to ensure that we get a valid current option in here too...
	if (!this->CurrentOption) this->CurrentOption = this->MenuOptions;

	// count of menu scrolling
	int scrollcount = 0;

	// send key events through the framework
	switch (k)
	{
	case K_ESCAPE:
		// to do - back out to previous menu or quit menus if no previous defined
		if (this->PreviousMenu)
			this->PreviousMenu->EnterMenu ();
		else
		{
			// exit the menus
		}
		break;

	case K_UPARROW:
		while (1)
		{
			// go to the previous option
			this->CurrentOption = this->CurrentOption->PrevOption;

			// break if it accepts input
			if (this->CurrentOption->CanAcceptInput ()) break;

			// check for a menu with no options that accept input
			if (++scrollcount > this->NumOptions) break;
		}

		menu_soundlevel = m_sound_nav;
		break;

	case K_DOWNARROW:
		while (1)
		{
			// go to the next option
			this->CurrentOption = this->CurrentOption->NextOption;

			// check for null and reset to start of list if so
			if (!this->CurrentOption) this->CurrentOption = this->MenuOptions;

			// break if it accepts input
			if (this->CurrentOption->CanAcceptInput ()) break;

			// check for a menu with no options that accept input
			if (++scrollcount > this->NumOptions) break;
		}

		menu_soundlevel = m_sound_nav;
		break;

	default:
		// send it through the option key handler
		if (this->CurrentOption->CanAcceptInput ())
			this->CurrentOption->Key (k);

		break;
	}
}


void CQMenu::EnterMenu (void)
{
	key_dest = key_menu;
	m_state = this->MenuState;
	menu_soundlevel = m_sound_enter;

	// perform all entry functions for all options
	// these are left till after the common state as they may override elements of it
	for (CQMenuOption *opt = this->MenuOptions; opt; opt = opt->NextOption)
		opt->PerformEntryFunction ();

	// store to current pointer for easier access
	menu_Current = this;
}


/*
=====================================================================================================================================

				SCROLLBOX PROVIDER

=====================================================================================================================================
*/


CScrollBoxProvider::CScrollBoxProvider (int numitems, int maxvisibleitems, int maxitemwidth)
{
	this->NumItems = numitems;
	this->MaxVisibleItems = maxvisibleitems;
	this->ScrollBoxStartItem = 0;
	this->ScrollBoxCurrentItem = 0;

	// set event callbacks to nothing yet
	this->DrawItemCallback = NULL;
	this->HoverItemCallback = NULL;
	this->EnterItemCallback = NULL;
	this->MaxItemWidth = maxitemwidth;
}


void CScrollBoxProvider::SetDrawItemCallback (menucommandii_t drawitemcallback)
{
	this->DrawItemCallback = drawitemcallback;
}


void CScrollBoxProvider::SetHoverItemCallback (menucommandiii_t hoveritemcallback)
{
	this->HoverItemCallback = hoveritemcallback;
}


void CScrollBoxProvider::SetEnterItemCallback (menucommandi_t enteritemcallback)
{
	this->EnterItemCallback = enteritemcallback;
}


int CScrollBoxProvider::DrawItems (int x, int starty)
{
	if (!this->DrawItemCallback) return starty;

	int initialy = starty;

	// draw the textbox
	Draw_TextBox (x, starty, this->MaxItemWidth * 8 + 8, this->MaxVisibleItems * 12 + 4);
	Draw_VScrollBar (x + this->MaxItemWidth * 8 + 8, starty + 8, this->MaxVisibleItems * 12 + 4, this->ScrollBoxCurrentItem, this->NumItems);

	starty += 12;

	for (int i = this->ScrollBoxStartItem; i < this->NumItems; i++)
	{
		// check for end of list
		if (i - this->ScrollBoxStartItem >= this->MaxVisibleItems)
			break;

		// check for active item and run the hover callback if any
		if (i == this->ScrollBoxCurrentItem)
			if (this->HoverItemCallback)
				this->HoverItemCallback (initialy, starty, i);

		// draw the current item
		this->DrawItemCallback (starty, i);
		starty += 12;
	}

	// testing...
	/*
	Menu_Print (-50, vid.height - 60, va ("ScrollBoxStartItem:   %i\n", this->ScrollBoxStartItem));
	Menu_Print (-50, vid.height - 50, va ("ScrollBoxCurrentItem: %i\n", this->ScrollBoxCurrentItem));
	Menu_Print (-50, vid.height - 40, va ("MaxVisibleItems:      %i\n", this->MaxVisibleItems));
	Menu_Print (-50, vid.height - 30, va ("NumItems:             %i\n", this->NumItems));
	*/

	return starty;
}


int CScrollBoxProvider::GetCurrent (void)
{
	return this->ScrollBoxCurrentItem;
}


void CScrollBoxProvider::SetCurrent (int newcurr)
{
	if (newcurr < 0)
		this->ScrollBoxCurrentItem = 0;
	else if (this->ScrollBoxCurrentItem >= this->NumItems)
		this->ScrollBoxCurrentItem = this->NumItems - 1;
	else this->ScrollBoxCurrentItem = newcurr;
}


void CScrollBoxProvider::KeyFunc (int key)
{
	switch (key)
	{
	case K_ENTER:
		if (this->EnterItemCallback)
			this->EnterItemCallback (this->ScrollBoxCurrentItem);
		break;

	case K_UPARROW:
		this->ScrollBoxCurrentItem--;

		// simple scrollbox
		if (this->NumItems <= this->MaxVisibleItems)
		{
			if (this->ScrollBoxCurrentItem < 0)
				this->ScrollBoxCurrentItem = this->NumItems - 1;

			break;
		}

		// check for wrap
		if (this->ScrollBoxCurrentItem < 0)
		{
			this->ScrollBoxStartItem = (this->NumItems - this->MaxVisibleItems);
			this->ScrollBoxCurrentItem = this->NumItems - 1;
		}

		// pull back the start item - note - current item can never go to -1 (trapped above)
		// so start item will never go below 0
		if (this->ScrollBoxCurrentItem < this->ScrollBoxStartItem)
			this->ScrollBoxStartItem--;

		break;

	case K_DOWNARROW:
		this->ScrollBoxCurrentItem++;

		// simple scrollbox
		if (this->NumItems <= this->MaxVisibleItems)
		{
			if (this->ScrollBoxCurrentItem >= this->NumItems)
				this->ScrollBoxCurrentItem = 0;

			break;
		}

		// check for advance
		if (this->ScrollBoxCurrentItem >= this->MaxVisibleItems)
		{
			// advance the start item
			this->ScrollBoxStartItem++;
		}

		// don't let start advance too far
		if (this->ScrollBoxStartItem > this->NumItems - this->MaxVisibleItems)
			this->ScrollBoxStartItem = this->NumItems - this->MaxVisibleItems;

		if (this->ScrollBoxCurrentItem >= this->NumItems)
		{
			// reset to start
			this->ScrollBoxCurrentItem = 0;
			this->ScrollBoxStartItem = 0;
		}

		break;
	}
}


/*
=====================================================================================================================================

				SUPPORT ROUTINES

=====================================================================================================================================
*/


void Menu_DrawOption (int x, int y, char *option, bool leftflash, bool rightflash)
{
	// do the option
	Menu_PrintWhite (x, y, option);

	if (leftflash) Menu_DrawBackwardsCharacter (x - 12, y, 12 + ((int) (realtime * 2) & 1));
	if (rightflash) Menu_DrawCharacter (x + 4 + strlen (option) * 8, y, 12 + ((int) (realtime * 2) & 1));
}


void Menu_DrawOption (int x, int y, char *option)
{
	// no flashes
	Menu_DrawOption (x, y, option, false, false);
}


void Menu_HighlightGeneric (int x, int y, int width, int height)
{
	int c = menu_fillcolor.integer * 16;
	int dir = 1;

	if (c < 0) c = 0; else if (c > 208) c = 208;

	if (menu_fillcolor.integer > 7)
	{
		// handle backwards ranges...
		c += 9;
		dir = -1;
	}
	else c += 6;

	int colorramp[] = {c, c + dir, c + (2 * dir), c + (3 * dir), c + (4 * dir), c + (3 * dir), c + (2 * dir), c + dir};

	int color = colorramp[(int) (realtime * 10) % 8];

	Draw_Fill (x, y, width, height, color, 192);
}


void Menu_HighlightBar (int xofs, int y, int width, int height)
{
	Menu_HighlightGeneric ((vid.width / 2) + xofs, y - 1, width, height);
}


void Menu_HighlightBar (int xofs, int y, int width)
{
	Menu_HighlightBar (xofs, y, width, 12);
}


void Menu_HighlightBar (int y, int width)
{
	Menu_HighlightBar (-175, y, width, 12);
}


void Menu_HighlightBar (int y)
{
	Menu_HighlightBar (-175, y, 350, 12);
}


/*
=====================================================================================================================================

				SUBSYSTEM ROUTINES

=====================================================================================================================================
*/


/*
================
Menu_ToggleMenu
================
*/
void Menu_ToggleMenu (void)
{
	if (key_dest == key_menu)
	{
		if (m_state != m_main)
		{
			menu_Main.EnterMenu ();
			return;
		}

		key_dest = key_game;
		m_state = m_none;
		return;
	}

	if (key_dest == key_console)
		Con_ToggleConsole_f ();
	else menu_Main.EnterMenu ();
}


void Menu_MainExitQuake (void);

cmd_t Menu_ToggleMenu_Cmd ("togglemenu", Menu_ToggleMenu);
cmd_t M_Menu_Main_f_Cmd ("menu_main", M_Menu_Main_f);
cmd_t M_Menu_SinglePlayer_f_Cmd ("menu_singleplayer", M_Menu_SinglePlayer_f);
cmd_t M_Menu_Load_f_Cmd ("menu_load", M_Menu_Load_f);
cmd_t M_Menu_Save_f_Cmd ("menu_save", M_Menu_Save_f);
cmd_t M_Menu_MultiPlayer_f_Cmd ("menu_multiplayer", M_Menu_MultiPlayer_f);
cmd_t M_Menu_Setup_f_Cmd ("menu_setup", M_Menu_Setup_f);
cmd_t M_Menu_Options_f_Cmd ("menu_options", M_Menu_Options_f);
cmd_t M_Menu_Keys_f_Cmd ("menu_keys", M_Menu_Keys_f);
cmd_t M_Menu_Video_f_Cmd ("menu_video", M_Menu_Video_f);
cmd_t M_Menu_Help_f_Cmd ("help", M_Menu_Help_f);
cmd_t Menu_MainExitQuake_Cmd ("menu_quit", Menu_MainExitQuake);


void M_Draw (void)
{
	// don't run a draw func if not in the menus or if we don't have a current menu set
	if (m_state == m_none || key_dest != key_menu || !menu_Current) return;

	// draw the appropriate background
	if (scr_con_current)
	{
		Draw_ConsoleBackground (vid.height);
		S_ExtraUpdate ();
	}
	else Draw_FadeScreen ();

	// run the draw func
	menu_Current->Draw ();

	// run the appropriate sound
	switch (menu_soundlevel)
	{
	case m_sound_enter:
		// entering a menu
		S_LocalSound ("misc/menu2.wav");
		break;

	case m_sound_nav:
		// navigating through a menu
		S_LocalSound ("misc/menu1.wav");
		break;

	case m_sound_option:
		// setting a menu's options
		S_LocalSound ("misc/menu3.wav");
		break;

	case m_sound_query:
		// a query that requires user input before continuing
		S_LocalSound ("misc/secret.wav");
		break;

	case m_sound_deny:
		// option denied
		S_LocalSound ("misc/talk.wav");
		break;

	case m_sound_none:
	default:
		// no sound
		break;
	}

	// return to no sounds
	menu_soundlevel = m_sound_none;

	// run an update so that things don't get stuck
	S_ExtraUpdate ();
}


void M_Keydown (int key)
{
	// don't run a key func if we're not in the menus or we don't have a current menu set
	if (m_state == m_none || !menu_Current) return;

	// run the appropriate key func
	menu_Current->Key (key);
}


// this is just provided as a development aid to have something to stick into unfinished options
void Menu_NullCommand (void) { /* does nothing */ }


void Menu_InitHelpMenu (void);
void Menu_InitOptionsMenu (void);
void Menu_InitSPMenu (void);
void Menu_InitMainMenu (void);
void Menu_InitSaveLoadMenu (void);
void Menu_InitMultiplayerMenu (void);
void Menu_InitHUDMenu (void);
void Menu_InitControllerMenu (void);
void Menu_InitContentMenu (void);

void Menu_CommonInit (void)
{
	// create the menus.
	// menus should be declared as non-pointers, otherwise you *MUST* be *VERY* *CAREFUL* about the order
	// in which you declare/instantiate/add options so that there are valid submenu/previous menu pointers
	// all the time.  just declare them as non-pointers, OK.
	Menu_InitMainMenu ();
	Menu_InitSPMenu ();
	Menu_InitSaveLoadMenu ();
	Menu_InitMultiplayerMenu ();
	Menu_InitOptionsMenu ();
	Menu_InitHelpMenu ();
	Menu_InitHUDMenu ();
	Menu_InitControllerMenu ();
	Menu_InitContentMenu ();
}


