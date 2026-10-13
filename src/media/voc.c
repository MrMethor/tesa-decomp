/* Creative Voice files. See voc.h. */
#include "voc.h"

#include <stdlib.h>
#include <string.h>

#include "gfx/img.h"   /* afile_load_all */

static const char kSig[] = "Creative Voice File\x1A";

static int fail(const char **msg, const char *why)
{
	if (msg)
		*msg = why;
	return -1;
}

static int append(arena_voc *v, const uint8_t *p, uint32_t n, uint8_t fill, int is_fill)
{
	uint8_t *s = realloc(v->samples, (size_t)v->count + n + 1);
	if (!s)
		return -1;
	v->samples = s;
	if (is_fill)
		memset(s + v->count, fill, n);
	else
		memcpy(s + v->count, p, n);
	v->count += n;
	return 0;
}

static void set_rate(arena_voc *v, uint32_t rate)
{
	if (!v->rate)
		v->rate = rate;
	else if (v->rate != rate)
		v->rate_changes++;
}

int voc_parse(const uint8_t *buf, uint32_t len, arena_voc *out, const char **msg)
{
	uint32_t p, version, check;

	memset(out, 0, sizeof *out);
	if (len < 26 || memcmp(buf, kSig, 20) != 0)
		return fail(msg, "not a Creative Voice file");
	p = (uint32_t)(buf[20] | (buf[21] << 8));
	version = (uint32_t)(buf[22] | (buf[23] << 8));
	check = (uint32_t)(buf[24] | (buf[25] << 8));
	if (((~version + 0x1234) & 0xFFFF) != check)
		return fail(msg, "header check word does not match the version");

	while (p < len) {
		uint8_t  type = buf[p];
		uint32_t size;
		const uint8_t *d;

		if (type == 0)
			return 0;
		if (p + 4 > len)
			return fail(msg, "truncated block header");
		size = (uint32_t)(buf[p + 1] | (buf[p + 2] << 8) | (buf[p + 3] << 16));
		d = buf + p + 4;
		if (p + 4 + size > len)
			return fail(msg, "block runs past the end of the file");
		out->blocks++;
		switch (type) {
		case 1:
			if (size < 2)
				return fail(msg, "short sound block");
			set_rate(out, 1000000u / (256u - d[0]));
			if (d[1] != 0)
				out->other_codec++;
			else if (append(out, d + 2, size - 2, 0, 0))
				return fail(msg, "out of memory");
			break;
		case 2:
			if (append(out, d, size, 0, 0))
				return fail(msg, "out of memory");
			break;
		case 3:
			if (size < 3)
				return fail(msg, "short silence block");
			if (append(out, NULL, (uint32_t)(d[0] | (d[1] << 8)) + 1u, 0x80, 1))
				return fail(msg, "out of memory");
			break;
		case 8:
			if (size < 4)
				return fail(msg, "short extended block");
			set_rate(out, 256000000u / (65536u - (uint32_t)(d[0] | (d[1] << 8))));
			break;
		case 9:
			if (size < 12)
				return fail(msg, "short type-9 block");
			set_rate(out, (uint32_t)(d[0] | (d[1] << 8) | (d[2] << 16) | ((uint32_t)d[3] << 24)));
			if (d[4] != 8 || d[5] != 1 || d[6] != 0 || d[7] != 0)
				out->other_codec++;
			else if (append(out, d + 12, size - 12, 0, 0))
				return fail(msg, "out of memory");
			break;
		default:   /* markers, text, repeats: no samples */
			break;
		}
		p += 4 + size;
	}
	return fail(msg, "no terminator block");
}

int voc_load(const char *name, arena_voc *out, const char **msg)
{
	uint32_t len;
	uint8_t *buf = afile_load_all(name, &len);
	int      rc;
	if (!buf)
		return fail(msg, "file not found");
	rc = voc_parse(buf, len, out, msg);
	free(buf);
	if (rc)
		voc_free(out);
	return rc;
}

void voc_free(arena_voc *voc)
{
	free(voc->samples);
	memset(voc, 0, sizeof *voc);
}
