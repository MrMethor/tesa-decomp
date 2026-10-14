"""Compare two state files (original game vs port) frame by frame.

    python -I statecmp.py <oracle.state> <port.state> [--fields x,y,z,angle,level] [--all]

By default the behavioural fields x, y, z, angle and level, and every
watched map cell (cell_<col>_<row>), are compared exactly; tolerances can be given as name:tol (e.g. x:1). Fields starting
with d_ are diagnostics: shown next to a mismatch, never compared.
Exit code 0 when every compared field matches on every frame.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from scenario import parse_state_file  # noqa: E402

DEFAULT_FIELDS = "x,y,z,angle,level"


def parse_fields(spec):
    out = []
    for item in spec.split(","):
        name, _, tol = item.partition(":")
        out.append((name, int(tol) if tol else 0))
    return out


def compare(a_rows, b_rows, fields, show_all=False, limit=20):
    n = min(len(a_rows), len(b_rows))
    mismatches = []
    first = None
    for i in range(n):
        ia, la, sa = a_rows[i]
        ib, lb, sb = b_rows[i]
        bad = []
        cells = [(k, 0) for k in sa if k.startswith("cell_") and k in sb]
        for name, tol in fields + cells:
            if name not in sa or name not in sb:
                continue
            if abs(sa[name] - sb[name]) > tol:
                bad.append(name)
        if bad:
            mismatches.append((ia, la or lb, bad, sa, sb))
            if first is None:
                first = ia
    lines = []
    if len(a_rows) != len(b_rows):
        lines.append("frame counts differ: %d vs %d" % (len(a_rows), len(b_rows)))
    for ia, label, bad, sa, sb in (mismatches if show_all else mismatches[:limit]):
        diag = sorted(k for k in set(sa) | set(sb) if k.startswith("d_"))
        lines.append("frame %d%s: %s" % (ia, " [%s]" % label if label else "",
                     ", ".join("%s %d vs %d" % (k, sa[k], sb[k]) for k in bad)))
        lines.append("    original: " + " ".join("%s=%s" % (k, sa.get(k)) for k in [f for f, _ in fields] + diag))
        lines.append("    port:     " + " ".join("%s=%s" % (k, sb.get(k)) for k in [f for f, _ in fields] + diag))
    if len(mismatches) > limit and not show_all:
        lines.append("... %d more mismatching frames" % (len(mismatches) - limit))
    return n, mismatches, first, lines


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    fields = parse_fields(DEFAULT_FIELDS)
    for i, a in enumerate(sys.argv):
        if a == "--fields" and i + 1 < len(sys.argv):
            fields = parse_fields(sys.argv[i + 1])
            args = [x for x in args if x != sys.argv[i + 1]]
    if len(args) != 2:
        print(__doc__)
        return 2
    a_rows = parse_state_file(args[0])
    b_rows = parse_state_file(args[1])
    n, mism, first, lines = compare(a_rows, b_rows, fields, "--all" in sys.argv)
    for line in lines:
        print(line)
    names = ",".join(f for f, _ in fields)
    if mism:
        print("MISMATCH: %d of %d frames differ in %s (first at frame %d)" % (len(mism), n, names, first))
        return 1
    if len(a_rows) != len(b_rows):
        return 1
    print("MATCH: %d frames, fields %s" % (n, names))
    return 0


if __name__ == "__main__":
    sys.exit(main())
