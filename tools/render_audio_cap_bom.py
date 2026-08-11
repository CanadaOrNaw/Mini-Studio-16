#!/usr/bin/env python3
"""Render the exact regional Audio Cap shopping sheet from bom.json."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "hardware" / "audio-cap" / "bom.json"
OUTPUT = ROOT / "docs" / "AUDIO_CAP_BOM.md"


def part_name(item: dict, source: dict) -> str:
    manufacturer = item.get("manufacturer") or source.get("manufacturer", "")
    part = item.get("mpn") or source["sku"]
    return f"{manufacturer} {part}".strip()


def render(data: dict) -> str:
    lines = [
        "# Audio Cap exact shopping list",
        "",
        "This page is generated from `hardware/audio-cap/bom.json`. Every link",
        "opens one exact product SKU—not a search or category page. Price and stock",
        "can change. If a link is unavailable, search the printed manufacturer part",
        "number or ASIN and do not substitute a visually similar part.",
        "",
        "Buy one row of each item unless the quantity column says otherwise. An adult",
        "should place orders and supervise cutting/stripping the four power leads.",
        "",
    ]
    for region in data["regions"]:
        lines.extend([
            f"## {region}",
            "",
            "| Qty | Exact part | Seller SKU | Buy |",
            "| ---: | --- | --- | --- |",
        ])
        for item in data["items"]:
            source = item["sources"][region][0]
            quantity = item["quantity"]
            name = part_name(item, source).replace("|", "\\|")
            description = item["description"].replace("|", "\\|")
            lines.append(
                f"| {quantity} | **{name}** — {description} | `{source['sku']}` | "
                f"[{source['vendor']}]({source['url']}) |")
        lines.append("")
    lines.extend([
        "## Do not substitute these details",
        "",
        "- The harness is Adafruit 4635, **24 AWG**. Ordinary 28-AWG jumper leads",
        "  are not allowed in the WAGO power branches.",
        "- The PCM1808 listing must show an installed I2S header, 3.5 mm input and",
        "  USB-C 5 V input, and the board must be no larger than 50.5 × 30.5 mm.",
        "- The cap header is Samtec `TSW-107-08-G-D`; print and use the fit gauge",
        "  before printing the complete enclosure.",
        "- The USB-C pigtail stays hidden inside the closed cap. It is not a second",
        "  external power cable.",
        "",
        "Continue with [`AUDIO_CAP_BUILD_GUIDE.md`](AUDIO_CAP_BUILD_GUIDE.md).",
        "",
    ])
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    data = json.loads(SOURCE.read_text(encoding="utf-8"))
    expected = render(data)
    if args.check:
        if not OUTPUT.is_file() or OUTPUT.read_text(encoding="utf-8") != expected:
            raise SystemExit(f"generated shopping list is stale: {OUTPUT}")
        print(f"verified {OUTPUT.relative_to(ROOT)}")
        return 0
    OUTPUT.write_text(expected, encoding="utf-8", newline="\n")
    print(f"wrote {OUTPUT.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
