# Mini Studio 16

SD-backed groovebox, sampler, looper, motion controller, recorder, and MIDI
firmware for the **M5Stack Cardputer-ADV**.

[![License: MIT](https://img.shields.io/badge/license-MIT-orange)](LICENSE)

> **Pre-hardware validation alpha.** The complete software paths described
> below are implemented and built in CI. They have not yet been flashed or
> stress-tested on a physical Cardputer-ADV, so timing, audio quality, USB
> enumeration, BLE behavior, IMU calibration, and SD-card limits remain
> hardware gates—not claims.

Mini Studio 16 is an independent fork of
[Microgroove](https://github.com/matoslav/MicroGroove). It retains the original
instrument and adds the requested long-audio and control systems; it is not an
official Microgroove or lebiro.studio release.

## Implemented instrument

| System | Implemented software | Remaining proof |
| --- | --- | --- |
| Six-track audio looper | Six independent 22.05 kHz mono SD streams, up to 20 seconds; Track 1 fixes the frame length; tracks 2–6 align to its boundary; mute, volume, recovery, and resync | Zero-underrun playback and simultaneous recording on the actual card |
| PO-style sampler | 16 SD-streamed slots sharing a normalized 40-second quota; melodic/sliced modes, 16 slices, trim, pitch, gain, filter, four voices, pattern triggers, and sparse parameter locks | Performance and latency on the actual SD card |
| Sequencer | 16 patterns × 16 steps and a 128-entry chain | Keyboard/UI usability pass |
| Event looper | Five role-mapped drum/bass/chord/lead/sample-control tracks, 1–128 bars, 2,048 events, arm/mute/clear | Long-run timing and live workflow |
| Motion | BMI270 filtering, tilt/accel/gyro/shake/slap sources, four mappings, synth cutoff/resonance targets, MIDI CC, and recordable automation | IMU calibration and gesture thresholds |
| MIDI | BLE MIDI input/output; composite USB CDC+MIDI device image; separate direct USB-MIDI host image; notes, CC, clock, song position, start/continue/stop | Enumeration, reconnect, clock jitter, OTG/VBUS behavior |
| Recording | Long master WAVs and optional five-bus master/synth1/synth2/synth3/drums stem containers on SD | Zero-drop 30-minute captures and power-cut cycles |
| Control | Bounded `MS16/1` USB serial protocol, desktop CLI, JSON, monitor, discovery, fuzzing, and soak client | Device-side 10,000-command soak |
| Existing Microgroove | Three synth tracks, eight drum lanes, keyboard, short sampler/resampler gestures, speaker, mic, headphones, projects, and factory content retained; samples use adaptive RAM or transparent SD-stream fallback | Regression pass on hardware |
| Expanded audio | Line input and conventional Bluetooth headphones/speakers | External codec/A2DP expansion hardware; unavailable from stock S3 firmware alone |

The DAW is optional: songs can be captured to master WAV or exported as five
stems without removing any inherited standalone workflow.

## What is already verified without hardware

- Both ESP32-S3 images compile and link in the pinned PlatformIO toolchain.
- The normal image provides native USB CDC plus class-compliant USB MIDI device
  support; the alternate image builds the ESP-IDF USB-host class driver.
- Host tests cover loop synchronization/stalls, streamed sample pitch/EOF/
  underrun/voice stealing, sampler quota/slices/locks, the 128-bar event model,
  motion filtering/cooldowns, MIDI parsing/transport, BLE framing, USB-MIDI host
  descriptor parsing, WAV/stem recovery, project layouts, protocol fuzzing,
  CLI behavior, and concurrent ring ordering.
- GitHub runs the host suite plus AddressSanitizer and UndefinedBehaviorSanitizer.
- Firmware size is checked from the ESP32 linker sections using the same DRAM
  accounting rules as PlatformIO, with a 200 KiB static-DRAM ceiling so runtime
  workers, wireless stacks, and the 8-bit UI canvas retain heap.
- GBX v7 persists the expanded sequencer, sampler, locks, event tracks, motion
  mappings, and six-loop mixer while retaining v1–v6 loading.
- Every long-audio subsystem uses bounded RAM rings; SD files are owned by
  storage workers, not opened or touched by the real-time renderer.
- The inherited mic-to-drum and short-resample gestures use the streamed
  recorder when the adaptive RAM pool is unavailable, avoiding an 84 KiB
  whole-take scratch allocation without removing those workflows.
- Interrupted master, stem, loop, and streamed-sample temporary files are
  recovered when structurally valid or preserved as `.bad` for diagnosis.

The exact evidence boundary is maintained in
[`docs/BUILD_STATUS.md`](docs/BUILD_STATUS.md).

## Build and flash

Build the normal computer-facing image:

```bash
pio run -e m5stack-cardputer-adv
```

Build the alternate direct-controller USB-host image:

```bash
pio run -e m5stack-cardputer-adv-usb-host
```

Upload the normal image over USB:

```bash
pio run -e m5stack-cardputer-adv -t upload --upload-port /dev/ttyACM0
```

CI also publishes merged 8 MB images that flash at offset `0x0`:

```bash
esptool.py --chip esp32s3 write_flash 0x0 microgroove-v3-alpha.bin
```

Each artifact includes `SHA256SUMS.txt`, `BUILD_INFO.txt`, both application
ELFs/images, both merged images, both resource reports, the license, and a
`Mini-Studio-16_SD.zip` starter card image. Verify the extracted artifact with
`sha256sum -c SHA256SUMS.txt` before flashing. The starter image is packaged
with normalized metadata so identical source trees produce identical ZIPs.

The host image is named `microgroove-v3-alpha-usb-host.bin`. Flashing it
replaces the normal CDC+MIDI image; the ESP32-S3's single native USB PHY cannot
serve both device and host roles simultaneously.

Run all desktop checks:

```bash
bash tests/run_host_tests.sh
bash tests/run_sanitizers.sh
```

Follow [`docs/CARDPUTER_TESTING.md`](docs/CARDPUTER_TESTING.md) when the device
arrives. Back up the SD card before flashing development firmware.

## Standalone controls added by Mini Studio 16

Tap `ctrl` to cycle through the original and new pages.

| Page | Controls |
| --- | --- |
| SAMPLE browser | `x/b` chooses slot, `v/c` chooses WAV, `/` assigns; hold `.` records mic; hold `n` records master bus |
| SAMPLE performance | 16 white/performance keys trigger pitches or slices; REC writes quantized events |
| SAMPLE edit | `tab` cycles browser/sound/step-lock; `v/c` selects parameter; `x/b` edits; hold `m` + `x/b` selects step; `/` clears a lock; `,` clears the sample event |
| LOOPS | `v/c` selects L1–L6, `x/b` changes volume, `/` records/stops, `.` mutes, `z` clears |
| EVENT | `v/c` selects one of five tracks, `x/b` changes bar length, `/` arms, `.` mutes, `z` clears |
| MOTION | `v/c` selects mapping, `x/b` selects source, `.` advances target, `z` clears |
| SONG | Existing 128-chain controls remain; hold `.` toggles master recording and hold `n` toggles stem recording |
| SD TEST | `/` starts the diagnostic pass |

The original keys, synth/drum performance, mic sampling, short resampling,
pattern editing, project slots, and song controls remain available. See
[`docs/USER_MANUAL.md`](docs/USER_MANUAL.md) for the complete map.

## Remote control

Install `pyserial` and use the checked-in CLI:

```bash
python -m pip install pyserial
python tools/ministudio_cli.py ports
python tools/ministudio_cli.py --port /dev/ttyACM0 status
python tools/ministudio_cli.py --port /dev/ttyACM0 loop 1 record
python tools/ministudio_cli.py --port /dev/ttyACM0 loop 1 volume 80
python tools/ministudio_cli.py --port /dev/ttyACM0 sample-record 1 bus melodic
python tools/ministudio_cli.py --port /dev/ttyACM0 master start
python tools/ministudio_cli.py --port /dev/ttyACM0 master stop
python tools/ministudio_cli.py --port /dev/ttyACM0 --json midi-status
python tools/hardware_smoke.py --port /dev/ttyACM0 --sd-test \
  --output cardputer-smoke.json
```

The bounded wire protocol and every command are documented in
[`docs/CONTROL_PROTOCOL.md`](docs/CONTROL_PROTOCOL.md).

## SD layout and audio formats

Use a FAT32 microSD card for the first hardware pass:

```text
/groovebox/
├── projects/       P1.gbx–P8.gbx, current write version GBX v7
├── samples/        adaptive RAM/streamed drum samples and 16-slot WAV assets
├── wavetables/     optional single-cycle WAVs
├── loops/          L1.wav–L6.wav and recovery files
├── recordings/     master WAVs, stem containers, and recovered takes
└── diag/           temporary benchmark files, removed after the test
```

Long internal audio uses 22,050 Hz mono signed 16-bit PCM. The streamed sampler
accepts mono 16-bit WAV assets and normalizes their duration to the engine rate
for the 40-second quota. Legacy Microgroove sample loading remains intact.

## Architecture

```text
microSD ⇄ SD arbiter/storage workers ⇄ bounded RAM rings ⇄ core-0 renderer ⇄ ES8311
```

The audio task never performs filesystem calls. Storage stalls become counted
underruns or dropped frames, and every subsystem publishes buffer/error/latency
telemetry through the UI or `MS16/1` status commands. A recursive SD arbiter
serializes FatFS access across loop, sampler, recorder, diagnostics, and project
operations without moving file I/O into the render callback.

## Remaining gates

No additional software-only milestone is intentionally deferred. The next
required evidence depends on the physical Cardputer-ADV, the exact SD card, or
external expansion hardware:

1. flash, boot, heap/stack telemetry, display, keyboard, speaker, mic, and
   headphone regression;
2. SD throughput/stall diagnostics, six-stream playback, concurrent recording,
   long master/stem captures, and power-cut recovery;
3. BLE MIDI and USB device/host enumeration, reconnect, timing, cable, and VBUS;
4. BMI270 calibration and live event/motion workflow;
5. lower-level ES8311 full-duplex experiments;
6. line input and conventional Bluetooth audio cap design/validation.

The cap pin/protocol boundary is specified in
[`docs/AUDIO_EXPANSION.md`](docs/AUDIO_EXPANSION.md); its packet layout and CRC
are already covered by host/sanitizer tests.

Passing compilation or host tests does not substitute for those measurements.

## Upstream, attribution, and license

Mini Studio 16 is a modified fork of
[Microgroove](https://github.com/matoslav/MicroGroove), created by
[lebiro.studio](https://lebiro.studio) and published by
[matoslav](https://github.com/matoslav). Microgroove supplied the original
synth, drum, sequencing, sampling, UI, hardware, and project-storage foundation
used here. If that foundation is useful, visit the upstream project and
[support its creator](https://ko-fi.com/makarov87).

The upstream copyright and full MIT permission notice are retained unchanged in
[`LICENSE`](LICENSE):

> Copyright (c) 2026 lebiro.studio

Microgroove credits parts of its synth voice, 808 drum synthesis, and audio-task
architecture to
[Cardputer-Adv-Tracker](https://github.com/qwertyuu/Cardputer-Adv-Tracker) by
**qwertyuu**, also MIT licensed. That attribution remains in `LICENSE`. The
factory sample pack is identified upstream as CC0 by lebiro.studio.

SD-card research was informed by
[bmorcelli/Launcher](https://github.com/bmorcelli/Launcher), whose Cardputer SD
file-handling paths were a useful reference for chunked I/O and failure
behavior. No Launcher source code is included; this acknowledges research
influence rather than a code-derived license obligation.

All modifications are distributed under the same MIT License. “Mini Studio 16”
identifies this fork; “Microgroove” and the original artwork belong to their
respective upstream creators.
