#!/usr/bin/env python3
"""Build the dependency-free Mini Studio 16 GitHub Pages artifact."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SITE = ROOT / "site"
ASSETS = [
    (ROOT / "hardware" / "button-layout.json", Path("assets/button-layout.json")),
    (ROOT / "hardware" / "mini-studio-16-button-layout.svg", Path("assets/mini-studio-16-button-layout.svg")),
    (ROOT / "hardware" / "mini-studio-16-button-layout.svg", Path("downloads/mini-studio-16-button-layout.svg")),
    (ROOT / "hardware" / "button-layout.json", Path("downloads/button-layout.json")),
    (ROOT / "hardware" / "stl" / "mini-studio-16-bench-cradle.stl", Path("downloads/mini-studio-16-bench-cradle.stl")),
    (ROOT / "hardware" / "cad" / "mini-studio-16-bench-cradle.scad", Path("downloads/mini-studio-16-bench-cradle.scad")),
    (ROOT / "hardware" / "audio-cap" / "stl" / "mini-studio-audio-cap-base.stl", Path("downloads/mini-studio-audio-cap-base.stl")),
    (ROOT / "hardware" / "audio-cap" / "stl" / "mini-studio-audio-cap-lid.stl", Path("downloads/mini-studio-audio-cap-lid.stl")),
    (ROOT / "hardware" / "audio-cap" / "cad" / "mini-studio-audio-cap.scad", Path("downloads/mini-studio-audio-cap.scad")),
    (ROOT / "hardware" / "audio-cap" / "BOM.csv", Path("downloads/mini-studio-audio-cap-bom.csv")),
    (ROOT / "hardware" / "audio-cap" / "SOURCING.md", Path("downloads/audio-cap-sourcing.md")),
    (ROOT / "hardware" / "audio-cap" / "pcb" / "generated" / "audio-cap-pcb.svg", Path("downloads/audio-cap-pcb-reference.svg")),
    (ROOT / "START_HERE.md", Path("downloads/START_HERE.md")),
    (ROOT / "docs" / "AUDIO_CAP_BUILD.md", Path("downloads/AUDIO_CAP_BUILD.md")),
]


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    digest.update(path.read_bytes())
    return digest.hexdigest()


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
    for source, destination in ASSETS:
        target = output / destination
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
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
