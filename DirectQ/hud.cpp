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
// gpl blah etc, just read "gnu.txt" that comes with this...


#include "quakedef.h"
#include "d3d_model.h"
#include "menu_common.h"
#include "d3d_quake.h"

CQMenu menu_HUD (m_hudoptions);

extern cvar_t	crosshair;
extern cvar_t	cl_crossx;
extern cvar_t	cl_crossy;
extern cvar_t	scr_crosshairscale;
extern cvar_t	scr_crosshaircolor;

#define STAT_MINUS		10	// num frame for '-' stats digit

qpic_t		*sb_nums[2][11];
qpic_t		*sb_colon, *sb_slash;
qpic_t		*sb_ibar;
qpic_t		*sb_sbar;
qpic_t		*sb_scorebar;

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

//MED 01/04/97 added two more weapons + 3 alternates for grenade launcher
qpic_t      *hsb_weapons[7][5];   // 0 is active, 1 is owned, 2-5 are flashes
//MED 01/04/97 added array to simplify weapon parsing
int         hipweapons[4] = {HIT_LASER_CANNON_BIT,HIT_MJOLNIR_BIT,4,HIT_PROXIMITY_GUN_BIT};
//MED 01/04/97 added hipnotic items array
qpic_t      *hsb_items[2];

bool	hud_showscores = false;
bool	hud_showdemoscores = false;

void HUD_Init (void)
{
	int i;

	for (i = 0; i < 10; i++)
	{
		sb_nums[0][i] = Draw_PicFromWad (va ("num_%i", i));
		sb_nums[1][i] = Draw_PicFromWad (va ("anum_%i", i));
	}

	sb_nums[0][10] = Draw_PicFromWad ("num_minus");
	sb_nums[1][10] = Draw_PicFromWad ("anum_minus");

	sb_colon = Draw_PicFromWad ("num_colon");
	sb_slash = Draw_PicFromWad ("num_slash");

	sb_weapons[0][0] = Draw_PicFromWad ("inv_shotgun");
	sb_weapons[0][1] = Draw_PicFromWad ("inv_sshotgun");
	sb_weapons[0][2] = Draw_PicFromWad ("inv_nailgun");
	sb_weapons[0][3] = Draw_PicFromWad ("inv_snailgun");
	sb_weapons[0][4] = Draw_PicFromWad ("inv_rlaunch");
	sb_weapons[0][5] = Draw_PicFromWad ("inv_srlaunch");
	sb_weapons[0][6] = Draw_PicFromWad ("inv_lightng");

	sb_weapons[1][0] = Draw_PicFromWad ("inv2_shotgun");
	sb_weapons[1][1] = Draw_PicFromWad ("inv2_sshotgun");
	sb_weapons[1][2] = Draw_PicFromWad ("inv2_nailgun");
	sb_weapons[1][3] = Draw_PicFromWad ("inv2_snailgun");
	sb_weapons[1][4] = Draw_PicFromWad ("inv2_rlaunch");
	sb_weapons[1][5] = Draw_PicFromWad ("inv2_srlaunch");
	sb_weapons[1][6] = Draw_PicFromWad ("inv2_lightng");

	for (i = 0; i < 5; i++)
	{
		sb_weapons[2 + i][0] = Draw_PicFromWad (va("inva%i_shotgun", i + 1));
		sb_weapons[2 + i][1] = Draw_PicFromWad (va("inva%i_sshotgun", i + 1));
		sb_weapons[2 + i][2] = Draw_PicFromWad (va("inva%i_nailgun", i + 1));
		sb_weapons[2 + i][3] = Draw_PicFromWad (va("inva%i_snailgun", i + 1));
		sb_weapons[2 + i][4] = Draw_PicFromWad (va("inva%i_rlaunch", i + 1));
		sb_weapons[2 + i][5] = Draw_PicFromWad (va("inva%i_srlaunch", i + 1));
		sb_weapons[2 + i][6] = Draw_PicFromWad (va("inva%i_lightng", i + 1));
	}

	sb_ammo[0] = Draw_PicFromWad ("sb_shells");
	sb_ammo[1] = Draw_PicFromWad ("sb_nails");
	sb_ammo[2] = Draw_PicFromWad ("sb_rocket");
	sb_ammo[3] = Draw_PicFromWad ("sb_cells");

	sb_armor[0] = Draw_PicFromWad ("sb_armor1");
	sb_armor[1] = Draw_PicFromWad ("sb_armor2");
	sb_armor[2] = Draw_PicFromWad ("sb_armor3");

	sb_items[0] = Draw_PicFromWad ("sb_key1");
	sb_items[1] = Draw_PicFromWad ("sb_key2");
	sb_items[2] = Draw_PicFromWad ("sb_invis");
	sb_items[3] = Draw_PicFromWad ("sb_invuln");
	sb_items[4] = Draw_PicFromWad ("sb_suit");
	sb_items[5] = Draw_PicFromWad ("sb_quad");

	sb_sigil[0] = Draw_PicFromWad ("sb_sigil1");
	sb_sigil[1] = Draw_PicFromWad ("sb_sigil2");
	sb_sigil[2] = Draw_PicFromWad ("sb_sigil3");
	sb_sigil[3] = Draw_PicFromWad ("sb_sigil4");

	sb_faces[4][0] = Draw_PicFromWad ("face1");
	sb_faces[4][1] = Draw_PicFromWad ("face_p1");
	sb_faces[3][0] = Draw_PicFromWad ("face2");
	sb_faces[3][1] = Draw_PicFromWad ("face_p2");
	sb_faces[2][0] = Draw_PicFromWad ("face3");
	sb_faces[2][1] = Draw_PicFromWad ("face_p3");
	sb_faces[1][0] = Draw_PicFromWad ("face4");
	sb_faces[1][1] = Draw_PicFromWad ("face_p4");
	sb_faces[0][0] = Draw_PicFromWad ("face5");
	sb_faces[0][1] = Draw_PicFromWad ("face_p5");

	sb_face_invis = Draw_PicFromWad ("face_invis");
	sb_face_invuln = Draw_PicFromWad ("face_invul2");
	sb_face_invis_invuln = Draw_PicFromWad ("face_inv2");
	sb_face_quad = Draw_PicFromWad ("face_quad");

	sb_sbar = Draw_PicFromWad ("sbar");
	sb_ibar = Draw_PicFromWad ("ibar");
	sb_scorebar = Draw_PicFromWad ("scorebar");

	//MED 01/04/97 added new hipnotic weapons
	if (hipnotic)
	{
		hsb_weapons[0][0] = Draw_PicFromWad ("inv_laser");
		hsb_weapons[0][1] = Draw_PicFromWad ("inv_mjolnir");
		hsb_weapons[0][2] = Draw_PicFromWad ("inv_gren_prox");
		hsb_weapons[0][3] = Draw_PicFromWad ("inv_prox_gren");
		hsb_weapons[0][4] = Draw_PicFromWad ("inv_prox");

		hsb_weapons[1][0] = Draw_PicFromWad ("inv2_laser");
		hsb_weapons[1][1] = Draw_PicFromWad ("inv2_mjolnir");
		hsb_weapons[1][2] = Draw_PicFromWad ("inv2_gren_prox");
		hsb_weapons[1][3] = Draw_PicFromWad ("inv2_prox_gren");
		hsb_weapons[1][4] = Draw_PicFromWad ("inv2_prox");

		for (i = 0; i < 5; i++)
		{
			hsb_weapons[2 + i][0] = Draw_PicFromWad (va ("inva%i_laser", i + 1));
			hsb_weapons[2 + i][1] = Draw_PicFromWad (va ("inva%i_mjolnir", i + 1));
			hsb_weapons[2 + i][2] = Draw_PicFromWad (va ("inva%i_gren_prox", i + 1));
			hsb_weapons[2 + i][3] = Draw_PicFromWad (va ("inva%i_prox_gren", i + 1));
			hsb_weapons[2 + i][4] = Draw_PicFromWad (va ("inva%i_prox", i + 1));
		}

		hsb_items[0] = Draw_PicFromWad ("sb_wsuit");
		hsb_items[1] = Draw_PicFromWad ("sb_eshld");
	}

	if (rogue)
	{
		rsb_invbar[0] = Draw_PicFromWad ("r_invbar1");
		rsb_invbar[1] = Draw_PicFromWad ("r_invbar2");

		rsb_weapons[0] = Draw_PicFromWad ("r_lava");
		rsb_weapons[1] = Draw_PicFromWad ("r_superlava");
		rsb_weapons[2] = Draw_PicFromWad ("r_gren");
		rsb_weapons[3] = Draw_PicFromWad ("r_multirock");
		rsb_weapons[4] = Draw_PicFromWad ("r_plasma");

		rsb_items[0] = Draw_PicFromWad ("r_shield1");
		rsb_items[1] = Draw_PicFromWad ("r_agrav1");

		// PGM 01/19/97 - team color border
		rsb_teambord = Draw_PicFromWad ("r_teambord");
		// PGM 01/19/97 - team color border

		rsb_ammo[0] = Draw_PicFromWad ("r_ammolava");
		rsb_ammo[1] = Draw_PicFromWad ("r_ammomulti");
		rsb_ammo[2] = Draw_PicFromWad ("r_ammoplasma");
	}
}


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

// specify a HUD to load on startup
cvar_t hud_defaulthud ("defaulthud", "classichud", CVAR_ARCHIVE);

// switch on or off center alignment for entire HUD
cvar_t hud_centerhud ("hud_centerhud", "1", CVAR_ARCHIVE);

// whether to autoload or autosave the HUD
cvar_t hud_autoload ("hud_autoload", "0", CVAR_ARCHIVE);
cvar_t hud_autosave ("hud_autosave", "0", CVAR_ARCHIVE);

// called scr_* for consistency with other engines
cvar_t scr_showfps ("scr_showfps", 0.0f, CVAR_ARCHIVE);
cvar_t scr_clock ("scr_clock", 0.0f, CVAR_ARCHIVE);

// allow a custom HUD layout
// positive = from left (or from top, for y)
// negative = from right (or from bottom)
// some of these have moved to archive cvars as there is a significant perf boost to be had from using them
cvar_t hud_overlay ("hud_overlay", "0", CVAR_HUD | CVAR_ARCHIVE);

cvar_t hud_dmoverlay_x ("hud_dmoverlay_x", 8, CVAR_HUD);
cvar_t hud_dmoverlay_y ("hud_dmoverlay_y", -44, CVAR_HUD);

cvar_t hud_fps_x ("hud_fps_x", -76, CVAR_HUD);
cvar_t hud_fps_y ("hud_fps_y", -64, CVAR_HUD);

cvar_t hud_clock_x ("hud_clock_x", -52, CVAR_HUD);
cvar_t hud_clock_y ("hud_clock_y", -76, CVAR_HUD);

cvar_t hud_sbaralpha ("hud_sbaralpha", 1, CVAR_HUD | CVAR_ARCHIVE);

cvar_t hud_drawsbar ("hud_drawsbar", 1, CVAR_HUD | CVAR_ARCHIVE);
cvar_t hud_sbar_x ("hud_sbar_x", -160, CVAR_HUD);
cvar_t hud_sbar_y ("hud_sbar_y", -24, CVAR_HUD);
cvar_t hud_sbar_cx ("hud_sbar_cx", 1, CVAR_HUD);
cvar_t hud_sbar_cy ("hud_sbar_cy", 0.0f, CVAR_HUD);

cvar_t hud_drawibar ("hud_drawibar", 1, CVAR_HUD | CVAR_ARCHIVE);
cvar_t hud_ibar_x ("hud_ibar_x", -160, CVAR_HUD);
cvar_t hud_ibar_y ("hud_ibar_y", -48, CVAR_HUD);
cvar_t hud_ibar_cx ("hud_ibar_cx", 1, CVAR_HUD);
cvar_t hud_ibar_cy ("hud_ibar_cy", 0.0f, CVAR_HUD);

cvar_t hud_facepic_x ("hud_facepic_x", -48, CVAR_HUD);
cvar_t hud_facepic_y ("hud_facepic_y", -24, CVAR_HUD);
cvar_t hud_facepic_cx ("hud_facepic_cx", 1, CVAR_HUD);
cvar_t hud_facepic_cy ("hud_facepic_cy", 0.0f, CVAR_HUD);

cvar_t hud_faceval_x ("hud_faceval_x", -24, CVAR_HUD);
cvar_t hud_faceval_y ("hud_faceval_y", -24, CVAR_HUD);
cvar_t hud_faceval_cx ("hud_faceval_cx", 1, CVAR_HUD);
cvar_t hud_faceval_cy ("hud_faceval_cy", 0.0f, CVAR_HUD);

cvar_t hud_teamcolor_x ("hud_teamcolor_x", -48, CVAR_HUD);
cvar_t hud_teamcolor_y ("hud_teamcolor_y", -24, CVAR_HUD);
cvar_t hud_teamcolor_cx ("hud_teamcolor_cx", 1, CVAR_HUD);
cvar_t hud_teamcolor_cy ("hud_teamcolor_cy", 0.0f, CVAR_HUD);

cvar_t hud_armorpic_x ("hud_armorpic_x", -160, CVAR_HUD);
cvar_t hud_armorpic_y ("hud_armorpic_y", -24, CVAR_HUD);
cvar_t hud_armorpic_cx ("hud_armorpic_cx", 1, CVAR_HUD);
cvar_t hud_armorpic_cy ("hud_armorpic_cy", 0.0f, CVAR_HUD);

cvar_t hud_armorval_x ("hud_armorval_x", -136, CVAR_HUD);
cvar_t hud_armorval_y ("hud_armorval_y", -24, CVAR_HUD);
cvar_t hud_armorval_cx ("hud_armorval_cx", 1, CVAR_HUD);
cvar_t hud_armorval_cy ("hud_armorval_cy", 0.0f, CVAR_HUD);
cvar_t hud_armorval_no0 ("hud_armorval_no0", "0", CVAR_HUD);

cvar_t hud_ammopic_x ("hud_ammopic_x", 64, CVAR_HUD);
cvar_t hud_ammopic_y ("hud_ammopic_y", -24, CVAR_HUD);
cvar_t hud_ammopic_cx ("hud_ammopic_cx", 1, CVAR_HUD);
cvar_t hud_ammopic_cy ("hud_ammopic_cy", 0.0f, CVAR_HUD);

cvar_t hud_ammoval_x ("hud_ammoval_x", 88, CVAR_HUD);
cvar_t hud_ammoval_y ("hud_ammoval_y", -24, CVAR_HUD);
cvar_t hud_ammoval_cx ("hud_ammoval_cx", 1, CVAR_HUD);
cvar_t hud_ammoval_cy ("hud_ammoval_cy", 0.0f, CVAR_HUD);
cvar_t hud_ammoval_no0 ("hud_ammoval_no0", "0", CVAR_HUD);

cvar_t hud_sigils_x ("hud_sigils_x", 128, CVAR_HUD);
cvar_t hud_sigils_y ("hud_sigils_y", -40, CVAR_HUD);
cvar_t hud_sigils_cx ("hud_sigils_cx", 1, CVAR_HUD);
cvar_t hud_sigils_cy ("hud_sigils_cy", "0", CVAR_HUD);
cvar_t hud_sigils_h ("hud_sigils_h", 1, CVAR_HUD);
cvar_t hud_sigils_v ("hud_sigils_v", "0", CVAR_HUD);
cvar_t hud_sigils_hs ("hud_sigils_hs", "0", CVAR_HUD);
cvar_t hud_sigils_vs ("hud_sigils_vs", "0", CVAR_HUD);

cvar_t hud_keys_x ("hud_keys_x", 32, CVAR_HUD);
cvar_t hud_keys_y ("hud_keys_y", -40, CVAR_HUD);
cvar_t hud_keys_cx ("hud_keys_cx", 1, CVAR_HUD);
cvar_t hud_keys_cy ("hud_keys_cy", "0", CVAR_HUD);
cvar_t hud_keys_h ("hud_keys_h", 1, CVAR_HUD);
cvar_t hud_keys_v ("hud_keys_v", "0", CVAR_HUD);
cvar_t hud_keys_hs ("hud_keys_hs", "0", CVAR_HUD);
cvar_t hud_keys_vs ("hud_keys_vs", "0", CVAR_HUD);

cvar_t hud_items_x ("hud_items_x", 64, CVAR_HUD);
cvar_t hud_items_y ("hud_items_y", -40, CVAR_HUD);
cvar_t hud_items_cx ("hud_items_cx", 1, CVAR_HUD);
cvar_t hud_items_cy ("hud_items_cy", "0", CVAR_HUD);
cvar_t hud_items_h ("hud_items_h", 1, CVAR_HUD);
cvar_t hud_items_v ("hud_items_v", "0", CVAR_HUD);
cvar_t hud_items_hs ("hud_items_hs", "0", CVAR_HUD);
cvar_t hud_items_vs ("hud_items_vs", "0", CVAR_HUD);

cvar_t hud_ammocount_x ("hud_ammocount_x", -152, CVAR_HUD);
cvar_t hud_ammocount_y ("hud_ammocount_y", -48, CVAR_HUD);
cvar_t hud_ammocount_cx ("hud_ammocount_cx", 1, CVAR_HUD);
cvar_t hud_ammocount_cy ("hud_ammocount_cy", "0", CVAR_HUD);
cvar_t hud_ammocount_hs ("hud_ammocount_hs", 48, CVAR_HUD);
cvar_t hud_ammocount_vs ("hud_ammocount_vs", "0", CVAR_HUD);
cvar_t hud_ammobox_show ("hud_ammobox_show", "0", CVAR_HUD);
cvar_t hud_ammobox_x ("hud_ammobox_x", "0", CVAR_HUD);
cvar_t hud_ammobox_y ("hud_ammobox_y", "0", CVAR_HUD);

cvar_t hud_weapons_x ("hud_weapons_x", -160, CVAR_HUD);
cvar_t hud_weapons_y ("hud_weapons_y", -40, CVAR_HUD);
cvar_t hud_weapons_cx ("hud_weapons_cx", 1, CVAR_HUD);
cvar_t hud_weapons_cy ("hud_weapons_cy", "0", CVAR_HUD);
cvar_t hud_weapons_h ("hud_weapons_h", 1, CVAR_HUD);
cvar_t hud_weapons_v ("hud_weapons_v", "0", CVAR_HUD);
cvar_t hud_weapons_hs ("hud_weapons_hs", "0", CVAR_HUD);
cvar_t hud_weapons_vs ("hud_weapons_vs", "0", CVAR_HUD);

// hipnotic hackery - these are a bit off the real thing (one less and one more respectively)
// to make them multiples of 4 so that i don't have to get silly with the menu code.  it was
// always a bit of a dirty hack anyway...
cvar_t hud_hipnokeys_x ("hud_hipnokeys_x", 48, CVAR_HUD);
cvar_t hud_hipnokeys_y ("hud_hipnokeys_y", -20, CVAR_HUD);
cvar_t hud_hipnokeys_cx ("hud_hipnokeys_cx", 1, CVAR_HUD);
cvar_t hud_hipnokeys_cy ("hud_hipnokeys_cy", "0", CVAR_HUD);
cvar_t hud_hipnokeys_h ("hud_hipnokeys_h", "0", CVAR_HUD);
cvar_t hud_hipnokeys_v ("hud_hipnokeys_v", 1, CVAR_HUD);
cvar_t hud_hipnokeys_hs ("hud_hipnokeys_hs", "0", CVAR_HUD);
cvar_t hud_hipnokeys_vs ("hud_hipnokeys_vs", 1, CVAR_HUD);

// needed for crosshair drawing
extern vrect_t scr_vrect;


// HUD saving
void HUD_SaveHUD (void)
{
	if (Cmd_Argc () != 2)
	{
		Con_Printf ("savehud <filename> : save HUD layout to a script file\n");
		return;
	}

	char hudscript[128];

	Q_strncpy (hudscript, Cmd_Argv (1), 127);

	COM_DefaultExtension (hudscript, ".cfg");

	if (!stricmp (hudscript, "autoexec.cfg"))
	{
		Con_Printf ("You cannot save a HUD with that name!\n");
		return;
	}

	if (!stricmp (hudscript, "directq.cfg"))
	{
		Con_Printf ("You cannot save a HUD with that name!\n");
		return;
	}

	if (!stricmp (hudscript, "config.cfg"))
	{
		Con_Printf ("You cannot save a HUD with that name!\n");
		return;
	}

	if (!stricmp (hudscript, "default.cfg"))
	{
		Con_Printf ("You cannot save a HUD with that name!\n");
		return;
	}

	if (!stricmp (hudscript, "quake.rc"))
	{
		Con_Printf ("You cannot save a HUD with that name!\n");
		return;
	}

	FILE *f = fopen (va ("%s/%s", com_gamedir, hudscript), "w");

	if (!f)
	{
		Con_Printf ("Failed to create \"%s\"\n", hudscript);
		return;
	}

	// need to write these as floats because of alpha
	for (cvar_t *var = cvar_vars; var; var = var->next)
		if (var->usage & CVAR_HUD)
			fprintf (f, "%s \"%g\"\n", var->name, var->value);

	fclose (f);
	Con_Printf ("Wrote HUD layout to \"%s\"\n", hudscript);
}


void HUD_LoadHUD (void)
{
	if (Cmd_Argc () != 2)
	{
		Con_Printf ("loadhud <filename> : load HUD layout from a script file\n");
		return;
	}

	char hudscript[128];

	Q_strncpy (hudscript, Cmd_Argv (1), 127);

	COM_DefaultExtension (hudscript, ".cfg");

	if (!stricmp (hudscript, "autoexec.cfg"))
	{
		Con_Printf ("You cannot load a HUD with that name!\n");
		return;
	}

	if (!stricmp (hudscript, "directq.cfg"))
	{
		Con_Printf ("You cannot load a HUD with that name!\n");
		return;
	}

	if (!stricmp (hudscript, "config.cfg"))
	{
		Con_Printf ("You cannot load a HUD with that name!\n");
		return;
	}

	if (!stricmp (hudscript, "default.cfg"))
	{
		Con_Printf ("You cannot load a HUD with that name!\n");
		return;
	}

	if (!stricmp (hudscript, "quake.rc"))
	{
		Con_Printf ("You cannot load a HUD with that name!\n");
		return;
	}

	char *f = (char *) COM_LoadTempFile (hudscript);

	if (!f)
	{
		Con_Printf ("couldn't load \"%s\"\n", hudscript);
		return;
	}

	Con_Printf ("loading HUD from \"%s\"\n", hudscript);

	// fix if a config file isn't \n terminated
	Cbuf_InsertText (f);
	Cbuf_InsertText ("\n");
}


cmd_t HUD_SaveHUD_Cmd ("savehud", HUD_SaveHUD);
cmd_t HUD_LoadHUD_Cmd ("loadhud", HUD_LoadHUD);


// positioning
int HUD_GetX (cvar_t *xcvar, cvar_t *xccvar = NULL)
{
	float xpos = xcvar->value;

	if (xccvar)
	{
		if (!hud_centerhud.integer)
			xpos += 160;
		else if (xccvar->value) return (vid.width / 2) + xpos;
	}

	// hack to reposition the deathmatch overlay
	if (xcvar == &hud_dmoverlay_x && !hud_centerhud.integer) xpos -= 320;

	if (xpos < 0)
		return vid.width + xpos;
	else return xpos;
}


int HUD_GetY (cvar_t *ycvar, cvar_t *yccvar = NULL)
{
	if (yccvar)
		if (yccvar->value)
			return (vid.height / 2) + ycvar->value;

	if (ycvar->value < 0)
		return vid.height + ycvar->value;
	else return ycvar->value;
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


void HUD_DrawNum (int x, int y, int num, int digits, int color)
{
	char str[12];
	char *ptr;
	int l, frame;

	l = HUD_itoa (num, str);
	ptr = str;

	if (l > digits) ptr += (l - digits);
	if (l < digits) x += (digits - l) * 24;

	while (*ptr)
	{
		if (*ptr == '-')
			frame = STAT_MINUS;
		else frame = *ptr - '0';

		Draw_Pic (x, y, sb_nums[color][frame]);
		x += 24;
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
	Draw_String ((vid.width / 2) - (len * 4) - 4, y, str);
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
	xofs = HUD_GetX (&hud_ibar_x, &hud_ibar_cx);
	y = HUD_GetY (&hud_ibar_y, &hud_ibar_cy);

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
		_snprintf (num, 12, "%3i",f);

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


void D3D_ScissorRect (float l, float t, float r, float b);

void HUD_DrawLevelName (int x, int y)
{
	// matches max com_token
	char str[1024];

	// level name
	Q_strncpy (str, cl.levelname, 1023);

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
		// this never actually happens as we force-truncate cl.levelname to 40 chars when loading
		D3D_SetRenderState (D3DRS_SCISSORTESTENABLE, TRUE);
		D3D_ScissorRect (x - 20 * 8, y - 2, x + 20 * 8, y + 10);

		int ofs = ((int) (realtime * 30)) % (l * 8);

		Draw_String (x - ofs, y, str);

		D3D_SetRenderState (D3DRS_SCISSORTESTENABLE, FALSE);
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


void HUD_DeathmatchOverlay (void)
{
	extern cvar_t pq_scoreboard_pings;

	// update scoreboard pings every 5 seconds; force immediate update if we haven't had one at all yet
	if (((cl.last_ping_time < cl.time - 5) || (cl.last_ping_time < 0.1)) && pq_scoreboard_pings.value && cl.Protocol == PROTOCOL_VERSION)
	{
		// send a ping command to the server
		MSG_WriteByte (&cls.message, clc_stringcmd);
		SZ_Print (&cls.message, "ping\n");
		cl.last_ping_time = cl.time;
	}

	char str[128];
	int l, i, x, y, f, k;
	qpic_t *pic;
	scoreboard_t *s;
	int top, bottom;
	char num[12];

	pic = Draw_CachePic ("gfx/ranking.lmp");
	Draw_Pic ((vid.width - pic->width) / 2, 32, pic);

	// base X
	x = vid.width / 2;

	// starting Y
	y = 32 + pic->height + 8;

	HUD_DrawLevelName (x, y);

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
	if (pq_scoreboard_pings.value && cl.Protocol == PROTOCOL_VERSION)
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

	x = 80 + ((vid.width - 320) >> 1);
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


void HUD_SoloScoreboard (char *picname, float solotime)
{
	char str[128];
	int l;
	int i;
	int SBX;
	int SBY;
	qpic_t *pic;

	pic = Draw_CachePic (picname);
	Draw_Pic ((vid.width - pic->width) / 2, 48, pic);

	// base X
	SBX = vid.width / 2;

	// starting Y
	SBY = 48 + pic->height + 10;

	// level name
	HUD_DrawLevelName (SBX, SBY);

	SBY += 12;
	HUD_DividerLine (SBY, 16);

	if (cl.gametype != GAME_DEATHMATCH)
	{
		// kill count - remake quake compatibility
		if (cl.stats[STAT_TOTALMONSTERS] == 0)
			_snprintf (str, 80, "Monsters: %i", cl.stats[STAT_MONSTERS]);
		else _snprintf (str, 80, "Monsters: %i/%i", cl.stats[STAT_MONSTERS], cl.stats[STAT_TOTALMONSTERS]);

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
	int x = HUD_GetX (&hud_facepic_x, &hud_facepic_cx);
	int y = HUD_GetY (&hud_facepic_y, &hud_facepic_cy);

	if ((ActiveItems & (IT_INVISIBILITY | IT_INVULNERABILITY)) == (IT_INVISIBILITY | IT_INVULNERABILITY))
		Draw_Pic (x, y, sb_face_invis_invuln);
	else if (ActiveItems & IT_QUAD)
		Draw_Pic (x, y, sb_face_quad);
	else if (ActiveItems & IT_INVISIBILITY)
		Draw_Pic (x, y, sb_face_invis);
	else if (ActiveItems & IT_INVULNERABILITY)
		Draw_Pic (x, y, sb_face_invuln);
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

		Draw_Pic (x, y, sb_faces[f][anim]);
	}

	x = HUD_GetX (&hud_faceval_x, &hud_faceval_cx);
	y = HUD_GetY (&hud_faceval_y, &hud_faceval_cy);

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

		if (rogue)
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

	if (armorpic)
	{
		x = HUD_GetX (&hud_armorpic_x, &hud_armorpic_cx);
		y = HUD_GetY (&hud_armorpic_y, &hud_armorpic_cy);

		Draw_Pic (x, y, armorpic);
	}

	if (!armorval && hud_armorval_no0.value) return;

	x = HUD_GetX (&hud_armorval_x, &hud_armorval_cx);
	y = HUD_GetY (&hud_armorval_y, &hud_armorval_cy);

	HUD_DrawNum (x, y, armorval, 3, armorval <= 25 || armorval == 666);
}


void Draw_Crosshair (int x, int y, int size);

void HUD_DrawCrossHair (int basex, int basey)
{
	if (m_state != m_hudoptions)
	{
		// adjust for chosen positioning
		basex += cl_crossx.value;
		basey += cl_crossy.value;
	}

	if (crosshair.integer == 1)
		Draw_Character (basex - 4, basey - 4, '+');
	else if (crosshair.integer == 2)
		Draw_Character (basex - 4, basey - 4, '+' + 128);
	else if (crosshair.integer > 0)
	{
		// get scale
		int crossscale = (int) (32.0f * scr_crosshairscale.value);

		// don't draw if too small
		if (crossscale < 2) return;

		// draw it
		Draw_Crosshair (basex - (crossscale / 2), basey - (crossscale / 2), crossscale);
	}
}


void HUD_DrawAmmo (int ActiveItems, int AmmoStat)
{
	int x, y;
	qpic_t *ammopic = NULL;

	// ammo icon
	if (rogue)
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

	if (ammopic)
	{
		x = HUD_GetX (&hud_ammopic_x, &hud_ammopic_cx);
		y = HUD_GetY (&hud_ammopic_y, &hud_ammopic_cy);

		Draw_Pic (x, y, ammopic);
	}

	// add crosshair drawing here too... (centered better)
	if (m_state != m_hudoptions) HUD_DrawCrossHair (scr_vrect.x + scr_vrect.width / 2, scr_vrect.y + scr_vrect.height / 2);

	if (!AmmoStat && hud_ammoval_no0.value) return;

	x = HUD_GetX (&hud_ammoval_x, &hud_ammoval_cx);
	y = HUD_GetY (&hud_ammoval_y, &hud_ammoval_cy);

	HUD_DrawNum (x, y, AmmoStat, 3, AmmoStat <= 10);
}


void HUD_DrawSigils (int ActiveItems)
{
	// rogue and hipnotic hackery
	if (rogue || hipnotic) return;

	// normal sane way of doing things
	int x = HUD_GetX (&hud_sigils_x, &hud_sigils_cx);
	int y = HUD_GetY (&hud_sigils_y, &hud_sigils_cy);

	for (int i = 0; i < 4; i++)
	{
		if (ActiveItems & (1 << (28 + i)))
			Draw_Pic (x, y, sb_sigil[i]);

		x += sb_sigil[i]->width * hud_sigils_h.value + hud_sigils_hs.value;
		y += sb_sigil[i]->height * hud_sigils_v.value + hud_sigils_vs.value;
	}
}


void HUD_DrawKeys (int ActiveItems)
{
	if (hipnotic)
	{
		// hipnotic hackery
		int x = HUD_GetX (&hud_hipnokeys_x, &hud_hipnokeys_cx);
		int y = HUD_GetY (&hud_hipnokeys_y, &hud_hipnokeys_cy);

		for (int i = 0; i < 2; i++)
		{
			if (ActiveItems & (1 << (17 + i)))
				Draw_Pic (x, y, sb_items[i]);

			x += sb_items[i]->width * hud_hipnokeys_h.value + hud_hipnokeys_hs.value;
			y += sb_items[i]->height * hud_hipnokeys_v.value + hud_hipnokeys_vs.value;
		}
	}
	else
	{
		// normal sane way of doing things
		int x = HUD_GetX (&hud_keys_x, &hud_keys_cx);
		int y = HUD_GetY (&hud_keys_y, &hud_keys_cy);

		for (int i = 0; i < 2; i++)
		{
			if (ActiveItems & (1 << (17 + i)))
				Draw_Pic (x, y, sb_items[i]);

			x += sb_items[i]->width * hud_keys_h.value + hud_keys_hs.value;
			y += sb_items[i]->height * hud_keys_v.value + hud_keys_vs.value;
		}
	}
}


void HUD_DrawItems (int ActiveItems)
{
	// normal same way of doing things
	int x = HUD_GetX (&hud_items_x, &hud_items_cx);
	int y = HUD_GetY (&hud_items_y, &hud_items_cy);

	for (int i = 2; i < 6; i++)
	{
		if (ActiveItems & (1 << (17 + i)))
			Draw_Pic (x, y, sb_items[i]);

		x += sb_items[i]->width * hud_items_h.value + hud_items_hs.value;
		y += sb_items[i]->height * hud_items_v.value + hud_items_vs.value;
	}

	// note - these overwrite the sigils slots so we just continue where we left off

	if (hipnotic)
	{
		// hipnotic hackery
		for (int i = 0; i < 2; i++)
		{
			if (ActiveItems & (1 << (24 + i)))
				Draw_Pic (x, y, hsb_items[i]);

			x += hsb_items[i]->width * hud_items_h.value + hud_items_hs.value;
			y += hsb_items[i]->height * hud_items_v.value + hud_items_vs.value;
		}
	}

	if (rogue)
	{
		// rogue hackery
		for (int i = 0; i < 2; i++)
		{
			if (ActiveItems & (1 << (29 + i)))
				Draw_Pic (x, y, rsb_items[i]);

			x += rsb_items[i]->width * hud_items_h.value + hud_items_hs.value;
			y += rsb_items[i]->height * hud_items_v.value + hud_items_vs.value;
		}
	}
}


void HUD_DrawAmmoCounts (int ac1, int ac2, int ac3, int ac4, int ActiveWeapon)
{
	int x, y;
	char num[10];
	int ActiveAmmo[] = {ac1, ac2, ac3, ac4};

	x = HUD_GetX (&hud_ammocount_x, &hud_ammocount_cx);
	y = HUD_GetY (&hud_ammocount_y, &hud_ammocount_cy);

	for (int i = 0; i < 4; i++)
	{
		_snprintf (num, 10, "%3i", ActiveAmmo[i]);

		if (num[0] != ' ') Draw_Character (x, y, 18 + num[0] - '0');
		if (num[1] != ' ') Draw_Character (x + 8, y, 18 + num[1] - '0');
		if (num[2] != ' ') Draw_Character (x + 16, y, 18 + num[2] - '0');

		if (hud_ammobox_show.value)
		{
			qpic_t *boxpic;

			if (rogue && ActiveWeapon >= RIT_LAVA_NAILGUN)
			{
				// switch the icon to powered-up ammo
				if (i == 0)
					boxpic = sb_ammo[0];
				else if (i == 1)
					boxpic = rsb_ammo[0];
				else if (i == 2)
					boxpic = rsb_ammo[2];
				else boxpic = rsb_ammo[1];
			}
			else boxpic = sb_ammo[i];

			Draw_HalfPic (x + hud_ammobox_x.value, y + hud_ammobox_y.value, boxpic);
		}

		x += hud_ammocount_hs.value;
		y += hud_ammocount_vs.value;
	}
}


void HUD_DrawWeapons (int ActiveItems, int ActiveWeapon)
{
	// normal sane way of doing things
	int x = HUD_GetX (&hud_weapons_x, &hud_weapons_cx);
	int y = HUD_GetY (&hud_weapons_y, &hud_weapons_cy);

	// save baseline x and y for hipnotic and rogue hackery
	int savedx[8];
	int savedy[8];

	for (int i = 0; i < 7; i++)
	{
		// save baseline x and y for hipnotic and rogue hackery
		savedx[i] = x;
		savedy[i] = y;

		if (ActiveItems & (IT_SHOTGUN << i))
		{
			float time = cl.item_gettime[i];
			int flashon = (int) ((cl.time - time) * 10);

			if (flashon >= 10)
			{
				if (ActiveWeapon == (IT_SHOTGUN << i))
					flashon = 1;
				else flashon = 0;
			}
			else flashon = (flashon % 5) + 2;

			Draw_Pic (x, y, sb_weapons[flashon][i]);
		}

		x += sb_weapons[0][i]->width * hud_weapons_h.value + hud_weapons_hs.value;
		y += sb_weapons[0][i]->height * hud_weapons_v.value + hud_weapons_vs.value;
	}

	// now here's where it starts to get REAL ugly...
	if (hipnotic)
	{
		// hipnotic hackery
		// jesus this is REALLY ugly stuff...
		int grenadeflashing = 0;
		qpic_t *hipnopic = NULL;

		for (int i = 0; i < 4; i++)
		{
			if (ActiveItems & (1 << hipweapons[i]))
			{
				float time = cl.item_gettime[hipweapons[i]];
				int flashon = (int) ((cl.time - time) * 10);

				if (flashon >= 10)
				{
					if (ActiveWeapon == (1 << hipweapons[i]))
						flashon = 1;
					else flashon = 0;
				}
				else flashon = (flashon % 5) + 2;

				// baseline pic - can change later on
				hipnopic = hsb_weapons[flashon][i];

				// check grenade launcher
				if (i == 2)
				{
					if (ActiveItems & HIT_PROXIMITY_GUN)
					{
						if (flashon)
						{
							grenadeflashing = 1;
							Draw_Pic (savedx[4], savedy[4], hsb_weapons[flashon][2]);
						}
					}
				}
				else if (i == 3)
				{
					if (ActiveItems & (IT_SHOTGUN << 4))
					{
						if (flashon && !grenadeflashing)
						{
							Draw_Pic (savedx[4], savedy[4], hsb_weapons[flashon][3]);
						}
						else if (!grenadeflashing)
						{
							Draw_Pic (savedx[4], savedy[4], hsb_weapons[0][3]);
						}
					}
					else Draw_Pic (savedx[4], savedy[4], hsb_weapons[flashon][4]);
				}
				else
				{
					Draw_Pic (x, y, hsb_weapons[flashon][i]);
					x += hsb_weapons[0][i]->width * hud_weapons_h.value + hud_weapons_hs.value;
					y += hsb_weapons[0][i]->height * hud_weapons_v.value + hud_weapons_vs.value;
				}
			}
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
				if (ActiveWeapon == (RIT_LAVA_NAILGUN << i))
				{
					Draw_Pic (savedx[i + 2], savedy[i + 2], rsb_weapons[i]);
				}
			}
		}
	}
}


void HUD_DrawTeamBorder (void)
{
	// the menu needs this
	int x = HUD_GetX (&hud_teamcolor_x, &hud_teamcolor_cx);
	int y = HUD_GetY (&hud_teamcolor_y, &hud_teamcolor_cy);

	Draw_Pic (x, y, rsb_teambord);
}


void HUD_DrawTeamColors (void)
{
	// couldn't be arsed reversing the logic and changing && to || here...
	if (!(rogue && (cl.maxclients != 1) && (teamplay.value > 3) && (teamplay.value < 7))) return;

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

	int x = HUD_GetX (&hud_teamcolor_x, &hud_teamcolor_cx);
	int y = HUD_GetY (&hud_teamcolor_y, &hud_teamcolor_cy);

	Draw_Pic (x, y, rsb_teambord);

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
	if (hud_drawsbar.value)
	{
		// status bar background
		int x = HUD_GetX (&hud_sbar_x, &hud_sbar_cx);
		int y = HUD_GetY (&hud_sbar_y, &hud_sbar_cy);

		Draw_Pic (x, y, sb_sbar, hud_sbaralpha.value);
	}
}


void HUD_DrawIBar (int ActiveWeapon, bool DrawFrags)
{
	if (hud_drawibar.value)
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

		int x = HUD_GetX (&hud_ibar_x, &hud_ibar_cx);
		int y = HUD_GetY (&hud_ibar_y, &hud_ibar_cy);

		Draw_Pic (x, y, ibarpic, hud_sbaralpha.value);

		// the 4 mini frag-lists are only drawn on this one
		if (DrawFrags) HUD_DrawFrags ();
	}
}


void HUD_MiniDeathmatchOverlay (void)
{
	if (cl.gametype != GAME_DEATHMATCH) return;

	// don't need the mini overlay if the full is showing
	if ((hud_showscores && !cls.demoplayback) || (hud_showdemoscores && cls.demoplayback)) return;

	int				i, k, l;
	int				top, bottom;
	int				x, y, f;
	char			num[12];
	scoreboard_t	*s;

	// scores
	HUD_SortFrags ();

	x = HUD_GetX (&hud_dmoverlay_x, NULL);
	y = HUD_GetY (&hud_dmoverlay_y, NULL);

	// draw the text
	l = hud_scoreboardlines;

	//find us
	for (i = 0; i < hud_scoreboardlines; i++)
		if (hud_fragsort[i] == cl.viewentity - 1)
			break;

	if (i == hud_scoreboardlines) i = 0;
	if (i < 0) i = 0;

	// no more than 4 lines in the mini overlay
	for (int n = 0; i < hud_scoreboardlines && n < 4; i++, n++)
	{
		k = hud_fragsort[i];
		s = &cl.scores[k];

		if (!s->name[0]) continue;

		// draw background
		top = s->colors & 0xf0;
		bottom = (s->colors & 15) << 4;
		top = HUD_ColorForMap (top);
		bottom = HUD_ColorForMap (bottom);

		Draw_Fill (x, y + 1, 40, 3, top);
		Draw_Fill (x, y + 4, 40, 4, bottom);

		// draw number
		f = s->frags;
		_snprintf (num, 12, "%3i", f);

		Draw_Character (x + 8, y, num[0]);
		Draw_Character (x + 16, y, num[1]);
		Draw_Character (x + 24, y, num[2]);

		if (k == cl.viewentity - 1)
		{
			Draw_Character (x, y, 16);
			Draw_Character (x + 32, y, 17);
		}

		// draw name
		Draw_String (x + 48, y, s->name);

		y += 10;
	}
}


void HUD_DrawFPS (bool force)
{
	extern DWORD dwRealTime;
	static DWORD dwLastTime = dwRealTime;
	static int lastframe = host_framecount;

	DWORD fps_frametime = dwRealTime - dwLastTime;
	int fps_frames = host_framecount - lastframe;

	static float fps_fps = 0;

	if (fps_frametime > 200)
	{
		fps_fps = ((float) (fps_frames * 1000)) / ((float) fps_frametime);

		dwLastTime = dwRealTime;
		lastframe = host_framecount;
	}

	if (scr_showfps.value || force)
	{
		// positioning
		int x = HUD_GetX (&hud_fps_x, NULL);
		int y = HUD_GetY (&hud_fps_y, NULL);
		char str[17];

		_snprintf (str, 16, "%4.1f fps", fps_fps);
		Draw_String (x, y, str);
	}
}


void HUD_DrawClock (bool force)
{
	char	str[9];

	if (scr_clock.value || force)
	{
		int minutes = cl.time / 60;
		int seconds = ((int) cl.time) % 60;

		_snprintf (str, 9, "%02i:%i%i", minutes, seconds / 10, seconds % 10);
	}
	else return;

	// positioning
	int x = HUD_GetX (&hud_clock_x, NULL);
	int y = HUD_GetY (&hud_clock_y, NULL);

	Draw_String (x, y, str);
}


void HUD_DrawOSDItems (bool force = false)
{
	HUD_DrawFPS (force);
	HUD_DrawClock (force);
}


void HUD_DrawHUD (void)
{
	// console is fullscreen
	if (scr_con_current == vid.height) return;

	static char oldhud[128] = {0};

	// i suppose one strcmp per frame is ok...
	if (strcmp (oldhud, hud_defaulthud.string) && hud_autoload.value)
	{
		// this is a hack to suppress "Execing..." when the HUD loads
		con_initialized = false;

		// run anything that's left in the command buffer, load our HUD, then run it again
		Cbuf_Execute ();
		Cbuf_InsertText (va ("exec %s\n", hud_defaulthud.string));
		Cbuf_Execute ();

		// store back
		Q_strncpy (oldhud, hud_defaulthud.string, 127);

		// go back to normal console mode
		con_initialized = true;
	}

	// don't confuse things if we're customizing the HUD
	// (deferred to here as gl_clear 1 will mean that the bottom portion of the screen won't get drawn this frame)
	if (m_state == m_hudoptions) return;

	// no HUD
	if (scr_viewsize.value > 111) return;

	if ((hud_showscores && !cls.demoplayback) || (hud_showdemoscores && cls.demoplayback) || cl.stats[STAT_HEALTH] <= 0)
	{
		// scoreboard
		if (cl.gametype == GAME_DEATHMATCH)
			HUD_DeathmatchOverlay ();
		else HUD_SoloScoreboard ("gfx/ranking.lmp", cl.time);
	}

	// OSD Items
	HUD_DrawOSDItems ();

	if (cl.stats[STAT_HEALTH] > 0)
	{
		// draw elements
		// note - team colors must be drawn after the face as they overlap in the default layout
		HUD_DrawSBar ();
		HUD_DrawFace (cl.items, cl.stats[STAT_HEALTH]);
		HUD_DrawTeamColors ();
		HUD_DrawArmor (cl.items, cl.stats[STAT_ARMOR]);
		HUD_DrawAmmo (cl.items, cl.stats[STAT_AMMO]);

		// no inventory
		if (scr_viewsize.value > 101) return;

		// inventory
		HUD_DrawIBar (cl.stats[STAT_ACTIVEWEAPON], true);
		HUD_DrawSigils (cl.items);
		HUD_DrawKeys (cl.items);
		HUD_DrawItems (cl.items);
		HUD_DrawWeapons (cl.items, cl.stats[STAT_ACTIVEWEAPON]);
		HUD_DrawAmmoCounts (cl.stats[STAT_SHELLS], cl.stats[STAT_NAILS], cl.stats[STAT_ROCKETS], cl.stats[STAT_CELLS], cl.stats[STAT_ACTIVEWEAPON]);

		// deathmatch overlay
		HUD_MiniDeathmatchOverlay ();
	}
}


void HUD_IntermissionOverlay (void)
{
	if (cl.gametype == GAME_DEATHMATCH)
		HUD_DeathmatchOverlay ();
	else HUD_SoloScoreboard ("gfx/complete.lmp", cl.completed_time);
}


void HUD_FinaleOverlay (void)
{
	if (cl.gametype == GAME_DEATHMATCH)
		HUD_DeathmatchOverlay ();
	else
	{
		qpic_t	*pic;

		pic = Draw_CachePic ("gfx/finale.lmp");
		Draw_Pic ((vid.width - pic->width) / 2, 16, pic);
	}
}


/*
==================================================================================================================================

		HUD MENU

	Placed here instead of in the menus code so that I don't have to extern all of those bloody cvars!

==================================================================================================================================
*/

// hack to create a functioning spin control for the crosshair image
char *crosshairnames[] =
{
	" Off ",
	"     ",	// +
	"     ",	// +
	"     ",	// custom 0
	"     ",	// custom 1
	"     ",	// custom 2
	"     ",	// custom 3
	"     ",	// custom 4
	"     ",	// custom 5
	"     ",	// custom 6
	"     ",	// custom 7
	"     ",	// custom 8
	"     ",	// custom 9
	"     ",	// custom 10
	"     ",	// custom 11
	"     ",	// custom 12
	"     ",	// custom 13
	"     ",	// custom 14
	"     ",	// custom 15

	// list terminator
	NULL
};


// menu tags for hiding/showing items
#define TAG_SBAR		(1 << 0)
#define TAG_IBAR		(1 << 1)
#define TAG_FACE		(1 << 2)
#define TAG_HEALTH		(1 << 3)
#define TAG_ARMORPIC	(1 << 4)
#define TAG_ARMORVAL	(1 << 5)
#define TAG_AMMOPIC		(1 << 6)
#define TAG_AMMOVAL		(1 << 7)
#define TAG_SIGILS		(1 << 8)
#define TAG_KEYS		(1 << 9)
#define TAG_ITEMS		(1 << 10)
#define TAG_WEAPONS		(1 << 11)
#define TAG_AMMOCNT		(1 << 12)
#define TAG_TEAM		(1 << 13)
#define TAG_OSD			(1 << 14)
#define TAG_CROSSHAIR	(1 << 15)
#define TAG_DMOVERLAY	(1 << 16)
#define TAG_HIPNOKEYS	(1 << 17)


char *hud_items[] =
{
	"Status Bar",
	"Inventory Bar",
	"Face Picture",
	"Health Value",
	"Armor Picture",
	"Armor Value",
	"Ammo Picture",
	"Ammo Value",
	"Sigils",
	"Keys",
	"Pickup Items",
	"Weapons",
	"Ammo Counts",
	"Team Colors",
	"OSD Items",
	"Crosshair",
	"Deathmatch Overlay",

	// terminates list
	NULL,
};

int hud_itemnum = 0;

int Menu_HUDCustomDraw (int y)
{
	// fixme - would this be easier with enable/disable options???
	// done.  show/hide i meant, obviously
	static int last_hud_itemnum = -1;

	// fix up crosshair stuff
	if (hud_itemnum == 15)
	{
		// bound cvars
		if (crosshair.integer < 0) crosshair.integer = 0;
		if (crosshair.integer > 18) crosshair.integer = 18;
		if (scr_crosshaircolor.integer < 0) scr_crosshaircolor.integer = 0;
		if (scr_crosshaircolor.integer > 13) scr_crosshaircolor.integer = 13;

		// these are hacks as the controls reference the integer directly
		Cvar_Set (&crosshair, crosshair.integer);
		Cvar_Set (&scr_crosshaircolor, scr_crosshaircolor.integer);
	}

	// nothing changed here
	if (last_hud_itemnum == hud_itemnum) return y;

	// check for sigils
	if ((rogue || hipnotic) && hud_itemnum == 8)
	{
		// skip over sigils
		if (last_hud_itemnum == 7) hud_itemnum = 9;
		if (last_hud_itemnum == 9) hud_itemnum = 7;
	}

	// check for team colors
	if (!rogue && hud_itemnum == 13)
	{
		// skip over team colors
		if (last_hud_itemnum == 12) hud_itemnum = 14;
		if (last_hud_itemnum == 14) hud_itemnum = 12;
	}

	// store back
	last_hud_itemnum = hud_itemnum;

	// so hipnotic doesn't trash the menu item number
	int real_huditemnum = hud_itemnum;

	// switch keys for hipnotic
	if (hipnotic && hud_itemnum == 9) real_huditemnum = 19;

	// hide all tags
	for (int i = 1; i < 21; i++)
		menu_HUD.HideMenuOptions (1 << i);

	// show selected tag (the defines are the num + 1 because 0 is the default tag)
	menu_HUD.ShowMenuOptions (1 << (real_huditemnum + 1));

	return y;
}


void HUD_DummyMiniDMOverlay (void)
{
	char num[10];

	int x = HUD_GetX (&hud_dmoverlay_x, NULL);
	int y = HUD_GetY (&hud_dmoverlay_y, NULL);

	for (int i = 0; i < 4; i++)
	{
		Draw_Fill (x, y + 1, 40, 3, HUD_ColorForMap (i * 3));
		Draw_Fill (x, y + 4, 40, 4, HUD_ColorForMap (i + 6));

		_snprintf (num, 10, "%3i", 10);

		Draw_Character (x + 8, y, num[0]);
		Draw_Character (x + 16, y, num[1]);
		Draw_Character (x + 24, y, num[2]);

		if (!i)
		{
			Draw_Character (x, y, 16);
			Draw_Character (x + 32, y, 17);
		}

		// draw name
		Draw_String (x + 48, y, va ("Player %i", i));

		y += 10;
	}
}


int Menu_HUDDrawHUD (int y)
{
	// draw all of the hud elements
	if (!hud_overlay.value) Draw_TileClear (0, vid.height - 48, vid.width, 48);
	HUD_DrawSBar ();
	HUD_DrawFace (0, 200);
	if (rogue) HUD_DrawTeamBorder ();
	HUD_DrawArmor (IT_ARMOR1 | RIT_ARMOR1, 200);
	HUD_DrawAmmo (IT_NAILS | RIT_NAILS, 200);
	HUD_DrawIBar (IT_SHOTGUN, false);
	HUD_DrawSigils (0xffffffff);
	HUD_DrawKeys (0xffffffff);
	HUD_DrawItems (0xffffffff);
	HUD_DrawWeapons (0xffffffff, IT_SHOTGUN);
	HUD_DrawAmmoCounts (100, 200, 100, 100, IT_SHOTGUN);
	HUD_DummyMiniDMOverlay ();
	HUD_DrawOSDItems (true);

	// only draw the crosshair if we're adjusting it - hacky positioning, will break if layout changes
	if (hud_itemnum == 15) HUD_DrawCrossHair ((vid.width / 2) + 32, 247);

	return y;
}


void Menu_HUDSaveLayout (void)
{
	Cbuf_AddText (va ("savehud %s\n", hud_defaulthud.string));
	Cbuf_Execute ();
}


void Menu_InitHUDMenu (void)
{
	// this needs to come first so that it can link up the inserted items
	menu_HUD.AddOption (new CQMenuCustomDraw (Menu_HUDCustomDraw));

	// now add the rest of the options
	menu_HUD.AddOption (new CQMenuBanner ("gfx/p_option.lmp"));
	menu_HUD.AddOption (new CQMenuTitle ("HUD Layout and Configuration"));
	menu_HUD.AddOption (new CQMenuCvarTextbox ("Default HUD", &hud_defaulthud, TBFLAGS_FILENAMEFLAGS));
	menu_HUD.AddOption (new CQMenuCvarToggle ("Auto-load HUD", &hud_autoload));
	menu_HUD.AddOption (new CQMenuCvarToggle ("Auto-save HUD", &hud_autosave));
	menu_HUD.AddOption (new CQMenuCvarToggle ("Draw as Overlay", &hud_overlay));
	menu_HUD.AddOption (new CQMenuCvarToggle ("Center-align HUD", &hud_centerhud));
	menu_HUD.AddOption (new CQMenuTitle ("Customize HUD Layout"));
	menu_HUD.AddOption (new CQMenuSpinControl ("Select HUD Item", &hud_itemnum, hud_items));
	menu_HUD.AddOption (new CQMenuSpacer (DIVIDER_LINE));

	// status bar
	menu_HUD.AddOption (TAG_SBAR, new CQMenuCvarToggle ("Show this Item", &hud_drawsbar));
	menu_HUD.AddOption (TAG_SBAR, new CQMenuCvarSlider ("Alpha", &hud_sbaralpha, 0, 1, 0.1));
	menu_HUD.AddOption (TAG_SBAR, new CQMenuSpinControl ("X Position", &hud_sbar_x, -1280, 1280, 4));
	menu_HUD.AddOption (TAG_SBAR, new CQMenuSpinControl ("Y Position", &hud_sbar_y, -1280, 1280, 4));
	menu_HUD.AddOption (TAG_SBAR, new CQMenuCvarToggle ("Center Align X", &hud_sbar_cx));
	menu_HUD.AddOption (TAG_SBAR, new CQMenuCvarToggle ("Center Align Y", &hud_sbar_cy));

	// inventory bar
	menu_HUD.AddOption (TAG_IBAR, new CQMenuCvarToggle ("Show this Item", &hud_drawibar));
	menu_HUD.AddOption (TAG_IBAR, new CQMenuCvarSlider ("Alpha", &hud_sbaralpha, 0, 1, 0.1));
	menu_HUD.AddOption (TAG_IBAR, new CQMenuSpinControl ("X Position", &hud_ibar_x, -1280, 1280, 4));
	menu_HUD.AddOption (TAG_IBAR, new CQMenuSpinControl ("Y Position", &hud_ibar_y, -1280, 1280, 4));
	menu_HUD.AddOption (TAG_IBAR, new CQMenuCvarToggle ("Center Align X", &hud_ibar_cx));
	menu_HUD.AddOption (TAG_IBAR, new CQMenuCvarToggle ("Center Align Y", &hud_ibar_cy));

	// face picture
	menu_HUD.AddOption (TAG_FACE, new CQMenuSpinControl ("X Position", &hud_facepic_x, -1280, 1280, 4));
	menu_HUD.AddOption (TAG_FACE, new CQMenuSpinControl ("Y Position", &hud_facepic_y, -1280, 1280, 4));
	menu_HUD.AddOption (TAG_FACE, new CQMenuCvarToggle ("Center Align X", &hud_facepic_cx));
	menu_HUD.AddOption (TAG_FACE, new CQMenuCvarToggle ("Center Align Y", &hud_facepic_cy));

	// health
	menu_HUD.AddOption (TAG_HEALTH, new CQMenuSpinControl ("X Position", &hud_faceval_x, -1280, 1280, 4));
	menu_HUD.AddOption (TAG_HEALTH, new CQMenuSpinControl ("Y Position", &hud_faceval_y, -1280, 1280, 4));
	menu_HUD.AddOption (TAG_HEALTH, new CQMenuCvarToggle ("Center Align X", &hud_faceval_cx));
	menu_HUD.AddOption (TAG_HEALTH, new CQMenuCvarToggle ("Center Align Y", &hud_faceval_cy));

	// armor picture
	menu_HUD.AddOption (TAG_ARMORPIC, new CQMenuSpinControl ("X Position", &hud_armorpic_x, -1280, 1280, 4));
	menu_HUD.AddOption (TAG_ARMORPIC, new CQMenuSpinControl ("Y Position", &hud_armorpic_y, -1280, 1280, 4));
	menu_HUD.AddOption (TAG_ARMORPIC, new CQMenuCvarToggle ("Center Align X", &hud_armorpic_cx));
	menu_HUD.AddOption (TAG_ARMORPIC, new CQMenuCvarToggle ("Center Align Y", &hud_armorpic_cy));

	// armor value
	menu_HUD.AddOption (TAG_ARMORVAL, new CQMenuSpinControl ("X Position", &hud_armorval_x, -1280, 1280, 4));
	menu_HUD.AddOption (TAG_ARMORVAL, new CQMenuSpinControl ("Y Position", &hud_armorval_y, -1280, 1280, 4));
	menu_HUD.AddOption (TAG_ARMORVAL, new CQMenuCvarToggle ("Center Align X", &hud_armorval_cx));
	menu_HUD.AddOption (TAG_ARMORVAL, new CQMenuCvarToggle ("Center Align Y", &hud_armorval_cy));
	menu_HUD.AddOption (TAG_ARMORVAL, new CQMenuCvarToggle ("Hide if 0", &hud_armorval_no0));

	// ammo picture
	menu_HUD.AddOption (TAG_AMMOPIC, new CQMenuSpinControl ("X Position", &hud_ammopic_x, -1280, 1280, 4));
	menu_HUD.AddOption (TAG_AMMOPIC, new CQMenuSpinControl ("Y Position", &hud_ammopic_y, -1280, 1280, 4));
	menu_HUD.AddOption (TAG_AMMOPIC, new CQMenuCvarToggle ("Center Align X", &hud_ammopic_cx));
	menu_HUD.AddOption (TAG_AMMOPIC, new CQMenuCvarToggle ("Center Align Y", &hud_ammopic_cy));

	// ammo value
	menu_HUD.AddOption (TAG_AMMOVAL, new CQMenuSpinControl ("X Position", &hud_ammoval_x, -1280, 1280, 4));
	menu_HUD.AddOption (TAG_AMMOVAL, new CQMenuSpinControl ("Y Position", &hud_ammoval_y, -1280, 1280, 4));
	menu_HUD.AddOption (TAG_AMMOVAL, new CQMenuCvarToggle ("Center Align X", &hud_ammoval_cx));
	menu_HUD.AddOption (TAG_AMMOVAL, new CQMenuCvarToggle ("Center Align Y", &hud_ammoval_cy));
	menu_HUD.AddOption (TAG_AMMOVAL, new CQMenuCvarToggle ("Hide if 0", &hud_ammoval_no0));

	// sigils
	menu_HUD.AddOption (TAG_SIGILS, new CQMenuSpinControl ("X Position", &hud_sigils_x, -1280, 1280, 4));
	menu_HUD.AddOption (TAG_SIGILS, new CQMenuSpinControl ("Y Position", &hud_sigils_y, -1280, 1280, 4));
	menu_HUD.AddOption (TAG_SIGILS, new CQMenuCvarToggle ("Center Align X", &hud_sigils_cx));
	menu_HUD.AddOption (TAG_SIGILS, new CQMenuCvarToggle ("Center Align Y", &hud_sigils_cy));
	menu_HUD.AddOption (TAG_SIGILS, new CQMenuCvarToggle ("Horizontal Align", &hud_sigils_h));
	menu_HUD.AddOption (TAG_SIGILS, new CQMenuCvarToggle ("Vertical Align", &hud_sigils_v));
	menu_HUD.AddOption (TAG_SIGILS, new CQMenuSpinControl ("Horizontal Spacing", &hud_sigils_hs, -32, 32, 1));
	menu_HUD.AddOption (TAG_SIGILS, new CQMenuSpinControl ("Vertical Spacing", &hud_sigils_vs, -32, 32, 1));

	// keys
	menu_HUD.AddOption (TAG_KEYS, new CQMenuSpinControl ("X Position", &hud_keys_x, -1280, 1280, 4));
	menu_HUD.AddOption (TAG_KEYS, new CQMenuSpinControl ("Y Position", &hud_keys_y, -1280, 1280, 4));
	menu_HUD.AddOption (TAG_KEYS, new CQMenuCvarToggle ("Center Align X", &hud_keys_cx));
	menu_HUD.AddOption (TAG_KEYS, new CQMenuCvarToggle ("Center Align Y", &hud_keys_cy));
	menu_HUD.AddOption (TAG_KEYS, new CQMenuCvarToggle ("Horizontal Align", &hud_keys_h));
	menu_HUD.AddOption (TAG_KEYS, new CQMenuCvarToggle ("Vertical Align", &hud_keys_v));
	menu_HUD.AddOption (TAG_KEYS, new CQMenuSpinControl ("Horizontal Spacing", &hud_keys_hs, -32, 32, 1));
	menu_HUD.AddOption (TAG_KEYS, new CQMenuSpinControl ("Vertical Spacing", &hud_keys_vs, -32, 32, 1));

	// items
	menu_HUD.AddOption (TAG_ITEMS, new CQMenuSpinControl ("X Position", &hud_items_x, -1280, 1280, 4));
	menu_HUD.AddOption (TAG_ITEMS, new CQMenuSpinControl ("Y Position", &hud_items_y, -1280, 1280, 4));
	menu_HUD.AddOption (TAG_ITEMS, new CQMenuCvarToggle ("Center Align X", &hud_items_cx));
	menu_HUD.AddOption (TAG_ITEMS, new CQMenuCvarToggle ("Center Align Y", &hud_items_cy));
	menu_HUD.AddOption (TAG_ITEMS, new CQMenuCvarToggle ("Horizontal Align", &hud_items_h));
	menu_HUD.AddOption (TAG_ITEMS, new CQMenuCvarToggle ("Vertical Align", &hud_items_v));
	menu_HUD.AddOption (TAG_ITEMS, new CQMenuSpinControl ("Horizontal Spacing", &hud_items_hs, -32, 32, 1));
	menu_HUD.AddOption (TAG_ITEMS, new CQMenuSpinControl ("Vertical Spacing", &hud_items_vs, -32, 32, 1));

	// weapons
	menu_HUD.AddOption (TAG_WEAPONS, new CQMenuSpinControl ("X Position", &hud_weapons_x, -1280, 1280, 4));
	menu_HUD.AddOption (TAG_WEAPONS, new CQMenuSpinControl ("Y Position", &hud_weapons_y, -1280, 1280, 4));
	menu_HUD.AddOption (TAG_WEAPONS, new CQMenuCvarToggle ("Center Align X", &hud_weapons_cx));
	menu_HUD.AddOption (TAG_WEAPONS, new CQMenuCvarToggle ("Center Align Y", &hud_weapons_cy));
	menu_HUD.AddOption (TAG_WEAPONS, new CQMenuCvarToggle ("Horizontal Align", &hud_weapons_h));
	menu_HUD.AddOption (TAG_WEAPONS, new CQMenuCvarToggle ("Vertical Align", &hud_weapons_v));
	menu_HUD.AddOption (TAG_WEAPONS, new CQMenuSpinControl ("Horizontal Spacing", &hud_weapons_hs, -32, 32, 1));
	menu_HUD.AddOption (TAG_WEAPONS, new CQMenuSpinControl ("Vertical Spacing", &hud_weapons_vs, -32, 32, 1));

	// ammo counts
	menu_HUD.AddOption (TAG_AMMOCNT, new CQMenuSpinControl ("X Position", &hud_ammocount_x, -1280, 1280, 4));
	menu_HUD.AddOption (TAG_AMMOCNT, new CQMenuSpinControl ("Y Position", &hud_ammocount_y, -1280, 1280, 4));
	menu_HUD.AddOption (TAG_AMMOCNT, new CQMenuCvarToggle ("Center Align X", &hud_ammocount_cx));
	menu_HUD.AddOption (TAG_AMMOCNT, new CQMenuCvarToggle ("Center Align Y", &hud_ammocount_cy));
	menu_HUD.AddOption (TAG_AMMOCNT, new CQMenuSpinControl ("Horizontal Spacing", &hud_ammocount_hs, -128, 128, 1));
	menu_HUD.AddOption (TAG_AMMOCNT, new CQMenuSpinControl ("Vertical Spacing", &hud_ammocount_vs, -128, 128, 1));
	menu_HUD.AddOption (TAG_AMMOCNT, new CQMenuCvarToggle ("Show Ammo Boxes", &hud_ammobox_show));
	menu_HUD.AddOption (TAG_AMMOCNT, new CQMenuSpinControl ("Ammo Box X Offset", &hud_ammobox_x, -128, 128, 1));
	menu_HUD.AddOption (TAG_AMMOCNT, new CQMenuSpinControl ("Ammo Box Y Offset", &hud_ammobox_y, -128, 128, 1));

	// team colors (rogue only)
	menu_HUD.AddOption (TAG_TEAM, new CQMenuSpinControl ("X Position", &hud_teamcolor_x, -1280, 1280, 4));
	menu_HUD.AddOption (TAG_TEAM, new CQMenuSpinControl ("Y Position", &hud_teamcolor_y, -1280, 1280, 4));
	menu_HUD.AddOption (TAG_TEAM, new CQMenuCvarToggle ("Center Align X", &hud_teamcolor_cx));
	menu_HUD.AddOption (TAG_TEAM, new CQMenuCvarToggle ("Center Align Y", &hud_teamcolor_cy));

	// OSD Items
	menu_HUD.AddOption (TAG_OSD, new CQMenuCvarToggle ("Show FPS", &scr_showfps));
	menu_HUD.AddOption (TAG_OSD, new CQMenuSpinControl ("X Position", &hud_fps_x, -1280, 1280, 4));
	menu_HUD.AddOption (TAG_OSD, new CQMenuSpinControl ("Y Position", &hud_fps_y, -1280, 1280, 4));
	menu_HUD.AddOption (TAG_OSD, new CQMenuSpacer (DIVIDER_LINE));
	menu_HUD.AddOption (TAG_OSD, new CQMenuCvarToggle ("Show Clock", &scr_clock));
	menu_HUD.AddOption (TAG_OSD, new CQMenuSpinControl ("X Position", &hud_clock_x, -1280, 1280, 4));
	menu_HUD.AddOption (TAG_OSD, new CQMenuSpinControl ("Y Position", &hud_clock_y, -1280, 1280, 4));

	// crosshair
	menu_HUD.AddOption (TAG_CROSSHAIR, new CQMenuSpacer ());
	menu_HUD.AddOption (TAG_CROSSHAIR, new CQMenuSpinControl ("Crosshair Image", &crosshair.integer, crosshairnames));
	menu_HUD.AddOption (TAG_CROSSHAIR, new CQMenuSpacer ());
	menu_HUD.AddOption (TAG_CROSSHAIR, new CQMenuColourBar ("Colour", &scr_crosshaircolor.integer));
	menu_HUD.AddOption (TAG_CROSSHAIR, new CQMenuSpinControl ("X Offset", &cl_crossx, -30, 30, 1));
	menu_HUD.AddOption (TAG_CROSSHAIR, new CQMenuSpinControl ("Y Offset", &cl_crossy, -30, 30, 1));
	menu_HUD.AddOption (TAG_CROSSHAIR, new CQMenuCvarSlider ("Scale", &scr_crosshairscale, 0, 2, 0.1));

	// deathmatch overlay
	menu_HUD.AddOption (TAG_DMOVERLAY, new CQMenuSpinControl ("X Position", &hud_dmoverlay_x, -1280, 1280, 4));
	menu_HUD.AddOption (TAG_DMOVERLAY, new CQMenuSpinControl ("Y Position", &hud_dmoverlay_y, -1280, 1280, 4));

	// hipnotic keys
	menu_HUD.AddOption (TAG_HIPNOKEYS, new CQMenuSpinControl ("X Position", &hud_hipnokeys_x, -1280, 1280, 4));
	menu_HUD.AddOption (TAG_HIPNOKEYS, new CQMenuSpinControl ("Y Position", &hud_hipnokeys_y, -1280, 1280, 4));
	menu_HUD.AddOption (TAG_HIPNOKEYS, new CQMenuCvarToggle ("Center Align X", &hud_hipnokeys_cx));
	menu_HUD.AddOption (TAG_HIPNOKEYS, new CQMenuCvarToggle ("Center Align Y", &hud_hipnokeys_cy));
	menu_HUD.AddOption (TAG_HIPNOKEYS, new CQMenuCvarToggle ("Horizontal Align", &hud_hipnokeys_h));
	menu_HUD.AddOption (TAG_HIPNOKEYS, new CQMenuCvarToggle ("Vertical Align", &hud_hipnokeys_v));
	menu_HUD.AddOption (TAG_HIPNOKEYS, new CQMenuSpinControl ("Horizontal Spacing", &hud_hipnokeys_hs, -32, 32, 1));
	menu_HUD.AddOption (TAG_HIPNOKEYS, new CQMenuSpinControl ("Vertical Spacing", &hud_hipnokeys_vs, -32, 32, 1));

	// last stuff
	menu_HUD.AddOption (new CQMenuSpacer (DIVIDER_LINE));
	menu_HUD.AddOption (new CQMenuCommand ("Save Layout", Menu_HUDSaveLayout));

	// this last one draws the hud layout based on the current selections
	menu_HUD.AddOption (new CQMenuCustomDraw (Menu_HUDDrawHUD));
}


