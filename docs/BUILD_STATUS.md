# Mini Studio 16 pre-hardware build status

Published branch: `agent/v3-alpha-sd-streaming`

This is the precise verification boundary for the current alpha. “Implemented”
means the production firmware path exists, its hardware-independent behavior is
host-tested, and both target profiles compile/link. “Hardware-verified” remains
false until a physical Cardputer-ADV produces measurements.

## Verified pre-hardware code checkpoint

- Firmware source head: `b2787d6ddfe2db0caee27c9eaea6c325818821bd`
- Pull-request workflow merge SHA: `e3a1d2bf4cbb53df7d359b7c37e8541780bc1dfb`
- GitHub Actions: [run 31406764225](https://github.com/CanadaOrNaw/Mini-Studio-16/actions/runs/31406764225)
- Host tests, AddressSanitizer, UndefinedBehaviorSanitizer, normal firmware,
  USB-host firmware, resource gates, merged-image generation, SD-card package,
  checksum manifest, and artifact upload: **passed**
- Normal image: 181,440 bytes static DRAM; 939,189-byte flash estimate;
  standalone merged-image SHA-256
  `5c591ddcc03fb2e2b2e21f7690b45b5de164a9f9da781c476920572d0d5e5c24`
- USB-host image: 169,872 bytes static DRAM; 957,609-byte flash estimate;
  standalone merged-image SHA-256
  `e167d2bef0b2a80e87d6aecfa3357721cd14df7200a69b0efef0b1b323519869`
- Combined dual-role image SHA-256
  `9f46dfed1ae2b2447cb5526cd0af9791d764cb6a703fa83c7a77f5978f4e58b2`
- [Artifact ID 9070055426](https://github.com/CanadaOrNaw/Mini-Studio-16/actions/runs/31406764225/artifacts/9070055426);
  ZIP SHA-256
  `50875ea36bfca0288757022f86c9d0c356b2c532979a17578543b5799cba71f1`
- Starter SD ZIP SHA-256
  `708e868ea108fee26cbdcd150740ba96e665e31712bca774d4ededb40d174256`

The downloaded artifact was independently extracted; all fifteen manifest
payload entries passed `sha256sum -c SHA256SUMS.txt`. The Normal application at
`0x10000` and USB-host application at `0x300000` were independently compared
against the combined image and are byte-identical; the complete inter-slot gap
contains only `0xFF`. The embedded partition-table SHA-256 is
`b7c4e29a17187775d3a5579a872259c376c47092a8a8d79803aaa4dc37f7e1e2`,
matching `dual-image-layout.json`. The artifact contains the combined image,
both standalone recovery images, application binaries and ELFs, both resource
reports, partition/layout files, starter SD ZIP, license, instructions,
manifest, and build provenance. A later documentation-only head can produce a
different outer ZIP digest because `BUILD_INFO.txt` embeds its workflow merge
SHA; the three image hashes above identify this firmware code checkpoint.

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
- CI compiles and links both target profiles, enforces the DRAM/flash budgets,
  generates both standalone images and the dual-role image, validates slot
  alignment/capacity/content, and uploads image/ELF/layout/size reports.
- Product-layer tests require exactly 56 unique keys, valid page-context
  references, deterministic generated outputs, a 336-triangle STL with every
  edge incident to exactly two faces and exact 88 × 58 × 6 mm bounds, and a
  static Pages artifact with no missing internal links or duplicate HTML IDs.
- Audio Cap tests freeze the one-plug/Cardputer-powered/no-PCB/no-solder
  requirements, protect 5VIN/SD/I2C pins, require Canada/US/EU sourcing for
  every part, validate three watertight meshes, and exercise packet corruption,
  sequence wrap, bounded rings and chunk-independent 48/22.05/44.1 kHz paths.
- CI also builds the pinned original-ESP32 ATOM Lite cap firmware and publishes
  its image/ELF plus the assembly guide beside the two Cardputer images.

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
| USB CDC+MIDI device | 181,440 B | 23,360 B | 939,189 B | 2,060,811 B |
| USB MIDI host | 169,872 B | 34,928 B | 957,609 B | 2,042,391 B |

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

### Solderless Audio Cap hardware

- Buy the exact ATOM Lite, photo-matched preassembled PCM1808 module, header,
  precrimped leads, two internal lever splices and matching hidden ADC plug.
- Print the header gauge first, then the checked-in two-part enclosure. Tune
  header/module/button/jack/snap clearances from actual measurements.
- Verify the Cardputer EXT `5VOUT` budget, voltage sag, temperature and battery
  impact; the cap has no second power connection by design.
- Validate line level/noise/clipping, 48 kHz I2S format/polarity, SPI CRC and
  clock-drift soak, A2DP pair/reconnect/latency and three sink models.

No unimplemented software-only milestone is being intentionally held back.
Failures found by the physical pass become the next bug-fix work, not evidence
that the pre-hardware paths were skipped.
