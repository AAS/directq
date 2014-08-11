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

// draw.h -- these are the only functions outside the refresh allowed
// to touch the vid buffer

extern	qpic_t		*draw_disc;	// also used on sbar

void Draw_Init (void);
void Draw_Character (int x, int y, int num);
void Draw_BackwardsCharacter (int x, int y, int num);
void Draw_RotateCharacter (int x, int y, int num);
void Draw_InvertCharacter (int x, int y, int num);
void Draw_VScrollBar (int x, int y, int height, int pos, int scale);
void Draw_DebugChar (char num);
void Draw_Pic (int x, int y, qpic_t *pic);
void Draw_Pic (int x, int y, qpic_t *pic);
void Draw_PicTranslate (int x, int y, qpic_t *pic, byte *translation);
void Draw_ConsoleBackground (int lines);
void Draw_TileClear (int x, int y, int w, int h);
void Draw_Fill (int x, int y, int w, int h, int c, int alpha = 255);
void Draw_FadeScreen (void);
void Draw_String (int x, int y, char *str);
void Draw_TextBox (int x, int y, int width, int height);
qpic_t *Draw_PicFromWad (char *name);
qpic_t *Draw_CachePic (char *path);
void Draw_HalfPic (int x, int y, qpic_t *pic);
void D3D_Set2DShade (float shadecolor);

