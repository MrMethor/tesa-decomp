/* Game session. See game.h. */
#include "engine/game.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fs/afile.h"

int eng_game_open(eng_game *g, const char *data_dir, const char *exe_path, char *err, int errsz)
{
	memset(g, 0, sizeof *g);
	if (bsa_open(data_dir) != 0) {
		snprintf(err, (size_t)errsz, "cannot open GLOBAL.BSA in %s", data_dir);
		return -1;
	}
	if (exe_load(exe_path, &g->exe) != 0) {
		snprintf(err, (size_t)errsz, "cannot read %s (the unpacked ACD.EXE)", exe_path);
		bsa_close();
		return -1;
	}
	if (eng_tables_load(&g->exe, &g->t) != 0) {
		snprintf(err, (size_t)errsz, "%s is not the expected ACD.EXE", exe_path);
		exe_free(&g->exe);
		bsa_close();
		return -1;
	}
	return 0;
}

void eng_game_close(eng_game *g)
{
	exe_free(&g->exe);
	bsa_close();
}

int eng_game_load_slot(eng_game *g, int slot, char *err, int errsz)
{
	arena_savegame *sg = malloc(sizeof *sg);
	arena_saveengn *se = malloc(sizeof *se);
	arena_inf       inf;
	char            name[16], inf_name[16];
	int             have_inf, have_se;

	if (!sg || !se) {
		free(sg);
		free(se);
		snprintf(err, (size_t)errsz, "out of memory");
		return -1;
	}
	snprintf(name, sizeof name, "SAVEGAME.%02d", slot);
	if (savegame_load(name, sg) != 0) {
		snprintf(err, (size_t)errsz, "cannot load %s", name);
		free(sg);
		free(se);
		return -1;
	}
	snprintf(name, sizeof name, "SAVEENGN.%02d", slot);
	have_se = saveengn_load(name, se) == 0;
	eng_save_inf_name(sg, inf_name, sizeof inf_name);
	have_inf = inf_load(inf_name, &inf) == 0;
	eng_level_from_save(&g->lvl, sg, have_inf ? &inf : NULL);
	eng_player_from_save(&g->pl, &g->t, sg, have_se ? se : NULL);
	if (have_inf)
		inf_free(&inf);
	free(sg);
	free(se);
	g->frame = 0;
	if (!have_inf) {
		snprintf(err, (size_t)errsz, "cannot load %s", inf_name);
		return -1;
	}
	return 0;
}

void eng_game_frame(eng_game *g, const eng_input *in)
{
	eng_player_frame(&g->pl, &g->lvl, &g->t, in, g->frame);
	g->frame++;
}

void eng_input_from_keys(const scn_keys *k, eng_input *in)
{
	in->keys = 0;
	if (scn_key(k, 0x48)) in->keys |= IN_UP;
	if (scn_key(k, 0x50)) in->keys |= IN_DOWN;
	if (scn_key(k, 0x4B)) in->keys |= IN_LEFT;
	if (scn_key(k, 0x4D)) in->keys |= IN_RIGHT;
	if (scn_key(k, 0x1D)) in->keys |= IN_CTRL;
	if (scn_key(k, 0x2A) || scn_key(k, 0x36)) in->keys |= IN_SHIFT;
}
