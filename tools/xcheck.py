"""Compare the Rust data readers with the C reference readers on every game
file: both decode everything and write a manifest of hashes (C:
build/xcheck.exe, Rust: target/release/arenatool.exe manifest); the
manifests must be identical.

    python -I xcheck.py [data dir]        (default: run/ARENA)
"""
import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))


def main():
    data = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "run", "ARENA")
    out = os.path.join(ROOT, "run", "xcheck")
    os.makedirs(out, exist_ok=True)
    c_txt = os.path.join(out, "manifest_c.txt")
    rs_txt = os.path.join(out, "manifest_rs.txt")
    subprocess.run([os.path.join(ROOT, "build", "xcheck.exe"), data, c_txt], check=True)
    subprocess.run([os.path.join(ROOT, "target", "release", "arenatool.exe"), "manifest", data, rs_txt], check=True)
    a = open(c_txt).read().splitlines()
    b = open(rs_txt).read().splitlines()
    diffs = [(x, y) for x, y in zip(a, b) if x != y]
    if len(a) != len(b):
        print("line counts differ: C %d, Rust %d" % (len(a), len(b)))
    for x, y in diffs[:20]:
        print("C:    " + x)
        print("Rust: " + y)
    kinds = {}
    for line in a:
        kinds[line.split()[0]] = kinds.get(line.split()[0], 0) + 1
    print("checked: " + ", ".join("%s %d" % kv for kv in sorted(kinds.items())))
    if diffs or len(a) != len(b):
        print("MISMATCH: %d lines differ" % len(diffs))
        return 1
    print("MATCH: the Rust readers decode every file exactly like the C readers")
    return 0


if __name__ == "__main__":
    sys.exit(main())
