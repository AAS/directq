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


/*
==============================================================================

			MESSAGE IO FUNCTIONS

Handles byte ordering and avoids alignment errors
==============================================================================
*/

//
// writing functions
//

void MSG_WriteChar (sizebuf_t *sb, int c)
{
	byte    *buf;

	buf = (byte *) SZ_GetSpace (sb, 1);
	buf[0] = c;
}

void MSG_WriteByte (sizebuf_t *sb, int c)
{
	byte    *buf;

	buf = (byte *) SZ_GetSpace (sb, 1);
	buf[0] = c;
}

void MSG_WriteShort (sizebuf_t *sb, int c)
{
	byte    *buf;

	buf = (byte *) SZ_GetSpace (sb, 2);
	buf[0] = c&0xff;
	buf[1] = c>>8;
}

void MSG_WriteLong (sizebuf_t *sb, int c)
{
	byte    *buf;
	
	buf = (byte *) SZ_GetSpace (sb, 4);
	buf[0] = c&0xff;
	buf[1] = (c>>8)&0xff;
	buf[2] = (c>>16)&0xff;
	buf[3] = c>>24;
}

void MSG_WriteFloat (sizebuf_t *sb, float f)
{
	union
	{
		float   f;
		int     l;
	} dat;

	dat.f = f;
	dat.l = LittleLong (dat.l);

	SZ_Write (sb, &dat.l, 4);
}

void MSG_WriteString (sizebuf_t *sb, char *s)
{
	if (!s)
		SZ_Write (sb, "", 1);
	else SZ_Write (sb, s, strlen(s)+1);
}


// reading functions
int         msg_readcount;
bool        msg_badread;

void MSG_BeginReading (void)
{
	msg_readcount = 0;
	msg_badread = false;
}

// returns -1 and sets msg_badread if no more characters are available
int MSG_ReadChar (void)
{
	int     c;

	if (msg_readcount+1 > net_message.cursize)
	{
		msg_badread = true;
		return -1;
	}
		
	c = (signed char)net_message.data[msg_readcount];
	msg_readcount++;
	
	return c;
}

int MSG_ReadByte (void)
{
	int     c;

	if (msg_readcount+1 > net_message.cursize)
	{
		msg_badread = true;
		return -1;
	}

	c = (unsigned char)net_message.data[msg_readcount];
	msg_readcount++;

	return c;
}

int MSG_ReadShort (void)
{
	int     c;
	
	if (msg_readcount+2 > net_message.cursize)
	{
		msg_badread = true;
		return -1;
	}
		
	c = (short)(net_message.data[msg_readcount]
	+ (net_message.data[msg_readcount+1]<<8));
	
	msg_readcount += 2;
	
	return c;
}

int MSG_ReadLong (void)
{
	int     c;
	
	if (msg_readcount+4 > net_message.cursize)
	{
		msg_badread = true;
		return -1;
	}
		
	c = net_message.data[msg_readcount]
	+ (net_message.data[msg_readcount+1]<<8)
	+ (net_message.data[msg_readcount+2]<<16)
	+ (net_message.data[msg_readcount+3]<<24);
	
	msg_readcount += 4;
	
	return c;
}

float MSG_ReadFloat (void)
{
	union
	{
		byte    b[4];
		float   f;
		int     l;
	} dat;
	
	dat.b[0] =      net_message.data[msg_readcount];
	dat.b[1] =      net_message.data[msg_readcount+1];
	dat.b[2] =      net_message.data[msg_readcount+2];
	dat.b[3] =      net_message.data[msg_readcount+3];
	msg_readcount += 4;
	
	dat.l = LittleLong (dat.l);

	return dat.f;   
}

char *MSG_ReadString (void)
{
	static char     string[2048];
	int             l,c;

	l = 0;
	do
	{
		c = MSG_ReadChar ();
		if (c == -1 || c == 0)
			break;
		string[l] = c;
		l++;
	} while (l < sizeof(string) - 1);

	string[l] = 0;

	return string;
}


// JPG - need this to check for ProQuake messages
int MSG_PeekByte (void)
{
	if (msg_readcount + 1 > net_message.cursize)
	{
		msg_badread = true;
		return -1;
	}

	return (unsigned char) net_message.data[msg_readcount];
}

float MSG_ReadAngle16 (void)
{
	int val = MSG_ReadShort();
	return val * (360.0 / 65536);
}

void MSG_WriteAngle16 (sizebuf_t *sb, float f)
{
	int val = (int)f*65536/360;
	MSG_WriteShort (sb, val & 65535);
}


float MSG_ReadCoord24 (void)
{
	return MSG_ReadShort () + ((float) MSG_ReadByte ()) * (1.0 / 255);
}


float MSG_ReadCoord32f (void)
{
	return MSG_ReadFloat ();
}


void MSG_WriteCoord24 (sizebuf_t *sb, float f)
{
	MSG_WriteShort (sb, f);
	MSG_WriteByte (sb, (int) (f * 255) % 255);
}


void MSG_WriteCoord (sizebuf_t *sb, float f, int protocol)
{
	if (protocol >= PROTOCOL_VERSION_MH)
		MSG_WriteFloat (sb, f);
	else if (protocol == PROTOCOL_VERSION_RMQ_MINUS2)
	{
		// coord24 is incredibbly jerky on elevator rides
		//MSG_WriteCoord24 (sb, f);
		MSG_WriteFloat (sb, f);
	}
	else MSG_WriteShort (sb, (int) (f * 8));
}


void MSG_WriteAngle (sizebuf_t *sb, float f, int protocol)
{
	switch (protocol)
	{
	case PROTOCOL_VERSION_MH:
		MSG_WriteShort (sb, ((int) ((f * 65536) / 360)) & 65535);
		break;

	case PROTOCOL_VERSION_RMQ_MINUS2:
		MSG_WriteFloat (sb, f);
		break;

	default:
		MSG_WriteByte (sb, ((int) ((f * 256) / 360)) & 255);
		break;
	}
}


float MSG_ReadCoord (int protocol)
{
	if (protocol >= PROTOCOL_VERSION_MH)
		return MSG_ReadFloat ();
	else if (protocol == PROTOCOL_VERSION_RMQ_MINUS2)
	{
		// coord24 is incredibbly jerky on elevator rides
		//return MSG_ReadCoord24 ();
		return MSG_ReadFloat ();
	}
	else return MSG_ReadShort () * (1.0f / 8.0f);
}


float MSG_ReadAngle (int protocol)
{
	switch (protocol)
	{
	case PROTOCOL_VERSION_MH:
		return MSG_ReadShort () * (360.0 / 65536);

	case PROTOCOL_VERSION_RMQ_MINUS2:
		return MSG_ReadFloat ();

	default:
		return (MSG_ReadChar () * 1.40625f);
	}
}


//===========================================================================

void SZ_Alloc (sizebuf_t *buf, int startsize)
{
	if (startsize < 256)
		startsize = 256;

	buf->data = (byte *) Zone_Alloc (startsize);
	buf->maxsize = startsize;
	buf->cursize = 0;
}


void SZ_Clear (sizebuf_t *buf)
{
	buf->cursize = 0;
}

void *SZ_GetSpace (sizebuf_t *buf, int length)
{
	void    *data;

	if (buf->cursize + length > buf->maxsize)
	{
		if (!buf->allowoverflow)
			Sys_Error ("SZ_GetSpace: overflow without allowoverflow set");

		if (length > buf->maxsize)
			Sys_Error ("SZ_GetSpace: %i is > full buffer size", length);

		buf->overflowed = true;
		Con_Printf ("SZ_GetSpace: overflow");
		SZ_Clear (buf); 
	}

	data = buf->data + buf->cursize;
	buf->cursize += length;

	return data;
}

void SZ_Write (sizebuf_t *buf, void *data, int length)
{
	Q_MemCpy (SZ_GetSpace (buf,length), data, length);         
}

void SZ_Print (sizebuf_t *buf, char *data)
{
	int             len;
	
	len = strlen(data)+1;

	// byte * cast to keep VC++ happy
	if (buf->data[buf->cursize-1])
		Q_MemCpy ((byte *)SZ_GetSpace(buf, len),data,len); // no trailing 0
	else
		Q_MemCpy ((byte *)SZ_GetSpace(buf, len-1)-1,data,len); // write over trailing 0
}


//============================================================================


/*
==============
COM_Parse

Parse a token out of a string or parses a full line
==============
*/
char *COM_Parse (char *data, bool parsefullline)
{
	// qw version
	int c;
	int len;

	len = 0;
	com_token[0] = 0;

	if (!data) return NULL;

	// skip whitespace
skipwhite:
	while ((c = *data) <= ' ')
	{
		// eof
		if (c == 0) return NULL;
		data++;
	}

	// skip // comments
	if (c=='/' && data[1] == '/')
	{
		while (*data && *data != '\n')
			data++;
		goto skipwhite;
	}

	// handle quoted strings specially
	if (c == '\"')
	{
		data++;

		while (1)
		{
			c = *data++;

			if (c == '\"' || !c)
			{
				com_token[len] = 0;
				return data;
			}

			com_token[len] = c;
			len++;
		}
	}

	// parse a regular word or line
	do
	{
		com_token[len] = c;
		data++;
		len++;
		c = *data;
	} while (c > (parsefullline ? 31 : 32));

	com_token[len] = 0;

	return data;
}


