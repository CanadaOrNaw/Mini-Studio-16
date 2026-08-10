# Mini Studio 16 implementation and verification plan

Branch: `agent/v3-alpha-sd-streaming`

All software-only milestones below are implemented. Their automated gates pass
locally and are compiled in GitHub Actions; physical gates are explicitly left
open until the Cardputer-ADV arrives.

## Fixed product targets

- Six independent SD-backed mono loop tracks, up to 20 seconds each. Track 1
  establishes the exact frame length; tracks 2–6 begin on its boundaries.
- Sixteen streamed sampler slots sharing a 40-second normalized quota, melodic
  and sliced playback, trim/pitch/gain/filter, and per-step locks.
- Sixteen 16-step patterns and a 128-entry pattern chain.
- Five event tracks over 128 bars.
- BMI270 motion mapping, MIDI CC, and recorded automation.
- BLE MIDI input/output and clock/transport.
- Composite USB CDC+MIDI device mode plus a separately flashed direct
  class-compliant USB-MIDI host mode.
- Long master WAV recording, optional five-bus stem export, and the inherited
  short resampler.
- A bounded serial command protocol and CLI for diagnostics/automation.
- Existing speaker, mic, headphone, keyboard, synth, drum, battery, project,
  and factory-content support.
- A future external interface for true line input and conventional Bluetooth
  A2DP audio, neither of which stock S3 firmware can create.

## Non-negotiable architecture boundaries

1. The audio renderer performs no file open/read/write/seek/close operation.
2. Storage workers own long-audio files; PCM crosses tasks only through bounded
   SPSC rings.
3. A storage stall causes a counted underrun/overrun and deterministic silence
   or dropped input, never a blocked render callback or out-of-bounds access.
4. Long internal audio is 22.05 kHz mono signed 16-bit PCM. Projects contain
   metadata and filenames, not long PCM payloads.
5. A shared recursive arbiter serializes all FatFS calls and publishes
   contention/failure/max-hold telemetry.
6. Every persisted format is versioned. GBX v7 is current; v1–v6 load paths are
   retained.
7. New systems are additive. Original Microgroove workflows remain compiled
   and usable.
8. USB serial parsing is bounded/non-allocating and only requests subsystem
   work; callbacks never perform audio storage I/O.
9. USB device and host are separate images because the S3 exposes one native
   USB PHY/role at a time.

## Completed milestones

| Milestone | Implemented result | Automated evidence | Physical gate |
| --- | --- | --- | --- |
| M0 reproducible build | Pinned PlatformIO/M5/TinyUSB/NimBLE dependencies; normal and host profiles; merged images | Both profiles compile/link in CI | Flash and boot |
| M1 storage boundary | SPSC rings, SD diagnostic, central arbiter, latency/error/high-water metrics | Concurrent ring test; diagnostic and arbiter integration compile | Exact-card stall distribution and 10-minute soak |
| M2 control plane | `MS16/1`, CLI, JSON, monitor/discovery, soak client | 100k malformed fuzz; 10k line soak; CLI tests | 10k commands while audio plays |
| M3 master/stems | Long master WAV; interleaved five-bus stem container; splitter; temp recovery | WAV/session/header/split/recovery tests | 30-minute zero-drop files and power cuts |
| M4 six loops | L1–L6 record/play/mute/volume/clear; exact timeline; SD refill; underrun resync; recovery | Timeline/stall/record/ring tests | Six streams for 30 minutes; six plus record |
| M5 sampler/sequencer | 16 patterns, 128 chain, 16 streamed slots, 40-second quota, melodic/sliced, trim/params/locks, bus/mic recording, GBX v4+ | Layout, migration syntax, quota/slice/lock/voice tests | Reboot persistence and live latency |
| M6 event/motion/MIDI | Five × 128-bar event tracks; BMI270 mappings/automation; BLE MIDI; composite USB device; alternate USB host | Event/motion/MIDI/BLE/USB descriptor tests; both images link | Gesture calibration, reconnect, enumeration, clock jitter |
| M7 full duplex/expansion | Low-level experiment boundary, cap pin map, fixed PCM packet/CRC contract, and external hardware requirements | Packet layout/CRC/bounds host-tested; no safe analog/RF host-only substitute exists | ES8311 experiment; line/A2DP hardware |

## Hardware test sequence

Run in this order so a failing lower layer does not invalidate later results.

1. Flash normal image; collect boot/heap/subsystem telemetry; regress display,
   keyboard, speaker, headphones, mic, synths, drums, save/load, and short
   sampling.
2. Cold-mount the FAT32 card ten times. Run SD diagnostics three times while
   audio plays, then a ten-minute storage soak.
3. Exercise loop L1 alone, then L1–L6, then L1–L6 plus one new recording.
4. Fill all 16 sampler slots to the 40-second quota; test pitch/slices/locks;
   save, reboot, and reload.
5. Run five event tracks and motion automation through 128 bars.
6. Capture 1-, 10-, and 30-minute master/stem sessions; inspect frames, drops,
   WAV/container structure, and split stems.
7. Perform intentional recording power cuts on a disposable SD card and verify
   recovery/quarantine behavior.
8. Pair BLE MIDI and run reconnect plus 30-minute external-clock tests.
9. Validate composite USB CDC+MIDI with a computer/DAW while the CLI runs.
10. Flash the host image and validate Yamaha/CYD with the correct OTG/VBUS
    arrangement, including attach/detach and non-MIDI devices.
11. Calibrate gesture thresholds and test lower-level ES8311 full duplex.

## Initial pass criteria

| Test | Minimum | Pass condition |
| --- | ---: | --- |
| 4 KiB sequential read | 2 min | average payload > 1.0 MiB/s |
| 4 KiB sequential write | 2 min | average payload > 0.5 MiB/s |
| Six-file round-robin read | 10 min | zero errors; worst stall covered by reservoir |
| Six loop streams | 30 min | zero phase drift; zero audible underruns |
| Six loops + recording | 10 min | zero underruns/overruns; valid published WAV |
| Full sampler quota | reboot cycle | all 16 metadata/events/locks valid and playable |
| Master/stems | 30 min | zero drops; exact duration/frame counts |
| Serial protocol | 10,000 commands | correlated replies; no reboot/audio stall |
| BLE/USB clock | 30 min each | repeatable transport; no stuck notes/reboot |
| Attach/reconnect | 20 cycles | no leak, crash, or audio-task stall |
| Power loss | 10 cycles | prior valid take survives; temp repaired/quarantined |

These thresholds are starting gates, not product specifications. Measured
latency distributions and free-heap telemetry determine any final buffer-size
changes.

## Plan-amendment rule

Hardware results must be committed with the firmware SHA, board revision, card
model/capacity/filesystem/cluster size, test duration, and full telemetry. A
failed gate creates a focused bug-fix item and rerun; it does not get relabeled
as “passed” from theoretical bandwidth or a successful compile.
