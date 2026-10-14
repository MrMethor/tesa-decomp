/* Scenario files. See scenario.h and tools/scenario.py. */
#include "engine/scenario.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const struct { const char *name; int code; } kKeys[] = {
	{ "esc", 0x01 }, { "tab", 0x0F }, { "enter", 0x1C }, { "ctrl", 0x1D },
	{ "lshift", 0x2A }, { "rshift", 0x36 }, { "alt", 0x38 }, { "space", 0x39 },
	{ "up", 0x48 }, { "left", 0x4B }, { "right", 0x4D }, { "down", 0x50 },
};

static int key_code(const char *name)
{
	static const char *rows[] = { "1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm" };
	static const int   base[] = { 0x02, 0x10, 0x1E, 0x2C };
	size_t i;
	for (i = 0; i < sizeof kKeys / sizeof kKeys[0]; i++)
		if (!strcmp(name, kKeys[i].name))
			return kKeys[i].code;
	if (name[0] && !name[1]) {
		for (i = 0; i < 4; i++) {
			const char *p = strchr(rows[i], name[0]);
			if (p)
				return base[i] + (int)(p - rows[i]);
		}
	}
	if (name[0] == 'f' && isdigit((unsigned char)name[1])) {
		int n = atoi(name + 1);
		if (n >= 1 && n <= 10)
			return 0x3B + n - 1;
	}
	return -1;
}

static int parse_keys(char *text, scn_keys *k)
{
	char *tok;
	memset(k, 0, sizeof *k);
	for (tok = strtok(text, "+"); tok; tok = strtok(NULL, "+")) {
		char *c;
		int   code;
		for (c = tok; *c; c++)
			*c = (char)tolower((unsigned char)*c);
		code = key_code(tok);
		if (code < 0)
			return -1;
		k->down[code >> 3] |= (uint8_t)(1 << (code & 7));
	}
	return 0;
}

int scenario_load(const char *path, scenario *out, char *err, int errsz)
{
	FILE *f = fopen(path, "r");
	char  line[256];
	int   n = 0;
	memset(out, 0, sizeof *out);
	out->slot = -1;
	if (!f) {
		snprintf(err, (size_t)errsz, "cannot open %s", path);
		return -1;
	}
	while (fgets(line, sizeof line, f)) {
		char *hash = strchr(line, '#'), *cmd, *a1, *a2;
		scn_step *st;
		n++;
		if (hash)
			*hash = 0;
		cmd = strtok(line, " \t\r\n");
		if (!cmd)
			continue;
		a1 = strtok(NULL, " \t\r\n");
		a2 = strtok(NULL, " \t\r\n");
		if (!strcmp(cmd, "end"))
			break;
		if (!strcmp(cmd, "save") && a1) {
			out->slot = atoi(a1);
			continue;
		}
		if (out->nsteps >= SCN_MAX_STEPS) {
			snprintf(err, (size_t)errsz, "%s:%d: too many steps", path, n);
			fclose(f);
			return -1;
		}
		st = &out->steps[out->nsteps];
		if (!strcmp(cmd, "hold") && a1 && a2) {
			if (parse_keys(a1, &st->keys) != 0) {
				snprintf(err, (size_t)errsz, "%s:%d: unknown key", path, n);
				fclose(f);
				return -1;
			}
			st->frames = atoi(a2);
		} else if (!strcmp(cmd, "wait") && a1) {
			st->frames = atoi(a1);
		} else if (!strcmp(cmd, "check") && a1) {
			st->check = 1;
			snprintf(st->label, sizeof st->label, "%s", a1);
		} else {
			snprintf(err, (size_t)errsz, "%s:%d: bad line", path, n);
			fclose(f);
			return -1;
		}
		out->nsteps++;
	}
	fclose(f);
	if (out->slot < 0) {
		snprintf(err, (size_t)errsz, "%s: no 'save' line", path);
		return -1;
	}
	return 0;
}
