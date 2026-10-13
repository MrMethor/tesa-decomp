"""Build decomp/names.csv (address,name) for ApplyNames.java.

Every function gets <module prefix>_<offset> from decomp/modules.csv.
Functions whose only system call identifies them get a specific name
(dos_read, ems_map_pages, ...). One-call C wrappers around asm routines are
named <callee name>_c. Hand-picked names in decomp/names_manual.csv win.

usage: python -I make_names.py   (run from the project root)
"""
import csv
import os

RAW = "decomp/raw"

DOS = {"3C": "dos_create", "3D": "dos_open", "3E": "dos_close", "3F": "dos_read",
       "40": "dos_write", "41": "dos_delete", "42": "dos_lseek", "4E": "dos_findfirst",
       "4F": "dos_findnext", "19": "dos_getdrive", "1A": "dos_setdta", "2F": "dos_getdta",
       "4A": "dos_resize_block", "09": "dos_print", "36": "dos_diskfree"}
EMS = {"40": "ems_status", "41": "ems_page_frame", "42": "ems_page_count",
       "43": "ems_alloc", "44": "ems_map_pages", "45": "ems_free", "46": "ems_version",
       "4E": "ems_page_map"}
BIOS10 = {"00": "vid_set_mode", "02": "vid_set_cursor", "03": "vid_get_cursor",
          "06": "vid_scroll", "09": "vid_write_char", "0F": "vid_get_mode"}


def key(a):
    s, o = a.split(":")
    return int(s, 16), int(o, 16)


def main():
    mods = list(csv.DictReader(open("decomp/modules.csv")))
    feat = {r["address"]: r for r in csv.DictReader(open(f"{RAW}/features.csv"))}
    calls = list(csv.DictReader(open(f"{RAW}/calls.csv")))
    out_calls = {}
    for c in calls:
        out_calls.setdefault(c["caller"], []).append(c["callee"])

    def prefix(a):
        s, o = key(a)
        for m in mods:
            if int(m["seg"], 16) == s and int(m["start"], 16) <= o < int(m["end"], 16):
                return m["prefix"]
        return "fn"

    names = {}
    for a in sorted(feat, key=key):
        names[a] = "%s_%04x" % (prefix(a), key(a)[1])
        ints = [i for i in feat[a]["ints"].split(";") if i]
        specific = None
        if len(ints) == 1:
            n, _, ah = ints[0].partition(":")
            if n == "21":
                specific = DOS.get(ah)
            elif n == "67":
                specific = EMS.get(ah)
            elif n == "10":
                specific = BIOS10.get(ah)
            elif n == "33" and ah == "00":
                specific = "mouse_reset"
        if specific:
            names[a] = specific

    # C wrappers: small framed function whose only call is to an asm routine
    for a, f in feat.items():
        callees = out_calls.get(a, [])
        if (f["has_frame"] == "true" and int(f["size"]) <= 30 and len(callees) == 1
                and callees[0] in names):
            names[a] = names[callees[0]] + "_c"

    if os.path.exists("decomp/names_manual.csv"):
        for r in csv.DictReader(open("decomp/names_manual.csv")):
            names[r["address"]] = r["name"]

    # make names unique
    seen = {}
    for a in sorted(names, key=key):
        n = names[a]
        if n in seen:
            names[a] = "%s_%04x" % (n, key(a)[1])
        seen[names[a]] = a

    with open("decomp/names.csv", "w", newline="") as fo:
        w = csv.writer(fo)
        w.writerow(["address", "name"])
        for a in sorted(names, key=key):
            w.writerow([a, names[a]])
    specific = sum(1 for n in names.values()
                   if n.startswith(("dos_", "ems_", "vid_", "mouse_")) or n.endswith("_c"))
    print("%d names written (%d specific or wrappers)" % (len(names), specific))


if __name__ == "__main__":
    main()
