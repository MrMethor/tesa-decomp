/* Engine constants from ACD.EXE. See consts.h. */
#include "engine/consts.h"

#include <string.h>

/* DGROUP offsets (ACD.EXE, CD version). */
#define DG_SINE        0xB03C
#define DG_STAMPS      0xE3A2
#define DG_NEAR_PLAYER 0xE7B8
#define DG_NEAR_ACTOR  0xE7EE
#define DG_BOX_BOTTOM  0xCC04   /* clean copy; the working copy CB94 is rebuilt from it per level */
#define DG_BOX_THICK   0xCC34
#define DG_CEILING     0xD80A
#define DG_FALL        0xE8BC
#define DG_BOB         0xE824
#define DG_SWIM_BOB    0x7A6E
#define DG_REGIONS     0x79D8   /* 9 x {handler, cursor, x0, y0, x1, y1} */

static int16_t s16(const uint8_t *p) { return (int16_t)(p[0] | (p[1] << 8)); }

static void words(const arena_exe *exe, uint16_t off, int16_t *out, int n)
{
	int i;
	for (i = 0; i < n; i++)
		out[i] = s16(exe->dgroup + off + 2 * i);
}

int eng_tables_load(const arena_exe *exe, eng_tables *t)
{
	int i;
	memset(t, 0, sizeof *t);
	if (!exe->dgroup || exe->dgroup_size < 0xE900)
		return -1;
	words(exe, DG_SINE, t->sine, ENG_ANGLES + ENG_ANGLES / 4);
	memcpy(t->stamp, exe->dgroup + DG_STAMPS, sizeof t->stamp);
	words(exe, DG_NEAR_PLAYER, &t->near_player[0][0], 27);
	words(exe, DG_NEAR_ACTOR, &t->near_actor[0][0], 27);
	words(exe, DG_BOX_BOTTOM, &t->box_bottom[0][0], 24);
	words(exe, DG_BOX_THICK, &t->box_thick[0][0], 32);
	t->ceiling_lo = s16(exe->dgroup + DG_CEILING);
	t->ceiling_hi = s16(exe->dgroup + DG_CEILING + 2);
	t->fall_target = s16(exe->dgroup + DG_FALL);
	t->fall_step = s16(exe->dgroup + DG_FALL + 2);
	t->step_up = s16(exe->dgroup + DG_FALL + 4);
	words(exe, DG_BOB, t->bob, 32);
	words(exe, DG_SWIM_BOB, t->swim_bob, 8);
	for (i = 0; i < REGION_COUNT; i++) {
		const uint8_t *r = exe->dgroup + DG_REGIONS + 12 * i;
		t->region[i].x0 = s16(r + 4);
		t->region[i].y0 = s16(r + 6);
		t->region[i].x1 = s16(r + 8);
		t->region[i].y1 = s16(r + 10);
	}
	/* Sanity: quarter-turn sine is the maximum, the centre region is centred. */
	if (t->sine[0] != 0 || t->sine[128] != 32767 || t->near_player[4][2] != 0)
		return -1;
	return 0;
}

static int16_t mul_hi(int a, int b)
{
	int32_t p = (int32_t)(int16_t)a * (int32_t)(int16_t)b;
	/* high word of the 32-bit product = floor(p / 65536) */
	return (int16_t)(p >= 0 ? p / 65536 : -((-p + 65535) / 65536));
}

int16_t eng_step_x(const eng_tables *t, int dist, unsigned angle)
{
	return mul_hi(2 * dist, t->sine[angle & 511]);
}

int16_t eng_step_z(const eng_tables *t, int dist, unsigned angle)
{
	return mul_hi(2 * dist, t->sine[(angle & 511) + 128]);
}
