"""Check an unpacked EXE loads to the same memory image as the packed original.

usage: python -I verify_unpack.py <packed.exe> <unpacked.exe>
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from unpklite import run  # noqa: E402
from emu8086 import SS, SP  # noqa: E402

SEG = 0x1000
packed = open(sys.argv[1], "rb").read()
unp = open(sys.argv[2], "rb").read()

cpu, mem, st, _ = run(packed, SEG)
h = struct.unpack_from("<14H", unp, 0)
_, cblp, cp, crlc, cparhdr, minalloc, _, ss, sp, _, ip, cs, lfarlc, _ = h
size = (cp - 1) * 512 + (cblp or 512)
img = bytearray(unp[cparhdr * 16:size])
for i in range(crlc):
    off, seg = struct.unpack_from("<HH", unp, lfarlc + 4 * i)
    a = seg * 16 + off
    v = struct.unpack_from("<H", img, a)[0]
    struct.pack_into("<H", img, a, (v + SEG) & 0xFFFF)

orig = mem[SEG << 4:(SEG << 4) + len(img)]
bad = [i for i in range(len(img)) if img[i] != orig[i]]
print("bytes compared:", len(img), "mismatches:", len(bad), bad[:10])
print("entry  packed %04X:%04X  unpacked %04X:%04X" % ((st["done"][0] - SEG) & 0xFFFF, st["done"][1], cs, ip))
print("stack  packed %04X:%04X  unpacked %04X:%04X" % ((cpu.s[SS] - SEG) & 0xFFFF, cpu.r[SP], ss, sp))
ok = not bad and (cs, ip) == ((st["done"][0] - SEG) & 0xFFFF, st["done"][1]) and (ss, sp) == ((cpu.s[SS] - SEG) & 0xFFFF, cpu.r[SP])
print("OK" if ok else "FAIL")
sys.exit(0 if ok else 1)
