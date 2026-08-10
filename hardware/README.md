# Mini Studio 16 printable hardware

The fork now has its own reproducible physical assets:

| File | Purpose | Status |
| --- | --- | --- |
| [`mini-studio-16-button-layout.svg`](mini-studio-16-button-layout.svg) | Full-resolution 56-key tap/hold/performance legend | Generated and data-validated |
| [`button-layout.json`](button-layout.json) | Canonical key and website-context data | Host-tested source of truth |
| [`stl/mini-studio-16-bench-cradle.stl`](stl/mini-studio-16-bench-cradle.stl) | Original open Cardputer-ADV cradle | Watertight and dimension-validated; physical fit pending |
| [`cad/mini-studio-16-bench-cradle.scad`](cad/mini-studio-16-bench-cradle.scad) | Editable OpenSCAD source | Parameters documented below |
| [`audio-cap/stl/mini-studio-audio-cap-base.stl`](audio-cap/stl/mini-studio-audio-cap-base.stl) | Optional cap lower shell | Watertight 84 × 24 × 13 mm Rev-A prototype |
| [`audio-cap/stl/mini-studio-audio-cap-lid.stl`](audio-cap/stl/mini-studio-audio-cap-lid.stl) | Compliant snap lid | Watertight PETG prototype; fatigue pending |
| [`audio-cap/cad/mini-studio-audio-cap.scad`](audio-cap/cad/mini-studio-audio-cap.scad) | Editable two-part cap source | Ports, header opening and RF zone documented |
| [`audio-cap/BOM.csv`](audio-cap/BOM.csv) | Controlled procurement list | Exact MPNs and substitution rules |
| [`audio-cap/pcb/`](audio-cap/pcb/) | Code-defined PCB/schematic review source | Deterministic review artifacts; first-article footprint sign-off pending |

The previous `microgroove_labels_v6_preview.png` remains solely as inherited
historical reference. It predates Mini Studio pages and is not the current
legend.

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

The generated cradle STL has 336 triangles. Automated tests require every edge
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

The optional cap uses the same deterministic mesh path and adds separate
base/lid watertightness and exact-envelope tests. See
[`docs/PRINTING.md`](../docs/PRINTING.md) and
[`docs/AUDIO_CAP_BUILD.md`](../docs/AUDIO_CAP_BUILD.md); do not infer a physical
fit pass from a valid STL.

## References and licenses

- [M5Stack Cardputer-ADV documentation](https://docs.m5stack.com/en/core/Cardputer-Adv)
  publishes the product dimensions and provides official structure downloads.
- [M5Stack's hardware repository](https://github.com/m5stack/M5_Hardware) is
  MIT licensed. Its structure was inspected as a mechanical reference only.
- [Microgroove](https://github.com/matoslav/MicroGroove) and its
  [MakerWorld enclosure listing](https://makerworld.com/en/models/3046012-microgroove-diy-pocketable-groovebox-sampler)
  remain credited as the inherited instrument and product-presentation
  inspiration. Their shell is not bundled or relabeled here.
