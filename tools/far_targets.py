"""List every far-call target in an MZ executable, using its relocation table.

Each far call (9A off seg) stores its target segment in a relocated word, so
the relocation table gives an exact list of far-call targets. Addresses are
written as Ghidra sees them (image loaded at segment 0x1000).

usage: python -I far_targets.py <unpacked.exe> <out.txt>
"""
import struct
import sys

LOAD_SEG = 0x1000

f = open(sys.argv[1], "rb").read()
h = struct.unpack_from("<14H", f, 0)
img = f[h[4] * 16:]
targets = set()
for i in range(h[3]):
    off, seg = struct.unpack_from("<HH", f, h[12] + 4 * i)
    r = seg * 16 + off
    if r >= 3 and img[r - 3] in (0x9A, 0xEA):          # call far / jmp far
        tseg = struct.unpack_from("<H", img, r)[0]
        toff = struct.unpack_from("<H", img, r - 2)[0]
        targets.add((tseg + LOAD_SEG, toff))
with open(sys.argv[2], "w") as out:
    out.write("\n".join("%04X:%04X" % t for t in sorted(targets)))
print("%d far-call targets -> %s" % (len(targets), sys.argv[2]))
