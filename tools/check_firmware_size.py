#!/usr/bin/env python3
"""Enforce ESP32-S3 DRAM/flash budgets from the linked firmware ELF.

GNU ``size``'s Berkeley summary is misleading for ESP32 images: it includes
virtual/padding sections in ``data`` and ``bss``.  PlatformIO's ESP32 builder
uses the System V section table and counts only the actual DRAM sections.  We
intentionally mirror that calculation here so the CI gate and PlatformIO's
link report describe the same hardware resources.
"""

from __future__ import annotations

import argparse
import glob
import json
import os
import pathlib
import subprocess
import sys


PROGRAM_SECTIONS = {
    ".iram0.text",
    ".iram0.vectors",
    ".dram0.data",
    ".flash.text",
    ".flash.rodata",
}
DRAM_SECTIONS = {".dram0.data", ".dram0.bss", ".noinit"}


def parse_size_output(output: str) -> dict[str, int]:
    """Parse ``xtensa-esp32s3-elf-size -A -d`` output.

    These sets match ``SIZEPROGREGEXP`` and ``SIZEDATAREGEXP`` in PlatformIO's
    espressif32 6.7.0 builder.  Unknown sections are retained in the ELF but do
    not get mistaken for on-chip static DRAM.
    """
    sections: dict[str, int] = {}
    for line in output.splitlines():
        fields = line.split()
        if len(fields) < 2 or not fields[0].startswith("."):
            continue
        try:
            sections[fields[0]] = int(fields[1])
        except ValueError:
            continue
    if not sections:
        raise ValueError("could not parse GNU size section output")

    static_ram = sum(sections.get(name, 0) for name in DRAM_SECTIONS)
    flash_estimate = sum(sections.get(name, 0) for name in PROGRAM_SECTIONS)
    if static_ram <= 0 or flash_estimate <= 0:
        raise ValueError("required ESP32 memory sections were not present")
    return {
        "dram_data": sections.get(".dram0.data", 0),
        "dram_bss": sections.get(".dram0.bss", 0),
        "noinit": sections.get(".noinit", 0),
        "flash_estimate": flash_estimate,
        "static_ram": static_ram,
    }


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
    parser.add_argument("--max-static", type=int, default=204_800,
                        help="static DRAM ceiling (62.5%% of the 327680-byte board budget)")
    parser.add_argument("--max-flash", type=int, default=3_000_000,
                        # P3: partitions_dual.csv slots are 0x2F0000 bytes.
                        help="program-image ceiling within the 3,080,192-byte app slot")
    parser.add_argument("--report", type=pathlib.Path)
    args = parser.parse_args()

    if not args.elf.is_file():
        parser.error(f"ELF does not exist: {args.elf}")
    result = subprocess.run([find_size_tool(), "-A", "-d", str(args.elf)], check=True,
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
