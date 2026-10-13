"""Run the original game (working copy in run/ARENA) in DOSBox-X under debugger
control, and grab screenshots straight from VGA memory.

The configuration mirrors the user's own DOSBox setup (arena.conf): fixed
25000 cycles, 32 MB, S3, SB16 220/7/1, MPU-401, EMS+XMS, same ACD.EXE
arguments. Sound is off by default so automated runs stay quiet.

usage (look at what is on screen over time):
    python -I arena_run.py shots <out dir> [--keys "<autotype args>"] <seconds> [<seconds> ...]
  --keys is passed to DOSBox-X AUTOTYPE before ACD starts, e.g. "-w 6 -p 2 esc esc".
  Screenshots use the live VGA palette.
"""
import os
import struct
import sys
import time
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dbx import DosboxX  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
GAME_DIR = os.path.join(ROOT, "run", "ARENA")
ACD_ARGS = "-Ssbpdig.adv -IOS220 -IRQS7 -DMAS1 -Mgenmidi.adv -IOM330 -IRQM2 -DMAM1"


def make_conf(game_dir=GAME_DIR, sound=False, autotype=None, exe="ACD", fileio_log=None):
    """autotype: optional DOSBox-X AUTOTYPE argument string, e.g. "-w 8 -p 1 esc enter".
    fileio_log: optional path; DOSBox-X then logs every file operation through INT 21h there."""
    lines = []
    if fileio_log:
        lines += ["[log]", "logfile = %s" % fileio_log, "fileio = true", "files = debug"]
    lines += [
        "[sdl]", "output = surface", "windowresolution = 1024x768",
        "[dosbox]", "machine = svga_s3", "memsize = 32", "mcp_server = @MCP_PORT@",
        "[cpu]", "core = normal", "cputype = auto", "cycles = fixed 25000",
        "[mixer]", "nosound = %s" % ("false" if sound else "true"),
        "[sblaster]", "sbtype = sb16", "sbbase = 220", "irq = 7", "dma = 1",
        "[midi]", "mpu401 = intelligent",
        "[dos]", "xms = true", "ems = true", "umb = true",
        "[autoexec]",
        "@echo off",
        'mount C "%s"' % game_dir,
        'mount D "%s" -t cdrom' % game_dir,
        "SET ARENADATA=C:",
        "D:",
    ]
    if autotype:
        lines.append("AUTOTYPE " + autotype)
    lines.append("%s %s" % (exe, ACD_ARGS))
    return "\n".join(lines) + "\n"


def load_palette(path=os.path.join(GAME_DIR, "PAL.COL")):
    """.COL: 8-byte header + 256 RGB triples (scaled to 0-255 if stored as 6-bit)."""
    data = open(path, "rb").read()[8:8 + 768]
    scale = 4 if max(data) < 64 else 1
    return bytes(min(255, b * scale) for b in data)


def write_png(path, pixels, palette, w=320, h=200):
    def chunk(tag, body):
        return (struct.pack(">I", len(body)) + tag + body +
                struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF))
    raw = b"".join(b"\0" + pixels[y * w:(y + 1) * w] for y in range(h))
    png = (b"\x89PNG\r\n\x1a\n" +
           chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 3, 0, 0, 0)) +
           chunk(b"PLTE", palette) + chunk(b"IDAT", zlib.compress(raw, 9)) +
           chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(png)


def read_dac(d):
    """Read the live 256-colour VGA palette through the DAC ports (6-bit -> 8-bit)."""
    d.exec("OUTP 3C7 0")
    out = bytearray()
    for _ in range(768):
        line = d.exec("INP 3C9")
        val = int(line[-1].split()[-1], 16) if line else 0
        out.append(min(255, (val & 0x3F) * 4))
    return bytes(out)


def screenshot(d, path, palette=None):
    pixels = d.dump("A000:0000", 64000, "screen.bin")
    write_png(path, pixels, palette or load_palette())
    return pixels


def main():
    args = sys.argv[1:]
    if len(args) < 3 or args[0] != "shots":
        print(__doc__)
        return 2
    out_dir = os.path.abspath(args[1])
    rest = args[2:]
    keys = None
    if rest[0] == "--keys":
        keys, rest = rest[1], rest[2:]
    os.makedirs(out_dir, exist_ok=True)
    times = sorted(float(t) for t in rest)
    with DosboxX(make_conf(autotype=keys), GAME_DIR) as d:
        t0 = time.time()
        for t in times:
            time.sleep(max(0, t0 + t - time.time()))
            name = os.path.join(out_dir, "shot_%05.1fs.png" % t)
            screenshot(d, name, read_dac(d))
            print("saved", name)
    return 0


if __name__ == "__main__":
    sys.exit(main())
