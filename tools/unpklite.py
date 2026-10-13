"""Unpack a PKLITE-compressed DOS EXE by emulating its own decompression stub.

The stub is run twice with different load segments. Words that differ by
exactly the segment delta between the two runs are relocation targets, which
lets us rebuild a normal MZ executable with a correct relocation table.

usage: python -I unpklite.py <packed.exe> <out.exe>
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from emu8086 import CPU, CS, DS, ES, SS, SP, AX, DX, EmuError  # noqa: E402

TOP_SEG = 0xA000
MAX_STEPS = 200_000_000


def run(data, load_seg):
    hdr = struct.unpack_from("<14H", data, 0)
    _, cblp, cp, crlc, cparhdr, minalloc, maxalloc, ss, sp, _, ip, cs, lfarlc, _ = hdr
    size = (cp - 1) * 512 + (cblp or 512)
    image = data[cparhdr * 16:size]

    mem = bytearray(1 << 20)
    psp = load_seg - 0x10
    base = load_seg << 4
    mem[base:base + len(image)] = image
    mem[psp << 4:(psp << 4) + 2] = b"\xCD\x20"
    struct.pack_into("<H", mem, (psp << 4) + 2, TOP_SEG)

    for i in range(crlc):
        off, seg = struct.unpack_from("<HH", data, lfarlc + 4 * i)
        a = base + seg * 16 + off
        v = struct.unpack_from("<H", mem, a)[0]
        struct.pack_into("<H", mem, a, (v + load_seg) & 0xFFFF)

    cpu = CPU(mem)
    cpu.s[CS] = (cs + load_seg) & 0xFFFF
    cpu.ip = ip
    cpu.s[SS] = (ss + load_seg) & 0xFFFF
    cpu.r[SP] = sp
    cpu.s[DS] = cpu.s[ES] = psp

    state = {"high": 0, "done": None, "far": []}

    def on_int(c, n):
        if n == 0x21:
            ah = c.r[AX] >> 8
            if ah == 0x30:              # DOS version -> 5.0
                c.r[AX] = 0x0005
                return True
            if ah == 0x4A:              # resize block: always succeed
                c.cf = 0
                return True
            if ah in (0x09, 0x4C):
                raise EmuError("stub exited via int 21h/%02X (out of memory path?)" % ah)
        return False

    # The first far transfer enters the stub's relocated copy; the next far
    # transfer to any other segment is the jump into the unpacked program.
    def on_far(c, fcs, fip):
        state["far"].append((fcs, fip))
        if not state["high"]:
            state["high"] = fcs
        elif fcs != state["high"]:
            state["done"] = (fcs, fip)

    cpu.int_handler = on_int
    cpu.far_jump_hook = on_far
    while state["done"] is None:
        cpu.step()
        if cpu.steps > MAX_STEPS:
            raise EmuError("step limit hit")
    return cpu, mem, state, hdr


def main():
    src, dst = sys.argv[1], sys.argv[2]
    data = open(src, "rb").read()

    # delta 0x1111 changes both bytes of every relocated word, so the first
    # differing byte is always the low byte of the word.
    seg_a, seg_b = 0x1000, 0x2111
    cpu_a, mem_a, st_a, hdr = run(data, seg_a)
    cpu_b, mem_b, st_b, _ = run(data, seg_b)

    entry_cs = (st_a["done"][0] - seg_a) & 0xFFFF
    entry_ip = st_a["done"][1]
    stack_ss = (cpu_a.s[SS] - seg_a) & 0xFFFF
    stack_sp = cpu_a.r[SP]
    assert (st_b["done"][0] - seg_b) & 0xFFFF == entry_cs and st_b["done"][1] == entry_ip
    print("far transfers (run A):", ["%04X:%04X" % f for f in st_a["far"]])
    print("steps:", cpu_a.steps)
    print("entry CS:IP = %04X:%04X  SS:SP = %04X:%04X (load-relative)" %
          (entry_cs, entry_ip, stack_ss, stack_sp))

    # The unpacked image is everything below the relocated stub that the two
    # runs agree on (modulo relocations). Find its end as the last non-zero
    # byte below the high-memory area the stub moved itself to.
    base_a, base_b = seg_a << 4, seg_b << 4
    limit = (st_a["high"] << 4) - base_a  # everything below the relocated stub
    img_a = mem_a[base_a:base_a + limit]
    img_b = mem_b[base_b:base_b + limit]
    # Keep everything up to the stub copy (no trailing-zero trimming): DOS does
    # not zero memory, so bytes the original relied on must stay in the file.
    end = limit

    relocs = []
    i = 0
    delta = seg_b - seg_a
    while i < end:
        if img_a[i] != img_b[i]:
            wa = struct.unpack_from("<H", img_a, i)[0]
            wb = struct.unpack_from("<H", img_b, i)[0]
            if (wb - wa) & 0xFFFF != delta:
                raise SystemExit("unexpected diff at image offset %05X: %04X vs %04X" % (i, wa, wb))
            relocs.append(i)
            i += 2
        else:
            i += 1
    print("image size: %d bytes, relocations: %d" % (end, len(relocs)))

    # Un-relocate run A's image (subtract load segment) to get file contents.
    image = bytearray(img_a[:end])
    for off in relocs:
        v = struct.unpack_from("<H", image, off)[0]
        struct.pack_into("<H", image, off, (v - seg_a) & 0xFFFF)

    # Memory needed beyond the file image: at least up to the top of the stack.
    stack_top = stack_ss * 16 + (stack_sp or 0x10000)
    extra_paras = max(0, (stack_top - end + 15) // 16)
    # PKLITE's stub starts with "mov ax, <total paragraphs the program needs>".
    image_off = hdr[4] * 16
    assert data[image_off] == 0xB8, "unexpected PKLITE stub layout"
    need_paras = struct.unpack_from("<H", data, image_off + 1)[0]
    minalloc = max(extra_paras, need_paras - end // 16)

    reloc_bytes = b"".join(struct.pack("<HH", off & 0xF, off >> 4) for off in relocs)
    hdr_size = 0x1C + len(reloc_bytes)
    hdr_paras = (hdr_size + 511) // 512 * 32       # align header to 512 bytes
    total = hdr_paras * 16 + len(image)
    out = bytearray(struct.pack("<14H", 0x5A4D, total % 512, (total + 511) // 512,
                                len(relocs), hdr_paras, minalloc, 0xFFFF,
                                stack_ss, stack_sp, 0, entry_ip, entry_cs, 0x1C, 0))
    out += reloc_bytes
    out += b"\0" * (hdr_paras * 16 - len(out))
    out += image
    open(dst, "wb").write(out)
    print("wrote %s (%d bytes, minalloc %04X paras)" % (dst, len(out), minalloc))


if __name__ == "__main__":
    main()
