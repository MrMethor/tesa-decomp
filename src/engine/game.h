/* The game session: constants, the current level, the player and the frame
 * counter. Platform-free; driven one 3D-loop frame at a time. */
#ifndef ENGINE_GAME_H
#define ENGINE_GAME_H

#include "engine/consts.h"
#include "engine/level.h"
#include "engine/player.h"
#include "engine/scenario.h"
#include "fs/exedata.h"

/* The original's 3D loop waits two timer ticks per frame. */
#define ENG_TICK_HZ        18.2065
#define ENG_TICKS_PER_FRAME 2

typedef struct eng_game {
	arena_exe  exe;
	eng_tables t;
	eng_level  lvl;
	eng_player pl;
	unsigned   frame;
} eng_game;

/* Open the data directory (GLOBAL.BSA + loose files) and the unpacked
 * ACD.EXE. Returns 0, or -1 with a message. */
int  eng_game_open(eng_game *g, const char *data_dir, const char *exe_path, char *err, int errsz);
void eng_game_close(eng_game *g);

/* Start from save slot n (SAVEGAME.0n / SAVEENGN.0n). */
int  eng_game_load_slot(eng_game *g, int slot, char *err, int errsz);

void eng_game_frame(eng_game *g, const eng_input *in);

/* Where the view is drawn from: the original places the camera 15 units
 * behind the player (30 x sine >> 16, without the doubling of a walking
 * step; 1000:a919). */
void eng_camera(const eng_game *g, int *x, int *z);

/* Movement keys of a scenario step as engine input. */
void eng_input_from_keys(const scn_keys *k, eng_input *in);

#endif
