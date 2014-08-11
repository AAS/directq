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
// cl_tent.c -- client side temporary entities

#include "quakedef.h"
#include "d3d_model.h"
#include "d3d_quake.h"

void D3D_AddVisEdict (entity_t *ent);


efrag_t		**lastlink;
vec3_t		r_emins, r_emaxs;
entity_t	*r_addent;

efrag_t		*free_efrags = NULL;
mnode_t	*r_pefragtopnode = NULL;


void R_InitEfrags (void)
{
	free_efrags = NULL;
}


#define EXTRA_EFRAGS	64

efrag_t *R_GetEFrag (void)
{
	int i;

	if (free_efrags)
	{
		efrag_t *ef = free_efrags;
		free_efrags = free_efrags->entnext;
		return ef;
	}

	free_efrags = (efrag_t *) MainHunk->Alloc (EXTRA_EFRAGS * sizeof (efrag_t));

	for (i = 0; i < EXTRA_EFRAGS - 1; i++)
		free_efrags[i].entnext = &free_efrags[i + 1];

	free_efrags[i].entnext = NULL;

	return R_GetEFrag ();
}


void R_SplitEntityOnNode (mnode_t *node)
{
	efrag_t		*ef;
	mplane_t	*splitplane;
	mleaf_t		*leaf;
	int			sides;

	if (node->contents == CONTENTS_SOLID) return;

	// add an efrag if the node is a leaf
	if (node->contents < 0)
	{
		if (!r_pefragtopnode)
			r_pefragtopnode = node;

		leaf = (mleaf_t *) node;

		// grab an efrag off the free list
		ef = R_GetEFrag ();
		ef->entity = r_addent;

		// add the entity link
		*lastlink = ef;
		lastlink = &ef->entnext;
		ef->entnext = NULL;

		// set the leaf links
		ef->leaf = leaf;
		ef->leafnext = leaf->efrags;
		leaf->efrags = ef;

		return;
	}

	splitplane = node->plane;
	sides = BOX_ON_PLANE_SIDE (r_emins, r_emaxs, splitplane);

	if (sides == 3)
	{
		// split on this plane
		// if this is the first splitter of this bmodel, remember it
		if (!r_pefragtopnode)
			r_pefragtopnode = node;
	}

	// recurse down the contacted sides
	if (sides & 1) R_SplitEntityOnNode (node->children[0]);
	if (sides & 2) R_SplitEntityOnNode (node->children[1]);
}


void R_AddEfrags (entity_t *ent)
{
	model_t		*entmodel;
	int			i;

	// entities with no model won't get drawn
	if (!ent->model) return;

	// never add the world
	if (ent == cl_entities[0]) return;

	r_addent = ent;

	lastlink = &ent->efrag;
	r_pefragtopnode = NULL;

	entmodel = ent->model;

	for (i = 0; i < 3; i++)
	{
		r_emins[i] = ent->origin[i] + entmodel->mins[i];
		r_emaxs[i] = ent->origin[i] + entmodel->maxs[i];
	}

	R_SplitEntityOnNode (cl.worldmodel->brushhdr->nodes);

	ent->topnode = r_pefragtopnode;
}


void R_StoreEfrags (efrag_t **ppefrag)
{
	entity_t	*pent;
	model_t		*clmodel;
	efrag_t		*pefrag;

	while ((pefrag = *ppefrag) != NULL)
	{
		pent = pefrag->entity;
		ppefrag = &pefrag->leafnext;
		clmodel = pent->model;

		// some progs might try to send static ents with no model through here...
		if (!clmodel) continue;

		// only add static entities on frames during which a full entity relink occurs otherwise we'll just keep on appending
		// them and appending them to the list, and as the scene runs faster things will only get worse (this was horrible)
		if (pent->relinkfame != d3d_RenderDef.relinkframe)
		{
			switch (clmodel->type)
			{
			case mod_alias:
			case mod_brush:
			case mod_sprite:
				pent = pefrag->entity;

				if (pent->visframe != d3d_RenderDef.framecount)
				{
					// add it to the visible edicts list
					D3D_AddVisEdict (pent);

					// mark that we've recorded this entity for this frame
					pent->visframe = d3d_RenderDef.framecount;
				}

				break;

			default:
				break;
			}

			// mark this static as now having been relinked
			pent->relinkfame = d3d_RenderDef.relinkframe;
		}
	}
}


