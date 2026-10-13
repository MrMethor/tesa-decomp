#ifndef ARENA_TOOLS_PNG_H
#define ARENA_TOOLS_PNG_H

#include <stdint.h>
#include <string.h>

/* Write an 8-bit indexed PNG. palette is 256 RGB triples (8-bit). 0 on success. */
int png_write_indexed(const char *path, const uint8_t *pixels, int w, int h,
                      const uint8_t palette[768]);

#endif
