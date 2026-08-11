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
CAP_BASE_STL = ROOT / "hardware" / "audio-cap" / "stl" / "audio-cap-base.stl"
CAP_LID_STL = ROOT / "hardware" / "audio-cap" / "stl" / "audio-cap-lid.stl"
CAP_GAUGE_STL = ROOT / "hardware" / "audio-cap" / "stl" / "audio-cap-14pin-fit-gauge.stl"

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

# Audio Cap enclosure mechanism constants (mm). Shared with the host tests so
# internal fits are asserted, not just outer bounds. The voxel mesher samples
# cell centers, so every band below must have matching grid lines in the
# functions that use it; tests assert the resulting geometry directly.
CAP_WALL_INNER_X = 40.4          # base end-wall inner face
CAP_WALL_INNER_Y = 17.4          # base side-wall inner face
CAP_DIVIDER_X0 = -15.8           # bay divider (moved from -17.0 so the
CAP_DIVIDER_X1 = -14.2           # 24.0 mm ATOM fits with 0.6 mm slack)
CAP_ATOM_SIZE_MM = 24.0
CAP_ADC_MAX_LEN_MM = 50.5
CAP_ATOM_BAY_SPAN_MM = CAP_DIVIDER_X0 + CAP_WALL_INNER_X      # 24.6
CAP_ADC_BAY_SPAN_MM = CAP_WALL_INNER_X - CAP_DIVIDER_X1       # 54.6
CAP_ATOM_BRACKET_Y0 = 12.25      # locating brackets flanking the ATOM
CAP_ATOM_BRACKET_Y1 = 13.45
CAP_LID_PLATE_MM = 1.2           # lid prints plate-down, fingers up
CAP_LID_LIP_TOP = 2.4            # locating lip: plate..2.4 (1.2 mm engagement)
CAP_LID_HEIGHT = 8.2
CAP_LID_LIP_X0 = 39.0            # end-lip band (0.2 mm clearance to 40.4 wall)
CAP_LID_LIP_X1 = 40.2
CAP_FINGER_X0 = 26.0             # four snap-finger blades, 2.0 x 1.2 mm
CAP_FINGER_X1 = 28.0
CAP_FINGER_Y0 = 16.0
CAP_FINGER_Y1 = 17.2
# Stepped catch nubs at the finger tips: protrusion tapers toward the tip so
# insertion is a lead-in ramp; max engagement past the 17.4 wall face is
# 0.30 mm (~1.6% strain over the 5.8 mm blade - safe for PLA), and the stepped
# shoulder keeps the lid deliberately removable for service.
CAP_NUB_STEPS = ((6.4, 7.0, 17.70), (7.0, 7.6, 17.50), (7.6, 8.2, 17.30))
# Through-wall catch/release windows in the base side walls. Assembled nub
# z = 21.2 - lid z, so the 17.70 step (lid 6.4..7.0) lands at 14.2..14.8 and
# the 17.50 step at 13.6..14.2; the window clears both seated steps.
CAP_WINDOW_X0 = 25.7
CAP_WINDOW_X1 = 28.3
CAP_WINDOW_Z0 = 13.4
CAP_WINDOW_Z1 = 15.0


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


def _voxel_triangles(xs, ys, zs, occupied):
    cells = set()
    for ix in range(len(xs) - 1):
        for iy in range(len(ys) - 1):
            for iz in range(len(zs) - 1):
                center = (
                    (xs[ix] + xs[ix + 1]) / 2,
                    (ys[iy] + ys[iy + 1]) / 2,
                    (zs[iz] + zs[iz + 1]) / 2,
                )
                if occupied(*center):
                    cells.add((ix, iy, iz))

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
    return _voxel_triangles(xs, ys, zs, _occupied)


def _render_binary_stl(triangles, title: bytes) -> bytes:
    output = bytearray(title.ljust(80, b"\0")[:80])
    output.extend(struct.pack("<I", len(triangles)))
    for triangle in triangles:
        normal = _normal(*triangle)
        output.extend(struct.pack("<3f", *normal))
        for vertex in triangle:
            output.extend(struct.pack("<3f", *vertex))
        output.extend(struct.pack("<H", 0))
    return bytes(output)


def render_binary_stl() -> bytes:
    return _render_binary_stl(
        cradle_triangles(),
        b"Mini Studio 16 bench cradle; original pre-hardware fit prototype",
    )


def cap_base_triangles():
    # Side-by-side bays: PCM1808 at the line-jack end, ATOM Lite at the bus end.
    # The end windows deliberately have extra clearance; a printable fit gauge
    # is supplied because module revisions and Cardputer shells vary.
    xs = [-42, -CAP_WALL_INNER_X, -CAP_WINDOW_X1, -CAP_WINDOW_X0,
          CAP_DIVIDER_X0, CAP_DIVIDER_X1, 10,
          CAP_WINDOW_X0, CAP_WINDOW_X1, CAP_WALL_INNER_X, 42]
    ys = [-19, -CAP_WALL_INNER_Y, -CAP_ATOM_BRACKET_Y1, -CAP_ATOM_BRACKET_Y0,
          -12, -10, -7, 7, 10, 12,
          CAP_ATOM_BRACKET_Y0, CAP_ATOM_BRACKET_Y1, CAP_WALL_INNER_Y, 19]
    zs = [0, 1.6, 3.0, 5.0, 9.5, 11.5, CAP_WINDOW_Z0, CAP_WINDOW_Z1, 18.0, 20.0]

    def occupied(x, y, z):
        bottom = z < 1.6
        side_wall = abs(y) > CAP_WALL_INNER_Y
        end_wall = abs(x) > CAP_WALL_INNER_X
        # Open one end for the Cardputer header nose and the other for line-in.
        header_window = x < -CAP_WALL_INNER_X and abs(y) < 10 and 3.0 < z < 11.5
        line_window = x > CAP_WALL_INNER_X and abs(y) < 7 and 3.0 < z < 11.5
        # Through-wall catch windows for the lid's four snap nubs. They double
        # as release slots: press the two nubs on a side inward to pop the lid.
        snap_window = (CAP_WINDOW_X0 < abs(x) < CAP_WINDOW_X1
                       and CAP_WINDOW_Z0 < z < CAP_WINDOW_Z1)
        shell = bottom or (side_wall and not snap_window) or (
            end_wall and not header_window and not line_window)
        # The ATOM's retail size is exact, so brackets locate it against y
        # drift; the PCM1808 bay keeps low friction ribs only, because retail
        # module widths vary (modules rest on the ribs under lid pressure).
        atom_brackets = (x < CAP_DIVIDER_X0 and z < 3.0
                         and CAP_ATOM_BRACKET_Y0 < abs(y) < CAP_ATOM_BRACKET_Y1)
        adc_ribs = x > 10 and (10 < abs(y) < 12) and z < 3.0
        divider = CAP_DIVIDER_X0 < x < CAP_DIVIDER_X1 and abs(y) > 10 and z < 5.0
        return shell or atom_brackets or adc_ribs or divider

    return _voxel_triangles(xs, ys, zs, occupied)


def cap_lid_triangles():
    # Printed plate-down: the 1.2 mm plate lies on the bed and the locating
    # lip plus four snap fingers rise straight up, so no supports are needed.
    # Flip fingers-down to assemble; the plate then rests on the base rim.
    xs = [-42, -CAP_LID_LIP_X1, -CAP_LID_LIP_X0, -CAP_FINGER_X1, -CAP_FINGER_X0,
          CAP_FINGER_X0, CAP_FINGER_X1, CAP_LID_LIP_X0, CAP_LID_LIP_X1, 42]
    ys = [-19, -17.7, -17.5, -17.3, -CAP_FINGER_Y1, -CAP_FINGER_Y0,
          CAP_FINGER_Y0, CAP_FINGER_Y1, 17.3, 17.5, 17.7, 19]
    zs = [0, CAP_LID_PLATE_MM, CAP_LID_LIP_TOP,
          CAP_NUB_STEPS[0][0], CAP_NUB_STEPS[1][0], CAP_NUB_STEPS[2][0],
          CAP_LID_HEIGHT]

    def occupied(x, y, z):
        plate = z < CAP_LID_PLATE_MM
        # Perimeter locating lip: 1.2 mm engagement into the base opening with
        # 0.2 mm clearance to the 40.4/17.4 inner wall faces.
        lip = (CAP_LID_PLATE_MM < z < CAP_LID_LIP_TOP
               and abs(x) < CAP_LID_LIP_X1 and abs(y) < CAP_FINGER_Y1 and (
                   CAP_LID_LIP_X0 < abs(x) < CAP_LID_LIP_X1
                   or CAP_FINGER_Y0 < abs(y) < CAP_FINGER_Y1
               ))
        # Four compliant snap-finger blades with stepped catch nubs at the
        # tips; the taper is the insertion lead-in and the stepped shoulder
        # snaps into the base's through-wall windows (removable for service).
        finger = (CAP_FINGER_X0 < abs(x) < CAP_FINGER_X1
                  and CAP_FINGER_Y0 < abs(y) < CAP_FINGER_Y1
                  and z < CAP_LID_HEIGHT)
        nub = False
        if CAP_FINGER_X0 < abs(x) < CAP_FINGER_X1:
            for z0, z1, y_out in CAP_NUB_STEPS:
                if z0 < z < z1 and CAP_FINGER_Y0 < abs(y) < y_out:
                    nub = True
        return plate or lip or finger or nub

    return _voxel_triangles(xs, ys, zs, occupied)


def cap_gauge_triangles():
    # A sacrificial 2x7 hole/pitch coupon. Print this first and push only the
    # header through it; the real cap should not be printed until this fits.
    pitch = 2.54
    xs = sorted({-12.0, -10.9, 12.0, *[(-7.62 + i * pitch + d) for i in range(7)
                                       for d in (-0.55, 0.55)]})
    ys = sorted({-5.0, -3.9, 5.0, *[(row + d) for row in (-1.27, 1.27)
                                    for d in (-0.55, 0.55)]})
    zs = [0, 2.0]

    def occupied(x, y, z):
        del z
        # Notched corner marks EXT pin 1 (the G3 end) so the gauge also
        # teaches header orientation, not just pitch.
        if x < -10.9 and y < -3.9:
            return False
        for col in range(7):
            if abs(x - (-7.62 + col * pitch)) < 0.55:
                for row in (-1.27, 1.27):
                    if abs(y - row) < 0.55:
                        return False
        return True

    return _voxel_triangles(xs, ys, zs, occupied)


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
    _write_or_check(CAP_BASE_STL, _render_binary_stl(
        cap_base_triangles(), b"Mini Studio 16 Audio Cap base; pre-hardware prototype"), args.check)
    _write_or_check(CAP_LID_STL, _render_binary_stl(
        cap_lid_triangles(), b"Mini Studio 16 Audio Cap snap lid; pre-hardware prototype"), args.check)
    _write_or_check(CAP_GAUGE_STL, _render_binary_stl(
        cap_gauge_triangles(), b"Mini Studio 16 Audio Cap 2x7 header fit gauge"), args.check)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
