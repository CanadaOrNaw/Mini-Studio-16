import functools
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import unittest

from tools.check_live_site import verify


ROOT = Path(__file__).resolve().parents[1]


class QuietHandler(SimpleHTTPRequestHandler):
    def log_message(self, _format, *args):
        pass


class SiteServer:
    def __init__(self, directory: Path):
        handler = functools.partial(QuietHandler, directory=str(directory))
        self.server = ThreadingHTTPServer(("127.0.0.1", 0), handler)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)

    def __enter__(self):
        self.thread.start()
        host, port = self.server.server_address
        return f"http://{host}:{port}/"

    def __exit__(self, exc_type, exc, traceback):
        self.server.shutdown()
        self.thread.join()
        self.server.server_close()


class LiveSiteCheckTests(unittest.TestCase):
    def build(self, target: Path) -> None:
        env = dict(os.environ, GITHUB_SHA="0123456789abcdef")
        subprocess.run(
            [sys.executable, str(ROOT / "tools" / "build_site.py"),
             "--output", str(target)],
            cwd=ROOT, env=env, check=True)

    def test_checks_every_manifested_download(self):
        with tempfile.TemporaryDirectory() as temporary:
            target = Path(temporary) / "pages"
            self.build(target)
            with SiteServer(target) as url:
                result = verify(url, "0123456789abcdef")
            self.assertEqual(result["assets_checked"], 15)
            self.assertGreater(result["bytes_checked"], 1000)

    def test_rejects_a_corrupt_download(self):
        with tempfile.TemporaryDirectory() as temporary:
            target = Path(temporary) / "pages"
            self.build(target)
            (target / "downloads" / "FLASHING.md").write_text(
                "corrupt", encoding="utf-8")
            with SiteServer(target) as url:
                with self.assertRaisesRegex(RuntimeError, "mismatch"):
                    verify(url, "0123456789abcdef")


if __name__ == "__main__":
    unittest.main()
