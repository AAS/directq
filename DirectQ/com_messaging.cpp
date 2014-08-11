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

// writing functions
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
	buf[0] = c & 0xff;
	buf[1] = c >> 8;
}

void MSG_WriteLong (sizebuf_t *sb, int c)
{
	byte    *buf;

	buf = (byte *) SZ_GetSpace (sb, 4);
	buf[0] = c & 0xff;
	buf[1] = (c >> 8) & 0xff;
	buf[2] = (c >> 16) & 0xff;
	buf[3] = c >> 24;
}

void MSG_WriteFloat (sizebuf_t *sb, float f)
{
	union
	{
		float   f;
		int     l;
	} dat;

	dat.f = f;

	SZ_Write (sb, &dat.l, 4);
}

void MSG_WriteString (sizebuf_t *sb, char *s)
{
	if (!s)
		SZ_Write (sb, "", 1);
	else SZ_Write (sb, s, strlen (s) + 1);
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

	if (msg_readcount + 1 > net_message.cursize)
	{
		msg_badread = true;
		return -1;
	}

	c = (signed char) net_message.data[msg_readcount];
	msg_readcount++;

	return c;
}

int MSG_ReadByte (void)
{
	int     c;

	if (msg_readcount + 1 > net_message.cursize)
	{
		msg_badread = true;
		return -1;
	}

	c = (unsigned char) net_message.data[msg_readcount];
	msg_readcount++;

	return c;
}

int MSG_ReadShort (void)
{
	int     c;

	if (msg_readcount + 2 > net_message.cursize)
	{
		msg_badread = true;
		return -1;
	}

	c = (short) (net_message.data[msg_readcount]
				 + (net_message.data[msg_readcount+1] << 8));

	msg_readcount += 2;

	return c;
}

int MSG_ReadLong (void)
{
	int     c;

	if (msg_readcount + 4 > net_message.cursize)
	{
		msg_badread = true;
		return -1;
	}

	c = net_message.data[msg_readcount]
		+ (net_message.data[msg_readcount+1] << 8)
		+ (net_message.data[msg_readcount+2] << 16)
		+ (net_message.data[msg_readcount+3] << 24);

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

	return dat.f;
}

char *MSG_ReadString (void)
{
	static char     string[2048];
	int             l, c;

	l = 0;

	do
	{
		c = MSG_ReadChar ();

		if (c == -1 || c == 0)
			break;

		string[l] = c;
		l++;
	} while (l < sizeof (string) - 1);

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

float MSG_ReadAngle16 (int protocol, unsigned int flags)
{
	if (protocol == PROTOCOL_VERSION_RMQ)
	{
		if (flags & PRFL_FLOATANGLE)
			return MSG_ReadFloat(); // make sure
		else return MSG_ReadShort() * (360.0 / 65536);
	}
	else return MSG_ReadShort() * (360.0 / 65536);
}


void MSG_WriteAngle16 (sizebuf_t *sb, float f, int protocol, unsigned int flags)
{
	if (protocol == PROTOCOL_VERSION_RMQ)
	{
		if (flags & PRFL_FLOATANGLE)
			MSG_WriteFloat (sb, f);
		else MSG_WriteShort (sb, Q_rint (f * 65536.0 / 360.0) & 65535);
	}
	else MSG_WriteShort (sb, Q_rint (f * 65536.0 / 360.0) & 65535);
}


void MSG_WriteCoord24 (sizebuf_t *sb, float f)
{
	MSG_WriteShort (sb, f);
	MSG_WriteByte (sb, (int) (f * 255) % 255);
}


void MSG_WriteCoord (sizebuf_t *sb, float f, int protocol, unsigned flags)
{
	if (protocol == PROTOCOL_VERSION_RMQ)
	{
		if (flags & PRFL_FLOATCOORD)
			MSG_WriteFloat (sb, f);
		else if (flags & PRFL_24BITCOORD)
			MSG_WriteCoord24 (sb, f);
		else MSG_WriteShort (sb, (int) (f * 8));
	}
	else MSG_WriteShort (sb, (int) (f * 8));
}


void MSG_WriteByteAngle (sizebuf_t *sb, float f, int angleindex)
{
	byte bang = 0;

	// -1 and -2 are legal values with special meaning so handle them
	// the same way as ID Quake did; other values can use rounding
	if ((f == -1 || f == -2) && angleindex == 1)
		bang = (int) (f * 256.0 / 360.0) & 255;
	else bang = Q_rint (f * 256.0 / 360.0) & 255;

	MSG_WriteByte (sb, bang);
}


void MSG_WriteShortAngle (sizebuf_t *sb, float f, int angleindex)
{
	int sang = 0;

	// -1 and -2 are legal values with special meaning so handle them
	// the same way as ID Quake did; other values can use rounding
	if ((f == -1 || f == -2) && angleindex == 1)
	{
		// encode to byte, decode back to float to get the same behaviour as ID Quake
		sang = (int) (f * 256.0 / 360.0) & 255;
		f = (float) sang * (360.0 / 256);
	}

	sang = Q_rint (f * 65536.0 / 360.0) & 65535;
	MSG_WriteShort (sb, sang);
}


void MSG_WriteFloatAngle (sizebuf_t *sb, float f, int angleindex)
{
	// -1 and -2 are legal values with special meaning so handle them
	// the same way as ID Quake did; other values can use rounding
	if ((f == -1 || f == -2) && angleindex == 1)
	{
		// encode to byte, decode back to float to get the same behaviour as ID Quake
		int fang = (int) (f * 256.0 / 360.0) & 255;
		f = (float) fang * (360.0 / 256);
	}

	MSG_WriteFloat (sb, f);
}


void MSG_WriteAngle (sizebuf_t *sb, float f, int protocol, unsigned int flags, int angleindex)
{
	if (protocol == PROTOCOL_VERSION_RMQ)
	{
		if (flags & PRFL_FLOATANGLE)
			MSG_WriteFloatAngle (sb, f, angleindex);
		else if (flags & PRFL_SHORTANGLE)
			MSG_WriteShortAngle (sb, f, angleindex);
		else MSG_WriteByteAngle (sb, f, angleindex);
	}
	else MSG_WriteByteAngle (sb, f, angleindex);
}


float MSG_ReadCoord24 (void)
{
	return MSG_ReadShort () + MSG_ReadByte () * (1.0 / 255);
}


float MSG_ReadCoord (int protocol, unsigned flags)
{
	if (protocol == PROTOCOL_VERSION_RMQ)
	{
		if (flags & PRFL_FLOATCOORD)
			return MSG_ReadFloat ();
		else if (flags & PRFL_24BITCOORD)
			return MSG_ReadCoord24 ();
		else return MSG_ReadShort () * (1.0f / 8.0f);
	}
	else return MSG_ReadShort () * (1.0f / 8.0f);
}


float AngleBuf[256] =
{
	0.000000, 1.406250, 2.812500, 4.218750, 5.625000, 7.031250, 8.437500, 9.843750, 11.250000, 12.656250, 14.062500, 15.468750, 16.875000, 18.281250,
	19.687500, 21.093750, 22.500000, 23.906250, 25.312500, 26.718750, 28.125000, 29.531250, 30.937500, 32.343750, 33.750000, 35.156250, 36.562500, 37.968750,
	39.375000, 40.781250, 42.187500, 43.593750, 45.000000, 46.406250, 47.812500, 49.218750, 50.625000, 52.031250, 53.437500, 54.843750, 56.250000, 57.656250,
	59.062500, 60.468750, 61.875000, 63.281250, 64.687500, 66.093750, 67.500000, 68.906250, 70.312500, 71.718750, 73.125000, 74.531250, 75.937500, 77.343750,
	78.750000, 80.156250, 81.562500, 82.968750, 84.375000, 85.781250, 87.187500, 88.593750, 90.000000, 91.406250, 92.812500, 94.218750, 95.625000, 97.031250,
	98.437500, 99.843750, 101.250000, 102.656250, 104.062500, 105.468750, 106.875000, 108.281250, 109.687500, 111.093750, 112.500000, 113.906250, 115.312500,
	116.718750, 118.125000, 119.531250, 120.937500, 122.343750, 123.750000, 125.156250, 126.562500, 127.968750, 129.375000, 130.781250, 132.187500, 133.593750,
	135.000000, 136.406250, 137.812500, 139.218750, 140.625000, 142.031250, 143.437500, 144.843750, 146.250000, 147.656250, 149.062500, 150.468750, 151.875000,
	153.281250, 154.687500, 156.093750, 157.500000, 158.906250, 160.312500, 161.718750, 163.125000, 164.531250, 165.937500, 167.343750, 168.750000, 170.156250,
	171.562500, 172.968750, 174.375000, 175.781250, 177.187500, 178.593750, -180.000000, -178.593750, -177.187500, -175.781250, -174.375000, -172.968750,
	-171.562500, -170.156250, -168.750000, -167.343750, -165.937500, -164.531250, -163.125000, -161.718750, -160.312500, -158.906250, -157.500000, -156.093750,
	-154.687500, -153.281250, -151.875000, -150.468750, -149.062500, -147.656250, -146.250000, -144.843750, -143.437500, -142.031250, -140.625000, -139.218750,
	-137.812500, -136.406250, -135.000000, -133.593750, -132.187500, -130.781250, -129.375000, -127.968750, -126.562500, -125.156250, -123.750000, -122.343750,
	-120.937500, -119.531250, -118.125000, -116.718750, -115.312500, -113.906250, -112.500000, -111.093750, -109.687500, -108.281250, -106.875000, -105.468750,
	-104.062500, -102.656250, -101.250000, -99.843750, -98.437500, -97.031250, -95.625000, -94.218750, -92.812500, -91.406250, -90.000000, -88.593750, -87.187500,
	-85.781250, -84.375000, -82.968750, -81.562500, -80.156250, -78.750000, -77.343750, -75.937500, -74.531250, -73.125000, -71.718750, -70.312500, -68.906250,
	-67.500000, -66.093750, -64.687500, -63.281250, -61.875000, -60.468750, -59.062500, -57.656250, -56.250000, -54.843750, -53.437500, -52.031250, -50.625000,
	-49.218750, -47.812500, -46.406250, -45.000000, -43.593750, -42.187500, -40.781250, -39.375000, -37.968750, -36.562500, -35.156250, -33.750000, -32.343750,
	-30.937500, -29.531250, -28.125000, -26.718750, -25.312500, -23.906250, -22.500000, -21.093750, -19.687500, -18.281250, -16.875000, -15.468750, -14.062500,
	-12.656250, -11.250000, -9.843750, -8.437500, -7.031250, -5.625000, -4.218750, -2.812500, -1.406250
};


float MSG_ReadAngle (int protocol, unsigned int flags) // gb, PROTOCOL_RMQ
{
	if (protocol == PROTOCOL_VERSION_RMQ)
	{
		if (flags & PRFL_FLOATANGLE)
			return MSG_ReadFloat ();
		else if (flags & PRFL_SHORTANGLE)
			return MSG_ReadShort () * (360.0 / 65536);
		else return AngleBuf[MSG_ReadByte ()];
	}
	else return AngleBuf[MSG_ReadByte ()];
}


void MSG_WriteProQuakeAngle (sizebuf_t *sb, float f)
{
	int val = (int) f * 65536 / 360;
	MSG_WriteShort (sb, (val & 65535));
}


float MSG_ReadProQuakeAngle (void)
{
	int val = MSG_ReadShort ();
	return val * (360.0 / 65536);
}


//===========================================================================

void SZ_Alloc (sizebuf_t *buf, int startsize)
{
	// ensure room
	if (startsize < 256)
		startsize = 256;

	buf->data = (byte *) Zone_Alloc (startsize);
	buf->maxsize = startsize;
	buf->cursize = 0;
}


void SZ_Init (sizebuf_t *buf, void *data, int len)
{
	buf->allowoverflow = false;
	buf->cursize = 0;
	buf->data = (byte *) data;
	buf->maxsize = len;
	buf->overflowed = false;

	memset (data, 0, len);
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
	memcpy (SZ_GetSpace (buf, length), data, length);
}

void SZ_Print (sizebuf_t *buf, char *data)
{
	int             len;

	len = strlen (data) + 1;

	// byte * cast to keep VC++ happy
	if (buf->data[buf->cursize-1])
		memcpy ((byte *) SZ_GetSpace (buf, len), data, len); // no trailing 0
	else
		memcpy ((byte *) SZ_GetSpace (buf, len - 1) - 1, data, len); // write over trailing 0
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
skipwhite:;
	while ((c = data[0]) <= ' ')
	{
		// eof
		if (c == 0) return NULL;

		data++;
	}

	// skip // comments
	if (c == '/' && data[1] == '/')
	{
		while (data[0] && data[0] != '\n')
			data++;

		goto skipwhite;
	}

	// skip /*..*/ comments
	if (c == '/' && data[1] == '*')
	{
		// comment
		data++;

		while (data[0] && (data[0] != '*' || data[1] != '/'))
			data++;

		if (data[0]) data++;
		if (data[0]) data++;

		goto skipwhite;
	}

	// handle quoted strings specially
	if (c == '\"')
	{
		data++;

		while (1)
		{
			c = data[0];
			data++;

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
		c = data[0];
	} while (c > (parsefullline ? 31 : 32));

	com_token[len] = 0;

	return data;
}


