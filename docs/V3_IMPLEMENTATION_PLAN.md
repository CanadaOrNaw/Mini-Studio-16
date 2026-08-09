# Mini Studio 16 implementation and verification plan

Status: implementation branch `agent/v3-alpha-sd-streaming`

This plan keeps the current groovebox usable while proving the risky parts in
the order that can invalidate later work. A feature is not called verified
until the corresponding gate passes on a physical Cardputer-ADV.

## Fixed product targets

- Six independent, SD-backed mono loop tracks, up to 20 seconds each. Track 1
  establishes the exact frame length; tracks 2–6 start at its next boundary
  and use the same frame count.
- Sixteen sampler slots with a 40-second project quota, melodic and sliced-drum
  playback modes, trim, pitch, gain, filter and step automation.
- Sixteen 16-step patterns and a 128-entry pattern chain.
- Five role-neutral event tracks covering 128 bars.
- BMI270 motion mappings and recordable motion automation.
- BLE MIDI input/output/clock/transport.
- USB MIDI input plus MIDI clock, start, continue and stop. Device mode and
  direct USB-host controller mode are separate validation targets; direct
  Yamaha/CYD connections depend on class compliance, OTG cabling and power.
- Long master WAV recording to microSD, independent of the existing short
  RAM-backed resampling workflow.
- A versioned USB serial command protocol and companion CLI for automation,
  diagnostics and OpenClaw-style control.
- Optional stem export for synth 1, synth 2, synth 3 and the combined drum bus.
- Existing speaker, microphone, headphone output and battery support.
- A documented expansion interface for line input and conventional Bluetooth
  A2DP audio. The ESP32-S3 cannot provide Bluetooth Classic A2DP by firmware.

## Architecture boundaries

1. The audio render task never opens, seeks, reads, writes or closes a file.
2. The storage task is the only owner of long-audio `File` objects.
3. Audio crosses the task boundary through bounded single-producer,
   single-consumer rings with monotonic counters.
4. A storage stall produces a counted underrun/overrun and silence/dropout,
   never a blocked audio callback or memory overrun.
5. Long audio is stored as 22.05 kHz, mono, signed 16-bit WAV. Project files
   contain metadata and filenames, not PCM.
6. Existing GBX v1/v2 files remain readable. v3 writes a new version rather
   than changing the legacy binary layout in place.
7. New transport, recording and remote-control paths are additive. Existing
   keyboard control, mic sampling, short resampling, synths and drums remain
   available throughout the migration.
8. USB serial commands are versioned, bounded and non-blocking. Parsing never
   allocates memory or performs file I/O; commands enqueue or request work from
   the subsystem that owns it.
9. Master and stem audio use one real-time tap and a storage-side writer. Stem
   export must not turn the audio render task into a multi-file SD writer.

## Milestones and gates

### M0 — reproducible baseline

- Add a checked-in PlatformIO environment matching the README.
- Compile the unmodified firmware and record flash/RAM usage.
- Preserve a baseline merged image.

Gate: clean compile and link with pinned dependencies.

### M1 — storage diagnostics and transport primitive

- Add a host-tested power-of-two SPSC PCM ring.
- Add on-device SD tests for sequential write/read and six-file round-robin
  reads using the same chunk size as the proposed audio worker.
- Report minimum heap, maximum operation latency and pass/fail thresholds over
  USB serial and on the device.

Gate: no ring corruption in stress tests; the physical card sustains the test
for ten minutes without an operation stall longer than the configured buffer
reservoir.

### M2 — versioned control plane

- Add a bounded `MS16/1` USB serial protocol with request IDs and explicit
  success/error responses.
- Expose status, transport, tempo, note/drum triggers, SD diagnostics and
  master-recorder control without removing device-keyboard control.
- Add a desktop CLI whose framing and response parser are host-tested without
  requiring a connected device.

Gate: malformed/oversized input cannot overflow buffers or stall the main
loop; every request receives a correlated response; host parser/CLI tests and
the target firmware build pass.

### M3 — long master recording and stem capture

- Add a master-bus SPSC recording ring and a storage-owned WAV writer.
- Record to a temporary WAV, finalize its header, then publish a unique file in
  `/groovebox/recordings`.
- Preserve the existing short resampling sampler as a separate workflow.
- Count ring overruns, write failures, maximum SD write latency and frames.
- Capture master plus synth 1/2/3/drum buses into one sequential storage stream
  when stems are enabled, then split/export outside the real-time renderer.

Gate: a 30-minute master recording has a valid header, exact frame count and
zero dropped frames; recovery and stem splitting survive interruption tests.

### M4 — six-track loop engine

- Add one storage worker, per-track playback rings and one recording ring.
- Record to a temporary WAV, finalize its header, then replace the old track.
- Keep a recoverable backup until the replacement is finalized.
- Add empty/armed/recording/playing/muted/error states and underrun counters.
- Add a LOOP page and six-track controls.

Gate: six generated 20-second tracks play for 30 minutes with zero underruns;
recording a seventh input stream while six play is tested separately.

### M5 — sequencer and sampler model

- Expand to 16 patterns and a 128-entry chain with banked UI.
- Introduce GBX v3 and migrate GBX v1/v2 on load.
- Replace RAM-only sample descriptors with hybrid cached/streamed assets.
- Add 16 melodic/sliced slots and parameter locks.

Gate: legacy projects load identically; v3 save/load round-trips byte-stable
metadata; 40 seconds of sample audio remains playable after reboot.

### M6 — event looper, motion, BLE MIDI and USB MIDI

- Add five timestamped event tracks, 128-bar bounds and event decimation.
- Read BMI270 at 50–100 Hz, filter tilt/gyro/jerk and map them to parameters.
- Add BLE MIDI notes, CC, clock and transport with bounded queues.
- Add USB MIDI input, 24 PPQN clock, start/continue/stop and optional song
  position handling. Validate USB-device and USB-host roles independently.

Gate: event save/load round-trip; recorded automation returns to the same
musical tick; BLE disconnect/reconnect and USB attach/detach never stall audio;
external clock drives a repeatable transport without main-loop drift.

### M7 — input and wireless-audio hardware validation

- Prototype a single low-level full-duplex I2S owner for ES8311 TX/RX; do not
  run the independent M5Unified speaker and mic drivers on the same I2S port.
- Measure feedback, clocking and CPU cost on Cardputer-ADV.
- If onboard full duplex is unsuitable, use the expansion audio codec for line
  input and monitored recording.
- Conventional Bluetooth headphones/speakers require an external Bluetooth
  Classic A2DP coprocessor; the S3 firmware exposes a digital PCM interface to
  that board.

Gate: monitored recording has no I2S driver conflicts or unstable feedback;
external audio hardware passes latency and noise checks.

## Initial hardware test matrix

Record the exact card brand/model/capacity/filesystem for every result.

| Test | Minimum run | Pass condition |
| --- | ---: | --- |
| 4 KiB sequential read | 2 min | average payload > 1.0 MB/s |
| 4 KiB sequential write | 2 min | average payload > 0.5 MB/s |
| Six-file 4 KiB round-robin read | 10 min | zero errors; max stall covered by ring |
| Six playback streams | 30 min | zero audio underruns |
| Six playback + one record | 10 min | zero underruns/overruns and valid WAV |
| Master WAV record | 30 min | zero dropped frames; exact playable duration |
| USB serial soak | 10,000 commands | correlated replies; no reboot or audio stall |
| USB MIDI clock | 30 min | stable transport; start/continue/stop repeatable |
| Power loss during recording | 10 cycles | prior loop remains recoverable |
| BLE reconnect while playing | 20 cycles | no audio task stall or reboot |

The thresholds are intentionally conservative starting points. The measured
latency distribution, not the card label or peak throughput, determines the
final chunk and ring sizes.
