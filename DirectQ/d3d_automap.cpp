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

/*
automap system

the automap isn't MEANT to be particularly robust or performant.  it's intended for use as a quick and dirty "where the fuck am i?" system

to do - highlight player position better + move to rmain
*/

#include "quakedef.h"
#include "d3d_quake.h"

#include <vector>


// use unit scales that are also used in most map editors
cvar_t r_automapscroll_x ("r_automapscroll_x", -64.0f, CVAR_ARCHIVE);
cvar_t r_automapscroll_y ("r_automapscroll_y", -64.0f, CVAR_ARCHIVE);
cvar_t r_automapscroll_z ("r_automapscroll_z", 32.0f, CVAR_ARCHIVE);

bool r_automap;
extern bool scr_drawmapshot;
extern bool scr_drawloading;

// default automap viewpoint
// x and y are updated on entry to the automap
float r_automap_x = 0;
float r_automap_y = 0;
float r_automap_z = 0;
float r_automap_scale = 5;
int automap_key = -1;
int screenshot_key = -1;

void Cmd_ToggleAutomap_f (void)
{
	if (cls.state != ca_connected) return;

	r_automap = !r_automap;

	// toggle a paused state
	if (key_dest == key_automap)
		key_dest = key_game;
	else key_dest = key_automap;

	// find which key is bound to the automap
	automap_key = Key_GetBinding ("toggleautomap");

	// find any other key defs we want to allow
	screenshot_key = Key_GetBinding ("screenshot");

	r_automap_x = r_refdef.vieworg[0];
	r_automap_y = r_refdef.vieworg[1];
	r_automap_z = 0;
}


void Key_Automap (int key)
{
	switch (key)
	{
	case K_PGUP:
		r_automap_z += r_automapscroll_z.value;
		break;

	case K_PGDN:
		r_automap_z -= r_automapscroll_z.value;
		break;

	case K_UPARROW:
		r_automap_y -= r_automapscroll_y.value;
		break;

	case K_DOWNARROW:
		r_automap_y += r_automapscroll_y.value;
		break;

	case K_LEFTARROW:
		r_automap_x += r_automapscroll_x.value;
		break;

	case K_RIGHTARROW:
		r_automap_x -= r_automapscroll_x.value;
		break;

	case K_HOME:
		r_automap_scale -= 0.5f;
		break;

	case K_END:
		r_automap_scale += 0.5f;
		break;

	default:
		if (key == automap_key)
			Cmd_ToggleAutomap_f ();
		else if (key == screenshot_key)
		{
			Cbuf_InsertText ("screenshot\n");
			Cbuf_Execute ();
		}
		break;
	}

	if (r_automap_scale < 0.5) r_automap_scale = 0.5;
	if (r_automap_scale > 20) r_automap_scale = 20;
}


cmd_t Cmd_ToggleAutomap ("toggleautomap", Cmd_ToggleAutomap_f);

int c_automapsurfs = 0;

bool D3D_DrawAutomap (void)
{
	if (!r_automap) return false;
	if (cls.state != ca_connected) return false;
	if (scr_drawmapshot) return false;
	if (scr_drawloading) return false;
	if (cl.intermission == 1 && key_dest == key_game) return false;
	if (cl.intermission == 2 && key_dest == key_game) return false;

	c_automapsurfs = 0;
	return true;
}


void D3D_AutomapReset (void)
{
}

