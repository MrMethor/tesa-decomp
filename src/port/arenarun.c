/* arenarun: play a behaviour scenario in the port, headless, and write the
 * state at every frame in the format of tools/oracle.py, so the two can be
 * compared with tools/statecmp.py.
 *
 *   arenarun <data dir> <ACD_unpacked.EXE> <scenario.scn> <out.state>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/game.h"

static void write_state(FILE *f, int index, const char *label, const eng_game *g)
{
	const eng_player *p = &g->pl;
	fprintf(f, "%d %s x=%u y=%d z=%u angle=%u level=%d frame=%u lflags=%u mflags=%u "
	        "d_accel=%u d_speed=%u\n",
	        index, label && *label ? label : "-", p->x, p->y, p->z, p->angle, g->lvl.index,
	        g->frame, g->lvl.lflags, g->lvl.mflags, p->accel, p->walk_speed);
}

int main(int argc, char **argv)
{
	static scenario scn;
	eng_game *g;
	char      err[256];
	FILE     *out;
	int       s, index = 0;
	const char *label = NULL;

	if (argc != 5) {
		fprintf(stderr, "usage: arenarun <data dir> <ACD_unpacked.EXE> <scenario.scn> <out.state>\n");
		return 2;
	}
	if (scenario_load(argv[3], &scn, err, sizeof err) != 0) {
		fprintf(stderr, "%s\n", err);
		return 1;
	}
	g = malloc(sizeof *g);
	if (!g || eng_game_open(g, argv[1], argv[2], err, sizeof err) != 0) {
		fprintf(stderr, "%s\n", g ? err : "out of memory");
		return 1;
	}
	if (eng_game_load_slot(g, scn.slot, err, sizeof err) != 0) {
		fprintf(stderr, "%s\n", err);
		eng_game_close(g);
		return 1;
	}
	out = fopen(argv[4], "w");
	if (!out) {
		fprintf(stderr, "cannot write %s\n", argv[4]);
		eng_game_close(g);
		return 1;
	}
	fprintf(out, "# port: scenario %s\n", argv[3]);
	for (s = 0; s < scn.nsteps; s++) {
		const scn_step *st = &scn.steps[s];
		eng_input       in;
		int             k;
		if (st->check) {
			label = st->label;
			continue;
		}
		eng_input_from_keys(&st->keys, &in);
		for (k = 0; k < st->frames; k++) {
			write_state(out, index++, label, g);
			label = NULL;
			eng_game_frame(g, &in);
		}
	}
	write_state(out, index, label, g);
	fclose(out);
	eng_game_close(g);
	free(g);
	printf("wrote %s (%d frames)\n", argv[4], index);
	return 0;
}
