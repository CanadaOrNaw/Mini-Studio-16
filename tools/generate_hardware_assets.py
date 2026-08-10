#!/usr/bin/env python3
"""Generate deterministic Mini Studio 16 printable hardware assets.

The SVG is generated from hardware/button-layout.json.  The STL is an original
open bench cradle sized from M5Stack's published 84 x 54 mm Cardputer-ADV
envelope.  It deliberately leaves the side and end centers open for ports and
is a pre-hardware fit prototype, not a copy of the upstream Microgroove shell.
"""

from __future__ import annotations

import argparse
import json
import math
import struct
from pathlib import Path
from xml.sax.saxutils import escape


ROOT = Path(__file__).resolve().parents[1]
LAYOUT = ROOT / "hardware" / "button-layout.json"
SVG = ROOT / "hardware" / "mini-studio-16-button-layout.svg"
STL = ROOT / "hardware" / "stl" / "mini-studio-16-bench-cradle.stl"
CAP_BASE_STL = ROOT / "hardware" / "audio-cap" / "stl" / "mini-studio-audio-cap-base.stl"
CAP_LID_STL = ROOT / "hardware" / "audio-cap" / "stl" / "mini-studio-audio-cap-lid.stl"

# Published Cardputer-ADV envelope plus a conservative unverified clearance.
DEVICE_X_MM = 84.0
DEVICE_Y_MM = 54.0
CLEARANCE_MM = 0.30
OUTER_X_MM = 88.0
OUTER_Y_MM = 58.0
BASE_HEIGHT_MM = 2.0
WALL_HEIGHT_MM = 6.0
CENTER_OPENING_X_MM = 70.0
CENTER_OPENING_Y_MM = 40.0
SIDE_CLIP_SPAN_Y_MM = 32.0
END_CLIP_SPAN_X_MM = 62.0


def _tspan(text: str, css_class: str, x: float, y: float) -> str:
    return (
        f'<text class="{css_class}" x="{x:.1f}" y="{y:.1f}" '
        f'text-anchor="middle">{escape(text)}</text>'
    )


def render_svg(layout: dict) -> bytes:
    width, height = 1280, 610
    key_w, key_h, gap = 78, 91, 9
    grid_w = 14 * key_w + 13 * gap
    left = (width - grid_w) / 2
    top = 108
    lines = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}" role="img" aria-labelledby="title desc">',
        '<title id="title">Mini Studio 16 button layout</title>',
        '<desc id="desc">Four rows of fourteen Cardputer-ADV keys showing tap, hold and musical actions.</desc>',
        '<defs><linearGradient id="bg" x1="0" y1="0" x2="1" y2="1"><stop stop-color="#09121a"/><stop offset="1" stop-color="#101d27"/></linearGradient></defs>',
        '<style>',
        'text{font-family:Inter,ui-sans-serif,system-ui,sans-serif}',
        '.title{fill:#f4f7f8;font-size:35px;font-weight:800;letter-spacing:1px}',
        '.sub{fill:#91a7b3;font-size:15px;font-weight:500}',
        '.key{fill:#17252e;stroke:#3b5360;stroke-width:1.5}',
        '.key.record{stroke:#ff5d63;stroke-width:2.5}',
        '.face{fill:#f4f7f8;font-size:16px;font-weight:800}',
        '.tap{fill:#dfe8eb;font-size:10px;font-weight:700}',
        '.hold{fill:#ffb14e;font-size:9px;font-weight:700}',
        '.music{fill:#55e5d2;font-size:10px;font-weight:800}',
        '.legend{fill:#9db0ba;font-size:13px;font-weight:600}',
        '</style>',
        f'<rect width="{width}" height="{height}" rx="24" fill="url(#bg)"/>',
        '<text class="title" x="34" y="48">MINI STUDIO 16</text>',
        '<text class="sub" x="34" y="76">CARDPUTER-ADV • TAP / HOLD / PERFORMANCE REFERENCE • PRE-HARDWARE ALPHA</text>',
    ]

    for row_index, row in enumerate(layout["rows"]):
        for col_index, key in enumerate(row):
            x = left + col_index * (key_w + gap)
            y = top + row_index * (key_h + gap)
            record_class = " record" if key.get("record") else ""
            lines.append(
                f'<g id="key-{escape(key["id"])}" data-key="{escape(key["id"])}">'
                f'<rect class="key{record_class}" x="{x:.1f}" y="{y:.1f}" width="{key_w}" height="{key_h}" rx="8"/>'
            )
            lines.append(_tspan(str(key["face"]), "face", x + key_w / 2, y + 23))
            content_y = y + 50
            if key.get("tap"):
                lines.append(_tspan(str(key["tap"]), "tap", x + key_w / 2, content_y))
                content_y += 15
            if key.get("hold"):
                lines.append(_tspan(str(key["hold"]), "hold", x + key_w / 2, content_y))
                content_y += 15
            if key.get("music"):
                lines.append(_tspan(str(key["music"]), "music", x + key_w / 2, content_y))
            lines.append('</g>')

    legend_y = 552
    lines.extend(
        [
            '<circle cx="36" cy="548" r="6" fill="#dfe8eb"/><text class="legend" x="51" y="553">TAP</text>',
            '<circle cx="124" cy="548" r="6" fill="#ffb14e"/><text class="legend" x="139" y="553">HOLD 450 ms</text>',
            '<circle cx="270" cy="548" r="6" fill="#55e5d2"/><text class="legend" x="285" y="553">NOTE / PAD</text>',
            '<circle cx="407" cy="548" r="6" fill="none" stroke="#ff5d63" stroke-width="3"/><text class="legend" x="422" y="553">RECORD / CAPTURE</text>',
            f'<text class="sub" x="1244" y="{legend_y}" text-anchor="end">hardware/button-layout.json • schema {layout["schema"]}</text>',
            '</svg>',
        ]
    )
    return ("\n".join(lines) + "\n").encode("utf-8")


def _normal(a, b, c):
    ux, uy, uz = (b[i] - a[i] for i in range(3))
    vx, vy, vz = (c[i] - a[i] for i in range(3))
    nx, ny, nz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
    length = math.sqrt(nx * nx + ny * ny + nz * nz)
    return (0.0, 0.0, 0.0) if length == 0 else (nx / length, ny / length, nz / length)


def _occupied(x: float, y: float, z: float) -> bool:
    ax, ay = abs(x), abs(y)
    if z < BASE_HEIGHT_MM:
        return not (
            ax < CENTER_OPENING_X_MM / 2 and ay < CENTER_OPENING_Y_MM / 2
        )
    cavity_x = DEVICE_X_MM / 2 + CLEARANCE_MM
    cavity_y = DEVICE_Y_MM / 2 + CLEARANCE_MM
    side_clip = ax >= cavity_x and ay >= SIDE_CLIP_SPAN_Y_MM / 2
    end_clip = ay >= cavity_y and ax >= END_CLIP_SPAN_X_MM / 2
    return side_clip or end_clip


def cradle_triangles():
    cavity_x = DEVICE_X_MM / 2 + CLEARANCE_MM
    cavity_y = DEVICE_Y_MM / 2 + CLEARANCE_MM
    xs = sorted(
        {
            -OUTER_X_MM / 2,
            -cavity_x,
            -CENTER_OPENING_X_MM / 2,
            -END_CLIP_SPAN_X_MM / 2,
            END_CLIP_SPAN_X_MM / 2,
            CENTER_OPENING_X_MM / 2,
            cavity_x,
            OUTER_X_MM / 2,
        }
    )
    ys = sorted(
        {
            -OUTER_Y_MM / 2,
            -cavity_y,
            -CENTER_OPENING_Y_MM / 2,
            -SIDE_CLIP_SPAN_Y_MM / 2,
            SIDE_CLIP_SPAN_Y_MM / 2,
            CENTER_OPENING_Y_MM / 2,
            cavity_y,
            OUTER_Y_MM / 2,
        }
    )
    zs = [0.0, BASE_HEIGHT_MM, WALL_HEIGHT_MM]
    cells = set()
    for ix in range(len(xs) - 1):
        for iy in range(len(ys) - 1):
            for iz in range(len(zs) - 1):
                center = (
                    (xs[ix] + xs[ix + 1]) / 2,
                    (ys[iy] + ys[iy + 1]) / 2,
                    (zs[iz] + zs[iz + 1]) / 2,
                )
                if _occupied(*center):
                    cells.add((ix, iy, iz))

    # Outward-oriented quads for the six cell faces. Each quad becomes two
    # triangles; internal faces are omitted, making the boolean union watertight.
    faces = [
        ((-1, 0, 0), lambda x0, x1, y0, y1, z0, z1: [(x0,y0,z0),(x0,y0,z1),(x0,y1,z1),(x0,y1,z0)]),
        ((1, 0, 0), lambda x0, x1, y0, y1, z0, z1: [(x1,y0,z0),(x1,y1,z0),(x1,y1,z1),(x1,y0,z1)]),
        ((0, -1, 0), lambda x0, x1, y0, y1, z0, z1: [(x0,y0,z0),(x1,y0,z0),(x1,y0,z1),(x0,y0,z1)]),
        ((0, 1, 0), lambda x0, x1, y0, y1, z0, z1: [(x0,y1,z0),(x0,y1,z1),(x1,y1,z1),(x1,y1,z0)]),
        ((0, 0, -1), lambda x0, x1, y0, y1, z0, z1: [(x0,y0,z0),(x0,y1,z0),(x1,y1,z0),(x1,y0,z0)]),
        ((0, 0, 1), lambda x0, x1, y0, y1, z0, z1: [(x0,y0,z1),(x1,y0,z1),(x1,y1,z1),(x0,y1,z1)]),
    ]
    triangles = []
    for ix, iy, iz in sorted(cells):
        bounds = (xs[ix], xs[ix + 1], ys[iy], ys[iy + 1], zs[iz], zs[iz + 1])
        for (dx, dy, dz), vertices in faces:
            if (ix + dx, iy + dy, iz + dz) in cells:
                continue
            q = vertices(*bounds)
            triangles.append((q[0], q[1], q[2]))
            triangles.append((q[0], q[2], q[3]))
    return triangles


def _voxel_triangles(xs, ys, zs, occupied):
    cells = set()
    for ix in range(len(xs) - 1):
        for iy in range(len(ys) - 1):
            for iz in range(len(zs) - 1):
                center = ((xs[ix] + xs[ix + 1]) / 2,
                          (ys[iy] + ys[iy + 1]) / 2,
                          (zs[iz] + zs[iz + 1]) / 2)
                if occupied(*center):
                    cells.add((ix, iy, iz))
    faces = [
        ((-1,0,0), lambda a,b,c,d,e,f: [(a,c,e),(a,c,f),(a,d,f),(a,d,e)]),
        ((1,0,0), lambda a,b,c,d,e,f: [(b,c,e),(b,d,e),(b,d,f),(b,c,f)]),
        ((0,-1,0), lambda a,b,c,d,e,f: [(a,c,e),(b,c,e),(b,c,f),(a,c,f)]),
        ((0,1,0), lambda a,b,c,d,e,f: [(a,d,e),(a,d,f),(b,d,f),(b,d,e)]),
        ((0,0,-1), lambda a,b,c,d,e,f: [(a,c,e),(a,d,e),(b,d,e),(b,c,e)]),
        ((0,0,1), lambda a,b,c,d,e,f: [(a,c,f),(b,c,f),(b,d,f),(a,d,f)]),
    ]
    triangles = []
    for ix, iy, iz in sorted(cells):
        bounds = (xs[ix], xs[ix + 1], ys[iy], ys[iy + 1], zs[iz], zs[iz + 1])
        for (dx, dy, dz), vertices in faces:
            if (ix + dx, iy + dy, iz + dz) in cells:
                continue
            q = vertices(*bounds)
            triangles.extend(((q[0], q[1], q[2]), (q[0], q[2], q[3])))
    return triangles


def cap_base_triangles():
    xs = [-42, -40, -36, -31, -24, -10, 10, 24, 31, 36, 40, 42]
    ys = [-12, -10, -7, -5, 5, 7, 10, 12]
    zs = [0, 2, 4, 6, 9, 11, 13]

    def occupied(x, y, z):
        ax, ay = abs(x), abs(y)
        if z < 2:
            # The 2x7 cap connector protrudes through this keyed floor opening.
            return not (ax < 10 and y < -5)
        wall = ax >= 40 or ay >= 10
        # End openings are deliberately generous until actual jack/USB height
        # is measured. The SCAD source carries the continuous nominal geometry.
        port_opening = ax >= 40 and ay < 5 and z < 9
        rails = z < 4 and ((24 < ax < 36 and ay < 7) or (ax < 10 and 5 < y < 7))
        latch_shelf = 9 < z < 11 and ay >= 10 and ax < 31
        return (wall and not port_opening) or rails or latch_shelf

    return _voxel_triangles(xs, ys, zs, occupied)


def cap_lid_triangles():
    xs = [-42.4, -40.4, -31, 31, 40.4, 42.4]
    ys = [-12.4, -10.4, -8.8, 8.8, 10.4, 12.4]
    zs = [0, 1.4, 3.0, 5.0]

    def occupied(x, y, z):
        ax, ay = abs(x), abs(y)
        top = z >= 3.0
        skirt = z < 3.0 and (ax >= 40.4 or ay >= 10.4)
        # Two compliant internal catches; PETG flexes the long skirt to release.
        catch = z < 1.4 and ay > 8.8 and ax < 31
        return top or skirt or catch

    return _voxel_triangles(xs, ys, zs, occupied)


def render_binary_stl(triangles=None, title=None) -> bytes:
    if triangles is None:
        triangles = cradle_triangles()
    if title is None:
        title = b"Mini Studio 16 bench cradle; original pre-hardware fit prototype"
    output = bytearray(title.ljust(80, b"\0")[:80])
    output.extend(struct.pack("<I", len(triangles)))
    for triangle in triangles:
        normal = _normal(*triangle)
        output.extend(struct.pack("<3f", *normal))
        for vertex in triangle:
            output.extend(struct.pack("<3f", *vertex))
        output.extend(struct.pack("<H", 0))
    return bytes(output)


def _write_or_check(path: Path, content: bytes, check: bool) -> None:
    if check:
        if not path.exists() or path.read_bytes() != content:
            raise SystemExit(f"generated asset is stale: {path.relative_to(ROOT)}")
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(content)
    print(f"wrote {path.relative_to(ROOT)} ({len(content)} bytes)")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true", help="fail if committed outputs are stale")
    args = parser.parse_args()
    layout = json.loads(LAYOUT.read_text(encoding="utf-8"))
    if len(layout["rows"]) != 4 or any(len(row) != 14 for row in layout["rows"]):
        raise SystemExit("button layout must contain exactly 4 rows of 14 keys")
    _write_or_check(SVG, render_svg(layout), args.check)
    _write_or_check(STL, render_binary_stl(), args.check)
    _write_or_check(
        CAP_BASE_STL,
        render_binary_stl(cap_base_triangles(),
                          b"Mini Studio Audio Cap Rev A base; NOT HARDWARE VERIFIED"),
        args.check,
    )
    _write_or_check(
        CAP_LID_STL,
        render_binary_stl(cap_lid_triangles(),
                          b"Mini Studio Audio Cap Rev A snap lid; NOT HARDWARE VERIFIED"),
        args.check,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
