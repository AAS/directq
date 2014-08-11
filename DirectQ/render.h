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

// refresh.h -- public interface to refresh functions

#define	MAXCLIPPLANES	11

#define	TOP_RANGE		16			// soldier uniform colors
#define	BOTTOM_RANGE	96

//=============================================================================

typedef struct aliascache_s
{
	// all the info we need to draw the model is here
	float lastlerp;
	float currlerp;

	struct image_s *teximage;
	struct image_s *lumaimage;
	vec3_t lightspot;
	struct mplane_s *lightplane;
} aliasstate_t;


typedef struct brushstate_s
{
	vec3_t bmoldorigin;
	vec3_t bmoldangles;
	bool bmrelinked;
} brushstate_t;


typedef struct efrag_s
{
	struct mleaf_s		*leaf;
	struct entity_s		*entity;
	struct efrag_s		*entnext;
	struct efrag_s		*leafnext;
} efrag_t;


// just for consistency although it's not protocol-dependent
#define LERP_FINISH (1 << 4)

typedef struct entity_s
{
	bool				forcelink;		// model changed

	int						update_type;

	entity_state_t			baseline;		// to fill in defaults in updates

	float					msgtime;

	vec3_t					msg_origins[2];	// last two updates (0 is newest)
	vec3_t					origin;
	vec3_t					msg_angles[2];	// last two updates (0 is newest)
	vec3_t					angles;
	struct model_s			*model;			// NULL = no model
	struct efrag_s			*efrag;
	int						frame;
	float					syncbase;		// for client-side animations
	float					skinbase;
	float					posebase;
	int						effects;		// light, particals, etc
	int						skinnum;		// for Alias models
	int						visframe;		// last frame this entity was found in an active leaf
	vec3_t					modelorg;		// relative to r_origin

	// player skins
	int						playerskin;

	// allocated at runtime client and server side
	int						entnum;

	// VBO cache
	int cacheposes;

	// interpolation
	float		framestarttime;
	int			lastpose, currpose;

	float		translatestarttime;
	vec3_t		lastorigin, currorigin;

	float		rotatestarttime;
	vec3_t		lastangles, currangles;

	// allows an alpha value to be assigned to any entity
	int			alphaval;
	float		lerpinterval;
	int			lerpflags;

	// light averaging
	float		shadelight[3];
	float		ambientlight[3];

	// false if the entity is to be subjected to bbox culling
	bool		nocullbox;

	// distance from client (for depth sorting)
	float		dist;

	// FIXME: could turn these into a union
	// done (trivial_accept is only on alias models, topnode only on brush models)
	union
	{
		int						trivial_accept;
		struct mnode_s			*topnode;		// for bmodels, first world node
	};
	//  that splits bmodel, or NULL if
	//  not split

	// the matrix used for transforming this entity
	// D3DXMATRIX in *incredibly* unhappy living in an entity_t struct...
	D3DMATRIX		matrix;

	// check transforms for all model types
	bool			rotated;

	// cached info about models
	// these can't be in a union as the entity slot could be reused for a different model type
	aliasstate_t		aliasstate;
	brushstate_t		brushstate;
} entity_t;


// !!! if this is changed, it must be changed in asm_draw.h too !!!
typedef struct
{
	vrect_t		vrect;				// subwindow in video for refresh
	// FIXME: not need vrect next field here?
	vrect_t		aliasvrect;			// scaled Alias version
	int			vrectright, vrectbottom;	// right & bottom screen coords
	int			aliasvrectright, aliasvrectbottom;	// scaled Alias versions
	float		vrectrightedge;			// rightmost right edge we care about,
	//  for use in edge list
	float		fvrectx, fvrecty;		// for floating-point compares
	float		fvrectx_adj, fvrecty_adj; // left and top edges, for clamping
	int			vrect_x_adj_shift20;	// (vrect.x + 0.5 - epsilon) << 20
	int			vrectright_adj_shift20;	// (vrectright + 0.5 - epsilon) << 20
	float		fvrectright_adj, fvrectbottom_adj;
	// right and bottom edges, for clamping
	float		fvrectright;			// rightmost edge, for Alias clamping
	float		fvrectbottom;			// bottommost edge, for Alias clamping
	float		horizontalFieldOfView;	// at Z = 1.0, this many X is visible
	// 2.0 = 90 degrees
	float		xOrigin;			// should probably allways be 0.5
	float		yOrigin;			// between be around 0.3 to 0.5

	vec3_t		vieworg;
	vec3_t		viewangles;

	float		fov_x, fov_y;

	int			ambientlight;
} refdef_t;


// refresh
extern	int		reinit_surfcache;


extern	refdef_t	r_refdef;
extern vec3_t	r_origin, vpn, vright, vup;

extern	struct texture_s	*r_notexture_mip;


void R_Init (void);
void R_InitTextures (void);
void R_RenderView (float timepassed);		// must set r_refdef first
void R_ViewChanged (vrect_t *pvrect, int lineadj, float aspect);
// called whenever r_refdef or vid change

void R_NewMap (void);

void R_ParseParticleEffect (void);
void R_RunParticleEffect (vec3_t org, vec3_t dir, int color, int count);
void R_RocketTrail (vec3_t start, vec3_t end, int type);

#ifdef QUAKE2
void R_DarkFieldParticles (entity_t *ent);
#endif
void R_EntityParticles (entity_t *ent);
void R_BlobExplosion (vec3_t org);
void R_ParticleExplosion (vec3_t org);
void R_ParticleExplosion2 (vec3_t org, int colorStart, int colorLength);
void R_LavaSplash (vec3_t org);
void R_TeleportSplash (vec3_t org);


// surface cache related
extern	int		reinit_surfcache;	// if 1, surface cache is currently empty and

int	D_SurfaceCacheForRes (int width, int height);
void D_DeleteSurfaceCache (void);
void D_InitCaches (void *buffer, int size);
void R_SetVrect (vrect_t *pvrect, vrect_t *pvrectin, int lineadj);

