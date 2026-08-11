import json
import os
import subprocess
import sys
import tempfile
import unittest
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import urlsplit


ROOT = Path(__file__).resolve().parents[1]


class PageParser(HTMLParser):
    def __init__(self):
        super().__init__()
        self.ids = []
        self.links = []
        self.contexts = []

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if "id" in attrs:
            self.ids.append(attrs["id"])
        for name in ("href", "src"):
            if name in attrs:
                self.links.append(attrs[name])
        if "data-context" in attrs:
            self.contexts.append(attrs["data-context"])


class SiteBuildTests(unittest.TestCase):
    def build(self, target):
        env = dict(os.environ, GITHUB_SHA="0123456789abcdef")
        subprocess.run(
            [sys.executable, str(ROOT / "tools" / "build_site.py"), "--output", str(target)],
            cwd=ROOT,
            env=env,
            check=True,
        )

    def test_site_build_has_valid_internal_references(self):
        with tempfile.TemporaryDirectory() as tmp:
            target = Path(tmp) / "pages"
            self.build(target)
            parser = PageParser()
            parser.feed((target / "index.html").read_text(encoding="utf-8"))
            self.assertEqual(len(parser.ids), len(set(parser.ids)))
            self.assertEqual(
                set(parser.contexts),
                {"global", "pattern", "sound", "sample", "loops", "event", "motion", "song", "diag"},
            )
            ids = set(parser.ids)
            for link in parser.links:
                parts = urlsplit(link)
                if parts.scheme in ("http", "https", "mailto"):
                    continue
                if not parts.path and parts.fragment:
                    self.assertIn(parts.fragment, ids, link)
                    continue
                path = target / parts.path
                if parts.path.endswith("/"):
                    path /= "index.html"
                self.assertTrue(path.exists(), link)

            info = json.loads((target / "BUILD_INFO.json").read_text(encoding="utf-8"))
            self.assertEqual(info["source_sha"], "0123456789abcdef")
            self.assertIn("0123456789ab", (target / "index.html").read_text(encoding="utf-8"))
            self.assertEqual(len(info["generated_assets"]), 15)
            self.assertIn("downloads/START_HERE.md", info["generated_assets"])
            self.assertIn("downloads/FLASHING.md", info["generated_assets"])
            self.assertIn("downloads/AUDIO_CAP_BOM.md", info["generated_assets"])

    def test_site_sources_have_accessible_fallbacks(self):
        html = (ROOT / "site" / "index.html").read_text(encoding="utf-8")
        script = (ROOT / "site" / "app.js").read_text(encoding="utf-8")
        self.assertIn("<noscript>", html)
        self.assertIn('aria-live="polite"', html)
        self.assertIn('fetch("assets/button-layout.json")', script)
        self.assertNotIn("innerHTML", script)

    def test_builder_refuses_unrecognized_output_directory(self):
        with tempfile.TemporaryDirectory() as tmp:
            target = Path(tmp) / "unrelated"
            target.mkdir()
            sentinel = target / "keep-me.txt"
            sentinel.write_text("user data", encoding="utf-8")
            result = subprocess.run(
                [sys.executable, str(ROOT / "tools" / "build_site.py"), "--output", str(target)],
                cwd=ROOT,
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertTrue(sentinel.exists())
            self.assertIn("refusing to replace", result.stderr + result.stdout)


if __name__ == "__main__":
    unittest.main()
