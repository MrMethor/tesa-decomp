"""How much of ACD.EXE has been studied: an original function counts when an
address inside it is cited in the hand-written analysis (docs/*.md,
src/**/*.c|h, PROGRESS.md, decomp/notes/*.md). Generated listings are not
counted. A citation means the function was read for a specific finding, not
that all of it is understood.

    python -I coverage.py [--markdown]

Needs the local Ghidra exports (decomp/raw/functions.csv) and
decomp/modules.csv. The subsystem split uses the automatic module labels,
so it is approximate (e.g. the player-input code sits in a segment the
labeller called "spelleffect").
"""
import bisect
import csv
import glob
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

GROUPS = [
    ("Levels, maps, automap", ["levelinf", "mifload", "rmdload", "automap", "automapio", "provmap",
                               "mapname", "citydata", "misc"]),
    ("3D engine, movement, rendering", ["engine", "wateranim", "sky", "bodysprite", "spscale", "video",
                                        "decomp", "hud", "weather", "spelleffect"]),
    ("Saves and game state", ["saveload", "startup2", "init", "status", "calendar", "cmos"]),
    ("Magic", ["spellmaker", "spellcast", "spellbook"]),
    ("Runtime library, memory, files", ["rtl_file", "rtl_mem", "rtl_fmt", "ems", "bsa", "startup",
                                        "timer", "text", "textproc"]),
    ("Quests", ["quest", "mainquest", "logbook", "cdspeech"]),
    ("Sound and music", ["sound", "sample", "musicsel"]),
    ("Towns, travel, NPCs, shops", ["townsvc", "npctalk", "travel", "shop", "dialog", "palace", "camp",
                                    "rest", "popup"]),
    ("Creatures, combat, items", ["creature", "combat", "combatres", "pickup", "loot", "itemname",
                                  "levelup", "fatigue", "attrib"]),
    ("Character creation, menus, input", ["chargen", "classquiz", "naming", "intro", "input", "kbd"]),
]
CITE = re.compile(r"(?<![0-9A-Za-z:])([1-4][0-9a-fA-F]{3}):([0-9a-fA-F]{4})(?![0-9a-fA-F])")


def load_functions():
    funcs = []
    with open(os.path.join(ROOT, "decomp", "raw", "functions.csv")) as f:
        for row in csv.DictReader(f):
            seg, off = (int(v, 16) for v in row["address"].split(":"))
            funcs.append((seg, off, int(row["size"])))
    funcs.sort()
    return funcs


def load_modules():
    mods = []
    with open(os.path.join(ROOT, "decomp", "modules.csv")) as f:
        for row in csv.DictReader(f):
            mods.append((int(row["seg"], 16), int(row["start"], 16), int(row["end"], 16), row["prefix"]))
    return mods


def module_of(mods, seg, off):
    for s, a, b, name in mods:
        if s == seg and a <= off < b:
            return name
    return "?"


def cited_addresses():
    files = (glob.glob(os.path.join(ROOT, "docs", "*.md")) +
             glob.glob(os.path.join(ROOT, "src", "**", "*.[ch]"), recursive=True) +
             glob.glob(os.path.join(ROOT, "decomp", "notes", "*.md")) +
             [os.path.join(ROOT, "PROGRESS.md")])
    out = set()
    for path in files:
        text = open(path, encoding="utf-8", errors="replace").read()
        for m in CITE.finditer(text):
            if text[max(0, m.start() - 7):m.start()].upper().endswith("DGROUP:"):
                continue
            out.add((int(m.group(1), 16), int(m.group(2), 16)))
    return out


def main():
    funcs = load_functions()
    mods = load_modules()
    keys = [(s, o) for s, o, _ in funcs]
    studied = set()
    for seg, off in cited_addresses():
        i = bisect.bisect_right(keys, (seg, off)) - 1
        if i >= 0:
            s, o, size = funcs[i]
            if s == seg and o <= off < o + max(size, 1):
                studied.add(i)
    group_of = {}
    for g, names in GROUPS:
        for n in names:
            group_of[n] = g
    stats = {}
    for i, (s, o, size) in enumerate(funcs):
        g = group_of.get(module_of(mods, s, o), "Unlabelled segments")
        st = stats.setdefault(g, [0, 0, 0, 0])
        st[0] += 1
        st[1] += size
        if i in studied:
            st[2] += 1
            st[3] += size
    total = [sum(v[k] for v in stats.values()) for k in range(4)]
    md = "--markdown" in sys.argv
    rows = sorted(stats.items(), key=lambda kv: -kv[1][3] / max(kv[1][1], 1))
    if md:
        print("| Subsystem | Functions | Studied functions | Studied code bytes |")
        print("|---|---|---|---|")
    for g, (n, b, sn, sb) in rows + [("Total", total)]:
        if md:
            print("| %s | %s | %d (%.1f%%) | %s%.1f%%%s |" % (
                g if g != "Total" else "**Total**", "{:,}".format(n) if g != "Total" else "**{:,}**".format(n),
                sn, 100.0 * sn / n, "**" if g == "Total" else "", 100.0 * sb / b, "**" if g == "Total" else ""))
        else:
            print("%-34s %5d fns  %4d studied (%5.1f%%)  bytes %6d / %6d (%5.1f%%)" % (
                g, n, sn, 100.0 * sn / n, sb, b, 100.0 * sb / b))
    return 0


if __name__ == "__main__":
    sys.exit(main())
