# Build the Mini Studio 16 Audio Cap

This is the beginner version. An adult should supervise a child during printing,
wire stripping/crimping, first power and headphone use. Nothing is soldered and
no circuit board is ordered or fabricated.

## What you are making

When closed, the cap has a 3.5 mm line-input hole, a pairing button, a status
light and one 14-pin plug. It plugs directly into the Cardputer. There is no
second power cord: the Cardputer powers it through the 14-pin plug.

## 1. Buy the parts

The canonical regional list is
[`hardware/audio-cap/bom.json`](../hardware/audio-cap/bom.json). It is tested to
contain purchase routes for Canada, the United States and the European Union.

| Qty | Part | The picture must show |
| ---: | --- | --- |
| 1 | M5Stack ATOM Lite C008 | white 24 mm square case, USB-C, Grove socket |
| 1 | PCM1808 stereo I2S module | 3.5 mm jack; installed DOUT/BCK/LRCK/GND pins; 5 V input |
| 1 | 2x7 extra-long 2.54 mm male header | two rows of seven, at least 11 mm exposed pins |
| 1 set | 2.54 mm housings and precrimped leads | female ends for rear header; male ends for ATOM sockets |
| 1 | M5Stack A096 Grove-to-Dupont lead set | factory-made Grove plug and separate Dupont ends |
| 2 | WAGO 221-413 three-port lever splices | genuine clear body/orange levers, three holes each |
| 1 | short internal USB power plug | male USB-C or Micro-USB matching the PCM1808 board, red/black leads |
| 2 | M2 x 6 nylon screws plus nuts | module retainers; not lid fasteners |

Do not buy a bare PCM1808 chip, a board with empty header holes, or a module
larger than 50.5 x 30.5 mm. Do not buy both USB power-plug types before checking
the module photo. Prices and stock change, so the BOM uses exact manufacturer
parts where possible and a precise search plus photo checklist for generic
items.

## 2. Print the fit gauge first

Download and print:

1. `audio-cap-14pin-fit-gauge.stl`;
2. `audio-cap-base.stl` only after the header fits the gauge;
3. `audio-cap-lid.stl` after the purchased modules sit in the base.

Recommended first pass: PLA, 0.20 mm layers, 0.40 mm nozzle, 3 walls, 20% infill,
no support. The base and lid print flat. Press the 2x7 header into the small fit
gauge by itself—never use the Cardputer as a hammer. If it needs force, scale
only X/Y by 101%, reprint the gauge and record the result.

The generated models are watertight and have checked bounds. Their real fit is
unverified until the Cardputer and the exact retail module revision arrive.

## 3. Flash before assembly

Keep the cap parts away from the Cardputer. Connect only the ATOM Lite to your
computer with a normal USB-C cable, then run:

```bash
pio run -e mini-studio-audio-cap-atom-lite -t upload --upload-port PORT
```

Replace `PORT` with the port shown by PlatformIO. The RGB should light and the
serial monitor should print `MS16_AUDIO_CAP_READY`. Unplug the USB-C cable. It
is a programming cable used before assembly; it is not part of the finished cap.

## 4. Make the signal harness

Power must be off. Use the table exactly. Slide the female ends of seven
precrimped wires onto the rear pins of the 2x7 header, then slide the other ends
onto the named ATOM sockets. Put each single connector into the printed keyed
header holder so it cannot rotate.

| Header position | Connect to |
| --- | --- |
| pin 1 G3 | ATOM G19 |
| pin 3 G4 | ATOM G21 |
| pin 5 G6 | ATOM G22 |
| pin 12 G13 | ATOM G23 |
| pin 13 G5 | ATOM G33 |

Leave header pins 2, 7, 8, 9, 10, 11 and 14 completely empty. Pin 2 is `5VIN`,
not cap power. A wrong power pin can damage the devices.

Connect the ADC using factory-crimped leads:

| PCM1808 | ATOM |
| --- | --- |
| DOUT | G25 |
| BCK | G26 (yellow Grove wire) |
| LRCK | G32 (white Grove wire) |
| GND | ground branch below |
| MCLK | leave empty |

## 5. Make the hidden power branch

Lift every orange lever. Put only bare copper under a lever, then close it and
tug gently. No copper should remain visible.

- `+5 V` splice: header pin 6 `5VOUT`, ATOM `5V`, red ADC-power wire.
- `GND` splice: header pin 4 `GND`, ATOM `GND`, black ADC-power wire.

Plug the short ADC-power connector into the PCM1808 module and tuck the entire
branch into its printed channel. Both lever splices and the ADC plug stay inside
the cap. No wire exits the enclosure.

If a lead is not already bare at the splice end, an adult may strip 7–9 mm or
use a correctly sized precrimp/bare lead. Never twist 5 V and GND together.

## 6. Check before power

Do these five checks aloud:

- “There is exactly one outside plug: the 14-pin header.”
- “Header pin 2 is empty.”
- “Header pin 6 goes to the +5 V splice.”
- “Header pin 4 goes to the ground splice.”
- “No loose copper can touch another pin.”

Use a multimeter continuity check before first power. `5VOUT` to GND must not
be a short. For the first prototype, place a multimeter in series with the
5VOUT branch and record idle, pairing and connected current. This temporary
bench measurement is removed before closing the finished cap.

## 7. Close and plug in

Seat the PCM1808 jack in its end window, seat the ATOM button/light under the
lid features, install the two nylon module retainers and fold wires into the
channels. Hook one long lid edge, press the other until all four compliant tabs
click. To reopen, press one tab at a time with a plastic pick; do not pry at the
Cardputer.

With the Cardputer off, align the keyed 14-pin plug and push straight in. Never
offset it by one pin. Power the Cardputer from its battery or normal USB-C.

## 8. Use it

The line input accepts a stereo 3.5 mm source; start the source volume low.
Mini Studio sums it to mono. Line monitoring defaults off:

```bash
python tools/ministudio_cli.py --port PORT cap-status
python tools/ministudio_cli.py --port PORT cap-monitor 25
python tools/ministudio_cli.py --port PORT cap-pair
```

Put one Bluetooth speaker/headset in pairing mode, then press the cap button or
run `cap-pair`. The first discovered audio-rendering device is selected.

| RGB | Meaning |
| --- | --- |
| blue | ADC/bridge ready |
| purple | discovering/pair armed |
| green | Bluetooth audio connected |
| red | firmware/I2S/SPI fault |

## 9. First-hardware checklist

Do not call the cap finished until all results are recorded in
`docs/CARDPUTER_TESTING.md`: connector fit, 5VOUT current and sag, heat, silence
noise, line clipping level, left/right polarity, ten-minute CRC/sequence soak,
30-minute Bluetooth output, simultaneous SD recording, battery runtime, pair/
reconnect with three different sinks, and five power cycles.

If anything becomes hot, smells unusual, flickers when audio peaks, or reports
CRC/sequence errors, turn the Cardputer off and reopen the cap. Do not “fix” a
power problem by adding a second power cable; record the failure and revise the
module choice or power budget while preserving the one-plug design.
