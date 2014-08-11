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


// these need to be kept high enough so that they don't interfere with other options
#define MENU_TAG_SIMPLE			(1 << 28)
#define MENU_TAG_FULL			(1 << 29)

#define	NUM_HELP_PAGES	6

typedef enum
{
	m_sound_none,
	m_sound_enter,
	m_sound_nav,
	m_sound_option,
	m_sound_query,
	m_sound_deny
} menu_soundlevel_t;

extern menu_soundlevel_t menu_soundlevel;

// these will go away when we finally remove the last of the old menu code.
// for now only save/load and some of the multiplayer ones are needed.
// i've added m_other so that i can define new options any time without
// having to add new items to this list...
typedef enum m_state_t
{
	m_none,
	m_main,
	m_load,
	m_save,
	m_multiplayer,
	m_setup,
	m_net,
	m_options,
	m_video,
	m_keys,
	m_help,
	m_quit,
	m_serialconfig,
	m_modemconfig,
	m_lanconfig_newgame,
	m_lanconfig_joingame,
	m_gameoptions,
	m_search,
	m_slist,
	m_hudoptions,
	m_other
} m_state_t;

extern m_state_t m_state;

typedef void (*menucommand_t) (void);
typedef int (*imenucommand_t) (void);
typedef int (*imenucommandi_t) (int);
typedef void (*menucommandii_t) (int, int);
typedef void (*menucommandiii_t) (int, int, int);
typedef void (*menucommandi_t) (int);

void Menu_CommonInit (void);
void Menu_NullCommand (void);


class CQMenuOption
{
public:
	virtual void Draw (int y);
	virtual int GetYAdvance (void);
	virtual void Key (int k);
	virtual bool CheckStatus (void *stuff);
	virtual void PerformEntryFunction (void);
	virtual void DrawCurrentOptionHighlight (int y);
	bool CanAcceptInput (void);
	CQMenuOption *NextOption;
	CQMenuOption *PrevOption;

	int GetOptionNumber (void);
	void SetOptionNumber (int optionnum);
	void SetParentMenu (class CQMenu *parent);

	void SetTag (unsigned int Tag);
	void CheckEnable (unsigned int Tag, bool enable);
	void CheckVisible (unsigned int Tag, bool shown);

	bool IsVisible (void);
	bool IsEnabled (void);

protected:
	void AllocCommandText (char *commandtext);
	char *MenuCommandText;
	bool AcceptsInput;
	unsigned int OptionTag;
	bool Enabled;
	bool Visible;
	class CQMenu *Parent;

private:
	int OptionNum;
};


class CQMenuCvar
{
protected:
	cvar_t *MenuCvar;
};


class CQMenuColourBar : public CQMenuOption
{
public:
	CQMenuColourBar (char *cmdtext, int *colour);
	void Draw (int y);
	void Key (int k);
	void PerformEntryFunction (void);

private:
	int *Colour;
	int Initial;
};


class CQMenuSpinControl : public CQMenuOption, public CQMenuCvar
{
public:
	CQMenuSpinControl (char *commandtext, cvar_t *menucvar, float minval, float maxval, float increment, char *zerotext = NULL, char *units = NULL);
	CQMenuSpinControl (char *commandtext, int *menuval, char **stringbuf);
	CQMenuSpinControl (char *commandtext, int *menuval, char ***stringbuf);
	void Draw (int y);
	void Key (int k);
	void DrawCurrentOptionHighlight (int y);

private:
	float MinVal;
	float MaxVal;
	float Increment;
	int *MenuVal;
	char **StringBuf;
	char ***StringBufPtr;
	char *ZeroText;
	char Units[64];
	char OutputText[256];
};


class CQMenuCvarToggle : public CQMenuOption, public CQMenuCvar
{
public:
	CQMenuCvarToggle (char *commandtext, cvar_t *menucvar, float toggleoffvalue = 0, float toggleonvalue = 1);
	void Draw (int y);
	void Key (int k);
	void DrawCurrentOptionHighlight (int y);

private:
	bool ToggleState;
	float ToggleOnValue;
	float ToggleOffValue;
};


class CQMenuIntegerToggle : public CQMenuOption
{
public:
	CQMenuIntegerToggle (char *commandtext, int *menuoption, int toggleoffvalue = 0, int toggleonvalue = 1);
	void Draw (int y);
	void Key (int k);
	void DrawCurrentOptionHighlight (int y);

private:
	int *MenuOption;
	int ToggleOnValue;
	int ToggleOffValue;
};


class CQMenuCvarSlider : public CQMenuOption, public CQMenuCvar
{
public:
	CQMenuCvarSlider (char *commandtext, cvar_t *menucvar, float minval, float maxval, float stepsize);
	void Draw (int y);
	void Key (int k);

private:
	float SliderMin;
	float SliderMax;
	float StepSize;
};


class CQMenuCvarExpSlider : public CQMenuOption, public CQMenuCvar
{
public:
	CQMenuCvarExpSlider (char *commandtext, cvar_t *menucvar, int initial, int exponent, int numsteps, bool invert = false);
	void Draw (int y);
	void Key (int k);

private:
	int Initial;
	int Exponent;
	int NumSteps;
	int MaxVal;
	int CurrentStep;
	bool Invert;
};


#define TBFLAG_FREETEXT			0
#define TBFLAG_ALLOWNUMBERS		1
#define TBFLAG_ALLOWLETTERS		2
#define TBFLAG_FILENAMECHARS	4
#define TBFLAG_ALLOWSPACE		8
#define TBFLAG_FOLDERPATH		16
#define TBFLAG_READONLY			32

// common combinations
#define TBFLAGS_FILENAMEFLAGS	(TBFLAG_ALLOWNUMBERS | TBFLAG_ALLOWLETTERS | TBFLAG_FILENAMECHARS)
#define TBFLAGS_ALPHANUMERICFLAGS (TBFLAG_ALLOWNUMBERS | TBFLAG_ALLOWLETTERS)
#define TBFLAGS_FOLDERNAMEFLAGS (TBFLAG_FOLDERPATH | TBFLAG_ALLOWNUMBERS | TBFLAG_ALLOWLETTERS | TBFLAG_FILENAMECHARS)

#define MAX_TBLENGTH	20
#define MAX_TBPOS		(MAX_TBLENGTH - 1)

class CQMenuCvarTextbox : public CQMenuOption, public CQMenuCvar
{
public:
	CQMenuCvarTextbox (char *commandtext, cvar_t *menucvar, int flags = TBFLAG_FREETEXT);
	void Draw (int y);
	void Key (int k);
	void PerformEntryFunction (void);
	int GetYAdvance (void);
	void DrawCurrentOptionHighlight (int y);

private:
	char *InitialValue;
	char *ScratchPad;
	char *WorkingText;

	// properties
	int TextStart;
	int TextPos;

	// insert or overwrite
	bool InsertMode;

	// true if the current option
	bool IsCurrent;

	// define the type of content allowed in the textbox
	int Flags;
};


class CQMenuCustomEnter : public CQMenuOption
{
public:
	CQMenuCustomEnter (menucommand_t enterfunc);
	void PerformEntryFunction (void);
	int GetYAdvance (void);

private:
	menucommand_t EnterFunc;
};


class CQMenuSpacer : public CQMenuOption
{
public:
	CQMenuSpacer (char *spacertext = NULL);
	void Draw (int y);
	int GetYAdvance (void);
};


class CQMenuCustomDraw : public CQMenuOption
{
public:
	CQMenuCustomDraw (imenucommandi_t customdrawfunc);
	void Draw (int y);
	int GetYAdvance (void);

private:
	imenucommandi_t CustomDrawFunc;
	int Y;
};


class CQMenuCustomKey : public CQMenuOption
{
public:
	CQMenuCustomKey (int keycapture, menucommand_t capturefunc, bool fwoverride = true);
	CQMenuCustomKey (int keycapture, menucommandi_t capturefunc, bool fwoverride = true);
	bool CheckStatus (void *stuff);
	int GetYAdvance (void);

private:
	int KeyCapture;
	bool OverRideFramework;
	menucommand_t CaptureFuncNoKeyArg;
	menucommandi_t CaptureFuncKeyArg;
};


class CQMenuBanner : public CQMenuOption
{
public:
	CQMenuBanner (qpic_t **pic);
	void Draw (int y);
	int GetYAdvance (void);

private:
	qpic_t **Pic;
	int Y;
};


class CQMenuChunkyPic : public CQMenuOption
{
public:
	CQMenuChunkyPic (qpic_t **pic);
	void Draw (int y);
	int GetYAdvance (void);

private:
	qpic_t **Pic;
	int Y;
};


class CQMenuTitle : public CQMenuOption
{
public:
	CQMenuTitle (char *title);
	void Draw (int y);
	int GetYAdvance (void);
};


class CQMenuCommand : public CQMenuOption
{
public:
	CQMenuCommand (char *cmdtext, menucommand_t command);
	void Draw (int y);
	void Key (int k);

private:
	menucommand_t Command;
};


class CQMenu
{
public:
	CQMenu (m_state_t menustate);
	void Draw (void);
	void Key (int k);
	void AddOption (CQMenuOption *opt);
	void AddOption (unsigned int OptionTag, CQMenuOption *opt);
	void EnterMenu (void);
	CQMenuOption *MenuOptions;
	void EnableMenuOptions (unsigned int Tag);
	void DisableMenuOptions (unsigned int Tag);
	void ShowMenuOptions (unsigned int Tag);
	void HideMenuOptions (unsigned int Tag);
	int NumCursorOptions;

private:
	int NumOptions;
	CQMenuOption *CurrentOption;
	m_state_t MenuState;
};


class CQMenuSubMenu : public CQMenuOption
{
public:
	CQMenuSubMenu (char *cmdtext, CQMenu *submenu, bool narrow = false);
	void Draw (int y);
	void Key (int k);
	void DrawCurrentOptionHighlight (int y);

private:
	CQMenu *SubMenu;
	bool Narrow;
};


class CQMenuCursorSubMenu : public CQMenuOption
{
public:
	CQMenuCursorSubMenu (CQMenu *submenu);
	CQMenuCursorSubMenu (menucommand_t command);
	void Draw (int y);
	void DrawCurrentOptionHighlight (int y);
	void Key (int k);
	int GetYAdvance (void);

private:
	CQMenu *SubMenu;
	menucommand_t Command;
	int Y;
};


class CScrollBoxProvider
{
public:
	CScrollBoxProvider (int numitems, int maxvisibleitems, int maxitemwidth);
	void SetDrawItemCallback (menucommandii_t drawitemcallback);
	void SetHoverItemCallback (menucommandiii_t hoveritemcallback);
	void SetEnterItemCallback (menucommandi_t enteritemcallback);
	void SetDeleteItemCallback (menucommandi_t deleteitemcallback);
	void KeyFunc (int key);
	int DrawItems (int x, int starty);
	int GetCurrent (void);
	void SetCurrent (int newcurr);

private:
	// callbacks - 2 args; which item number it happened on and the vertical position it happened on
	menucommandii_t DrawItemCallback;
	menucommandiii_t HoverItemCallback;
	menucommandi_t EnterItemCallback;
	menucommandi_t DeleteItemCallback;

	// properties
	int ScrollBoxStartItem;
	int ScrollBoxCurrentItem;
	int NumItems;
	int MaxVisibleItems;
	int MaxItemWidth;
};


// menus
extern CQMenu menu_Main;
extern CQMenu menu_Singleplayer;
extern CQMenu menu_Save;
extern CQMenu menu_Load;
extern CQMenu menu_Multiplayer;
extern CQMenu menu_TCPIPNewGame;
extern CQMenu menu_TCPIPJoinGame;
extern CQMenu menu_GameConfig;
extern CQMenu menu_Setup;
extern CQMenu menu_Options;
extern CQMenu menu_Keybindings;
extern CQMenu menu_Video;
extern CQMenu menu_Help;

// our current menu
extern CQMenu *menu_Current;


// drawing functions - relative to 320 width
void Menu_Print (int cx, int cy, char *str);
void Menu_PrintWhite (int cx, int cy, char *str);
void Menu_PrintCenter (int cy, char *str);
void Menu_PrintCenter (int cx, int cy, char *str);
void Menu_PrintCenterWhite (int cy, char *str);
void Menu_PrintCenterWhite (int cx, int cy, char *str);
void Menu_HighlightBar (int y);
void Menu_HighlightBar (int y, int width);
void Menu_HighlightBar (int xofs, int y, int width);
void Menu_HighlightBar (int xofs, int y, int width, int height);
void Menu_HighlightGeneric (int x, int y, int width, int height);
void Menu_DrawOption (int x, int y, char *option);
void Menu_DrawOption (int x, int y, char *option, bool leftflash, bool rightflash);
void Menu_DrawCharacter (int cx, int line, int num);
void Menu_DrawBackwardsCharacter (int cx, int line, int num);

// menu stack
void Menu_StackPush (CQMenu *menu);
void Menu_StackPop (void);
