#!/usr/bin/env python3
"""
tcp_cmd.py - one-shot JSON command to a running runner's cmd_server.

Usage:
  python tcp_cmd.py <port> '<json>'
  python tcp_cmd.py 4390 '{"id":1,"cmd":"audio_stats"}'
"""
import socket
import sys


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    port = int(sys.argv[1])
    payload = sys.argv[2].strip()
    s = socket.create_connection(("127.0.0.1", port), timeout=10)
    s.sendall(payload.encode() + b"\n")
    buf = b""
    while not buf.endswith(b"\n"):
        chunk = s.recv(65536)
        if not chunk:
            break
        buf += chunk
    s.close()
    print(buf.decode(errors="replace").strip())


if __name__ == "__main__":
    main()
