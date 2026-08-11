from __future__ import annotations

import hashlib
import os
from pathlib import Path
import tempfile
import unittest
import zipfile

from tools.package_sd import ZIP_EPOCH, package_directory


class PackageSdTests(unittest.TestCase):
    def test_output_is_stable_when_source_mtimes_change(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "groovebox"
            (source / "projects").mkdir(parents=True)
            (source / "projects" / "P1.gbx").write_bytes(b"project")
            (source / "README.txt").write_text("starter\n", encoding="utf-8")

            first = root / "first.zip"
            second = root / "second.zip"
            package_directory(source, first)
            for path in source.rglob("*"):
                os.utime(path, (1_900_000_000, 1_900_000_000))
            package_directory(source, second)

            self.assertEqual(hashlib.sha256(first.read_bytes()).digest(),
                             hashlib.sha256(second.read_bytes()).digest())

            with zipfile.ZipFile(first) as archive:
                self.assertEqual(
                    archive.namelist(),
                    [
                        "groovebox/",
                        "groovebox/README.txt",
                        "groovebox/projects/",
                        "groovebox/projects/P1.gbx",
                    ],
                )
                self.assertTrue(all(item.date_time == ZIP_EPOCH for item in archive.infolist()))
                self.assertEqual(archive.testzip(), None)

    def test_rejects_symlinks(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / "groovebox"
            source.mkdir()
            (source / "target").write_bytes(b"data")
            (source / "link").symlink_to("target")
            with self.assertRaisesRegex(ValueError, "symlinks"):
                package_directory(source, Path(temporary) / "output.zip")


if __name__ == "__main__":
    unittest.main()
