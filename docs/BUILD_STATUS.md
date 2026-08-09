# Mini Studio 16 alpha build status

Published branch: `agent/v3-alpha-sd-streaming`

This checkpoint is the hardware-validation alpha, not the completed six-loop
instrument. It adds the lowest-risk product changes, a versioned remote-control
plane, and the first long master-recording path. It does not remove or replace
the inherited keyboard, microphone sampler or short resampling workflow.

## Verified in the local sandbox

- Host syntax/integration compilation covers the sketch, audio engine,
  sequencer, sampler, storage, UI/input and SD diagnostics.
- The single-producer/single-consumer PCM ring preserves one million ordered
  values across concurrent producer/consumer threads.
- The `MS16/1` parser rejects malformed prefixes, IDs, ranges, extra arguments
  and oversized input without dynamic allocation; valid transport, tempo,
  note, drum, diagnostic and recorder requests round-trip in host tests.
- The protocol survives 100,000 deterministic malformed inputs without
  corrupting request canaries, and the line framer completes a 10,000-command
  overflow-recovery soak.
- WAV header tests pin the finalized RIFF/data sizes for mono 16-bit output.
- Recorder lifecycle tests cover start/record/drop/write/stop/complete/error and
  preservation of a recovered take across later sessions.
- MIDI tests cover running status, interleaved realtime clock, note-on velocity
  zero, song position, 24-PPQN step emission, bounded-queue overflow and stop.
- Loop tests pin Track-1 length establishment, next-boundary scheduling, forced
  common frame length, mute transitions and exact wrap position.
- Stem tests cover container headers, interleaving, chunked five-WAV splitting,
  and truncated-payload rejection.
- The desktop CLI request/response framing passes host tests without requiring
  `pyserial` or a connected device.
- GBX v1, v2 and v3 serialized layouts are pinned by compile-time assertions.
- The v3 loader compiles with fixed v1/v2 layouts, migration paths and backup
  fallback for an absent, truncated or corrupt primary. Functional project-I/O
  validation remains part of the on-device test pass.
- The merged-firmware command generator is unit tested for the ESP32-S3 image
  offsets.
- `tests/run_host_tests.sh` completes without errors.

## Verified in GitHub Actions

- The pinned ESP32-S3 PlatformIO environment compiles and links successfully.
- The merged 8 MB flash image is generated successfully.
- The host-test and firmware jobs both pass for published commit `5082cf1` in
  [Actions run 31341532904](https://github.com/CanadaOrNaw/Mini-Studio-16/actions/runs/31341532904).
- The linked target reports 107,472 static RAM bytes (32.8% of the configured
  327,680-byte data region) and 596,381 flash bytes (17.8% of the configured
  application partition). Runtime heap and task-stack margins still require
  physical measurement.
- Build artifact `microgroove-v3-alpha-cardputer-adv` contains the merged image,
  application image, and ELF.
- Artifact ZIP SHA-256:
  `116ba3d8b0fb3a2eb961e85a1cdcfba4b36728a291a8873bc41d4275bd1950db`.
- Merged-image SHA-256:
  `cc0b5d8dd5e3700713d38ffab820fdfb3aab363138b8d6b3a30702e6860a6a49`.

## Requires external verification

- Flashing and booting on a Cardputer-ADV.
- SD throughput/stall results from the exact card using the on-device `SD TEST`
  page.
- Full-duplex ES8311 input/output and every later long-audio milestone.
- A duration-correct master WAV with zero dropped frames on the physical
  Cardputer-ADV. The writer is implemented but not hardware-verified.
- USB serial soak testing and direct USB MIDI device/host testing.
- Five-bus stem capture throughput and recovery on the physical SD card.
- USB MIDI stack integration. The musical parser/queue/clock layer is complete,
  but the pinned Arduino-ESP32 2.0.16 environment needs either explicit
  TinyUSB composite integration or a tested framework migration.

Do not describe this checkpoint as hardware-verified or as implementing the six
loop tracks. Continue with `CARDPUTER_TESTING.md` using the CI-produced merged
binary.
