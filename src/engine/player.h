/* Player movement: turning, walking, collision and height.
 *
 * Written from the behaviour notes in docs/movement.md. One call of
 * eng_player_frame() is one frame of the original's 3D loop.
 */
#ifndef ENGINE_PLAYER_H
#define ENGINE_PLAYER_H

#include <stdint.h>

#include "engine/consts.h"
#include "engine/level.h"

/* Keys that steer movement. */
#define IN_UP     0x0001
#define IN_DOWN   0x0002
#define IN_LEFT   0x0004
#define IN_RIGHT  0x0008
#define IN_CTRL   0x0010
#define IN_SHIFT  0x0020

typedef struct eng_input {
	unsigned keys;           /* IN_* bits held this frame */
} eng_input;

typedef struct eng_player {
	uint16_t x, z;           /* 1/128 cell units */
	int16_t  y;              /* height; 60 = standing on the floor */
	uint16_t angle;          /* 0..511; x grows with sine, z with cosine */
	uint16_t accel;          /* movement ramp 0..0x100 */
	uint16_t walk_speed;     /* speed used by this frame's steps */
	uint16_t walk_max;       /* recomputed every frame */
	uint16_t turn_speed;
	int16_t  target_y;       /* height the vertical step moves towards */
	int16_t  fall_step;      /* current falling speed */
	uint8_t  steering;       /* a movement key was handled in the last input step */
	/* From the character record. */
	uint8_t  speed_attr;     /* raw 0..255 */
	uint8_t  burden;         /* 0..255, slows walking */
	uint8_t  race;
	uint16_t status;         /* bit 6: cannot move or turn */
} eng_player;

/* Set position, facing and character values from the save files. */
void eng_player_from_save(eng_player *p, const eng_tables *t, const arena_savegame *sg,
                          const arena_saveengn *se);

/* Walking and turning speed from the character (the original recomputes
 * them once per frame, after the movement step). */
void eng_player_speeds(eng_player *p);

/* One 3D-loop frame of player movement. `frame` is the frame counter; its
 * parity times the falling speed-up. */
void eng_player_frame(eng_player *p, eng_level *lvl, const eng_tables *t, const eng_input *in,
                      unsigned frame);

/* Collision test used by the movement step: the grid code at (x, z) for the
 * player at height y; 0 = free. Exposed for tests. */
int eng_collision_code(const eng_level *lvl, const eng_tables *t, uint16_t x, uint16_t z, int16_t y);

#endif
