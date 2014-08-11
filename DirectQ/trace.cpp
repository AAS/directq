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

// line tracing from the light program

#include "quakedef.h"
#include "d3d_quake.h"

typedef struct tnode_s
{
	int		type;
	vec3_t	normal;
	float	dist;
	int		children[2];
	int		pad;
} tnode_t;

tnode_t *tnodes;
tnode_t *tnode_p;


/*
==============
MakeTnode

Converts the disk node structure into the efficient tracing structure
==============
*/
void MakeTnode (int nodenum)
{
	tnode_t			*t;
	dplane_t		*plane;
	int				i;
	dnode_t 		*node;

	t = tnode_p++;

	node = dnodes + nodenum;
	plane = dplanes + node->planenum;

	t->type = plane->type;
	VectorCopy (plane->normal, t->normal);
	t->dist = plane->dist;

	for (i = 0; i < 2; i++)
	{
		if (node->children[i] < 0)
			t->children[i] = dleafs[-node->children[i] - 1].contents;
		else
		{
			t->children[i] = tnode_p - tnodes;
			MakeTnode (node->children[i]);
		}
	}
}


/*
=============
MakeTnodes

Loads the node structure out of a .bsp file to be used for light occlusion
=============
*/
void MakeTnodes (int numnodes)
{
	tnode_p = tnodes = (tnode_t *) Heap_TagAlloc (TAG_BRUSHMODELS, numnodes * sizeof (tnode_t));
	MakeTnode (0);
}



/*
==============================================================================

LINE TRACING

The major lighting operation is a point to point visibility test, performed
by recursive subdivision of the line by the BSP tree.

==============================================================================
*/

typedef struct
{
	vec3_t	backpt;
	int		side;
	int		node;
} tracestack_t;


/*
==============
TestLine
==============
*/
bool TestLine (vec3_t start, vec3_t stop)
{
	int				node;
	float			front, back;
	tracestack_t	*tstack_p;
	int				side;
	float 			frontx,fronty, frontz, backx, backy, backz;
	tracestack_t	tracestack[64];
	tnode_t			*tnode;

	frontx = start[0];
	fronty = start[1];
	frontz = start[2];
	backx = stop[0];
	backy = stop[1];
	backz = stop[2];

	tstack_p = tracestack;
	node = 0;

	while (1)
	{
		while (node < 0 && node != CONTENTS_SOLID)
		{
			// pop up the stack for a back side
			tstack_p--;

			if (tstack_p < tracestack)
				return true;
			node = tstack_p->node;

			// set the hit point for this plane
			frontx = backx;
			fronty = backy;
			frontz = backz;

			// go down the back side
			backx = tstack_p->backpt[0];
			backy = tstack_p->backpt[1];
			backz = tstack_p->backpt[2];

			node = tnodes[tstack_p->node].children[!tstack_p->side];
		}

		if (node == CONTENTS_SOLID)
			return false;	// DONE!

		tnode = &tnodes[node];

		switch (tnode->type)
		{
		case PLANE_X:
			front = frontx - tnode->dist;
			back = backx - tnode->dist;
			break;
		case PLANE_Y:
			front = fronty - tnode->dist;
			back = backy - tnode->dist;
			break;
		case PLANE_Z:
			front = frontz - tnode->dist;
			back = backz - tnode->dist;
			break;
		default:
			front = (frontx*tnode->normal[0] + fronty*tnode->normal[1] + frontz*tnode->normal[2]) - tnode->dist;
			back = (backx*tnode->normal[0] + backy*tnode->normal[1] + backz*tnode->normal[2]) - tnode->dist;
			break;
		}

		if (front > -ON_EPSILON && back > -ON_EPSILON)
		{
			node = tnode->children[0];
			continue;
		}

		if (front < ON_EPSILON && back < ON_EPSILON)
		{
			node = tnode->children[1];
			continue;
		}

		side = front < 0;

		front = front / (front-back);

		tstack_p->node = node;
		tstack_p->side = side;
		tstack_p->backpt[0] = backx;
		tstack_p->backpt[1] = backy;
		tstack_p->backpt[2] = backz;

		tstack_p++;

		backx = frontx + front*(backx-frontx);
		backy = fronty + front*(backy-fronty);
		backz = frontz + front*(backz-frontz);

		node = tnode->children[side];		
	}	
}


float PointDist (vec3_t p1, vec3_t p2)
{
	int		i;
	vec_t	t;

	for (i = 0, t = 0; i < 3; i++)
		t += (p2[i] - p1[i]) * (p2[i] - p1[i]);

	// don't blow up...
	if (t < 1) t = 1;

	return sqrt (t);
}


/*
============
CastRay

Returns the distance between the points, or -1 if blocked
=============
*/
float CastRay (float *p1, float *p2)
{
	bool	trace;

	trace = TestLine (p1, p2);

	if (!trace)
		return -1;		// ray was blocked

	return PointDist (p1, p2);
}


__inline float OffsetRandom (float MIN, float MAX)
{
	return (float) (rand () & 255) * ((MAX - MIN) * 0.00390625f) + MIN;
}


bool TestEntityVolumePoints (float *view, float *pt, float *mins, float *maxs)
{
	for (int i = 0; i < 64; i++)
	{
		// set up a random point in the entity volume
		float end[3] =
		{
			pt[0] + OffsetRandom (mins[0], maxs[0]),
			pt[1] + OffsetRandom (mins[1], maxs[1]),
			pt[2] + OffsetRandom (mins[2], maxs[2]),
		};

		// not blocked
		if (TestLine (view, end)) return true;
	}

	// blocked
	return false;
}


