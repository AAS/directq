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

// a lot of this crap is probably unnecessary but we'll clean it out gradually instead of doing a grand sweep

#include "quakedef.h"
#include "d3d_model.h"
#include "d3d_quake.h"


void D3D_AddVisEdict (entity_t *ent);


typedef struct efrag_s
{
	struct entity_s		*entity;
	struct efrag_s		*leafnext;
} efrag_t;


// let's get rid of some more globals...
typedef struct r_efragdef_s
{
	vec3_t		mins, maxs;
	entity_t	*addent;

	mnode_t	*topnode;
} r_efragdef_t;


void R_SplitEntityOnNode (mnode_t *node, r_efragdef_t *ed)
{
	if (node->contents == CONTENTS_SOLID) return;

	// add an efrag if the node is a leaf
	if (node->contents < 0)
	{
		if (!ed->topnode)
			ed->topnode = node;

		mleaf_t *leaf = (mleaf_t *) node;

		// efrags can be just allocated as required without needing to be pulled from a list (cleaner)
		efrag_t *ef = (efrag_t *) MainHunk->Alloc (sizeof (efrag_t));
		ef->entity = ed->addent;

		// set the leaf links
		ef->leafnext = leaf->efrags;
		leaf->efrags = ef;

		return;
	}

	int sides = BOX_ON_PLANE_SIDE (ed->mins, ed->maxs, node->plane);

	if (sides == 3)
	{
		// split on this plane
		// if this is the first splitter of this model, remember it
		if (!ed->topnode)
			ed->topnode = node;
	}

	// recurse down the contacted sides
	if (sides & 1) R_SplitEntityOnNode (node->children[0], ed);
	if (sides & 2) R_SplitEntityOnNode (node->children[1], ed);
}


void R_AddEfrags (entity_t *ent)
{
	// entities with no model won't get drawn
	if (!ent->model) return;

	// never add the world
	if (ent == cl_entities[0]) return;

	r_efragdef_t ed;

	// init the efrag definition struct so that we can avoid more ugly globals
	ed.topnode = NULL;
	ed.addent = ent;

	VectorAdd (ent->origin, ent->model->mins, ed.mins);
	VectorAdd (ent->origin, ent->model->maxs, ed.maxs);

	R_SplitEntityOnNode (cl.worldmodel->brushhdr->nodes, &ed);

	ent->topnode = ed.topnode;
}


void R_StoreEfrags (efrag_t **ppefrag)
{
	efrag_t *pefrag = NULL;

	while ((pefrag = *ppefrag) != NULL)
	{
		ppefrag = &pefrag->leafnext;

		// some progs might try to send static ents with no model through here...
		if (!pefrag->entity->model) continue;

		// prevent adding twice
		if (pefrag->entity->visframe == d3d_RenderDef.framecount) continue;
		if (pefrag->entity->relinkframe == d3d_RenderDef.relinkframe) continue;

		switch (pefrag->entity->model->type)
		{
		case mod_alias:
		case mod_brush:
		case mod_sprite:
			// add it to the visible edicts list
			D3D_AddVisEdict (pefrag->entity);
			break;

		default:
			break;
		}

		// mark that we've recorded this entity for this frame
		pefrag->entity->visframe = d3d_RenderDef.framecount;
		pefrag->entity->relinkframe = d3d_RenderDef.relinkframe;
	}
}


