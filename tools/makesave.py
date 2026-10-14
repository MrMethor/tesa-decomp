"""Build a test save slot in the working copy of the game (run/ARENA) that
puts the player into a given level of a given .MIF, for scenarios that need
places the user's own saves do not reach (e.g. stairs between dungeon
levels). Only the working copy is changed; the original game folder never.

    python -I makesave.py <base slot> <new slot> <file.MIF> <level> <x> <z> <angle> [name]

The new slot starts as a copy of every file of the base slot; then in
SAVEGAME the location fields are replaced (.MIF name, level index, .INF
name, level name, the MHDR copy, LOCK / TRIG / TARG records, map layers,
position and facing), the timed-cell table and light list are cleared, and
STATES gets each level's LOCK / TRIG records from the .MIF (as on a first
visit). NAMES.DAT gets a name for the slot so the load screen lists it.
Field offsets: src/data/save.h, crates/arena-data/src/save.rs.
"""
import os
import shutil
import struct
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
GAME = os.path.join(ROOT, "run", "ARENA")
TOOL = os.path.join(ROOT, "target", "release", "arenatool.exe")

SCREEN, PAL, STATE = 64000, 768, 0x0DE7
ST = SCREEN + PAL          # offset of the state block in SAVEGAME
LAYERS = ST + STATE        # MAP1, FLOR, MAP2


def mif_chunks(data):
    """MHDR body and per-level {tag: payload} (first chunk of each tag)."""
    p, mhdr, levels = 0, None, []
    while p + 6 <= len(data):
        tag, n = data[p:p + 4], struct.unpack_from("<H", data, p + 4)[0]
        if tag == b"MHDR":
            mhdr = data[p + 6:p + 6 + n]
            p += 6 + n
        elif tag == b"LEVL":
            q, chunks = p + 6, {}
            while q + 6 <= len(data) and data[q:q + 4] != b"LEVL":
                t, m = data[q:q + 4], struct.unpack_from("<H", data, q + 4)[0]
                chunks.setdefault(t, data[q + 6:q + 6 + m])
                q += 6 + m
            levels.append(chunks)
            p = q
        else:
            break
    return mhdr, levels


def cstr(b, size):
    return (b + b"\0" * size)[:size]


def main():
    if len(sys.argv) < 8:
        print(__doc__)
        return 2
    base, slot, mif_name, level = int(sys.argv[1]), int(sys.argv[2]), sys.argv[3], int(sys.argv[4])
    x, z, angle = int(sys.argv[5]), int(sys.argv[6]), int(sys.argv[7])
    name = sys.argv[8] if len(sys.argv) > 8 else "test"
    for f in os.listdir(GAME):
        stem, _, ext = f.rpartition(".")
        if ext == "%02d" % base:
            shutil.copyfile(os.path.join(GAME, f), os.path.join(GAME, "%s.%02d" % (stem, slot)))
    mif = open(os.path.join(GAME, mif_name), "rb").read() if os.path.exists(os.path.join(GAME, mif_name)) else None
    if mif is None:
        raise SystemExit("%s must be a loose file in run/ARENA" % mif_name)
    mhdr, levels = mif_chunks(mif)
    lv = levels[level]
    layers = os.path.join(ROOT, "run", "makesave_layers.bin")
    subprocess.run([TOOL, "layers", GAME, mif_name, str(level), "e000", layers], check=True, capture_output=True)
    lay = open(layers, "rb").read()

    path = os.path.join(GAME, "SAVEGAME.%02d" % slot)
    sg = bytearray(open(path, "rb").read())

    def put(off, data):
        sg[ST + off:ST + off + len(data)] = data

    put(0x002, bytes([level]))
    put(0x003, b"\xff")                                   # no pending level
    put(0x877, cstr(mif_name.lower().encode(), 13))       # .MIF name (DGROUP:C37F)
    put(0x7AD, cstr(lv.get(b"INFO", b"").split(b"\0")[0].lower(), 13))
    put(0x78C, cstr(lv.get(b"NAME", b"").split(b"\0")[0], 33))
    put(0x83A, mhdr[:61])
    for (tag, off, count_off, size) in ((b"LOCK", 0xB1C, 0xBDC, 3), (b"TARG", 0xBDD, 0xC5D, 2),
                                        (b"TRIG", 0xC5E, 0xD5E, 4)):
        recs = lv.get(tag, b"")[:64 * size]
        put(off, cstr(recs, 64 * size))
        put(count_off, bytes([len(recs) // size]))
    put(0x7BA, b"\xff\xff" * 32 + b"\0" * 64)             # timed cells: none
    put(0x186, b"\0\0")                                   # light list: empty
    flags = struct.unpack_from("<H", sg, ST + 0x890)[0] & ~0x0004
    if b"MAP2" in lv:
        flags |= 0x0004
    put(0x890, struct.pack("<H", flags))
    put(0x8C8, struct.pack("<HhH", x, 60, z))
    put(0x8D2, struct.pack("<H", angle & 511))
    sg[LAYERS:LAYERS + len(lay)] = lay
    open(path, "wb").write(sg)

    states = bytearray(64 * 450)
    for i, l in enumerate(levels[:64]):
        trig, lock = l.get(b"TRIG", b"")[:256], l.get(b"LOCK", b"")[:192]
        r = 450 * i
        states[r] = len(trig) // 4
        states[r + 1:r + 1 + len(trig)] = trig
        states[r + 257] = len(lock) // 3
        states[r + 258:r + 258 + len(lock)] = lock
    open(os.path.join(GAME, "STATES.%02d" % slot), "wb").write(states)

    names = bytearray(open(os.path.join(GAME, "NAMES.DAT"), "rb").read())
    names[48 * slot:48 * slot + 48] = cstr(name.encode(), 48)
    open(os.path.join(GAME, "NAMES.DAT"), "wb").write(names)
    print("slot %d: %s level %d of %d, player at (%d, %d) facing %d" % (slot, mif_name, level, len(levels), x, z, angle))
    return 0


if __name__ == "__main__":
    sys.exit(main())
