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

// contains copies of the missing cvars and commands from WinQuake and GLQuake for purposes
// of soaking up abuse from mods that like to dance with the devil that is stuffcmd.
// any other compatibility soaks and suchlike should also go here...
#include "quakedef.h"

cvar_t	vid_bpp ("vid_bpp", "");
cvar_t	vid_fullscreen ("vid_fullscreen", "");
cvar_t	vid_height_compat ("vid_height", "");
cvar_t	vid_refreshrate ("vid_refreshrate", "");
cvar_t	vid_width_compat ("vid_width", "");

cvar_t	windowed_mouse ("_windowed_mouse", "");

cvar_t	cl_maxpitch ("cl_maxpitch", "");
cvar_t	cl_minpitch ("cl_minpitch", "");

cvar_t	gl_farclip ("gl_farclip", "");
cvar_t	max_edicts ("max_edicts", "");

cvar_t	scr_conwidth ("scr_conwidth", "");
cvar_t	sv_altnoclip ("sv_altnoclip", "");

cvar_t	config_com_port ("_config_com_port", "0x3f8");
cvar_t	config_com_irq ("_config_com_irq", "4");
cvar_t	config_com_baud ("_config_com_baud", "57600");
cvar_t	config_com_modem ("_config_com_modem", "1");

cvar_t	config_modem_dialtype ("_config_modem_dialtype", "T");
cvar_t	config_modem_clear ("_config_modem_clear", "ATZ");
cvar_t	config_modem_init ("_config_modem_init", "");
cvar_t	config_modem_hangup ("_config_modem_hangup", "AT H");

// removed all of the stuff cos it was throwing 4189 warnings that i couldn't suppress (not that i tried very hard)
// it was of pretty dubious usefulness anyway

// register compatiblity cvars.  if these already exist as a real cvar they will be just ignored.
// if a real cvar is registered with the same name as a prior compatibility cvar the real cvar will stomp the compatibility cvar
void Cvar_MakeCompatLayer (void)
{
}

// compatiblity commands
// if an incoming registered command has the same name we replace the function
// if one of these has the same name as a previous command we ignore it.
void Cmd_MakeCompatLayer (void)
{
}

