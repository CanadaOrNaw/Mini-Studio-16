# Mini Studio Audio Cap Rev A design

**Status: complete pre-hardware engineering package; not electrically,
mechanically, RF, or audio verified. Do not sell or certify Rev A before the
acceptance checklist passes.**

## What it adds

- protected 3.5 mm stereo line input, converted to Mini Studio's 22.05 kHz mono bus;
- conventional Bluetooth Classic A2DP output to headphones/speakers;
- USB-C serial/programming for the cap itself;
- pair button, status LED, removable two-part printed shell;
- fixed, CRC-protected full-duplex SPI link to the Cardputer.

## Architecture freeze

| Block | Rev-A part | Reason |
| --- | --- | --- |
| Bluetooth/bridge MCU | ESP32-WROOM-32E-N4 | Mature certified module, Classic BT + A2DP, open SDK |
| Line ADC | PCM1808PWR | Active stereo 24-bit ADC, single-ended inputs, I2S, broad distribution |
| Audio clock | KC3225Z11.2896C16X00 | Exact 11.2896 MHz family clock for 44.1 kHz |
| USB/UART | CP2102N-A02-GQFN24 | Widely supported USB serial and automatic boot/reset |
| 3.3 V supply | AP63203WU-7 | 2 A synchronous buck with headroom for ESP32 radio bursts |
| Line jack | SJ-3523-SMT-TR | Stocked stereo switched 3.5 mm SMT jack |

The Cardputer sends its 22.05 kHz mono master mix in 128-frame packets. The cap
performs exact 2× interpolation to 44.1 kHz stereo for A2DP. PCM1808 captures
44.1 kHz stereo; a bounded FIR low-pass and 2:1 decimator returns 22.05 kHz
mono. The conversion and protocol are host-tested in
`audio_cap_bridge_core.cpp`.

## Connector assignment

| Cardputer-ADV EXT pin | Cap function | Cap ESP32 pin |
| --- | --- | --- |
| G3 | SPI CS | GPIO15 |
| G4 | SPI clock | GPIO14 |
| G5 | MOSI, Cardputer → cap | GPIO13 |
| G6 | MISO, cap → Cardputer | GPIO12 |
| G13 | IRQ, cap → Cardputer | GPIO4 |
| G15 | cap reset/enable | ESP32 EN/reset circuit |
| 5VOUT | cap input power | AP63203 + PCM1808 analogue rail |
| GND | common ground | ground plane |

The microSD and onboard I2C pins remain untouched. Pin ownership is based on
the official [Cardputer-ADV EXT map](https://docs.m5stack.com/en/core/Cardputer-Adv)
and must be continuity-checked on the physical unit before attaching a powered
prototype.

## Safety and signal rules

- Input is **line level only**. Never connect a passive/active speaker output.
- Add the BOM's ESD and input series components; do not wire a jack straight to
  the ADC.
- Keep the WROOM antenna end free of copper, batteries, screws, and metal-filled
  filament per Espressif's module layout guidance.
- Separate ADC analogue return/filtering from ESP32/buck switching current and
  join them at the specified ground plane point.
- The cap must never back-power the Cardputer through an IO pin.

## Software files

- `audio_cap_firmware/AudioCap.cpp` — original-ESP32 firmware
- `audio_cap.cpp` — Cardputer bridge task and monitoring
- `audio_cap_protocol.*` — fixed wire format/CRC
- `audio_cap_bridge_core.*` — rings and sample-rate conversion
- `platformio.ini` environment `mini-studio-audio-cap`

## Remaining physical gates

Power/current/thermal, RF range, analogue levels/noise, A2DP latency/reconnect,
clock drift, SPI signal integrity, fit, port alignment, and clip fatigue cannot
be proven by compilation. The exact pass/fail procedure is in
`hardware/audio-cap/MECHANICAL_CHECKLIST.md` and `docs/CARDPUTER_TESTING.md`.
