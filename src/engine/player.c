/* Player movement. The rules are described in docs/movement.md; section
 * names from that file are given in the comments. */
#include "engine/player.h"

#include <string.h>

#define STAND_Y 60

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

/* Character record fields (SAVEENGN block 1 offsets). */
#define REC_RACE    0x004
#define REC_SPEED   0x02D
#define REC_STATUS  0x40D
#define REC_BURDEN  0x419
/* SAVEGAME state: saved walking and turning speed. */
#define SG_WALK     0x888
#define SG_TURN     0x88A

/* Not yet modelled state of the original, kept here so the rules read like
 * the notes. */
typedef struct move_ctx {
	eng_player       *p;
	eng_level        *lvl;
	const eng_tables *t;
} move_ctx;

void eng_player_from_save(eng_player *p, const eng_tables *t, const arena_savegame *sg,
                          const arena_saveengn *se)
{
	(void)t;
	memset(p, 0, sizeof *p);
	p->x = rd16(sg->state + SG_POS_X);
	p->y = (int16_t)rd16(sg->state + SG_POS_Y);
	p->z = rd16(sg->state + SG_POS_Z);
	p->angle = rd16(sg->state + SG_ANGLE) & 511;
	p->walk_max = rd16(sg->state + SG_WALK);
	p->turn_speed = rd16(sg->state + SG_TURN);
	/* "Speeds": right after a load the first frame still walks at the base
	 * speed; the character's speed applies from the second frame. */
	p->walk_speed = 20;
	p->target_y = 0;
	p->fall_step = 4;
	if (se) {
		p->race = se->block1[REC_RACE];
		p->speed_attr = se->block1[REC_SPEED];
		p->burden = se->block1[REC_BURDEN];
		p->status = rd16(se->block1 + REC_STATUS);
	}
}

/* "Speeds" */
void eng_player_speeds(eng_player *p)
{
	unsigned bonus = ((unsigned)p->speed_attr * 20u & 0xFFFFu) >> 8;
	unsigned walk = ((bonus * (256u - p->burden)) >> 8) + 20u;
	p->walk_max = (uint16_t)walk;
	p->walk_speed = (uint16_t)walk;
	p->turn_speed = (uint16_t)((bonus >> 1) + 13u);
}

/* ---- "Collision grid" ------------------------------------------------ */

static void box_heights(const eng_level *lvl, uint16_t v, int *bottom, int *top)
{
	unsigned hi = v >> 8, set = 0;
	if (lvl->lflags & LF_UPPER) {
		set = 1;
		if (lvl->mflags & MF_EXTERIOR)
			set = 2;
	}
	*bottom = lvl->box_bottom[set][hi & 7];
	*top = lvl->box_thick[(lvl->mflags & MF_EXTERIOR) ? 1 : 0][(hi >> 3) & 15] + *bottom;
}

/* What a neighbour cell stamps into the grid: returns the stamp index or -1,
 * and the code written. */
static int cell_stamp(const eng_level *lvl, const eng_tables *t, uint16_t v, int16_t y, int *code)
{
	unsigned lo = v & 0xFF, hi = v >> 8;
	if (v == 0)
		return -1;
	if (v & 0x8000) {
		switch (v >> 12) {
		case 0x8:
			if (v & 0x0200)
				return -1;              /* creature marker */
			*code = (int)((0x80u | lo) + 1u) & 0xFF;
			return (lo < (unsigned)lvl->nflats && (lvl->flats[lo].f & 1)) ? STAMP_FLAT : -1;
		case 0xD:
			*code = (int)(0x80u | lo);
			return (v & 0x0100) ? STAMP_DIAG_B : STAMP_DIAG_A;
		case 0xA:
			*code = (int)lo;
			return (v & 0x0100) ? STAMP_SOLID : -1;
		case 0x9:
			*code = (int)lo;
			return (v & 0x0100) ? -1 : STAMP_SOLID;
		case 0xB:
			*code = 0xB0;
			return (v & 0x0F00) ? -1 : STAMP_SOLID;
		default:
			return -1;
		}
	}
	*code = (int)lo;
	if (lo == hi)
		return STAMP_SOLID;
	{
		int bottom, top, ax;
		box_heights(lvl, v, &bottom, &top);
		if (bottom > y)
			return -1;                  /* walk under it */
		ax = top;
		if (!(lvl->mflags & MF_EXTERIOR)) {
			ax += STAND_Y;
			if ((uint16_t)ax >= (uint16_t)lvl->ceiling)
				return STAMP_SOLID;     /* reaches the ceiling */
		}
		if ((int16_t)(ax - y) > t->step_up)
			return STAMP_SOLID;         /* too high to step onto */
		return -1;
	}
}

static void paint(uint8_t grid[16][16], const uint8_t stamp[16][16], int shc, int shr, int code)
{
	int r, c;
	for (r = 0; r < 16; r++) {
		int dr = shr + r;
		if (dr < 0 || dr >= 16)
			continue;
		for (c = 0; c < 16; c++) {
			int dc = c + shc;
			if (dc < 0 || dc >= 16)
				continue;
			if (stamp[r][c])
				grid[dr][dc] = (uint8_t)code;
		}
	}
}

int eng_collision_code(const eng_level *lvl, const eng_tables *t, uint16_t x, uint16_t z, int16_t y)
{
	uint8_t grid[16][16];
	int     i, cell = eng_cell_offset(x, z);
	int     in_chasm = y < STAND_Y;
	memset(grid, 0, sizeof grid);
	for (i = 0; i < 9; i++) {
		const int16_t *nb = t->near_player[i];
		int            code = 0, stamp;
		if (in_chasm) {
			/* In a chasm only other chasm cells are open. */
			uint16_t f = eng_cell_at(lvl->flor, cell + nb[2]);
			if (f >= FLOR_CHASM)
				continue;
			stamp = STAMP_SOLID;
			code = 9 - i;
		} else {
			stamp = cell_stamp(lvl, t, eng_cell_at(lvl->map1, cell + nb[2]), y, &code);
			if (stamp < 0)
				continue;
		}
		paint(grid, t->stamp[stamp], nb[0], nb[1], code);
	}
	return grid[(z & 127) >> 3][(x & 127) >> 3];
}

/* ---- "Sliding" ------------------------------------------------------- */

enum { POS_FREE, POS_BLOCKED, POS_STOP };

static int test_position(move_ctx *m, uint16_t x, uint16_t z)
{
	int code = eng_collision_code(m->lvl, m->t, x, z, m->p->y);
	/* Stairs (walking into a *LEVELUP / *LEVELDOWN wall) and climbing out
	 * of a chasm are not modelled yet: such walls just block. */
	if (code && !(code & 0x80) && m->p->y >= STAND_Y)
		return POS_BLOCKED;
	m->lvl->lflags &= (uint16_t)~LF_NOFALL;
	return code ? POS_BLOCKED : POS_FREE;
}

static void step(move_ctx *m, int dist, unsigned angle)
{
	eng_player *p = m->p;
	uint16_t    ox = p->x, oz = p->z;
	uint16_t    nx = (uint16_t)(ox + eng_step_x(m->t, dist, angle));
	uint16_t    nz = (uint16_t)(oz + eng_step_z(m->t, dist, angle));
	uint16_t    cand[3][2] = { { nx, nz }, { nx, oz }, { ox, nz } };
	int         i;
	for (i = 0; i < 3; i++) {
		int r = test_position(m, cand[i][0], cand[i][1]);
		if (r == POS_FREE) {
			p->x = cand[i][0];
			p->z = cand[i][1];
			return;
		}
		if (r == POS_STOP || (m->lvl->lflags & LF_LEAVE))
			break;
	}
	p->x = ox;
	p->z = oz;
}

/* ---- "Steering by screen region" ------------------------------------- */

static uint16_t frac16(int num, int den)
{
	uint16_t a = (uint16_t)((unsigned)num << 8);
	return den ? (uint16_t)(a / (uint16_t)den) : 0;
}

static uint16_t scale(uint16_t frac, unsigned speed)
{
	return (uint16_t)(((uint32_t)frac * (uint16_t)speed) >> 8);
}

static unsigned water_speed(const move_ctx *m, unsigned dx)
{
	if ((m->lvl->mflags & MF_SWIMMING) && !(m->lvl->mflags & MF_WATERWALK)) {
		dx >>= 2;
		if (m->p->race == 7)
			dx += dx;
	}
	return dx;
}

/* "Acceleration": one movement step with the ramp. */
static void walk(move_ctx *m, uint16_t speed, unsigned angle, int button)
{
	eng_player *p = m->p;
	if (p->status & 0x40)
		return;
	if (button) {
		if (p->accel < 0x100)
			p->accel += 0x20;
	} else {
		p->accel = p->accel >= 0x40 ? (uint16_t)(p->accel - 0x40) : 0;
	}
	step(m, (uint16_t)((uint32_t)speed * p->accel) >> 8, angle & 511);
}

static void move_forward(move_ctx *m, int cy, int button)
{
	const eng_region *r = &m->t->region[REGION_FORWARD];
	int cx = r->y1 - r->y0;
	uint16_t speed = scale(frac16(-((cy - r->y0) - cx), cx), water_speed(m, m->p->walk_speed));
	walk(m, speed, m->p->angle, button);
}

static void move_back(move_ctx *m, int cy, int button)
{
	const eng_region *r = &m->t->region[REGION_BACK];
	uint16_t speed = scale(frac16(cy - r->y0, r->y1 - r->y0),
	                       water_speed(m, m->p->walk_speed >> 1));
	walk(m, speed, m->p->angle + 256, button);
}

static void move_side_left(move_ctx *m, int cx_, int button)
{
	const eng_region *r = &m->t->region[REGION_SIDE_LEFT];
	int cx = r->x1 - r->x0;
	uint16_t speed = scale(frac16(-((cx_ - r->x0) - cx), cx), water_speed(m, m->p->walk_speed));
	walk(m, speed, m->p->angle - 128, button);
}

static void move_side_right(move_ctx *m, int cx_, int button)
{
	const eng_region *r = &m->t->region[REGION_SIDE_RIGHT];
	uint16_t speed = scale(frac16(cx_ - r->x0, r->x1 - r->x0), water_speed(m, m->p->walk_speed));
	walk(m, speed, m->p->angle + 128, button);
}

static void turn(move_ctx *m, int frac_num, int frac_den, int dir)
{
	unsigned dx = m->p->turn_speed;
	uint16_t amount;
	if ((m->lvl->mflags & MF_SWIMMING) && m->p->race != 7)
		dx >>= 1;
	amount = scale(frac16(frac_num, frac_den), dx);
	if (!(m->p->status & 0x40))
		m->p->angle = (uint16_t)((m->p->angle + dir * (int)amount) & 511);
}

static void turn_left(move_ctx *m, int cx_)
{
	const eng_region *r = &m->t->region[REGION_TURN_LEFT];
	int cx = r->x1 - r->x0;
	turn(m, -((cx_ - r->x0) - cx), cx, -1);
}

static void turn_right(move_ctx *m, int cx_)
{
	const eng_region *r = &m->t->region[REGION_TURN_RIGHT];
	turn(m, cx_ - r->x0, r->x1 - r->x0, +1);
}

/* "Keyboard": the arrow keys stand in for the mouse. Returns 1 if a
 * movement key was handled. */
static int keyboard(move_ctx *m, const eng_input *in)
{
	int handled = 0;
	if (in->keys & IN_UP) {
		move_forward(m, 0, 1);
		handled = 1;
	} else if (in->keys & IN_DOWN) {
		move_back(m, 0x90, 1);
		handled = 1;
	}
	if (in->keys & IN_LEFT) {
		if (in->keys & IN_CTRL)
			move_side_left(m, 0, 1);
		else
			turn_left(m, 0);
		handled = 1;
	} else if (in->keys & IN_RIGHT) {
		if (in->keys & IN_CTRL)
			move_side_right(m, 0x13C, 1);
		else
			turn_right(m, 0x13C);
		handled = 1;
	}
	return handled;
}

/* ---- "Height" -------------------------------------------------------- */

static void platform_target(move_ctx *m, uint16_t v)
{
	eng_player *p = m->p;
	int bottom, top, ax;
	if (p->y < STAND_Y || (v & 0x8000))
		return;
	box_heights(m->lvl, v, &bottom, &top);
	if (bottom > p->y)
		return;
	ax = top + STAND_Y;
	if (!(m->lvl->mflags & MF_EXTERIOR) && (uint16_t)ax > (uint16_t)m->lvl->ceiling)
		return;
	p->target_y = (int16_t)ax;
}

static uint16_t centre_pull(uint16_t v)
{
	v &= 127;
	if (v < 0x32)
		return (uint16_t)(v + 16);
	if (v > 0x4E)
		return (uint16_t)(v - 16);
	return 0x40;
}

static void height(move_ctx *m, unsigned frame)
{
	eng_player *p = m->p;
	eng_level  *lvl = m->lvl;
	int         cell = eng_cell_offset(p->x, p->z);
	int         liquid = 0;
	uint16_t    v = eng_cell_at(lvl->map1, cell);

	lvl->mflags &= (uint16_t)~MF_SWIMMING;
	lvl->lflags &= (uint16_t)~LF_FALLING;
	if (p->y >= STAND_Y && v != 0) {
		if ((v & 0xFF) != (v >> 8))
			platform_target(m, v);
		else
			p->target_y = STAND_Y;
	} else {
		uint16_t f = eng_cell_at(lvl->flor, cell);
		if (f < FLOR_CHASM || ((lvl->mflags & MF_LEVITATE) && p->y <= STAND_Y + 2)) {
			p->target_y = STAND_Y;
		} else if (f == FLOR_CHASM) {
			p->target_y = -80;                     /* dry chasm */
		} else {
			liquid = 1;                            /* water or lava */
			p->target_y = (lvl->mflags & MF_WATERWALK) ? -10 : -25;
			if (lvl->lflags & LF_UPPER) {
				p->target_y = -50;
				if (lvl->mflags & MF_EXTERIOR)
					p->target_y = (lvl->mflags & MF_WATERWALK) ? -1 : -10;
			}
		}
	}

	if (p->target_y < p->y) {
		int16_t ny;
		if ((lvl->lflags & LF_NOFALL) || (lvl->mflags & MF_NOFALL))
			return;
		lvl->lflags |= LF_FALLING;
		ny = (int16_t)(p->y - p->fall_step);
		if (p->target_y < ny) {
			if (frame & 1)
				p->fall_step = (int16_t)(p->fall_step << 1);
			if (p->y == ny)
				return;
			p->y = ny;
			if (ny < STAND_Y) {
				p->x = (uint16_t)((p->x & 0xFF80) + centre_pull(p->x));
				p->z = (uint16_t)((p->z & 0xFF80) + centre_pull(p->z));
			}
			return;
		}
	}
	if (p->y != p->target_y) {
		p->y = p->target_y;
		p->fall_step = 4;
	}
	if (liquid)
		lvl->mflags |= MF_SWIMMING;
}

/* ---- One frame --------------------------------------------------------- */

void eng_player_frame(eng_player *p, eng_level *lvl, const eng_tables *t, const eng_input *in,
                      unsigned frame)
{
	move_ctx m;
	m.p = p;
	m.lvl = lvl;
	m.t = t;
	/* The keyboard is not read while falling; the steering flag then keeps
	 * its value from the last frame that read it. */
	if (!(lvl->lflags & LF_FALLING))
		p->steering = (uint8_t)keyboard(&m, in);
	height(&m, frame);
	/* End of the frame: the ramp runs down while nothing steers. */
	if (!p->steering)
		p->accel = p->accel >= 0x40 ? (uint16_t)(p->accel - 0x40) : 0;
	eng_player_speeds(p);
}
