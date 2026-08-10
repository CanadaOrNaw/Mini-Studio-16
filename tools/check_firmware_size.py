#!/usr/bin/env python3
"""Enforce an SRAM/flash budget from the linked ESP32-S3 ELF."""

from __future__ import annotations

import argparse
import glob
import json
import os
import pathlib
import subprocess
import sys


def parse_size_output(output: str) -> dict[str, int]:
    lines = [line.split() for line in output.splitlines() if line.strip()]
    for fields in reversed(lines):
        if len(fields) >= 6 and all(item.isdigit() for item in fields[:4]):
            text, data, bss, total = map(int, fields[:4])
            if total != text + data + bss:
                raise ValueError("GNU size total does not match its components")
            return {
                "text": text,
                "data": data,
                "bss": bss,
                "flash_estimate": text + data,
                "static_ram": data + bss,
            }
    raise ValueError("could not parse GNU size output")


def find_size_tool() -> str:
    core = pathlib.Path(os.environ.get("PLATFORMIO_CORE_DIR", pathlib.Path.home() / ".platformio"))
    patterns = [
        str(core / "packages" / "toolchain-xtensa-esp32s3" / "bin" /
            "xtensa-esp32s3-elf-size"),
        str(core / "packages" / "toolchain-xtensa-esp32s3*" / "bin" /
            "xtensa-esp32s3-elf-size"),
    ]
    for pattern in patterns:
        matches = glob.glob(pattern)
        if matches and os.access(matches[0], os.X_OK):
            return matches[0]
    raise FileNotFoundError("PlatformIO ESP32-S3 size tool was not found")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("elf", type=pathlib.Path)
    parser.add_argument("--max-static", type=int, default=200_000)
    parser.add_argument("--max-flash", type=int, default=7_340_032)
    parser.add_argument("--report", type=pathlib.Path)
    args = parser.parse_args()

    if not args.elf.is_file():
        parser.error(f"ELF does not exist: {args.elf}")
    result = subprocess.run([find_size_tool(), str(args.elf)], check=True,
                            capture_output=True, text=True)
    metrics = parse_size_output(result.stdout)
    metrics.update({"max_static": args.max_static, "max_flash": args.max_flash})
    metrics["static_headroom"] = args.max_static - metrics["static_ram"]
    metrics["flash_headroom"] = args.max_flash - metrics["flash_estimate"]
    report = json.dumps(metrics, indent=2, sort_keys=True) + "\n"
    print(report, end="")
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(report, encoding="utf-8")
    if metrics["static_ram"] > args.max_static:
        print("static SRAM budget exceeded", file=sys.stderr)
        return 2
    if metrics["flash_estimate"] > args.max_flash:
        print("flash budget exceeded", file=sys.stderr)
        return 3
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
