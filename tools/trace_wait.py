"""Start the game (with sound) for the user to play, wait for a trigger file,
then pause and record a CPU trace of the next N instructions.

    python -I trace_wait.py <label> [instructions]

Create run/trace.go to trigger. Results go to run/traces/<label>/:
    logcpu.txt   CS:IP of every executed instruction (LOGC)
    screen.png   what was on screen when triggered
    regs.txt     CS:EIP, DS and SS at the moment of the pause
DOSBox-X is closed afterwards.
"""
import os
import shutil
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from arena_run import GAME_DIR, ROOT, make_conf, read_dac, screenshot  # noqa: E402
from dbx import DosboxX  # noqa: E402

TRIGGER = os.path.join(ROOT, "run", "trace.go")


def main():
    label = sys.argv[1]
    count = int(sys.argv[2]) if len(sys.argv) > 2 else 3000000
    out = os.path.join(ROOT, "run", "traces", label)
    os.makedirs(out, exist_ok=True)
    if os.path.exists(TRIGGER):
        os.remove(TRIGGER)
    with DosboxX(make_conf(sound=True), GAME_DIR) as d:
        print("game running; waiting for", TRIGGER, flush=True)
        while not os.path.exists(TRIGGER):
            if d.proc.poll() is not None:
                print("DOSBox-X was closed before the trigger")
                return 1
            time.sleep(0.5)
        os.remove(TRIGGER)
        d.brk()
        screenshot(d, os.path.join(out, "screen.png"), read_dac(d))
        cs = d.exec("EV CS EIP")
        print("paused at", cs[-1] if cs else "?", flush=True)
        log = os.path.join(GAME_DIR, "LOGCPU.TXT")
        if os.path.exists(log):
            os.remove(log)
        t = time.time()
        # LOGC returns at once; DOSBox-X keeps writing the log while it runs the
        # instructions. Wait until the file stops growing, then copy it.
        d.exec("LOGC %X" % count, timeout=1800)
        last, stable = -1, 0
        while stable < 6:
            time.sleep(0.5)
            size = os.path.getsize(log) if os.path.exists(log) else 0
            stable = stable + 1 if size == last and size > 0 else 0
            last = size
        print("traced (%d bytes of log) in %.1fs" % (last, time.time() - t), flush=True)
        shutil.copyfile(log, os.path.join(out, "logcpu.txt"))
        regs = d.exec("EV DS SS")
        with open(os.path.join(out, "regs.txt"), "w") as f:
            f.write("CS EIP: %s\nDS SS: %s\n" % (cs[-1] if cs else "", regs[-1] if regs else ""))
    print("done ->", out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
