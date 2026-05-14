#!/usr/bin/env python3
"""
ESP32 Bridge - Ultra-lightweight HTTP endpoint for ESP32
Exposes minimal JSON response, proxies to main daemon locally.

Purpose: Provide a microcontroller-friendly interface to the usage daemon.
- No redirects
- Minimal headers
- Tiny JSON response
- HTTP/1.0 compatible
- Fast, deterministic response

Usage: python3 esp32-bridge.py
Listen on: http://0.0.0.0:8080
Then: tailscale funnel 8080
Access: https://mac-mini-de-marc.tail29a4b0.ts.net:8080/api/usage
"""

import json
import sys
from http.server import HTTPServer, BaseHTTPRequestHandler
from socketserver import ThreadingMixIn
import urllib.request
import urllib.error

# Local daemon (where the main scraper runs)
DAEMON_LOCAL = "http://127.0.0.1:80"

class ESPBridgeHandler(BaseHTTPRequestHandler):
    """Ultra-simple handler - no fancy logging, just serve the data."""

    def do_GET(self):
        """Handle GET request for /api/usage"""
        if self.path == "/api/usage":
            try:
                # Fetch from local daemon
                url = f"{DAEMON_LOCAL}/api/usage"
                with urllib.request.urlopen(url, timeout=3) as response:
                    data = json.loads(response.read().decode())

                # Build minimal response for ESP32
                # Only include what the device needs
                esp32_response = {
                    "ok": data.get("ok", False),
                    "s": data.get("s", 0),    # session %
                    "w": data.get("w", 0),    # weekly %
                    "sr": data.get("sr", -1), # session reset mins
                    "wr": data.get("wr", -1), # weekly reset mins
                }

                # Send response
                response_json = json.dumps(esp32_response, separators=(',', ':'))
                response_bytes = response_json.encode('utf-8')

                self.send_response(200)
                self.send_header('Content-Type', 'application/json')
                self.send_header('Content-Length', len(response_bytes))
                self.send_header('Connection', 'close')
                self.end_headers()
                self.wfile.write(response_bytes)

            except urllib.error.URLError as e:
                # Daemon unreachable
                self.send_error(503, "Daemon unavailable")
            except json.JSONDecodeError:
                # Invalid JSON from daemon
                self.send_error(502, "Invalid daemon response")
            except Exception as e:
                self.send_error(500, str(e))
        else:
            self.send_error(404)

    def log_message(self, format, *args):
        """Minimal logging - only errors and startup"""
        if "GET" in format or "POST" in format:
            return  # Skip request logs
        print(format % args, file=sys.stderr)


class ThreadedHTTPServer(ThreadingMixIn, HTTPServer):
    """Threaded server for concurrent requests"""
    daemon_threads = True


def main():
    server = ThreadedHTTPServer(("0.0.0.0", 8080), ESPBridgeHandler)
    print("ESP32 Bridge listening on http://0.0.0.0:8080", file=sys.stderr)
    print("Proxying to daemon: " + DAEMON_LOCAL, file=sys.stderr)
    print("Endpoint: GET /api/usage", file=sys.stderr)
    print("\nTo expose via Tailscale Funnel, run:", file=sys.stderr)
    print("  tailscale funnel --bg 8080", file=sys.stderr)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nShutdown", file=sys.stderr)
        server.shutdown()


if __name__ == "__main__":
    main()
