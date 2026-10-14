"""Differential test run: every scenario in tests/scenarios is played in the
original game (tools/oracle.py, results cached in run/oracle/) and in the
port (build/arenarun.exe, results in run/port/), and the states are compared
(tools/statecmp.py).

    python -I difftest.py [--refresh] [--fields x,y,z,angle,level] [name ...]

--refresh replays the original even when a cached result exists (a cached
result is also replayed when the scenario file is newer). The cached
original states are observations of the original game and stay in run/,
which is not committed.
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import statecmp  # noqa: E402
from scenario import parse_state_file  # noqa: E402

ROOT = os.path.abspath(os.path.join(HERE, ".."))
SCN_DIR = os.path.join(ROOT, "tests", "scenarios")
ORACLE_DIR = os.path.join(ROOT, "run", "oracle")
PORT_DIR = os.path.join(ROOT, "run", "port")
ARENARUN = os.path.join(ROOT, "build", "arenarun.exe")
DATA_DIR = os.path.join(ROOT, "run", "ARENA")
EXE = os.path.join(ROOT, "bin", "ACD_unpacked.EXE")


def run_oracle(scn, out):
    print("  original: playing %s ..." % os.path.basename(scn), flush=True)
    r = subprocess.run([sys.executable, "-I", os.path.join(HERE, "oracle.py"), scn, out],
                       capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stdout[-2000:], r.stderr[-2000:])
        return False
    return True


def run_port(scn, out):
    r = subprocess.run([ARENARUN, DATA_DIR, EXE, scn, out], capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stdout, r.stderr)
        return False
    return True


def main():
    args = sys.argv[1:]
    refresh = "--refresh" in args
    fields = statecmp.parse_fields(statecmp.DEFAULT_FIELDS)
    if "--fields" in args:
        fields = statecmp.parse_fields(args[args.index("--fields") + 1])
        args.remove(args[args.index("--fields") + 1])
    names = [a for a in args if not a.startswith("--")]
    if not names:
        names = sorted(f[:-4] for f in os.listdir(SCN_DIR) if f.endswith(".scn"))
    os.makedirs(ORACLE_DIR, exist_ok=True)
    os.makedirs(PORT_DIR, exist_ok=True)
    results = []
    for name in names:
        scn = os.path.join(SCN_DIR, name + ".scn")
        o_state = os.path.join(ORACLE_DIR, name + ".state")
        p_state = os.path.join(PORT_DIR, name + ".state")
        print(name)
        stale = not os.path.exists(o_state) or os.path.getmtime(o_state) < os.path.getmtime(scn)
        if (refresh or stale) and not run_oracle(scn, o_state):
            results.append((name, "ORACLE FAILED", ""))
            continue
        if not run_port(scn, p_state):
            results.append((name, "PORT FAILED", ""))
            continue
        n, mism, first, lines = statecmp.compare(parse_state_file(o_state), parse_state_file(p_state), fields)
        if mism:
            for line in lines[:9]:
                print("  " + line)
            results.append((name, "MISMATCH", "%d of %d frames, first at %d" % (len(mism), n, first)))
        else:
            results.append((name, "match", "%d frames" % n))
    print()
    print("%-20s %-14s %s" % ("scenario", "result", ""))
    for name, res, detail in results:
        print("%-20s %-14s %s" % (name, res, detail))
    ok = sum(1 for r in results if r[1] == "match")
    print("%d of %d scenarios match the original" % (ok, len(results)))
    return 0 if ok == len(results) else 1


if __name__ == "__main__":
    sys.exit(main())
