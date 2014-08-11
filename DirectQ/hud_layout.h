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


#define HUD_VISIBLE		1
#define HUD_CENTERX		2
#define HUD_CENTERY		4
#define HUD_HIDEIF0		8

typedef struct huditem_s
{
	int flags;
	int x, y;
	int hx, hy;
} huditem_t;


huditem_t hud_sbar[4] =
{
	{HUD_VISIBLE | HUD_CENTERX, -160, -24},
	{HUD_VISIBLE | HUD_CENTERX, -160, -24},
	{0},
	{0}
};


huditem_t hud_ibar[4] =
{
	{HUD_VISIBLE | HUD_CENTERX, -160, -48},
	{HUD_VISIBLE | HUD_CENTERX, -160, -48},
	{0},
	{0}
};


huditem_t hud_facepic[4] =
{
	{HUD_VISIBLE | HUD_CENTERX, -48, -24},
	{HUD_VISIBLE | HUD_CENTERX, -48, -24},
	{HUD_VISIBLE | HUD_CENTERX, 28, -28},
	{HUD_VISIBLE, 84, -28}
};


huditem_t hud_faceval[4] =
{
	{HUD_VISIBLE | HUD_CENTERX, -24, -24},
	{HUD_VISIBLE | HUD_CENTERX, -24, -24},
	{HUD_VISIBLE | HUD_CENTERX, -52, -28},
	{HUD_VISIBLE, 4, -28}
};


huditem_t hud_ammopic[4] =
{
	{HUD_VISIBLE | HUD_CENTERX, 64, -24},
	{HUD_VISIBLE | HUD_CENTERX, 64, -24},
	{HUD_VISIBLE | HUD_CENTERX, 148, -28},
	{HUD_VISIBLE, -28, -28}
};


huditem_t hud_ammoval[4] =
{
	{HUD_VISIBLE | HUD_CENTERX, 88, -24},
	{HUD_VISIBLE | HUD_CENTERX, 88, -24},
	{HUD_VISIBLE | HUD_CENTERX, 68, -28},
	{HUD_VISIBLE, -108, -28}
};


huditem_t hud_armorpic[4] =
{
	{HUD_VISIBLE | HUD_CENTERX, -160, -24},
	{HUD_VISIBLE | HUD_CENTERX, -160, -24},
	{HUD_VISIBLE | HUD_CENTERX, -92, -28},
	{HUD_VISIBLE, 84, -56}
};


huditem_t hud_armorval[4] =
{
	{HUD_VISIBLE | HUD_CENTERX, -136, -24},
	{HUD_VISIBLE | HUD_CENTERX, -136, -24},
	{HUD_VISIBLE | HUD_CENTERX, -172, -28},
	{HUD_VISIBLE, 4, -56}
};


huditem_t hud_keys[4] =
{
	{HUD_VISIBLE | HUD_CENTERX, 32, -40, 16, 0},
	{HUD_VISIBLE | HUD_CENTERX, 32, -40, 16, 0},
	{HUD_VISIBLE | HUD_CENTERX, 0, -48, 18, 0},
	{HUD_VISIBLE, -266, -24, 18, 0},
};


huditem_t hud_items[4] = 
{
	{HUD_VISIBLE | HUD_CENTERX, 64, -40, 16, 0},
	{HUD_VISIBLE | HUD_CENTERX, 64, -40, 16, 0},
	{HUD_VISIBLE | HUD_CENTERX, 36, -48, 18, 0},
	{HUD_VISIBLE, -230, -24, 18, 0},
};


huditem_t hud_sigils[4] =
{
	{HUD_VISIBLE | HUD_CENTERX, 128, -40, 8, 0},
	{HUD_VISIBLE | HUD_CENTERX, 128, -40, 8, 0},
	{HUD_VISIBLE | HUD_CENTERX, 108, -48, 8, 0},
	{HUD_VISIBLE, -158, -24, 8, 0},
};


huditem_t hud_ammocount[4] =
{
	{HUD_VISIBLE | HUD_CENTERX, -152, -48, 48, 0},
	{HUD_VISIBLE | HUD_CENTERX, -152, -48, 48, 0},
	{HUD_VISIBLE, -42, -94, 0, 13},
	{HUD_VISIBLE, -42, -108, 0, 13}
};


huditem_t hud_weapons[4] =
{
	{HUD_VISIBLE | HUD_CENTERX, -160, -40, 24, 0},
	{HUD_VISIBLE | HUD_CENTERX, -160, -40, 24, 0},
	{HUD_VISIBLE, -4, 4, 0, 18},
	{HUD_VISIBLE, -4, 4, 0, 18}
};


huditem_t hud_dmoverlay[4] =
{
	{HUD_VISIBLE, 8, -44},
	{HUD_VISIBLE, 8, -44},
	{HUD_VISIBLE, 8, -44},
	{HUD_VISIBLE, 8, -100}
};


huditem_t hud_teamcolor[4] =
{
	{HUD_VISIBLE | HUD_CENTERX, 160, -24},
	{HUD_VISIBLE | HUD_CENTERX, 160, -24},
	{HUD_VISIBLE | HUD_CENTERX, 180, -28},
	{HUD_VISIBLE, 112, -28}
};


huditem_t hud_hipnokeys[4] =
{
	{HUD_VISIBLE | HUD_CENTERX, 48, -21, 0, 9},
	{HUD_VISIBLE | HUD_CENTERX, 48, -21, 0, 9},
	{HUD_VISIBLE | HUD_CENTERX, 18, -50, 0, 11},
	{HUD_VISIBLE, -248, -26, 0, 11}
};
