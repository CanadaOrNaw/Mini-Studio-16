# Mini Studio 16 pre-hardware build status

Completion branch: `agent/complete-three-in-one`

This is the precise verification boundary for the current alpha. “Implemented”
means the production firmware path exists, its hardware-independent behavior is
host-tested, and both target profiles compile/link. “Hardware-verified” remains
false until a physical Cardputer-ADV produces measurements.

## Three-in-one completion checkpoint

The behavior-level HiChord, PO-33, and MEDO completion is verified at branch
head `f2d356c942233eaf825f86a06b3f08fddd51e403` (pull-request merge test tree
`d7feb365e6f8de03255e2ea3a6128fb895f3749a`). This supersedes the earlier
capacity-only software claim without altering the immutable alpha.1 release.

- Full validation workflow:
  [run 31598932662](https://github.com/CanadaOrNaw/Mini-Studio-16/actions/runs/31598932662)
- Pages/CAD/site workflow:
  [run 31598932652](https://github.com/CanadaOrNaw/Mini-Studio-16/actions/runs/31598932652)
- Host suite, AddressSanitizer + UndefinedBehaviorSanitizer, all three pinned
  firmware builds, both Cardputer resource gates, dual-image merge, package
  manifest, generated-asset checks, and site build: **passed**
- Normal image: 204,728 bytes static DRAM (72 bytes inside the unchanged
  204,800-byte project gate); 985,985-byte flash estimate; application
  SHA-256 `356db35efcbe9c51b77ab95f6a5264d8f23c2e282f55f7855c89dd1310f4c224`
- USB-host image: 193,160 bytes static DRAM; 1,004,317-byte flash estimate;
  application SHA-256
  `49b54dd0bc1a48e759dbf385fefb0e01f88e2b9f9ad4bb352f4d640768ff7e26`
- Combined dual-role image SHA-256
  `8b89d6f04f262ad844bc1b08245d71b4e09f05477e74b78861697578798b8ffc`
- ATOM Lite Audio Cap merged image SHA-256
  `eab53ff27a135cc58844a2fa67ff2f10728a64b76f424d155f98d283ae85f1ca`
- Artifact
  [9142385169](https://github.com/CanadaOrNaw/Mini-Studio-16/actions/runs/31598932662/artifacts/9142385169),
  33,477,295 bytes; outer ZIP SHA-256
  `4d02148026da278697685b224782f7d03f4c7fd6f5332fb6f23280244066627d`
- Every entry in the downloaded `SHA256SUMS.txt` passed independently; the
  starter SD ZIP also retained SHA-256
  `3564b1d151e6b4f7ac622d1f2d33ce6f75f9166b7a80f616be54a2eae69af680`.

The Normal RAM correction did not raise a budget or remove a feature. It
packed bounded held-note/gate state, removed invalid per-layer fixed-loop
request fields, and added a host/sanitizer test for the wrapping compact gate
deadline. The original MG/303 golden hash remains `a202afdc`; FM4 offline
hashes remain `ac9acded` and `96bd5991`.

## Preserved v3.0.0-alpha.1 checkpoint

The following is the immutable prior release evidence. The completion branch
is additive and does not move or overwrite that release.

- Verified firmware/package source head:
  `f978bd991cc578ff080d63ebac5fec743c92f52c`
- Push workflow:
  [run 31542626067](https://github.com/CanadaOrNaw/Mini-Studio-16/actions/runs/31542626067)
- Pull-request workflow:
  [run 31542628616](https://github.com/CanadaOrNaw/Mini-Studio-16/actions/runs/31542628616)
- Host tests, AddressSanitizer, UndefinedBehaviorSanitizer, all three pinned
  firmware builds, both Cardputer resource gates, all one-file merges,
  packaging, and artifact upload: **passed**
- Normal image: 190,024 bytes static DRAM; 946,205-byte flash estimate;
  application SHA-256
  `9732e2a9d8eab9f8ed9ca089a1c94700742ae8e9a9fb3d7051b88a46dc1a6e1f`
- USB-host image: 178,456 bytes static DRAM; 964,741-byte flash estimate;
  application SHA-256
  `5b00e69dcc49214a574cf01775cabf4fa0c97d6fd6df3148a7e5b85bdd7bd6d9`
- Combined dual-role image SHA-256
  `2fe97106a7fc91423a0b668368d81831045699f74bfb65deb95b2ba609061c67`
- ATOM Lite Audio Cap image: 49,016 bytes PlatformIO RAM; 1,132,225-byte
  PlatformIO flash use; one-file merged-image SHA-256
  `eab53ff27a135cc58844a2fa67ff2f10728a64b76f424d155f98d283ae85f1ca`
- Artifact ID
  [9121376552](https://github.com/CanadaOrNaw/Mini-Studio-16/actions/runs/31542626067/artifacts/9121376552)
  (Actions artifact downloads require GitHub sign-in and expire; the release
  workflow is the durable beginner-distribution path); outer ZIP SHA-256
  `76f856fd378d3286cfa7b6fa61fba205caa8eae5442c6bdc378ad4d59a8d13d7`
- Starter SD ZIP SHA-256
  `3564b1d151e6b4f7ac622d1f2d33ce6f75f9166b7a80f616be54a2eae69af680`

The 32,861,874-byte artifact was downloaded and extracted independently. All
33 entries in `SHA256SUMS.txt` passed. The starter SD ZIP passed archive
testing. The Normal application at `0x10000` and USB-host application at
`0x300000` were byte-compared against the combined image and are identical.
The combined-image, application, and partition-table hashes match
`dual-image-layout.json`.

The package includes the combined image, standalone recovery images,
application binaries and ELFs, resource reports, the independently compiled
ATOM Lite cap image/ELF, beginner flashing and cap guides, attribution,
partition/layout files, starter SD ZIP, generated hardware assets, manifest,
and build provenance.

The branch can contain a later documentation-only commit carrying this record;
the source SHA above is the exact tree from which the verified binaries and
published site were generated.

Permanent release
[`v3.0.0-alpha.1`](https://github.com/CanadaOrNaw/Mini-Studio-16/releases/tag/v3.0.0-alpha.1)
targets release-workflow head `5c9b82ab75c1743a5f2d6a2df6613b292eaddb8e`.
Its 32,243,142-byte beginner package has SHA-256
`60a8e327a3a871e0d47eb049a40ac61c8c1ee692518a101bb5b4708c43410671`.
A fresh public, signed-out download passed that release checksum and all 33
inner `SHA256SUMS.txt` entries. Its firmware images are byte-identical to the
verified package-source artifact above.

Standalone public-site commit
[`92012249`](https://github.com/CanadaOrNaw/MiniStudio.github.io/commit/92012249fcf009dcf0e750bdc6f0a76790ce6d87)
publishes source `f978bd99`. `tools/check_live_site.py` fetched the deployed
Pages endpoint and verified content size and SHA-256 for all 15 manifested
downloads (272,341 bytes total), including the regenerated snap-fit cap lid.

## Implemented

- Original Microgroove synth, drums, keyboard, UI, short mic sampler, short
  master resampler, project slots, speaker, mic, headphone, and SD workflows
  are retained.
- The missing three-in-one product layer is now explicit: a seven-button
  HiChord harmony engine and all 15 performance modes; PO sound/slice copy,
  swing and 15 momentary/recordable punch effects; and MEDO five-role
  performance with shared 1–128 bars, three quantizers, scales and all eight
  gesture-to-MIDI mappings.
- HiChord state includes 10 scales, 28 chord types, three maps, locks,
  inversion/octave, root/slash bass, voice leading, strum/arp/repeat/drone,
  seven kits/56 grooves, chord sequence bounce, pitch-detected mic sample,
  tuner, practice/ear modes, loop mixer, effects, vocoder and four presets.
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
  MIDI telemetry, complete chord/PO/MEDO controls, and project status/save/load.
- One recursive SD arbiter serializes all FatFS traffic and measures calls,
  contention, failures, and maximum hold time.
- GBX v9 saves the expanded sequencer, sampler, PO effect locks/swing, HiChord
  modes/harmony/presets, MEDO settings, effects/vocoder, event data, motion,
  loop mixer, and all synth patches. GBX v1–v8 remain readable and explicitly
  migrate to the original `MG/303` engine where required.
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
  underrun, sound/slice copy, and four-voice stealing.
- Three-in-one tests cover every chord type/scale/map, arp schedules, 56
  grooves, pitch detection, PO effect determinism and locks, MEDO roles/scales/
  quantizers/gestures, master effects, vocoder modulation, swing, presets, GBX
  v9 round trips, malformed state, protocol fuzzing and CLI generation.
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
  ranges, SOUND bank behavior, and GBX v9 round-trip/migration/malformed input.
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

The final reconciled firmware resource boundary is:

| Profile | Static DRAM | Gate headroom | Flash estimate | Flash headroom |
| --- | ---: | ---: | ---: | ---: |
| USB CDC+MIDI device | 190,024 B | 14,776 B | 946,205 B | 2,053,795 B |
| USB MIDI host | 178,456 B | 26,344 B | 964,741 B | 2,035,259 B |
| ATOM Lite Audio Cap | 49,016 B* | 278,664 B* | 1,132,225 B* | 178,495 B* |

\* The ATOM row uses PlatformIO's original-ESP32 board totals (327,680-byte RAM
and 1,310,720-byte application partition), not the Cardputer ELF section gate.

Fixed host-layout measurements are 60 bytes per original voice, 76 bytes per
MGX voice, 124 bytes per FM voice, and 956 bytes for one `SynthTrack` containing
all engine state. All three synth tracks therefore reserve 2,868 bytes of
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
