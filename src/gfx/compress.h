/* Decompressors used by Arena's data files. */
#ifndef ARENA_COMPRESS_H
#define ARENA_COMPRESS_H

#include <stddef.h>
#include <stdint.h>

/* LZSS (Okumura): 4096-byte ring buffer pre-filled with spaces, write position
 * starts at 0xFEE, one flag byte per 8 items (1 = literal), references are
 * 12-bit position + 4-bit length (3..18 bytes). Decodes until `in` is used up.
 * Reconstructed from 1000:7608 (IMG flag 0x0004). Returns bytes written; output
 * beyond `out_cap` is dropped (the original has no bound). */
size_t lzss_decode(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_cap);

/* LZHUF (Okumura/Yoshizaki 1988): LZSS (N=4096, F=60, THRESHOLD=2) with
 * adaptive Huffman coding. `in` starts with a uint16 giving the decoded size.
 * Reconstructed from 4065:0d2d (IMG flag 0x0008). Returns bytes written
 * (min of the declared size and out_cap). */
size_t lzhuf_decode(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_cap);

/* Run-length coding with signed 16-bit control words (3294:0cbd).
 * Input: uint16 total (number of words to produce), then repeated:
 *   int16 c < 0  -> next word repeated -c times
 *   int16 c >= 0 -> c literal words follow
 * Returns words written, or 0 if the input runs out first or a run overshoots
 * `total` / `out_cap`. *consumed (optional) receives the input bytes used. */
size_t rle16_decode(const uint8_t *in, size_t in_len, uint16_t *out, size_t out_cap,
                    size_t *consumed);

/* The byte-sized variant (3294:0cde): same scheme on bytes, uint16 total
 * first (0 = nothing), int16 control words, byte values. Start-up stores its
 * address in the far-pointer slot DGROUP:CD72 (3294:024e), but no call
 * through that slot was found, so the game probably never uses it. */
size_t rle8w_decode(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_cap,
                    size_t *consumed);

#endif
