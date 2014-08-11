
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

#include "quakedef.h"
#include "location.h"


char *locDefault = "somewhere";
char *locNowhere = "nowhere";

location_t	*locations = NULL;
int			numlocations = 0;


void LOC_LoadLocations (void)
{
	locations = NULL;
	numlocations = 0;

	char locname[MAX_PATH];
	COM_StripExtension (cl.worldmodel->name, locname);

	locname[0] = 'l';
	locname[1] = 'o';
	locname[2] = 'c';
	locname[3] = 's';
	COM_DefaultExtension (locname, ".loc");

	char *locdata = (char *) COM_LoadTempFile (locname);

	if (!locdata)
	{
		Con_DPrintf ("Failed to load %s\n", locname);
		return;
	}

	Con_DPrintf ("Loading %s\n", locname);

	locations = (location_t *) Pool_Alloc (POOL_MAP, sizeof (location_t));
	location_t *l = locations;

	while (1)
	{
		// parse a line from the LOC string
		if (!(locdata = COM_Parse (locdata, COM_PARSE_LINE))) break;

		// scan it in to a temp location
		if (sscanf (com_token, "%f, %f, %f, %f, %f, %f, ", &l->a[0], &l->a[1], &l->a[2], &l->b[0], &l->b[1], &l->b[2]) == 6)
		{
			l->sd = 0;	// JPG 1.05 

			for (int i = 0; i < 3; i++)
			{
				if (l->a[i] > l->b[i])
				{
					float temp = l->a[i];
					l->a[i] = l->b[i];
					l->b[i] = temp;
				}

				l->sd += l->b[i] - l->a[i];  // JPG 1.05
			}

			l->a[2] -= 32.0;
			l->b[2] += 32.0;

			// now get the name - this is potentially evil stuff...
			// scan to first quote and remove it
			for (int i = 0; ; i++)
			{
				if (!com_token[i])
				{
					// there may not be a first quote...
					strncpy (l->name, com_token, 31);
					break;
				}

				if (com_token[i] == '\"')
				{
					// the valid first character is after the quote
					strncpy (l->name, &com_token[i + 1], 31);
					break;
				}
			}

			// scan to last quote and NULL term it there
			for (int i = 0; ; i++)
			{
				if (!l->name[i]) break;

				if (l->name[i] == '\"')
				{
					l->name[i] = 0;
					break;
				}
			}

			Con_DPrintf ("Read location %s\n", l->name);

			// set up a new empty location (this may not be used...)
			l = (location_t *) Pool_Alloc (POOL_MAP, sizeof (location_t));
			numlocations++;
		}
	}

	Con_DPrintf ("Read %i locations\n", numlocations);
}


char *LOC_GetLocation (vec3_t p)
{
	// no locations available
	if (!locations || !numlocations) return locNowhere;

	location_t *l;
	location_t *bestloc;
	float dist, bestdist;

	bestloc = NULL;
	bestdist = 999999;

	for (l = locations; l < locations + numlocations; l++)
	{
		dist =	fabs (l->a[0] - p[0]) + fabs (l->b[0] - p[0]) + 
				fabs (l->a[1] - p[1]) + fabs (l->b[1] - p[1]) +
				fabs (l->a[2] - p[2]) + fabs (l->b[2] - p[2]) - l->sd;

		if (dist < .01) return l->name;

		if (dist < bestdist)
		{
			bestdist = dist;
			bestloc = l;
		}
	}

	if (bestloc)
		return bestloc->name;

	return locDefault;
}
