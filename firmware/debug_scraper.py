#!/usr/bin/env python3
import asyncio
from playwright.async_api import async_playwright
from pathlib import Path

STORAGE_DIR = Path.home() / '.config' / 'claude-usage-scraper'

async def main():
    playwright = await async_playwright().start()
    context = await playwright.chromium.launch_persistent_context(
        str(STORAGE_DIR / 'browser_data'),
        headless=False,
    )
    page = await context.new_page()
    await page.goto('https://claude.ai/settings/usage', wait_until='networkidle')
    await page.wait_for_load_state('networkidle')

    # Get text content
    text = await page.inner_text('body')
    print("=== PAGE TEXT ===")
    print(text[:2000])  # First 2000 chars

    # Get HTML
    html = await page.content()
    with open('/tmp/usage_page.html', 'w') as f:
        f.write(html)
    print("\n✅ HTML saved to /tmp/usage_page.html")

asyncio.run(main())
