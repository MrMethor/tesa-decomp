"""Behaviour scenarios: shared input scripts for the original game (oracle.py)
and the port (the C engine reads the same files, src/engine/scenario.c).

A scenario is a text file, one command per line, '#' starts a comment:

    save <slot>             start from save slot <slot> (SAVEGAME.<slot>)
    hold <key>[+<key>...] <frames>
                            hold the keys for that many 3D-loop frames
    wait <frames>           no keys held
    check <label>           name the state at this point
    end                     stop (optional)

Time is counted in 3D-loop frames, not wall-clock time: both programs are
stepped frame by frame, so equal scripts mean equal simulation steps.
Key names: up down left right lshift rshift ctrl alt space enter esc tab,
a-z, 0-9, f1-f10.

State files (written by oracle.py and by the port) hold one line per frame:

    <frame> <label|-> key=value key=value ...

The line for frame n is the state before frame n's input is applied; the
last line is the state after the last frame.
"""
import collections

SCANCODES = {
    "esc": 0x01, "tab": 0x0F, "enter": 0x1C, "ctrl": 0x1D, "lshift": 0x2A,
    "rshift": 0x36, "alt": 0x38, "space": 0x39,
    "up": 0x48, "left": 0x4B, "right": 0x4D, "down": 0x50,
}
for i, ch in enumerate("1234567890"):
    SCANCODES[ch] = 0x02 + i
for row, base in (("qwertyuiop", 0x10), ("asdfghjkl", 0x1E), ("zxcvbnm", 0x2C)):
    for i, ch in enumerate(row):
        SCANCODES[ch] = base + i
for i in range(10):
    SCANCODES["f%d" % (i + 1)] = 0x3B + i

Scenario = collections.namedtuple("Scenario", "slot steps")
Step = collections.namedtuple("Step", "kind keys frames label")
Frame = collections.namedtuple("Frame", "index keys label")


class ScenarioError(Exception):
    pass


def parse_keys(text):
    keys = set()
    for name in text.lower().split("+"):
        if name not in SCANCODES:
            raise ScenarioError("unknown key %r" % name)
        keys.add(SCANCODES[name])
    return frozenset(keys)


def load(path):
    slot = None
    steps = []
    with open(path) as f:
        for n, raw in enumerate(f, 1):
            line = raw.split("#", 1)[0].split()
            if not line:
                continue
            cmd, args = line[0].lower(), line[1:]
            try:
                if cmd == "save":
                    slot = int(args[0])
                elif cmd == "hold":
                    steps.append(Step("hold", parse_keys(args[0]), int(args[1]), None))
                elif cmd == "wait":
                    steps.append(Step("hold", frozenset(), int(args[0]), None))
                elif cmd == "check":
                    steps.append(Step("check", None, 0, args[0]))
                elif cmd == "end":
                    break
                else:
                    raise ScenarioError("unknown command %r" % cmd)
            except (IndexError, ValueError) as e:
                raise ScenarioError("%s:%d: %s" % (path, n, e))
    if slot is None:
        raise ScenarioError("%s: no 'save' line" % path)
    return Scenario(slot, steps)


def frames(scn):
    """Yield one Frame per simulated frame, then a final Frame with keys None."""
    index = 0
    label = None
    for st in scn.steps:
        if st.kind == "check":
            label = st.label
            continue
        for _ in range(st.frames):
            yield Frame(index, st.keys, label)
            label = None
            index += 1
    yield Frame(index, None, label)


def all_scancodes(scn):
    out = set()
    for st in scn.steps:
        if st.keys:
            out |= st.keys
    return out


FIELD_ORDER = ["x", "y", "z", "angle", "level", "frame", "lflags", "mflags"]


def format_state(label, index, st):
    keys = [k for k in FIELD_ORDER if k in st] + sorted(k for k in st if k not in FIELD_ORDER)
    return "%d %s %s" % (index, label or "-", " ".join("%s=%d" % (k, st[k]) for k in keys))


def parse_state_file(path):
    rows = []
    with open(path) as f:
        for raw in f:
            if raw.startswith("#") or not raw.strip():
                continue
            parts = raw.split()
            st = {}
            for kv in parts[2:]:
                k, v = kv.split("=", 1)
                st[k] = int(v)
            rows.append((int(parts[0]), None if parts[1] == "-" else parts[1], st))
    return rows
