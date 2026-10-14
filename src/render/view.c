/* 3D view renderer. See view.h. */
#include "render/view.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "gfx/img.h"

#define CELL 128.0
#define PI2  6.283185307179586

static uint8_t *copy_texture(const uint8_t *src)
{
	uint8_t *t = malloc(64 * 64);
	if (t)
		memcpy(t, src, 64 * 64);
	return t;
}

int view_textures_load(view_textures *vt, const arena_inf *inf)
{
	int k, ceiling_slot = 0, counter = 1;
	memset(vt, 0, sizeof *vt);
	vt->ceiling = -1;
	for (k = 0; k < inf->count; k++) {
		const inf_entry *e = &inf->entries[k];
		if (e->section != INF_WALLS && e->section != INF_FLOORS)
			continue;
		if (e->kind == INF_DIRECTIVE && !strcmp(e->name, "CEILING"))
			ceiling_slot = counter;
		if (e->kind != INF_FILE)
			continue;
		if (e->set_count) {
			arena_texset ts;
			int i;
			if (texset_load(e->name, e->set_count, &ts) == 0) {
				for (i = 0; i < e->set_count && vt->count < VIEW_MAX_TEXTURES; i++)
					vt->tex[vt->count++] = copy_texture(texset_texture(&ts, i));
				texset_free(&ts);
			} else {
				for (i = 0; i < e->set_count && vt->count < VIEW_MAX_TEXTURES; i++)
					vt->tex[vt->count++] = NULL;
			}
			counter += e->set_count;
		} else {
			arena_img   img;
			const char *msg;
			if (vt->count < VIEW_MAX_TEXTURES) {
				if (img_load(e->name, &img, &msg) == 0) {
					vt->tex[vt->count++] = (img.width == 64 && img.height == 64) ? copy_texture(img.pixels) : NULL;
					img_free(&img);
				} else {
					vt->tex[vt->count++] = NULL;
				}
			}
			counter++;
		}
	}
	if (ceiling_slot > 0 && ceiling_slot <= vt->count)
		vt->ceiling = ceiling_slot - 1;
	return 0;
}

void view_textures_free(view_textures *vt)
{
	int i;
	for (i = 0; i < vt->count; i++)
		free(vt->tex[i]);
	memset(vt, 0, sizeof *vt);
}

void view_default_params(view_params *vp)
{
	vp->cx = 159;
	vp->cy = 73;
	vp->focal = 160.0;
	vp->eye = 60.0;
	vp->shade_step = 48.0;
}

static const uint8_t *texture(const view_textures *vt, int index)
{
	return (index >= 0 && index < vt->count) ? vt->tex[index] : NULL;
}

static uint8_t shade(const arena_lgt *lgt, uint8_t c, double dist, const view_params *vp)
{
	int t;
	if (!lgt)
		return c;
	t = (int)(dist / vp->shade_step);
	if (t > LGT_TABLES - 1)
		t = LGT_TABLES - 1;
	if (t < 0)
		t = 0;
	return lgt->table[t][c];
}

/* Does a MAP1 cell stop the ray as a full-height wall? Returns the 0-based
 * texture index, or -1 to look further. */
static int wall_texture(uint16_t v)
{
	unsigned lo = v & 0xFF, hi = v >> 8;
	if (v == 0)
		return -1;
	if (v & 0x8000) {
		switch (v >> 12) {
		case 0xB:                       /* door: drawn closed for now */
		case 0x9:                       /* see-through wall */
			return (int)(lo & 0x3F) - 1;
		default:
			return -1;
		}
	}
	if (lo == hi)
		return (int)(lo & 0x3F) - 1;
	return -1;
}

void view_render(uint8_t *screen, const eng_level *lvl, const view_textures *vt,
                 const arena_lgt *lgt, double cam_x, double cam_z, unsigned angle, const view_params *vp)
{
	double a = (angle & 511) * PI2 / 512.0;
	double fx = sin(a), fz = cos(a);          /* forward */
	double rx = cos(a), rz = -sin(a);         /* right: facing + a quarter turn */
	double ceiling = lvl->ceiling;
	int    col, y;
	const uint8_t *ctex = texture(vt, vt->ceiling);

	for (col = 0; col < VIEW_W; col++) {
		double k = (col - vp->cx) / vp->focal;
		double dx = fx + rx * k, dz = fz + rz * k;   /* depth component along forward = 1 */
		double px = cam_x / CELL, pz = cam_z / CELL;
		int    mx = (int)floor(px), mz = (int)floor(pz);
		int    stepx = dx < 0 ? -1 : 1, stepz = dz < 0 ? -1 : 1;
		double ddx = dx != 0 ? fabs(1.0 / dx) : 1e30, ddz = dz != 0 ? fabs(1.0 / dz) : 1e30;
		double sx = dx < 0 ? (px - mx) * ddx : (mx + 1.0 - px) * ddx;
		double sz = dz < 0 ? (pz - mz) * ddz : (mz + 1.0 - pz) * ddz;
		double depth = 1e30, u = 0;
		int    tex = -1, side = 0, guard;
		int    top, bottom;

		for (guard = 0; guard < 256; guard++) {
			if (sx < sz) {
				sx += ddx;
				mx += stepx;
				side = 0;
			} else {
				sz += ddz;
				mz += stepz;
				side = 1;
			}
			if (mx < 0 || mz < 0 || mx >= LEVEL_DIM || mz >= LEVEL_DIM)
				break;
			tex = wall_texture(lvl->map1[mz * LEVEL_DIM + mx]);
			if (tex >= 0) {
				double t = side == 0 ? sx - ddx : sz - ddz;   /* in cells along (dx, dz) */
				depth = t * CELL;
				if (side == 0)
					u = pz + t * dz;
				else
					u = px + t * dx;
				u -= floor(u);
				if ((side == 0 && dx < 0) || (side == 1 && dz > 0))
					u = 1.0 - u;
				break;
			}
		}

		/* Wall span. */
		if (tex >= 0 && depth > 0.5) {
			double s = vp->focal / depth;
			top = (int)ceil(vp->cy - (ceiling - vp->eye) * s);
			bottom = (int)floor(vp->cy + vp->eye * s);
		} else {
			top = bottom = vp->cy;
		}
		for (y = 0; y < VIEW_H; y++) {
			uint8_t c = 0;
			if (y >= top && y <= bottom && tex >= 0) {
				const uint8_t *t = texture(vt, tex);
				double v = (vp->cy + vp->eye * vp->focal / depth - y) / ((ceiling) * vp->focal / depth);
				int tu = (int)(u * 64) & 63, tv = 63 - ((int)(v * 64) & 63);
				c = t ? shade(lgt, t[tv * 64 + tu], depth, vp) : 0;
			} else if (y > vp->cy) {
				/* Floor. */
				double dist = vp->eye * vp->focal / (y - vp->cy);
				double wx = cam_x + dx * dist, wz = cam_z + dz * dist;
				int    cx = (int)floor(wx / CELL), cz = (int)floor(wz / CELL);
				if (cx >= 0 && cz >= 0 && cx < LEVEL_DIM && cz < LEVEL_DIM) {
					const uint8_t *t = texture(vt, lvl->flor[cz * LEVEL_DIM + cx] >> 8);
					int tu = ((int)floor(wx) & 127) >> 1, tv = ((int)floor(wz) & 127) >> 1;
					c = t ? shade(lgt, t[tv * 64 + tu], dist, vp) : 0;
				}
			} else if (y < vp->cy && ctex) {
				/* Ceiling. */
				double dist = (ceiling - vp->eye) * vp->focal / (vp->cy - y);
				double wx = cam_x + dx * dist, wz = cam_z + dz * dist;
				int    tu = ((int)floor(wx) & 127) >> 1, tv = ((int)floor(wz) & 127) >> 1;
				c = shade(lgt, ctex[tv * 64 + tu], dist, vp);
			}
			screen[y * SCREEN_W + col] = c;
		}
	}
}
