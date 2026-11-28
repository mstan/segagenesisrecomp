#!/usr/bin/env python3
"""
tcp_cmd.py - one-shot JSON command to a running runner's cmd_server.

Usage:
  python tcp_cmd.py <port> '<json>'
  python tcp_cmd.py 4390 '{"id":1,"cmd":"audio_stats"}'
"""
from genesis_tcp import request_raw
import sys


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    port = int(sys.argv[1])
    payload = sys.argv[2].strip()
    print(request_raw(port, payload))


if __name__ == "__main__":
    main()
