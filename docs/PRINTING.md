# Print the enclosure parts

All supplied models use millimetres. The normal instrument uses the **bench
cradle**. The optional Audio Cap uses a separate **base + lid**.

## Files

| Part | Print file | Editable source |
| --- | --- | --- |
| Cardputer bench cradle | `hardware/stl/mini-studio-16-bench-cradle.stl` | `hardware/cad/mini-studio-16-bench-cradle.scad` |
| Audio Cap base | `hardware/audio-cap/stl/mini-studio-audio-cap-base.stl` | `hardware/audio-cap/cad/mini-studio-audio-cap.scad` |
| Audio Cap lid | `hardware/audio-cap/stl/mini-studio-audio-cap-lid.stl` | same SCAD file |

## Easy slicer recipe

- Material: **PETG** recommended for the cap's flexible clips; PLA is fine for
  a first fit test or the cradle.
- Layer height: 0.20 mm
- Nozzle: 0.4 mm
- Walls: 3
- Top/bottom layers: 4
- Infill: 20%
- Supports: off
- Scale: 100%
- Base orientation: flat floor on the build plate
- Lid orientation: flat top on the build plate

Do not “auto scale to fit.” A 2.54 mm header will not fit if the model is
silently resized.

## Fit check before electronics

1. Let the print cool completely.
2. Remove strings with fingers or a plastic tool—ask an adult before using a
   knife.
3. Put the empty base against the Cardputer cap connector. Do not force it.
4. Confirm the USB-C and 3.5 mm openings face outward.
5. Press the empty lid on gently and release each side clip one at a time.

The committed STLs are deterministic, watertight pre-hardware Rev-A models.
Actual connector height, button reach, port alignment, RF behavior, and latch
fatigue are physical acceptance tests. Record any needed clearance change in
`hardware/audio-cap/MECHANICAL_CHECKLIST.md` rather than scaling one print.

## Regenerate the files

```sh
python tools/generate_hardware_assets.py
python tools/generate_hardware_assets.py --check
```

The second command succeeds only when the checked-in SVG/STLs match their
source exactly.
