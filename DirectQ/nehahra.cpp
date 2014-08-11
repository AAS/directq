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

// this module collects as many as possible of the disgusting nehahra hacks in one place

#include "versions.h"

#include <math.h>
#include "quakedef.h"
#include "d3d_model.h"
#include "pr_class.h"


// nehahra cutscene - fixme - need to find a way to skip a cutscene while playing
cvar_t cutscene ("cutscene", "1", CVAR_ARCHIVE);


// poxy fucking nehahra cvars
// and there I was thinking that HIPNOTIC was a collection of vile hacks...
cvar_t nehx00 ("nehx00", "0", CVAR_NEHAHRA); cvar_t nehx01 ("nehx01", "0", CVAR_NEHAHRA);
cvar_t nehx02 ("nehx02", "0", CVAR_NEHAHRA); cvar_t nehx03 ("nehx03", "0", CVAR_NEHAHRA);
cvar_t nehx04 ("nehx04", "0", CVAR_NEHAHRA); cvar_t nehx05 ("nehx05", "0", CVAR_NEHAHRA);
cvar_t nehx06 ("nehx06", "0", CVAR_NEHAHRA); cvar_t nehx07 ("nehx07", "0", CVAR_NEHAHRA);
cvar_t nehx08 ("nehx08", "0", CVAR_NEHAHRA); cvar_t nehx09 ("nehx09", "0", CVAR_NEHAHRA);
cvar_t nehx10 ("nehx10", "0", CVAR_NEHAHRA); cvar_t nehx11 ("nehx11", "0", CVAR_NEHAHRA);
cvar_t nehx12 ("nehx12", "0", CVAR_NEHAHRA); cvar_t nehx13 ("nehx13", "0", CVAR_NEHAHRA);
cvar_t nehx14 ("nehx14", "0", CVAR_NEHAHRA); cvar_t nehx15 ("nehx15", "0", CVAR_NEHAHRA);
cvar_t nehx16 ("nehx16", "0", CVAR_NEHAHRA); cvar_t nehx17 ("nehx17", "0", CVAR_NEHAHRA);
cvar_t nehx18 ("nehx18", "0", CVAR_NEHAHRA); cvar_t nehx19 ("nehx19", "0", CVAR_NEHAHRA);

// these cvars do nothing for now; they only exist to soak up abuse from nehahra maps which expect them to be there
cvar_t r_oldsky ("r_oldsky", "1", CVAR_NEHAHRA);



// these are just to soak up nehahra abuse
void FuckOffAndDieYouCollectionOfPoxyHacks (void) {}
cmd_t Cmd_PlayMod ("playmod", FuckOffAndDieYouCollectionOfPoxyHacks);
cmd_t Cmd_StopMod ("stopmod", FuckOffAndDieYouCollectionOfPoxyHacks);


/*
============================================================================================================

		NEHAHRA SHOWLMP STUFF

============================================================================================================
*/

// nehahra showlmp stuff; this was UGLY, EVIL and DISGUSTING code.
#define SHOWLMP_MAXLABELS 256

typedef struct showlmp_s
{
	bool		isactive;
	float		x;
	float		y;
	char		label[32];
	char		pic[128];
	qpic_t		*qpic;
} showlmp_t;

showlmp_t *showlmp = NULL;
extern bool nehahra;

void SHOWLMP_decodehide (void)
{
	if (!showlmp) showlmp = (showlmp_t *) GameZone->Alloc (sizeof (showlmp_t) * SHOWLMP_MAXLABELS);

	char *lmplabel = MSG_ReadString ();

	for (int i = 0; i < SHOWLMP_MAXLABELS; i++)
	{
		if (showlmp[i].isactive && strcmp (showlmp[i].label, lmplabel) == 0)
		{
			showlmp[i].isactive = false;
			return;
		}
	}
}


void SHOWLMP_decodeshow (void)
{
	if (!showlmp) showlmp = (showlmp_t *) GameZone->Alloc (sizeof (showlmp_t) * SHOWLMP_MAXLABELS);

	char lmplabel[256], picname[256];

	Q_strncpy (lmplabel, MSG_ReadString (), 255);
	Q_strncpy (picname, MSG_ReadString (), 255);

	float x = MSG_ReadByte ();
	float y = MSG_ReadByte ();

	int k = -1;

	for (int i = 0; i < SHOWLMP_MAXLABELS; i++)
	{
		if (showlmp[i].isactive)
		{
			if (strcmp (showlmp[i].label, lmplabel) == 0)
			{
				// drop out to replace it
				k = i;
				break;
			}
		}
		else if (k < 0)
		{
			// find first empty one to replace
			k = i;
		}
	}

	// none found to replace
	if (k < 0) return;

	// change existing one
	showlmp[k].isactive = true;
	Q_strncpy (showlmp[k].label, lmplabel, 255);
	Q_strncpy (showlmp[k].pic, picname, 255);
	showlmp[k].x = x;
	showlmp[k].y = y;
}


void SHOWLMP_drawall (void)
{
	if (!nehahra) return;

	if (!showlmp) showlmp = (showlmp_t *) GameZone->Alloc (sizeof (showlmp_t) * SHOWLMP_MAXLABELS);

	D3DDraw_SetSize (&vid.sbarsize);

	// evil evil evil evil
	// ugly ugly ugly ugly
	// vile vile vile vile
	// hack hack hack hack
	for (int i = 0; i < SHOWLMP_MAXLABELS; i++)
	{
		if (showlmp[i].isactive)
		{
			if (!showlmp[i].qpic)
				showlmp[i].qpic = Draw_LoadPic (showlmp[i].pic);

			Draw_Pic (showlmp[i].x, showlmp[i].y, showlmp[i].qpic, 1, true);
		}
	}
}


void SHOWLMP_clear (void)
{
	if (!nehahra) return;
	if (!showlmp) showlmp = (showlmp_t *) GameZone->Alloc (sizeof (showlmp_t) * SHOWLMP_MAXLABELS);

	for (int i = 0; i < SHOWLMP_MAXLABELS; i++) showlmp[i].isactive = false;
}


void SHOWLMP_newgame (void)
{
	showlmp = NULL;
}


/*
============================================================================================================

		RESTORING FOG, SKYBOX, ETC

	Any normal sane implementation would have put these in WorldSpawn, but Nehahra HAS to bow down to the
	great gods of QC.  For Jesus Fucking Christ Almighty and all his wee goblins SAKE.

============================================================================================================
*/

dfunction_t *ED_FindFunction (char *name);

void Neh_QCWeeniesBurnInHell (void)
{
	// eeeewwww!
	if (!nehahra) return;

	func_t RestoreGame;
	dfunction_t *f;

	if ((f = ED_FindFunction ("RestoreGame")))
	{
		if ((RestoreGame = (func_t) (f - SVProgs->Functions)))
		{
			SVProgs->GlobalStruct->time = sv.time;
			SVProgs->GlobalStruct->self = EDICT_TO_PROG (sv_player);
			SVProgs->ExecuteProgram (RestoreGame);
		}
	}
}



