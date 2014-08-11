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
	if (!cls.demofile)
		return;

	fclose (cls.demofile);
	cls.demofile = NULL;
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

	len = LittleLong (net_message.cursize);
	Success = fwrite (&len, 4, 1, cls.demofile) == 1;

	for (i=0 ; i<3 && Success ; i++)
	{
		f = LittleFloat (cl.viewangles[i]);
		Success = fwrite (&f, 4, 1, cls.demofile) == 1;
	}

	if (Success)
		Success = fwrite (net_message.data, net_message.cursize, 1, cls.demofile) == 1;

	if (Success)
		fflush (cls.demofile);
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
	
	if	(cls.demoplayback)
	{
	// decide if it is time to grab the next message		
		if (cls.signon == SIGNONS)	// allways grab until fully connected
		{
			if (cls.timedemo)
			{
				if (host_framecount == cls.td_lastframe)
					return 0;		// allready read this frame's message
				cls.td_lastframe = host_framecount;
			// if this is the second frame, grab the real td_starttime
			// so the bogus time on the first frame doesn't count
				if (host_framecount == cls.td_startframe + 1)
					cls.td_starttime = realtime;
			}
			else if ( /* cl.time > 0 && */ cl.time <= cl.mtime[0])
			{
					return 0;		// don't need another message yet
			}
		}
		
		// Detect EOF, especially for demos in pak files
		if (ftell(cls.demofile) - demofile_start >= demofile_len)
			Host_EndGame ("Missing disconnect in demofile\n");
	
	// get the next message
		Success = fread (&net_message.cursize, 4, 1, cls.demofile) == 1;

		VectorCopy (cl.mviewangles[0], cl.mviewangles[1]);
		for (i=0 ; i<3 && Success ; i++)
		{
			Success = fread (&f, 4, 1, cls.demofile) == 1;
			cl.mviewangles[0][i] = LittleFloat (f);
		}
		
		if (Success)
		{
			net_message.cursize = LittleLong (net_message.cursize);
			if (net_message.cursize > MAX_MSGLEN)
				Host_Error ("Demo message %d > MAX_MSGLEN (%d)", net_message.cursize, MAX_MSGLEN);
			Success = fread (net_message.data, net_message.cursize, 1, cls.demofile) == 1;
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
		else
			break;
	}

	if (cls.demorecording)
	{
		if (!CL_WriteDemoMessage ())
			return -1; // File write failure
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

	if (cls.demofile)
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
	char	name[MAX_OSPATH];
	int		track;

	if (cmd_source != src_command)
		return;

	c = Cmd_Argc();
	if (c != 2 && c != 3 && c != 4)
	{
		Con_Printf ("record <demoname> [<map> [cd track]]\n");
		return;
	}

	if (strstr(Cmd_Argv(1), ".."))
	{
		Con_Printf ("Relative pathnames are not allowed.\n");
		return;
	}

	if (c == 2 && cls.state == ca_connected)
	{
		Con_Printf("Can not record - already connected to server\nClient demo recording must be started before connecting\n");
		return;
	}

// write the forced cd track number, or -1
	if (c == 4)
	{
		track = atoi(Cmd_Argv(3));

		// bug - this was cls.forcetrack
		Con_Printf ("Forcing CD track to %i\n", track);
	}
	else track = -1;

	sprintf (name, "%s/%s", com_gamedir, Cmd_Argv(1));
	
//
// start the map up
//
	if (c > 2)
		Cmd_ExecuteString ( va("map %s", Cmd_Argv(2)), src_command);
	
//
// open the demo file
//
	COM_DefaultExtension (name, ".dem");

	Con_Printf ("recording to %s.\n", name);
	cls.demofile = fopen (name, "wb");
	if (!cls.demofile)
	{
		Con_Printf ("ERROR: couldn't open.\n");
		return;
	}

	cls.forcetrack = track;
	fprintf (cls.demofile, "%i\n", cls.forcetrack);
	
	cls.demorecording = true;

	// force a refersh of the demo list
	Menu_DemoPopulate ();
}


bool CL_DoPlayDemo (void)
{
	char name[MAX_OSPATH];
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
	strcpy (name, Cmd_Argv(1));
	COM_DefaultExtension (name, ".dem");

	Con_Printf ("Playing demo from %s.\n", name);
        demofile_len = COM_FOpenFile (name, &cls.demofile);
	
	if (!cls.demofile)
	{
		Con_Printf ("ERROR: couldn't open %s\n", name);
		cls.demonum = -1;		// stop demo loop
		return false;
	}

    demofile_start = ftell (cls.demofile);

	cls.demoplayback = true;
	cls.state = ca_connected;
	cls.forcetrack = 0;

	while ((c = getc(cls.demofile)) != '\n')
	{
		if (c == '-')
			neg = true;
		else cls.forcetrack = cls.forcetrack * 10 + (c - '0');
	}

	if (neg) cls.forcetrack = -cls.forcetrack;

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

	// the first frame didn't count
	frames = (host_framecount - cls.td_startframe) - 1;
	time = realtime - cls.td_starttime;

	if (!time) time = 1;
	float fps = (float) frames / time;

	Con_Printf ("%i frames %5.1f seconds %5.1f fps\n", frames, time, fps);
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
	cls.td_startframe = host_framecount;
	cls.td_lastframe = -1;		// get a new message this frame
}


