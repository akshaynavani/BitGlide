#!/usr/bin/env python3
"""Bandwidth-shaped HLS origin for BitGlide scenario tests.

Serves a directory over HTTP/1.1 while emulating a single bottleneck link whose
capacity follows a time-based profile. All concurrent connections share one token
bucket, so parallel prefetch workers compete for the same bandwidth like they would
on a real access link. Optional per-request latency and error injection exercise the
controller's retry and abandonment logic.

Profile JSON:
    {
      "name": "step_down",
      "latency_ms": 40,
      "error_rate": 0.0,
      "steps": [{"at": 0, "kbps": 8000}, {"at": 20, "kbps": 900}],
      "expect": {...}            # used by run_scenarios.py, ignored here
    }
"""

from __future__ import annotations

import argparse
import json
import os
import random
import re
import sys
import threading
import time
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

CHUNK = 8 * 1024


class LinkShaper:
    """Token bucket shared by all connections; refill rate follows the profile."""

    def __init__(self, steps, burst_s: float = 0.05):
        self.steps = sorted(steps, key=lambda s: s["at"]) or [{"at": 0, "kbps": 0}]
        self.start = time.monotonic()
        self.burst_s = burst_s
        self.tokens = 0.0
        self.last = self.start
        self.lock = threading.Lock()

    def rate_bps(self, now: float | None = None) -> float:
        t = (now or time.monotonic()) - self.start
        rate = self.steps[0]["kbps"]
        for step in self.steps:
            if step["at"] <= t:
                rate = step["kbps"]
        return float(rate) * 1000.0

    def consume(self, nbytes: int) -> None:
        """Blocks until nbytes may be sent. kbps <= 0 means unlimited."""
        need = nbytes * 8.0
        while True:
            with self.lock:
                now = time.monotonic()
                rate = self.rate_bps(now)
                if rate <= 0:
                    return
                self.tokens = min(self.tokens + (now - self.last) * rate, rate * self.burst_s + need)
                self.last = now
                if self.tokens >= need:
                    self.tokens -= need
                    return
                wait = (need - self.tokens) / rate
            time.sleep(min(wait, 0.05))


class OriginState:
    def __init__(self, root: Path, profile: dict, log_requests: bool):
        self.root = root
        self.shaper = LinkShaper(profile.get("steps", []))
        self.latency_s = profile.get("latency_ms", 0) / 1000.0
        self.error_rate = float(profile.get("error_rate", 0.0))
        self.log_requests = log_requests
        self.rng = random.Random(profile.get("seed", 1234))
        self.rng_lock = threading.Lock()
        self.requests = 0

    def should_fail(self) -> bool:
        with self.rng_lock:
            self.requests += 1
            return self.rng.random() < self.error_rate


RANGE_RE = re.compile(r"bytes=(\d+)-(\d*)")


def make_handler(state: OriginState):
    class Handler(SimpleHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def __init__(self, *args, **kwargs):
            super().__init__(*args, directory=os.fspath(state.root), **kwargs)

        def log_message(self, fmt, *args):  # noqa: N802 (stdlib signature)
            if state.log_requests:
                sys.stderr.write("[origin %.2fs] %s\n" % (time.monotonic() - state.shaper.start, fmt % args))

        def do_GET(self):  # noqa: N802
            if state.latency_s:
                time.sleep(state.latency_s)
            path = Path(self.translate_path(self.path))
            if not path.is_file():
                self.send_error(404)
                return
            is_segment = path.suffix in (".ts", ".m4s", ".mp4", ".aac")
            if is_segment and state.should_fail():
                self.send_response(503)
                self.send_header("Content-Length", "0")
                self.send_header("Connection", "close")
                self.end_headers()
                return

            size = path.stat().st_size
            start, end, status = 0, size - 1, 200
            m = RANGE_RE.match(self.headers.get("Range", ""))
            if m:
                start = int(m.group(1))
                end = min(int(m.group(2)) if m.group(2) else size - 1, size - 1)
                if start > end:
                    self.send_error(416)
                    return
                status = 206
            length = end - start + 1

            ctype = "application/vnd.apple.mpegurl" if path.suffix == ".m3u8" else "video/mp2t"
            self.send_response(status)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(length))
            if status == 206:
                self.send_header("Content-Range", f"bytes {start}-{end}/{size}")
            self.send_header("Cache-Control", "no-cache")
            self.send_header("Connection", "close")
            self.end_headers()

            try:
                with path.open("rb") as fh:
                    fh.seek(start)
                    remaining = length
                    while remaining > 0:
                        chunk = fh.read(min(CHUNK, remaining))
                        if not chunk:
                            break
                        if is_segment:
                            state.shaper.consume(len(chunk))
                        self.wfile.write(chunk)
                        remaining -= len(chunk)
            except (BrokenPipeError, ConnectionResetError):
                pass  # client abandoned the download
            self.close_connection = True

    return Handler


def load_profile(path: str | None, kbps: float | None) -> dict:
    if path:
        with open(path) as fh:
            return json.load(fh)
    return {"name": "constant", "steps": [{"at": 0, "kbps": kbps or 0}]}


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Bandwidth-shaped HLS origin")
    ap.add_argument("--root", default="media", help="directory to serve")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8080, help="0 picks a free port")
    ap.add_argument("--profile", help="bandwidth profile JSON")
    ap.add_argument("--kbps", type=float, help="constant link rate when no profile is given (0 = unlimited)")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args(argv)

    profile = load_profile(args.profile, args.kbps)
    state = OriginState(Path(args.root).resolve(), profile, args.verbose)
    server = ThreadingHTTPServer((args.host, args.port), make_handler(state))
    server.daemon_threads = True
    host, port = server.server_address[:2]
    # The scenario runner parses this line to discover the port.
    print(f"LISTENING http://{host}:{port}/ profile={profile.get('name', 'custom')}", flush=True)
    try:
        server.serve_forever(poll_interval=0.1)
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
