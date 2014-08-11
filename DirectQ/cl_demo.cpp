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
#include "d3d_model.h"
#include "d3d_quake.h"

// JPG 1.05 - support for recording demos after connecting to the server
byte	demo_head[3][MAX_MSGLEN];
int		demo_head_size[2];

static int td_frames = 0;

static HANDLE demohandle = INVALID_HANDLE_VALUE;

static long demofile_len, demofile_start;

void CL_FinishTimeDemo (void);

/*
==============================================================================

DEMO CODE

When a demo is playing back, all NET_SendMessages are skipped, and
NET_GetMessages are read from the demo file.

Whenever cl.time gets past the last received message, another message is
read from the demo file.
==============================================================================
*/

/*
====================
CL_CloseDemoFile
====================
*/
void CL_CloseDemoFile (void)
{
	if (demohandle == INVALID_HANDLE_VALUE)
		return;

	COM_FCloseFile (&demohandle);
	demohandle = INVALID_HANDLE_VALUE;
}


void SCR_SetTimeout (float timeout);

/*
==============
CL_StopPlayback

Called when a demo file runs out, or the user starts a game
==============
*/
void CL_StopPlayback (void)
{
	if (!cls.demoplayback)
		return;

	cls.demoplayback = false;
	CL_CloseDemoFile ();
	cls.state = ca_disconnected;

	// Make sure screen is updated shortly after this
	SCR_SetTimeout (0);

	if (cls.timedemo) CL_FinishTimeDemo ();
}

/*
====================
CL_WriteDemoMessage

Dumps the current net message, prefixed by the length and view angles
====================
*/
bool CL_WriteDemoMessage (void)
{
	int	    len;
	int	    i;
	float	    f;
	bool    Success;

	len = net_message.cursize;
	Success = (COM_FWriteFile (demohandle, &len, 4) == 1);

	for (i = 0; i < 3 && Success; i++)
	{
		f = cl.viewangles[i];
		Success = (COM_FWriteFile (demohandle, &f, 4) == 1);
	}

	if (Success)
		Success = (COM_FWriteFile (demohandle, net_message.data, net_message.cursize) == 1);

	if (Success)
		; // flush buffer
	else
	{
		CL_CloseDemoFile ();
		Con_Printf ("Error writing demofile\n");
	}

	return Success;
}

/*
====================
CL_GetMessage

Handles recording and playback of demos, on top of NET_ code
====================
*/
int CL_GetMessage (void)
{
	int	    r, i;
	float	    f;
	bool    Success;

	if (cls.demoplayback)
	{
		// decide if it is time to grab the next message
		if (cls.signon == SIGNON_CONNECTED)	// allways grab until fully connected
		{
			if (cls.timedemo)
			{
				// allready read this frame's message
				if (d3d_RenderDef.framecount == cls.td_currframe) return 0;

				cls.td_currframe = d3d_RenderDef.framecount;

				// if this is the third frame, grab the real td_starttime
				// so the bogus time on the first frame doesn't count
				if (td_frames == 2) cls.td_starttime = realtime;

				td_frames++;
			}
			else
			{
				if (cl.time <= cl.mtime[0]) return 0;
			}
		}

		// Detect EOF, especially for demos in pak files
		if (SetFilePointer (demohandle, 0, NULL, FILE_CURRENT) - demofile_start >= demofile_len)
			Host_EndGame ("Missing disconnect in demofile\n");

		// get the next message
		Success = (COM_FReadFile (demohandle, &net_message.cursize, 4) != -1);

		VectorCopy2 (cl.mviewangles[1], cl.mviewangles[0]);

		for (i = 0; i < 3 && Success; i++)
		{
			Success = (COM_FReadFile (demohandle, &f, 4) != -1);
			cl.mviewangles[0][i] = f;
		}

		if (Success)
		{
			if (net_message.cursize > MAX_MSGLEN)
				Host_Error ("Demo message %d > MAX_MSGLEN (%d)", net_message.cursize, MAX_MSGLEN);

			Success = (COM_FReadFile (demohandle, net_message.data, net_message.cursize) != -1);
		}

		if (!Success)
		{
			Con_Printf ("Error reading demofile\n");
			CL_Disconnect ();
			return 0;
		}

		return 1;
	}

	while (1)
	{
		r = NET_GetMessage (cls.netcon);

		if (r != 1 && r != 2)
			return r;

		// discard nop keepalive message
		if (net_message.cursize == 1 && net_message.data[0] == svc_nop)
			Con_Printf ("<-- server to client keepalive\n");
		else break;
	}

	if (cls.demorecording)
	{
		if (!CL_WriteDemoMessage ())
			return -1; // File write failure
	}

	// JPG 1.05 - support for recording demos after connecting
	if (cls.signon < 2)
	{
		memcpy (demo_head[cls.signon], net_message.data, net_message.cursize);
		demo_head_size[cls.signon] = net_message.cursize;

		if (!cls.signon)
		{
			char *ch = (char *) (demo_head[0] + demo_head_size[0]);

			*ch++ = svc_print;

			ch += 1 + sprintf
			(
				ch,
				"\n\35\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\37\n"
				"\nRecorded on DirectQ Release "DIRECTQ_VERSION"\n"
				"\n\35\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\37\n\n"
			);

			demo_head_size[0] = ch - (char *) demo_head[0];
		}
	}

	return r;
}


/*
====================
CL_Stop_f

stop recording a demo
====================
*/
void CL_Stop_f (void)
{
	if (cmd_source != src_command)
		return;

	if (!cls.demorecording)
	{
		Con_Printf ("Not recording a demo.\n");
		return;
	}

	if (demohandle != INVALID_HANDLE_VALUE)
	{
		// write a disconnect message to the demo file
		SZ_Clear (&net_message);
		MSG_WriteByte (&net_message, svc_disconnect);
		CL_WriteDemoMessage ();

		// finish up
		CL_CloseDemoFile ();
	}

	cls.demorecording = false;
	Con_Printf ("Completed demo\n");
}


void Menu_DemoPopulate (void);

/*
====================
CL_Record_f

record <demoname> <map> [cd track]
====================
*/
void CL_Record_f (void)
{
	int		c;
	char	name[MAX_PATH];
	int		track;

	if (cmd_source != src_command)
		return;

	c = Cmd_Argc();

	if (c != 2 && c != 3 && c != 4)
	{
		Con_Printf ("record <demoname> [<map> [cd track]]\n");
		return;
	}

	if (strstr (Cmd_Argv (1), ".."))
	{
		Con_Printf ("Relative pathnames are not allowed.\n");
		return;
	}

	if (cls.demoplayback)
	{
		Con_Printf ("Can't record during demo playback\n");
		return;
	}

	if (c == 2 && cls.state == ca_connected && cls.signon < 2)
	{
		Con_Printf ("Can't record - try again when connected\n");
		return;
	}

	// stop any currently recording demo
	if (cls.demorecording) CL_Stop_f ();

	// write the forced cd track number, or -1
	if (c == 4)
	{
		track = atoi (Cmd_Argv (3));

		// bug - this was cls.forcetrack
		Con_Printf ("Forcing CD track to %i\n", track);
	}
	else track = -1;

	_snprintf (name, 128, "%s/%s", com_gamedir, Cmd_Argv (1));

	// start the map up
	if (c > 2)
	{
		Cmd_ExecuteString (va ("map %s", Cmd_Argv (2)), src_command);

		// if we couldn't find the map we abort recording
		if (cls.state != ca_connected) return;
	}

	// open the demo file
	if (demohandle != INVALID_HANDLE_VALUE)
	{
		COM_FCloseFile (&demohandle);
		demohandle = INVALID_HANDLE_VALUE;
	}

	COM_DefaultExtension (name, ".dem");

	Con_Printf ("recording to %s.\n", name);
	demohandle = CreateFile (name, FILE_WRITE_DATA, 0, NULL, CREATE_ALWAYS, 0, NULL);

	if (demohandle == INVALID_HANDLE_VALUE)
	{
		Con_Printf ("ERROR: couldn't open.\n");
		return;
	}

	cls.forcetrack = track;

	char demotrack[64];

	sprintf (demotrack, "%i\n", cls.forcetrack);
	COM_FWriteFile (demohandle, demotrack, strlen (demotrack));

	cls.demorecording = true;

	// initialize the demo file if we're already connected
	if (c < 3 && cls.state == ca_connected)
	{
		byte *data = net_message.data;
		int	i, cursize = net_message.cursize;

		for (i = 0; i < 2; i++)
		{
			net_message.data = demo_head[i];
			net_message.cursize = demo_head_size[i];
			CL_WriteDemoMessage ();
		}

		net_message.data = demo_head[2];
		SZ_Clear (&net_message);

		// current names, colors, and frag counts
		for (i = 0; i < cl.maxclients; i++)
		{
			MSG_WriteByte (&net_message, svc_updatename);
			MSG_WriteByte (&net_message, i);
			MSG_WriteString (&net_message, cl.scores[i].name);
			MSG_WriteByte (&net_message, svc_updatefrags);
			MSG_WriteByte (&net_message, i);
			MSG_WriteShort (&net_message, cl.scores[i].frags);
			MSG_WriteByte (&net_message, svc_updatecolors);
			MSG_WriteByte (&net_message, i);
			MSG_WriteByte (&net_message, cl.scores[i].colors);
		}

		// send all current light styles
		for (i = 0; i < MAX_LIGHTSTYLES; i++)
		{
			MSG_WriteByte (&net_message, svc_lightstyle);
			MSG_WriteByte (&net_message, i);
			MSG_WriteString (&net_message, (char *) cl_lightstyle[i].map);
		}

		// view entity
		MSG_WriteByte (&net_message, svc_setview);
		MSG_WriteShort (&net_message, cl.viewentity);

		// signon
		MSG_WriteByte (&net_message, svc_signonnum);
		MSG_WriteByte (&net_message, 3);

		CL_WriteDemoMessage ();

		// restore net_message
		net_message.data = data;
		net_message.cursize = cursize;
	}

	// force a refersh of the demo list
	Menu_DemoPopulate ();
}


void Host_ResetFixedTime (void);

bool CL_DoPlayDemo (void)
{
	char name[MAX_PATH];
	int	 c;
	bool neg = false;

	if (cmd_source != src_command) return false;

	if (Cmd_Argc() != 2)
	{
		Con_Printf ("playdemo <demoname> : plays a demo\n");
		return false;
	}

	// disconnect from server
	CL_Disconnect ();

	// open the demo file
	Q_strncpy (name, Cmd_Argv (1), 127);
	COM_DefaultExtension (name, ".dem");

	if (demohandle != INVALID_HANDLE_VALUE)
	{
		COM_FCloseFile (&demohandle);
		demohandle = INVALID_HANDLE_VALUE;
	}

	Con_Printf ("Playing demo from %s.\n", name);
	demofile_len = COM_FOpenFile (name, &demohandle);

	if (demohandle == INVALID_HANDLE_VALUE)
	{
		Con_Printf ("ERROR: couldn't open %s\n", name);
		cls.demonum = -1;		// stop demo loop
		return false;
	}

	demofile_start = SetFilePointer (demohandle, 0, NULL, FILE_CURRENT);

	cls.demoplayback = true;
	cls.state = ca_connected;
	cls.forcetrack = 0;

	Q_strncpy (cls.demoname, name, 63);

	while ((c = COM_FReadChar (demohandle)) != '\n')
	{
		if (c == '-')
			neg = true;
		else cls.forcetrack = cls.forcetrack * 10 + (c - '0');
	}

	if (neg) cls.forcetrack = -cls.forcetrack;

	unsigned int demoseed = 0;

	// keep demo effects reproducible between playbacks ;)
	// cls.demoname is guaranteed to be at least 4 chars as it includes the .dem extension so this is valid
	// (this will normally be 1869440356, or "demo"...
	memcpy (&demoseed, cls.demoname, sizeof (unsigned int));
	srand (demoseed);

	// reinit the timers to keep fx consistent
	Host_ResetFixedTime ();

	// success
	return true;
}


/*
====================
CL_PlayDemo_f

playdemo [demoname]
====================
*/
void CL_PlayDemo_f (void)
{
	CL_DoPlayDemo ();
}

/*
====================
CL_FinishTimeDemo

====================
*/
void CL_FinishTimeDemo (void)
{
	int		frames;
	float	time;

	cls.timedemo = false;

	// the first two frames didn't count
	frames = td_frames - 2;
	time = realtime - cls.td_starttime;
	td_frames = 0;

	if (!time) time = 1;

	// recalibrate to match ID Quake and stop people from complaining
	time = (time / frames) * (frames + 1);
	frames++;

	float fps = (float) frames / time;

	Con_Printf ("%i frames %0.1f seconds %0.1f fps\n", frames, time, fps);
}

/*
====================
CL_TimeDemo_f

timedemo [demoname]
====================
*/
void CL_TimeDemo_f (void)
{
	if (cmd_source != src_command)
		return;

	if (Cmd_Argc() != 2)
	{
		Con_Printf ("timedemo <demoname> : gets demo speeds\n");
		return;
	}

	// dn't switch into timedemo mode if we fail to load the demo!
	if (!CL_DoPlayDemo ()) return;

	// cls.td_starttime will be grabbed at the second frame of the demo, so
	// all the loading time doesn't get counted
	cls.timedemo = true;
	cls.td_currframe = -1;		// get a new message this frame
	td_frames = 0;
}


