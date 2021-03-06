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
#include "menu_common.h"

int Key_ModifyKey (int key);

qpic_t *menu_dot_lmp[6];
qpic_t *gfx_qplaque_lmp;
qpic_t *gfx_sp_menu_lmp;
qpic_t *gfx_mainmenu_lmp;
qpic_t *gfx_mp_menu_lmp;
qpic_t *gfx_bigbox_lmp;
qpic_t *gfx_menuplyr_lmp;
qpic_t *menu_help_lmp[NUM_HELP_PAGES];
qpic_t *gfx_ttl_sgl_lmp;
qpic_t *gfx_p_save_lmp;
qpic_t *gfx_p_load_lmp;
qpic_t *gfx_ttl_main_lmp;
qpic_t *gfx_p_option_lmp;
qpic_t *gfx_ttl_cstm_lmp;
qpic_t *gfx_vidmodes_lmp;
qpic_t *gfx_p_multi_lmp;

void Menu_InitPics (void)
{
	// if NUM_HELP_PAGES ever changes from 6 this will also need to be changed...
	for (int i = 0; i < 6; i++)
	{
		menu_help_lmp[i] = Draw_LoadPic (va ("gfx/help%i.lmp", i));
		menu_dot_lmp[i] = Draw_LoadPic (va ("gfx/menudot%i.lmp", (i + 1)));
	}

	gfx_qplaque_lmp = Draw_LoadPic ("gfx/qplaque.lmp");
	gfx_sp_menu_lmp = Draw_LoadPic ("gfx/sp_menu.lmp");

	if (nehahra)
		gfx_mainmenu_lmp = Draw_LoadPic ("gfx/gamemenu.lmp");
	else gfx_mainmenu_lmp = Draw_LoadPic ("gfx/mainmenu.lmp");

	gfx_mp_menu_lmp = Draw_LoadPic ("gfx/mp_menu.lmp");
	gfx_bigbox_lmp = Draw_LoadPic ("gfx/bigbox.lmp");
	gfx_menuplyr_lmp = Draw_LoadPic ("gfx/menuplyr.lmp", false);
	gfx_ttl_sgl_lmp = Draw_LoadPic ("gfx/ttl_sgl.lmp");
	gfx_p_save_lmp = Draw_LoadPic ("gfx/p_save.lmp");
	gfx_p_load_lmp = Draw_LoadPic ("gfx/p_load.lmp");
	gfx_ttl_main_lmp = Draw_LoadPic ("gfx/ttl_main.lmp");
	gfx_p_option_lmp = Draw_LoadPic ("gfx/p_option.lmp");
	gfx_ttl_cstm_lmp = Draw_LoadPic ("gfx/ttl_cstm.lmp");
	gfx_vidmodes_lmp = Draw_LoadPic ("gfx/vidmodes.lmp");
	gfx_p_multi_lmp = Draw_LoadPic ("gfx/p_multi.lmp");
}


// our current menu
CQMenu *menu_Current = NULL;

// our menu stack
CQMenu *menu_Stack[256] = {NULL};
int menu_StackDepth = 0;


void Menu_StackPush (CQMenu *menu)
{
	menu_StackDepth++;
	Con_DPrintf ("Stack level %i\n", menu_StackDepth);
	menu_Stack[menu_StackDepth] = menu;
	menu->EnterMenu ();
}


void Menu_StackPop (void)
{
	menu_StackDepth--;
	Con_DPrintf ("Stack level %i\n", menu_StackDepth);

	if (menu_StackDepth <= 0)
	{
		// exit the menus entirely
		menu_StackDepth = 0;

		// set correct key_dest
		if (cls.state != ca_connected)
			key_dest = key_console;
		else key_dest = key_game;

		m_state = m_none;
		menu_Current = NULL;
		return;
	}

	if (menu_Stack[menu_StackDepth]) menu_Stack[menu_StackDepth]->EnterMenu ();
}


// sounds to play in menus
menu_soundlevel_t menu_soundlevel = m_sound_none;

// current menu state (largely unused now)
m_state_t m_state;

// EnterMenu wrappers for old functions; required for command support
// and for any other external calls
void M_Menu_Main_f (void) {Menu_StackPush (&menu_Main);}
void M_Menu_SinglePlayer_f (void) {Menu_StackPush (&menu_Singleplayer);}
void M_Menu_Load_f (void) {Menu_StackPush (&menu_Load);}
void M_Menu_Save_f (void) {Menu_StackPush (&menu_Save);}
void M_Menu_Options_f (void) {Menu_StackPush (&menu_Options);}
void M_Menu_Keys_f (void) {Menu_StackPush (&menu_Keybindings);}
void M_Menu_Video_f (void) {Menu_StackPush (&menu_Video);}
void M_Menu_Help_f (void) {Menu_StackPush (&menu_Help);}
void M_Menu_MultiPlayer_f (void) {Menu_StackPush (&menu_Multiplayer);}
void M_Menu_Setup_f (void) {Menu_StackPush (&menu_Setup);}

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
	Draw_Character (cx + ((vid.currsize->width - 320) >> 1), line, num);
}


void Menu_DrawBackwardsCharacter (int cx, int line, int num)
{
	Draw_BackwardsCharacter (cx + ((vid.currsize->width - 320) >> 1), line, num);
}


void Menu_Print (int cx, int cy, char *str)
{
	while (*str)
	{
		Menu_DrawCharacter (cx, cy, (*str) + 128);
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


char *Menu_TrimString (char *str)
{
	// con_printf can have appended spaces - ugh
	static char menu_static[1024];

	Q_strncpy (menu_static, str, 1023);

	for (int i = strlen (menu_static); i; i--)
	{
		if (menu_static[i] < 32) continue;

		if (menu_static[i] != ' ')
		{
			menu_static[i + 1] = 0;
			break;
		}
	}

	for (int i = 0; ; i++)
	{
		if (!menu_static[i])
		{
			str = menu_static;
			break;
		}

		if (menu_static[i] != ' ')
		{
			str = &menu_static[i];
			break;
		}
	}

	return str;
}


void Menu_PrintCenter (int cx, int cy, char *str)
{
	str = Menu_TrimString (str);
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
	str = Menu_TrimString (str);
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
	this->AllocCommandText (commandtext);

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

	this->MinVal = 0;
	this->MaxVal = 0;
	this->Increment = 0;
	this->ZeroText = NULL;
	this->Units[0] = 0;
}


CQMenuSpinControl::CQMenuSpinControl (char *commandtext, int *menuval, char **stringbuf)
{
	this->AllocCommandText (commandtext);
	this->StringBuf = stringbuf;
	this->MenuVal = menuval;
	this->StringBufPtr = NULL;

	// ensure that we're good to begin with
	this->OutputText[0] = 0;

	// value for alternate type
	this->MenuCvar = NULL;

	this->AcceptsInput = true;

	this->MinVal = 0;
	this->MaxVal = 0;
	this->Increment = 0;
	this->ZeroText = NULL;
	this->Units[0] = 0;
}


CQMenuSpinControl::CQMenuSpinControl (char *commandtext, cvar_t *menucvar, float minval, float maxval, float increment, char *zerotext, char *units)
{
	this->AllocCommandText (commandtext);
	this->MenuCvar = menucvar;
	this->MinVal = minval;
	this->MaxVal = maxval;
	this->Increment = increment;

	if (zerotext)
	{
		this->ZeroText = (char *) Zone_Alloc (strlen (zerotext));
		strcpy (this->ZeroText, zerotext);
	}
	else this->ZeroText = NULL;

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
		if (this->StringBuf[* (this->MenuVal) + 1]) drawright = true;
	}

	int lpos = 160;

	if (!this->MenuCommandText[0])
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
	if (this->MenuCommandText[0])
		Menu_Print (148 - strlen (this->MenuCommandText) * 8, y, this->MenuCommandText);

	if (this->MenuCvar)
	{
		// build the output text
		if (!this->MenuCvar->value && this->ZeroText)
			strcpy (this->OutputText, this->ZeroText);
		else
		{
			_snprintf (this->OutputText, 256, "%g", this->MenuCvar->value);

			if (this->Units[0] != 0)
			{
				strcat (this->OutputText, " ");
				strcat (this->OutputText, this->Units);
			}
		}
	}
	else if (this->StringBuf)
	{
		if (!this->StringBuf[*(this->MenuVal)])
			this->OutputText[0] = 0;
		else strcpy (this->OutputText, this->StringBuf[*(this->MenuVal)]);
	}

	if (!this->MenuCommandText[0])
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

			(*(this->MenuVal)) ++;

			if (!this->StringBuf[* (this->MenuVal)])
			{
				(*(this->MenuVal))--;
				menu_soundlevel = m_sound_deny;
			}

			break;

		default:

			if (k >= 'A' && k <= 'Z') k += 32;

			if (k >= 'a' && k <= 'z')
			{
				for (int i = 0;; i++)
				{
					if (!this->StringBuf[i]) break;

					if ((this->StringBuf[i][0] == k) || (this->StringBuf[i][0] == (k - 32)))
					{
						menu_soundlevel = m_sound_option;
						*(this->MenuVal) = i;
						break;
					}
				}
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
	this->AllocCommandText (cmdtext);
	this->Colour = colour;
	this->Initial = * (this->Colour);
	this->AcceptsInput = true;
}


void CQMenuColourBar::PerformEntryFunction (void)
{
	this->Initial = * (this->Colour);
}


void CQMenuColourBar::Draw (int y)
{
	// text
	Menu_Print (148 - strlen (this->MenuCommandText) * 8, y, this->MenuCommandText);

	int colour = * (this->Colour);
	int intense = colour * 16 + (colour < 8 ? 11 : 4);

	// colour bar
	for (int i = 0; i < 14; i++)
	{
		// take the approximate midpoint colour (handle backward ranges)
		int c = i * 16 + (i < 8 ? 8 : 7);

		// braw baseline colour (offset downwards a little so that it fits correctly
		Draw_Fill (vid.currsize->width / 2 + 12 + i * 8, y + 1, 8, 8, c, 255);
	}

	// draw the highlight rectangle
	Draw_Fill (vid.currsize->width / 2 + 11 + colour * 8, y, 10, 10, 15, 255);

	// redraw the highlighted color at brighter intensity (handle backward ranges)
	Draw_Fill (vid.currsize->width / 2 + 12 + colour * 8, y + 1, 8, 8, intense, 255);
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

		(*(this->Colour)) ++;

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
	this->AllocCommandText (commandtext);

	this->InitialValue = (char *) Zone_Alloc (2);
	this->ScratchPad = (char *) Zone_Alloc (1024);
	this->WorkingText = (char *) Zone_Alloc (1024);

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


void CQMenuCvarTextbox::Draw (int y)
{
	// update the cvar from the working text
	if (strcmp (this->MenuCvar->string, this->WorkingText))
		Cvar_Set (this->MenuCvar, this->WorkingText);

	// give some space above as well as below
	y += 2;

	// text
	Menu_Print (148 - strlen (this->MenuCommandText) * 8, y, this->MenuCommandText);

	// textbox
	Draw_Fill (vid.currsize->width / 2 - 160 + 168, y - 1, MAX_TBLENGTH * 8 + 4, 10, 20, 255);
	Draw_Fill (vid.currsize->width / 2 - 160 + 169, y, MAX_TBLENGTH * 8 + 2, 8, 0, 192);

	// current text
	for (int i = 0; i < MAX_TBLENGTH; i++)
	{
		if (!this->WorkingText[this->TextStart + i]) break;

		Menu_DrawCharacter (170 + i * 8, y, this->WorkingText[this->TextStart + i]);
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

		if (this->TextPos > strlen (this->WorkingText))
		{
			menu_soundlevel = m_sound_deny;
			this->TextPos = strlen (this->WorkingText);
		}

		if (this->TextPos > MAX_TBPOS)
		{
			this->TextPos = MAX_TBPOS;
			this->TextStart++;
		}

		if (this->TextStart > strlen (this->WorkingText) - MAX_TBPOS)
		{
			this->TextStart = strlen (this->WorkingText) - MAX_TBPOS;
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

		if (this->TextPos > strlen (this->WorkingText)) this->TextPos = strlen (this->WorkingText);

		this->TextStart = strlen (this->WorkingText) - MAX_TBPOS;

		if (this->TextStart < 0) this->TextStart = 0;

		break;

	case K_DEL:
		// prevent deletion if at end of string
		if (RealTextPos >= strlen (this->WorkingText))
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
		strcpy (this->ScratchPad, &this->WorkingText[RealTextPos]);
		strcpy (&this->WorkingText[RealTextPos - 1], this->ScratchPad);

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
		if (strlen (this->WorkingText) > 1020)
		{
			menu_soundlevel = m_sound_deny;
			break;
		}

		// this needs working over and it's not used anymore anyway
#if 0
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
#endif

		if (this->Flags & TBFLAG_FUNNAME)
		{
			if ((k = Key_ModifyKey (k)) == 0) return;
		}

		menu_soundlevel = m_sound_option;

		if (this->InsertMode)
		{
			// insert mode
			strcpy (this->ScratchPad, &this->WorkingText[RealTextPos]);
			strcpy (&this->WorkingText[RealTextPos + 1], this->ScratchPad);
			this->WorkingText[RealTextPos] = k;

			// move right
			this->Key (K_RIGHTARROW);
		}
		else
		{
			// overwrite mode
			this->WorkingText[RealTextPos] = k;

			// move right
			this->Key (K_RIGHTARROW);
		}

		break;
	}
}


void CQMenuCvarTextbox::PerformEntryFunction (void)
{
	// copy cvar string to temp storage
	Zone_Free (this->InitialValue);
	this->InitialValue = (char *) Zone_Alloc (strlen (this->MenuCvar->string) + 1);
	strcpy (this->InitialValue, this->MenuCvar->string);
	strcpy (this->WorkingText, this->MenuCvar->string);

	// set positions
	this->TextStart = 0;
	this->TextPos = strlen (this->WorkingText);

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

CQMenuSubMenu::CQMenuSubMenu (char *cmdtext, CQMenu *submenu, bool narrow)
{
	this->AllocCommandText (cmdtext);
	this->SubMenu = submenu;
	this->AcceptsInput = true;
	this->Narrow = narrow;
}


void CQMenuSubMenu::Draw (int y)
{
	Menu_PrintCenter (y, this->MenuCommandText);
}


void CQMenuSubMenu::Key (int k)
{
	if (k == K_ENTER) Menu_StackPush (this->SubMenu);
}


void CQMenuSubMenu::DrawCurrentOptionHighlight (int y)
{
	if (this->Narrow)
		Menu_HighlightBar (-100, y, 200, 12);
	else Menu_HighlightBar (y);
}


/*
=====================================================================================================================================

				SUBMENU (CURSOR HIGHLIGHT)

	use with CQMenuChunkyPic for building an old-style Quake menu

=====================================================================================================================================
*/

CQMenuCursorSubMenu::CQMenuCursorSubMenu (CQMenu *submenu)
{
	this->SubMenu = submenu;
	this->Command = NULL;
	this->AcceptsInput = true;
}


CQMenuCursorSubMenu::CQMenuCursorSubMenu (menucommand_t command)
{
	this->SubMenu = NULL;
	this->Command = command;
	this->AcceptsInput = true;
}


void CQMenuCursorSubMenu::Draw (int y)
{
	// increment here as the parent doesn't yet exist at construction time
	this->Parent->NumCursorOptions++;
}


void CQMenuCursorSubMenu::Key (int k)
{
	if (k == K_ENTER)
	{
		if (this->SubMenu) Menu_StackPush (this->SubMenu);
		if (this->Command) this->Command ();
	}
}


void CQMenuCursorSubMenu::DrawCurrentOptionHighlight (int y)
{
	Draw_Pic (((vid.currsize->width - 240) >> 1) + 5, y, menu_dot_lmp[(int) (realtime * 10) % 6], 1, true);
}


int CQMenuCursorSubMenu::GetYAdvance (void)
{
	// menu cursor is 20 high per ID Quake source
	return 20;
}


/*
=====================================================================================================================================

				COMMAND

=====================================================================================================================================
*/

CQMenuCommand::CQMenuCommand (char *cmdtext, menucommand_t command)
{
	this->AllocCommandText (cmdtext);

	if (command)
		this->Command = command;
	else this->Command = Menu_NullCommand;

	this->AcceptsInput = true;
}


void CQMenuCommand::Draw (int y)
{
	Menu_PrintCenter (y, this->MenuCommandText);
}


void CQMenuCommand::Key (int k)
{
	if (k == K_ENTER)
	{
		menu_soundlevel = m_sound_option;

		if (this->Command) this->Command ();
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
	this->AllocCommandText (spacertext);
	this->AcceptsInput = false;
}


void CQMenuSpacer::Draw (int y)
{
	if (this->MenuCommandText[0])
	{
		Menu_PrintCenter (y, this->MenuCommandText);
	}
}


int CQMenuSpacer::GetYAdvance ()
{
	if (this->MenuCommandText[0])
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
	int k = * ((int *) stuff);

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
	this->AllocCommandText (title);
	this->AcceptsInput = false;
}


void CQMenuTitle::Draw (int y)
{
	// everywhere we had a title we put a spacer before it, so let's get rid of the need for that
	Menu_PrintCenterWhite (y + 5, this->MenuCommandText);
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

CQMenuBanner::CQMenuBanner (qpic_t **pic)
{
	this->Pic = pic;
	this->AcceptsInput = false;
}


void CQMenuBanner::Draw (int y)
{
	// draw centered on screen and offset down
	Draw_Pic ((vid.currsize->width - this->Pic[0]->width) >> 1, y, this->Pic[0], 1, true);
	this->Y = this->Pic[0]->height + 10;
}


int CQMenuBanner::GetYAdvance (void)
{
	return this->Y;
}


/*
=====================================================================================================================================

				CHUNKY PIC - for simple menus

=====================================================================================================================================
*/

CQMenuChunkyPic::CQMenuChunkyPic (qpic_t **pic)
{
	this->Pic = pic;
	this->AcceptsInput = false;
}


void CQMenuChunkyPic::Draw (int y)
{
	// per ID Quake each option is 20 high
	y -= this->Parent->NumCursorOptions * 20;

	// quake plaque
	Draw_Pic (((vid.currsize->width - 240) >> 1) - 35, y - 15, gfx_qplaque_lmp, 1, true);

	// draw centered on screen and offset down
	// avoid bilerp seam
	Draw_Pic (((vid.currsize->width - 240) >> 1) + 26, y + 1, this->Pic[0], 1, true);//, 1, 1, this->Pic[0]->width - 2, this->Pic[0]->height - 2);

	this->Y = this->Pic[0]->height + 10;
}


int CQMenuChunkyPic::GetYAdvance (void)
{
	// no advance because this one is non-interactive and we use the cursor submenu to do the actual stuff
	return 0;
}


/*
=====================================================================================================================================

				CVAR SLIDER

=====================================================================================================================================
*/

CQMenuCvarSlider::CQMenuCvarSlider (char *commandtext, cvar_t *menucvar, float minval, float maxval, float stepsize)
{
	this->AllocCommandText (commandtext);

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
	Menu_Print (148 - strlen (this->MenuCommandText) * 8, y, this->MenuCommandText);

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

	this->AllocCommandText (commandtext);
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
	Menu_Print (148 - strlen (this->MenuCommandText) * 8, y, this->MenuCommandText);

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
	this->AllocCommandText (commandtext);
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
	Menu_Print (148 - strlen (this->MenuCommandText) * 8, y, this->MenuCommandText);

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

				INTEGER ON/OFF TOGGLE

=====================================================================================================================================
*/

CQMenuIntegerToggle::CQMenuIntegerToggle (char *commandtext, int *menuoption, int toggleoffvalue, int toggleonvalue)
{
	this->AllocCommandText (commandtext);
	this->AcceptsInput = true;
	this->ToggleOffValue = toggleoffvalue;
	this->ToggleOnValue = toggleonvalue;
	this->MenuOption = menuoption;
}


void CQMenuIntegerToggle::DrawCurrentOptionHighlight (int y)
{
	Menu_HighlightBar (y);

	// this is a hack to get the l/r indicators working correctly here
	Menu_DrawOption (172, y, this->MenuOption[0] ? "  " : "   ", true, true);
}


void CQMenuIntegerToggle::Draw (int y)
{
	Menu_Print (148 - strlen (this->MenuCommandText) * 8, y, this->MenuCommandText);

	// this is a hack to get the l/r indicators working correctly here
	Menu_DrawOption (172, y, this->MenuOption[0] ? "On" : "Off", false, false);
}


void CQMenuIntegerToggle::Key (int k)
{
	switch (k)
	{
	case K_LEFTARROW:
	case K_RIGHTARROW:
	case K_ENTER:
		// flip the toggle state
		this->MenuOption[0] = !this->MenuOption[0];
		menu_soundlevel = m_sound_option;
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

void CQMenuOption::AllocCommandText (char *commandtext)
{
	if (commandtext)
	{
		this->MenuCommandText = (char *) Zone_Alloc (strlen (commandtext) + 1);
		strcpy (this->MenuCommandText, commandtext);
	}
	else
	{
		this->MenuCommandText = (char *) Zone_Alloc (2);
		this->MenuCommandText[0] = 0;
	}
}


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
void CQMenuOption::SetTag (unsigned int Tag) {this->OptionTag = Tag;}

// enable/disable on a tag match
void CQMenuOption::CheckEnable (unsigned int Tag, bool enable) {if (this->OptionTag == Tag) this->Enabled = enable;}

// show/hide on a tag match
void CQMenuOption::CheckVisible (unsigned int Tag, bool shown) {if (this->OptionTag == Tag) this->Visible = shown;}

/*
=====================================================================================================================================

				THE MENU ITSELF

=====================================================================================================================================
*/

CQMenu::CQMenu (m_state_t menustate)
{
	// defaults
	this->MenuOptions = NULL;
	this->CurrentOption = NULL;
	this->NumOptions = 0;
	this->NumCursorOptions = 0;
	this->MenuState = menustate;
}


void CQMenu::EnableMenuOptions (unsigned int Tag)
{
	for (CQMenuOption *opt = this->MenuOptions; opt; opt = opt->NextOption)
	{
		opt->CheckEnable (Tag, true);
	}
}


void CQMenu::DisableMenuOptions (unsigned int Tag)
{
	for (CQMenuOption *opt = this->MenuOptions; opt; opt = opt->NextOption)
	{
		opt->CheckEnable (Tag, false);
	}
}


void CQMenu::ShowMenuOptions (unsigned int Tag)
{
	for (CQMenuOption *opt = this->MenuOptions; opt; opt = opt->NextOption)
	{
		opt->CheckVisible (Tag, true);
	}
}


void CQMenu::HideMenuOptions (unsigned int Tag)
{
	for (CQMenuOption *opt = this->MenuOptions; opt; opt = opt->NextOption)
	{
		opt->CheckVisible (Tag, false);
	}
}


void CQMenu::AddOption (CQMenuOption *opt)
{
	opt->SetTag (0);
	this->AddOption (0, opt);
}


void CQMenu::AddOption (unsigned int OptionTag, CQMenuOption *opt)
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

	// reset for each draw frame

	this->NumCursorOptions = 0;

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

			// the fact that it's current needs special handling in some draw funcs
			opt->Draw (y);
		}
		else opt->Draw (y);

		// get the next y position
		y += opt->GetYAdvance ();
	}
}


void CQMenu::Key (int k)
{
	if (!this->MenuOptions)
	{
		switch (k)
		{
		case K_ESCAPE:
			// back out to previous menu or quit menus if no previous defined
			Menu_StackPop ();
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
		// back out to previous menu or quit menus if no previous defined
		Menu_StackPop ();
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
	// prevent entering the same menu twice
	if (menu_Current == this) return;

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
	this->DeleteItemCallback = NULL;
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


void CScrollBoxProvider::SetDeleteItemCallback (menucommandi_t deleteitemcallback)
{
	this->DeleteItemCallback = deleteitemcallback;
}


int CScrollBoxProvider::DrawItems (int x, int starty)
{
	if (!this->DrawItemCallback) return starty;
	if (!this->NumItems) return starty;

	int initialy = starty;

	// draw the textbox
	Draw_TextBox (x, starty, this->MaxItemWidth * 8 + 8, this->MaxVisibleItems * 12 + 4);
	Draw_VScrollBar (x + this->MaxItemWidth * 8 + 8, starty + 8, this->MaxVisibleItems * 12 + 4, this->ScrollBoxCurrentItem, this->NumItems);

	if (this->DeleteItemCallback)
		Draw_String (x + 20, starty + this->MaxVisibleItems * 12 + 4 + 20, "DEL: Delete Item", 128);

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
	Menu_Print (-50, vid.currsize->height - 60, va ("ScrollBoxStartItem:   %i\n", this->ScrollBoxStartItem));
	Menu_Print (-50, vid.currsize->height - 50, va ("ScrollBoxCurrentItem: %i\n", this->ScrollBoxCurrentItem));
	Menu_Print (-50, vid.currsize->height - 40, va ("MaxVisibleItems:      %i\n", this->MaxVisibleItems));
	Menu_Print (-50, vid.currsize->height - 30, va ("NumItems:             %i\n", this->NumItems));
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
	// protect
	if (!this->NumItems) return;

	switch (key)
	{
	case K_DEL:
		if (this->DeleteItemCallback)
			this->DeleteItemCallback (this->ScrollBoxCurrentItem);

		break;

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
	Menu_HighlightGeneric ((vid.currsize->width / 2) + xofs, y - 1, width, height);
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


void Menu_RemoveMenu (void)
{
	// begin a new stack
	menu_StackDepth = 0;
	m_state = m_none;
	key_dest = key_game;
	menu_Current = NULL;
}


/*
================
Menu_ToggleMenu
================
*/
void Menu_ToggleMenu (void)
{
	// begin a new stack
	menu_StackDepth = 0;
	menu_Current = NULL;

	if (key_dest == key_menu)
	{
		if (m_state != m_main)
		{
			Menu_StackPush (&menu_Main);
			return;
		}

		key_dest = key_game;
		m_state = m_none;
		return;
	}

	S_ClearBuffer ();
	Sleep (5);

	if (key_dest == key_console)
		Con_ToggleConsole_f ();
	else Menu_StackPush (&menu_Main);
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
	// sync up the fill colour in case it was changed to an invalid range outside the menu
	if (menu_fillcolor.integer < 0) Cvar_Set (&menu_fillcolor, 0.0f);
	if (menu_fillcolor.integer > 13) Cvar_Set (&menu_fillcolor, 13.0f);

	Cvar_Set (&menu_fillcolor, menu_fillcolor.integer);

	// don't run a draw func if not in the menus or if we don't have a current menu set
	if (m_state == m_none || key_dest != key_menu || !menu_Current || menu_StackDepth <= 0)
	{
		// reset the stack depth and exit the menus entirely
		// we can't reset key_dest here as doing so breaks the console entirely
		m_state = m_none;
		menu_StackDepth = 0;
		menu_Current = NULL;
		return;
	}

	D3DDraw_SetSize (&vid.menusize);

	// draw the appropriate background
	if (scr_con_current)
	{
		// draw console fullscreen (this is crap, it should take a percentage rather than lines)
		Draw_ConsoleBackground (100);

		// partial alpha
		Draw_FadeScreen (128);
	}
	else
	{
		// full alpha
		Draw_FadeScreen (200);
	}

	// offset the menu to a 640x480 rect centered in the screen
	D3DDraw_SetOfs (0, (vid.menusize.height - 480) / 2);

	// run the draw func
	menu_Current->Draw ();

	// restore original draw offsets
	D3DDraw_SetOfs (0, 0);

	// draw console notify lines even if in the menus
	if (cls.maprunning) Con_DrawNotify ();

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
}


void M_Keydown (int key)
{
	// don't run a key func if we're not in the menus or we don't have a current menu set
	if (m_state == m_none || !menu_Current)
	{
		menu_Current = NULL;
		return;
	}

	// run the appropriate key func
	menu_Current->Key (key);
}


// this is just provided as a development aid to have something to stick into unfinished options
void Menu_NullCommand (void) { /* does nothing */}


void Menu_InitHelpMenu (void);
void Menu_InitOptionsMenu (void);
void Menu_InitSPMenu (void);
void Menu_InitMainMenu (void);
void Menu_InitSaveLoadMenu (void);
void Menu_InitMultiplayerMenu (void);
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
	Menu_InitControllerMenu ();
	Menu_InitContentMenu ();
}


