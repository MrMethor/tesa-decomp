/* .IMG images and .CIF image sets.
 *
 * Image record (layout read by the draw routine at 1000:582c):
 *   int16  x, y          default screen position
 *   uint16 width, height
 *   uint16 flags
 *   uint16 length        bytes of pixel data that follow the header
 *   uint8  data[length]
 *   uint8  palette[768]  only when flags & IMG_PALETTE (.IMG files only)
 * Flags:
 *   0x0001 IMG_RLE       run-length coded over width*height bytes (1000:5b65)
 *   0x0002 IMG_RLE_ROWS  run-length coded per row, drawn straight to the
 *                        screen; zero bytes are not written (1000:5b9a)
 *   0x0004 IMG_LZSS      LZSS (1000:7608); the length word is also the
 *                        compressed byte count
 *   0x0008 IMG_LZHUF     LZHUF (4065:0d2d), data starts with the size
 *   0x0100 IMG_PALETTE   a 256-colour palette follows the data
 *   0x0800 IMG_MASKED    colour 0 is transparent when drawn
 * RLE control byte: bit 7 clear -> (b + 1) literal bytes follow;
 *                   bit 7 set   -> next byte repeated (b & 0x7F) + 1 times.
 *
 * .CIF: image records back to back, no palettes. Frame n is found by
 * skipping n records (header + length each); the same loop appears at
 * 1000:3c94, 1000:4d52, 2194:5ccc, 36f7:0785, 3826:03fa, ...
 *
 * Headerless images: wall/floor textures are raw 64x64 pixels (4096 bytes).
 */
#ifndef ARENA_IMG_H
#define ARENA_IMG_H

#include <stddef.h>
#include <stdint.h>

#define IMG_RLE      0x0001
#define IMG_RLE_ROWS 0x0002
#define IMG_LZSS     0x0004
#define IMG_LZHUF    0x0008
#define IMG_PALETTE  0x0100
#define IMG_MASKED   0x0800

#define IMG_HEADER_SIZE 12

typedef struct arena_img {
	int16_t  x, y;
	uint16_t width, height;
	uint16_t flags;
	int      headerless;        /* raw 64x64 texture */
	uint8_t *pixels;            /* width * height, malloc'd; 0 = transparent if masked */
	int      has_palette;
	uint8_t  palette[768];      /* 8-bit RGB, as stored */
} arena_img;

/* Decode one image record at buf (avail bytes available). *used receives the
 * record size (header + data + palette). 0 on success, -1 on error. */
int  img_decode_record(const uint8_t *buf, size_t avail, arena_img *out, size_t *used,
                       const char **msg);

/* Decode a whole .IMG file (one record that fills the file, or a 64x64
 * headerless texture). 0 on success, -1 if not understood. */
int  img_decode(const uint8_t *buf, size_t len, arena_img *out, const char **msg);
void img_free(arena_img *img);

/* Load an .IMG through the file layer (loose file or GLOBAL.BSA). */
int  img_load(const char *name, arena_img *out, const char **msg);

/* .CIF: all frames of a set. */
typedef struct arena_cif {
	int        count;
	arena_img *frames;
} arena_cif;

int  cif_decode(const uint8_t *buf, size_t len, arena_cif *out, const char **msg);
int  cif_load(const char *name, arena_cif *out, const char **msg);
void cif_free(arena_cif *cif);

/* .RCI screen animations (WATERANI.RCI, LAVAANI.RCI): raw 320x100 frames of
 * 32000 bytes, no header, 5 per file. 1000:dfdb reads the 160000 bytes into
 * three EMS pages (64000 + 64000 + 32000); 1000:a483 picks WATERANI for
 * exteriors and water levels (DGROUP:CB5B) and LAVAANI otherwise. Agrees
 * with OpenTESArena's RCIFile. Frames are returned in an arena_cif (free with
 * cif_free). SPPARTS.RCI is a different thing despite the extension: nine
 * 3x3 tiles for tileset_load (2194:5301 -> 1000:756c). */
#define RCI_W 320
#define RCI_H 100
int  rci_load(const char *name, arena_cif *out, const char **msg);

/* Read a whole file through the file layer into a malloc'd buffer. */
uint8_t *afile_load_all(const char *name, uint32_t *size);

/* .MNU menu screens (STARTGAM, TAMRIEL, MAPBTNS): one image record, loaded
 * and drawn by the generic routine 1000:56cc (which never reads past the
 * record), followed by a trailer of 1-bit masks, most significant bit first:
 * MAPBTNS 339 bytes = 113 rows of 3 bytes (rounded shapes ~18 pixels wide),
 * STARTGAM 1400 bytes = 35 rows of 40 bytes (320 pixels), TAMRIEL 6556 bytes
 * (not whole 40-byte rows; layout unknown).
 * No reader was found in ACD.EXE: the loader (1000:56cc -> 742c) reads the
 * whole file but decodes only the record; no code uses the trailer offsets;
 * province clicks are tested against the CITYDATA province rectangles
 * {x, y, w, h} (2194:3080, quadrant test 2194:3032). So the trailers look
 * like leftovers of the authoring tool; a port can ignore them. */
typedef struct arena_mnu {
	arena_img image;
	uint8_t  *trailer;      /* malloc'd copy, may be NULL */
	uint32_t  trailer_len;
} arena_mnu;

int  mnu_load(const char *name, arena_mnu *out, const char **msg);
void mnu_free(arena_mnu *mnu);

/* Raw full-screen images with no header (CHARSPEL.DAT): 320x200 bytes. */
#define RAW_SCREEN_W 320
#define RAW_SCREEN_H 200
int  raw_screen_load(const char *name, arena_img *out, const char **msg);

/* Raw headerless images whose size is fixed by the code that draws them:
 *   CITY.IMG 16x11, TOWN.IMG 9x10, VILLAGE.IMG 8x8, DUNGEON.IMG 14x8
 *     (province-map icons, 36f7:0268 -> 36f7:0325)
 *   P1.IMG 320x53 (HUD bar, loaded straight into the screen buffer at row 147, 1000:4dd6)
 *   NOSPELL.IMG 25x19 (drawn at 91,177 by 392b:032a); NOCAMP.IMG has the same size
 *   ARENARW.IMG 256 bytes copied into the mouse-cursor buffer (1000:9ac4)
 *   SLIDER.IMG 289x7 compass strip: 1000:4d7c copies a 32x7 window starting at
 *     column ((facing + 0xE0) & 0x1FF) / 2 (256 positions per turn; 33 extra
 *     columns so the window never wraps) to screen (144, 7), then draws
 *     COMPASS.IMG over it; shown while DGROUP:05C8 bit 13 is set
 *   UPDOWN.IMG 8x16 scroll arrows of the character-creation lists: loaded raw
 *     by 2194:12f5, drawn by 1000:5b04 at x 38 (2194:1370-1381)
 * POPTALK.IMG (24640 bytes) and S2.IMG (11520 bytes) are never used: no code
 * refers to POPTALK's name string (DGROUP:8726), S2's name does not occur in
 * ACD.EXE, and neither is opened in the logged play sessions.
 * DITHER.IMG / DITHER2.IMG are 800-byte tables, not pictures (3294:0341). */
typedef struct raw_image_def {
	const char *name;
	uint16_t    width, height;
} raw_image_def;

extern const raw_image_def raw_image_defs[];
extern const int           raw_image_def_count;

/* Load a raw image of known size; fails if the file size is not w*h. */
int raw_image_load(const char *name, uint16_t w, uint16_t h, arena_img *out, const char **msg);

#endif
