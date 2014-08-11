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

// screen.c -- master for refresh, status bar, console, chat, notify, etc

#include "quakedef.h"
#include "d3d_quake.h"
#include "d3d_hlsl.h"

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
Need to double buffer?


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


int			glx, gly, glwidth, glheight;

float		scr_con_current;
float		scr_conlines;		// lines of console to display

// timeout when loading plaque is up
#define SCR_DEFTIMEOUT 60
float		scr_timeout;

float		oldscreensize, oldfov, oldconscale, oldhudbgfill;
extern cvar_t		gl_conscale;
cvar_t		scr_viewsize ("viewsize", 100, CVAR_ARCHIVE);
cvar_t		scr_fov ("fov", 90);	// 10 - 170
cvar_t		scr_fovcompat ("fov_compatible", 1, CVAR_ARCHIVE);
cvar_t		scr_conspeed ("scr_conspeed", 3000);
cvar_t		scr_centertime ("scr_centertime", 2);
cvar_t		scr_centerlog ("scr_centerlog", 1, CVAR_ARCHIVE);
cvar_t		scr_showram ("showram", 1);
cvar_t		scr_showturtle ("showturtle", "0");
cvar_t		scr_showpause ("showpause", 1);
cvar_t		scr_printspeed ("scr_printspeed", 20);

cvar_t		scr_screenshotformat ("scr_screenshotformat", "tga", CVAR_ARCHIVE);
cvar_t		scr_screenshotdir ("scr_screenshotdir", "screenshot", CVAR_ARCHIVE);
cvar_t		scr_shotnamebase ("scr_shotnamebase", "Quake", CVAR_ARCHIVE);

cvar_t	r_automapshot ("r_automapshot", "0", CVAR_ARCHIVE);

extern	cvar_t	crosshair;

bool	scr_initialized;		// ready to draw

qpic_t		*scr_ram;
qpic_t		*scr_net;
qpic_t		*scr_turtle;

int			clearconsole;
int			clearnotify;

vrect_t		scr_vrect;

bool	scr_disabled_for_loading;
bool	scr_drawloading;
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
float		scr_centertime_start;	// for slow victory printing
float		scr_centertime_off;
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
	if (!str[0]) return;

	// cheesy centerprint logging - need to do this right sometime!!!
	if (scr_centerlog.integer)
	{
		Con_SilentPrintf ("\n\35\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\37\n");
		Con_SilentPrintf ("%s\n", str);
		Con_SilentPrintf ("\35\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\37\n\n");

		// ensure (required as a standard Con_Printf immediately following a centerprint will screw up the notify lines)
		Con_ClearNotify ();
	}

	strncpy (scr_centerstring, str, sizeof (scr_centerstring) - 1);
	scr_centertime_off = scr_centertime.value;
	scr_centertime_start = cl.time;

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


void SCR_DrawCenterString (void)
{
	char	*start;
	int		l;
	int		j;
	int		x, y;
	int		remaining;

	// the finale prints the characters one at a time
	if (cl.intermission)
		remaining = scr_printspeed.value * (cl.time - scr_centertime_start);
	else
		remaining = 9999;

	scr_erase_center = 0;
	start = scr_centerstring;

	y = (vid.height - (scr_center_lines * 10)) / 3;

	do
	{
		// scan the width of the line
		for (l = 0; l < 70; l++)
			if (start[l] == '\n' || !start[l])
				break;

		x = (vid.width - l * 8) / 2;

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
}

void SCR_CheckDrawCenterString (void)
{
	if (scr_center_lines > scr_erase_lines)
		scr_erase_lines = scr_center_lines;

	scr_centertime_off -= host_frametime;

	if (scr_centertime_off <= 0 && !cl.intermission) return;
	if (key_dest != key_game) return;

	// intermission 2 prints an extended message which we don't want to fade
	if (scr_centertime_off < 1.0f && cl.intermission != 2)
		D3D_Set2DShade (scr_centertime_off);
	else D3D_Set2DShade (1.0f);

	SCR_DrawCenterString ();

	if (scr_centertime_off < 1.0f) D3D_Set2DShade (1.0f);
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

	y = height / tan (fov_y / 360 * M_PI);

	a = atan (width / y);

    a = a * 360 / M_PI;

    return a;
}


float SCR_CalcFovY (float fov_x, float width, float height)
{
	float   a;
	float   x;

	// bound, don't crash
	if (fov_x < 1) fov_x = 1;
	if (fov_x > 179) fov_x = 179;

	x = width / tan (fov_x / 360 * M_PI);

	a = atan (height / x);

    a = a * 360 / M_PI;

    return a;
}


/*
=================
SCR_CalcRefdef

Must be called whenever vid changes
Internal use only
=================
*/
extern cvar_t hud_overlay;

static void SCR_CalcRefdef (void)
{
	float		size;
	int		h;
	bool		full = false;

	vid.recalc_refdef = 0;

	// bound viewsize
	if (scr_viewsize.value < 30) Cvar_Set ("viewsize","30");
	if (scr_viewsize.value > 120) Cvar_Set ("viewsize","120");

	// bound field of view
	if (scr_fov.value < 10) Cvar_Set ("fov","10");
	if (scr_fov.value > 170) Cvar_Set ("fov","170");

	// intermission is always full screen
	if (cl.intermission)
		size = 120;
	else
		size = scr_viewsize.value;

	if (size >= 120)
		sb_lines = 0;		// no status bar at all
	else if (size >= 110)
		sb_lines = 24;		// no inventory
	else
		sb_lines = 24 + 16 + 8;

	if (scr_viewsize.value >= 100.0)
	{
		full = true;
		size = 100.0;
	}
	else size = scr_viewsize.value;

	if (cl.intermission)
	{
		full = true;
		size = 100;
		sb_lines = 0;
	}

	// draw HUD as an overlay rather than as a separate component
	if (hud_overlay.value) sb_lines = 0;

	size /= 100.0;

	// bound console scale
	if (gl_conscale.value < 0) Cvar_Set ("gl_conscale", "0");
	if (gl_conscale.value > 1) Cvar_Set ("gl_conscale", "1");

	// recalculate vid.width and vid.height
	vid.width = (glwidth - vid.conwidth) * gl_conscale.value + vid.conwidth;
	vid.height = (glheight - vid.conheight) * gl_conscale.value + vid.conheight;

	h = vid.height - sb_lines;

	r_refdef.vrect.width = vid.width * size;

	if (r_refdef.vrect.width < 96)
	{
		size = 96.0 / r_refdef.vrect.width;
		r_refdef.vrect.width = 96;	// min for icons
	}

	r_refdef.vrect.height = vid.height * size;
	if (r_refdef.vrect.height > vid.height - sb_lines) r_refdef.vrect.height = vid.height - sb_lines;
	if (r_refdef.vrect.height > vid.height) r_refdef.vrect.height = vid.height;

	r_refdef.vrect.x = (vid.width - r_refdef.vrect.width) / 2;

	if (full)
		r_refdef.vrect.y = 0;
	else 
		r_refdef.vrect.y = (h - r_refdef.vrect.height) / 2;

	// x fov is initially the selected value
	r_refdef.fov_x = scr_fov.value;

	if (scr_fov.integer == 90 || !scr_fovcompat.integer)
	{
		// calculate y fov as if the screen was 640 x 432; this ensures that the top and bottom
		// doesn't get clipped off if we have a widescreen display (also keeps the same amount of the viewmodel visible)
		// use 640 x 432 to keep fov_y constant for different values of scr_viewsize too. ;)
		r_refdef.fov_y = SCR_CalcFovY (r_refdef.fov_x, 640, 432);

		// now recalculate fov_x so that it's correctly proportioned for fov_y
		r_refdef.fov_x = SCR_CalcFovX (r_refdef.fov_y, r_refdef.vrect.width, r_refdef.vrect.height);
	}
	else
	{
		// the user wants their own FOV.  this may not be correct in terms of yfov, but it's at least
		// consistent with GLQuake and other engines
		r_refdef.fov_y = SCR_CalcFovY (r_refdef.fov_x, r_refdef.vrect.width, r_refdef.vrect.height);
	}

	scr_vrect = r_refdef.vrect;
}


/*
=================
SCR_SizeUp_f

Keybinding command
=================
*/
void SCR_SizeUp_f (void)
{
	Cvar_Set ("viewsize",scr_viewsize.value+10);
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
	Cvar_Set ("viewsize",scr_viewsize.value-10);
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
	scr_ram = Draw_PicFromWad ("ram");
	scr_net = Draw_PicFromWad ("net");
	scr_turtle = Draw_PicFromWad ("turtle");

	scr_initialized = true;
}



/*
==============
SCR_DrawRam
==============
*/
void SCR_DrawRam (void)
{
	if (!scr_showram.value)
		return;

	if (!r_cache_thrash)
		return;

	Draw_Pic (scr_vrect.x+32, scr_vrect.y, scr_ram);
}


/*
==============
SCR_DrawTurtle
==============
*/
void SCR_DrawTurtle (void)
{
	static int	count;
	
	if (!scr_showturtle.value)
		return;

	if (host_frametime < 0.1)
	{
		count = 0;
		return;
	}

	count++;
	if (count < 3)
		return;

	Draw_Pic (scr_vrect.x, scr_vrect.y, scr_turtle);
}

/*
==============
SCR_DrawNet
==============
*/
void SCR_DrawNet (void)
{
	if (realtime - cl.last_received_message < 0.3)
		return;
	if (cls.demoplayback)
		return;

	Draw_Pic (scr_vrect.x + 64, scr_vrect.y, scr_net);
}

/*
==============
DrawPause
==============
*/
void SCR_DrawPause (void)
{
	qpic_t	*pic;

	if (!scr_showpause.value)		// turn off for screenshots
		return;

	if (!cl.paused)
		return;

	pic = Draw_CachePic ("gfx/pause.lmp");

	Draw_Pic ((vid.width - pic->width) / 2, (vid.height - 48 - pic->height) / 2, pic);
}


/*
==============
SCR_DrawLoading
==============
*/
void SCR_DrawLoading (void)
{
	qpic_t	*pic;

	if (!scr_drawloading)
		return;
		
	pic = Draw_CachePic ("gfx/loading.lmp");
	Draw_Pic ( (vid.width - pic->width)/2, 
		(vid.height - 48 - pic->height)/2, pic);
}



//=============================================================================


/*
==================
SCR_SetUpToDrawConsole
==================
*/
void SCR_SetUpToDrawConsole (void)
{
	Con_CheckResize ();
	
	if (scr_drawloading)
		return;		// never a console with loading plaque

	// decide on the height of the console
	con_forcedup = !cl.worldmodel || cls.signon != SIGNONS;

	if (con_forcedup)
	{
		scr_conlines = vid.height;		// full screen
		scr_con_current = scr_conlines;
	}
	else if (key_dest == key_console)
		scr_conlines = vid.height/2;	// half screen
	else
		scr_conlines = 0;				// none visible

	if (scr_conlines < scr_con_current)
	{
		scr_con_current -= scr_conspeed.value*host_frametime;
		if (scr_conlines > scr_con_current)
			scr_con_current = scr_conlines;

	}
	else if (scr_conlines > scr_con_current)
	{
		scr_con_current += scr_conspeed.value*host_frametime;
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
	if (scr_con_current)
	{
		Con_DrawConsole (scr_con_current, true);
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


// d3dx doesn't support tga writes (BASTARDS) so we made our own
void SCR_WriteSurfaceToTGA (char *filename, LPDIRECT3DSURFACE9 rts)
{
	D3DSURFACE_DESC surfdesc;
	D3DLOCKED_RECT lockrect;
	LPDIRECT3DSURFACE9 surf;

	// get the surface description
	rts->GetDesc (&surfdesc);

	// create a surface to copy to
	d3d_Device->CreateOffscreenPlainSurface
	(
		surfdesc.Width,
		surfdesc.Height,
		surfdesc.Format,
		D3DPOOL_SYSTEMMEM,
		&surf,
		NULL
	);

	// copy from the rendertarget to system memory
	d3d_Device->GetRenderTargetData (rts, surf);

	// lock the surface rect
	HRESULT hr = surf->LockRect (&lockrect, NULL, 0);

	if (FAILED (hr)) return;

	// try to open it
	FILE *f = fopen (filename, "wb");

	// didn't work
	if (!f) return;

	// allocate space for the header
	byte buffer[18];
	memset (buffer, 0, 18);

	// compose the header
	buffer[2] = 2;
	buffer[12] = surfdesc.Width & 255;
	buffer[13] = surfdesc.Width >> 8;
	buffer[14] = surfdesc.Height & 255;
	buffer[15] = surfdesc.Height >> 8;
	buffer[16] = 24;
	buffer[17] = 0x20;

	// write out the header
	fwrite (buffer, 18, 1, f);

	// do each RGB triplet individually as we want to reduce from 32 bit to 24 bit
	for (int i = 0; i < surfdesc.Width * surfdesc.Height; i++)
	{
		// retrieve the data
		byte *data = (byte *) &((unsigned *) lockrect.pBits)[i];

		// write it out
		fwrite (data, 3, 1, f);
	}

	// unlock it
	surf->UnlockRect ();
	surf->Release ();

	// done
	fclose (f);
}


/* 
================== 
SCR_ScreenShot_f
================== 
*/
void SCR_ScreenShot_f (void) 
{
	byte		*buffer;
	char		checkname[MAX_OSPATH];
	int			i, c, temp;

	// check the screenshot directory
	COM_CheckContentDirectory (&scr_screenshotdir, true);

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

	if (!stricmp (scr_screenshotformat.string, "bmp"))
		ssfmt = D3DXIFF_BMP;
	else if (!stricmp (scr_screenshotformat.string, "tga"))
		ssfmt = D3DXIFF_TGA;
	else if (!stricmp (scr_screenshotformat.string, "jpg"))
		ssfmt = D3DXIFF_JPG;
	else if (!stricmp (scr_screenshotformat.string, "png"))
		ssfmt = D3DXIFF_PNG;
	else if (!stricmp (scr_screenshotformat.string, "dds"))
		ssfmt = D3DXIFF_DDS;
	else
	{
		// unimplemented
		Con_Printf ("Unimplemented format \"%s\": defaulting to \"TGA\" (D3DXIFF_TGA)\n", scr_screenshotformat.string);
		ssfmt = D3DXIFF_TGA;
		Cvar_Set (&scr_screenshotformat, "tga");
	}

	// find a file name to save it to 
	for (i = 0; i <= 9999; i++)
	{
		sprintf (checkname, "%s/%s/%s%04i.%s", com_gamedir, scr_screenshotdir.string, scr_shotnamebase.string, i, scr_screenshotformat.string);

		// file doesn't exist
		if (Sys_FileTime (checkname) == -1) break;
	} 

	if (i == 10000) 
	{
		Con_Printf ("SCR_ScreenShot_f: 9999 Screenshots exceeded.\n"); 
		return;
 	}

	// hack - get rid of the console notify lines and refresh the screen before taking a screenshot
	Con_ClearNotify ();
	SCR_UpdateScreen ();

	// the surface we'll use
	LPDIRECT3DSURFACE9 Surf;

	// get the backbuffer (note - this might be a render to texture surf if it's underwater!!!)
	d3d_Device->GetRenderTarget (0, &Surf);

	// write to file
	// d3dx doesn't support tga writes (BASTARDS) so we made our own
	if (ssfmt == D3DXIFF_TGA)
		SCR_WriteSurfaceToTGA (checkname, Surf);
	else D3DXSaveSurfaceToFile (checkname, ssfmt, Surf, NULL, NULL);

	// not releasing is a memory leak!!!
	Surf->Release ();

	// report
	Con_Printf ("Wrote %s\n", checkname);
}


void R_RenderScene (void);
void Draw_InvalidateMapshot (void);

void SCR_Mapshot_f (char *shotname, bool report, bool overwrite)
{
	char workingname[256];

	// copy the name out so that we can safely modify it
	strncpy (workingname, shotname, 255);

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
	d3d_Device->GetRenderTarget (0, &d3d_CurrentRenderTarget);

	// create a surface for the mapshot to render to
	d3d_Device->CreateRenderTarget (d3d_CurrentMode.Width, d3d_CurrentMode.Height, D3DFMT_X8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &d3d_MapshotRenderSurf, NULL);

	// create a surface for the final mapshot destination
	d3d_Device->CreateRenderTarget (128, 128, D3DFMT_X8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &d3d_MapshotFinalSurf, NULL);

	// set the render target for the mapshot
	HRESULT hr = d3d_Device->SetRenderTarget (0, d3d_MapshotRenderSurf);

	// go into mapshot mode
	scr_drawmapshot = true;

	// do a screen refresh to get rid of any UI/etc
	SCR_UpdateScreen ();

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

	// save the surface out to file (use PNG)
	SCR_WriteSurfaceToTGA (workingname, d3d_MapshotFinalSurf);
	//D3DXSaveSurfaceToFile (workingname, D3DXIFF_PNG, d3d_MapshotFinalSurf, NULL, NULL);

	// reset to the original render target
	d3d_Device->SetRenderTarget (0, d3d_CurrentRenderTarget);

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
	if (!cl.worldmodel) return;
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

void SCR_BeginLoadingPlaque (void)
{
	S_StopAllSounds (true);

	if (cls.state != ca_connected) return;
	if (cls.signon != SIGNONS) return;

	// redraw with no console and the loading plaque
	Con_ClearNotify ();
	scr_centertime_off = 0;
	scr_con_current = 0;

	// switch viewsize to 120
	// (value was saved out in SCR_CalcRefdef)
	saved_viewsize = scr_viewsize.value;
	Cvar_Set (&scr_viewsize, 120);

	scr_drawloading = true;
	SCR_UpdateScreen ();
	scr_drawloading = false;

	scr_disabled_for_loading = true;
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
	scr_disabled_for_loading = false;
	Con_ClearNotify ();

	// restore viewsize
	if (saved_viewsize) Cvar_Set (&scr_viewsize, saved_viewsize);
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


bool scr_drawdialog = false;
char *scr_notifytext = NULL;
char *scr_notifycaption = NULL;
int scr_notifyflags = 0;

void SCR_DrawNotifyString (char *text, char *caption, int flags)
{
	int		y;

	char *lines[64] = {NULL};
	int scr_modallines = 0;
	char *textbuf = (char *) malloc (strlen (text) + 1);
	strcpy (textbuf, text);

	lines[0] = textbuf;

	// count the number of lines
	for (int i = 0; ; i++)
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

	for (int i = 0; ; i++)
	{
		if (!lines[i]) break;
		if (strlen (lines[i]) > maxline) maxline = strlen (lines[i]);
	}

	// caption might be longer...
	if (strlen (caption) > maxline) maxline = strlen (caption);

	// adjust positioning
	y = (vid.height - ((scr_modallines + 5) * 10)) / 3;

	// fade out background
	Draw_FadeScreen ();

	// background
	Draw_TextBox ((vid.width - (maxline * 8)) / 2 - 16, y - 12, maxline * 8 + 16, (scr_modallines + 5) * 10 - 5);
	Draw_TextBox ((vid.width - (maxline * 8)) / 2 - 16, y - 12, maxline * 8 + 16, 15);

	// draw caption
	Draw_String ((vid.width - (strlen (caption) * 8)) / 2, y, caption);

	y += 20;

	for (int i = 0; ; i++)
	{
		if (!lines[i]) break;

		for (int s = 0; s < strlen (lines[i]); s++)
			lines[i][s] += 128;

		Draw_String ((vid.width - strlen (lines[i]) * 8) / 2, y, lines[i]);
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

	if (prompt) Draw_String ((vid.width - strlen (prompt) * 8) / 2, y + 5, prompt);

	free (textbuf);
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
	// draw a fresh screen
	// fixme - the renderer needs to be reworked here...
	scr_drawdialog = true;

	scr_notifytext = text;
	scr_notifycaption = caption;
	scr_notifyflags = flags;

	SCR_UpdateScreen ();

	scr_drawdialog = false;

	/*
	this needs the reworked renderer - it's too messy and unreliable without it...
	D3D_Set2D ();
	Draw_FadeScreen ();
	SCR_DrawNotifyString (text, caption, flags);
	d3d_Flat2DFX->EndPass ();
	d3d_Flat2DFX->End ();
	d3d_Device->EndScene ();
	d3d_Device->Present (NULL, NULL, NULL, NULL);
	*/

	S_ClearBuffer ();		// so dma doesn't loop current sound

	bool key_accept = false;

	do
	{
		key_count = -1;		// wait for a key down and up
		Sys_SendKeyEvents ();

		if (key_lastpress == K_ESCAPE) {key_accept = false; break;}

		if (flags == MB_OK)
		{
			if (key_lastpress == 'o' || key_lastpress == 'O') {key_accept = true; break;}
		}
		else if (flags == MB_YESNO)
		{
			if (key_lastpress == 'y' || key_lastpress == 'Y') {key_accept = true; break;}
			if (key_lastpress == 'n' || key_lastpress == 'N') {key_accept = false; break;}
		}
		else if (flags == MB_OKCANCEL)
		{
			if (key_lastpress == 'o' || key_lastpress == 'O') {key_accept = true; break;}
			if (key_lastpress == 'c' || key_lastpress == 'C') {key_accept = false; break;}
		}
		else if (flags == MB_RETRYCANCEL)
		{
			if (key_lastpress == 'r' || key_lastpress == 'R') {key_accept = true; break;}
			if (key_lastpress == 'c' || key_lastpress == 'C') {key_accept = false; break;}
		}
		else
		{
			if (key_lastpress == 'o' || key_lastpress == 'O') {key_accept = true; break;}
		}
	} while (1);

	SCR_UpdateScreen ();

	return key_accept;
}


void SCR_SyncRender (bool syncbegin)
{
	d3d_Device->EndScene ();
	d3d_Device->Present (NULL, NULL, NULL, NULL);

	if (syncbegin) d3d_Device->BeginScene ();
}


//=============================================================================

/*
===============
SCR_BringDownConsole

Brings the console down and fades the palettes back to normal
================
*/
void SCR_BringDownConsole (void)
{
	int		i;
	
	scr_centertime_off = 0;
	
	for (i=0 ; i<20 && scr_conlines != scr_con_current ; i++)
		SCR_UpdateScreen ();

	cl.cshifts[0].percent = 0;		// no area contents palette on next frame
	VID_SetPalette (host_basepal);
}

void SCR_TileClear (void)
{
	if (r_refdef.vrect.x > 0)
	{
		// left
		Draw_TileClear
		(
			0,
			0,
			r_refdef.vrect.x,
			vid.height - sb_lines
		);

		// right
		Draw_TileClear
		(
			r_refdef.vrect.x + r_refdef.vrect.width,
			0, 
			vid.width - r_refdef.vrect.x + r_refdef.vrect.width, 
			vid.height - sb_lines
		);
	}

	if (r_refdef.vrect.y > 0)
	{
		// top
		Draw_TileClear
		(
			r_refdef.vrect.x,
			0, 
			r_refdef.vrect.x + r_refdef.vrect.width, 
			r_refdef.vrect.y
		);

		// bottom
		Draw_TileClear
		(
			r_refdef.vrect.x,
			r_refdef.vrect.y + r_refdef.vrect.height, 
			r_refdef.vrect.width, 
			vid.height - sb_lines - (r_refdef.vrect.height + r_refdef.vrect.y)
		);
	}
}

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
void HUD_FinaleOverlay (void);

void SCR_UpdateScreen (void)
{
	extern bool d3d_DeviceLost;

	if (block_drawing) return;

	if (scr_disabled_for_loading)
	{
		if (realtime - scr_disabled_time > scr_timeout)
		{
			scr_disabled_for_loading = false;

			if (scr_timeout >= SCR_DEFTIMEOUT) Con_Printf ("load failed.\n");
		}
		else return;
	}

	// not initialized yet
	if (!scr_initialized || !con_initialized || !d3d_Device) return;

	// begin rendering; get the size of the refresh window and set up for the render
	// this is also used for lost device recovery mode
	D3D_BeginRendering (&glx, &gly, &glwidth, &glheight);

	// if we've just lost the device we're going into recovery mode, so don't draw anything
	if (d3d_DeviceLost) return;

	// determine size of refresh window
	if (oldfov != scr_fov.value)
	{
		oldfov = scr_fov.value;
		vid.recalc_refdef = true;
	}

	if (oldconscale != gl_conscale.value)
	{
		oldconscale = gl_conscale.value;
		vid.recalc_refdef = true;
	}

	if (oldscreensize != scr_viewsize.value)
	{
		oldscreensize = scr_viewsize.value;
		vid.recalc_refdef = true;
	}

	if (oldhudbgfill != hud_overlay.value)
	{
		oldhudbgfill = hud_overlay.value;
		vid.recalc_refdef = true;
	}

	if (vid.recalc_refdef) SCR_CalcRefdef ();

	// do 3D refresh drawing, and then update the screen
	SCR_SetUpToDrawConsole ();
	
	V_RenderView ();

	D3D_Set2D ();

	// draw any areas not covered by the refresh
	SCR_TileClear ();

	if (scr_drawloading)
	{
		SCR_DrawLoading ();
		// removed because it looks WRONG
		// HUD_DrawHUD ();
	}
	else if (cl.intermission == 1 && key_dest == key_game)
	{
		HUD_IntermissionOverlay ();
	}
	else if (cl.intermission == 2 && key_dest == key_game)
	{
		HUD_FinaleOverlay ();
		SCR_CheckDrawCenterString ();
	}
	else if (!scr_drawmapshot)
	{
		SCR_DrawRam ();
		SCR_DrawNet ();
		SCR_DrawTurtle ();
		SCR_DrawPause ();
		SCR_CheckDrawCenterString ();
		HUD_DrawHUD ();
		SCR_DrawConsole ();	
		M_Draw ();
	}

	// this should always be drawn as an overlay to what's currently on screen
	if (scr_drawdialog) SCR_DrawNotifyString (scr_notifytext, scr_notifycaption, scr_notifyflags);

	V_UpdatePalette ();

	d3d_Flat2DFX.EndRender ();
	D3D_EndRendering ();

	// take a mapshot on entry to the map, unless one already exists
	// unless we're already in mapshot mode, in which case we'll have an infinite loop!!!
	if (r_automapshot.value && r_framecount == 5 && !scr_drawmapshot)
	{
		// first ensure we have a "maps" directory
		CreateDirectory (va ("%s/maps", com_gamedir), NULL);

		// now take the mapshot; don't overwrite if one is already there
		SCR_Mapshot_f (va ("%s/%s", com_gamedir, cl.worldmodel->name), false, false);
	}
}
