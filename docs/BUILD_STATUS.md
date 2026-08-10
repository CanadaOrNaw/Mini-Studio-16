# Mini Studio 16 pre-hardware build status

Published branch: `agent/v3-alpha-sd-streaming`

This is the precise verification boundary for the current alpha. “Implemented”
means the production firmware path exists, its hardware-independent behavior is
host-tested, and all three target profiles compile/link. “Hardware-verified” remains
false until a physical Cardputer-ADV produces measurements.

## Verified pre-hardware code checkpoint

- Firmware source head: `91cfbf4a175fe0460584ebd1dc0b8c90b18c45b2`
- GitHub Actions: [run 31442472843](https://github.com/CanadaOrNaw/Mini-Studio-16/actions/runs/31442472843)
- Host tests, AddressSanitizer, UndefinedBehaviorSanitizer, normal firmware,
  USB-host firmware, original-ESP32 Audio Cap firmware, resource gates, all
  three merged images, dual-role generation, SD-card package, checksum
  manifest, code-defined PCB artifact check, and uploads: **passed**
- Normal image: 185,704 bytes static DRAM; 942,373-byte flash estimate.
- USB-host image: 174,120 bytes static DRAM; 960,861-byte flash estimate.
- Audio Cap PlatformIO report: 70,600 bytes RAM and 1,101,625 bytes flash.
- [Artifact ID 9083418631](https://github.com/CanadaOrNaw/Mini-Studio-16/actions/runs/31442472843/artifacts/9083418631),
  32,332,301 bytes; GitHub artifact digest
  `sha256:948c415b1f024b09f80c4996b1aaa273aafaee340beed6f0d90b6b19cf3e2491`.

The artifact contains the combined Cardputer image, both standalone recovery
images, the one-file Audio Cap image, three application binaries/ELFs, both
Cardputer resource reports, partition/layout files, starter SD ZIP, license,
instructions, per-file checksum manifest, and build provenance. GitHub's
artifact digest covers the complete uploaded ZIP; `SHA256SUMS.txt` is the
builder-facing check after extraction.

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
- A common early-boot selector stores the two applications in labelled OTA
  slots and changes USB role with a validated reboot instead of reflashing.
  `Tab` switches roles before storage/audio/wireless/USB initialization; serial
  and CLI commands expose status and switching from Normal mode. Missing,
  corrupt, or wrong-slot applications are refused without replacing the
  current working image.
- Role switching preserves in-flight writes: active recording, pending mic
  commits, loop clears, sampler mutations, and the destructive SD diagnostic
  each return a specific busy error. Read-only playback remains switchable.
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
- CI produces both standalone merged recovery images plus one combined
  dual-role image with a validated OTA partition layout.
- A canonical 56-key JSON map generates the print-ready Mini Studio SVG and
  drives the responsive interactive project site, so global/page highlights
  cannot silently diverge from the downloadable legend.
- An original parameterized Cardputer-ADV open cradle is checked in as OpenSCAD
  source and deterministic binary STL. It uses M5Stack's published envelope;
  no upstream Microgroove/MakerWorld mesh is redistributed.
- A dedicated Pages workflow validates, packages, and deploys the static site,
  printable legend, editable CAD source, and STL downloads.
- The optional Rev-A Audio Cap now has a fixed ESP32-WROOM-32E/PCM1808
  architecture, separate compiled firmware, a Cardputer SPI task that idles
  almost completely when no cap is detected, shared tested rate conversion,
  line monitoring and pairing controls, regional sourcing/BOM, code-defined
  PCB review artifacts, and watertight deterministic two-part shell STLs.

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
- Boot tests cover both roles, missing/corrupt targets, wrong-slot standalone
  layouts, active recording, pending storage mutations, diagnostic ownership,
  and blocker precedence. The sampler/loop mutation counters publish before
  worker visibility so a fast storage task cannot create an uncounted window.
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
- CI compiles and links both Cardputer profiles plus the Audio Cap profile,
  enforces the Cardputer DRAM/flash budgets,
  generates both standalone images and the dual-role image, validates slot
  alignment/capacity/content, and uploads image/ELF/layout/size reports.
- Product-layer tests require exactly 56 unique keys, valid page-context
  references, deterministic generated outputs, a 336-triangle STL with every
  edge incident to exactly two faces and exact 88 × 58 × 6 mm bounds, and a
  static Pages artifact with no missing internal links or duplicate HTML IDs.

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

Compared with the verified synthesis checkpoint, the complete dual-role boot
selector and shutdown-safety layer add 16 bytes of static DRAM to each profile,
3,872 bytes estimated flash to Normal, and 3,908 bytes estimated flash to USB
Host. The resulting resource boundary is:

| Profile | Static DRAM | Gate headroom | Flash estimate | Flash headroom |
| --- | ---: | ---: | ---: | ---: |
| USB CDC+MIDI device | 185,704 B | 19,096 B | 942,373 B | 2,057,627 B |
| USB MIDI host | 174,120 B | 30,680 B | 960,861 B | 2,039,139 B |

Fixed host-layout measurements are 60 bytes per original voice, 76 bytes per
MGX voice, 124 bytes per FM voice, and 952 bytes for one `SynthTrack` containing
all engine state. All three synth tracks therefore reserve 2,856 bytes of
bounded engine state before target ABI differences. Hardware still determines
the safe *active* FM polyphony under the complete audio/storage workload.

## Only remaining gates

### Stock Cardputer-ADV

- Flash the combined image once; complete twenty Normal/USB-host round trips
  without reflashing and confirm persistent selection, boot/UI/keyboard/audio
  regression, actual USB role, and recovery after selection-time power cuts.
- Flash each standalone recovery image once and confirm it boots while safely
  refusing dual-role switching from an absent or deliberately wrong slot.
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
- Print the Mini Studio cradle, measure the real Cardputer and finished part,
  validate port access/retention/comfort, then tune the documented 0.30 mm per
  side clearance. Photograph the real device before replacing the site's
  original vector mockup with product photography.

### Optional Audio Cap first article

- Confirm every unique production footprint/header height against the physical
  Cardputer and current MPN datasheets before releasing manufacturing Gerbers.
- Assemble Rev A and execute the unpowered, rail/current/thermal, ADC clock,
  SPI soak, line-level/frequency/noise, RF/A2DP latency/reconnect, battery, fit,
  port-alignment, and PETG clip-fatigue checklist.
- A failed/removed/reset cap must leave the stock instrument fully usable and
  must not back-power, crash, or corrupt its SD card.

No unimplemented software-only milestone is being intentionally held back.
Failures found by the physical pass become the next bug-fix work, not evidence
that the pre-hardware paths were skipped.
