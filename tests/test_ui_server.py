"""
Integration tests for src/ui/server.py and the JSON mode of bin/banker.
Run with: python3 -m unittest tests/test_ui_server.py  (or: make test)
"""

import json
import socket
import subprocess
import sys
import time
import unittest
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class ServerTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.port = free_port()
        cls.proc = subprocess.Popen(
            [sys.executable, str(ROOT / "src/ui/server.py"),
             "--port", str(cls.port)],
            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        cls.base = f"http://127.0.0.1:{cls.port}"
        for _ in range(50):
            try:
                urllib.request.urlopen(cls.base + "/api/state", timeout=1)
                return
            except OSError:
                time.sleep(0.1)
        cls.proc.kill()
        raise RuntimeError("server did not start: " +
                           cls.proc.stderr.read().decode())

    @classmethod
    def tearDownClass(cls):
        cls.proc.terminate()
        cls.proc.wait()
        cls.proc.stderr.close()

    def setUp(self):
        self.post("/api/restart")

    def get(self, path):
        with urllib.request.urlopen(self.base + path, timeout=5) as r:
            return json.loads(r.read())

    def post(self, path, body=None):
        data = json.dumps(body or {}).encode()
        req = urllib.request.Request(
            self.base + path, data=data,
            headers={"Content-Type": "application/json"})
        with urllib.request.urlopen(req, timeout=5) as r:
            return json.loads(r.read())

    def cmd(self, command):
        return self.post("/api/command", {"command": command})

    def test_initial_state_is_textbook_example(self):
        r = self.get("/api/state")
        self.assertEqual(r["outcome"], "ready")
        self.assertEqual(r["state"]["available"], [3, 3, 2])
        self.assertTrue(r["safety"]["safe"])
        self.assertEqual(r["safety"]["sequence"], [1, 3, 4, 0, 2])

    def test_textbook_request_sequence(self):
        r = self.cmd("RQ 1 1 0 2")
        self.assertEqual(r["outcome"], "granted")
        self.assertEqual(r["state"]["available"], [2, 3, 0])

        r = self.cmd("RQ 4 3 3 0")
        self.assertEqual((r["outcome"], r["reason"]), ("denied", "unavailable"))
        self.assertIsNone(r["trial"])

        r = self.cmd("RQ 0 0 2 0")
        self.assertEqual((r["outcome"], r["reason"]), ("denied", "unsafe"))
        self.assertEqual(r["state"]["available"], [2, 3, 0])
        self.assertFalse(r["trial"]["safety"]["safe"])
        self.assertEqual(r["trial"]["state"]["available"], [2, 1, 0])

    def test_release_and_errors(self):
        self.assertEqual(self.cmd("RL 1 2 0 0")["outcome"], "released")
        r = self.cmd("RL 1 5 0 0")
        self.assertEqual((r["outcome"], r["reason"]),
                         ("denied", "exceeds_allocation"))
        self.assertEqual(self.cmd("RQ 0 a 0 0")["outcome"], "error")
        self.assertEqual(self.cmd("XYZ")["outcome"], "error")

    def test_newline_cannot_inject_second_command(self):
        r = self.cmd("RQ 1 1 0 2\nRQ 1 0 0 0")
        self.assertEqual(r["outcome"], "error")
        self.assertEqual(self.get("/api/state")["state"]["available"],
                         [3, 3, 2])

    def test_quit_is_rejected(self):
        with self.assertRaises(urllib.error.HTTPError) as ctx:
            self.cmd("QUIT")
        self.assertEqual(ctx.exception.code, 400)
        ctx.exception.close()
        self.assertEqual(self.cmd("*")["outcome"], "ok")

    def test_batch_stops_at_first_error(self):
        results = self.post("/api/batch", {
            "commands": ["AVAIL 5 5 5", "ALLOC 0 9 9 9", "SAVE"],
            "stop_on_error": True})
        self.assertEqual([r["outcome"] for r in results], ["ok", "error"])

    def test_every_example_loads_without_error(self):
        examples = self.get("/api/examples")
        self.assertGreaterEqual(len(examples), 6)
        for ex in examples:
            with self.subTest(example=ex["id"]):
                results = self.post("/api/batch", {
                    "commands": ex["commands"], "stop_on_error": True})
                self.assertEqual(len(results), len(ex["commands"]))
                self.assertNotIn("error", [r["outcome"] for r in results])

    def test_static_page_is_served(self):
        with urllib.request.urlopen(self.base + "/", timeout=5) as r:
            self.assertIn(b"Banker's Algorithm", r.read())


if __name__ == "__main__":
    unittest.main()
