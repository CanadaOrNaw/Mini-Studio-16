#!/usr/bin/env python3
"""Build the dependency-free Mini Studio 16 GitHub Pages artifact."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path
from urllib.parse import urlsplit


ROOT = Path(__file__).resolve().parents[1]
SITE = ROOT / "site"
ASSETS = [
    (ROOT / "hardware" / "button-layout.json", Path("assets/button-layout.json")),
    (ROOT / "hardware" / "mini-studio-16-button-layout.svg", Path("assets/mini-studio-16-button-layout.svg")),
    (ROOT / "hardware" / "mini-studio-16-button-layout.svg", Path("downloads/mini-studio-16-button-layout.svg")),
    (ROOT / "hardware" / "button-layout.json", Path("downloads/button-layout.json")),
    (ROOT / "hardware" / "stl" / "mini-studio-16-bench-cradle.stl", Path("downloads/mini-studio-16-bench-cradle.stl")),
    (ROOT / "hardware" / "cad" / "mini-studio-16-bench-cradle.scad", Path("downloads/mini-studio-16-bench-cradle.scad")),
    (ROOT / "hardware" / "audio-cap" / "stl" / "audio-cap-base.stl", Path("downloads/audio-cap-base.stl")),
    (ROOT / "hardware" / "audio-cap" / "stl" / "audio-cap-lid.stl", Path("downloads/audio-cap-lid.stl")),
    (ROOT / "hardware" / "audio-cap" / "stl" / "audio-cap-14pin-fit-gauge.stl", Path("downloads/audio-cap-14pin-fit-gauge.stl")),
    (ROOT / "hardware" / "audio-cap" / "design.json", Path("downloads/audio-cap-design.json")),
    (ROOT / "hardware" / "audio-cap" / "bom.json", Path("downloads/audio-cap-bom.json")),
    (ROOT / "docs" / "AUDIO_CAP_BUILD_GUIDE.md", Path("downloads/AUDIO_CAP_BUILD_GUIDE.md")),
    (ROOT / "docs" / "AUDIO_CAP_BOM.md", Path("downloads/AUDIO_CAP_BOM.md")),
    (ROOT / "docs" / "START_HERE.md", Path("downloads/START_HERE.md")),
    (ROOT / "docs" / "FLASHING.md", Path("downloads/FLASHING.md")),
]


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    digest.update(path.read_bytes())
    return digest.hexdigest()


# Markdown docs copied into downloads/ lose their repository context: a
# relative link to a file that is not shipped beside them (for example
# START_HERE.md -> CARDPUTER_TESTING.md, or the build guide's
# ../hardware/audio-cap/bom.json) would 404 on the published site. Rewrite
# such links to absolute GitHub URLs at the shipped main branch;
# links between docs that ship together in downloads/ stay relative.
REPO_BLOB_BASE = ("https://github.com/CanadaOrNaw/Mini-Studio-16/blob/"
                  "main/")
_MD_LINK = re.compile(r"(\]\()([^)\s#][^)#]*)((?:#[^)]*)?\))")


def _rewrite_downloads_markdown(source: Path, text: str,
                                shipped: set) -> str:
    def replace(match: "re.Match[str]") -> str:
        target = match.group(2)
        if urlsplit(target).scheme or target.startswith("/"):
            return match.group(0)
        resolved = (source.parent / target).resolve()
        try:
            repo_relative = resolved.relative_to(ROOT)
        except ValueError:
            return match.group(0)
        if resolved.parent == source.parent and resolved.name in shipped:
            return match.group(0)
        return (f"{match.group(1)}{REPO_BLOB_BASE}"
                f"{repo_relative.as_posix()}{match.group(3)}")

    return _MD_LINK.sub(replace, text)


def build(output: Path) -> None:
    subprocess.run(
        [sys.executable, str(ROOT / "tools" / "generate_hardware_assets.py"), "--check"],
        cwd=ROOT,
        check=True,
    )
    if output.exists():
        marker = output / "BUILD_INFO.json"
        try:
            prior = json.loads(marker.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            raise SystemExit(f"refusing to replace unrecognized directory: {output}")
        if prior.get("project") != "Mini Studio 16":
            raise SystemExit(f"refusing to replace unrecognized directory: {output}")
        shutil.rmtree(output)
    shutil.copytree(SITE, output)

    copied = {}
    shipped_markdown = {destination.name for _, destination in ASSETS
                        if destination.suffix == ".md"}
    for source, destination in ASSETS:
        target = output / destination
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        if destination.suffix == ".md" and destination.parts[0] == "downloads":
            rewritten = _rewrite_downloads_markdown(
                source, target.read_text(encoding="utf-8"), shipped_markdown)
            target.write_text(rewritten, encoding="utf-8", newline="\n")
        copied[destination.as_posix()] = {
            "bytes": target.stat().st_size,
            "sha256": sha256(target),
        }

    build_sha = os.environ.get("GITHUB_SHA", "development")
    index = output / "index.html"
    html = index.read_text(encoding="utf-8").replace(
        '<span id="build-sha">development</span>',
        f'<span id="build-sha">{build_sha[:12]}</span>',
    )
    index.write_text(html, encoding="utf-8", newline="\n")
    (output / "BUILD_INFO.json").write_text(
        json.dumps(
            {
                "project": "Mini Studio 16",
                "source_sha": build_sha,
                "generated_assets": copied,
            },
            indent=2,
            sort_keys=True,
        )
        + "\n",
        encoding="utf-8",
        newline="\n",
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=ROOT / "build" / "pages")
    args = parser.parse_args()
    output = args.output.resolve()
    if output in (ROOT, SITE):
        raise SystemExit("refusing to overwrite the repository or site source")
    build(output)
    print(f"built Pages artifact: {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
