# Mini Studio 16 pre-hardware build status

Published branch: `agent/v3-alpha-sd-streaming`

This is the precise verification boundary for the current alpha. “Implemented”
means the production firmware path exists, its hardware-independent behavior is
host-tested, and both target profiles compile/link. “Hardware-verified” remains
false until a physical Cardputer-ADV produces measurements.

## Implemented

- Original Microgroove synth, drums, keyboard, UI, short mic sampler, short
  master resampler, project slots, speaker, mic, headphone, and SD workflows
  are retained.
- Six SD-backed loop tracks implement exact Track-1 frame length, later-track
  boundary scheduling, record/play/mute/volume/clear, underrun silence,
  boundary re-prime, temporary-file publication, and boot recovery.
- The streamed sampler implements 16 slots, a normalized 40-second quota,
  melodic and sliced modes, trim/pitch/gain/filter, four playback voices,
  per-step triggers and sparse locks, master-bus/mic streamed recording, and
  interruption recovery.
- The sequencer has 16 patterns, 16 steps, A/B banks, and a 128-entry chain.
- The event looper has five tracks, 1–128 bars, 2,048 bounded events, and
  note/drum/sample/control recording.
- BMI270 motion filtering produces tilt X/Y, acceleration, gyro, shake, and
  slap sources; four recordable/MIDI-CC mappings target synth cutoff/resonance.
- BLE MIDI input/output and the shared note/CC/clock/song-position/start/
  continue/stop pipeline are implemented.
- The normal firmware profile builds composite USB CDC+MIDI device mode. A
  second firmware profile builds a class-compliant USB-MIDI controller host
  using ESP-IDF's USB Host API; it is input-only for the first hardware pass.
- Long master WAV and five-bus stem capture use storage workers and bounded
  rings. The desktop splitter emits master, synth 1/2/3, and drums WAVs.
- The bounded `MS16/1` control protocol and CLI expose status, transport,
  tempo, notes/drums, SD test, recorders, loops, sampler, event looper, motion,
  and MIDI telemetry.
- One recursive SD arbiter serializes all FatFS traffic and measures calls,
  contention, failures, and maximum hold time.
- GBX v7 saves the expanded sequencer, sampler, locks, event data, motion
  mappings, and loop mixer. GBX v1–v6 remain readable.
- Normal and USB-host merged 8 MB flash images are produced by CI.

## Verified by automated tests

- All firmware translation units and the top-level sketch pass a host C++11
  syntax/integration compile.
- A concurrent one-million-value SPSC test preserves ordering.
- Loop tests cover frame-boundary arithmetic, exact common length, mute/volume,
  injected storage stalls, underrun silence, re-prime, record overrun, and
  recovery state.
- Sampler tests cover quota normalization, trim/slicing, locks, pitch, EOF,
  underrun, and four-voice stealing.
- Event tests cover five tracks, 128 bars, ordering, bounds, and capacity.
- Motion tests cover filtering, range, gesture cooldown, and mappings.
- MIDI tests cover running status, realtime interleave, velocity-zero note-off,
  queue overflow, 24-PPQN stepping, song position, and transport.
- BLE codec and USB-host descriptor/event-packet parsing have isolated tests.
- Protocol parsing is fuzzed with 100,000 deterministic malformed inputs; the
  line buffer survives overflow recovery and a 10,000-line soak.
- WAV, master-session, stem-header/split, temporary-file recovery helpers,
  CLI framing, firmware merge commands, and ESP32 linker-section budgeting are
  tested.
- GitHub runs the host suite and separate AddressSanitizer plus
  UndefinedBehaviorSanitizer jobs.
- CI compiles and links both target profiles, enforces the DRAM/flash budgets,
  generates both merged images, and uploads the image/ELF/size reports.

## Resource boundary

The budget tool mirrors PlatformIO espressif32 6.7.0 section accounting:
`.dram0.data`, `.dram0.bss`, and `.noinit` are static DRAM. It does not use the
misleading Berkeley `size` summary, which includes ESP32 virtual/padding
sections and previously produced a false 1.15 MB result.

The CI ceiling is 262,144 static DRAM bytes, 80% of the board definition's
327,680-byte data budget. This is a regression gate, not runtime-heap proof.
Boot telemetry reports internal free heap and largest block after subsystem
initialization; only hardware can validate task stacks, library allocations,
and the adaptive legacy sample pool together.

## Only remaining gates

### Stock Cardputer-ADV

- Flash both profiles and confirm boot/UI/keyboard/audio regression.
- Capture boot heap, largest-block, subsystem, SD-arbiter, and MIDI telemetry.
- Run the exact SD card through repeated diagnostics and long concurrent
  playback/recording tests.
- Verify six 20-second loops for 30 minutes with zero phase drift/underruns.
- Verify 40 seconds of slot audio after save/reboot and exercise locks/slices.
- Verify 30-minute master/stem files, zero drops, and intentional power cuts.
- Verify built-in mic behavior and test whether a lower-level ES8311 path can
  safely provide full-duplex input/output.
- Verify BLE MIDI reconnect and USB CDC+MIDI enumeration/clock timing.
- Verify the USB-host profile with the Yamaha/CYD, correct OTG adapter, and safe
  VBUS power; test attach/detach and malformed/non-MIDI devices.
- Calibrate BMI270 gestures and motion automation.

### Additional hardware

- A real line input requires an external ADC/codec or board modification.
- Conventional Bluetooth headphone/speaker audio requires an external
  Bluetooth Classic A2DP coprocessor; ESP32-S3 firmware alone cannot supply it.

No unimplemented software-only milestone is being intentionally held back.
Failures found by the physical pass become the next bug-fix work, not evidence
that the pre-hardware paths were skipped.
