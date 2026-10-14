/* Behaviour scenarios: the input scripts shared with tools/oracle.py (which
 * plays them in the original game). Format: tools/scenario.py. */
#ifndef ENGINE_SCENARIO_H
#define ENGINE_SCENARIO_H

#include <stdint.h>

#define SCN_MAX_STEPS 512
#define SCN_LABEL     32

typedef struct scn_keys {
	uint8_t down[16];          /* bit per scancode 0..127 */
} scn_keys;

typedef struct scn_step {
	int      check;            /* 1: label the state here; 0: hold keys */
	scn_keys keys;
	int      frames;
	char     label[SCN_LABEL];
} scn_step;

typedef struct scenario {
	int      slot;
	int      nsteps;
	scn_step steps[SCN_MAX_STEPS];
} scenario;

/* 0 on success; on failure -1 with a message in err. */
int scenario_load(const char *path, scenario *out, char *err, int errsz);

static inline int scn_key(const scn_keys *k, int scancode)
{
	return (k->down[(scancode >> 3) & 15] >> (scancode & 7)) & 1;
}

#endif
