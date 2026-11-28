import json
from pathlib import Path
import socket
import sys
import threading
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from genesis_tcp import Connection, request


class Server:
    def __init__(self, handler):
        self.listener = socket.socket()
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen(1)
        self.port = self.listener.getsockname()[1]
        self.errors = []
        def serve():
            try:
                with self.listener, self.listener.accept()[0] as client:
                    handler(client)
            except BaseException as error:
                self.errors.append(error)
        self.thread = threading.Thread(target=serve)

    def __enter__(self):
        self.thread.start()
        return self

    def __exit__(self, *_):
        self.thread.join()
        if self.errors:
            raise self.errors[0]


class GenesisTcpTests(unittest.TestCase):
    def test_fragmented_large_response(self):
        value = {"ok": True, "data": "x" * 150000}
        def handler(client):
            self.assertEqual(json.loads(client.makefile("rb").readline()), {"cmd": "ping", "id": 7})
            payload = (json.dumps(value) + "\n").encode()
            for start in range(0, len(payload), 997):
                client.sendall(payload[start:start + 997])
        with Server(handler) as server:
            self.assertEqual(request(server.port, {"cmd": "ping", "id": 7}), value)

    def test_session_retains_next_frame(self):
        def handler(client):
            stream = client.makefile("rb")
            stream.readline()
            client.sendall(b'{"id":1}\n{"id":2}\n')
            stream.readline()
        with Server(handler) as server, Connection(server.port) as connection:
            self.assertEqual(connection.query({"id": 1}), {"id": 1})
            self.assertEqual(connection.query({"id": 2}), {"id": 2})
        self.assertIsNone(connection.socket)

    def test_partial_eof_is_an_error(self):
        def handler(client):
            client.recv(100)
            client.sendall(b'{"incomplete":')
        with Server(handler) as server:
            with self.assertRaises(EOFError):
                request(server.port, {"cmd": "ping"})

    def test_size_guard(self):
        def handler(client):
            client.recv(100)
            client.sendall(b'"too large"\n')
        with Server(handler) as server:
            with self.assertRaises(ValueError):
                request(server.port, {}, max_response=4)

    def test_multiline_command_rejected_before_send(self):
        def handler(client):
            self.assertEqual(client.recv(100), b'')
        with Server(handler) as server, Connection(server.port) as connection:
            with self.assertRaises(ValueError):
                connection.query_raw('{}\n{}')

    def test_timeout_reports_without_process_side_effects(self):
        # A request timeout propagates. The peer remains available to complete
        # its work and the library owns no game process or termination action.
        ready = threading.Event()
        def handler(client):
            client.recv(100)
            ready.wait()
        with Server(handler) as server:
            try:
                with self.assertRaises(TimeoutError):
                    request(server.port, {}, timeout=0.02)
            finally:
                ready.set()

if __name__ == "__main__":
    unittest.main()
