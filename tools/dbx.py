"""Drive DOSBox-X's debugger over its control protocol ("mcp_server").

DOSBox-X connects out to 127.0.0.1:<port>. Requests are single lines:
    REQ <id> PING | BREAK | EXEC <debugger command>
Replies:
    BEGIN <id> OK|ERR
    <text lines>
    END <id>
DOSBox-X may also send unsolicited "BEGIN <n> ... event" blocks (e.g. when a
breakpoint hits); those are collected in DosboxX.events.

Typical use (from Python):
    from dbx import DosboxX
    with DosboxX(conf_text, workdir) as d:
        d.brk()
        d.exec("BP 1000:946A")
        ...
"""
import os
import socket
import subprocess
import time

DOSBOX_EXE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools",
                          "dosbox-x", "bin", "x64", "Release", "dosbox-x.exe")


class DosboxError(Exception):
    pass


class DosboxX:
    def __init__(self, conf_text, workdir, port=47777, exe=DOSBOX_EXE):
        self.workdir = os.path.abspath(workdir)
        self.port = port
        self.exe = os.path.abspath(exe)
        self.conf_path = os.path.join(self.workdir, "dbx.conf")
        with open(self.conf_path, "w") as f:
            f.write(conf_text.replace("@MCP_PORT@", str(port)))
        self.events = []
        self._buf = b""
        self._id = 100
        self.proc = None
        self.conn = None

    # -- lifecycle -----------------------------------------------------
    def __enter__(self):
        self.start()
        return self

    def __exit__(self, *exc):
        self.stop()

    def start(self, timeout=60):
        srv = socket.socket()
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind(("127.0.0.1", self.port))
        srv.listen(1)
        srv.settimeout(timeout)
        self.proc = subprocess.Popen([self.exe, "-nopromptfolder", "-conf", self.conf_path],
                                     cwd=self.workdir)
        try:
            self.conn, _ = srv.accept()
        finally:
            srv.close()

    def stop(self):
        if self.conn:
            try:
                self.conn.close()
            except OSError:
                pass
            self.conn = None
        if self.proc and self.proc.poll() is None:
            self.proc.kill()
            self.proc.wait(10)

    # -- protocol ------------------------------------------------------
    def _read_block(self, timeout):
        """Return (id, status, lines) of the next complete BEGIN..END block."""
        deadline = time.time() + timeout
        while True:
            start = self._buf.find(b"BEGIN ")
            if start >= 0:
                head_end = self._buf.find(b"\n", start)
                if head_end >= 0:
                    head = self._buf[start:head_end].decode("latin1").split()
                    end_tag = ("\nEND %s\n" % head[1]).encode()
                    end = self._buf.find(end_tag, head_end)
                    if end >= 0:
                        body = self._buf[head_end + 1:end].decode("latin1")
                        self._buf = self._buf[end + len(end_tag):]
                        return head[1], " ".join(head[2:]), body.split("\n") if body else []
            left = deadline - time.time()
            if left <= 0:
                raise DosboxError("timeout waiting for reply")
            self.conn.settimeout(left)
            try:
                chunk = self.conn.recv(65536)
            except socket.timeout:
                raise DosboxError("timeout waiting for reply")
            if not chunk:
                raise DosboxError("DOSBox-X closed the connection")
            self._buf += chunk

    def request(self, kind, arg="", timeout=30):
        self._id += 1
        rid = str(self._id)
        line = "REQ %s %s%s\n" % (rid, kind, (" " + arg) if arg else "")
        self.conn.sendall(line.encode("latin1"))
        while True:
            bid, status, lines = self._read_block(timeout)
            if bid == rid:
                if not status.startswith("OK"):
                    raise DosboxError("%s %s -> %s: %s" % (kind, arg, status, " / ".join(lines)))
                return lines
            self.events.append((bid, status, lines))

    def ping(self):
        return self.request("PING")

    def brk(self):
        return self.request("BREAK")

    def exec(self, cmd, timeout=30):
        return self.request("EXEC", cmd, timeout)

    def wait_event(self, timeout):
        """Wait for an unsolicited block (e.g. a breakpoint hit)."""
        if self.events:
            return self.events.pop(0)
        return self._read_block(timeout)

    # -- helpers -------------------------------------------------------
    def dump(self, segoff, length, name="dbx_dump.bin"):
        """MEMDUMPBIN into the work dir and return the bytes."""
        path = os.path.join(self.workdir, name)
        if os.path.exists(path):
            os.remove(path)
        self.exec("MEMDUMPBIN %s %X %s" % (segoff, length, name))
        # DOSBox-X upper-cases nothing on Windows, but be tolerant
        for cand in (name, name.upper(), name.lower()):
            p = os.path.join(self.workdir, cand)
            if os.path.exists(p):
                with open(p, "rb") as f:
                    return f.read()
        raise DosboxError("dump file not written: " + name)
