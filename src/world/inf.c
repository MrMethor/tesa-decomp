/* .INF level descriptions. See inf.h. */
#include "inf.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "gfx/img.h"   /* afile_load_all */

const char *inf_section_name(inf_section s)
{
	static const char *const names[] = { "-", "WALLS", "FLOORS", "FLATS", "SOUND", "TEXT", "?" };
	return names[s];
}

static inf_section section_from_line(const char *s)
{
	if (strlen(s) < 4)
		return INF_UNKNOWN;
	switch (toupper((unsigned char)s[3])) {
	case 'L': return INF_WALLS;
	case 'O': return INF_FLOORS;
	case 'A': return INF_FLATS;
	case 'U': return INF_SOUND;
	case 'X': return INF_TEXT;
	default:  return INF_UNKNOWN;
	}
}

static char *trim(char *s)
{
	char *e;
	while (*s == ' ' || *s == '\t')
		s++;
	e = s + strlen(s);
	while (e > s && (e[-1] == ' ' || e[-1] == '\t'))
		*--e = 0;
	return s;
}

/* "NAME.EXT" at the start of s (letters, digits, '_', a dot, 3 letters). */
static int file_token_len(const char *s)
{
	int i = 0, dot = -1;
	while (isalnum((unsigned char)s[i]) || s[i] == '_' || s[i] == '.') {
		if (s[i] == '.')
			dot = i;
		i++;
	}
	return (dot > 0 && i - dot == 4) ? i : 0;
}

static void parse_numbers(const char *s, inf_entry *e)
{
	while (*s && e->nargs < INF_MAX_ARGS) {
		char *end;
		long  v;
		while (*s && !isdigit((unsigned char)*s) && *s != '-')
			s++;
		if (!*s)
			break;
		v = strtol(s, &end, 10);
		if (end == s) {
			s++;
			continue;
		}
		e->args[e->nargs++] = (int)v;
		s = end;
	}
}

int inf_parse(const uint8_t *buf, uint32_t len, arena_inf *out)
{
	char       *p, *line;
	int         cap = 64, lineno = 0, pending = 0;
	inf_section sec = INF_NONE;

	memset(out, 0, sizeof *out);
	out->text = malloc(len + 1);
	out->entries = malloc((size_t)cap * sizeof *out->entries);
	if (!out->text || !out->entries) {
		inf_free(out);
		return -1;
	}
	memcpy(out->text, buf, len);
	out->text[len] = 0;

	for (p = out->text; p && *p; ) {
		char      *nl = strpbrk(p, "\r\n");
		inf_entry *e;
		char      *s;
		int        flen;

		line = p;
		if (nl) {
			if (nl[0] == '\r' && nl[1] == '\n')
				*nl++ = 0;
			*nl = 0;
			p = nl + 1;
		} else {
			p = NULL;
		}
		lineno++;
		s = trim(line);
		if (!*s)
			continue;
		if (*s == '@') {
			sec = section_from_line(s);
			pending = out->count;   /* directives before a section end own nothing */
			continue;
		}
		if (out->count == cap) {
			inf_entry *n = realloc(out->entries, (size_t)cap * 2 * sizeof *n);
			if (!n) {
				inf_free(out);
				return -1;
			}
			out->entries = n;
			cap *= 2;
		}
		e = &out->entries[out->count];
		memset(e, 0, sizeof *e);
		e->section = sec;
		e->line = lineno;
		e->owner = -1;

		if (*s == '*' && isalpha((unsigned char)s[1])) {
			int i = 0;
			e->kind = INF_DIRECTIVE;
			s++;
			while (isalpha((unsigned char)*s) && i < (int)sizeof e->name - 1)
				e->name[i++] = (char)toupper((unsigned char)*s++);
			parse_numbers(s, e);
		} else if ((flen = file_token_len(s + (*s == '-'))) > 0 && flen < (int)sizeof e->name) {
			const char *rest;
			e->kind = INF_FILE;
			e->minus = (*s == '-');
			s += e->minus;
			memcpy(e->name, s, (size_t)flen);
			e->name[flen] = 0;
			rest = s + flen;
			while (*rest == ' ' || *rest == '\t')
				rest++;
			e->rest = rest;
			if (*rest == '#')
				e->set_count = atoi(rest + 1);
			else if (sec == INF_SOUND)
				parse_numbers(rest, e);
			/* A directive describes the next file entry: the game stores its
			 * counters when the directive line is read, before the file. */
			for (; pending < out->count; pending++)
				if (out->entries[pending].kind == INF_DIRECTIVE)
					out->entries[pending].owner = out->count;
			pending = out->count + 1;
		} else {
			e->kind = INF_LINE;
			e->rest = s;
		}
		out->count++;
	}
	return 0;
}

int inf_load(const char *name, arena_inf *out)
{
	uint32_t len;
	uint8_t *buf = afile_load_all(name, &len);
	int      rc;
	if (!buf)
		return -1;
	rc = inf_parse(buf, len, out);
	free(buf);
	return rc;
}

void inf_free(arena_inf *inf)
{
	free(inf->entries);
	free(inf->text);
	memset(inf, 0, sizeof *inf);
}
