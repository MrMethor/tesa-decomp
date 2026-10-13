"""Play the game copy (run/ARENA) with DOSBox-X logging every DOS file
operation, then summarise which loose files were opened.

    python -I play_logged.py <label>

Play normally, then close the DOSBox-X window. The raw log goes to
run/logs/<label>_fileio.txt and a summary to run/logs/<label>_files.txt.
Files read from inside GLOBAL.BSA only show up as GLOBAL.BSA accesses.
"""
import collections
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from arena_run import GAME_DIR, ROOT, make_conf  # noqa: E402
from dbx import DOSBOX_EXE  # noqa: E402


def summarise(log_path, out_path):
    text = open(log_path, errors="replace").read()
    opens = collections.Counter()
    for m in re.finditer(r"FILES:(?:Special )?file open command \w+ file (\S+)", text):
        name = m.group(1).replace("\\", "/").split("/")[-1].upper()
        opens[name] += 1
    with open(out_path, "w") as f:
        for name, n in sorted(opens.items()):
            f.write("%-14s %d\n" % (name, n))
    return opens


def main():
    label = sys.argv[1] if len(sys.argv) > 1 else "session"
    logs = os.path.join(ROOT, "run", "logs")
    os.makedirs(logs, exist_ok=True)
    log = os.path.join(logs, label + "_fileio.txt")
    conf_path = os.path.join(GAME_DIR, "play_logged.conf")
    with open(conf_path, "w") as f:
        f.write(make_conf(sound=True, fileio_log=log).replace("mcp_server = @MCP_PORT@", "mcp_server = 0"))
    subprocess.run([DOSBOX_EXE, "-nopromptfolder", "-conf", conf_path], cwd=GAME_DIR)
    opens = summarise(log, os.path.join(logs, label + "_files.txt"))
    print("%d distinct files opened; summary in run/logs/%s_files.txt" % (len(opens), label))
    for watch in ("RAND.DAT", "GUARD.DAT"):
        print("%-10s opened %d time(s)" % (watch, opens.get(watch, 0)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
