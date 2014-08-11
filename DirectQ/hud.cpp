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
// gpl blah etc, just read "gnu.txt" that comes with this...


#include "quakedef.h"
#include "d3d_model.h"
#include "menu_common.h"
#include "d3d_quake.h"

extern cvar_t	crosshair;
extern cvar_t	cl_crossx;
extern cvar_t	cl_crossy;
extern cvar_t	scr_crosshairscale;
extern cvar_t	scr_crosshaircolor;
extern cvar_t	scr_viewsize;

#define STAT_MINUS		10	// num frame for '-' stats digit

qpic_t		*sb_nums[2][11];
qpic_t		*sb_colon, *sb_slash;
qpic_t		*sb_ibar;
qpic_t		*sb_sbar;

qpic_t      *sb_weapons[7][8];   // 0 is active, 1 is owned, 2-5 are flashes
qpic_t      *sb_ammo[4];
qpic_t		*sb_sigil[4];
qpic_t		*sb_armor[3];
qpic_t		*sb_items[32];

qpic_t	*sb_faces[7][2];		// 0 is gibbed, 1 is dead, 2-6 are alive
// 0 is static, 1 is temporary animation
qpic_t	*sb_face_invis;
qpic_t	*sb_face_quad;
qpic_t	*sb_face_invuln;
qpic_t	*sb_face_invis_invuln;

int			sb_lines;			// scan lines to draw

qpic_t      *rsb_invbar[2];
qpic_t      *rsb_weapons[5];
qpic_t      *rsb_items[2];
qpic_t      *rsb_ammo[3];
qpic_t      *rsb_teambord;		// PGM 01/19/97 - team color border

qpic_t		*gfx_ranking_lmp;
qpic_t		*gfx_finale_lmp;
qpic_t		*gfx_inter_lmp;
qpic_t		*gfx_complete_lmp;

//MED 01/04/97 added two more weapons + 3 alternates for grenade launcher
qpic_t      *hsb_weapons[7][5];   // 0 is active, 1 is owned, 2-5 are flashes
//MED 01/04/97 added array to simplify weapon parsing
int         hipweapons[4] = {HIT_LASER_CANNON_BIT, HIT_MJOLNIR_BIT, 4, HIT_PROXIMITY_GUN_BIT};
//MED 01/04/97 added hipnotic items array
qpic_t      *hsb_items[2];

qpic_t		*ksb_ammo[1];

bool	hud_showscores = false;
bool	hud_showdemoscores = false;


void HUD_Init (void)
{
	int i;

	gfx_ranking_lmp = Draw_LoadPic ("gfx/ranking.lmp");
	gfx_finale_lmp = Draw_LoadPic ("gfx/finale.lmp");
	gfx_complete_lmp = Draw_LoadPic ("gfx/complete.lmp");
	gfx_inter_lmp = Draw_LoadPic ("gfx/inter.lmp");

	// these must be loaded first otherwise 0 will butt on to the turtle pic, causing the right edge of
	// the turtle pic to bleed into the left edge of 0 when using linear filtering with gl_conscale < 1
	sb_nums[0][10] = Draw_LoadPic ("num_minus");
	sb_nums[1][10] = Draw_LoadPic ("anum_minus");

	sb_slash = Draw_LoadPic ("num_slash");
	sb_colon = Draw_LoadPic ("num_colon");

	for (i = 0; i < 10; i++)
	{
		sb_nums[0][i] = Draw_LoadPic (va ("num_%i", i));
		sb_nums[1][i] = Draw_LoadPic (va ("anum_%i", i));
	}

	sb_weapons[0][0] = Draw_LoadPic ("inv_shotgun");
	sb_weapons[0][1] = Draw_LoadPic ("inv_sshotgun");
	sb_weapons[0][2] = Draw_LoadPic ("inv_nailgun");
	sb_weapons[0][3] = Draw_LoadPic ("inv_snailgun");
	sb_weapons[0][4] = Draw_LoadPic ("inv_rlaunch");
	sb_weapons[0][5] = Draw_LoadPic ("inv_srlaunch");
	sb_weapons[0][6] = Draw_LoadPic ("inv_lightng");

	sb_weapons[1][0] = Draw_LoadPic ("inv2_shotgun");
	sb_weapons[1][1] = Draw_LoadPic ("inv2_sshotgun");
	sb_weapons[1][2] = Draw_LoadPic ("inv2_nailgun");
	sb_weapons[1][3] = Draw_LoadPic ("inv2_snailgun");
	sb_weapons[1][4] = Draw_LoadPic ("inv2_rlaunch");
	sb_weapons[1][5] = Draw_LoadPic ("inv2_srlaunch");
	sb_weapons[1][6] = Draw_LoadPic ("inv2_lightng");

	for (i = 0; i < 5; i++)
	{
		sb_weapons[2 + i][0] = Draw_LoadPic (va ("inva%i_shotgun", i + 1));
		sb_weapons[2 + i][1] = Draw_LoadPic (va ("inva%i_sshotgun", i + 1));
		sb_weapons[2 + i][2] = Draw_LoadPic (va ("inva%i_nailgun", i + 1));
		sb_weapons[2 + i][3] = Draw_LoadPic (va ("inva%i_snailgun", i + 1));
		sb_weapons[2 + i][4] = Draw_LoadPic (va ("inva%i_rlaunch", i + 1));
		sb_weapons[2 + i][5] = Draw_LoadPic (va ("inva%i_srlaunch", i + 1));
		sb_weapons[2 + i][6] = Draw_LoadPic (va ("inva%i_lightng", i + 1));
	}

	sb_ammo[0] = Draw_LoadPic ("sb_shells");
	sb_ammo[1] = Draw_LoadPic ("sb_nails");
	sb_ammo[2] = Draw_LoadPic ("sb_rocket");
	sb_ammo[3] = Draw_LoadPic ("sb_cells");

	sb_armor[0] = Draw_LoadPic ("sb_armor1");
	sb_armor[1] = Draw_LoadPic ("sb_armor2");
	sb_armor[2] = Draw_LoadPic ("sb_armor3");

	sb_items[0] = Draw_LoadPic ("sb_key1");
	sb_items[1] = Draw_LoadPic ("sb_key2");
	sb_items[2] = Draw_LoadPic ("sb_invis");
	sb_items[3] = Draw_LoadPic ("sb_invuln");
	sb_items[4] = Draw_LoadPic ("sb_suit");
	sb_items[5] = Draw_LoadPic ("sb_quad");

	sb_sigil[0] = Draw_LoadPic ("sb_sigil1");
	sb_sigil[1] = Draw_LoadPic ("sb_sigil2");
	sb_sigil[2] = Draw_LoadPic ("sb_sigil3");
	sb_sigil[3] = Draw_LoadPic ("sb_sigil4");

	sb_faces[4][0] = Draw_LoadPic ("face1");
	sb_faces[4][1] = Draw_LoadPic ("face_p1");
	sb_faces[3][0] = Draw_LoadPic ("face2");
	sb_faces[3][1] = Draw_LoadPic ("face_p2");
	sb_faces[2][0] = Draw_LoadPic ("face3");
	sb_faces[2][1] = Draw_LoadPic ("face_p3");
	sb_faces[1][0] = Draw_LoadPic ("face4");
	sb_faces[1][1] = Draw_LoadPic ("face_p4");
	sb_faces[0][0] = Draw_LoadPic ("face5");
	sb_faces[0][1] = Draw_LoadPic ("face_p5");

	sb_face_invis = Draw_LoadPic ("face_invis");
	sb_face_invuln = Draw_LoadPic ("face_invul2");
	sb_face_invis_invuln = Draw_LoadPic ("face_inv2");
	sb_face_quad = Draw_LoadPic ("face_quad");

	//MED 01/04/97 added new hipnotic weapons
	if (hipnotic)
	{
		hsb_weapons[0][0] = Draw_LoadPic ("inv_laser");
		hsb_weapons[0][1] = Draw_LoadPic ("inv_mjolnir");
		hsb_weapons[0][2] = Draw_LoadPic ("inv_gren_prox");
		hsb_weapons[0][3] = Draw_LoadPic ("inv_prox_gren");
		hsb_weapons[0][4] = Draw_LoadPic ("inv_prox");

		hsb_weapons[1][0] = Draw_LoadPic ("inv2_laser");
		hsb_weapons[1][1] = Draw_LoadPic ("inv2_mjolnir");
		hsb_weapons[1][2] = Draw_LoadPic ("inv2_gren_prox");
		hsb_weapons[1][3] = Draw_LoadPic ("inv2_prox_gren");
		hsb_weapons[1][4] = Draw_LoadPic ("inv2_prox");

		for (i = 0; i < 5; i++)
		{
			hsb_weapons[2 + i][0] = Draw_LoadPic (va ("inva%i_laser", i + 1));
			hsb_weapons[2 + i][1] = Draw_LoadPic (va ("inva%i_mjolnir", i + 1));
			hsb_weapons[2 + i][2] = Draw_LoadPic (va ("inva%i_gren_prox", i + 1));
			hsb_weapons[2 + i][3] = Draw_LoadPic (va ("inva%i_prox_gren", i + 1));
			hsb_weapons[2 + i][4] = Draw_LoadPic (va ("inva%i_prox", i + 1));
		}

		hsb_items[0] = Draw_LoadPic ("sb_wsuit");
		hsb_items[1] = Draw_LoadPic ("sb_eshld");
	}

	if (rogue)
	{
		rsb_weapons[0] = Draw_LoadPic ("r_lava");
		rsb_weapons[1] = Draw_LoadPic ("r_superlava");
		rsb_weapons[2] = Draw_LoadPic ("r_gren");
		rsb_weapons[3] = Draw_LoadPic ("r_multirock");
		rsb_weapons[4] = Draw_LoadPic ("r_plasma");

		rsb_items[0] = Draw_LoadPic ("r_shield1");
		rsb_items[1] = Draw_LoadPic ("r_agrav1");

		// PGM 01/19/97 - team color border
		rsb_teambord = Draw_LoadPic ("r_teambord");
		// PGM 01/19/97 - team color border

		rsb_ammo[0] = Draw_LoadPic ("r_ammolava");
		rsb_ammo[1] = Draw_LoadPic ("r_ammomulti");
		rsb_ammo[2] = Draw_LoadPic ("r_ammoplasma");
	}

	// save the bars till last to ensure that everything else goes into the scrap OK
	sb_sbar = Draw_LoadPic ("sbar");
	sb_ibar = Draw_LoadPic ("ibar");

	if (rogue)
	{
		rsb_invbar[0] = Draw_LoadPic ("r_invbar1");
		rsb_invbar[1] = Draw_LoadPic ("r_invbar2");
	}

	if (kurok) ksb_ammo[0] = Draw_LoadPic ("sb_50cal");
}


void Draw_TileClear (void);


/*
===============
HUD_ShowScores

Tab key down
===============
*/
void HUD_ShowScores (void)
{
	hud_showscores = !hud_showscores;
}


void HUD_ShowDemoScores (void)
{
	hud_showdemoscores = !hud_showdemoscores;
}


/*
===============
HUD_DontShowScores

Tab key up
===============
*/
void HUD_DontShowScores (void)
{
	if (cl.maxclients > 1)
	{
		// revert to old behaviour in multiplayer
		hud_showscores = false;
		hud_showdemoscores = false;
	}
}


/*
===============
HUD_Init
===============
*/
cmd_t HUD_ShowScores_Cmd ("+showscores", HUD_ShowScores);
cmd_t HUD_DontShowScores_Cmd ("-showscores", HUD_DontShowScores);

void SCR_RefdefCvarChange (cvar_t *blah);

// new names for compatibility with other engines
cvar_t cl_sbar ("cl_sbar", "0", CVAR_ARCHIVE, SCR_RefdefCvarChange);
cvar_t scr_sbaralpha ("scr_sbaralpha", "0.5f", CVAR_ARCHIVE);
cvar_t scr_centersbar ("scr_centersbar", "1", CVAR_ARCHIVE);

// aliases so that the old dq cvars will still work
cvar_alias_t hud_sbaralpha ("hud_sbaralpha", &scr_sbaralpha);
cvar_alias_t hud_centerhud ("hud_centerhud", &scr_centersbar);
cvar_alias_t hud_overlay ("hud_overlay", &cl_sbar);

// called scr_* for consistency with other engines
cvar_t scr_clock ("scr_clock", 0.0f, CVAR_ARCHIVE);
cvar_t scr_showfps ("scr_showfps", 0.0f, CVAR_ARCHIVE);

// grrrrrrrrr
cvar_alias_t show_fps ("show_fps", &scr_showfps);
cvar_alias_t showfps ("showfps", &scr_showfps);

#include "hud_layout.h"

// set to true to draw all items whether we have them or not (testing)
bool hud_drawfull = false;


int HUD_GetX (huditem_t *hi)
{
	int xpos = hi->x;

	if (hi->flags & HUD_CENTERX)
	{
		if (!scr_centersbar.integer && cl_sbar.integer < 2)
			xpos += 160;	// the HUD elements are 320 wide
		else return (vid.currsize->width / 2) + xpos;
	}

	if (xpos < 0)
		return vid.currsize->width + xpos;
	else return xpos;
}


int HUD_GetY (huditem_t *hi)
{
	if (hi->flags & HUD_CENTERY)
		return (vid.currsize->height / 2) + hi->y;

	if (hi->y < 0)
		return vid.currsize->height + hi->y;
	else return hi->y;
}


int HUD_itoa (int num, char *buf)
{
	char *str;
	int pow10;
	int dig;

	str = buf;

	if (num < 0)
	{
		*str++ = '-';
		num = -num;
	}

	for (pow10 = 10; num >= pow10; pow10 *= 10);

	do
	{
		pow10 /= 10;
		dig = num / pow10;
		*str++ = '0' + dig;
		num -= dig * pow10;
	} while (pow10 != 1);

	*str = 0;

	return str - buf;
}


void HUD_DrawNum (int x, int y, int num, int digits, int color = 0)
{
	char str[12];
	char *ptr;
	int l, frame;

	l = HUD_itoa (num, str);
	ptr = str;

	if (l > digits) ptr += (l - digits);
	if (l < digits) x += (digits - l) * (kurok ? 16 : 24);

	while (*ptr)
	{
		if (*ptr == '-')
			frame = STAT_MINUS;
		else frame = *ptr - '0';

		Draw_Pic (x, y, sb_nums[color][frame], 1, true);
		x += (kurok ? 16 : 24);
		ptr++;
	}
}


void HUD_DrawNum2 (int x, int y, int num, int digits, int color)
{
	char str[12];
	char *ptr;
	int l, frame;

	l = HUD_itoa (num, str);
	ptr = str;

	if (l > digits) ptr += (l - digits);
	if (l < digits) x += (digits - l) * 0;

	while (*ptr)
	{
		if (*ptr == '-')
			frame = STAT_MINUS;
		else frame = *ptr - '0';

		Draw_Pic (x, y, sb_nums[color][frame]);
		x += 16;
		ptr++;
	}
}


void HUD_DividerLine (int y, int len)
{
	int i;
	char str[80];

	if (len > 75) len = 75;

	for (i = 0; i < len; i++)
	{
		if (!i)
			str[i] = 32;
		else if (i == 1)
			str[i] = 29;
		else if (i == len - 1)
			str[i] = 31;
		else str[i] = 30;
	}

	str[len] = 0;
	Draw_String ((vid.currsize->width / 2) - (len * 4) - 4, y, str);
}


char hud_scoreboardtext[MAX_SCOREBOARD][64];
int hud_fragsort[MAX_SCOREBOARD];
int hud_scoreboardtop[MAX_SCOREBOARD];
int hud_scoreboardbottom[MAX_SCOREBOARD];
int hud_scoreboardcount[MAX_SCOREBOARD];
int hud_scoreboardlines;

/*
===============
HUD_SortFrags
===============
*/
void HUD_SortFrags (void)
{
	int		i, j, k;

	// sort by frags
	hud_scoreboardlines = 0;

	for (i = 0; i < cl.maxclients; i++)
	{
		if (cl.scores[i].name[0])
		{
			hud_fragsort[hud_scoreboardlines] = i;
			hud_scoreboardlines++;
		}
	}

	for (i = 0; i < hud_scoreboardlines; i++)
	{
		for (j = 0; j < hud_scoreboardlines - 1 - i; j++)
		{
			if (cl.scores[hud_fragsort[j]].frags < cl.scores[hud_fragsort[j + 1]].frags)
			{
				k = hud_fragsort[j];
				hud_fragsort[j] = hud_fragsort[j + 1];
				hud_fragsort[j + 1] = k;
			}
		}
	}
}


int	HUD_ColorForMap (int m)
{
	return m < 128 ? m + 8 : m + 8;
}


void HUD_DrawFrags (void)
{
	if (cl.maxclients < 2) return;

	if (!(hud_ibar[cl_sbar.integer].flags & HUD_VISIBLE)) return;

	int i, k, l;
	int top, bottom;
	int x, y, f;
	int xofs;
	char num[12];
	scoreboard_t *s;

	HUD_SortFrags ();

	// draw the text (only room for 4)
	l = hud_scoreboardlines <= 4 ? hud_scoreboardlines : 4;

	x = 23;

	// positioning is locked to the ibar position
	xofs = HUD_GetX (&hud_ibar[cl_sbar.integer]);
	y = HUD_GetY (&hud_ibar[cl_sbar.integer]);

	for (i = 0; i < l; i++)
	{
		k = hud_fragsort[i];
		s = &cl.scores[k];

		if (!s->name[0]) continue;

		// draw background
		top = s->colors & 0xf0;
		bottom = (s->colors & 15) << 4;
		top = HUD_ColorForMap (top);
		bottom = HUD_ColorForMap (bottom);

		Draw_Fill (xofs + x * 8 + 10, y + 1, 28, 4, top);
		Draw_Fill (xofs + x * 8 + 10, y + 5, 28, 3, bottom);

		// draw number
		f = s->frags;
		_snprintf (num, 12, "%3i", f);

		Draw_Character (xofs + (x + 1) * 8, y, num[0]);
		Draw_Character (xofs + (x + 2) * 8, y, num[1]);
		Draw_Character (xofs + (x + 3) * 8, y, num[2]);

		if (k == cl.viewentity - 1)
		{
			Draw_Character (xofs + x * 8 + 6, y, 16);
			Draw_Character (xofs + (x + 4) * 8, y, 17);
		}

		x += 4;
	}
}


char *HUD_GetMapName (void)
{
	char *mapname = (char *) scratchbuf;

	if (!cls.maprunning)
	{
		strcpy (mapname, "null");
		return mapname;
	}

	strcpy (mapname, &cl.worldmodel->name[5]);

	for (int i = strlen (mapname); i; i--)
	{
		if (mapname[i] == '.')
		{
			mapname[i] = 0;
			break;
		}
	}

	return mapname;
}


void HUD_DrawLevelName (char *levelname, int x, int y)
{
	// matches max com_token
	char str[1024];

	// level name
	Q_strncpy (str, levelname, 1023);

	// deal with 'cute' messages in the level name (die die die)
	for (int i = 0;; i++)
	{
		if (!str[i]) break;

		if (str[i] == '\n')
		{
			str[i] = 0;
			break;
		}

		if (i == 39)
		{
			str[i] = 0;
			break;
		}
	}

	int l = strlen (str);

	for (int i = 0;; i++)
	{
		if (str[i])
			str[i] += 128;
		else break;
	}

	if (l < 40)
		Draw_String (x - l * 4, y, str);
	else
	{
		int ofs = ((int) (cl.time * 30)) % (l * 8);

		Draw_String (x - ofs, y, str);
	}
}


void HUD_DrawElapsedTime (int x, int y, float time)
{
	char str[64];

	// calculate time
	int minutes = time / 60;
	int seconds = time - 60 * minutes;
	int tens = seconds / 10;
	int units = seconds - 10 * tens;

	_snprintf (str, 80, "Time: %i:%i%i", minutes, tens, units);

	int l = strlen (str);

	Draw_String (x - l * 4, y, str);
}


void HUD_CenterMessage (int x, int y, char *str)
{
	int l = strlen (str);

	Draw_String (x - l * 4, y, str);
}


void CL_QueuePingCommand (void);
void CL_QueueStatusCommand (void);

void HUD_DeathmatchOverlay (void)
{
	extern cvar_t pq_scoreboard_pings;

	// update scoreboard pings every 5 seconds; force immediate update if we haven't had one at all yet
	if (((cl.lastpingtime < cl.time - 5) || (cl.lastpingtime < 0.1)) && pq_scoreboard_pings.value && cl.Protocol == PROTOCOL_VERSION_NQ)
	{
		CL_QueuePingCommand ();
		cl.lastpingtime = cl.time;
	}

	// JPG 1.05 - check to see if we should update IP status
	if (iplog_size && ((cl.laststatustime < cl.time - 5) || (cl.laststatustime < 0.1)))
	{
		CL_QueueStatusCommand ();
		cl.laststatustime = cl.time;
	}

	char str[128];
	int l, i, x, y, f, k;
	scoreboard_t *s;
	int top, bottom;
	char num[12];

	Draw_Pic ((vid.currsize->width - gfx_ranking_lmp->width) / 2, 32, gfx_ranking_lmp, 1, true);

	// base X
	x = vid.currsize->width / 2;

	// starting Y
	y = 32 + gfx_ranking_lmp->height + 8;

	HUD_DrawLevelName (va ("%s (%s)", cl.levelname, HUD_GetMapName ()), x, y);
	y += 12;

	HUD_DividerLine (y, 16);

	// time elapsed
	y += 12;
	HUD_DrawElapsedTime (x, y, cl.time);

	// need to get this up here so that we have valid values for ping checking
	HUD_SortFrags ();
	l = hud_scoreboardlines;

	// determine if we have pings
	bool have_cl_pings = false;

	// only ping on prot 15 servers
	if (pq_scoreboard_pings.value && cl.Protocol == PROTOCOL_VERSION_NQ)
	{
		for (i = 0; i < l; i++)
		{
			k = hud_fragsort[i];
			s = &cl.scores[k];

			if (!s->name[0]) continue;

			if (s->ping)
			{
				have_cl_pings = true;
				break;
			}
		}
	}

	y += 12;
	HUD_DividerLine (y, 16);

	x = 80 + ((vid.currsize->width - 320) * 0.5);
	y += 12;

	for (i = 0; i < l; i++)
	{
		k = hud_fragsort[i];
		s = &cl.scores[k];

		if (!s->name[0]) continue;

		// draw background
		top = HUD_ColorForMap (s->colors & 0xf0);
		bottom = HUD_ColorForMap ((s->colors & 15) << 4);

		Draw_Fill (x, y, 40, 4, top);
		Draw_Fill (x, y + 4, 40, 4, bottom);

		// draw number
		f = s->frags;
		_snprintf (num, 12, "%3i", f);

		Draw_Character (x + 8 , y, num[0]);
		Draw_Character (x + 16 , y, num[1]);
		Draw_Character (x + 24 , y, num[2]);

		// mark us
		if (k == cl.viewentity - 1)
		{
			Draw_Character (x, y, 16);
			Draw_Character (x + 32, y, 17);
		}

		// pings
		// if we don't have ping times that means that the server didn't send them so don't show them at all
		if (have_cl_pings)
		{
			_snprintf (num, 12, "%4ims", s->ping > 9999 ? 9999 : s->ping);

			Draw_Character (x + 56 , y, num[0]);
			Draw_Character (x + 64 , y, num[1]);
			Draw_Character (x + 72 , y, num[2]);
			Draw_Character (x + 80 , y, num[3]);
			Draw_Character (x + 88 , y, num[4]);
			Draw_Character (x + 96 , y, num[5]);

			// draw name
			Draw_String (x + 128, y, s->name);
		}
		else
			Draw_String (x + 64, y, s->name);

		y += 12;
	}
}


void HUD_SoloScoreboard (qpic_t *pic, float solotime)
{
	char str[128];
	int l;
	int i;
	int SBX;
	int SBY;

	Draw_Pic ((vid.currsize->width - pic->width) / 2, 48, pic, 1, true);

	// base X
	SBX = vid.currsize->width / 2;

	// starting Y
	SBY = 48 + pic->height + 10;

	// level name
	HUD_DrawLevelName (va ("%s (%s)", cl.levelname, HUD_GetMapName ()), SBX, SBY);
	SBY += 12;

	HUD_DividerLine (SBY, 16);

	if (cl.gametype != GAME_DEATHMATCH)
	{
		// kill count - remake quake compatibility
		if (cl.stats[STAT_TOTALMONSTERS] == 0)
			_snprintf (str, 80, "Kills: %i", cl.stats[STAT_MONSTERS]);
		else _snprintf (str, 80, "Kills: %i/%i", cl.stats[STAT_MONSTERS], cl.stats[STAT_TOTALMONSTERS]);

		l = strlen (str);
		SBY += 12;
		Draw_String (SBX - l * 4, SBY, str);

		// secrets count - remake quake compatibility
		if (cl.stats[STAT_TOTALSECRETS] == 0)
			_snprintf (str, 80, "Secrets: %i", cl.stats[STAT_SECRETS]);
		else _snprintf (str, 80, "Secrets: %i/%i", cl.stats[STAT_SECRETS], cl.stats[STAT_TOTALSECRETS]);

		l = strlen (str);
		SBY += 12;
		Draw_String (SBX - l * 4, SBY, str);
	}

	// time elapsed
	SBY += 12;
	HUD_DrawElapsedTime (SBX, SBY, solotime);

	if (cl.gametype == GAME_DEATHMATCH) return;

	// skill
	switch ((int) skill.value)
	{
	case 0:
		strcpy (str, "Skill: Easy");
		break;

	case 1:
		strcpy (str, "Skill: Normal");
		break;

	case 2:
		strcpy (str, "Skill: Hard");
		break;

	case 3:
		strcpy (str, "Skill: NIGHTMARE");
		break;
	}

	l = strlen (str);
	SBY += 12;
	Draw_String (SBX - l * 4, SBY, str);
}


void HUD_DrawFace (int ActiveItems, int HealthStat)
{
	int f, anim;
	int x = HUD_GetX (&hud_facepic[cl_sbar.integer]);
	int y = HUD_GetY (&hud_facepic[cl_sbar.integer]);
	qpic_t *facepic = NULL;

	if ((ActiveItems & (IT_INVISIBILITY | IT_INVULNERABILITY)) == (IT_INVISIBILITY | IT_INVULNERABILITY))
		facepic = sb_face_invis_invuln;
	else if (ActiveItems & IT_QUAD)
		facepic = sb_face_quad;
	else if (ActiveItems & IT_INVISIBILITY)
		facepic = sb_face_invis;
	else if (ActiveItems & IT_INVULNERABILITY)
		facepic = sb_face_invuln;
	else
	{
		// fix crash when health goes below -19
		if (HealthStat >= 100)
			f = 4;
		else if (HealthStat < 0)
			f = 0;
		else f = HealthStat / 20;

		if (cl.time <= cl.faceanimtime)
			anim = 1;
		else anim = 0;

		facepic = sb_faces[f][anim];
	}

	if (hud_drawfull)
	{
		facepic = sb_face_quad;
		HealthStat = 200;
	}

	Draw_Pic (x, y, facepic, 1, true);

	x = HUD_GetX (&hud_faceval[cl_sbar.integer]);
	y = HUD_GetY (&hud_faceval[cl_sbar.integer]);

	HUD_DrawNum (x, y, HealthStat, 3, HealthStat <= 25);
}


void HUD_DrawArmor (int ActiveItems, int ArmorStat)
{
	int x, y;
	qpic_t *armorpic = NULL;
	int armorval = 0;

	if (ActiveItems & IT_INVULNERABILITY)
	{
		// note - be sure to switch the number to red!
		armorval = 666;
		armorpic = draw_disc;
	}
	else
	{
		armorval = ArmorStat;

		if (kurok)
		{
			if (ActiveItems & KIT_ARMOR3)
				armorpic = sb_armor[2];
			else if (ActiveItems & KIT_ARMOR2)
				armorpic = sb_armor[1];
			else if (ActiveItems & KIT_ARMOR1)
				armorpic = sb_armor[0];
		}
		else if (rogue)
		{
			// rogue hackery
			if (ActiveItems & RIT_ARMOR3)
				armorpic = sb_armor[2];
			else if (ActiveItems & RIT_ARMOR2)
				armorpic = sb_armor[1];
			else if (ActiveItems & RIT_ARMOR1)
				armorpic = sb_armor[0];
		}
		else
		{
			// normal sane way of doing things
			if (ActiveItems & IT_ARMOR3)
				armorpic = sb_armor[2];
			else if (ActiveItems & IT_ARMOR2)
				armorpic = sb_armor[1];
			else if (ActiveItems & IT_ARMOR1)
				armorpic = sb_armor[0];
		}
	}

	if (hud_drawfull)
	{
		armorpic = sb_armor[2];
		armorval = 200;
	}

	if (armorpic)
	{
		x = HUD_GetX (&hud_armorpic[cl_sbar.integer]);
		y = HUD_GetY (&hud_armorpic[cl_sbar.integer]);

		Draw_Pic (x, y, armorpic, 1, true);
	}

	if (!armorval && (hud_armorval[cl_sbar.integer].flags & HUD_HIDEIF0)) return;

	x = HUD_GetX (&hud_armorval[cl_sbar.integer]);
	y = HUD_GetY (&hud_armorval[cl_sbar.integer]);

	HUD_DrawNum (x, y, armorval, 3, armorval <= 25 || armorval == 666);
}


void Draw_Crosshair (int x, int y);

void HUD_DrawCrossHair (int basex, int basey)
{
	// adjust for chosen positioning
	basex += cl_crossx.value;
	basey += cl_crossy.value;

	Draw_Crosshair (basex, basey);
}


void HUD_DrawAmmo (int ActiveItems, int AmmoStat)
{
	int x, y;
	qpic_t *ammopic = NULL;

	// ammo icon
	if (kurok)
	{
		if (ActiveItems & KIT_SHELLS)
			ammopic = sb_ammo[0];
		else if (ActiveItems & KIT_NAILS)
			ammopic = sb_ammo[1];
		else if (ActiveItems & KIT_ROCKETS)
			ammopic = sb_ammo[2];
		else if (ActiveItems & KIT_CELLS)
			ammopic = sb_ammo[3];
		else if (ActiveItems & KIT_50CAL)
			ammopic = ksb_ammo[0];
	}
	else if (rogue)
	{
		// rogue hackery
		if (ActiveItems & RIT_SHELLS)
			ammopic = sb_ammo[0];
		else if (ActiveItems & RIT_NAILS)
			ammopic = sb_ammo[1];
		else if (ActiveItems & RIT_ROCKETS)
			ammopic = sb_ammo[2];
		else if (ActiveItems & RIT_CELLS)
			ammopic = sb_ammo[3];
		else if (ActiveItems & RIT_LAVA_NAILS)
			ammopic = rsb_ammo[0];
		else if (ActiveItems & RIT_PLASMA_AMMO)
			ammopic = rsb_ammo[1];
		else if (ActiveItems & RIT_MULTI_ROCKETS)
			ammopic = rsb_ammo[2];
	}
	else
	{
		// normal sane way of doing things
		if (ActiveItems & IT_SHELLS)
			ammopic = sb_ammo[0];
		else if (ActiveItems & IT_NAILS)
			ammopic = sb_ammo[1];
		else if (ActiveItems & IT_ROCKETS)
			ammopic = sb_ammo[2];
		else if (ActiveItems & IT_CELLS)
			ammopic = sb_ammo[3];
	}

	if (hud_drawfull)
	{
		ammopic = sb_ammo[1];
		AmmoStat = 200;
	}

	if (ammopic)
	{
		x = HUD_GetX (&hud_ammopic[cl_sbar.integer]);
		y = HUD_GetY (&hud_ammopic[cl_sbar.integer]);

		Draw_Pic (x, y, ammopic, 1, true);
	}

	if (!AmmoStat && (hud_ammoval[cl_sbar.integer].flags & HUD_HIDEIF0)) return;

	x = HUD_GetX (&hud_ammoval[cl_sbar.integer]);
	y = HUD_GetY (&hud_ammoval[cl_sbar.integer]);

	HUD_DrawNum (x, y, AmmoStat, 3, AmmoStat <= 10);
}


void HUD_DrawSigils (int ActiveItems)
{
	// rogue and hipnotic hackery
	if (rogue || hipnotic) return;

	// normal sane way of doing things
	int x = HUD_GetX (&hud_sigils[cl_sbar.integer]);
	int y = HUD_GetY (&hud_sigils[cl_sbar.integer]);

	if (cl_sbar.integer > 1)
	{
		// sigil background
		Draw_SubPic (x, y, sb_ibar, 288, 8, 32, 16);
	}

	for (int i = 0; i < 4; i++)
	{
		if ((ActiveItems & (1 << (28 + i))) || hud_drawfull)
			Draw_Pic (x, y, sb_sigil[i], 1, true);

		x += hud_sigils[cl_sbar.integer].hx;
		y += hud_sigils[cl_sbar.integer].hy;
	}
}


void HUD_DrawKeys (int ActiveItems)
{
	if (hipnotic)
	{
		// hipnotic hackery
		int x = HUD_GetX (&hud_hipnokeys[cl_sbar.integer]);
		int y = HUD_GetY (&hud_hipnokeys[cl_sbar.integer]);

		for (int i = 0; i < 2; i++)
		{
			if ((ActiveItems & (1 << (17 + i))) || hud_drawfull)
				Draw_Pic (x, y, sb_items[i], 1, true);

			x += hud_hipnokeys[cl_sbar.integer].hx;
			y += hud_hipnokeys[cl_sbar.integer].hy;
		}
	}
	else
	{
		// normal sane way of doing things
		int x = HUD_GetX (&hud_keys[cl_sbar.integer]);
		int y = HUD_GetY (&hud_keys[cl_sbar.integer]);

		for (int i = 0; i < 2; i++)
		{
			if ((ActiveItems & (1 << (17 + i))) || hud_drawfull)
				Draw_Pic (x, y, sb_items[i], 1, true);

			x += hud_keys[cl_sbar.integer].hx;
			y += hud_keys[cl_sbar.integer].hy;
		}
	}
}


void HUD_DrawPowerUpTimers (int ActiveItems)
{
	// to do
	if (hipnotic || rogue) return;

	// normal same way of doing things
	int x = HUD_GetX (&hud_items[cl_sbar.integer]);
	int y = HUD_GetY (&hud_items[cl_sbar.integer]);

	char str[16];

	for (int i = 2; i < 6; i++)
	{
		int CurrentItem = 17 + i;

		if (ActiveItems & (1 << CurrentItem))
		{
			int seconds = 30 - (cl.time - cl.itemgettime[CurrentItem]);

			if (seconds > 0)
			{
				sprintf (str, "%02i", seconds);

				// we need to scrunch these together a little to make room
				if (str[0] != ' ') Draw_Character (x + 1, y - (cl_sbar.integer > 1 ? 12 : 20), 18 + str[0] - '0');
				if (str[1] != ' ') Draw_Character (x + 7, y - (cl_sbar.integer > 1 ? 12 : 20), 18 + str[1] - '0');
			}
		}

		x += hud_items[cl_sbar.integer].hx;
		y += hud_items[cl_sbar.integer].hy;
	}
}


void HUD_DrawItems (int ActiveItems)
{
	// normal same way of doing things
	int x = HUD_GetX (&hud_items[cl_sbar.integer]);
	int y = HUD_GetY (&hud_items[cl_sbar.integer]);

	for (int i = 2; i < 6; i++)
	{
		int CurrentItem = 17 + i;

		if (ActiveItems & (1 << CurrentItem))
			Draw_Pic (x, y, sb_items[i], 1, true);

		x += hud_items[cl_sbar.integer].hx;
		y += hud_items[cl_sbar.integer].hy;
	}

	// note - these overwrite the sigils slots so we just continue where we left off
	if (hipnotic)
	{
		// hipnotic hackery
		for (int i = 0; i < 2; i++)
		{
			if ((ActiveItems & (1 << (24 + i))) || hud_drawfull)
				Draw_Pic (x, y, hsb_items[i], 1, true);

			x += hud_items[cl_sbar.integer].hx;
			y += hud_items[cl_sbar.integer].hy;
		}
	}

	if (rogue)
	{
		// rogue hackery
		for (int i = 0; i < 2; i++)
		{
			if ((ActiveItems & (1 << (29 + i))) || hud_drawfull)
				Draw_Pic (x, y, rsb_items[i], 1, true);

			x += hud_items[cl_sbar.integer].hx;
			y += hud_items[cl_sbar.integer].hy;
		}
	}
}


void HUD_DrawAmmoCounts (int ac1, int ac2, int ac3, int ac4, int ActiveWeapon)
{
	int x, y;
	char num[10];
	int ActiveAmmo[] = {ac1, ac2, ac3, ac4};

	x = HUD_GetX (&hud_ammocount[cl_sbar.integer]);
	y = HUD_GetY (&hud_ammocount[cl_sbar.integer]);

	for (int i = 0; i < 4; i++)
	{
		if (cl_sbar.integer > 1)
		{
			// background pic at x/y
			Draw_SubPic (x - 4, y, sb_ibar, 3 + (i * 48), 0, 42, 10);
		}

		_snprintf (num, 10, "%3i", ActiveAmmo[i]);

		if (num[0] != ' ') Draw_Character (x, y, 18 + num[0] - '0');
		if (num[1] != ' ') Draw_Character (x + 8, y, 18 + num[1] - '0');
		if (num[2] != ' ') Draw_Character (x + 16, y, 18 + num[2] - '0');

		x += hud_ammocount[cl_sbar.integer].hx;
		y += hud_ammocount[cl_sbar.integer].hy;
	}
}


void HUD_DrawWeapons (int ActiveItems, int ActiveWeapon)
{
	// normal sane way of doing things
	int x = HUD_GetX (&hud_weapons[cl_sbar.integer]);
	int y = HUD_GetY (&hud_weapons[cl_sbar.integer]);

	// save baseline x and y for hipnotic and rogue hackery
	int savedx[8];
	int savedy[8];

	for (int i = 0; i < 7; i++)
	{
		// save baseline x and y for hipnotic and rogue hackery
		savedx[i] = x;
		savedy[i] = y;

		if ((ActiveItems & (IT_SHOTGUN << i)) || hud_drawfull)
		{
			float time = cl.itemgettime[i];
			int flashon = (int) ((cl.time - time) * 10);

			if (flashon >= 10)
			{
				if (ActiveWeapon == (IT_SHOTGUN << i))
					flashon = 1;
				else flashon = 0;
			}
			else flashon = (flashon % 5) + 2;

			// handle wider lightning gun pic
			if (cl_sbar.value > 1)
				Draw_Pic (x - sb_weapons[flashon][i]->width, y, sb_weapons[flashon][i], 1, true);
			else Draw_Pic (x, y, sb_weapons[flashon][i], 1, true);
		}

		x += hud_weapons[cl_sbar.integer].hx;
		y += hud_weapons[cl_sbar.integer].hy;
	}

	// now here's where it starts to get REAL ugly...
	if (hipnotic)
	{
		// hipnotic hackery
		// jesus this is REALLY ugly stuff...
		int grenadeflashing = 0;

		for (int i = 0; i < 4; i++)
		{
			if ((ActiveItems & (1 << hipweapons[i])) || hud_drawfull)
			{
				float time = cl.itemgettime[hipweapons[i]];
				int flashon = (int) ((cl.time - time) * 10);
				qpic_t *hipnopic = NULL;
				int hipnox = x;
				int hipnoy = y;

				if (flashon >= 10)
				{
					if (ActiveWeapon == (1 << hipweapons[i]))
						flashon = 1;
					else flashon = 0;
				}
				else flashon = (flashon % 5) + 2;

				// check grenade launcher
				switch (i)
				{
				case 2:

					if (ActiveItems & HIT_PROXIMITY_GUN)
					{
						if (flashon)
						{
							grenadeflashing = 1;
							hipnopic = hsb_weapons[flashon][2];
							hipnox = savedx[4];
							hipnoy = savedy[4];
						}
					}

					break;

				case 3:

					if (ActiveItems & (IT_SHOTGUN << 4))
					{
						if (flashon && !grenadeflashing)
						{
							hipnopic = hsb_weapons[flashon][3];
							hipnox = savedx[4];
							hipnoy = savedy[4];
						}
						else if (!grenadeflashing)
						{
							hipnopic = hsb_weapons[0][3];
							hipnox = savedx[4];
							hipnoy = savedy[4];
						}
					}
					else
					{
						hipnopic = hsb_weapons[flashon][4];
						hipnox = savedx[4];
						hipnoy = savedy[4];
					}

					break;

				default:
					hipnopic = hsb_weapons[flashon][i];
				}

				if (hipnopic)
				{
					if (cl_sbar.value > 1)
						Draw_Pic (hipnox - hipnopic->width, hipnoy, hipnopic, 1, true);
					else Draw_Pic (hipnox, hipnoy, hipnopic, 1, true);
				}
			}

			x += hud_weapons[cl_sbar.integer].hx;
			y += hud_weapons[cl_sbar.integer].hy;
		}
	}

	if (rogue)
	{
		// rogue hackery; not as ugly as hipnotic hackery but still hackery all the same
		// check for powered up weapon.
		if (ActiveWeapon >= RIT_LAVA_NAILGUN)
		{
			for (int i = 0; i < 5; i++)
			{
				if ((ActiveWeapon == (RIT_LAVA_NAILGUN << i)) || hud_drawfull)
				{
					// handle pic widths
					if (cl_sbar.value > 1)
						Draw_Pic (savedx[i + 2] - rsb_weapons[i]->width, savedy[i + 2], rsb_weapons[i], 1, true);
					else Draw_Pic (savedx[i + 2], savedy[i + 2], rsb_weapons[i], 1, true);
				}
			}
		}
	}
}


void HUD_DrawTeamColors (void)
{
	// couldn't be arsed reversing the logic and changing && to || here...
	if (!rogue) return;

	int x = HUD_GetX (&hud_teamcolor[cl_sbar.integer]);
	int y = HUD_GetY (&hud_teamcolor[cl_sbar.integer]);

	if (hud_drawfull)
	{
		Draw_Pic (x, y, rsb_teambord, 1, true);
		return;
	}

	if (!((cl.maxclients != 1) && (teamplay.value > 3) && (teamplay.value < 7))) return;

	// these aren't as precisely aligned as they could be, and may tend to move about a little
	// as gl_conscale changes.  i don't suppose many folks play rogue ctf these days anyway.
	// this routine was an ugly piece of hackery to begin with anyway
	int				top, bottom;
	int				xofs;
	char			num[12];
	scoreboard_t	*s;
	int				f;

	s = &cl.scores[cl.viewentity - 1];

	// draw background
	top = s->colors & 0xf0;
	bottom = (s->colors & 15) << 4;
	top = HUD_ColorForMap (top);
	bottom = HUD_ColorForMap (bottom);

	Draw_Pic (x, y, rsb_teambord, 1, true);

	Draw_Fill (x + 2, y + 2, 21, 10, top);
	Draw_Fill (x + 2, y + 12, 21, 10, bottom);

	// draw number
	f = s->frags;
	_snprintf (num, 12, "%3i", f);

	if (top == 8)
	{
		if (num[0] != ' ') Draw_Character (x + 2, y + 2, 18 + num[0] - '0');

		if (num[1] != ' ') Draw_Character (x + 9, y + 2, 18 + num[1] - '0');

		if (num[2] != ' ') Draw_Character (x + 16, y + 2, 18 + num[2] - '0');
	}
	else
	{
		Draw_Character (x + 2, y + 2, num[0]);
		Draw_Character (x + 9, y + 2, num[1]);
		Draw_Character (x + 16, y + 2, num[2]);
	}
}


void HUD_DrawSBar (void)
{
	// basic layout
	if (hud_sbar[cl_sbar.integer].flags & HUD_VISIBLE)
	{
		// status bar background
		int x = HUD_GetX (&hud_sbar[cl_sbar.integer]);
		int y = HUD_GetY (&hud_sbar[cl_sbar.integer]);

		if (cl_sbar.integer == 1)
			Draw_Pic (x, y, sb_sbar, scr_sbaralpha.value, true);
		else Draw_Pic (x, y, sb_sbar, 1.0f, true);
	}
}


void HUD_DrawIBar (int ActiveWeapon, bool DrawFrags)
{
	if (hud_ibar[cl_sbar.integer].flags & HUD_VISIBLE)
	{
		// inventory bar background
		qpic_t *ibarpic = sb_ibar;

		if (rogue)
		{
			// rogue switches the inventory bar - bastards
			if (ActiveWeapon >= RIT_LAVA_NAILGUN)
				ibarpic = rsb_invbar[0];
			else ibarpic = rsb_invbar[1];
		}

		int x = HUD_GetX (&hud_ibar[cl_sbar.integer]);
		int y = HUD_GetY (&hud_ibar[cl_sbar.integer]);

		if (cl_sbar.integer == 1)
			Draw_Pic (x, y, ibarpic, scr_sbaralpha.value, true);
		else Draw_Pic (x, y, ibarpic, 1.0f, true);

		// the 4 mini frag-lists are only drawn on this one
		if (DrawFrags) HUD_DrawFrags ();
	}
}


void HUD_MiniDMOverlayItem (int fragsort, int x, int y, int top, int bottom, int frags, char *name)
{
	char num[12];

	Draw_Fill (x, y + 1, 40, 3, top);
	Draw_Fill (x, y + 4, 40, 4, bottom);

	_snprintf (num, 12, "%3i", frags);

	Draw_Character (x + 8, y, num[0]);
	Draw_Character (x + 16, y, num[1]);
	Draw_Character (x + 24, y, num[2]);

	if (fragsort == cl.viewentity - 1)
	{
		Draw_Character (x, y, 16);
		Draw_Character (x + 32, y, 17);
	}

	// draw name
	Draw_String (x + 48, y, name);
}


void HUD_MiniDeathmatchOverlay (void)
{
	int x = HUD_GetX (&hud_dmoverlay[cl_sbar.integer]);
	int y = HUD_GetY (&hud_dmoverlay[cl_sbar.integer]);

	if (!scr_centersbar.integer && cl_sbar.integer < 2)
	{
		// make room for the team colours in rogue
		if (rogue)
			x += 344;
		else x += 320;
	}

	if (hud_drawfull)
	{
		for (int n = 0; n < 4; n++)
		{
			HUD_MiniDMOverlayItem (n, x, y, n * 2, 12 - n * 2, 666, va ("player %i", n));
			y += 10;
		}

		return;
	}

	if (cl.gametype != GAME_DEATHMATCH) return;

	// don't need the mini overlay if the full is showing
	if ((hud_showscores && !cls.demoplayback) || (hud_showdemoscores && cls.demoplayback)) return;

	// scores
	HUD_SortFrags ();

	// draw the text
	int i = 0, l = hud_scoreboardlines;

	//find us
	for (i = 0; i < hud_scoreboardlines; i++)
		if (hud_fragsort[i] == cl.viewentity - 1)
			break;

	if (i == hud_scoreboardlines) i = 0;
	if (i < 0) i = 0;

	// no more than 4 lines in the mini overlay
	for (int n = 0; i < hud_scoreboardlines && n < 4; i++, n++)
	{
		scoreboard_t *s = &cl.scores[hud_fragsort[i]];

		if (!s->name[0]) continue;

		// draw background
		int top = HUD_ColorForMap (s->colors & 0xf0);
		int bottom = HUD_ColorForMap ((s->colors & 15) << 4);

		HUD_MiniDMOverlayItem (hud_fragsort[i], x, y, top, bottom, s->frags, s->name);

		y += 10;
	}
}


void HUD_DrawFPS (void)
{
	static float	oldtime = 0;
	static float	fps = 0;
	static int		oldframecount = 0;

	float time = realtime - oldtime;

	if (time < 0)
	{
		oldtime = realtime;
		oldframecount = d3d_RenderDef.framecount;
		return;
	}

	if (time > 0.25f) // update value every 1/4 second
	{
		fps = (float) (d3d_RenderDef.framecount - oldframecount) / time;
		oldtime = realtime;
		oldframecount = d3d_RenderDef.framecount;
	}

	if (scr_showfps.value)
	{
		// positioning
		char str[17];

		// position over the main refresh so that it will always update properly
		int y1 = 40 + sb_lines;
		int y2 = 11 + sb_lines;

		// sometimes between level transitions we get a < 0 fps so don't let it happen
		if (fps < 0)
			_snprintf (str, 16, "0 fps");
		else _snprintf (str, 16, "%i fps", (int) (fps + 0.5f));

		int x = vid.currsize->width - (strlen (str) * 8 + 4);

		if (cl_sbar.integer > 2)
			Draw_String (x, vid.currsize->height - y1, str);
		else Draw_String (x, vid.currsize->height - y2, str);
	}
}


void HUD_DrawClock (void)
{
	char	str[9];

	if (scr_clock.value)
	{
		int seconds = (int) cl.time;
		int minutes = (int) (cl.time / 60);
		seconds -= minutes * 60;

		_snprintf (str, 9, "%02i:%i%i", minutes, seconds / 10, seconds % 10);
	}
	else return;

	// position over the main refresh so that it will always update properly
	int y1 = 50 + sb_lines;
	int y2 = 21 + sb_lines;

	// positioning
	if (cl_sbar.integer > 2)
		Draw_String (vid.currsize->width - (strlen (str) * 8 + 4), vid.currsize->height - y1, str);
	else Draw_String (vid.currsize->width - (strlen (str) * 8 + 4), vid.currsize->height - y2, str);
}


void HUD_DrawOSDItems (void)
{
	HUD_DrawFPS ();
	HUD_DrawClock ();
}


void HUD_DrawHUD (void)
{
	// no HUD conditions
	if (!cls.maprunning) return;
	if (cl.intermission) return;
	if (scr_viewsize.value > 111) return;
	if (cls.state != ca_connected) return;

	// set our scaling factor here
	D3DDraw_SetSize (&vid.sbarsize);

	// bound sbar
	if (cl_sbar.integer < 0) Cvar_Set (&cl_sbar, "0");
	if (cl_sbar.integer > 3) Cvar_Set (&cl_sbar, "3");

	if (cl.stats[STAT_HEALTH] > 0)
	{
		// add crosshair drawing here because it's drawn over the main view (offset so it's correctly centered)
		HUD_DrawCrossHair (vid.currsize->width / 2, (vid.currsize->height - sb_lines) / 2);
	}

	// OSD Items (always draw even if dead)
	HUD_DrawOSDItems ();

	// always draw the scoreboards if active
	if ((hud_showscores && !cls.demoplayback) || (hud_showdemoscores && cls.demoplayback) || cl.stats[STAT_HEALTH] < 1)
	{
		// only show them in-game
		if (key_dest == key_game)
		{
			// scoreboard
			if (cl.gametype == GAME_DEATHMATCH)
				HUD_DeathmatchOverlay ();
			else HUD_SoloScoreboard (gfx_ranking_lmp, cl.time);
		}
	}

	if (cl.stats[STAT_HEALTH] > 0)
	{
		// the HUD always redraws every frame as doing so allows fast clears to work properly
		Draw_TileClear ();

		// draw elements
		// note - team colors must be drawn after the face as they overlap in the default layout
		if (!kurok) HUD_DrawSBar ();
		HUD_DrawFace (cl.items, cl.stats[STAT_HEALTH]);
		if (!kurok) HUD_DrawTeamColors ();
		HUD_DrawArmor (cl.items, cl.stats[STAT_ARMOR]);
		HUD_DrawAmmo (cl.items, cl.stats[STAT_AMMO]);

		// no inventory
		if (!(scr_viewsize.value > 101))
		{
			// inventory
			if (!kurok) HUD_DrawIBar (cl.stats[STAT_ACTIVEWEAPON], true);
			if (!kurok) HUD_DrawSigils (cl.items);
			HUD_DrawKeys (cl.items);
			HUD_DrawItems (cl.items);
			if (!kurok) HUD_DrawWeapons (cl.items, cl.stats[STAT_ACTIVEWEAPON]);

			// ammocounts are left till last because they don't batch with everything else
			if (!kurok) HUD_DrawAmmoCounts (cl.stats[STAT_SHELLS], cl.stats[STAT_NAILS], cl.stats[STAT_ROCKETS], cl.stats[STAT_CELLS], cl.stats[STAT_ACTIVEWEAPON]);
			if (!kurok) HUD_DrawPowerUpTimers (cl.items);

			// deathmatch overlay
			HUD_MiniDeathmatchOverlay ();
		}
	}
}


void HUD_DrawStuffGotten (int x, int y, int numstuff, int totalstuff)
{
	char stuffstr[64];

	if (totalstuff)
		sprintf (stuffstr, "%4i/%i", numstuff, totalstuff);
	else sprintf (stuffstr, "%4i", numstuff);

	for (int i = 0;; i++)
	{
		if (!stuffstr[i]) break;

		if (stuffstr[i] == '/')
		{
			Draw_Pic (x, y, sb_slash, 1, true);
			x += 16;
		}
		else if (stuffstr[i] == ' ')
			x += 24;
		else
		{
			Draw_Pic (x, y, sb_nums[0][stuffstr[i] - '0'], 1, true);
			x += 24;
		}
	}
}


void HUD_IntermissionOverlay (void)
{
	if (!cls.maprunning) return;

	D3DDraw_SetSize (&vid.sbarsize);

#if 1
	if (cl.gametype == GAME_DEATHMATCH)
		HUD_DeathmatchOverlay ();
	else HUD_SoloScoreboard (gfx_complete_lmp, cl.completed_time);
#else
	if (cl.gametype == GAME_DEATHMATCH)
	{
		HUD_DeathmatchOverlay ();
		return;
	}

	// so that we can position everything correctly relative to each other and move it around as may be required
	int basex = 192;
	int basey = 24;

	Draw_Pic (basex - 96, basey, gfx_complete_lmp, 1, true);

	// because the MP folks sometimes use the level name as a "server message" instead,
	// containing additional info, etc, we need to take a copy of it, potentially truncate it, and tidy it up for display
	char levelname[60];

	for (int i = 0; i < 59; i++)
	{
		if (cl.levelname[i] < ' ') break;

		levelname[i] = cl.levelname[i];
		levelname[i + 1] = 0;

		if (!cl.levelname[i]) break;
	}

	Draw_String (basex - strlen (levelname) * 4, basey + 34, levelname);
	Draw_String (basex - 56, basey + 49, DIVIDER_LINE);
	Draw_Pic (basex - 160, basey + 64, gfx_inter_lmp, 1, true);

	int dig = cl.completed_time / 60;
	HUD_DrawStuffGotten (basex, basey + 72, dig, 0);
	int num = cl.completed_time - dig * 60;

	Draw_Pic (basex + 96, basey + 72, sb_colon, 1, true);
	Draw_Pic (basex + 112, basey + 72, sb_nums[0][num / 10], 1, true);
	Draw_Pic (basex + 136, basey + 72, sb_nums[0][num % 10], 1, true);

	HUD_DrawStuffGotten (basex, basey + 112, cl.stats[STAT_SECRETS], cl.stats[STAT_TOTALSECRETS]);
	HUD_DrawStuffGotten (basex, basey + 152, cl.stats[STAT_MONSTERS], cl.stats[STAT_TOTALMONSTERS]);
#endif
}


void HUD_FinaleOverlay (int y)
{
	D3DDraw_SetSize (&vid.sbarsize);

	if (cl.gametype == GAME_DEATHMATCH)
		HUD_DeathmatchOverlay ();
	else
	{
		Draw_Pic ((vid.currsize->width - gfx_finale_lmp->width) / 2, y - gfx_finale_lmp->height - 16, gfx_finale_lmp, 1, true);
		HUD_DividerLine (y - 12, 16);
	}
}

