# Mini Studio 16 pre-hardware build status

Published branch: `agent/v3-alpha-sd-streaming`

This is the precise verification boundary for the current alpha. “Implemented”
means the production firmware path exists, its hardware-independent behavior is
host-tested, and both target profiles compile/link. “Hardware-verified” remains
false until a physical Cardputer-ADV produces measurements.

## Verified pre-hardware code checkpoint

- Firmware source head: `21725e82c4ae6636455f1cde2dc303d356031184`
- GitHub Actions: [run 31379497311](https://github.com/CanadaOrNaw/Mini-Studio-16/actions/runs/31379497311)
- Host tests, AddressSanitizer, UndefinedBehaviorSanitizer, normal firmware,
  USB-host firmware, resource gates, merged-image generation, SD-card package,
  checksum manifest, and artifact upload: **passed**
- Normal image: 181,424 bytes static DRAM; 935,317-byte flash estimate;
  merged-image SHA-256 `ebb7feee85852a35f1cee3f8f6833767e9c8f92d2d07f4d280e6c7d227de7631`
- USB-host image: 169,856 bytes static DRAM; 953,701-byte flash estimate;
  merged-image SHA-256 `7a08cba7fe10e5f18641a1506c66faf1e1af6927be1632f86dd1c2718739d6d8`
- [Artifact ID 9059387691](https://github.com/CanadaOrNaw/Mini-Studio-16/actions/runs/31379497311/artifacts/9059387691);
  ZIP SHA-256
  `d74deaeab6baa5990cb73bac2cdf74af674d1ab819de278943574d434c055af5`
- Starter SD ZIP SHA-256
  `708e868ea108fee26cbdcd150740ba96e665e31712bca774d4ededb40d174256`

The downloaded artifact was independently extracted; all eleven payload
entries passed `sha256sum -c SHA256SUMS.txt`. The artifact contains both merged
8 MB flash images, firmware binaries and ELFs, both resource reports, the
starter SD ZIP, license, SD instructions, manifest, and build provenance. A
later documentation-only head can produce a different outer ZIP digest because
`BUILD_INFO.txt` embeds its workflow merge SHA; the two merged-image hashes
above identify the firmware code checkpoint.

## Implemented

- Original Microgroove synth, drums, keyboard, UI, short mic sampler, short
  master resampler, project slots, speaker, mic, headphone, and SD workflows
  are retained.
- Each of the three synth tracks now selects one of three fixed-size engines:
  exact original `MG/303`, expanded subtractive `MGX`, or true four-operator
  `FM4`. Engine dispatch performs no heap allocation, file I/O, or per-sample
  transcendental sine calls.
- `MGX` adds amplitude/filter ADSR, LP/BP/HP SVF output, PWM, sub oscillator,
  assignable LFO, velocity routing, and bounded drive without altering the
  legacy `MG/303` render path.
- `FM4` uses audio-rate phase modulation, a 256-entry interpolated sine table,
  phase accumulators, eight fixed algorithms, operator ratios/levels/ADSR,
  modulation index, and bounded operator-4 feedback. It is real FM rather than
  detuned oscillators.
- SOUND exposes engine/common, legacy, MGX, FM-global, and operator banks while
  preserving the existing one-key editing/audition flow. The serial protocol
  and desktop CLI expose the same validated engine/parameter model plus note
  release and DSP timing telemetry.
- Six SD-backed loop tracks implement exact Track-1 frame length, later-track
  boundary scheduling, record/play/mute/volume/clear, underrun silence,
  boundary re-prime, temporary-file publication, and boot recovery.
- The streamed sampler implements 16 slots, a normalized 40-second quota,
  melodic and sliced modes, trim/pitch/gain/filter, four playback voices,
  per-step triggers and sparse locks, master-bus/mic streamed recording, and
  interruption recovery.
- The sequencer has 16 patterns, 16 steps, A/B banks, and a 128-entry chain.
  Sparse-start and entry-128 loop wrapping are covered by a pure chain test.
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
  MIDI telemetry, and project status/save/load.
- One recursive SD arbiter serializes all FatFS traffic and measures calls,
  contention, failures, and maximum hold time.
- GBX v8 saves the expanded sequencer, sampler, locks, event data, motion
  mappings, loop mixer, and all three synth engine patches. GBX v1–v7 remain
  readable and explicitly migrate to the original `MG/303` engine.
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
  Live and replayed note releases are recorded explicitly so ADSR engines do
  not leave sustained voices stuck.
- Motion tests cover filtering, range, gesture cooldown, and mappings.
- MIDI tests cover running status, realtime interleave, velocity-zero note-off,
  queue overflow, 24-PPQN stepping, song position, and transport.
- BLE codec and USB-host descriptor/event-packet parsing have isolated tests.
- Protocol parsing is fuzzed with 100,000 deterministic malformed inputs; the
  line buffer survives overflow recovery and a 10,000-line soak.
- WAV, master-session, stem-header/split, temporary-file recovery helpers,
  CLI framing, firmware merge commands, and ESP32 linker-section budgeting are
  tested.
- A fake-serial test covers the machine-readable hardware-arrival smoke runner,
  including correlated probes and terminal SD diagnostic pass/fail behavior.
- Synthesis tests cover the original `MG/303` golden render, phase/ratio math,
  ADSR state, all eight FM algorithms, operator modulation, feedback bounds,
  deterministic voice reset/allocation, engine switching, validated parameter
  ranges, SOUND bank behavior, and GBX v8 round-trip/migration/malformed input.
- Offline render tests require finite, bounded, non-silent deterministic PCM.
  They also verify that obvious FM modulation changes both waveform hashes and
  sideband energy rather than merely detuning an oscillator.
- The original `MG/303` golden hash is `a202afdc`; the known base and modulated
  offline-render hashes are `ac9acded` and `96bd5991` respectively.
- On-device DSP telemetry measures last/worst 256-frame render time, the
  11.61 ms deadline, and missed deadlines; protocol status/reset commands and
  the hardware smoke runner expose it for the arrival benchmark.
- GitHub runs the host suite and separate AddressSanitizer plus
  UndefinedBehaviorSanitizer jobs.
- CI compiles and links both target profiles, enforces the DRAM/flash budgets,
  generates both merged images, and uploads the image/ELF/size reports.

## Resource boundary

The budget tool mirrors PlatformIO espressif32 6.7.0 section accounting:
`.dram0.data`, `.dram0.bss`, and `.noinit` are static DRAM. It does not use the
misleading Berkeley `size` summary, which includes ESP32 virtual/padding
sections and previously produced a false 1.15 MB result.

The CI ceiling is 204,800 static DRAM bytes, 62.5% of the board definition's
327,680-byte data budget. The UI canvas is explicitly 8-bit, recorder rings use
bounded reservoirs, and the legacy whole-take scratch
buffer was replaced by the streamed recorder. This is still a regression gate,
not runtime-heap proof.
Boot telemetry reports internal free heap and largest block after subsystem
initialization; only hardware can validate task stacks, library allocations,
and the adaptive legacy sample pool together.

Compared with the verified pre-synthesis checkpoint (`0a5c9ee...`), expanded
synthesis adds 2,320 bytes normal-profile static DRAM and 13,788 bytes estimated
flash; the USB-host profile adds 2,344 bytes static DRAM and 13,696 bytes flash.
The resulting resource boundary is:

| Profile | Static DRAM | Gate headroom | Flash estimate | Flash headroom |
| --- | ---: | ---: | ---: | ---: |
| USB CDC+MIDI device | 181,424 B | 23,376 B | 935,317 B | 2,064,683 B |
| USB MIDI host | 169,856 B | 34,944 B | 953,701 B | 2,046,299 B |

Fixed host-layout measurements are 60 bytes per original voice, 76 bytes per
MGX voice, 124 bytes per FM voice, and 952 bytes for one `SynthTrack` containing
all engine state. All three synth tracks therefore reserve 2,856 bytes of
bounded engine state before target ABI differences. Hardware still determines
the safe *active* FM polyphony under the complete audio/storage workload.

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
- Run `MG/303`, `MGX`, and `FM4` at 1–3 voices, then repeat maximum FM software
  polyphony with loops, sampler, drums, master recording, MIDI, event/motion
  automation, and UI active. The worst render block must remain below 11.61 ms
  with zero missed deadlines; measured results set the product's safe FM limit.

### Additional hardware

- A real line input requires an external ADC/codec or board modification.
- Conventional Bluetooth headphone/speaker audio requires an external
  Bluetooth Classic A2DP coprocessor; ESP32-S3 firmware alone cannot supply it.

No unimplemented software-only milestone is being intentionally held back.
Failures found by the physical pass become the next bug-fix work, not evidence
that the pre-hardware paths were skipped.
