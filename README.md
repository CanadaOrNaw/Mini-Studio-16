# Mini Studio 16

Experimental groovebox, sampler, looper, motion controller, and MIDI firmware
for the **M5Stack Cardputer-ADV**.

[![License: MIT](https://img.shields.io/badge/license-MIT-orange)](LICENSE)

> **Early hardware-validation alpha:** the current firmware builds and contains
> the expanded sequencer plus SD-card diagnostics. The long-audio looper,
> PO-style sampler, event looper, motion mappings, and BLE MIDI are product
> targets, not finished features yet.

Mini Studio 16 starts from the excellent
[Microgroove](https://github.com/matoslav/MicroGroove) firmware and grows it
toward a more ambitious SD-backed portable studio. It is an independent fork,
not an official Microgroove or lebiro.studio release.

## The instrument we are building

| System | Target | Current state |
| --- | --- | --- |
| Audio looper | Six independent mono tracks, up to 20 seconds each | SD transport and hardware tests in progress |
| Sampler | 16 slots sharing a 40-second project quota; melodic and sliced modes | Existing short RAM sampler only |
| Step sequencer | 16 patterns × 16 steps and a 128-entry chain | Implemented in the alpha |
| Event looper | Five tracks over 128 bars | Planned |
| Motion | BMI270 tilt, gyro, shake, and recordable automation | Planned |
| MIDI | BLE MIDI notes, CC, clock, and transport | Planned |
| Built-in audio | Speaker, microphone, and headphone output | Existing Microgroove paths retained |
| Expanded audio | Line input and conventional Bluetooth audio | Requires expansion hardware |

The engineering plan and the pass/fail gates for each stage are in
[`docs/V3_IMPLEMENTATION_PLAN.md`](docs/V3_IMPLEMENTATION_PLAN.md).

## What works in this alpha

- Microgroove's three synth tracks with mono 303-style behavior or up to
  three-voice polyphony.
- Eight 808/909/sample drum lanes with tuning, decay, level, and choke groups.
- Live microphone sampling and short master resampling using the existing
  RAM-backed engine.
- Sixteen 16-step patterns, selected through A/B banks.
- A 128-entry song chain.
- GBX v3 projects with GBX v1/v2 migration, temporary-file saves, and backup
  recovery.
- An on-device SD test measuring sequential writes, sequential reads,
  six-file round-robin reads, maximum operation latency, and minimum free heap.
- A host-tested single-producer/single-consumer ring for the future boundary
  between real-time audio and SD storage.
- Reproducible GitHub Actions and PlatformIO builds for ESP32-S3.

This checkpoint intentionally does **not** claim that six long audio streams or
simultaneous recording/playback have been proven on physical hardware.

## Flash the current alpha

The build attached to
[GitHub Actions run 31311866811](https://github.com/CanadaOrNaw/Mini-Studio-16/actions/runs/31311866811)
passed the host suite, ESP32-S3 compilation/link, merged-image generation, and
artifact upload.

Download the `microgroove-v3-alpha-cardputer-adv` artifact, extract
`microgroove-v3-alpha.bin`, and flash it at offset `0x0`:

```bash
esptool.py --chip esp32s3 write_flash 0x0 microgroove-v3-alpha.bin
```

Merged-image SHA-256 for that run:

```text
18fed23b856cdf34efbdd7faa7384a798c1ad1cdbdb141474300db25ab86a5ef
```

Back up the SD card before using development firmware. The alpha test creates
temporary files only under `/groovebox/diag`, but it is still pre-release code.
Follow [`docs/CARDPUTER_TESTING.md`](docs/CARDPUTER_TESTING.md) and retain every
`SDDIAG` result, including failures.

## Build and test from source

The pinned environment targets the Cardputer-ADV's 8 MB ESP32-S3 configuration:

```bash
pio run -e m5stack-cardputer-adv
```

Upload directly over USB:

```bash
pio run -e m5stack-cardputer-adv -t upload --upload-port /dev/ttyACM0
```

Run the desktop regression suite:

```bash
./tests/run_host_tests.sh
```

The host suite uses GNU C++11 to match the pinned Arduino-ESP32 toolchain. It
checks the PCM ring under concurrent load, serialized project layouts, the SD
diagnostic source, the firmware modules, the top-level sketch, and merged-image
command generation.

## SD card layout

Use a FAT32 microSD card. Existing Microgroove samples and projects remain
compatible:

```text
/groovebox/
├── projects/       GBX project files
├── samples/        WAV samples
├── wavetables/     optional single-cycle WAVs
└── diag/           temporary SD-test files; removed after the test
```

The starter content remains under [`factory-sd/`](factory-sd/). The inherited
instrument controls are documented in
[`docs/USER_MANUAL.md`](docs/USER_MANUAL.md); alpha-specific controls are in the
[Cardputer testing guide](docs/CARDPUTER_TESTING.md).

## Architecture direction

Long audio will not be loaded into the ESP32-S3's small internal RAM. The
real-time renderer consumes deterministic RAM rings while a separate storage
worker handles variable-latency SD operations:

```text
microSD ⇄ storage task ⇄ bounded RAM rings ⇄ audio render task ⇄ ES8311
```

The audio task is never allowed to open, seek, read, write, or close a file.
Storage stalls become counted underruns or overruns instead of blocking the
audio deadline. Buffer sizes will be chosen from physical Cardputer-ADV results,
not SD-card labels or theoretical throughput.

## Project status

- **M0:** reproducible build — passed in GitHub Actions.
- **M1:** SD diagnostics and transport primitive — implemented; physical test
  results pending.
- **M2:** six-track audio looper — pending M1 hardware data.
- **M3:** complete 16-slot sampler and parameter automation — planned.
- **M4:** five-track event looper, motion, and BLE MIDI — planned.
- **M5:** full-duplex input and optional audio expansion — planned.

See [`docs/BUILD_STATUS.md`](docs/BUILD_STATUS.md) for the exact verification
boundary. Please report bugs with the firmware commit, Cardputer-ADV revision,
SD-card model/filesystem, and the complete serial output.

## Upstream, attribution, and license

Mini Studio 16 is a modified fork of
[Microgroove](https://github.com/matoslav/MicroGroove), created by
[lebiro.studio](https://lebiro.studio) and published by
[matoslav](https://github.com/matoslav). Microgroove supplied the original
synth, drum, sequencing, sampling, UI, hardware, and project-storage foundation
used here. If you find that foundation useful, please visit the upstream project
and [support its creator](https://ko-fi.com/makarov87).

The upstream copyright and full MIT permission notice are retained unchanged in
[`LICENSE`](LICENSE), as required when redistributing substantial portions of
the software:

> Copyright (c) 2026 lebiro.studio

Microgroove itself credits parts of its synth voice, 808 drum synthesis, and
audio-task architecture to
[Cardputer-Adv-Tracker](https://github.com/qwertyuu/Cardputer-Adv-Tracker) by
**qwertyuu**, also under the MIT License. That attribution remains in the
license file. The included factory sample pack is identified upstream as CC0 by
lebiro.studio.

SD-card research was informed by
[bmorcelli/Launcher](https://github.com/bmorcelli/Launcher), whose ESP32
file-handling paths provided a useful reference for chunked I/O and SD failure
behavior. No Launcher source code is included in Mini Studio 16; this is an
acknowledgement of research influence rather than a code-derived license notice.

All modifications in this repository are distributed under the same MIT
License. “Mini Studio 16” identifies this fork; “Microgroove” and the original
project artwork belong to their respective upstream creators.
