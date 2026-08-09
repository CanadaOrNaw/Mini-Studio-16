#!/usr/bin/env python3
"""Create a single image that can be flashed to an ESP32-S3 at offset 0."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import sys


def find_boot_app0(core_dir: Path) -> Path:
    candidates = sorted(
        (core_dir / "packages").glob(
            "framework-arduinoespressif32*/tools/partitions/boot_app0.bin"
        )
    )
    if not candidates:
        raise FileNotFoundError(f"boot_app0.bin not found below {core_dir / 'packages'}")
    return candidates[-1]


def build_command(build_dir: Path, output: Path, boot_app0: Path) -> list[str]:
    inputs = {
        "bootloader": build_dir / "bootloader.bin",
        "partitions": build_dir / "partitions.bin",
        "firmware": build_dir / "firmware.bin",
        "boot_app0": boot_app0,
    }
    missing = [str(path) for path in inputs.values() if not path.is_file()]
    if missing:
        raise FileNotFoundError("missing build input(s): " + ", ".join(missing))

    output.parent.mkdir(parents=True, exist_ok=True)
    return [
        sys.executable,
        "-m",
        "esptool",
        "--chip",
        "esp32s3",
        "merge_bin",
        "-o",
        str(output),
        "--flash_mode",
        "dio",
        "--flash_freq",
        "80m",
        "--flash_size",
        "8MB",
        "0x0000",
        str(inputs["bootloader"]),
        "0x8000",
        str(inputs["partitions"]),
        "0xe000",
        str(inputs["boot_app0"]),
        "0x10000",
        str(inputs["firmware"]),
    ]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--build-dir",
        type=Path,
        default=Path(".pio/build/m5stack-cardputer-adv"),
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path(".pio/build/m5stack-cardputer-adv/microgroove-v3-alpha.bin"),
    )
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    configured_core = os.environ.get("PLATFORMIO_CORE_DIR")
    core_dir = Path(configured_core) if configured_core else Path.home() / ".platformio"
    command = build_command(args.build_dir, args.output, find_boot_app0(core_dir))
    if args.dry_run:
        print(" ".join(command))
        return 0
    subprocess.run(command, check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

