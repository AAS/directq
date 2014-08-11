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
// r_main.c

#include "quakedef.h"
#include "d3d_model.h"
#include "d3d_quake.h"

float d3d_FogColor[4] = {0.3, 0.3, 0.3, 0};
float d3d_FogDensity = 0;
char lastworldmodel[64] = {0};


void D3DFog_UpdateCvars (cvar_t *var);

cvar_t gl_fogenable ("gl_fogenable", 0.0f, 0, D3DFog_UpdateCvars);
cvar_t gl_fogred ("gl_fogred", 0.3f, 0, D3DFog_UpdateCvars);
cvar_t gl_foggreen ("gl_foggreen", 0.3f, 0, D3DFog_UpdateCvars);
cvar_t gl_fogblue ("gl_fogblue", 0.3f, 0, D3DFog_UpdateCvars);
cvar_t gl_fogdensity ("gl_fogdensity", 0.0f, 0, D3DFog_UpdateCvars);
cvar_t gl_fogdensityscale ("gl_fogdensityscale", 0.0f);

bool fog_LockCvars = false;

void D3DFog_UpdateCvars (cvar_t *var)
{
	// testing stuff to see if we can figure out just WTF Nehahra is doing
	// Con_Printf ("%s set to %s\n", var->name, var->string);

	// fog from worldspawn needs to lock the cvars in order to prevent them from wiping stuff while loading
	if (fog_LockCvars) return;

	// copy the cvars out to our values
	if (gl_fogenable.value && gl_fogdensity.value)
	{
		d3d_FogDensity = gl_fogdensity.value;
		d3d_FogColor[0] = gl_fogred.value;
		d3d_FogColor[1] = gl_foggreen.value;
		d3d_FogColor[2] = gl_fogblue.value;
		d3d_FogColor[3] = 0;
	}
	else
	{
		d3d_FogDensity = 0;
		d3d_FogColor[0] = 0;
		d3d_FogColor[1] = 0;
		d3d_FogColor[2] = 0;
		d3d_FogColor[3] = 0;
	}
}


void Fog_Update (float density, float r, float g, float b)
{
	d3d_FogDensity = density > 1 ? 1 : (density < 0 ? 0 : density);

	if ((r > 1 && g > 1) || (r > 1 && b > 1) || (g > 1 && b > 1))
	{
		// fucking content bug workaround for maps that send fog on a 0..255 scale
		r /= 255.0f;
		g /= 255.0f;
		b /= 255.0f;
	}

	d3d_FogColor[0] = r > 1 ? 1 : (r < 0 ? 0 : r);
	d3d_FogColor[1] = g > 1 ? 1 : (g < 0 ? 0 : g);
	d3d_FogColor[2] = b > 1 ? 1 : (b < 0 ? 0 : b);
	d3d_FogColor[3] = 0;

	// fog from worldspawn needs to lock the cvars in order to prevent them from wiping stuff while loading
	fog_LockCvars = true;

	// update cvars
	if (d3d_FogDensity > 0)
	{
		Cvar_Set (&gl_fogenable, 1.0f);
		Cvar_Set (&gl_fogdensity, d3d_FogDensity);
	}
	else Cvar_Set (&gl_fogenable, 0.0f);

	Cvar_Set (&gl_fogred, d3d_FogColor[0]);
	Cvar_Set (&gl_foggreen, d3d_FogColor[1]);
	Cvar_Set (&gl_fogblue, d3d_FogColor[2]);

	// now unlock the cvars and do another update to sync things up right
	fog_LockCvars = false;
	Cvar_Set (&gl_fogenable, gl_fogenable.value);
}


void Fog_ParseWorldspawn (void)
{
	char key[128], value[4096];
	char *data;

	// if we're on the same map as before we keep the old settings, otherwise we wipe them
	if (!strcmp (lastworldmodel, cl.worldmodel->name))
		return;

	// always wipe density
	d3d_FogDensity = 0.0;

	// to do - possibly change these depending on worldtype?????
	// should we even wipe these?  or leave them as the player set them???
	Fog_Update (0, 0.3, 0.3, 0.3);

	data = COM_Parse (cl.worldmodel->brushhdr->entities);
	strcpy (lastworldmodel, cl.worldmodel->name);

	if (!data) return;
	if (com_token[0] != '{') return;

	while (1)
	{
		if (!(data = COM_Parse (data))) return;
		if (com_token[0] == '}') return;

		if (com_token[0] == '_')
			strcpy (key, com_token + 1);
		else strcpy (key, com_token);

		while (key[strlen (key) - 1] == ' ') // remove trailing spaces
			key[strlen (key) - 1] = 0;

		if (!(data = COM_Parse (data))) return; // error

		strcpy (value, com_token);

		if (!strcmp ("fog", key))
		{
			sscanf (value, "%f %f %f %f", &d3d_FogDensity, &d3d_FogColor[0], &d3d_FogColor[1], &d3d_FogColor[2]);
			Fog_Update (d3d_FogDensity, d3d_FogColor[0], d3d_FogColor[1], d3d_FogColor[2]);
		}
	}
}


void Fog_ParseServerMessage (void)
{
	float density, red, green, blue, time;

	density = MSG_ReadByte () / 255.0;
	red = MSG_ReadByte () / 255.0;
	green = MSG_ReadByte () / 255.0;
	blue = MSG_ReadByte () / 255.0;

	// swallow a short (time)
	MSG_ReadShort ();

	Fog_Update (density, red, green, blue);
}


void D3D_Fog_f (void)
{
	switch (Cmd_Argc ())
	{
	default:
	case 1:
		Con_Printf ("usage:\n");
		Con_Printf ("   fog <density>\n");
		Con_Printf ("   fog <red> <green> <blue>\n");
		Con_Printf ("   fog <density> <red> <green> <blue>\n");
		Con_Printf ("current values:\n");
		Con_Printf ("   \"density\" is \"%g\"\n", d3d_FogDensity);
		Con_Printf ("   \"red\" is \"%g\"\n", d3d_FogColor[0]);
		Con_Printf ("   \"green\" is \"%g\"\n", d3d_FogColor[1]);
		Con_Printf ("   \"blue\" is \"%g\"\n", d3d_FogColor[2]);
		break;

	case 2: Fog_Update (atof (Cmd_Argv (1)), d3d_FogColor[0], d3d_FogColor[1], d3d_FogColor[2]); break;
	case 4: Fog_Update (d3d_FogDensity, atof (Cmd_Argv (1)), atof (Cmd_Argv (2)), atof (Cmd_Argv (3))); break;
	case 5: Fog_Update (atof (Cmd_Argv (1)), atof (Cmd_Argv (2)), atof (Cmd_Argv (3)), atof (Cmd_Argv (4))); break;
	}
}

cmd_t fogCmd ("fog", D3D_Fog_f);
