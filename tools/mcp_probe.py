"""Probe DOSBox-X's debugger control protocol ("mcp_server" option).

DOSBox-X connects *out* to 127.0.0.1:<port>. This script listens there,
starts DOSBox-X at a plain DOS prompt, sends each message given on the
command line (one line each) and logs the replies.

Known so far: requests are single lines "REQ <id> <PING|BREAK|EXEC> [args]".

usage: python -I mcp_probe.py <dosbox-x.exe> "<message>" ["<message>" ...]
"""
import os
import socket
import subprocess
import sys
import tempfile
import time

PORT = 47777


def recv_all(conn, wait=1.5):
    conn.settimeout(wait)
    chunks = []
    try:
        while True:
            b = conn.recv(65536)
            if not b:
                break
            chunks.append(b)
    except socket.timeout:
        pass
    return b"".join(chunks)


def main():
    exe, messages = sys.argv[1], sys.argv[2:]
    conf = os.path.join(tempfile.mkdtemp(), "probe.conf")
    with open(conf, "w") as f:
        f.write("[dosbox]\nmcp_server = %d\n[sdl]\noutput = surface\n[autoexec]\necho probe\n" % PORT)

    srv = socket.socket()
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", PORT))
    srv.listen(1)
    srv.settimeout(30)
    proc = subprocess.Popen([exe, "-nopromptfolder", "-conf", conf])
    try:
        conn, _ = srv.accept()
        print("connected")
        print("initial:", recv_all(conn, 3))
        for m in messages:
            msg = (m + "\n").encode()
            conn.sendall(msg)
            print(">>", msg)
            print("<<", recv_all(conn, 2))
        conn.close()
    finally:
        time.sleep(0.5)
        proc.kill()
        srv.close()


if __name__ == "__main__":
    main()
