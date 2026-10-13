/* Data read at run time from the unpacked ACD.EXE. See exedata.h. */
#include "exedata.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

int exe_load(const char *path, arena_exe *out)
{
	FILE    *f = fopen(path, "rb");
	long     n;
	uint32_t hdr, entry, dgroup;

	memset(out, 0, sizeof *out);
	if (!f)
		return -1;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	out->file = malloc(n > 0 ? (size_t)n : 1);
	if (!out->file || fread(out->file, 1, (size_t)n, f) != (size_t)n) {
		fclose(f);
		exe_free(out);
		return -1;
	}
	fclose(f);
	out->file_size = (uint32_t)n;

	if (n < 0x40 || out->file[0] != 'M' || out->file[1] != 'Z')
		goto bad;
	hdr = rd16(out->file + 8) * 16u;
	/* entry = CS:IP from the header, relative to the image */
	entry = hdr + rd16(out->file + 0x16) * 16u + rd16(out->file + 0x14);
	if (entry + 3 > out->file_size || out->file[entry] != 0xBA)   /* mov dx, imm16 */
		goto bad;
	dgroup = hdr + rd16(out->file + entry + 1) * 16u;
	if (dgroup >= out->file_size)
		goto bad;
	out->dgroup = out->file + dgroup;
	out->dgroup_size = out->file_size - dgroup;
	return 0;

bad:
	exe_free(out);
	return -1;
}

void exe_free(arena_exe *exe)
{
	free(exe->file);
	memset(exe, 0, sizeof *exe);
}

const char *exe_list_string(const arena_exe *exe, uint16_t offset, unsigned n)
{
	uint32_t p = offset;
	while (n--) {
		while (p < exe->dgroup_size && exe->dgroup[p])
			p++;
		p++;
		if (p >= exe->dgroup_size)
			return NULL;
	}
	if (!memchr(exe->dgroup + p, 0, exe->dgroup_size - p))
		return NULL;
	return (const char *)exe->dgroup + p;
}

int exe_creature_cfa(const arena_exe *exe, unsigned type, unsigned anim, char *out, int outsz)
{
	const char *t;
	int         i;

	if (type < 1 || type > EXE_CREATURE_COUNT || anim > 9)
		return -1;
	t = exe_list_string(exe, EXE_CREATURE_TEMPLATES, type);
	if (!t || (int)strlen(t) >= outsz)
		return -1;
	for (i = 0; t[i]; i++)
		out[i] = t[i] == '@' ? (char)('0' + anim) : t[i];
	out[i] = 0;
	return 0;
}
