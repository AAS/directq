
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


// the callback knows what type of data to expect and will interpret it accordingly
typedef void (*vbostatefunc_t) (void *);

typedef struct quaddef_textured_s
{
	float x;
	float w;
	float y;
	float h;
	DWORD c;
	float l;
	float r;
	float t;
	float b;
} quaddef_textured_t;


typedef struct quaddef_coloured_s
{
	float x;
	float w;
	float y;
	float h;
	DWORD c;
} quaddef_coloured_t;


void VBO_BeginFrame (void);
void VBO_EndFrame (void);
void VBO_AddCallback (vbostatefunc_t callback, void *data = NULL, int len = 0);
void VBO_DestroyBuffers (void);
void VBO_Add2DQuad (quaddef_textured_t *q, bool rotate = false);
void VBO_Add2DQuad (quaddef_coloured_t *q);
void VBO_AddSky (msurface_t *surf, entity_t *ent);
void VBO_AddSolidSurf (msurface_t *surf, entity_t *ent);
void VBO_AddWarpSurf (msurface_t *surf, entity_t *ent);
void VBO_AddAliasPart (entity_t *ent, aliaspart_t *part);
void VBO_AddAliasShadow (entity_t *ent, aliashdr_t *hdr, aliaspart_t *part, aliasstate_t *aliasstate, DWORD shadecolor);
void VBO_AddBBox (float *origin, float *mins, float *maxs, float expand);
void VBO_AddParticle (float *origin, float scale, float *up, float *right, DWORD color);
void VBO_AddSprite (mspriteframe_t *frame, float *origin, float *up, float *right, int alpha);
void VBO_AddSkySphere (warpverts_t *verts, int numverts, unsigned short *indexes, int numindexes);
void VBO_RTTBlendScreen (D3DCOLOR blend);
void VBO_RTTWarpScreen (int tess, float rdt, D3DCOLOR blend);


