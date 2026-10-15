"""Run a behaviour scenario in the original game (DOSBox-X, working copy in
run/ARENA) and write the observed state at every frame.

The original is driven through DOSBox-X's debugger, without changing the
game files:
  - a save slot is loaded through the menus: AUTOTYPE presses the keys up to
    the load screen, and the click on the slot is answered by skipping the
    load screen's wait-for-click call (35fd:07b0) with the click's
    coordinates placed in AX / BX;
  - the run is then stepped one 3D-loop frame at a time with a breakpoint on
    the call to the movement step (1000:a3b3). At that point the player
    position is restored and the frame's input has not been read yet;
  - held keys are written straight into the game's key-state table
    (DGROUP:ACB5 + scancode, 0xFF = down), which the game's own keyboard
    handler normally maintains;
  - the state is read from memory at every stop.

Addresses are Ghidra addresses (image loaded at 1000:0000); the run-time
segment is found from the DOS memory blocks and checked against the code
bytes of bin/ACD_unpacked.EXE when that file is present.

usage:
    python -I oracle.py <scenario.scn> <out.state> [--keep]
--keep leaves DOSBox-X running at the end (for a look at the screen).
"""
import os
import re
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from arena_run import GAME_DIR, ROOT, make_conf  # noqa: E402
from dbx import DosboxX, DosboxError  # noqa: E402
import scenario  # noqa: E402

UNPACKED = os.path.join(ROOT, "bin", "ACD_unpacked.EXE")

# Ghidra addresses.
BP_MOVE = (0x1000, 0xA3B3)      # CALL movement step, once per 3D frame
BP_LOADCLICK = (0x35FD, 0x07B0)  # load screen: wait for a click
LOADCLICK_RET = 0x07B5
DGROUP = 0x4BA0
KEYS = 0xACB5                   # key-state table, one byte per scancode
MOUSE_BUTTONS = 0xAFF9

# State block read at every stop: DGROUP offsets -> field.
STATE_FIELDS = [
    # name, offset, struct format
    ("x", 0xC3D0, "<H"),
    ("y", 0xC3D2, "<h"),
    ("z", 0xC3D4, "<H"),
    ("angle", 0xC3DA, "<H"),
    ("level", 0xBB0A, "<B"),
    ("frame", 0xCCBC, "<H"),
    ("lflags", 0xC398, "<H"),
    ("mflags", 0xC39A, "<H"),
]
# Diagnostic fields: recorded, not compared by default.
DIAG_FIELDS = [
    ("accel", 0x7A46, "<H"),      # movement ramp, 0..0x100
    ("speed", 0xCCBE, "<H"),      # walk speed used this frame
    ("mx", 0x8F92, "<h"),         # mouse cursor
    ("my", 0x8F94, "<h"),
    ("rng", 0x91F6, "<I"),        # random generator state (1000:6e10)
]
# Load screen: rows of 14 pixels from y = 4 (35fd:07c3-07fa).
SLOT_ROW_Y = lambda slot: 4 + 14 * slot + 7  # noqa: E731
AUTOTYPE_TO_LOAD = "-w 10 -p 2 esc esc esc space l"


class Oracle:
    def __init__(self, d):
        self.d = d
        self.base = None

    # -- addresses -----------------------------------------------------
    def seg(self, ghidra_seg):
        return ghidra_seg - 0x1000 + self.base

    def find_base(self):
        """Run-time segment of image offset 0 = PSP of ACD + 0x10."""
        lines = self.d.exec("DOS MCBS")
        for line in lines:
            if re.search(r"\bACD\b", line):
                m = re.findall(r"\b([0-9A-F]{4})\b", line)
                # The MCB line lists the block segment and its owner PSP.
                for cand in m:
                    base = int(cand, 16) + 0x10
                    if self.check_code(base):
                        self.base = base
                        return base
        # Fallback: try every 4-digit hex number in the listing.
        for line in lines:
            for cand in re.findall(r"\b([0-9A-F]{4})\b", line):
                base = int(cand, 16) + 0x10
                if self.check_code(base):
                    self.base = base
                    return base
        raise DosboxError("ACD.EXE not found in memory:\n" + "\n".join(lines))

    def check_code(self, base):
        seg = BP_MOVE[0] - 0x1000 + base
        try:
            got = self.d.dump("%04X:%04X" % (seg, BP_MOVE[1]), 3, "oracle_chk.bin")
        except DosboxError:
            return False
        want = expected_bytes(BP_MOVE, 3)
        return got == want if want else got[:1] == b"\xe8"

    # -- debugger primitives --------------------------------------------
    def run(self):
        self.d.exec("RUN")

    def wait_break(self, ghidra, timeout, moved_from=None):
        """Wait until the CPU sits on the breakpoint at `ghidra`.

        DOSBox-X does not report breakpoint hits over the control
        connection, so CS:IP is polled (an EXEC does not pause a running
        machine). Two equal readings on the breakpoint mean it is stopped.
        moved_from: the frame counter value at the previous stop; the stop
        only counts once the counter has changed (so the stop being left
        is not mistaken for the next one)."""
        want = (self.seg(ghidra[0]), ghidra[1])
        deadline = time.time() + timeout
        while time.time() < deadline:
            if self.cs_ip() == want and self.cs_ip() == want:
                if moved_from is None or self.read(0xCCBC, 2) != moved_from:
                    return want
            time.sleep(0.01)
        raise DosboxError("no stop at %04X:%04X within %ds" % (want + (timeout,)))

    def cs_ip(self):
        out = self.d.exec("EV CS EIP")
        vals = re.findall(r"[0-9A-Fa-f]+", out[-1].split("is:")[-1]) if out else []
        return int(vals[0], 16), int(vals[1], 16) & 0xFFFF

    def bp(self, ghidra):
        self.d.exec("BP %04X:%04X" % (self.seg(ghidra[0]), ghidra[1]))

    def read(self, off, n):
        return self.d.dump("%04X:%04X" % (self.seg(DGROUP), off), n, "oracle_mem.bin")

    def write(self, off, data):
        vals = " ".join("%02X" % b for b in data)
        self.d.exec("SM %04X:%04X %s" % (self.seg(DGROUP), off, vals))

    # -- game steps ------------------------------------------------------
    def attach(self, timeout=60):
        """Pause as soon as ACD.EXE is in memory and find its segment."""
        deadline = time.time() + timeout
        while True:
            time.sleep(1.0)
            self.d.brk()
            try:
                return self.find_base()
            except DosboxError:
                if time.time() > deadline:
                    raise
                self.run()

    def load_slot(self, slot, timeout=90):
        self.attach()
        self.bp(BP_LOADCLICK)
        self.run()
        self.wait_break(BP_LOADCLICK, timeout)
        self.d.exec("BPDEL *")
        # Pretend the wait returned a click at (160, row centre).
        self.d.exec("SR AX %X BX %X EIP %X" % (160, SLOT_ROW_Y(slot), LOADCLICK_RET))
        self.write(MOUSE_BUTTONS, b"\x00")
        self.bp(BP_MOVE)
        self.run()
        self.wait_break(BP_MOVE, timeout)

    def map1_seg(self):
        return struct.unpack("<H", self.read(0xD806, 2))[0]

    def read_cell(self, col, row):
        data = self.d.dump("%04X:%04X" % (self.map1_seg(), (row * 128 + col) * 2), 2, "oracle_cell.bin")
        return struct.unpack("<H", data)[0]

    def set_cell(self, col, row, value):
        self.d.exec("SM %04X:%04X %02X %02X" % (self.map1_seg(), (row * 128 + col) * 2,
                                                 value & 0xFF, value >> 8))

    def state(self):
        lo = min(f[1] for f in STATE_FIELDS)
        hi = max(f[1] + struct.calcsize(f[2]) for f in STATE_FIELDS)
        mem = self.read(lo, hi - lo)
        st = {}
        for name, off, fmt in STATE_FIELDS:
            st[name] = struct.unpack_from(fmt, mem, off - lo)[0]
        for name, off, fmt in DIAG_FIELDS:
            st["d_" + name] = struct.unpack(fmt, self.read(off, struct.calcsize(fmt)))[0]
        return st

    def set_keys(self, scancodes, managed):
        for sc in managed:
            self.write(KEYS + sc, b"\xff" if sc in scancodes else b"\x00")

    def step(self):
        before = self.read(0xCCBC, 2)
        self.run()
        self.wait_break(BP_MOVE, 30, moved_from=before)


def expected_bytes(addr, n):
    if not os.path.exists(UNPACKED):
        return None
    b = open(UNPACKED, "rb").read()
    hdr = struct.unpack_from("<H", b, 8)[0] * 16
    o = hdr + (addr[0] - 0x1000) * 16 + addr[1]
    return b[o:o + n]


ATTEMPTS = 3


def play(conf, scn, scn_path, out_path, keep):
    with DosboxX(conf, GAME_DIR) as d:
        o = Oracle(d)
        o.load_slot(scn.slot)
        managed = scenario.all_scancodes(scn)
        with open(out_path, "w") as out:
            out.write("# oracle: original game, scenario %s\n" % os.path.basename(scn_path))
            frame0 = o.state()["frame"]
            for ev in scenario.frames(scn):
                st = o.state()
                st["frame"] = (st["frame"] - frame0) & 0xFFFF
                for col, row in scn.watch:
                    st["cell_%d_%d" % (col, row)] = o.read_cell(col, row)
                out.write(scenario.format_state(ev.label, ev.index, st) + "\n")
                for col, row, value in ev.actions:
                    o.set_cell(col, row, value)
                if ev.keys is None:
                    break
                o.set_keys(ev.keys, managed)
                o.step()
        if keep:
            d.exec("BPDEL *")
            o.set_keys(set(), managed)
            o.run()
            input("DOSBox-X left running; press Enter to close it ")


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    keep = "--keep" in sys.argv
    if len(args) != 2:
        print(__doc__)
        return 2
    scn = scenario.load(args[0])
    out_path = os.path.abspath(args[1])
    conf = make_conf(autotype=AUTOTYPE_TO_LOAD)
    # DOSBox-X's control connection occasionally stops answering (a timeout
    # in the first seconds of a run); a fresh DOSBox-X then works, so a run
    # is retried a few times before giving up.
    for attempt in range(1, ATTEMPTS + 1):
        try:
            play(conf, scn, args[0], out_path, keep)
            break
        except DosboxError as e:
            if attempt == ATTEMPTS:
                raise
            print("attempt %d failed (%s); retrying" % (attempt, e), file=sys.stderr)
    print("wrote", out_path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
