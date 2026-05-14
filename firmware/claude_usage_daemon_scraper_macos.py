#!/usr/bin/env python3
"""Claude usage scraper from claude.ai/settings/usage — keeps browser open."""
import argparse, json, time, os, asyncio, re
from pathlib import Path
from time import time as current_time_ms
from datetime import datetime, timedelta

try:
    from playwright.async_api import async_playwright
    import serial
except ImportError as e:
    print(f"Missing dependency: {e}")
    print("Install with: pip install playwright pyserial")
    exit(1)

OUT = Path.home() / '.clawdmeter-fire-usage.json'
STORAGE_DIR = Path.home() / '.config' / 'claude-usage-scraper'
USAGE_URL = 'https://claude.ai/settings/usage'

def find_m5_port():
    import glob
    # Prefer cu.usbserial over tty.usbserial (cu is better for communication)
    patterns = ['/dev/cu.usbmodem*', '/dev/cu.usbserial*', '/dev/tty.usbmodem*', '/dev/tty.usbserial*']
    for pattern in patterns:
        ports = glob.glob(pattern)
        if ports:
            return sorted(ports)[-1]
    return None

def parse_weekly_reset_time(text):
    """Parse 'Resets Wed 11:00 PM' format and return minutes until that time."""
    try:
        match = re.search(r'Resets\s+(\w+)\s+(\d{1,2}):(\d{2})\s+(AM|PM)', text)
        if not match:
            return 10080  # default 7 days

        day_name, hour_str, min_str, ampm = match.groups()
        hour = int(hour_str)
        minute = int(min_str)

        # Convert to 24-hour format
        if ampm == 'PM' and hour != 12:
            hour += 12
        elif ampm == 'AM' and hour == 12:
            hour = 0

        # Map day name to day of week (0=Mon, 6=Sun)
        # Support both full names and abbreviations
        days = {
            'Monday': 0, 'Mon': 0,
            'Tuesday': 1, 'Tue': 1,
            'Wednesday': 2, 'Wed': 2,
            'Thursday': 3, 'Thu': 3,
            'Friday': 4, 'Fri': 4,
            'Saturday': 5, 'Sat': 5,
            'Sunday': 6, 'Sun': 6
        }
        target_day = days.get(day_name)
        if target_day is None:
            return 10080

        # Calculate time until reset
        now = datetime.now()
        current_day = now.weekday()
        current_time = now.hour * 60 + now.minute
        target_time = hour * 60 + minute

        # Days until target day
        days_until = (target_day - current_day) % 7
        if days_until == 0 and target_time <= current_time:
            days_until = 7  # If it's today but time has passed, it's next week

        # Calculate minutes
        if days_until == 0:
            mins_until = target_time - current_time
        else:
            mins_until = (days_until * 1440) + (target_time - current_time)

        return max(0, mins_until)
    except Exception as e:
        return 10080

def write_serial(ser_conn, line):
    if not ser_conn or not ser_conn.is_open:
        return False
    try:
        ser_conn.write((line + '\n').encode())
        ser_conn.flush()
        return True
    except Exception:
        return False

def read_serial_command(ser_conn):
    """Non-blocking check for incoming commands from M5Stack."""
    if not ser_conn or not ser_conn.is_open:
        return None
    try:
        if ser_conn.in_waiting > 0:
            line = ser_conn.readline().decode('utf-8', errors='ignore').strip()
            if 'refresh' in line.lower() or 'cmd' in line.lower():
                return line
    except Exception:
        pass
    return None

async def scrape_usage(page):
    """Scrape percentages and reset times from page."""
    try:
        # Wait for page to fully load and render
        await page.wait_for_timeout(3000)

        # Get all text content on the page
        page_text = await page.inner_text('body')

        # Debug: Check what we're seeing on the page
        if 'Current session' not in page_text and '% used' not in page_text:
            print(f"⚠️  Page content preview (first 500 chars):\n{page_text[:500]}")

        # Find session % and reset time
        current_idx = page_text.find('Current session')
        weekly_idx = page_text.find('Weekly limits')

        s5h = 0
        sr = 300  # default 5 hours
        if current_idx >= 0 and weekly_idx >= current_idx:
            session_part = page_text[current_idx:weekly_idx]
            session_match = re.search(r'(\d+)%\s+used', session_part)
            s5h = int(session_match.group(1)) if session_match else 0

            # Extract reset time (e.g., "Resets in 1 hr 49 min")
            reset_match = re.search(r'Resets in\s+(?:(\d+)\s+d[a-z]*\s+)?(?:(\d+)\s+hr[a-z]*\s+)?(\d+)\s+min', session_part, re.IGNORECASE)
            if reset_match:
                days = int(reset_match.group(1)) if reset_match.group(1) else 0
                hours = int(reset_match.group(2)) if reset_match.group(2) else 0
                mins = int(reset_match.group(3)) if reset_match.group(3) else 0
                sr = days * 1440 + hours * 60 + mins

        # Find weekly % and reset time
        s7d = 0
        wr = 10080  # default 7 days
        all_models_idx = page_text.find('All models', weekly_idx if weekly_idx >= 0 else 0)
        claude_design_idx = page_text.find('Claude Design')

        if all_models_idx >= 0 and claude_design_idx >= all_models_idx:
            weekly_part = page_text[all_models_idx:claude_design_idx]
            weekly_match = re.search(r'(\d+)%\s+used', weekly_part)
            s7d = int(weekly_match.group(1)) if weekly_match else 0

            # Extract reset time (format: "Resets Wed 11:00 PM")
            wr = parse_weekly_reset_time(weekly_part)

        print(f"✅ Extracted: {s5h}% (resets in {sr}m) / {s7d}% (resets in {wr}m)")
        return s5h, s7d, sr, wr

    except Exception as e:
        print(f"⚠️  Scrape error: {e}")
        return 0, 0, 300, 10080

async def open_browser_persistent():
    """Open browser with persistent storage for login session."""
    STORAGE_DIR.mkdir(parents=True, exist_ok=True)

    playwright = await async_playwright().start()

    # Use persistent context with anti-bot measures
    try:
        context = await playwright.chromium.launch_persistent_context(
            str(STORAGE_DIR / 'browser_data'),
            headless=False,  # Use headed mode to bypass Cloudflare
            args=[
                '--disable-blink-features=AutomationControlled',
                '--disable-dev-shm-usage',
                '--no-sandbox',
            ],
            ignore_https_errors=True,
        )
        page = await context.new_page()

        print(f"📱 Opening {USAGE_URL} (with browser visible)")
        await page.goto(USAGE_URL, timeout=15000)

        # Wait for Cloudflare challenge to complete
        print("⏳ Waiting for Cloudflare verification...")
        await page.wait_for_timeout(5000)

        return context, page
    except Exception as e:
        print(f"⚠️  Context failed: {e}")
        print("❌ Please ensure you're logged into claude.ai/settings/usage")
        return None, None

async def refresh_and_scrape(page):
    """Click refresh button and scrape."""
    try:
        # Find and click refresh button (SVG icon button)
        # Looking for button that contains the refresh icon
        refresh_button = await page.query_selector('button[title*="efresh"], button[aria-label*="efresh"], svg[viewBox="0 0 20 20"] >> xpath=../button')

        if not refresh_button:
            # Try to find by clicking any button with refresh-like appearance
            buttons = await page.query_selector_all('button')
            for btn in buttons:
                title = await btn.get_attribute('title')
                aria_label = await btn.get_attribute('aria-label')
                if title and 'refresh' in title.lower():
                    refresh_button = btn
                    break
                if aria_label and 'refresh' in aria_label.lower():
                    refresh_button = btn
                    break

        if refresh_button:
            print("🔄 Clicking refresh...")
            await refresh_button.click()
            await page.wait_for_timeout(1500)
        else:
            print("⚠️  Refresh button not found, reloading page")
            await page.reload(wait_until='domcontentloaded')

        return await scrape_usage(page)
    except Exception as e:
        print(f"⚠️  Refresh error: {e}")
        return 0, 0, 300, 10080

def build_payload(s5h, s7d, sr, wr):
    """Build JSON for M5Stack."""
    return {
        's': max(0, min(100, s5h)),
        'sr': sr,
        'w': max(0, min(100, s7d)),
        'wr': wr,
        'st': 'scraper-web',
        'ok': True,
    }

async def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--port', help='serial device (auto-detect if omitted)')
    parser.add_argument('--show-source', action='store_true')
    args = parser.parse_args()

    print("🌐 Claude usage scraper starting...")
    print("📱 Open the browser window and log in to claude.ai/settings/usage")
    print("🔄 Will refresh every 5 seconds and send data to M5Stack")

    context, page = await open_browser_persistent()
    if not page:
        print("❌ Failed to open browser")
        return

    print("✅ Browser open - log in now, I'll keep scraping every 60 seconds")
    print("   Press button C on M5Stack for immediate refresh")

    last_scrape_ms = 0
    port = args.port or find_m5_port()
    ser_conn = None

    # Try to open serial connection
    if port:
        try:
            ser_conn = serial.Serial(port, 115200, timeout=0.1)
            time.sleep(0.2)
            print(f"📡 Connected to M5Stack on {port}")
        except Exception as e:
            print(f"⚠️  Could not open serial port {port}: {e}")

    while True:
        try:
            now = time.time() * 1000

            # Check for refresh command from M5Stack
            force_refresh = False
            if ser_conn:
                cmd = read_serial_command(ser_conn)
                if cmd:
                    print(f"🔄 Button C pressed on M5Stack: {cmd}")
                    force_refresh = True

            # Refresh and scrape every 60 seconds OR on demand
            if force_refresh or now - last_scrape_ms >= 60000:
                s5h, s7d, sr, wr = await refresh_and_scrape(page)
                last_scrape_ms = now

                payload = build_payload(s5h, s7d, sr, wr)
                line = json.dumps(payload, separators=(',', ':'))
                OUT.write_text(line + '\n')

                if args.show_source:
                    print(f'# source=claude.ai-web-scraper | {time.strftime("%H:%M:%S")}')
                print(f"[{time.strftime('%H:%M:%S')}] {line}")

                # Re-discover port if connection lost
                if not ser_conn or not ser_conn.is_open:
                    port = args.port or find_m5_port()
                    if port:
                        try:
                            ser_conn = serial.Serial(port, 115200, timeout=0.1)
                            time.sleep(0.2)
                            print(f"📡 Reconnected to M5Stack on {port}")
                        except Exception:
                            ser_conn = None

                if ser_conn and ser_conn.is_open:
                    if write_serial(ser_conn, line):
                        print(f"📡 Sent to {port}")
                    else:
                        print(f"⚠️  Failed to send to {port}")
                        ser_conn = None

            # Small delay to avoid busy loop
            await page.wait_for_timeout(500)
        except KeyboardInterrupt:
            print("\n👋 Shutting down...")
            break
        except Exception as e:
            print(f"❌ Error: {e}")
            break

    try:
        await context.close()
    except:
        pass

if __name__ == '__main__':
    asyncio.run(main())
