/* Software renderer for the 3D view: a column ray caster over the map
 * layers, drawing into the original's 320 x 200 8-bit frame buffer.
 *
 * This is presentation, not game rules: it may differ from the original's
 * quad renderer as long as what the player sees matches. The projection
 * parameters are calibrated against frames captured from the original
 * (see docs/rendering.md).
 */
#ifndef RENDER_VIEW_H
#define RENDER_VIEW_H

#include <stdint.h>

#include "engine/level.h"
#include "gfx/palette.h"
#include "world/inf.h"

#define VIEW_W 320
#define VIEW_H 147            /* rows 0..146 of the screen; the interface is below */
#define SCREEN_W 320
#define SCREEN_H 200
#define VIEW_MAX_TEXTURES 256

typedef struct view_textures {
	int      count;
	uint8_t *tex[VIEW_MAX_TEXTURES];   /* 64 x 64, indexed by slot - 1 */
	int      ceiling;                  /* 0-based texture index, -1 = none */
} view_textures;

typedef struct view_params {
	int    cx, cy;       /* projection centre on screen */
	double focal;        /* pixels per unit of lateral offset at unit depth */
	double eye;          /* eye height above the floor, world units */
	double shade_step;   /* world units of distance per light-table step */
} view_params;

/* Textures named by the level's .INF (@FLOORS and @WALLS share the slot
 * numbering, a .SET counts as its n textures). */
int  view_textures_load(view_textures *vt, const arena_inf *inf);
void view_textures_free(view_textures *vt);

void view_default_params(view_params *vp);

/* Draw the 3D view for a camera at (x, z), facing `angle` (512 per turn),
 * into rows 0..VIEW_H-1 of `screen` (SCREEN_W x SCREEN_H). */
void view_render(uint8_t *screen, const eng_level *lvl, const view_textures *vt,
                 const arena_lgt *lgt, double x, double z, unsigned angle, const view_params *vp);

#endif
