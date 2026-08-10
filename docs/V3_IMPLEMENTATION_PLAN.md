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
- Composite USB CDC+MIDI device application plus a separately compiled direct
  class-compliant USB-MIDI host application.
- One combined offset-zero flash image containing both USB-role applications,
  with an on-device startup selector that changes roles by validated OTA-slot
  selection and reboot instead of requiring a reflash. The two standalone
  images remain available as recovery and diagnostic artifacts.
- Long master WAV recording, optional five-bus stem export, and the inherited
  short resampler.
- A bounded serial command protocol and CLI for diagnostics/automation.
- Existing speaker, mic, headphone, keyboard, synth, drum, battery, project,
  and factory-content support.
- A future external interface for true line input and conventional Bluetooth
  A2DP audio, neither of which stock S3 firmware can create.

## Expanded synthesis product pillar

Synthesis is a first-class Mini Studio 16 pillar alongside long audio,
sampling, sequencing, event looping, motion, MIDI, and recording. Every synth
track selects a bounded engine through one render/note interface:

| Engine | Product role | Hardware-independent scope |
| --- | --- | --- |
| `MG/303` | Exact original Microgroove voice | Existing oscillators, wavetable selection, SVF low-pass, decay envelopes, accent, slide, mono behavior, and 1–3 voice polyphony remain the original render path |
| `MGX` | Expanded subtractive voice | ADSR amplitude and filter envelopes, LP/BP/HP SVF output, PWM, sub oscillator, one assignable LFO, velocity routing, and inexpensive bounded drive |
| `FM4` | Four-operator phase/frequency-modulation voice | Eight useful fixed algorithms, ratios, per-operator ADSR/level, carriers/modulators, and bounded operator-4 feedback |

This is additive, not a rewrite of `SynthVoice`. Versions GBX v1–v7 always
migrate to `MG/303`, preserving their saved parameters and legacy note/slide
semantics. The expanded engines have separate fixed-size patch and voice state;
switching engines silences/reinitializes transient state but retains each
engine's patch.

The selected `MGX` feature set is intentionally coherent. ADSR, filter mode,
PWM, sub, LFO, velocity, and drive extend a single subtractive signal path and
fit the existing one-key performance model. Oscillator-level unison is not
included in this pass: track polyphony already provides up to three voices,
and multiplying oscillators again would consume the same real-time headroom
needed by FM, SD streams, drums, and recording. That decision can be revisited
only with device benchmark evidence.

`FM4` uses genuine audio-rate phase modulation between operators. Operator
phase is a wrapping fixed-width accumulator; a shared interpolated sine table
replaces per-sample `sinf()`. Algorithms are fixed, acyclic routing graphs so
render cost is bounded and inspectable, with the sole cycle being explicitly
bounded one-sample feedback on operator 4. Ratios and control/envelope values
remain single-precision because the current engine is already float DSP, while
phase and lookup are integer/fixed-width.

Research basis:

- Chowning's original paper establishes audio-rate sinusoidal FM and evolving
  modulation index as the actual spectral mechanism:
  [The Synthesis of Complex Audio Spectra by Means of Frequency Modulation](https://yamahasynth.com/wp-content/uploads/images/fm_synthesispaper-2.pdf).
- Espressif documents the ESP32-S3 single-precision FPU but recommends integer
  representations and lookup/precomputation where practical, and warns that
  double precision is software-emulated:
  [ESP32-S3 speed optimization](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/performance/speed.html).
- Web Audio's scheduled exponential targets were reviewed as a common envelope
  model; Mini Studio 16 deliberately uses finite linear segments so zero-time
  stages and bounded completion remain deterministic without per-sample
  exponentials:
  [Web Audio parameter automation](https://webaudio.github.io/web-audio-api/#dom-audioparam-settargetattime).

SOUND uses small banks rather than one unusable parameter list: engine/common,
MG/303 legacy, MGX oscillator/filter/envelope/LFO, and FM global/operator
banks. Serial control mirrors the same validated parameter IDs. No edit or
protocol operation allocates or performs file I/O in the render path.

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
6. Every persisted format is versioned. GBX v8 is current; v1–v7 load paths are
   retained and explicitly select the original `MG/303` engine.
7. New systems are additive. Original Microgroove workflows remain compiled
   and usable.
8. USB serial parsing is bounded/non-allocating and only requests subsystem
   work; callbacks never perform audio storage I/O.
9. USB device and host are separate images because the S3 exposes one native
   USB PHY/role at a time. They share a startup selector and occupy separate
   OTA app slots in the combined artifact; selecting a role always reboots.
10. Synth engine state is fixed-size. The renderer calls one bounded track
    interface and performs no heap allocation, file I/O, transcendental sine,
    or algorithm graph construction per sample.
11. `MG/303` output is a regression contract. New subtractive behavior lives
    in `MGX`; old projects never opt into a new engine implicitly.

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
| M8 expanded synthesis | Per-track `MG/303`, `MGX`, and true four-operator `FM4`; banked UI/CLI; GBX v8 migration; render telemetry/benchmark | Legacy golden-vector regression; operator/algorithm/ratio/envelope/feedback/switch tests; deterministic offline PCM/spectral statistics; malformed patch validation; both images link under memory gates | Worst-case render time and safe simultaneous FM polyphony with loops/sampler/drums/recording |
| M9 dual-role boot | Common pre-subsystem selector in both apps; `normal`/`usbhost` OTA slots; validated reboot switch; serial/CLI control; combined and standalone artifacts | Pure decision tests; malformed/missing/mismatched slot tests; strict partition-layout and merge-placement tests; both profiles link against the same partition table; combined artifact manifest | Flash combined image, switch both directions repeatedly, power-cut the selection write, and verify USB role/enumeration after every reboot |

## Hardware test sequence

Run in this order so a failing lower layer does not invalidate later results.

1. Flash the combined image at offset zero. Verify the startup selector reports
   both images, defaults to Normal, switches Normal -> USB Host -> Normal for
   twenty cycles without reflashing, and refuses any invalid/missing target.
2. Collect boot/heap/subsystem telemetry in Normal mode; regress display,
   keyboard, speaker, headphones, mic, synths, drums, save/load, and short
   sampling.
3. Cold-mount the FAT32 card ten times. Run SD diagnostics three times while
   audio plays, then a ten-minute storage soak.
4. Exercise loop L1 alone, then L1–L6, then L1–L6 plus one new recording.
5. Fill all 16 sampler slots to the 40-second quota; test pitch/slices/locks;
   save, reboot, and reload.
6. Run five event tracks and motion automation through 128 bars.
7. Capture 1-, 10-, and 30-minute master/stem sessions; inspect frames, drops,
   WAV/container structure, and split stems.
8. Perform intentional recording power cuts on a disposable SD card and verify
   recovery/quarantine behavior.
9. Pair BLE MIDI and run reconnect plus 30-minute external-clock tests.
10. Validate composite USB CDC+MIDI with a computer/DAW while the CLI runs.
11. Select USB Host from the startup menu and validate Yamaha/CYD with the
    correct OTG/VBUS arrangement, including attach/detach and non-MIDI devices.
12. Run the on-device synth benchmark for every engine/voice count, then repeat
    FM4 at maximum software polyphony while loops, sampler, drums, master
    recording, MIDI, and motion are active. Capture worst render block time and
    missed audio deadlines.
13. Calibrate gesture thresholds and test lower-level ES8311 full duplex.

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
| USB-role switch | 20 round trips | no reflash, wrong-role boot, corrupt selection state, or selector lockout |
| Selection power loss | 10 cuts on disposable setup | boots one valid role and retains access to the startup selector |
| Power loss | 10 cycles | prior valid take survives; temp repaired/quarantined |
| Synth offline regression | every host run | legacy golden vector unchanged; deterministic bounded finite MGX/FM4 output; FM modulation changes waveform and spectral energy |
| Synth render deadline | each engine × 1–3 voices | worst 256-frame block remains below 11.61 ms with zero missed deadlines |
| Full-load FM soak | 30 min | chosen FM polyphony plus loops/sampler/drums/recording has no audio deadline miss or reboot |

These thresholds are starting gates, not product specifications. Measured
latency distributions and free-heap telemetry determine any final buffer-size
changes.

## Plan-amendment rule

Hardware results must be committed with the firmware SHA, board revision, card
model/capacity/filesystem/cluster size, test duration, and full telemetry. A
failed gate creates a focused bug-fix item and rerun; it does not get relabeled
as “passed” from theoretical bandwidth or a successful compile.
