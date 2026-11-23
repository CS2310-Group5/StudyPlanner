"""Offline authentication-flow regression tests; no browser or account is used."""
import asyncio
import importlib.util
import io
from pathlib import Path
import subprocess
import sys
import tempfile
import types
import unittest
from contextlib import redirect_stdout
from unittest.mock import AsyncMock, Mock, patch

HELPER = Path(__file__).resolve().parents[1] / "auth_helper.py"
spec = importlib.util.spec_from_file_location("auth_helper_under_test", HELPER)
helper = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helper)
URL = "https://degreeaudit.cityu.edu.hk:9040/dashboard/worksheets"
AUDIT = "Audit Date 22/11/2026\nStill Needed:\nCS2310\n"


class AuthHelperTests(unittest.TestCase):
    def test_readiness_rejects_sign_in_loading_and_unrelated_pages(self):
        self.assertTrue(helper.is_worksheet(URL, AUDIT))
        self.assertTrue(helper.is_worksheet(URL + "/123", "Student ID\nCS 2310"))
        self.assertFalse(helper.is_worksheet(URL, "Degree Works Loading..."))
        self.assertFalse(helper.is_worksheet(URL, "Sign in to your account"))
        self.assertFalse(helper.is_worksheet("https://idp.example.invalid/sso", AUDIT))
        self.assertFalse(helper.is_worksheet(URL.replace(".hk", ".hk.example.invalid"), AUDIT))
        self.assertFalse(helper.is_worksheet(URL + "-login", AUDIT))
        self.assertFalse(helper.is_worksheet(URL, "CS2310"))

    def test_waits_for_user_and_loaded_audit(self):
        page = Mock()
        page.url = URL
        page.locator.return_value.inner_text = AsyncMock(side_effect=["Loading...", AUDIT])
        with patch("builtins.input", side_effect=["", ""]), redirect_stdout(io.StringIO()):
            result = asyncio.run(helper.read_worksheet(page))
        self.assertEqual(result, AUDIT)
        self.assertEqual(page.locator.return_value.inner_text.await_count, 2)

    def run_flow(self, root, answers, corrupt_state=False):
        args = types.SimpleNamespace(check=False, state=str(root / "session.json"),
                                     page_text=str(root / "page.txt"),
                                     courses_db=str(root / "courses.json"))
        Path(args.courses_db).write_text("{}")
        page = Mock(url=URL)
        page.goto = AsyncMock()
        page.locator.return_value.inner_text = AsyncMock(return_value=AUDIT)
        context = Mock()
        context.new_page = AsyncMock(return_value=page)
        async def save_session(path):
            Path(path).write_text('{"cookies": []}')
        context.storage_state = AsyncMock(side_effect=save_session)
        browser = Mock()
        browser.new_context = AsyncMock(return_value=context)
        browser.close = AsyncMock()
        if corrupt_state:
            browser.new_context.side_effect = [ValueError("invalid state"), context]
        playwright = Mock()
        playwright.chromium.launch = AsyncMock(return_value=browser)
        playwright.stop = AsyncMock()
        manager = Mock()
        manager.start = AsyncMock(return_value=playwright)
        api = types.ModuleType("playwright.async_api")
        api.async_playwright = Mock(return_value=manager)
        with patch.dict(sys.modules, {"playwright.async_api": api}), \
             patch("builtins.input", side_effect=answers), redirect_stdout(io.StringIO()):
            result = asyncio.run(helper.main(args))
        browser.close.assert_awaited_once()
        playwright.stop.assert_awaited_once()
        return result, context, browser

    def test_saves_session_even_without_login_redirect(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            result, context, _ = self.run_flow(root, [""])
            self.assertEqual(result, 0)
            context.storage_state.assert_awaited_once()
            self.assertEqual((root / "page.txt").read_text(), AUDIT)
            self.assertTrue((root / "session.json").exists())

    def test_cancel_preserves_previous_audit(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "page.txt").write_text("Previous synthetic audit")
            result, context, _ = self.run_flow(root, ["Q"])
            self.assertEqual(result, 130)
            context.storage_state.assert_not_awaited()
            self.assertEqual((root / "page.txt").read_text(), "Previous synthetic audit")

    def test_closed_terminal_cancels_without_saving(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            result, context, _ = self.run_flow(root, EOFError())
            self.assertEqual(result, 130)
            context.storage_state.assert_not_awaited()
            self.assertFalse((root / "page.txt").exists())

    def test_corrupt_session_can_recover_with_manual_login(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "session.json").write_text("invalid json")
            result, _, browser = self.run_flow(root, [""], corrupt_state=True)
            self.assertEqual(result, 0)
            self.assertEqual(browser.new_context.await_count, 2)

    def test_missing_playwright_prints_actionable_error(self):
        result = subprocess.run([sys.executable, "-S", str(HELPER), "--check"],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 21)
        self.assertIn("AUTH-PLAYWRIGHT-1", result.stdout)
        self.assertNotIn("Traceback", result.stderr)


if __name__ == "__main__":
    unittest.main()
