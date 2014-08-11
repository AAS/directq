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

// the rate at which dynamic client-side effects are updated
extern cvar_t cl_effectrate;

void CL_InitFX (void);
void CL_MuzzleFlash (entity_t *ent, int entnum);
void CL_BrightLight (entity_t *ent, int entnum);
void CL_DimLight (entity_t *ent, int entnum);
void CL_WizardTrail (entity_t *ent, int entnum);
void CL_KnightTrail (entity_t *ent, int entnum);
void CL_RocketTrail (entity_t *ent, int entnum);
void CL_VoreTrail (entity_t *ent, int entnum);
void CL_ColourDlight (dlight_t *dl, unsigned short r, unsigned short g, unsigned short b);


// keep dlight colours in the one place so that if i need to change them i only need to do it once
#define DL_COLOR_GREEN		308, 351, 109
#define DL_COLOR_PURPLE		399, 141, 228
#define DL_COLOR_BLUE		65, 232, 470
#define DL_COLOR_ORANGE		408, 242, 117
#define DL_COLOR_RED		460, 154, 154
#define DL_COLOR_YELLOW		384, 307, 77
#define DL_COLOR_WHITE		256, 256, 256


