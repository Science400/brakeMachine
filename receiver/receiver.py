#!/usr/bin/env python3
"""
brakeMachine Receiver — saves weight data dumps from the ESP32 as .tsv files.

Usage:
    python receiver.py
    python receiver.py --port 5000 --dir ./dumps
    python receiver.py --port 5000 --dir "Z:\\WeightData"

No dependencies beyond Python 3.7+ stdlib.
"""
import argparse
import datetime
import logging
import os
from http.server import BaseHTTPRequestHandler, HTTPServer

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s  %(levelname)-8s  %(message)s",
    datefmt="%Y-%m-%d %H:%M:%S",
)
log = logging.getLogger("receiver")

MAX_BODY = 64 * 1024  # 64 KB safety limit


class UploadHandler(BaseHTTPRequestHandler):
    save_dir: str = "./dumps"

    def do_POST(self):
        if self.path != "/upload":
            self.send_error(404, "Not found")
            return

        content_length = int(self.headers.get("Content-Length", 0))
        if content_length == 0:
            self.send_error(400, "Empty body")
            return
        if content_length > MAX_BODY:
            self.send_error(413, "Payload too large")
            return

        body = self.rfile.read(content_length)

        device = self.headers.get("X-Device-Name", "unknown")
        ts_hdr = self.headers.get("X-Timestamp", "")
        dump_id = self.headers.get("X-Dump-Id", "0")

        try:
            ts = datetime.datetime.fromisoformat(ts_hdr)
        except (ValueError, TypeError):
            ts = datetime.datetime.now()

        base = f"{ts.strftime('%Y%m%d_%H%M%S')}_{device}_{dump_id}"
        os.makedirs(self.save_dir, exist_ok=True)

        # Avoid overwriting if same dump is retried
        filepath = os.path.join(self.save_dir, f"{base}.tsv")
        if os.path.exists(filepath):
            # Check if content is identical — skip duplicate
            with open(filepath, "rb") as existing:
                if existing.read() == body:
                    log.info("Skip   %-45s  (duplicate)", f"{base}.tsv")
                    self.send_response(200)
                    self.send_header("Content-Type", "text/plain")
                    self.end_headers()
                    self.wfile.write(b"OK")
                    return

            # Different content with same name — add suffix
            n = 2
            while os.path.exists(filepath):
                filepath = os.path.join(self.save_dir, f"{base}_{n}.tsv")
                n += 1

        with open(filepath, "wb") as f:
            f.write(body)
        filename = os.path.basename(filepath)

        size_kb = len(body) / 1024
        log.info("Saved  %-45s  (%.1f KB)  from %s", filename, size_kb, self.client_address[0])

        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.end_headers()
        self.wfile.write(b"OK")

    def do_GET(self):
        if self.path == "/":
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.end_headers()
            self.wfile.write(b"brakeMachine receiver is running.\n")
        else:
            self.send_error(404, "Not found")

    def log_message(self, format, *args):
        pass  # Suppress default access log — we use our own


def main():
    parser = argparse.ArgumentParser(description="brakeMachine HTTP receiver")
    parser.add_argument("--port", type=int, default=5000, help="Port (default: 5000)")
    parser.add_argument("--dir", default="./dumps", help="Save directory (default: ./dumps)")
    parser.add_argument("--host", default="0.0.0.0", help="Bind address (default: 0.0.0.0)")
    args = parser.parse_args()

    UploadHandler.save_dir = args.dir
    os.makedirs(args.dir, exist_ok=True)

    server = HTTPServer((args.host, args.port), UploadHandler)
    log.info("Listening on %s:%d", args.host, args.port)
    log.info("Saving dumps to: %s", os.path.abspath(args.dir))
    log.info("Press Ctrl+C to stop")

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        log.info("Stopped.")


if __name__ == "__main__":
    main()
