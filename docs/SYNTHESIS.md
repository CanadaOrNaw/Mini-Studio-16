# Mini Studio 16 synthesis architecture

Expanded synthesis is additive. Every track owns three fixed-size engine
states and selects one at runtime; the core-0 audio renderer only calls
`SynthTrack::render()`. No engine allocates, touches files, or constructs
routing graphs in the per-sample path. The expanded `MGX`/`FM4` engines use
the interpolated 256-point sine table for every per-sample sine; the
inherited `MG/303` path keeps its original per-sample `sinf()` filter
coefficient exactly as upstream wrote it, because its output is guarded by
a golden PCM hash (P2-19: an earlier revision of this page overclaimed
"no engine calls a transcendental per sample").

## Engines and compatibility

| Engine | Signal path | Compatibility contract |
| --- | --- | --- |
| `MG/303` | Original `SynthVoice` oscillator → original low-pass SVF → original exponential-decay amplitude | The inherited code path, first-free/quietest voice allocation, accent, slide, oscillators, wavetable, and 1–3 voices are unchanged; a golden 4,096-frame PCM hash guards it |
| `MGX` | Oscillator + optional sub → bounded drive → LP/BP/HP SVF → ADSR/VCA | New behavior is isolated from legacy projects; amp/filter ADSR, PWM, velocity, and one LFO share a coherent subtractive patch |
| `FM4` | Four sine operators in one of eight fixed graphs → velocity/VCA | Actual audio-rate phase modulation, fixed 32-bit phase accumulators, 256-point interpolated Q15 sine table, per-operator ADSR/ratio/level, and bounded one-sample operator-4 feedback |

GBX v8 appends 98 bytes of engine/patch state per synth track to the v7
payload. The existing base record still stores voice count and the original
MG/303 patch. GBX v1–v7 load through their unchanged layouts and explicitly
select `MG/303`; v8 validation completes before project state is applied.

## MGX parameter model

The selected additions all belong to one subtractive path:

- original oscillator set and wavetable bank;
- pulse width for square/pulse sounds and a one-octave-down square sub;
- amplitude and filter ADSR, each with 0–5,000 ms A/D/R and 0–100% sustain;
- existing state-variable filter with selectable low-, band-, or high-pass
  output;
- one 0.05–20 Hz LFO assigned to pitch, filter, PWM, or amplitude;
- independent velocity-to-amplitude and velocity-to-filter amounts;
- inexpensive polynomial saturation, exactly bypassed at zero drive.

Oscillator unison was not added. Track polyphony already supplies up to three
voices, and multiplying oscillators again would consume the DSP headroom that
must also cover FM, drums, streaming, loops, recording, MIDI, and motion.

## FM algorithms

Operators are numbered 1–4; arrows point from modulator to destination.
(P2-19: the A2 row now matches the shipped code, where operator 4 also
modulates 3 before both feed 2 — the pinned offline-render hashes lock this
behavior, so the diagram was corrected rather than the DSP.)

| UI | Routing | Carriers |
| --- | --- | --- |
| A1 | 4 → 3 → 2 → 1 | 1 |
| A2 | 4 → 3, then (3 + 4) → 2 → 1 | 1 |
| A3 | 4 → 3 and 2 → 1 | 1, 3 |
| A4 | 4 → 3 → 2 plus 1 | 1, 2 |
| A5 | 4 → 3 plus 2 plus 1 | 1, 2, 3 |
| A6 | 4 → 1, 2, 3 | 1, 2, 3 |
| A7 | 4 → 3 → 1, 2 | 1, 2 |
| A8 | four additive operators | 1, 2, 3, 4 |

The global modulation index scales every algorithm edge. Operator 4 feedback
is limited to the previous sample and clamps both the stored sample and
user-controlled amount. Ratios cover 0.25–16.00. Each operator's phase resets
deterministically on note-on and advances by its ratio-derived increment.

## Controls and performance evidence

The SOUND page exposes small engine-specific banks, and the serial protocol
uses the same validated parameter registry. See `CONTROL_PROTOCOL.md` for all
names and integer units.

Host gates cover ADSR sample progression, ratios/increments, all algorithms,
feedback bounds, deterministic note reset, three-voice allocation, engine
switching, named parameter bounds, v8 encode/decode/malformed data, bank
cycling, legacy PCM hash `a202afdc`, and offline FM hashes/spectral sidebands.
The fixed state is 956 bytes per track (2,868 bytes for all three) in the host
C++11 layout; target linker reports remain authoritative for ESP32-S3 memory.

The renderer publishes block count, last/max render microseconds, missed
11.61 ms deadlines, and the exact deadline through `synth status`. Software
supports three FM voices per track, but the safe simultaneous limit is not
claimed until the physical Cardputer runs the full-load matrix in
`CARDPUTER_TESTING.md`.
