/* XMIDI -> standard MIDI. See xmi.h. */
#include "xmi.h"

#include <stdlib.h>
#include <string.h>

static uint32_t be32(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static int fail(const char **msg, const char *why)
{
	if (msg)
		*msg = why;
	return -1;
}

/* ---- event list ---------------------------------------------------------- */

typedef struct ev {
	uint32_t       time;
	uint32_t       order;    /* stable sort key */
	uint8_t        status;
	uint8_t        d[2];
	int            nd;
	const uint8_t *ext;      /* sysex / meta payload */
	uint32_t       ext_len;
	uint8_t        meta_type;
} ev;

typedef struct evlist {
	ev      *e;
	uint32_t n, cap;
} evlist;

static ev *ev_add(evlist *l)
{
	if (l->n == l->cap) {
		uint32_t cap = l->cap ? l->cap * 2 : 256;
		ev *ne = realloc(l->e, cap * sizeof *ne);
		if (!ne)
			return NULL;
		l->e = ne;
		l->cap = cap;
	}
	memset(&l->e[l->n], 0, sizeof l->e[0]);
	l->e[l->n].order = l->n;
	return &l->e[l->n++];
}

static int ev_cmp(const void *a, const void *b)
{
	const ev *x = a, *y = b;
	if (x->time != y->time)
		return x->time < y->time ? -1 : 1;
	return x->order < y->order ? -1 : (x->order > y->order);
}

static int read_varlen(const uint8_t **p, const uint8_t *end, uint32_t *v)
{
	uint32_t r = 0;
	int      i;
	for (i = 0; i < 4; i++) {
		if (*p >= end)
			return -1;
		r = (r << 7) | (**p & 0x7Fu);
		if (!(*(*p)++ & 0x80))
			break;
	}
	*v = r;
	return 0;
}

/* Decode one EVNT chunk into absolute-time events (note-ons plus synthetic
 * note-offs). Returns 0 and sets *clean if end-of-track was the last thing
 * in the chunk. */
static int parse_evnt(const uint8_t *p, const uint8_t *end, evlist *out, xmi_stats *st,
                      int *clean)
{
	uint32_t time = 0;
	*clean = 0;
	while (p < end) {
		ev     *e;
		uint8_t s;
		while (p < end && *p < 0x80)
			time += *p++;
		if (p >= end)
			break;
		s = *p++;
		if (!(e = ev_add(out)))
			return -1;
		e->time = time;
		e->status = s;
		st->events++;
		switch (s & 0xF0) {
		case 0x90: {
			uint32_t dur;
			ev      *off;
			if (p + 2 > end)
				return -1;
			e->d[0] = p[0];
			e->d[1] = p[1];
			e->nd = 2;
			p += 2;
			if (read_varlen(&p, end, &dur))
				return -1;
			if (!(off = ev_add(out)))
				return -1;
			off->time = time + dur;
			off->status = (uint8_t)(0x80 | (s & 0x0F));
			off->d[0] = out->e[out->n - 2].d[0];
			off->d[1] = 0x40;
			off->nd = 2;
			st->notes++;
			if (time + dur > st->ticks)
				st->ticks = time + dur;
			break;
		}
		case 0x80: case 0xA0: case 0xB0: case 0xE0:
			if (p + 2 > end)
				return -1;
			e->d[0] = p[0];
			e->d[1] = p[1];
			e->nd = 2;
			p += 2;
			break;
		case 0xC0: case 0xD0:
			if (p + 1 > end)
				return -1;
			e->d[0] = p[0];
			e->nd = 1;
			p += 1;
			break;
		default:
			if (s == 0xF0 || s == 0xF7) {
				uint32_t n;
				if (read_varlen(&p, end, &n) || p + n > end)
					return -1;
				e->ext = p;
				e->ext_len = n;
				p += n;
			} else if (s == 0xFF) {
				uint32_t n;
				if (p >= end)
					return -1;
				e->meta_type = *p++;
				if (read_varlen(&p, end, &n) || p + n > end)
					return -1;
				e->ext = p;
				e->ext_len = n;
				p += n;
				if (e->meta_type == 0x2F) {
					if (time > st->ticks)
						st->ticks = time;
					/* 38 of the game's 66 files have one 0x00 pad byte after the
					 * end of track inside the chunk; anything else is unexpected. */
					*clean = 1;
					while (p < end)
						if (*p++ != 0)
							*clean = 0;
					return 0;
				}
			} else {
				return -1;
			}
		}
	}
	return 0;
}

/* ---- SMF writer -------------------------------------------------------- */

typedef struct buf {
	uint8_t *p;
	uint32_t n, cap;
} buf;

static int put(buf *b, const void *d, uint32_t n)
{
	if (b->n + n > b->cap) {
		uint32_t cap = b->cap ? b->cap : 4096;
		uint8_t *np;
		while (cap < b->n + n)
			cap *= 2;
		np = realloc(b->p, cap);
		if (!np)
			return -1;
		b->p = np;
		b->cap = cap;
	}
	memcpy(b->p + b->n, d, n);
	b->n += n;
	return 0;
}

static int put_varlen(buf *b, uint32_t v)
{
	uint8_t t[5];
	int     n = 0, i;
	t[n++] = (uint8_t)(v & 0x7F);
	while (v >>= 7)
		t[n++] = (uint8_t)(0x80 | (v & 0x7F));
	for (i = n - 1; i >= 0; i--)
		if (put(b, &t[i], 1))
			return -1;
	return 0;
}

static int write_smf(evlist *l, uint8_t **mid, uint32_t *mid_len)
{
	static const uint8_t hdr[] = { 'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0,60 };
	static const uint8_t tempo[] = { 0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20 };
	static const uint8_t eot[] = { 0xFF, 0x2F, 0x00 };
	buf      trk = { 0 }, out = { 0 };
	uint32_t i, last = 0;
	uint8_t  len[4];

	qsort(l->e, l->n, sizeof l->e[0], ev_cmp);
	if (put(&trk, tempo, sizeof tempo))
		goto oom;
	for (i = 0; i < l->n; i++) {
		const ev *e = &l->e[i];
		if (e->status == 0xFF && (e->meta_type == 0x2F || e->meta_type == 0x51))
			continue;   /* end of track written last; tempo is fixed */
		if (put_varlen(&trk, e->time - last))
			goto oom;
		last = e->time;
		if (put(&trk, &e->status, 1))
			goto oom;
		if (e->status == 0xFF) {
			if (put(&trk, &e->meta_type, 1) || put_varlen(&trk, e->ext_len) ||
			    put(&trk, e->ext, e->ext_len))
				goto oom;
		} else if (e->status == 0xF0 || e->status == 0xF7) {
			if (put_varlen(&trk, e->ext_len) || put(&trk, e->ext, e->ext_len))
				goto oom;
		} else if (put(&trk, e->d, (uint32_t)e->nd)) {
			goto oom;
		}
	}
	if (put_varlen(&trk, 0) || put(&trk, eot, sizeof eot))
		goto oom;
	len[0] = (uint8_t)(trk.n >> 24); len[1] = (uint8_t)(trk.n >> 16);
	len[2] = (uint8_t)(trk.n >> 8);  len[3] = (uint8_t)trk.n;
	if (put(&out, hdr, sizeof hdr) || put(&out, "MTrk", 4) || put(&out, len, 4) ||
	    put(&out, trk.p, trk.n))
		goto oom;
	free(trk.p);
	*mid = out.p;
	*mid_len = out.n;
	return 0;
oom:
	free(trk.p);
	free(out.p);
	return -1;
}

/* ---- IFF walking ----------------------------------------------------- */

/* Find the EVNT chunk of sequence `seq`; also count sequences. */
static int find_evnt(const uint8_t *b, uint32_t len, int seq, const uint8_t **evnt,
                     uint32_t *evnt_len, int *count, const char **msg)
{
	uint32_t p = 0;
	int      n = 0;

	*evnt = NULL;
	while (p + 12 <= len) {
		uint32_t size = be32(b + p + 4);
		if (p + 8 + size > len)
			return fail(msg, "IFF chunk runs past the end of the file");
		if (!memcmp(b + p, "CAT ", 4) && !memcmp(b + p + 8, "XMID", 4)) {
			uint32_t q = p + 12, qend = p + 8 + size;
			while (q + 12 <= qend) {
				uint32_t fsize = be32(b + q + 4), r, rend;
				if (q + 8 + fsize > qend)
					return fail(msg, "FORM runs past its CAT");
				if (!memcmp(b + q, "FORM", 4) && !memcmp(b + q + 8, "XMID", 4)) {
					r = q + 12;
					rend = q + 8 + fsize;
					while (r + 8 <= rend) {
						uint32_t csize = be32(b + r + 4);
						if (r + 8 + csize > rend)
							return fail(msg, "chunk runs past its FORM");
						if (!memcmp(b + r, "EVNT", 4) && n == seq) {
							*evnt = b + r + 8;
							*evnt_len = csize;
						}
						r += 8 + csize + (csize & 1);
					}
					n++;
				}
				q += 8 + fsize + (fsize & 1);
			}
		}
		p += 8 + size + (size & 1);
	}
	*count = n;
	return n ? 0 : fail(msg, "no XMID sequences");
}

int xmi_to_midi(const uint8_t *buf_, uint32_t len, int seq, uint8_t **mid, uint32_t *mid_len,
                xmi_stats *stats, const char **msg)
{
	const uint8_t *evnt;
	uint32_t       elen;
	evlist         l = { 0 };
	int            count, clean, rc;

	memset(stats, 0, sizeof *stats);
	if (find_evnt(buf_, len, seq, &evnt, &elen, &count, msg))
		return -1;
	stats->sequences = count;
	if (!evnt)
		return fail(msg, "sequence has no EVNT chunk");
	if (parse_evnt(evnt, evnt + elen, &l, stats, &clean)) {
		free(l.e);
		return fail(msg, "EVNT stream does not decode");
	}
	stats->ended_at_chunk_end = clean;
	rc = mid ? write_smf(&l, mid, mid_len) : 0;
	free(l.e);
	return rc ? fail(msg, "out of memory") : 0;
}

int xmi_check(const uint8_t *buf_, uint32_t len, xmi_stats *stats, const char **msg)
{
	xmi_stats one, total;
	int       s, count = 1;

	memset(&total, 0, sizeof total);
	total.ended_at_chunk_end = 1;
	for (s = 0; s < count; s++) {
		if (xmi_to_midi(buf_, len, s, NULL, NULL, &one, msg))
			return -1;
		count = one.sequences;
		total.sequences = one.sequences;
		total.events += one.events;
		total.notes += one.notes;
		if (one.ticks > total.ticks)
			total.ticks = one.ticks;
		if (!one.ended_at_chunk_end)
			total.ended_at_chunk_end = 0;
	}
	*stats = total;
	return 0;
}
