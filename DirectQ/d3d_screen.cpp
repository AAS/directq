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

// screen.c -- master for refresh, status bar, console, chat, notify, etc

#include "quakedef.h"
#include "d3d_model.h"
#include "d3d_quake.h"

void R_RenderView (double frametime);
void Menu_PrintCenterWhite (int cy, char *str);
void Menu_PrintWhite (int cx, int cy, char *str);
void D3D_GenerateTextureList (void);

/*

background clear
rendering
turtle/net/ram icons
sbar
centerprint / slow centerprint
notify lines
intermission / finale overlay
loading plaque
console
menu

required background clears
required update regions


syncronous draw mode or async
One off screen buffer, with updates either copied or xblited


async draw will require the refresh area to be cleared, because it will be
xblited, but sync draw can just ignore it.

sync
draw

CenterPrint ()
SlowPrint ()
Screen_Update ();
Con_Printf ();

net
turn off messages option

the refresh is allways rendered, unless the console is full screen


console is:
	notify lines
	half
	full


*/


void SCR_RefdefCvarChange (cvar_t *blah)
{
	vid.recalc_refdef = true;
}


float		scr_con_current;

cvar_t		scr_showcoords ("scr_showcoords", "0");

// timeout when loading plaque is up
#define SCR_DEFTIMEOUT 60
float		scr_timeout;

float		oldscreensize, oldfov, oldconscale, oldhudbgfill, oldsbaralpha;

extern cvar_t gl_conscale;
extern cvar_t scr_sbarscale;
extern cvar_t scr_menuscale;
extern cvar_t scr_conscale;

cvar_t		scr_viewsize ("viewsize", 100, CVAR_ARCHIVE, SCR_RefdefCvarChange);
cvar_t		scr_fov ("fov", 90, 0, SCR_RefdefCvarChange);	// 10 - 170
cvar_t		scr_fovcompat ("fov_compatible", 0.0f, CVAR_ARCHIVE, SCR_RefdefCvarChange);
cvar_t		scr_conspeed ("scr_conspeed", 3000);
cvar_t		scr_centertime ("scr_centertime", 2);
cvar_t		scr_centerlog ("scr_centerlog", 1, CVAR_ARCHIVE);
cvar_t		scr_showram ("showram", 1);
cvar_t		scr_showturtle ("showturtle", "0");
cvar_t		scr_showpause ("showpause", 1);
cvar_t		scr_printspeed ("scr_printspeed", 10);

cvar_t		scr_screenshotformat ("scr_screenshotformat", "tga", CVAR_ARCHIVE);
cvar_t		scr_shotnamebase ("scr_shotnamebase", "Quake", CVAR_ARCHIVE);
cvar_t scr_screenshotdir ("scr_screenshotdir", "screenshot/", CVAR_ARCHIVE, COM_ValidateUserSettableDir);

cvar_t	r_automapshot ("r_automapshot", "0", CVAR_ARCHIVE);

// qrack compatibility
cvar_alias_t scr_sshot_type ("scr_sshot_type", &scr_screenshotformat);

// darkplaces
cvar_t scr_screenshot_jpeg ("scr_screenshot_jpeg", "1", CVAR_ARCHIVE);
cvar_t scr_screenshot_png ("scr_screenshot_png", "1", CVAR_ARCHIVE);
cvar_t scr_screenshot_gammaboost ("scr_screenshot_gammaboost", "1", CVAR_ARCHIVE);

// sanity - it's not a gamma boost, it's a gamma *modification*
cvar_alias_t scr_screenshot_gamma ("scr_screenshot_gamma", &scr_screenshot_gammaboost);

extern	cvar_t	crosshair;

bool	scr_initialized;		// ready to draw

qpic_t		*scr_ram;
qpic_t		*scr_net;
qpic_t		*scr_turtle;

int			clearconsole;
int			clearnotify;

bool	scr_disabled_for_loading;
bool	scr_drawmapshot;
float	saved_viewsize = 0;

float		scr_disabled_time;

bool	block_drawing;

void SCR_ScreenShot_f (void);
void HUD_DrawHUD (void);


/*
===============================================================================

CENTER PRINTING

===============================================================================
*/

char		scr_centerstring[1024];
float		ScrCenterTimeStart;	// for slow victory printing
float		ScrCenterTimeOff;
int			scr_center_lines;
int			scr_erase_lines;
int			scr_erase_center;
int			scr_center_width;

/*
==============
SCR_CenterPrint

Called for important messages that should stay in the center of the screen
for a few moments
==============
*/
void SCR_CenterPrint (char *str)
{
	// the server sends a blank centerstring in some places.  this must be el-obscuro bug number 666, as i have
	// no recollection of any references to it anywhere else, whatsoever.  i only noticed it while experimenting
	// with putting a textbox around the center string!
	if (!str[0])
	{
		// an empty print is sometimes used to explicitly clear the previous centerprint
		SCR_ClearCenterString ();
		return;
	}

	// only log if the previous centerprint has already been cleared (cl.time for timedemo compat)
	if (scr_centerlog.integer && !cl.intermission && ScrCenterTimeOff <= cl.time)
	{
		Con_SilentPrintf ("\n\35\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\37\n");
		Con_SilentPrintf ("\n%s\n\n", str);
		Con_SilentPrintf ("\35\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\37\n\n");

		// ensure (required as a standard Con_Printf immediately following a centerprint will screw up the notify lines)
		Con_ClearNotify ();
	}

	Q_strncpy (scr_centerstring, str, sizeof (scr_centerstring) - 1);

	// use cl.time for timedemo compatibility
	ScrCenterTimeOff = cl.time + scr_centertime.value;
	ScrCenterTimeStart = cl.time;

	// count the number of lines for centering
	scr_center_lines = 1;
	scr_center_width = 0;
	int widthcount = 0;

	while (*str)
	{
		if (*str == '\n')
		{
			scr_center_lines++;

			if (widthcount > scr_center_width) scr_center_width = widthcount;

			widthcount = 0;
		}

		str++;
		widthcount++;
	}

	// single line
	if (widthcount > scr_center_width) scr_center_width = widthcount;
}


int finaley = 0;

// some of sandy's e4 messages are offset strangely so we use this to center them properly
// (fixme - will this break qc menu hacks?)
void Menu_PrintCenterWhite (int cy, char *str);

void SCR_DrawCenterString (void)
{
	char	*start;
	int		l;
	int		j;
	int		x, y;
	int		remaining;

	// the finale prints the characters one at a time
	// needs to use cl.time for demos
	if (cl.intermission)
		remaining = scr_printspeed.value * cl.time - ScrCenterTimeStart;
	else remaining = 9999;

	scr_erase_center = 0;
	start = scr_centerstring;

	finaley = y = (vid.currsize->height - (scr_center_lines * 10)) / 3;

#if 0
	// some of sandy's e4 messages are offset strangely so we use this to center them properly
	// (fixme - will this break qc menu hacks?)
	for (int i = 0; ; i++)
	{
		if (!scr_centerstring[i])
		{
			Menu_PrintCenterWhite (y, start);
			break;
		}

		if (scr_centerstring[i] == '\n')
		{
			scr_centerstring[i] = 0;
			Menu_PrintCenterWhite (y, start);
			scr_centerstring[i] = '\n';
			start = &scr_centerstring[i + 1];
			y += 10;
		}
	}
#else
	do
	{
		// scan the width of the line
		for (l = 0; l < 70; l++)
			if (start[l] == '\n' || !start[l])
				break;

		x = (vid.currsize->width - l * 8) / 2;

		for (j = 0; j < l; j++, x += 8)
		{
			Draw_Character (x, y, start[j]);

			if (!remaining--)
				return;
		}

		y += 10;

		while (*start && *start != '\n')
			start++;

		if (!*start)
			break;

		// skip the \n
		start++;
	} while (1);
#endif
}

void SCR_CheckDrawCenterString (void)
{
	if (scr_center_lines > scr_erase_lines)
		scr_erase_lines = scr_center_lines;

	// bug - this will potentially print the last seen centerprint during the end intermission!!!
	// (cl.time for timedemo compat)
	if (ScrCenterTimeOff <= cl.time && !cl.intermission)
	{
		SCR_ClearCenterString ();
		return;
	}

	if (key_dest != key_game)
	{
		// ensure it's off
		SCR_ClearCenterString ();
		return;
	}

	// should never happen
	if (!scr_centerstring[0])
	{
		SCR_ClearCenterString ();
		return;
	}

	D3DDraw_SetSize (&vid.sbarsize);
	SCR_DrawCenterString ();
}


void SCR_ClearCenterString (void)
{
	scr_centerstring[0] = 0;
	ScrCenterTimeOff = -1;
}


//=============================================================================

/*
====================
SCR_CalcFovY
====================
*/
float SCR_CalcFovX (float fov_y, float width, float height)
{
	float   a;
	float   y;

	// bound, don't crash
	if (fov_y < 1) fov_y = 1;
	if (fov_y > 179) fov_y = 179;

	y = height / tan (fov_y / 360 * D3DX_PI);
	a = atan (width / y);
	a = a * 360 / D3DX_PI;

	return a;
}


float SCR_CalcFovY (float fov_x, float width, float height)
{
	float   a;
	float   x;

	// bound, don't crash
	if (fov_x < 1) fov_x = 1;
	if (fov_x > 179) fov_x = 179;

	x = width / tan (fov_x / 360 * D3DX_PI);
	a = atan (height / x);
	a = a * 360 / D3DX_PI;

	return a;
}


/*
=================
SCR_CalcRefdef

Must be called whenever vid changes
Internal use only
=================
*/
extern cvar_t scr_sbaralpha;

void SCR_CalcGUIScaleFactor (cvar_t *var, sizedef_t *sizedef, sizedef_t *basesize, sizedef_t *sizeclamp)
{
	sizedef->width = (float) basesize->width / var->value;
	sizedef->height = (float) basesize->height / var->value;

	if (sizedef->width < sizeclamp->width || sizedef->height < sizeclamp->height)
	{
		sizedef->width = sizeclamp->width;
		sizedef->height = sizeclamp->height;
	}

	if (sizedef->width > d3d_CurrentMode.Width || sizedef->height > d3d_CurrentMode.Height)
	{
		sizedef->width = d3d_CurrentMode.Width;
		sizedef->height = d3d_CurrentMode.Height;
	}
}


void SCR_SetFOV (float *fovx, float *fovy, float fovvar, int width, int height, bool guncalc)
{
	float aspect = (float) height / (float) width;

#define BASELINE_W	640.0f
#define BASELINE_H	432.0f

	// http://www.gamedev.net/topic/431111-perspective-math-calculating-horisontal-fov-from-vertical/
	// horizontalFov = atan( tan(verticalFov) * aspectratio )
	// verticalFov = atan( tan(horizontalFov) / aspectratio )
	if ((scr_fovcompat.integer || aspect > (BASELINE_H / BASELINE_W)) && !guncalc)
	{
		// use the same calculation as GLQuake did
		// (horizontal is constant, vertical varies)
		fovx[0] = fovvar;
		fovy[0] = SCR_CalcFovY (fovx[0], width, height);
	}
	else
	{
		// alternate calculation (vertical is constant, horizontal varies)
		// consistent with http://www.emsai.net/projects/widescreen/fovcalc/
		// note that the gun always uses this calculation irrespective of the aspect)
		fovy[0] = SCR_CalcFovY (fovvar, BASELINE_W, BASELINE_H);
		fovx[0] = SCR_CalcFovX (fovy[0], width, height);
	}
}


extern cvar_t cl_sbar;

static void SCR_CalcRefdef (void)
{
	// don't need a recalc
	vid.recalc_refdef = 0;

	// rebuild world surfaces
	d3d_RenderDef.rebuildworld = true;

	// yeah, and fuck you too
	if (kurok) Cvar_Set (&scr_viewsize, scr_viewsize.value - 10);

	// bound viewsize
	if (scr_viewsize.value < 100) Cvar_Set ("viewsize", "100");
	if (scr_viewsize.value > 120) Cvar_Set ("viewsize", "120");

	// bound field of view
	if (scr_fov.value < 10) Cvar_Set ("fov", "10");
	if (scr_fov.value > 170) Cvar_Set ("fov", "170");

	// conditions for switching off the HUD - viewsize 120 always switches it off, period
	if (cl.intermission || scr_viewsize.value > 110 || cl.stats[STAT_HEALTH] < 1)
		sb_lines = 0;
	else if (scr_viewsize.value > 100)
		sb_lines = 24;
	else sb_lines = 48;

	// only the classic hud uses lines
	if (cl_sbar.integer) sb_lines = 0;

	// bound console scale
	if (gl_conscale.value < 0) Cvar_Set (&gl_conscale, "0");
	if (gl_conscale.value > 1) Cvar_Set (&gl_conscale, "1");
	if (scr_sbarscale.value < 1) Cvar_Set (&scr_sbarscale, "1");
	if (scr_menuscale.value < 1) Cvar_Set (&scr_menuscale, "1");
	if (scr_conscale.value < 1) Cvar_Set (&scr_conscale, "1");

	// adjust a basesize.width and basesize.height to match the mode aspect
	// they should be the same aspect as the mode, with width never less than 640 and height never less than 480
	sizedef_t basesize = {480 * d3d_CurrentMode.Width / d3d_CurrentMode.Height, 480};

	// bring it up to 640
	if (basesize.width < 640)
	{
		basesize.width = 640;
		basesize.height = basesize.width * d3d_CurrentMode.Height / d3d_CurrentMode.Width;
	}

	// round
	basesize.width = (basesize.width + 7) & ~7;
	basesize.height = (basesize.height + 7) & ~7;

	// clamp
	if (basesize.width > d3d_CurrentMode.Width) basesize.width = d3d_CurrentMode.Width;
	if (basesize.height > d3d_CurrentMode.Height) basesize.height = d3d_CurrentMode.Height;

	// set width and height from our gl_conscale cvar
	sizedef_t fullsize =
	{
		(d3d_CurrentMode.Width - basesize.width) * gl_conscale.value + basesize.width,
		(d3d_CurrentMode.Height - basesize.height) * gl_conscale.value + basesize.height
	};

	// eval our GUI scale factors
	SCR_CalcGUIScaleFactor (&scr_sbarscale, &vid.sbarsize, &fullsize, &basesize);
	SCR_CalcGUIScaleFactor (&scr_menuscale, &vid.menusize, &fullsize, &basesize);
	SCR_CalcGUIScaleFactor (&scr_conscale, &vid.consize, &fullsize, &basesize);

	// calc our sbar lines portion
	vid.sbar_lines = sb_lines;
	vid.sbar_lines *= d3d_CurrentMode.Height;
	vid.sbar_lines /= vid.sbarsize.height;

	// and finally calc our 3D refresh size
	vid.ref3dsize.width = d3d_CurrentMode.Width;
	vid.ref3dsize.height = d3d_CurrentMode.Height - vid.sbar_lines;

#if 1
	SCR_SetFOV (&r_refdef.fov_x, &r_refdef.fov_y, scr_fov.value, vid.ref3dsize.width, vid.ref3dsize.height, false);
	// Con_Printf ("aspect %ix%i  fovx %f  fovy %f\n", vid.ref3dsize.width, vid.ref3dsize.height, r_refdef.fov_x, r_refdef.fov_y);
#else
	// x fov is initially the selected value
	r_refdef.fov_x = scr_fov.value;

	if (scr_fov.integer == 90 || !scr_fovcompat.integer)
	{
		float aspect = (float) vid.ref3dsize.width / (float) vid.ref3dsize.height;

		if (aspect > (4.0f / 3.0f))
		{
			// calculate y fov as if the screen was 640 x 432; this ensures that the top and bottom
			// doesn't get clipped off if we have a widescreen display (also keeps the same amount of the viewmodel visible)
			r_refdef.fov_y = SCR_CalcFovY (r_refdef.fov_x, 640, 432);

			// now recalculate fov_x so that it's correctly proportioned for fov_y
			r_refdef.fov_x = SCR_CalcFovX (r_refdef.fov_y, vid.ref3dsize.width, vid.ref3dsize.height);

			// Con_Printf ("widescreen\n");
		}
		else
		{
			// readjust height so that it's correctly proportioned
			float fovheight = (640.0f * aspect) / (4.0f / 3.0f);

			// calculate y fov for the new height; x fov stays as is
			r_refdef.fov_y = SCR_CalcFovY (r_refdef.fov_x, fovheight, 432);

			// Con_Printf ("tallscreen\n");
		}
	}
	else
	{
		// the user wants their own FOV.  this may not be correct in terms of yfov, but it's at least
		// consistent with GLQuake and other engines
		r_refdef.fov_y = SCR_CalcFovY (r_refdef.fov_x, vid.ref3dsize.width, vid.ref3dsize.height);
	}
#endif
}


/*
=================
SCR_SizeUp_f

Keybinding command
=================
*/
void SCR_SizeUp_f (void)
{
	Cvar_Set ("viewsize", scr_viewsize.value + 10);
	vid.recalc_refdef = 1;
}


/*
=================
SCR_SizeDown_f

Keybinding command
=================
*/
void SCR_SizeDown_f (void)
{
	Cvar_Set ("viewsize", scr_viewsize.value - 10);
	vid.recalc_refdef = 1;
}


//============================================================================

/*
==================
SCR_Init
==================
*/
cmd_t SCR_ScreenShot_f_Cmd ("screenshot", SCR_ScreenShot_f);
cmd_t SCR_SizeUp_f_Cmd ("sizeup", SCR_SizeUp_f);
cmd_t SCR_SizeDown_f_Cmd ("sizedown", SCR_SizeDown_f);

void SCR_Init (void)
{
	scr_ram = Draw_LoadPic ("ram");
	scr_net = Draw_LoadPic ("net");
	scr_turtle = Draw_LoadPic ("turtle");

	scr_initialized = true;
}


/*
==============
SCR_DrawTurtle
==============
*/
void SCR_DrawTurtle (double frametime)
{
	static int	count = 0;

	if (!scr_showturtle.value)
		return;

	if (frametime < 0.1f)
	{
		count = 0;
		return;
	}

	count++;

	if (count < 3)
		return;

	D3DDraw_SetSize (&vid.sbarsize);
	Draw_Pic (vid.currsize->width - 132, 4, scr_turtle, 1, true);
}

/*
==============
SCR_DrawNet
==============
*/
void SCR_DrawNet (void)
{
	if (realtime - cl.lastrecievedmessage < 0.3f) return;
	if (cls.demoplayback) return;
	if (sv.active) return;

	D3DDraw_SetSize (&vid.sbarsize);
	Draw_Pic (vid.currsize->width - 168, 4, scr_net, 1, true);
}

/*
==============
DrawPause
==============
*/
void SCR_DrawPause (void)
{
	extern qpic_t *gfx_pause_lmp;

	if (!scr_showpause.value) return;

	if (!cl.paused) return;

	D3DDraw_SetSize (&vid.sbarsize);
	Draw_Pic ((vid.currsize->width - gfx_pause_lmp->width) / 2, (vid.currsize->height - 48 - gfx_pause_lmp->height) / 2, gfx_pause_lmp, 1, true);
}


/*
==================
SCR_SetUpToDrawConsole
==================
*/
void SCR_SetUpToDrawConsole (void)
{
	// console timings are based on a delta between current realtime and previous realtime
	// so that they can be independent of changes made to host_framerate or host_timescale.
	// scr_conlines and scr_con_current are now percentages of the full screen size instead
	// of line counts so that they are independent of resolution.  the old architecture was evil.

	float scr_conlines = 0;
	static double oldcontime = realtime;
	double frametime = realtime - oldcontime;

	oldcontime = realtime;

	Con_CheckResize ();

	if (cls.signon != SIGNON_CONNECTED || !cls.maprunning)
	{
		scr_conlines = 100;	// full screen
		scr_con_current = scr_conlines;
	}
	else if (key_dest == key_console)
		scr_conlines = 50;	// half screen
	else scr_conlines = 0;	// none visible

	if (scr_conlines < scr_con_current)
	{
		scr_con_current -= scr_conspeed.value * frametime * 0.1f;

		if (scr_conlines > scr_con_current)
			scr_con_current = scr_conlines;

	}
	else if (scr_conlines > scr_con_current)
	{
		scr_con_current += scr_conspeed.value * frametime * 0.1f;

		if (scr_conlines < scr_con_current)
			scr_con_current = scr_conlines;
	}

	con_notifylines = 0;
}


/*
==================
SCR_DrawConsole
==================
*/
void SCR_DrawConsole (void)
{
	D3DDraw_SetSize (&vid.consize);

	if (scr_con_current)
	{
		Con_DrawConsole ((vid.consize.height * scr_con_current) / 100, true);
		clearconsole = 0;
	}
	else
	{
		if (key_dest == key_game || key_dest == key_message)
			Con_DrawNotify ();	// only draw notify in game
	}
}


/*
==============================================================================

						SCREEN SHOTS

==============================================================================
*/


void SCR_WriteDataToTGA (char *filename, byte *data, int width, int height, int srcbpp, int dstbpp)
{
	if (!(srcbpp == 8 || srcbpp == 32))
	{
		Con_Printf ("SCR_WriteDataToTGA: unknown source bpp (%i) for file %s\n", srcbpp, filename);
		return;
	}

	if (!(dstbpp == 24 || dstbpp == 32))
	{
		Con_Printf ("SCR_WriteDataToTGA: unknown dest bpp (%i) for file %s\n", dstbpp, filename);
		return;
	}

	// try to open it
	FILE *f = fopen (filename, "wb");

	// didn't work
	if (!f) return;

	// allocate space for the header
	byte buffer[18];
	memset (buffer, 0, 18);

	// compose the header
	buffer[2] = 2;
	buffer[12] = width & 255;
	buffer[13] = width >> 8;
	buffer[14] = height & 255;
	buffer[15] = height >> 8;
	buffer[16] = dstbpp;
	buffer[17] = 0x20;

	// write out the header
	fwrite (buffer, 18, 1, f);

	for (int i = 0; i < width * height; i++)
	{
		byte *outcolor = NULL;

		if (srcbpp == 8)
			outcolor = (byte *) &d3d_QuakePalette.standard32[data[i]];
		else outcolor = (byte *) &((unsigned *) data)[i];

		if (dstbpp == 24)
			fwrite (outcolor, 3, 1, f);
		else fwrite (outcolor, 4, 1, f);
	}

	// done
	fclose (f);
}


typedef struct writepcx_s
{
    char	manufacturer;
    char	version;
    char	encoding;
    char	bits_per_pixel;
    unsigned short	xmin,ymin,xmax,ymax;
    unsigned short	hres,vres;
    unsigned char	palette[48];
    char	reserved;
    char	color_planes;
    unsigned short	bytes_per_line;
    unsigned short	palette_type;
    char	filler[58];
    unsigned char	data;			// unbounded
} writepcx_t;


void SCR_WriteSurfaceToPCX (char *filename, LPDIRECT3DSURFACE9 rts)
{
	D3DSURFACE_DESC surfdesc;
	D3DLOCKED_RECT lockrect;
	LPDIRECT3DSURFACE9 surf;

	// get the surface description
	hr = rts->GetDesc (&surfdesc);

	if (FAILED (hr))
	{
		Con_Printf ("SCR_WriteSurfaceToPCX: Failed to get backbuffer description\n");
		return;
	}

	// create a surface to copy to (ensure the dest surface is the correct format!!!)
	hr = d3d_Device->CreateOffscreenPlainSurface
	(
		surfdesc.Width,
		surfdesc.Height,
		D3DFMT_X8R8G8B8,	// PCX has no alpha and we rely on the data to be 32-bit
		D3DPOOL_SCRATCH,
		&surf,
		NULL
	);

	if (FAILED (hr))
	{
		Con_Printf ("SCR_WriteSurfaceToPCX: Failed to create a surface to copy to\n");
		return;
	}

	// copy from the rendertarget to system memory
	hr = D3DXLoadSurfaceFromSurface (surf, NULL, NULL, rts, NULL, NULL, D3DX_FILTER_NONE, 0);

	if (FAILED (hr))
	{
		Con_Printf ("SCR_WriteSurfaceToPCX: Failed to copy backbuffer data\n");
		surf->Release ();
		return;
	}

	// lock the surface rect
	hr = surf->LockRect (&lockrect, NULL, d3d_GlobalCaps.DefaultLock);

	if (FAILED (hr))
	{
		Con_Printf ("SCR_WriteSurfaceToPCX: Failed to access backbuffer data\n");
		surf->Release ();
		return;
	}

	// try to open it
	FILE *f = fopen (filename, "wb");

	// didn't work
	if (!f)
	{
		surf->UnlockRect ();
		surf->Release ();
		return;
	}

	int hunkmark = MainHunk->GetLowMark ();
	writepcx_t *pcx = (writepcx_t *) MainHunk->Alloc (surfdesc.Width * surfdesc.Height * 2 + 1000);

	pcx->manufacturer = 0x0a;
	pcx->version = 5;
 	pcx->encoding = 1;
	pcx->bits_per_pixel = 8;

	pcx->xmin = 0;
	pcx->ymin = 0;

	pcx->xmax = (short) (surfdesc.Width - 1);
	pcx->ymax = (short) (surfdesc.Height - 1);
	pcx->hres = 72;
	pcx->vres = 72;

	memset (pcx->palette, 0, sizeof (pcx->palette));
	pcx->color_planes = 1;
	pcx->bytes_per_line = (short) surfdesc.Width;
	pcx->palette_type = 1;
	memset (pcx->filler, 0, sizeof (pcx->filler));

	byte *pack = &pcx->data;

	// encode to PCX
	// adapted from code at http://svn.icculus.org/qshed/trunk/qwalk/
	for (int y = 0; y < surfdesc.Height; y++)
	{
		const unsigned int *pix = ((unsigned int *) lockrect.pBits) + y * surfdesc.Width;

		for (int x = 0; x < surfdesc.Width;)
		{
			unsigned int pix_x = (x < surfdesc.Width) ? pix[x] : pix[surfdesc.Width - 1];
			int runlen = 1;

			for (runlen = 1; runlen < 63 && x + runlen < surfdesc.Width; runlen++)
				if (pix_x != (((x + runlen) < surfdesc.Width) ? pix[x + runlen] : pix[surfdesc.Width - 1]))
					break;

			int besti = -1, bestdist = 0xffffff;
			byte *bgr = (byte *) &pix_x;

			for (int i = 0; i < 256; i++)
			{
				int dist = 30 * abs (d3d_QuakePalette.standard[i].peRed - bgr[2]) + 
					59 * abs (d3d_QuakePalette.standard[i].peGreen - bgr[1]) + 
					11 * abs (d3d_QuakePalette.standard[i].peBlue - bgr[0]);

				if (besti == -1 || dist < bestdist)
				{
					besti = i;
					bestdist = dist;
				}
			}

			byte data = (besti != -1) ? besti : 0;

			if (runlen == 1)
			{
                if ((data & 0xc0) == 0xc0)
                    *pack++ = 0xc1;

                *pack++ = data;
			}
			else
			{
                *pack++ = 0xc0 | runlen;
                *pack++ = data;
			}

			x += runlen;
		}
	}

	// unpack to byte * for further compression
	byte *pbuf = (byte *) MainHunk->Alloc (surfdesc.Width * surfdesc.Height * 2);
	byte *fin = (byte *) &pcx->data;
	byte *enddata = pack;
	pack = &pcx->data;

	for (int y = 0; y < surfdesc.Height; y++)
	{
		byte *a = pbuf + y * surfdesc.Width;
		int x, x2;

		for (x = 0; x < surfdesc.Width && fin < enddata;)
		{
			byte dataByte = *fin++;

			if (dataByte >= 0xC0)
			{
				if (fin >= enddata) break;

				x2 = x + (dataByte & 0x3F);
				dataByte = *fin++;

				if (x2 > surfdesc.Width) x2 = surfdesc.Width;
				while (x < x2) a[x++] = dataByte;
			}
			else a[x++] = dataByte;
		}

		while (x < surfdesc.Width)
			a[x++] = 0;
	}

	// now RLE the bastard again for the final version
	for (int y = 0; y < surfdesc.Height; y++)
	{
		const unsigned char *pix = pbuf + y * surfdesc.Width;

		for (int x = 0; x < surfdesc.Width;)
		{
			unsigned char pix_x = (x < surfdesc.Width) ? pix[x] : pix[surfdesc.Width - 1];
			int runlen = 1;

			for (runlen = 1; runlen < 63 && x + runlen < surfdesc.Width; runlen++)
				if (pix_x != (((x + runlen) < surfdesc.Width) ? pix[x + runlen] : pix[surfdesc.Width - 1]))
					break;

			if (runlen == 1)
			{
                if ((pix_x & 0xc0) == 0xc0)
                    *pack++ = 0xc1;

                *pack++ = pix_x;
			}
			else
			{
                *pack++ = 0xc0 | runlen;
                *pack++ = pix_x;
			}

			x += runlen;
		}
	}

	// write the palette
	*pack++ = 0x0c;	// palette ID byte

	for (int i = 0; i < 256; i++, pack += 3)
	{
		pack[0] = d3d_QuakePalette.standard[i].peRed;
		pack[1] = d3d_QuakePalette.standard[i].peGreen;
		pack[2] = d3d_QuakePalette.standard[i].peBlue;
	}

	// write it out
	fwrite (pcx, pack - (byte *) pcx, 1, f);

	// done
	fclose (f);
	MainHunk->FreeToLowMark (hunkmark);

	// unlock it
	surf->UnlockRect ();
	surf->Release ();
}


void SCR_WriteSurfaceToDDS (char *filename, LPDIRECT3DSURFACE9 rts, D3DFORMAT fmt)
{
	if (fmt == D3DFMT_DXT1 || fmt == D3DFMT_DXT2 || fmt == D3DFMT_DXT3 || fmt == D3DFMT_DXT4 || fmt == D3DFMT_DXT5)
	{
		D3DSURFACE_DESC surfdesc;
		LPDIRECT3DSURFACE9 surf;

		// get the surface description
		hr = rts->GetDesc (&surfdesc);

		if (FAILED (hr))
		{
			Con_Printf ("SCR_WriteSurfaceToDDS: Failed to get backbuffer description\n");
			return;
		}

		// create a surface to copy to (ensure the dest surface is the correct format!!!)
		hr = d3d_Device->CreateOffscreenPlainSurface
		(
		// DXT formats don't work with pool_systemmem... ???
			surfdesc.Width,
			surfdesc.Height,
			fmt,
			D3DPOOL_SCRATCH,
			&surf,
			NULL
		);

		if (FAILED (hr))
		{
			Con_Printf ("SCR_WriteSurfaceToDDS: Failed to create a surface to copy to\n");
			return;
		}

		// copy from the rendertarget to system memory
		hr = D3DXLoadSurfaceFromSurface (surf, NULL, NULL, rts, NULL, NULL, D3DX_FILTER_NONE, 0);

		if (FAILED (hr))
		{
			Con_Printf ("SCR_WriteSurfaceToDDS: Failed to copy backbuffer data\n");
			surf->Release ();
			return;
		}

		D3DXSaveSurfaceToFile (filename, D3DXIFF_DDS, surf, NULL, NULL);

		surf->Release ();
	}
	else Con_Printf ("SCR_WriteSurfaceToDDS : invalid surface format %s\n", D3DTypeToString (fmt));
}


// d3dx doesn't support tga writes (BASTARDS) so we made our own
void SCR_WriteSurfaceToTGA (char *filename, LPDIRECT3DSURFACE9 rts, D3DFORMAT fmt)
{
	if (fmt == D3DFMT_X8R8G8B8 || fmt == D3DFMT_A8R8G8B8)
	{
		D3DSURFACE_DESC surfdesc;
		D3DLOCKED_RECT lockrect;
		LPDIRECT3DSURFACE9 surf;

		// get the surface description
		hr = rts->GetDesc (&surfdesc);

		if (FAILED (hr))
		{
			Con_Printf ("SCR_WriteSurfaceToTGA: Failed to get backbuffer description\n");
			return;
		}

		// create a surface to copy to (ensure the dest surface is the correct format!!!)
		hr = d3d_Device->CreateOffscreenPlainSurface
		(
			surfdesc.Width,
			surfdesc.Height,
			fmt,
			D3DPOOL_SCRATCH,
			&surf,
			NULL
		);

		if (FAILED (hr))
		{
			Con_Printf ("SCR_WriteSurfaceToTGA: Failed to create a surface to copy to\n");
			return;
		}

		// copy from the rendertarget to system memory
		hr = D3DXLoadSurfaceFromSurface (surf, NULL, NULL, rts, NULL, NULL, D3DX_FILTER_NONE, 0);

		if (FAILED (hr))
		{
			Con_Printf ("SCR_WriteSurfaceToTGA: Failed to copy backbuffer data\n");
			surf->Release ();
			return;
		}

		// lock the surface rect
		hr = surf->LockRect (&lockrect, NULL, d3d_GlobalCaps.DefaultLock);

		if (FAILED (hr))
		{
			Con_Printf ("SCR_WriteSurfaceToTGA: Failed to access backbuffer data\n");
			surf->Release ();
			return;
		}

		// try to open it
		FILE *f = fopen (filename, "wb");

		// didn't work
		if (!f)
		{
			surf->UnlockRect ();
			surf->Release ();
			return;
		}

		// allocate space for the header
		byte buffer[18];
		memset (buffer, 0, 18);

		int fmtbits = (fmt == D3DFMT_A8R8G8B8) ? 32 : 24;
		int fmtbytes = fmtbits / 8;

		// compose the header
		buffer[2] = 2;
		buffer[12] = surfdesc.Width & 255;
		buffer[13] = surfdesc.Width >> 8;
		buffer[14] = surfdesc.Height & 255;
		buffer[15] = surfdesc.Height >> 8;
		buffer[16] = fmtbits;
		buffer[17] = 0x20;

		// write out the header
		fwrite (buffer, 18, 1, f);

		// do each RGB triplet individually as we want to reduce from 32 bit to 24 bit
		// (can't create with D3DFMT_R8G8B8 so this is necessary even if suboptimal)
		for (int i = 0; i < surfdesc.Width * surfdesc.Height; i++)
		{
			// retrieve the data
			byte *data = (byte *) &((unsigned *) lockrect.pBits)[i];

			// write it out
			fwrite (data, fmtbytes, 1, f);
		}

		// unlock it
		surf->UnlockRect ();
		surf->Release ();

		// done
		fclose (f);
	}
	else Con_Printf ("SCR_WriteSurfaceToTGA : invalid surface format %s\n", D3DTypeToString (fmt));
}


void SCR_WriteTextureToTGA (char *filename, LPDIRECT3DTEXTURE9 rts, D3DFORMAT fmt)
{
	LPDIRECT3DSURFACE9 texsurf;
	rts->GetSurfaceLevel (0, &texsurf);

	SCR_WriteSurfaceToTGA (filename, texsurf, fmt);
	texsurf->Release ();
}


/*
==================
SCR_ScreenShot_f
==================
*/
void SCR_ScreenShot_f (void)
{
	if (!COM_ValidateContentFolderCvar (&scr_screenshotdir)) return;

	// clear the sound buffer as this can take some time
	S_ClearBuffer ();

	char		checkname[MAX_PATH];
	int			i;

	// try the screenshot format
	if (scr_screenshotformat.string[0] == '.')
	{
		// allow them to prefix with a '.' but silently remove it if they do
		scr_screenshotformat.string[0] = scr_screenshotformat.string[1];
		scr_screenshotformat.string[1] = scr_screenshotformat.string[2];
		scr_screenshotformat.string[2] = scr_screenshotformat.string[3];
	}

	// truncate the format to 3 chars
	scr_screenshotformat.string[3] = 0;

	// ensure
	Cvar_Set (&scr_screenshotformat, scr_screenshotformat.string);

	D3DXIMAGE_FILEFORMAT ssfmt;

	// darkplaces compatibility
	if (scr_screenshot_jpeg.value) Cvar_Set (&scr_screenshotformat, "jpg");
	if (scr_screenshot_png.value) Cvar_Set (&scr_screenshotformat, "png");

	if (!_stricmp (scr_screenshotformat.string, "bmp"))
		ssfmt = D3DXIFF_BMP;
	else if (!_stricmp (scr_screenshotformat.string, "tga"))
		ssfmt = D3DXIFF_TGA;
	else if (!_stricmp (scr_screenshotformat.string, "jpg"))
		ssfmt = D3DXIFF_JPG;
	else if (!_stricmp (scr_screenshotformat.string, "png"))
		ssfmt = D3DXIFF_PNG;
	else if (!_stricmp (scr_screenshotformat.string, "dds"))
		ssfmt = D3DXIFF_DDS;
	else if (!_stricmp (scr_screenshotformat.string, "pcx"))
		ssfmt = D3DXIFF_FORCE_DWORD;	// there's no D3DXIFF_PCX so this is a hack
	else
	{
		// unimplemented
		Con_Printf ("Unimplemented format \"%s\": defaulting to \"TGA\" (D3DXIFF_TGA)\n", scr_screenshotformat.string);
		ssfmt = D3DXIFF_TGA;
		Cvar_Set (&scr_screenshotformat, "tga");
	}

	if (Cmd_Argc () > 1)
	{
		// specify the name
		_snprintf
		(
			checkname,
			128,
			"%s/%s%s.%s",
			com_gamedir,
			scr_screenshotdir.string,
			Cmd_Argv (1),
			scr_screenshotformat.string
		);
	}
	else
	{
		// find a file name to save it to
		for (i = 0; i <= 9999; i++)
		{
			_snprintf
			(
				checkname,
				128,
				"%s/%s%s%04i.%s",
				com_gamedir,
				scr_screenshotdir.string,
				scr_shotnamebase.string,
				i,
				scr_screenshotformat.string
			);

			// file doesn't exist (fixme - replace this with our fs table checker)
			if (!Sys_FileExists (checkname)) break;
		}

		if (i == 10000)
		{
			Con_Printf ("SCR_ScreenShot_f: 9999 Screenshots exceeded.\n");
			return;
		}
	}

	// run a screen refresh
	SCR_UpdateScreen (0);

	// the surface we'll use
	LPDIRECT3DSURFACE9 Surf = NULL;

	// get the backbuffer (note - this might be a render to texture surf if it's underwater!!!)
	d3d_Device->GetRenderTarget (0, &Surf);

	if (scr_screenshot_gammaboost.value != 1.0f)
	{
		// copy off the surface, change gamma
		byte gammatab[256];

		// build a gamma table
		for (int i = 0; i < 256; i++)
		{
			float f = pow ((i + 1) / 256.0f, scr_screenshot_gammaboost.value);
			float inf = f * 255 + 0.5;

			if (inf < 0) inf = 0;
			if (inf > 255) inf = 255;

			gammatab[i] = inf;
		}

		LPDIRECT3DSURFACE9 osps = NULL;
		D3DSURFACE_DESC surfdesc;
		D3DLOCKED_RECT lockrect;

		hr = Surf->GetDesc (&surfdesc);

		if (SUCCEEDED (hr))
		{
			hr = d3d_Device->CreateOffscreenPlainSurface
				(surfdesc.Width,
				surfdesc.Height,
				D3DFMT_X8R8G8B8,
				D3DPOOL_SCRATCH,
				&osps,
				NULL);

			if (SUCCEEDED (hr))
			{
				// copy from the rendertarget to system memory
				hr = D3DXLoadSurfaceFromSurface (osps, NULL, NULL, Surf, NULL, NULL, D3DX_FILTER_NONE, 0);

				if (SUCCEEDED (hr))
				{
					hr = osps->LockRect (&lockrect, NULL, d3d_GlobalCaps.DefaultLock);

					if (SUCCEEDED (hr))
					{
						byte *data = (byte *) lockrect.pBits;

						for (int i = 0; i < surfdesc.Width * surfdesc.Height; i++, data += 4)
						{
							data[0] = gammatab[data[0]];
							data[1] = gammatab[data[1]];
							data[2] = gammatab[data[2]];
						}

						osps->UnlockRect ();
					}

					Surf->Release ();
					Surf = osps;
				}
				else
				{
					osps->Release ();
				}
			}
		}
	}

	// write to file
	// d3dx doesn't support tga writes (BASTARDS) so we made our own
	if (ssfmt == D3DXIFF_TGA)
		SCR_WriteSurfaceToTGA (checkname, Surf, D3DFMT_X8R8G8B8);
	else if (ssfmt == D3DXIFF_DDS)
		SCR_WriteSurfaceToDDS (checkname, Surf, D3DFMT_DXT1);
	else if (ssfmt == D3DXIFF_FORCE_DWORD)
		SCR_WriteSurfaceToPCX (checkname, Surf);	// hack for PCX
	else D3DXSaveSurfaceToFile (checkname, ssfmt, Surf, NULL, NULL);

	// not releasing is a memory leak!!!
	Surf->Release ();

	// report
	Con_Printf ("Wrote %s\n", checkname);
}


void Draw_InvalidateMapshot (void);

void SCR_Mapshot_f (char *shotname, bool report, bool overwrite)
{
	// clear the sound buffer as this can take some time
	S_ClearBuffer ();

	char workingname[256];

	// copy the name out so that we can safely modify it
	Q_strncpy (workingname, shotname, 255);

	// ensure that we have some kind of extension on it - anything will do
	COM_DefaultExtension (workingname, ".blah");

	// now put the correct extension on it
	// note - D3DXSaveTextureToFile won't let us write a TGA so we'll write a BMP instead.  we'll still attempt to load TGAs though...
	// update - TGAs work now.
	// update 2 - went back to BMP - TGA went slightly iffy, can't be bothered fixing it right now
	for (int c = strlen (workingname) - 1; c; c--)
	{
		if (workingname[c] == '.')
		{
			strcpy (&workingname[c + 1], "tga");
			break;
		}
	}

	if (!overwrite)
	{
		// check does it exist
		FILE *f = fopen (workingname, "rb");

		if (f)
		{
			fclose (f);
			Con_DPrintf ("Overwrite of \"%s\" prevented\n", workingname);
			return;
		}
	}

	// new mapshot code - the previous was vile, and caused things to explode pretty dramatically when saving underwater.
	// make viewsize go fullscreen
	float vsv = scr_viewsize.value;
	scr_viewsize.value = 120;

	// the surfaces we'll use
	LPDIRECT3DSURFACE9 d3d_CurrentRenderTarget;
	LPDIRECT3DSURFACE9 d3d_MapshotRenderSurf;
	LPDIRECT3DSURFACE9 d3d_MapshotFinalSurf;

	// store the current render target
	hr = d3d_Device->GetRenderTarget (0, &d3d_CurrentRenderTarget);

	if (FAILED (hr))
	{
		Con_Printf ("SCR_Mapshot_f: failed to get current render target surface\n");
		return;
	}

	extern D3DPRESENT_PARAMETERS d3d_PresentParams;

	// create a surface for the mapshot to render to
	hr = d3d_Device->CreateRenderTarget (d3d_CurrentMode.Width,
		d3d_CurrentMode.Height,
		D3DFMT_X8R8G8B8,
		d3d_PresentParams.MultiSampleType,
		d3d_PresentParams.MultiSampleQuality,
		FALSE,
		&d3d_MapshotRenderSurf,
		NULL);

	if (FAILED (hr))
	{
		SAFE_RELEASE (d3d_CurrentRenderTarget);
		Con_Printf ("SCR_Mapshot_f: failed to create a render target surface\n");
		return;
	}

	// create a surface for the final mapshot destination
	d3d_Device->CreateRenderTarget (128,
		128,
		D3DFMT_X8R8G8B8,
		d3d_PresentParams.MultiSampleType,
		d3d_PresentParams.MultiSampleQuality,
		FALSE,
		&d3d_MapshotFinalSurf,
		NULL);

	if (FAILED (hr))
	{
		SAFE_RELEASE (d3d_CurrentRenderTarget);
		SAFE_RELEASE (d3d_MapshotRenderSurf);
		Con_Printf ("SCR_Mapshot_f: failed to create a render target surface\n");
		return;
	}

	// set the render target for the mapshot
	hr = d3d_Device->SetRenderTarget (0, d3d_MapshotRenderSurf);

	if (FAILED (hr))
	{
		SAFE_RELEASE (d3d_CurrentRenderTarget);
		SAFE_RELEASE (d3d_MapshotRenderSurf);
		Con_Printf ("SCR_Mapshot_f: failed to set render target surface\n");
		return;
	}

	// build the directory
	for (int i = strlen (workingname) - 1; i; i--)
	{
		if (workingname[i] == '/' || workingname[i] == '\\')
		{
			char c = workingname[i];
			workingname[i] = 0;

			Sys_mkdir (workingname);
			workingname[i] = c;
			break;
		}
	}

	// go into mapshot mode
	scr_drawmapshot = true;

	// do a screen refresh to get rid of any UI/etc
	SCR_UpdateScreen (0);

	// sample the rendered surf down to 128 * 128 for the correct mapshot sise
	if (d3d_CurrentMode.Width > d3d_CurrentMode.Height)
	{
		// setup source rect
		RECT srcrect;

		// clip the rectangle
		srcrect.top = 0;
		srcrect.bottom = d3d_CurrentMode.Height;
		srcrect.left = (d3d_CurrentMode.Width - d3d_CurrentMode.Height) / 2;
		srcrect.right = srcrect.left + d3d_CurrentMode.Height;

		// copy the clipped rect
		d3d_Device->StretchRect (d3d_MapshotRenderSurf, &srcrect, d3d_MapshotFinalSurf, NULL, D3DTEXF_LINEAR);
	}
	else if (d3d_CurrentMode.Width == d3d_CurrentMode.Height)
	{
		// straight copy of full source rect
		d3d_Device->StretchRect (d3d_MapshotRenderSurf, NULL, d3d_MapshotFinalSurf, NULL, D3DTEXF_LINEAR);
	}
	else
	{
		// setup source rect
		RECT srcrect;

		// clip the rectangle
		srcrect.left = 0;
		srcrect.right = d3d_CurrentMode.Width;
		srcrect.top = (d3d_CurrentMode.Height - d3d_CurrentMode.Width) / 2;
		srcrect.bottom = srcrect.top + d3d_CurrentMode.Width;

		// copy the clipped rect
		d3d_Device->StretchRect (d3d_MapshotRenderSurf, &srcrect, d3d_MapshotFinalSurf, NULL, D3DTEXF_LINEAR);
	}

	// save the surface out to file (use TGA)
	SCR_WriteSurfaceToTGA (workingname, d3d_MapshotFinalSurf, D3DFMT_X8R8G8B8);

	// reset to the original render target
	hr = d3d_Device->SetRenderTarget (0, d3d_CurrentRenderTarget);

	if (FAILED (hr))
	{
		// this is an error condition as we can't recover gracefully or just do nothing
		SAFE_RELEASE (d3d_CurrentRenderTarget);
		SAFE_RELEASE (d3d_MapshotFinalSurf);
		SAFE_RELEASE (d3d_MapshotRenderSurf);
		Sys_Error ("SCR_Mapshot_f: failed to restore render target surface\n");
		return;
	}

	// not releasing is a memory leak!!!
	d3d_CurrentRenderTarget->Release ();
	d3d_MapshotFinalSurf->Release ();
	d3d_MapshotRenderSurf->Release ();

	// restore old viewsize
	scr_viewsize.value = vsv;

	// exit mapshot mode
	scr_drawmapshot = false;

	// invalidate the cached mapshot
	Draw_InvalidateMapshot ();

	// done
	if (report) Con_Printf ("Wrote mapshot \"%s\"\n", workingname);
}


void SCR_Mapshot_cmd (void)
{
	if (!cls.maprunning) return;
	if (cls.state != ca_connected) return;

	// first ensure we have a "maps" directory
	CreateDirectory (va ("%s/maps", com_gamedir), NULL);

	// now take the mapshot; this is user initiated so always report and overwrite
	SCR_Mapshot_f (va ("%s/%s", com_gamedir, cl.worldmodel->name), true, true);
}


cmd_t Mapshot_Cmd ("mapshot", SCR_Mapshot_cmd);



//=============================================================================


/*
===============
SCR_BeginLoadingPlaque

================
*/
void SCR_SetTimeout (float timeout)
{
	scr_timeout = timeout;
}

bool scr_drawloading = false;

void SCR_DrawLoading (void)
{
	if (!scr_drawloading) return;

	extern qpic_t *gfx_loading_lmp;

	D3DDraw_SetSize (&vid.sbarsize);
	Draw_Pic ((vid.currsize->width - gfx_loading_lmp->width) / 2, (vid.currsize->height - 48 - gfx_loading_lmp->height) / 2, gfx_loading_lmp, 1, true);
}


void SCR_BeginLoadingPlaque (void)
{
	S_StopAllSounds (true);

	if (cls.state != ca_connected) return;
	if (cls.signon != SIGNON_CONNECTED) return;

	// redraw with no console and no center text
	Con_ClearNotify ();
	SCR_ClearCenterString ();
	scr_con_current = 0;

	scr_drawloading = true;
	SCR_UpdateScreen (0);
	scr_drawloading = false;

	Host_DisableForLoading (true);
	scr_disabled_time = realtime;
	SCR_SetTimeout (SCR_DEFTIMEOUT);
}


/*
===============
SCR_EndLoadingPlaque

================
*/
void SCR_EndLoadingPlaque (void)
{
	Host_DisableForLoading (false);
	Con_ClearNotify ();
}

//=============================================================================

char *mbpromts[] =
{
	// convert to orange text
	"Y\345\363  N\357",
	"O\313",
	"O\313  C\341\356\343\345\354",
	"R\345\364\362\371  C\341\356\343\345\354",
	NULL
};


char scr_notifytext[2048];
char scr_notifycaption[80];
int scr_notifyflags = 0;
bool scr_modalmessage = false;

void SCR_DrawNotifyString (char *text, char *caption, int flags)
{
	int		y;

	char *lines[64] = {NULL};
	int scr_modallines = 0;
	char *textbuf = (char *) Zone_Alloc (strlen (text) + 1);
	strcpy (textbuf, text);

	lines[0] = textbuf;

	// count the number of lines
	for (int i = 0;; i++)
	{
		// end
		if (textbuf[i] == 0) break;

		// add a line
		if (textbuf[i] == '\n')
		{
			scr_modallines++;

			// this is to catch a \n\0 case
			if (textbuf[i + 1]) lines[scr_modallines] = &textbuf[i + 1];

			textbuf[i] = 0;
		}
	}

	int maxline = 0;

	for (int i = 0;; i++)
	{
		if (!lines[i]) break;

		if (strlen (lines[i]) > maxline) maxline = strlen (lines[i]);
	}

	// caption might be longer...
	if (strlen (caption) > maxline) maxline = strlen (caption);

	// adjust positioning
	y = (vid.currsize->height - ((scr_modallines + 5) * 10)) / 3;

	// background
	Draw_TextBox ((vid.currsize->width - (maxline * 8)) / 2 - 16, y - 12, maxline * 8 + 16, (scr_modallines + 5) * 10 - 5);
	Draw_TextBox ((vid.currsize->width - (maxline * 8)) / 2 - 16, y - 12, maxline * 8 + 16, 15);

	// draw caption
	Draw_String ((vid.currsize->width - (strlen (caption) * 8)) / 2, y, caption);

	y += 20;

	for (int i = 0;; i++)
	{
		if (!lines[i]) break;

		for (int s = 0; s < strlen (lines[i]); s++)
			lines[i][s] += 128;

		Draw_String ((vid.currsize->width - strlen (lines[i]) * 8) / 2, y, lines[i]);
		y += 10;
	}

	// draw prompt
	char *prompt = NULL;

	if (flags == MB_YESNO)
		prompt = mbpromts[0];
	else if (flags == MB_OK)
		prompt = mbpromts[1];
	else if (flags == MB_OKCANCEL)
		prompt = mbpromts[2];
	else if (flags == MB_RETRYCANCEL)
		prompt = mbpromts[3];
	else prompt = mbpromts[1];

	if (prompt) Draw_String ((vid.currsize->width - strlen (prompt) * 8) / 2, y + 5, prompt);

	Zone_Free (textbuf);
}


/*
==================
SCR_ModalMessage

Displays a text string in the center of the screen and waits for a Y or N
keypress.
==================
*/
int SCR_ModalMessage (char *text, char *caption, int flags)
{
	// prevent being called recursively
	if (scr_modalmessage) return false;

	Q_strncpy (scr_notifytext, text, 2047);
	Q_strncpy (scr_notifycaption, caption, 79);
	scr_notifyflags = flags;

	// so dma doesn't loop current sound
	S_ClearBuffer ();

	bool key_accept = false;

	// force a screen update
	scr_modalmessage = true;
	SCR_UpdateScreen (0);
	SCR_UpdateScreen (0);
	SCR_UpdateScreen (0);
	SCR_UpdateScreen (0);
	scr_modalmessage = false;

	do
	{
		key_count = -1;	// wait for a key down and up
		key_lastpress = 0;	// clear last pressed key
		Sys_SendKeyEvents ();

		// this was trying to be too clever...
		//if (key_lastpress == K_ENTER) {key_accept = true; break;}
		//if (key_lastpress == K_ESCAPE) {key_accept = false; break;}

		// allow ESC key to cancel for options that have a cancel
		if (flags == MB_OK)
		{
			if (key_lastpress == 'o' || key_lastpress == 'O') {key_accept = true; break;}
		}
		else if (flags == MB_YESNO)
		{
			if (key_lastpress == 'y' || key_lastpress == 'Y') {key_accept = true; break;}
			if (key_lastpress == 'n' || key_lastpress == 'N') {key_accept = false; break;}
			if (key_lastpress == K_ESCAPE) {key_accept = false; break;}
		}
		else if (flags == MB_OKCANCEL)
		{
			if (key_lastpress == 'o' || key_lastpress == 'O') {key_accept = true; break;}
			if (key_lastpress == 'c' || key_lastpress == 'C') {key_accept = false; break;}
			if (key_lastpress == K_ESCAPE) {key_accept = false; break;}
		}
		else if (flags == MB_RETRYCANCEL)
		{
			if (key_lastpress == 'r' || key_lastpress == 'R') {key_accept = true; break;}
			if (key_lastpress == 'c' || key_lastpress == 'C') {key_accept = false; break;}
			if (key_lastpress == K_ESCAPE) {key_accept = false; break;}
		}
		else
		{
			if (key_lastpress == 'o' || key_lastpress == 'O') {key_accept = true; break;}
		}

		Sleep (5);
	} while (1);

	return key_accept;
}


//=============================================================================


/*
==================
SCR_UpdateScreen

This is called every frame, and can also be called explicitly to flush
text to the screen.

WARNING: be very careful calling this from elsewhere, because the refresh
needs almost the entire 256k of stack space!
==================
*/
void M_Draw (void);
void HUD_IntermissionOverlay (void);
void HUD_FinaleOverlay (int y);
void SHOWLMP_drawall (void);

extern bool vid_restarted;

void D3DDraw_End2D (void);

extern cvar_t host_speeds;
extern int r_speedstime;

int scr_hostspeeds1;
int scr_hostspeeds2;
int scr_hostspeeds3;

int scr_hostspeedstot1;
int scr_hostspeedstot2;
int scr_hostspeedstot3;
int scr_hostspeedsframes;

double host_speedstime;

void SCR_SetHostSpeeds (double frametime, int pass1, int pass2, int pass3)
{
	if (key_dest != key_game) return;

	host_speedstime += frametime;
	scr_hostspeedstot1 += pass1;
	scr_hostspeedstot2 += pass2;
	scr_hostspeedstot3 += pass3;
	scr_hostspeedsframes++;

	if (host_speedstime >= 0.1)
	{
		scr_hostspeeds1 = (scr_hostspeedstot1 + (scr_hostspeedsframes - 1)) / scr_hostspeedsframes;
		scr_hostspeeds2 = (scr_hostspeedstot2 + (scr_hostspeedsframes - 1)) / scr_hostspeedsframes;
		scr_hostspeeds3 = (scr_hostspeedstot3 + (scr_hostspeedsframes - 1)) / scr_hostspeedsframes;

		host_speedstime = 0;

		scr_hostspeedstot1 = 0;
		scr_hostspeedstot2 = 0;
		scr_hostspeedstot3 = 0;
		scr_hostspeedsframes = 0;
	}
}


void SCR_UpdateScreen (double frametime)
{
	// ensure that everything needed is up
	if (!d3d_Device) return;

	extern D3DDISPLAYMODE d3d_DesktopMode;
	extern D3DDISPLAYMODE d3d_CurrentMode;

	// update the mouse state
	IN_SetMouseState (d3d_CurrentMode.RefreshRate != 0);

	if (block_drawing) return;

	if (scr_disabled_for_loading)
	{
		if (realtime - scr_disabled_time > scr_timeout)
		{
			Host_DisableForLoading (false);

			if (scr_timeout >= SCR_DEFTIMEOUT) Con_Printf ("load failed.\n");
		}
		else return;
	}

	// not initialized yet
	if (!scr_initialized || !con_initialized || !d3d_Device) return;

	// begin rendering; get the size of the refresh window and set up for the render
	// this is also used for lost device recovery mode
	D3DVid_BeginRendering ();

	// this assumes that hr = d3d_Device->BeginScene (); is the final D3D call in D3DVid_BeginRendering...
	if (FAILED (hr)) return;

	// if we needed to restart video skip updating this frame
	if (vid_restarted) return;

	// determine size of refresh window
	if (vid.recalc_refdef) SCR_CalcRefdef ();

	D3DHLSL_BeginFrame ();

	SCR_SetUpToDrawConsole ();

	// save conditiion as in Host_Frame
	if (cls.maprunning && cl.worldmodel && cls.signon == SIGNON_CONNECTED)
		R_RenderView (frametime);

	D3DDraw_Begin2D ();

	if (scr_drawloading)
	{
		SCR_DrawLoading ();
	}
	else if (cl.intermission == 1 && key_dest == key_game && cls.maprunning)
	{
		HUD_IntermissionOverlay ();
	}
	else if (cl.intermission == 2 && key_dest == key_game && cls.maprunning)
	{
		finaley = 16;
		SCR_CheckDrawCenterString ();
		HUD_FinaleOverlay (finaley);
	}
	else if (!scr_drawmapshot)
	{
		SCR_DrawNet ();
		SCR_DrawTurtle (frametime);
		SCR_DrawPause ();

		if (cls.maprunning)
		{
			SCR_CheckDrawCenterString ();
			HUD_DrawHUD ();
			SCR_DrawConsole ();
			SHOWLMP_drawall ();

			if (host_speeds.value && key_dest == key_game)
			{
				/*
				D3DDraw_SetSize (&vid.sbarsize);
				Draw_String (vid.currsize->width - 100, 20, "Host Speeds");
				Draw_String (vid.currsize->width - 124, 30, va ("%5i total", scr_hostspeeds1 + scr_hostspeeds2 + scr_hostspeeds3));
				Draw_String (vid.currsize->width - 124, 40, va ("%5i server", scr_hostspeeds1));
				Draw_String (vid.currsize->width - 124, 50, va ("%5i graphics", scr_hostspeeds2));
				Draw_String (vid.currsize->width - 124, 60, va ("%5i sound", scr_hostspeeds3));
				*/
			}
			else if (r_speeds.value)
			{
				D3DDraw_SetSize (&vid.sbarsize);
				Draw_String (vid.currsize->width - 100, 20, va ("%5i ms", r_speedstime));
				Draw_String (vid.currsize->width - 100, 30, va ("%5i surf", d3d_RenderDef.brush_polys));
				Draw_String (vid.currsize->width - 100, 40, va ("%5i mdl", d3d_RenderDef.alias_polys));
				Draw_String (vid.currsize->width - 100, 50, va ("%5i dlight", d3d_RenderDef.numdlight));
				Draw_String (vid.currsize->width - 100, 60, va ("%5i stream", d3d_RenderDef.numsss));
				Draw_String (vid.currsize->width - 100, 70, va ("%5i lock", d3d_RenderDef.numlock));
				Draw_String (vid.currsize->width - 100, 80, va ("%5i draw", d3d_RenderDef.numdrawprim));
			}

			if (scr_showcoords.integer)
			{
				D3DDraw_SetSize (&vid.sbarsize);
				Draw_String (10, 10, va ("%0.3f %0.3f %0.3f", r_refdef.vieworigin[0], r_refdef.vieworigin[1], r_refdef.vieworigin[2]));
			}
		}
		else
		{
			SCR_ClearCenterString ();
			SCR_DrawConsole ();
		}

		M_Draw ();
	}

	if (scr_modalmessage)
	{
		// ensure that we have a valid size selected
		if (!vid.currsize) D3DDraw_SetSize (&vid.sbarsize);
		SCR_DrawNotifyString (scr_notifytext, scr_notifycaption, scr_notifyflags);
	}

	d3d_RenderDef.numdrawprim = 0;
	d3d_RenderDef.numsss = 0;
	d3d_RenderDef.numlock = 0;
	d3d_RenderDef.numdlight = 0;

	D3DDraw_End2D ();

	D3DHLSL_EndFrame ();
	D3DVid_EndRendering ();

	// take a mapshot on entry to the map
	// unless we're already in mapshot mode, in which case we'll have an infinite loop!!!
	if (r_automapshot.value && d3d_RenderDef.framecount == 5 && !scr_drawmapshot && cls.maprunning)
	{
		// first ensure we have a "maps" directory
		CreateDirectory (va ("%s/maps", com_gamedir), NULL);

		// now take the mapshot; don't overwrite if one is already there
		SCR_Mapshot_f (va ("%s/%s", com_gamedir, cl.worldmodel->name), false, false);
	}

	D3D_GenerateTextureList ();

	if (cls.signon == SIGNON_CONNECTED)
	{
		// particle updating has been moved back to draw time to preserve cache friendliness
		CL_DecayLights ();
	}
}


void SCR_DrawSlider (int x, int y, int width, int stage, int maxstage)
{
	// stage goes from 1 to maxstage inclusive
	// width should really be a multiple of 8
	// slider left
	Draw_Character (x, y, 128);

	// slider body
	for (int i = 16; i < width; i += 8)
		Draw_Character (x + i - 8, y, 129);

	// slider right
	Draw_Character (x + width - 8, y, 130);

	// slider position
	x = (int) ((float) (width - 24) * (((100.0f / (float) (maxstage - 1)) * (float) (stage - 1)) / 100.0f)) + x + 8;

	Draw_Character (x, y, 131);
}


void SCR_QuakeIsLoading (int stage, int maxstage)
{
	// pretend we're fullscreen because we definitely want to hide the mouse
	IN_SetMouseState (true);

	SCR_CalcRefdef ();
	D3DVid_BeginRendering ();

	if (FAILED (hr)) return;

	// if we needed to restart video skip updating this frame
	if (vid_restarted) return;

	D3DHLSL_BeginFrame ();
	D3DDraw_Begin2D ();
	D3DDraw_SetSize (&vid.consize);

	Draw_ConsoleBackground (100);

	extern qpic_t *gfx_loading_lmp;

	int x = (vid.currsize->width - gfx_loading_lmp->width) / 2;
	int y = (vid.currsize->height - 48 - gfx_loading_lmp->height) / 2;

	Draw_Pic (x, y, gfx_loading_lmp, 1, true);

	SCR_DrawSlider (x + 8, y + gfx_loading_lmp->height + 8, gfx_loading_lmp->width - 16, stage, maxstage);

	D3DDraw_End2D ();
	D3DHLSL_EndFrame ();

	D3DVid_EndRendering ();
}

