#!/usr/bin/env python3
"""
WiFi API Daemon - Scrapes Claude usage and serves via HTTP/JSON to ESP32
Opens browser visually, maintains persistent login, updates via API

Usage: ./wifi-api-daemon.py [--port 80] [--host 0.0.0.0]

Listens on http://0.0.0.0:80/api/usage
"""

import json
import time
import sys
import argparse
import logging
import asyncio
import re
import threading
from http.server import HTTPServer, BaseHTTPRequestHandler
from socketserver import ThreadingMixIn
from pathlib import Path
from datetime import datetime, timedelta

try:
    from playwright.async_api import async_playwright
except ImportError:
    print("ERROR: playwright not installed")
    print("Install with: pip3 install playwright")
    print("Then: playwright install")
    sys.exit(1)

STORAGE_DIR = Path.home() / '.config' / 'claude-usage-scraper'
USAGE_URL = 'https://claude.ai/settings/usage'

def parse_weekly_reset_time(text):
    """Parse 'Resets Wed 11:00 PM' format and return minutes until that time."""
    try:
        match = re.search(r'Resets\s+(\w+)\s+(\d{1,2}):(\d{2})\s+(AM|PM)', text)
        if not match:
            return 10080

        day_name, hour_str, min_str, ampm = match.groups()
        hour = int(hour_str)
        minute = int(min_str)

        if ampm == 'PM' and hour != 12:
            hour += 12
        elif ampm == 'AM' and hour == 12:
            hour = 0

        days = {
            'Monday': 0, 'Mon': 0, 'Tuesday': 1, 'Tue': 1,
            'Wednesday': 2, 'Wed': 2, 'Thursday': 3, 'Thu': 3,
            'Friday': 4, 'Fri': 4, 'Saturday': 5, 'Sat': 5,
            'Sunday': 6, 'Sun': 6
        }
        target_day = days.get(day_name)
        if target_day is None:
            return 10080

        now = datetime.now()
        current_day = now.weekday()
        current_time = now.hour * 60 + now.minute
        target_time = hour * 60 + minute

        days_until = (target_day - current_day) % 7
        if days_until == 0 and target_time <= current_time:
            days_until = 7

        if days_until == 0:
            mins_until = target_time - current_time
        else:
            mins_until = (days_until * 1440) + (target_time - current_time)

        return max(0, mins_until)
    except Exception as e:
        return 10080

class UsageCache:
    def __init__(self):
        self.data = {
            "s": 0,
            "sr": -1,
            "w": 0,
            "wr": -1,
            "st": "initializing",
            "ok": False
        }
        self.last_update = 0
        self.page = None
        self.context = None
        self.loop = None  # Will be set to asyncio event loop

    def get_usage(self):
        return self.data

    def update_usage(self, s5h, s7d, sr, wr):
        """Update cached usage data"""
        self.data = {
            "s": max(0, min(100, s5h)),
            "sr": sr,
            "w": max(0, min(100, s7d)),
            "wr": wr,
            "st": "scraper-web",
            "ok": True,
        }
        self.last_update = time.time()

    async def scrape_usage(self):
        """Scrape percentages and reset times from page."""
        try:
            if not self.page:
                return False

            await self.page.wait_for_timeout(2000)
            page_text = await self.page.inner_text('body')

            # Find session % and reset time
            current_idx = page_text.find('Current session')
            weekly_idx = page_text.find('Weekly limits')

            s5h = 0
            sr = 300
            if current_idx >= 0 and weekly_idx >= current_idx:
                session_part = page_text[current_idx:weekly_idx]
                session_match = re.search(r'(\d+)%\s+used', session_part)
                s5h = int(session_match.group(1)) if session_match else 0

                reset_match = re.search(r'Resets in\s+(?:(\d+)\s+d[a-z]*\s+)?(?:(\d+)\s+hr[a-z]*\s+)?(\d+)\s+min', session_part, re.IGNORECASE)
                if reset_match:
                    days = int(reset_match.group(1)) if reset_match.group(1) else 0
                    hours = int(reset_match.group(2)) if reset_match.group(2) else 0
                    mins = int(reset_match.group(3)) if reset_match.group(3) else 0
                    sr = days * 1440 + hours * 60 + mins

            # Find weekly % and reset time
            s7d = 0
            wr = 10080
            all_models_idx = page_text.find('All models', weekly_idx if weekly_idx >= 0 else 0)
            claude_design_idx = page_text.find('Claude Design')

            if all_models_idx >= 0 and claude_design_idx >= all_models_idx:
                weekly_part = page_text[all_models_idx:claude_design_idx]
                weekly_match = re.search(r'(\d+)%\s+used', weekly_part)
                s7d = int(weekly_match.group(1)) if weekly_match else 0
                wr = parse_weekly_reset_time(weekly_part)

            logging.info(f"Scraped: {s5h}% (resets {sr}m) / {s7d}% (resets {wr}m)")
            self.update_usage(s5h, s7d, sr, wr)
            return True

        except Exception as e:
            logging.error(f"Scrape error: {e}")
            return False

    async def start_browser(self):
        """Open browser with persistent storage"""
        try:
            STORAGE_DIR.mkdir(parents=True, exist_ok=True)
            playwright = await async_playwright().start()

            self.context = await playwright.chromium.launch_persistent_context(
                str(STORAGE_DIR / 'browser_data'),
                headless=False,
                args=[
                    '--disable-blink-features=AutomationControlled',
                    '--disable-dev-shm-usage',
                    '--no-sandbox',
                ],
                ignore_https_errors=True,
            )
            self.page = await self.context.new_page()

            logging.info(f"Opening {USAGE_URL}")
            await self.page.goto(USAGE_URL, timeout=15000)
            logging.info("Waiting for page to load...")
            await self.page.wait_for_timeout(5000)

            # Initial scrape
            await self.scrape_usage()
            return True

        except Exception as e:
            logging.error(f"Browser start failed: {e}")
            return False

    async def refresh_loop(self):
        """Continuously refresh and scrape every 60 seconds"""
        while True:
            try:
                await asyncio.sleep(60)

                if not self.page:
                    await self.start_browser()
                    continue

                # Try to click refresh button
                try:
                    refresh_button = await self.page.query_selector('button[title*="efresh"], button[aria-label*="efresh"]')
                    if refresh_button:
                        logging.info("Clicking refresh...")
                        await refresh_button.click()
                        await self.page.wait_for_timeout(1500)
                    else:
                        logging.info("Reloading page...")
                        await self.page.reload(wait_until='domcontentloaded')
                except:
                    await self.page.reload(wait_until='domcontentloaded')

                await self.scrape_usage()

            except Exception as e:
                logging.error(f"Refresh loop error: {e}")
                await asyncio.sleep(10)

cache = UsageCache()

class APIHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/api/usage":
            data = cache.get_usage()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()
            self.wfile.write(json.dumps(data).encode())
        elif self.path == "/":
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.end_headers()
            self.wfile.write(b"ClawdMeter WiFi API (Playwright scraper) running\n")
        else:
            self.send_error(404)

    def do_POST(self):
        if self.path == "/api/refresh":
            # Trigger immediate scrape
            logging.info("Button C: Requesting immediate refresh")
            asyncio.run_coroutine_threadsafe(cache.scrape_usage(), cache.loop)

            data = cache.get_usage()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()
            self.wfile.write(json.dumps(data).encode())
        else:
            self.send_error(404)

    def do_OPTIONS(self):
        self.send_response(200)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, OPTIONS")
        self.end_headers()

    def log_message(self, format, *args):
        logging.info(format % args)

class ThreadingHTTPServer(ThreadingMixIn, HTTPServer):
    daemon_threads = True

async def main():
    parser = argparse.ArgumentParser(description="WiFi API Daemon (Playwright scraper)")
    parser.add_argument("--port", type=int, default=80, help="Port (default: 80)")
    parser.add_argument("--host", default="0.0.0.0", help="Host (default: 0.0.0.0)")
    args = parser.parse_args()

    logging.basicConfig(
        level=logging.INFO,
        format="[%(asctime)s] %(message)s",
        datefmt="%H:%M:%S"
    )

    logging.info("Starting WiFi API Daemon (Playwright scraper)")
    logging.info("Opening browser for claude.ai login...")

    # Save event loop for thread-safe scraping from HTTP handler
    cache.loop = asyncio.get_event_loop()

    # Start browser
    if not await cache.start_browser():
        logging.error("Failed to start browser")
        sys.exit(1)

    try:
        # Start HTTP server in background thread
        server_address = (args.host, args.port)
        server = ThreadingHTTPServer(server_address, APIHandler)
        logging.info(f"Server ready on http://{args.host}:{args.port}/api/usage")

        server_thread = threading.Thread(target=server.serve_forever, daemon=True)
        server_thread.start()

        # Run refresh loop in main asyncio loop
        await cache.refresh_loop()

    except KeyboardInterrupt:
        logging.info("Shutting down...")
    except Exception as e:
        logging.error(f"Error: {e}")
    finally:
        if cache.context:
            await cache.context.close()

if __name__ == "__main__":
    asyncio.run(main())
