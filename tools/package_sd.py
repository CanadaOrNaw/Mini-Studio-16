#!/usr/bin/env python3
"""Create a byte-reproducible starter SD ZIP.

The standard ``python -m zipfile`` command records checkout-time mtimes, so two
otherwise identical CI runs produce different archives.  This packager fixes
ZIP metadata, ordering, permissions, and compression to keep the result stable.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import shutil
import zipfile


ZIP_EPOCH = (1980, 1, 1, 0, 0, 0)


def _info(name: str, *, directory: bool) -> zipfile.ZipInfo:
    if directory and not name.endswith("/"):
        name += "/"
    info = zipfile.ZipInfo(name, ZIP_EPOCH)
    info.create_system = 3
    info.compress_type = zipfile.ZIP_STORED
    info.external_attr = ((0o40755 if directory else 0o100644) << 16)
    if directory:
        info.external_attr |= 0x10
    return info


def package_directory(source: Path, output: Path) -> None:
    source = source.resolve()
    if not source.is_dir():
        raise NotADirectoryError(source)

    entries = [source, *sorted(source.rglob("*"), key=lambda path: path.as_posix())]
    symlinks = [path for path in entries if path.is_symlink()]
    if symlinks:
        raise ValueError("starter SD tree must not contain symlinks: " + str(symlinks[0]))

    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, "w", allowZip64=True) as archive:
        for path in entries:
            name = path.relative_to(source.parent).as_posix()
            if path.is_dir():
                archive.writestr(_info(name, directory=True), b"")
                continue
            if not path.is_file():
                raise ValueError(f"unsupported SD tree entry: {path}")
            with path.open("rb") as source_file, archive.open(
                _info(name, directory=False), "w"
            ) as destination:
                shutil.copyfileobj(source_file, destination, length=64 * 1024)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    package_directory(args.source, args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
