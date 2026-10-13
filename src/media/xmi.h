/* XMIDI music (.XMI general MIDI, .XFM FM versions), the Miles Sound System
 * format (not Bethesda's). Converted here to a standard MIDI file so any
 * synth can play it.
 *
 * IFF layout (big-endian chunk sizes, padded to even length):
 *   FORM XDIR { INFO: uint16 sequence count (little-endian) }
 *   CAT  XMID { FORM XMID { [TIMB] [RBRN] EVNT } ... }
 * EVNT stream:
 *   delay: a run of bytes < 0x80, summed, before each event (120 Hz ticks)
 *   0x9n note velocity duration(varlen)  note-on carrying its own duration
 *   0x8n/0xAn/0xBn/0xEn 2 data bytes, 0xCn/0xDn 1 data byte
 *   0xF0/0xF7 varlen length + data, 0xFF type varlen length + data
 *   0xFF 0x2F end of track
 * Output: SMF format 0, 60 ticks per quarter note at 500000 us per quarter
 * note (= 120 ticks per second, XMIDI's rate). Tempo events in the stream
 * are dropped, as XMIDI players run at the fixed rate.
 */
#ifndef ARENA_XMI_H
#define ARENA_XMI_H

#include <stdint.h>

typedef struct xmi_stats {
	int      sequences;
	uint32_t events, notes, ticks;
	int      ended_at_chunk_end;   /* all EVNT streams stopped exactly at their end */
} xmi_stats;

/* Convert sequence `seq` (0-based) of an XMIDI file to a standard MIDI file.
 * *mid / *mid_len receive a malloc'd SMF. 0 on success, -1 on error. */
int xmi_to_midi(const uint8_t *buf, uint32_t len, int seq, uint8_t **mid, uint32_t *mid_len,
                xmi_stats *stats, const char **msg);

/* Walk every sequence and gather stats without keeping the output. */
int xmi_check(const uint8_t *buf, uint32_t len, xmi_stats *stats, const char **msg);

#endif
