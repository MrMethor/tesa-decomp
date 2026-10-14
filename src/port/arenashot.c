/* arenashot: render the view of the port after a number of scenario frames
 * to a PNG, for comparison with frames captured from the original.
 *
 *   arenashot <data dir> <ACD_unpacked.EXE> <scenario.scn> <frame> <out.png>
 *             [focal eye shade_step]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/game.h"
#include "render/view.h"
#include "tools/png.h"

int main(int argc, char **argv)
{
	static scenario scn;
	static uint8_t  screen[SCREEN_W * SCREEN_H];
	eng_game     *g;
	view_textures vt;
	view_params   vp;
	arena_inf     inf;
	arena_lgt     lgt;
	arena_palette pal;
	uint8_t       rgb[768];
	char          err[256];
	int           s, frame = 0, target;
	int           have_lgt;

	if (argc != 6 && argc != 9) {
		fprintf(stderr, "usage: arenashot <data dir> <ACD_unpacked.EXE> <scenario.scn> <frame> <out.png> "
		                "[focal eye shade_step]\n");
		return 2;
	}
	target = atoi(argv[4]);
	if (scenario_load(argv[3], &scn, err, sizeof err) != 0) {
		fprintf(stderr, "%s\n", err);
		return 1;
	}
	g = malloc(sizeof *g);
	if (!g || eng_game_open(g, argv[1], argv[2], err, sizeof err) != 0 ||
	    eng_game_load_slot(g, scn.slot, err, sizeof err) != 0) {
		fprintf(stderr, "%s\n", err);
		return 1;
	}
	for (s = 0; s < scn.nsteps && frame < target; s++) {
		eng_input in;
		int       k;
		if (scn.steps[s].check)
			continue;
		eng_input_from_keys(&scn.steps[s].keys, &in);
		for (k = 0; k < scn.steps[s].frames && frame < target; k++, frame++)
			eng_game_frame(g, &in);
	}
	if (inf_load(g->lvl.inf_name, &inf) != 0) {
		fprintf(stderr, "cannot load %s\n", g->lvl.inf_name);
		return 1;
	}
	view_textures_load(&vt, &inf);
	inf_free(&inf);
	have_lgt = lgt_load("NORMAL.LGT", &lgt) == 0;
	if (col_load("PAL.COL", &pal) != 0) {
		fprintf(stderr, "cannot load PAL.COL\n");
		return 1;
	}
	palette_to_rgb8(&pal, rgb);
	view_default_params(&vp);
	if (argc == 9) {
		vp.focal = atof(argv[6]);
		vp.eye = atof(argv[7]);
		vp.shade_step = atof(argv[8]);
	}
	{
		int cx, cz;
		eng_camera(g, &cx, &cz);
		view_render(screen, &g->lvl, &vt, have_lgt ? &lgt : NULL, cx, cz, g->pl.angle, &vp);
	}
	if (png_write_indexed(argv[5], screen, SCREEN_W, SCREEN_H, rgb) != 0) {
		fprintf(stderr, "cannot write %s\n", argv[5]);
		return 1;
	}
	printf("frame %d: x=%u z=%u angle=%u -> %s\n", frame, g->pl.x, g->pl.z, g->pl.angle, argv[5]);
	view_textures_free(&vt);
	eng_game_close(g);
	free(g);
	return 0;
}
