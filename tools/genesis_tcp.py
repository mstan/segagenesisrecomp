"""Newline-framed JSON transport for Genesis diagnostic clients.

Commands, capture plans and process ownership remain with the caller. A request
failure never stops a game. Connection can retain a session; request helpers
open and close one connection for callers using the original one-shot contract.
"""
import json
import socket


class Connection:
    def __init__(self, port, host="127.0.0.1", timeout=10, max_response=64 * 1024 * 1024):
        self.port, self.host, self.timeout = int(port), host, timeout
        self.max_response = max_response
        self.socket = None
        self.buffer = bytearray()

    def __enter__(self):
        self.socket = socket.create_connection((self.host, self.port), self.timeout)
        return self

    def close(self):
        if self.socket is not None:
            self.socket.close()
            self.socket = None
        self.buffer.clear()

    def __exit__(self, *_):
        self.close()

    def query_raw(self, command):
        if self.socket is None:
            raise RuntimeError("connection is not open")
        payload = command if isinstance(command, str) else json.dumps(command)
        if "\n" in payload or "\r" in payload:
            raise ValueError("a command must occupy one line")
        self.socket.sendall(payload.encode("utf-8") + b"\n")
        while True:
            end = self.buffer.find(b"\n")
            if end >= 0:
                if end > self.max_response:
                    raise ValueError("diagnostic response exceeds max_response")
                line = bytes(self.buffer[:end])
                del self.buffer[:end + 1]
                return line.decode("utf-8", errors="replace").rstrip("\r")
            if len(self.buffer) > self.max_response:
                raise ValueError("diagnostic response exceeds max_response")
            chunk = self.socket.recv(65536)
            if not chunk:
                raise EOFError("diagnostic server closed before a complete response")
            self.buffer.extend(chunk)

    def query(self, command):
        return json.loads(self.query_raw(command))


def request_raw(port, command, **options):
    with Connection(port, **options) as connection:
        return connection.query_raw(command)


def request(port, command, **options):
    return json.loads(request_raw(port, command, **options))
