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

typedef struct location_s
{
	vec3_t a;		// min xyz corner
	vec3_t b;		// max xyz corner
	vec_t sd;		// sum of dimensions  // JPG 1.05 
	char name[32];
} location_t;

// Load the locations for the current level from the location file
void LOC_LoadLocations (void);

// Get the name of the location of a point
char *LOC_GetLocation (vec3_t p);

