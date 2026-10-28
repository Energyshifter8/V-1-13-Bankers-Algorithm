#!/usr/bin/env python3
"""
Local web server for the Banker's Algorithm UI.

The server starts the C program (bin/banker) in JSON mode (-j) and forwards
every command from the browser to it. The browser never computes the
algorithm itself: everything it draws comes from the C program's answer.

Only the Python standard library is used.

    python3 src/ui/server.py [--port 8000] [--open]
"""

import argparse
import json
import os
import subprocess
import sys
import threading
import webbrowser
from http import HTTPStatus
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
STATIC = Path(__file__).resolve().parent / "static"
EXAMPLES = ROOT / "examples"
DATA = ROOT / "tests" / "data"

MAX_COMMAND_LENGTH = 1000


def find_binary():
    for name in ("banker", "banker.exe"):
        path = ROOT / "bin" / name
        if path.exists():
            return path
    return None


class Banker:
    """A running bin/banker -j process. One command in, one JSON line out."""

    def __init__(self, binary):
        self.binary = binary
        self.lock = threading.Lock()
        self.proc = None
        self.last = None
        self.start()

    def start(self):
        if self.proc is not None:
            self.proc.kill()
            self.proc.wait()
        # Start with the textbook snapshot of Section 8.6.3.3
        args = [
            str(self.binary), "-j",
            "-f", str(DATA / "book_example_max.txt"),
            "-a", str(DATA / "book_example_alloc.txt"),
            "3", "3", "2",
        ]
        self.proc = subprocess.Popen(
            args, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, text=True, bufsize=1, cwd=ROOT)
        self.last = self._read()

    def _read(self):
        line = self.proc.stdout.readline()
        if not line:
            err = self.proc.stderr.read()
            raise RuntimeError("banker process stopped: " + err.strip())
        return json.loads(line)

    def send(self, command):
        command = command.replace("\r", " ").replace("\n", " ").strip()
        if len(command) > MAX_COMMAND_LENGTH:
            raise ValueError("command is too long")
        if command.split(" ", 1)[0].upper() in ("QUIT", "EXIT"):
            raise ValueError("QUIT is not available in the UI, "
                             "use Restart instead")
        with self.lock:
            if self.proc.poll() is not None:
                self.start()
            self.proc.stdin.write(command + "\n")
            self.proc.stdin.flush()
            self.last = self._read()
            return self.last

    def restart(self):
        with self.lock:
            self.start()
            return self.last


def load_examples():
    """Read examples/*.txt. '# title:' and '# description:' lines are
    metadata, every other non-empty line is a banker command."""
    result = []
    for path in sorted(EXAMPLES.glob("*.txt")):
        item = {"id": path.stem, "title": path.stem, "description": "",
                "commands": []}
        for raw in path.read_text(encoding="utf-8").splitlines():
            line = raw.strip()
            if line.startswith("# title:"):
                item["title"] = line[len("# title:"):].strip()
            elif line.startswith("# description:"):
                item["description"] = line[len("# description:"):].strip()
            elif line and not line.startswith("#"):
                item["commands"].append(line)
        result.append(item)
    return result


class Handler(SimpleHTTPRequestHandler):
    banker = None

    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(STATIC), **kwargs)

    def log_message(self, fmt, *args):
        if "--verbose" in sys.argv:
            super().log_message(fmt, *args)

    def send_json(self, data, status=HTTPStatus.OK):
        body = json.dumps(data).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def read_json(self):
        length = int(self.headers.get("Content-Length") or 0)
        if length > 1_000_000:
            raise ValueError("request is too large")
        raw = self.rfile.read(length) if length else b"{}"
        return json.loads(raw.decode("utf-8"))

    def do_GET(self):
        if self.path == "/api/state":
            self.send_json(self.banker.last)
        elif self.path == "/api/examples":
            self.send_json(load_examples())
        elif self.path.startswith("/api/"):
            self.send_json({"error": "not found"}, HTTPStatus.NOT_FOUND)
        else:
            super().do_GET()

    def do_POST(self):
        try:
            body = self.read_json()
            if self.path == "/api/command":
                self.send_json(self.banker.send(str(body.get("command", ""))))
            elif self.path == "/api/batch":
                commands = body.get("commands", [])
                if not isinstance(commands, list) or len(commands) > 2000:
                    raise ValueError("commands must be a list")
                results = [self.banker.send(str(c)) for c in commands]
                self.send_json(results)
            elif self.path == "/api/restart":
                self.send_json(self.banker.restart())
            else:
                self.send_json({"error": "not found"}, HTTPStatus.NOT_FOUND)
        except (ValueError, json.JSONDecodeError) as exc:
            self.send_json({"error": str(exc)}, HTTPStatus.BAD_REQUEST)
        except RuntimeError as exc:
            self.send_json({"error": str(exc)},
                           HTTPStatus.INTERNAL_SERVER_ERROR)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8000)
    parser.add_argument("--open", action="store_true",
                        help="open the UI in the default web browser")
    parser.add_argument("--verbose", action="store_true",
                        help="log every HTTP request")
    args = parser.parse_args()

    binary = find_binary()
    if binary is None:
        sys.exit("error: bin/banker not found. Build it first with 'make'.")

    Handler.banker = Banker(binary)
    try:
        server = ThreadingHTTPServer((args.host, args.port), Handler)
    except OSError as exc:
        sys.exit(f"error: cannot listen on {args.host}:{args.port}: {exc}\n"
                 "Try another port, e.g. --port 8080")

    url = f"http://{args.host}:{args.port}/"
    print(f"Banker's Algorithm UI running at {url} (Ctrl+C to stop)")
    if args.open:
        webbrowser.open(url)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopping...")
    finally:
        server.server_close()
        Handler.banker.proc.kill()


if __name__ == "__main__":
    os.chdir(ROOT)
    main()
