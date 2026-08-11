# Mini Studio 16 printable hardware

The fork now has its own reproducible physical assets:

| File | Purpose | Status |
| --- | --- | --- |
| [`mini-studio-16-button-layout.svg`](mini-studio-16-button-layout.svg) | Full-resolution 56-key tap/hold/performance legend | Generated and data-validated |
| [`button-layout.json`](button-layout.json) | Canonical key and website-context data | Host-tested source of truth |
| [`stl/mini-studio-16-bench-cradle.stl`](stl/mini-studio-16-bench-cradle.stl) | Original open Cardputer-ADV cradle | Watertight and dimension-validated; physical fit pending |
| [`cad/mini-studio-16-bench-cradle.scad`](cad/mini-studio-16-bench-cradle.scad) | Editable OpenSCAD source | Parameters documented below |
| [`audio-cap/stl/audio-cap-base.stl`](audio-cap/stl/audio-cap-base.stl) | Solderless bus-powered Audio Cap base | Watertight/dimension checked; physical fit pending |
| [`audio-cap/stl/audio-cap-lid.stl`](audio-cap/stl/audio-cap-lid.stl) | Two-part enclosure's compliant snap lid | Watertight/dimension checked; physical fit pending |
| [`audio-cap/stl/audio-cap-14pin-fit-gauge.stl`](audio-cap/stl/audio-cap-14pin-fit-gauge.stl) | Cheap first print for 2x7 header pitch/clearance | Generated and checked |
| [`audio-cap/design.json`](audio-cap/design.json) | Hard one-plug/no-PCB/no-solder requirements and pin map | Enforced by host tests |
| [`audio-cap/bom.json`](audio-cap/bom.json) | Exact Canada/US/EU retail SKUs | Region/product-page/wire-gauge constraints tested |

The previous `microgroove_labels_v6_preview.png` remains solely as inherited
historical reference. It predates Mini Studio pages and is not the current
legend.

## Solderless Audio Cap

The cap is a separate original pre-hardware design inspired by the finished
plug-in form of M5Stack caps. It is 84 x 38 x 24 mm across its two closed parts;
the extra thickness makes room for retail modules and two compact lever splices
because the project deliberately refuses a custom PCB. It contains an ATOM
Lite, preassembled PCM1808 line-input module and only factory/precrimped plug-in
connections. Cardputer EXT `5VOUT/GND` provides all power through the same 14-pin
plug that carries data. See [`../docs/AUDIO_CAP_BUILD_GUIDE.md`](../docs/AUDIO_CAP_BUILD_GUIDE.md).

## Cradle design

This is an original open bench/transport cradle, not a copy or redistribution
of the Microgroove MakerWorld enclosure. It uses the Cardputer-ADV's published
84 × 54 mm product envelope and M5Stack's MIT-licensed hardware repository as
mechanical references. No M5Stack mesh and no MakerWorld mesh is included.

| Parameter | Current value |
| --- | ---: |
| Published device footprint | 84.0 × 54.0 mm |
| Nominal clearance | 0.30 mm per side |
| Cradle envelope | 88.0 × 58.0 × 6.0 mm |
| Base | 2.0 mm ventilated frame |
| Port strategy | Open side/end centers; corner-only guides |

The generated binary STL has 336 triangles. Automated tests require every edge
to have exactly two incident triangles and enforce bounds of 88 × 58 × 6 mm.
That proves a closed printable mesh; it does **not** prove fit, shrinkage,
warping, connector access, comfort, or drop retention.

Suggested first prototype settings:

- 0.20 mm layer height, 0.40 mm nozzle;
- 3 walls and 4 top/bottom layers;
- 15–20% infill, PLA for the first dimensional pass;
- print flat, open side upward; no supports expected;
- stop after the first few millimetres if the base is visibly warped.

Do not force the Cardputer into the first print. Confirm width/height with
calipers, lower it straight into the cradle, inspect all ports, and record the
actual printer/material clearance in `docs/CARDPUTER_TESTING.md`. Adjust
`clearance` in the SCAD and the matching `CLEARANCE_MM` generator constant if
needed, regenerate, and repeat.

## Regeneration

The committed SVG and STL are deterministic outputs:

```bash
python tools/generate_hardware_assets.py
python tools/generate_hardware_assets.py --check
python -m unittest tests/test_hardware_assets.py
```

The Python generator uses only the standard library so CI does not depend on a
particular CAD package. OpenSCAD source is provided for human editing; when a
dimension changes, update both parameter sets and let `--check` prevent stale
exports.

## References and licenses

- [M5Stack Cardputer-ADV documentation](https://docs.m5stack.com/en/core/Cardputer-Adv)
  publishes the product dimensions and provides official structure downloads.
- [M5Stack's hardware repository](https://github.com/m5stack/M5_Hardware) is
  MIT licensed. Its structure was inspected as a mechanical reference only.
- [Microgroove](https://github.com/matoslav/MicroGroove) and its
  [MakerWorld enclosure listing](https://makerworld.com/en/models/3046012-microgroove-diy-pocketable-groovebox-sampler)
  remain credited as the inherited instrument and product-presentation
  inspiration. Their shell is not bundled or relabeled here.
