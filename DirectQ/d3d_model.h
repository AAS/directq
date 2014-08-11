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
#define	EF_REDLIGHT 			16
#define	EF_BLUELIGHT 			32

#define EF_FULLBRIGHT			16384
#define EF_NOOCCLUDE			32768

/*
==============================================================================

BRUSH MODELS

==============================================================================
*/

#define	SIDE_FRONT	0
#define	SIDE_BACK	1
#define	SIDE_ON		2

typedef struct mvertex_s
{
	vec3_t position;
} mvertex_t;

typedef struct mclipnode_s
{
	int			planenum;
	int			children[2];	// negative numbers are contents
} mclipnode_t;

// plane_t structure
// !!! if this is changed, it must be changed in asm_i386.h too !!!
typedef struct mplane_s
{
	vec3_t	normal;
	float	dist;
	byte	type;			// for texture axis selection and fast side tests
	byte	signbits;		// signx + signy<<1 + signz<<1
	byte	pad[2];			// wtf???
	int		cacheframe;
	float	cachedist;
	entity_t *cacheent;
} mplane_t;

typedef struct texture_s
{
	// 1 extra for 16 char texnames
	char		name[17];

	float		size[2];

	struct image_s *teximage;
	struct image_s *lumaimage;

	int			visframe;

	int			anim_total;				// total tenths in sequence (0 = no)
	int			anim_min, anim_max;		// time for this frame min <=time< max

	struct texture_s *anim_next;		// in the animation sequence
	struct texture_s *alternate_anims;	// bmodels in frmae 1 use these

	int contentscolor[3];

	// chains for rendering
	struct msurface_s *texturechain;
} texture_t;


// basic types
#define	SURF_PLANEBACK		2
#define	SURF_DRAWSKY		4
#define SURF_DRAWTURB		16

// contents types
#define SURF_DRAWLAVA		256
#define SURF_DRAWTELE		512
#define SURF_DRAWWATER		1024
#define SURF_DRAWSLIME		2048
#define SURF_DRAWFENCE		4096

typedef struct medge_s
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


typedef struct warpverts_s
{
	float v[3];
	float st[2];
} warpverts_t;


typedef struct colouredvert_s
{
	float xyz[3];
	D3DCOLOR color;
} colouredvert_t;


typedef struct aliaspolyvert_s
{
	float xyz[3];
	D3DCOLOR color;
	float st[2];
} aliaspolyvert_t;


typedef struct brushpolyvert_s
{
	float xyz[3];
	float st[2][2];
} brushpolyvert_t;


typedef struct msurface_s
{
	// data needed to draw the surface
	int firstvertex;
	int numvertexes;
	int vertexrange;
	int numindexes;
	int streamoffset;

	mtexinfo_t	*texinfo;

	int			LightmapTextureNum;
	LPDIRECT3DTEXTURE9 d3d_LightmapTex;

	// other stuff
	int			clipflags;
	int			visframe;		// should be drawn when node is crossed

	struct model_s *model;
	mplane_t	*plane;
	int			flags;

	int			firstedge;

	struct msurface_s *texturechain;
	struct msurface_s *lightmapchain;

	short		texturemins[2];
	short		extents[2];

	// changed to ints for the new larger lightmap sizes
	// note - even shorts are too small for the new max surface extents
	int			smax, tmax;			// lightmap extents (width and height) (relative to LIGHTMAP_SIZE, not 0 to 1)

	// rectangle specifying the surface lightmap
	RECT		LightRect;
	int			LightmapOffset;

	// lighting info
	int			dlightframe;
	int			dlightbits[MAX_DLIGHTS >> 5];

	// lighting parameters may change in which case the lightmap is rebuilt for this surface
	int			LightProperties;

	// extents of the surf in world space
	float		mins[3];
	float		maxs[3];

	byte		styles[MAXLIGHTMAPS];
	int			cached_light[MAXLIGHTMAPS];	// values currently used in lightmap
	int			numstyles;

	byte		*samples[MAXLIGHTMAPS];		// [numstyles*surfsize]

	// for alpha sorting
	float		midpoint[3];
} msurface_t;


typedef struct mnode_s
{
	// common with leaf
	int			contents;		// 0, to differentiate from leafs
	int			visframe;		// node needs to be traversed if current
	float		mins[3];		// for bounding box culling
	float		maxs[3];		// for bounding box culling
	struct mnode_s	*parent;
	int			num;
	int			flags;

	// node specific
	float		dot;
	int			side;
	mplane_t	*plane;
	struct mnode_s	*children[2];
	msurface_t	*surfaces;
	int	numsurfaces;
} mnode_t;



typedef struct mleaf_s
{
	// common with node
	int			contents;		// wil be a negative contents number
	int			visframe;		// node needs to be traversed if current
	float		mins[3];		// for bounding box culling
	float		maxs[3];		// for bounding box culling
	struct mnode_s	*parent;
	int			num;
	int			flags;

	// leaf specific
	byte		*compressed_vis;
	msurface_t	**firstmarksurface;
	int			nummarksurfaces;
	struct efrag_s	*efrags;	// client-side for static entities
	// int			sv_visframe;
	// int			key;			// BSP sequence number for leaf's contents
	byte		ambient_sound_level[NUM_AMBIENTS];

	// contents colour for cshifts
	int *contentscolor;
} mleaf_t;

// !!! if this is changed, it must be changed in asm_i386.h too !!!
typedef struct
{
	mclipnode_t	*clipnodes;
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
	float	s, t;
	struct image_s *texture;
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
	float				*intervals;
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

typedef struct aliasmesh_s
{
	unsigned short st[2];
	unsigned short vertindex;
} aliasmesh_t;

typedef struct aliasskin_s
{
	struct image_s *cmapimage[4];	// player skins only
	struct image_s *teximage[4];
	struct image_s *lumaimage[4];
} aliasskin_t;


typedef struct aliasbbox_s
{
	float mins[3];
	float maxs[3];
} aliasbbox_t;


typedef struct aliashdr_s
{
	vec3_t		scale;
	vec3_t		scale_origin;

	float		boundingradius;
	synctype_t	synctype;
	unsigned int drawflags;
	float		size;

	int			nummeshframes;
	int			numtris;

	int			vertsperframe;
	int			numframes;

	aliasmesh_t			*meshverts;
	int					nummesh;
	int					meshoffset;
	int					*streamoffsets;

	unsigned short		*indexes;
	int					numindexes;
	int					firstindex;

	// for view model updating
	float				lastblends[2];
	bool				mfdelerp;

	struct drawvertx_s	**vertexes;
	maliasframedesc_t	*frames;
	aliasbbox_t			*bboxes;

	int			skinwidth;
	int			skinheight;
	int			numskins;
	aliasskin_t *skins;
} aliashdr_t;


//===================================================================

// Whole model

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
#define EF_PLAYER	(1 << 21)

// prevent bmodels from picking up entity dlight effects
#define MOD_WORLD	65536
#define MOD_BMODEL	131072

typedef struct brushheader_s
{
	int			firstmodelsurface;
	int			nummodelsurfaces;

	int			numsubmodels;
	dmodel_t	*submodels;

	int			numplanes;
	mplane_t	*planes;

	int			numleafs;		// number of visible leafs, not counting 0
	mleaf_t		*leafs;

	int			numnodes;
	mnode_t		*nodes;

	int			numtexinfo;
	mtexinfo_t	*texinfo;

	int			numsurfaces;
	msurface_t	*surfaces;

	int			numclipnodes;
	mclipnode_t	*clipnodes;

	int			nummarksurfaces;
	msurface_t	**marksurfaces;

	hull_t		hulls[MAX_MAP_HULLS];

	int			numtextures;
	texture_t	**textures;

	byte		*lightdata;
	char		*entities;

	// loaded directly from disk with no intermediate processing
	dvertex_t	*dvertexes;
	dedge_t		*dedges;
	int			*dsurfedges;

	int			numsurfvertexes;

	// 29 (Q1) or 30 (HL)
	int			bspversion;

	// bounding box used for rendering with
	float		bmins[3];
	float		bmaxs[3];
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
	brushhdr_t	*brushhdr;
	aliashdr_t	*aliashdr;
	msprite_t	*spritehdr;

	// will be == d3d_RenderDef.RegistrationSequence is this model was touched on this map load
	int RegistrationSequence;
} model_t;

//============================================================================

void	Mod_Init (void);
void	Mod_ClearAll (void);
model_t *Mod_ForName (char *name, bool crash);
void	Mod_TouchModel (char *name);

// this can be greater than MAX_MODELS if an alias model is in the cache
#define	MAX_MOD_KNOWN	8192

extern model_t	**mod_known;
extern int mod_numknown;

mleaf_t *Mod_PointInLeaf (float *p, model_t *model);
byte	*Mod_LeafPVS (mleaf_t *leaf, model_t *model);
byte *Mod_FatPVS (vec3_t org);

#endif	// __MODEL__
