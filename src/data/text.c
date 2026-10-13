/* Text data files. See text.h. */
#include "text.h"

#include <stdlib.h>
#include <string.h>

#include "gfx/img.h"   /* afile_load_all */

int strlist_load(const char *name, arena_strlist *out)
{
	uint32_t size, i, start = 0;
	uint8_t *buf = afile_load_all(name, &size);
	int      n = 0, k = 0;

	memset(out, 0, sizeof *out);
	if (!buf)
		return -1;
	for (i = 0; i < size; i++)
		if (!buf[i])
			n++;
	out->data = realloc(buf, size + 1);
	if (!out->data) {
		free(buf);
		return -1;
	}
	out->data[size] = 0;
	out->size = size;
	out->items = malloc((size_t)(n ? n : 1) * sizeof *out->items);
	if (!out->items) {
		strlist_free(out);
		return -1;
	}
	for (i = 0; i < size; i++) {
		if (!out->data[i]) {
			out->items[k++] = out->data + start;
			start = i + 1;
		}
	}
	out->count = n;
	out->trailing_garbage = (int)(size - start);
	return 0;
}

void strlist_free(arena_strlist *sl)
{
	free(sl->items);
	free(sl->data);
	memset(sl, 0, sizeof *sl);
}

static int is_header(const char *s, char marker)
{
	return s[0] == marker && (marker != '[' || strchr(s, ']'));
}

int textdb_load(const char *name, char marker, arena_textdb *out)
{
	uint32_t size;
	uint8_t *buf = afile_load_all(name, &size);
	char    *p, *line;
	int      cap = 64, lineno = 0;

	memset(out, 0, sizeof *out);
	if (!buf)
		return -1;
	out->data = realloc(buf, size + 1);
	if (!out->data) {
		free(buf);
		return -1;
	}
	out->data[size] = 0;
	out->records = malloc((size_t)cap * sizeof *out->records);
	if (!out->records) {
		textdb_free(out);
		return -1;
	}

	/* Split into lines in place; headers become keys, bodies keep their
	 * newlines up to the next header (the terminator overwrites the '\r' or
	 * '\n' just before it). */
	for (p = out->data; *p; ) {
		char *nl = strpbrk(p, "\r\n"), *next;
		line = p;
		next = nl ? nl + ((nl[0] == '\r' && nl[1] == '\n') ? 2 : 1) : p + strlen(p);
		lineno++;
		if (is_header(line, marker)) {
			text_record *r;
			if (out->count == cap) {
				text_record *nr = realloc(out->records, (size_t)cap * 2 * sizeof *nr);
				if (!nr) {
					textdb_free(out);
					return -1;
				}
				out->records = nr;
				cap *= 2;
			}
			if (line > out->data)
				line[-1] = 0;   /* end the previous body */
			if (nl)
				*nl = 0;
			r = &out->records[out->count++];
			r->key = line + 1;
			if (marker == '[') {
				char *close = strchr(line, ']');
				if (close)
					*close = 0;
			}
			r->body = next;
			r->lines = 0;
			r->line = lineno;
		} else if (out->count) {
			if (*line && *line != '\r' && *line != '\n')
				out->records[out->count - 1].lines++;
		} else if (*line && *line != '\r' && *line != '\n') {
			out->lines_before_first++;
		}
		p = next;
	}
	return 0;
}

void textdb_free(arena_textdb *db)
{
	free(db->records);
	free(db->data);
	memset(db, 0, sizeof *db);
}

const text_record *textdb_find(const arena_textdb *db, const char *key)
{
	int i;
	for (i = 0; i < db->count; i++)
		if (!strcmp(db->records[i].key, key))
			return &db->records[i];
	return NULL;
}

int tplindex_load(const char *name, uint32_t template_size, arena_tplindex *out)
{
	uint32_t len;
	uint8_t *buf = afile_load_all(name, &len);
	int      i;

	memset(out, 0, sizeof *out);
	if (!buf)
		return -1;
	if (len < 2 * TPL_KEYS + 4 * TPL_OFFSETS) {
		free(buf);
		return -1;
	}
	for (i = 0; i < TPL_KEYS; i++)
		out->key[i] = (uint16_t)(buf[2 * i] | (buf[2 * i + 1] << 8));
	for (i = 0; i < TPL_OFFSETS; i++) {
		const uint8_t *p = buf + 2 * TPL_KEYS + 4 * i;
		out->offset[i] = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
		                 ((uint32_t)p[3] << 24);
	}
	while (out->used < TPL_OFFSETS && out->offset[out->used] > 0 &&
	       out->offset[out->used] < template_size)
		out->used++;
	free(buf);
	return 0;
}

uint16_t tpl_key(unsigned number, char letter)
{
	if (!letter)
		return (uint16_t)number;
	return (uint16_t)(0x8000u | ((unsigned)(letter | 0x20) - 'a') << 8 | number);
}

int tpl_find(const arena_tplindex *ix, uint16_t key, int nth)
{
	int i;
	for (i = 0; i < ix->used; i++)
		if (ix->key[i] == key && nth-- == 0)
			return i;
	return -1;
}

int text_split_alternatives(char *text, char **parts, int max)
{
	int n = 0;
	if (max <= 0)
		return 0;
	parts[n++] = text;
	for (; *text; text++) {
		if (*text == '&') {
			*text = 0;
			if (n < max)
				parts[n++] = text + 1;
		}
	}
	return n;
}

int quiz_load(const char *name, arena_quiz *out)
{
	uint32_t size, pos = 0;
	uint8_t *buf = afile_load_all(name, &size);
	int      expect = 1, answers = QUIZ_ANSWERS, ok = 1;

	memset(out, 0, sizeof *out);
	if (!buf)
		return -1;
	out->size = size;
	while (pos < size) {
		uint32_t start = pos, n = 0;
		uint8_t  c = buf[pos];
		while (pos < size && buf[pos] != '\n')
			pos++;
		pos++;   /* past '\n' */
		if (c >= '0' && c <= '9') {
			uint32_t i = start;
			while (i < size && buf[i] >= '0' && buf[i] <= '9')
				n = n * 10 + (buf[i++] - '0');
			if (answers != QUIZ_ANSWERS || (int)n != expect || i >= size || buf[i] != '.' ||
			    out->count == QUIZ_MAX) {
				ok = 0;
				break;
			}
			out->q[out->count++].question = start;
			expect++;
			answers = 0;
		} else if (c >= 'a' && c <= 'c' && start + 1 < size && buf[start + 1] == ')') {
			if (out->count == 0 || answers >= QUIZ_ANSWERS || c != 'a' + answers) {
				ok = 0;
				break;
			}
			out->q[out->count - 1].answer[answers++] = start;
		}
		/* anything else: continuation (indented) or empty line */
	}
	if (answers != QUIZ_ANSWERS)
		ok = 0;
	free(buf);
	return ok ? 0 : -1;
}
