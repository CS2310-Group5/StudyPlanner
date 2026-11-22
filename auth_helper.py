import argparse
import asyncio
import os
import re
from urllib.parse import urlparse
import subprocess
import sys

parser = argparse.ArgumentParser(description="CityU Degree Works authentication helper")
parser.add_argument("--state", default="session_state.json")
parser.add_argument("--page-text", default="page_text.txt")
parser.add_argument("--courses-db", default="courses_db.json")
parser.add_argument("--check", action="store_true", help="verify Playwright driver and Chromium")


def playwright_error(exc):
    print("AUTH-PLAYWRIGHT-1: Playwright could not start its Node driver or Chromium is not installed.")
    print("Suggested action: create a project .venv and install Playwright and its matching browser:")
    if os.name == "nt":
        print("  py -m venv .venv")
        print(r"  .venv\Scripts\python.exe -m pip install --upgrade playwright openpyxl")
        print(r"  .venv\Scripts\python.exe -m playwright install chromium")
    else:
        print("  python3 -m venv .venv")
        print("  .venv/bin/python -m pip install --upgrade playwright openpyxl")
        print("  .venv/bin/python -m playwright install chromium")
    print("Python interpreter:", sys.executable)
    print("Technical detail:", exc)


def check_playwright():
    try:
        result = subprocess.run(
            [sys.executable, "-m", "playwright", "--version"],
            capture_output=True,
            text=True,
            timeout=60,
            check=False,
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        playwright_error(exc)
        return 21
    if result.returncode:
        detail = result.stderr.strip() or result.stdout.strip()
        playwright_error(RuntimeError(detail or f"Playwright check exited with {result.returncode}"))
        return 21
    try:
        from playwright.async_api import async_playwright
    except Exception as exc:
        playwright_error(exc)
        return 21
    print("Playwright Node driver is ready.")
    print("If Chromium is not installed yet, run:")
    print(f"  {sys.executable} -m playwright install chromium")
    return 0


def is_worksheet(url, text):
    """Reject sign-in/loading pages; this checks readiness, not degree validity."""
    parsed = urlparse(url)
    if parsed.hostname != "degreeaudit.cityu.edu.hk":
        return False
    path = parsed.path.rstrip("/")
    if path != "/dashboard/worksheets" and not path.startswith("/dashboard/worksheets/"):
        return False
    has_courses = re.search(r"\b[A-Z]{2,5}\s*\d{4}[A-Z]?\b", text)
    has_audit_section = re.search(
        r"still\s+needed|in[- ]progress|credits?\s+applied|student\s+id|audit\s+date",
        text, re.IGNORECASE,
    )
    return bool(has_courses and has_audit_section)


async def read_worksheet(page):
    while True:
        answer = input(
            "Complete login and MFA, then open your Degree Works worksheet. "
            "When it has fully loaded, press Enter (Q to cancel)..."
        ).strip().upper()
        if answer == "Q":
            return None
        text = await page.locator("body").inner_text()
        if is_worksheet(page.url, text):
            return text
        print("Worksheet data is not ready. The login/loading page has not been saved.")
        print("Open the worksheet with course codes and audit details, then try again.")


async def main(args=None):
    if args is None:
        args = parser.parse_args()
    print("CityU Authentication Helper")
    if args.check:
        return check_playwright()

    try:
        from playwright.async_api import async_playwright
    except Exception as exc:
        playwright_error(exc)
        return 21

    if not os.path.exists(args.courses_db):
        with open(args.courses_db, "w", encoding="utf-8") as f:
            f.write("{}")

    try:
        playwright = await async_playwright().start()
    except Exception as exc:
        playwright_error(exc)
        return 21

    try:
        browser = await playwright.chromium.launch(headless=False)
    except Exception as exc:
        print("AUTH-BROWSER-1: Could not launch Chromium.")
        print("Suggested action: install the browser with the command shown by `auth_helper.py --check`.")
        print("Technical detail:", exc)
        await playwright.stop()
        return 21

    try:
        if os.path.exists(args.state):
            print("Loaded the saved local browser session.")
            try:
                context = await browser.new_context(storage_state=args.state, accept_downloads=True)
            except Exception:
                print("The saved session could not be loaded. Please sign in again.")
                context = await browser.new_context(accept_downloads=True)
        else:
            print("No saved session found. Complete login and MFA in the browser.")
            context = await browser.new_context(accept_downloads=True)

        page = await context.new_page()
        try:
            await page.goto("https://degreeaudit.cityu.edu.hk:9040/dashboard/worksheets")
            text = await read_worksheet(page)
            if text is None:
                print("Authentication cancelled; no worksheet was saved.")
                return 130
            # Save sessions even when no recognizable login redirect occurred.
            await context.storage_state(path=args.state)
            with open(args.page_text, "w", encoding="utf-8") as f:
                f.write(text)
        except EOFError:
            print("Input closed; no worksheet was saved.")
            return 130
        except Exception as exc:
            print("AUTH-SESSION-1: Degree Works session failed. Suggested action: sign in again or update the browser session.")
            print("Technical detail:", exc)
            return 21

        print("Degree Works data was saved.")
    finally:
        await browser.close()
        await playwright.stop()
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(asyncio.run(main()) or 0)
    except KeyboardInterrupt:
        print("Authentication cancelled.")
        raise SystemExit(130)
