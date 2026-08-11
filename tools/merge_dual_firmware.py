#!/usr/bin/env python3
"""Merge Normal and USB-host applications into one Cardputer-ADV image."""

from __future__ import annotations

import argparse
import csv
from dataclasses import asdict, dataclass
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
from typing import Dict, Iterable

try:
    from tools.merge_firmware import find_boot_app0
except ModuleNotFoundError:  # direct `python tools/merge_dual_firmware.py`
    from merge_firmware import find_boot_app0


FLASH_SIZE = 8 * 1024 * 1024
PARTITION_TABLE_END = 0x9000


@dataclass(frozen=True)
class Partition:
    name: str
    kind: str
    subtype: str
    offset: int
    size: int


@dataclass(frozen=True)
class DualLayout:
    nvs: Partition
    otadata: Partition
    normal: Partition
    usbhost: Partition


def parse_number(value: str) -> int:
    text = value.strip().lower()
    multiplier = 1
    if text.endswith("k"):
        multiplier = 1024
        text = text[:-1]
    elif text.endswith("m"):
        multiplier = 1024 * 1024
        text = text[:-1]
    if not text:
        raise ValueError("empty partition number")
    return int(text, 0) * multiplier


def _partition_rows(path: Path) -> Iterable[Partition]:
    with path.open(newline="", encoding="utf-8") as source:
        for row_number, row in enumerate(csv.reader(source), 1):
            if not row or not row[0].strip() or row[0].lstrip().startswith("#"):
                continue
            if len(row) < 5:
                raise ValueError(f"partition row {row_number} has fewer than five columns")
            name, kind, subtype, offset, size = (item.strip() for item in row[:5])
            if not name or not kind or not subtype or not offset or not size:
                raise ValueError(f"partition row {row_number} contains a blank required field")
            yield Partition(name, kind.lower(), subtype.lower(),
                            parse_number(offset), parse_number(size))


def load_layout(path: Path) -> DualLayout:
    partitions: Dict[str, Partition] = {}
    for item in _partition_rows(path):
        if item.name in partitions:
            raise ValueError(f"duplicate partition name: {item.name}")
        if item.offset < PARTITION_TABLE_END:
            raise ValueError(f"partition {item.name} overlaps boot metadata")
        if item.size <= 0 or item.offset + item.size > FLASH_SIZE:
            raise ValueError(f"partition {item.name} exceeds 8 MB flash")
        if item.kind == "app" and item.offset % 0x10000:
            raise ValueError(f"application partition {item.name} is not 64 KiB aligned")
        partitions[item.name] = item

    ordered = sorted(partitions.values(), key=lambda item: item.offset)
    for previous, current in zip(ordered, ordered[1:]):
        if previous.offset + previous.size > current.offset:
            raise ValueError(f"partitions {previous.name} and {current.name} overlap")

    required = {"nvs", "otadata", "normal", "usbhost"}
    missing = sorted(required - partitions.keys())
    if missing:
        raise ValueError("missing partition(s): " + ", ".join(missing))

    nvs = partitions["nvs"]
    otadata = partitions["otadata"]
    normal = partitions["normal"]
    usbhost = partitions["usbhost"]
    if (nvs.kind, nvs.subtype) != ("data", "nvs") or nvs.size < 0x3000:
        raise ValueError("nvs must be data/nvs and at least 0x3000 bytes")
    if (otadata.kind, otadata.subtype, otadata.offset, otadata.size) != (
            "data", "ota", 0xE000, 0x2000):
        raise ValueError("otadata must be data/ota at 0xe000 with size 0x2000")
    if (normal.kind, normal.subtype) != ("app", "ota_0"):
        raise ValueError("normal must be app/ota_0")
    if (usbhost.kind, usbhost.subtype) != ("app", "ota_1"):
        raise ValueError("usbhost must be app/ota_1")
    if normal.size != usbhost.size:
        raise ValueError("normal and usbhost slots must have equal capacity")
    return DualLayout(nvs, otadata, normal, usbhost)


def _same_file(first: Path, second: Path) -> bool:
    return first.read_bytes() == second.read_bytes()


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def validate_builds(normal_dir: Path, host_dir: Path,
                    layout: DualLayout) -> Dict[str, Path]:
    inputs = {
        "bootloader": normal_dir / "bootloader.bin",
        "partitions": normal_dir / "partitions.bin",
        "normal": normal_dir / "firmware.bin",
        "usbhost": host_dir / "firmware.bin",
        "host_bootloader": host_dir / "bootloader.bin",
        "host_partitions": host_dir / "partitions.bin",
    }
    missing = [str(path) for path in inputs.values() if not path.is_file()]
    if missing:
        raise FileNotFoundError("missing build input(s): " + ", ".join(missing))
    if not _same_file(inputs["bootloader"], inputs["host_bootloader"]):
        raise ValueError("Normal and USB-host bootloaders differ")
    if not _same_file(inputs["partitions"], inputs["host_partitions"]):
        raise ValueError("Normal and USB-host partition tables differ")
    if inputs["normal"].stat().st_size > layout.normal.size:
        raise ValueError("Normal firmware exceeds its OTA slot")
    if inputs["usbhost"].stat().st_size > layout.usbhost.size:
        raise ValueError("USB-host firmware exceeds its OTA slot")
    return inputs


def build_command(normal_dir: Path, host_dir: Path, output: Path,
                  boot_app0: Path, partitions_csv: Path) -> list[str]:
    layout = load_layout(partitions_csv)
    inputs = validate_builds(normal_dir, host_dir, layout)
    if not boot_app0.is_file():
        raise FileNotFoundError(f"boot_app0.bin not found: {boot_app0}")
    output.parent.mkdir(parents=True, exist_ok=True)
    return [
        sys.executable, "-m", "esptool", "--chip", "esp32s3", "merge_bin",
        "-o", str(output), "--flash_mode", "dio", "--flash_freq", "80m",
        "--flash_size", "8MB",
        "0x0000", str(inputs["bootloader"]),
        "0x8000", str(inputs["partitions"]),
        "0xe000", str(boot_app0),
        hex(layout.normal.offset), str(inputs["normal"]),
        hex(layout.usbhost.offset), str(inputs["usbhost"]),
    ]


def verify_merged_image(output: Path, normal_dir: Path, host_dir: Path,
                        partitions_csv: Path) -> None:
    layout = load_layout(partitions_csv)
    inputs = validate_builds(normal_dir, host_dir, layout)
    if not output.is_file():
        raise FileNotFoundError(f"merged image not found: {output}")
    if output.stat().st_size > FLASH_SIZE:
        raise ValueError("merged image exceeds 8 MB flash")
    with output.open("rb") as merged:
        for role, partition in (("normal", layout.normal),
                                ("usbhost", layout.usbhost)):
            expected = inputs[role].read_bytes()
            merged.seek(partition.offset)
            if merged.read(len(expected)) != expected:
                raise ValueError(f"merged image does not contain {role} firmware at "
                                 f"{partition.offset:#x}")


def write_report(path: Path, normal_dir: Path, host_dir: Path,
                 partitions_csv: Path, output: Path) -> None:
    layout = load_layout(partitions_csv)
    inputs = validate_builds(normal_dir, host_dir, layout)
    verify_merged_image(output, normal_dir, host_dir, partitions_csv)
    report = {
        "flash_size": FLASH_SIZE,
        "output": output.name,
        "normal": {
            **asdict(layout.normal),
            "binary_size": inputs["normal"].stat().st_size,
            "sha256": _sha256(inputs["normal"]),
        },
        "usbhost": {
            **asdict(layout.usbhost),
            "binary_size": inputs["usbhost"].stat().st_size,
            "sha256": _sha256(inputs["usbhost"]),
        },
        "partition_table_sha256": _sha256(inputs["partitions"]),
        "bootloader_sha256": _sha256(inputs["bootloader"]),
    }
    report["merged_size"] = output.stat().st_size
    report["merged_sha256"] = _sha256(output)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n",
                    encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--normal-dir", type=Path,
                        default=Path(".pio/build/m5stack-cardputer-adv"))
    parser.add_argument("--host-dir", type=Path,
                        default=Path(".pio/build/m5stack-cardputer-adv-usb-host"))
    parser.add_argument("--partitions", type=Path,
                        default=Path("partitions_dual.csv"))
    parser.add_argument("--output", type=Path,
                        default=Path(".pio/build/mini-studio-16-dual-role.bin"))
    parser.add_argument("--report", type=Path,
                        default=Path(".pio/build/dual-image-layout.json"))
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    configured_core = os.environ.get("PLATFORMIO_CORE_DIR")
    core_dir = Path(configured_core) if configured_core else Path.home() / ".platformio"
    boot_app0 = find_boot_app0(core_dir)
    command = build_command(args.normal_dir, args.host_dir, args.output,
                            boot_app0, args.partitions)
    if args.dry_run:
        print(" ".join(command))
        return 0
    subprocess.run(command, check=True)
    verify_merged_image(args.output, args.normal_dir, args.host_dir,
                        args.partitions)
    write_report(args.report, args.normal_dir, args.host_dir,
                 args.partitions, args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
