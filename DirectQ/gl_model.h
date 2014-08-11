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


#ifndef __MODEL__
#define __MODEL__

#include "modelgen.h"
#include "spritegn.h"

/*

d*_t structures are on-disk representations
m*_t structures are in-memory

*/

// entity effects

#define	EF_BRIGHTFIELD			1
#define	EF_MUZZLEFLASH 			2
#define	EF_BRIGHTLIGHT 			4
#define	EF_DIMLIGHT 			8


/*
==============================================================================

BRUSH MODELS

==============================================================================
*/


//
// in memory representation
//
// !!! if this is changed, it must be changed in asm_draw.h too !!!
typedef struct
{
	vec3_t		position;
} mvertex_t;

#define	SIDE_FRONT	0
#define	SIDE_BACK	1
#define	SIDE_ON		2


// plane_t structure
// !!! if this is changed, it must be changed in asm_i386.h too !!!
typedef struct mplane_s
{
	vec3_t	normal;
	float	dist;
	byte	type;			// for texture axis selection and fast side tests
	byte	signbits;		// signx + signy<<1 + signz<<1
	byte	pad[2];
} mplane_t;

typedef struct texture_s
{
	// 1 extra for 16 char texnames
	char		name[17];

	unsigned	width, height;

	void		*d3d_Texture;
	void		*d3d_Fullbright;
	int			visframe;

	struct msurface_s	*texturechain;
	struct msurface_s	*chaintail;

	int			anim_total;				// total tenths in sequence ( 0 = no)
	int			anim_min, anim_max;		// time for this frame min <=time< max
	struct texture_s *anim_next;		// in the animation sequence
	struct texture_s *alternate_anims;	// bmodels in frmae 1 use these
	unsigned	offsets[MIPLEVELS];		// four mip maps stored

	int contentscolor[3];
} texture_t;


#define	SURF_PLANEBACK		2
#define	SURF_DRAWSKY		4
#define SURF_DRAWSPRITE		8
#define SURF_DRAWTURB		16
#define SURF_DRAWTILED		32
#define SURF_DRAWBACKGROUND	64
#define SURF_UNDERWATER		128

// contents types
#define SURF_DRAWLAVA		256
#define SURF_DRAWTELE		512
#define SURF_DRAWWATER		1024
#define SURF_DRAWSLIME		2048

// !!! if this is changed, it must be changed in asm_draw.h too !!!
typedef struct
{
	unsigned short	v[2];
	unsigned int	cachededgeoffset;
} medge_t;

typedef struct
{
	float		vecs[2][4];
	float		mipadjust;
	texture_t	*texture;
	int			flags;
} mtexinfo_t;

#define VERTEXSIZE		7

typedef struct glpolyvert_s
{
	float xyz[3];
	float st[2];
	float lm[2];
} glpolyvert_t;

typedef struct glpoly_s
{
	int numverts;
	glpolyvert_t *verts;
	struct glpoly_s *next;
} glpoly_t;

typedef struct msurface_s
{
	int			visframe;		// should be drawn when node is crossed

	mplane_t	*plane;
	int			flags;

	// 0 to 255 scale
	int			alpha;

	int			firstedge;	// look up in model->surfedges[], negative numbers
	int			numedges;	// are backwards edges

	short		texturemins[2];
	short		extents[2];

	// changed to bytes as they go 0 to 128
	// (pegair03 bugs still happen with ints...)
	byte		light_l, light_t;	// lightmap coordinates (relative to LIGHTMAP_SIZE, not 0 to 1)
	byte		smax, tmax;			// lightmap extents (width and height) (relative to LIGHTMAP_SIZE, not 0 to 1)
	byte		light_r, light_b;	// lightmap rect right and bottom (light_s + smax, light_t + tmax)

	glpoly_t *polys;

	struct	msurface_s	*texturechain;

	mtexinfo_t	*texinfo;

	// lighting info
	int			dlightframe;
	int			dlightbits[4];

	// direct3d stuff
	void		*d3d_Lightmap;

	// the matrix used for transforming this surf
	void		*matrix;

	// extents of the surf in world space
	float		mins[3];
	float		maxs[3];

	byte		styles[MAXLIGHTMAPS];
	int			cached_light[MAXLIGHTMAPS];	// values currently used in lightmap
	bool		cached_dlight;				// true if dynamic light in cache
	byte		*samples;		// [numstyles*surfsize]

	// for alpha sorting
	float		dist;
	float		midpoint[3];
} msurface_t;

typedef struct mnode_s
{
	// common with leaf
	int			contents;		// 0, to differentiate from leafs
	int			visframe;		// node needs to be traversed if current
	bool		seen;
	float		minmaxs[6];		// for bounding box culling
	float		radius;
	struct mnode_s	*parent;
	int			num;
	int			flags;

	// node specific
	mplane_t	*plane;
	struct mnode_s	*children[2];	

	unsigned short		firstsurface;
	unsigned short		numsurfaces;
} mnode_t;



typedef struct mleaf_s
{
	// common with node
	int			contents;		// wil be a negative contents number
	int			visframe;		// node needs to be traversed if current
	bool		seen;
	float		minmaxs[6];		// for bounding box culling
	float		radius;
	struct mnode_s	*parent;
	int			num;
	int			flags;

	// leaf specific
	byte		*compressed_vis;

	msurface_t	**firstmarksurface;
	int			nummarksurfaces;

	// static entities contained in this leaf
	struct staticent_s *statics;

	int			key;			// BSP sequence number for leaf's contents
	byte		ambient_sound_level[NUM_AMBIENTS];

	// contents colour for cshifts
	int *contentscolor;
} mleaf_t;

// !!! if this is changed, it must be changed in asm_i386.h too !!!
typedef struct
{
	dclipnode_t	*clipnodes;
	mplane_t	*planes;
	int			firstclipnode;
	int			lastclipnode;
	vec3_t		clip_mins;
	vec3_t		clip_maxs;
} hull_t;

/*
==============================================================================

SPRITE MODELS

==============================================================================
*/


// FIXME: shorten these?
typedef struct mspriteframe_s
{
	int		width;
	int		height;
	float	up, down, left, right;
	void	*texture;
} mspriteframe_t;

typedef struct
{
	int				numframes;
	float			*intervals;
	mspriteframe_t	*frames[1];
} mspritegroup_t;

typedef struct
{
	spriteframetype_t	type;
	mspriteframe_t		*frameptr;
} mspriteframedesc_t;

typedef struct
{
	int					type;
	int					version;
	int					maxwidth;
	int					maxheight;
	int					numframes;
	float				beamlength;		// remove?
	void				*cachespot;		// remove?
	mspriteframedesc_t	frames[1];
} msprite_t;


/*
==============================================================================

ALIAS MODELS

Alias models are position independent, so the cache manager can move them.
==============================================================================
*/

typedef struct
{
	int					firstpose;
	int					numposes;
	float				interval;
	trivertx_t			bboxmin;
	trivertx_t			bboxmax;
	int					frame;
	char				name[16];
} maliasframedesc_t;

typedef struct
{
	trivertx_t			bboxmin;
	trivertx_t			bboxmax;
	int					frame;
} maliasgroupframedesc_t;

typedef struct
{
	int						numframes;
	int						intervals;
	maliasgroupframedesc_t	frames[1];
} maliasgroup_t;

// !!! if this is changed, it must be changed in asm_draw.h too !!!
typedef struct mtriangle_s
{
	int					facesfront;
	int					vertindex[3];
} mtriangle_t;


#define	MAX_SKINS	32

typedef struct aliashdr_s
{
	int			ident;
	int			version;
	vec3_t		scale;
	vec3_t		scale_origin;
	float		boundingradius;
	vec3_t		eyeposition;
	int			numskins;
	int			skinwidth;
	int			skinheight;
	int			numverts;
	int			numtris;
	int			numframes;
	synctype_t	synctype;
	int			flags;
	float		size;

	bool		mfdelerp;
	bool		nolerp;

	int					numposes;
	int					numorder;
	void *posedata;	// numposes*poseverts trivert_t
	int	 *commands;	// gl command list with embedded s/t
	void				*texture[MAX_SKINS][4];
	void				*fullbright[MAX_SKINS][4];
	byte				*texels[MAX_SKINS];	// only for player skins
	maliasframedesc_t	frames[1];	// variable sized
} aliashdr_t;


extern	stvert_t	*stverts;
extern	mtriangle_t	*triangles;

//===================================================================

//
// Whole model
//

typedef enum {mod_brush, mod_sprite, mod_alias} modtype_t;

#define	EF_ROCKET	1			// leave a trail
#define	EF_GRENADE	2			// leave a trail
#define	EF_GIB		4			// leave a trail
#define	EF_ROTATE	8			// rotate (bonus items)
#define	EF_TRACER	16			// green split trail
#define	EF_ZOMGIB	32			// small blood trail
#define	EF_TRACER2	64			// orange split trail + rotate
#define	EF_TRACER3	128			// purple trail

// mh - special flags
#define EF_RMQRAIN	(1 << 20)	// remake quake rain

// Quake uses 3 (count 'em!) different types of brush model and we need to distinguish between all of them
#define MOD_BRUSH_WORLD		0
#define MOD_BRUSH_INLINE	1
#define MOD_BRUSH_INSTANCED	2

typedef struct brushheader_s
{
	// world, inline or instanced
	int			brushtype;

	int			firstmodelsurface;
	int			nummodelsurfaces;

	int			numsubmodels;
	dmodel_t	*submodels;

	int			numplanes;
	mplane_t	*planes;

	int			numleafs;		// number of visible leafs, not counting 0
	mleaf_t		*leafs;

	int			numvertexes;
	mvertex_t	*vertexes;

	int			numedges;
	medge_t		*edges;

	int			numnodes;
	mnode_t		*nodes;

	int			numtexinfo;
	mtexinfo_t	*texinfo;

	int			numsurfaces;
	msurface_t	*surfaces;
	msurface_t	*alphasurfaces;

	int			numsurfedges;
	int			*surfedges;

	int			numclipnodes;
	dclipnode_t	*clipnodes;

	int			nummarksurfaces;
	msurface_t	**marksurfaces;

	hull_t		hulls[MAX_MAP_HULLS];

	int			numtextures;
	texture_t	**textures;

	byte		*lightdata;
	char		*entities;
} brushhdr_t;


typedef struct model_s
{
	char	name[MAX_QPATH];
	bool	needload;

	modtype_t	type;
	int			numframes;
	synctype_t	synctype;
	
	int			flags;

	// volume occupied by the model graphics
	// alias models normally check this per frame rather than for the entire model;
	// this is retained for compatibility with anything server-side that sill uses it
	vec3_t		mins, maxs;
	float		radius;

	// solid volume for clipping 
	bool	clipbox;
	vec3_t	clipmins, clipmaxs;

	// brush/alias/sprite headers
	// this gets a LOT of polluting data OUT of the model_t struct
	// could turn these into a union... bit it makes accessing them a bit more awkward...
	// besides, it's only an extra 8 bytes per model... chickenfeed, really...
	// it also allows us to be more robust by setting the others to NULL in the loader
	brushhdr_t	*bh;
	aliashdr_t	*ah;
	msprite_t	*sh;

	// the entity that used this model (inline bmodels only)
	struct entity_s *cacheent;

	// true if the model was ever seen
	bool wasseen;
} model_t;

//============================================================================

void	Mod_Init (void);
void	Mod_ClearAll (void);
model_t *Mod_ForName (char *name, bool crash);
void	Mod_TouchModel (char *name);

mleaf_t *Mod_PointInLeaf (float *p, model_t *model);
byte	*Mod_LeafPVS (mleaf_t *leaf, model_t *model);
byte *Mod_FatPVS (vec3_t org);

#endif	// __MODEL__
