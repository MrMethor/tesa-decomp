/* .RMD wilderness blocks. See rmd.h. */
#include "rmd.h"

#include <stdlib.h>
#include <string.h>

#include "gfx/compress.h"
#include "gfx/img.h"   /* afile_load_all */

int rmd_load(const char *name, arena_rmd *out)
{
	uint32_t  len;
	uint8_t  *buf = afile_load_all(name, &len);
	uint16_t *cells;
	size_t    used = 0, n;

	memset(out, 0, sizeof *out);
	if (!buf)
		return -1;
	cells = malloc(3 * RMD_CELLS * sizeof *cells);
	if (!cells) {
		free(buf);
		return -1;
	}
	n = rle16_decode(buf, len, cells, 3 * RMD_CELLS, &used);
	free(buf);
	if (n != 3 * RMD_CELLS) {
		free(cells);
		return -1;
	}
	memcpy(out->flor, cells, sizeof out->flor);
	memcpy(out->map1, cells + RMD_CELLS, sizeof out->map1);
	memcpy(out->map2, cells + 2 * RMD_CELLS, sizeof out->map2);
	out->file_size = len;
	out->consumed = (uint32_t)used;
	free(cells);
	return 0;
}
