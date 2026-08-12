# Mini Studio 16 user manual

This manual covers the inherited Microgroove instrument plus Mini Studio 16's
SD looper, streamed sampler, event/motion pages, recorders, and MIDI/control
paths. Hardware-dependent behavior is still alpha until validated on a physical
Cardputer-ADV.

The complete printable physical legend is
[`hardware/mini-studio-16-button-layout.svg`](../hardware/mini-studio-16-button-layout.svg).
The [project site](https://canadaornaw.github.io/MiniStudio.github.io/) presents the
same 56 keys as an interactive page-by-page map. Both are generated from
`hardware/button-layout.json`; this manual remains authoritative for detailed
context behavior.

## Interaction rule

Most labeled keys have a tap action and a 450 ms hold action. A footer progress
bar appears before a hold fires. Arrow keys repeat. Tap `ctrl` to cycle pages;
hold it to return to PATTERN.

## Global keys

| Key | Tap | Hold |
| --- | --- | --- |
| `` ` 1 2 3 `` | Select synth 1/2/3 or drums | Mute/unmute that part |
| `4 5 6 7 8 9 0 -` | Pattern 1–8 in current A/B bank; page-specific selection | Clone pattern on PATTERN/SOUND |
| `tab` | Toggle pattern bank A/B | — |
| `=` | Show load slot | Load project |
| `del` | Show save slot | Save project |
| `opt` / `alt` | BPM −/+ | Octave −/+; on SONG, project slot −/+ |
| `z` | Clear current cell/page item | Clear pattern on PATTERN/SOUND |
| `x c v b` | Left/down/up/right | Repeat while held |
| `n` | Toggle song mode; page-specific action | Short master resample, streamed bus sample, or stems on SONG |
| `m` | Accent/fine/step modifier | — |
| `,` | Toggle slide or clear sampler step | — |
| `.` | Preview/page action | Mic sample or master recorder on SONG |
| `/` | REC or page action | — |
| `space` | Play/stop | Restart from top |

Holding LOAD and SAVE together loads the built-in demo.

## Synths and drums

Synth tracks 1–3 independently select one of three engines and support one to
three voices:

- `MG/303` is the unchanged original Microgroove voice: saw, square, triangle,
  sine, wavetable, low-pass SVF, decay envelopes, accent, and mono slide. Old
  projects always select this engine.
- `MGX` adds proper amplitude/filter ADSR envelopes, LP/BP/HP output, pulse
  width, sub oscillator, drive, velocity-to-amp/filter, and one LFO routed to
  pitch, filter, PWM, or amplitude.
- `FM4` is genuine four-operator phase modulation with eight routing
  algorithms, operator ratios/levels/ADSR, modulation index, and feedback.

Mono `MGX`/`FM4` notes enter their release envelope on key/MIDI note-off.
Poly mode records chords up to three notes. Event tracks record both note-on
and note-off so ADSR performances replay without stuck sustained notes.

The drum track contains eight lanes. Each can use 808 synthesis, 909 synthesis,
or the inherited sample engine, with volume, tune, decay, and choke group.
Short files use the adaptive RAM pool when it fits; otherwise the same lane
gesture transparently uses a streamed sampler slot.

On PATTERN/SOUND, the home-row piano plays synths. With drums selected,
`fn shift a s d f g h` trigger lanes 1–8. Hold `m` for accent; overlapping mono
notes create slides, while overlapping poly notes create chords.

## PATTERN page

- Sixteen patterns contain sixteen steps each.
- `tab` selects bank A (1–8) or B (9–16); `4` through `-` select within it.
- Pattern changes while playing are bar-quantized.
- `/` arms live recording. Playing quantizes to the nearest step; while stopped,
  notes/pads step-write and advance.
- Arrows move the cursor; `z` clears a cell; hold `z` clears the pattern.
- With REC on while playing, hold `z` for live erase.
- Hold a pattern key to clone into that slot.

## SOUND page

`v/c` chooses a row, `x/b` changes it, and hold `m` for fine changes.
Piano/pads audition the current sound; releasing a key releases MGX/FM4.

For synth tracks, tap `tab` to cycle small banks. `COMMON` contains engine,
voice count, and volume. `MG/303` retains its original nine-row page. `MGX`
has oscillator, filter, amp-envelope, filter-envelope, and LFO banks. `FM4`
has one global bank plus one six-row bank for each operator. Selecting a new
engine moves directly to that engine's first useful bank; each engine's patch
is retained when switching away and back.

Drum rows remain lane, engine, type/sample, volume, tune, decay, and choke.

## FX and VOCODER pages

FX provides seven bounded master effects: reverb, delay, chorus, flanger,
tremolo, vibrato, and filter. `v/c` selects a row, `x/b` edits, and `/`
toggles the selected effect. Mix is per effect; feedback, rate, and filter are
shared. The audio task receives settings through a block-safe snapshot.

VOCODER is an eight-band carrier/modulator processor. Loop 1 is a working
modulator without extra hardware. The onboard mic and Audio Cap line choices
are present and persisted, but live full-duplex validation is deliberately a
hardware test. Use arrows to edit enabled/source/formant/Q/envelopes/noise/gate
and `/` to toggle it.

## CHORD page (HiChord workflow)

The seven chord buttons are `fn shift a s d f g` (degrees I–VII). Hold one or
two of `x/c/v/b` while pressing a chord for the eight directional chord
variants. The harmony core provides 10 scales, 28 chord types, three maps,
per-degree inversion/octave/lock state, root or slash bass, and voice leading.
With slash bass selected, holding one chord and pressing another uses the first
degree as the slash bass.

The MODE row selects Play, Strum, Lead, Drone, Arpeggio, Repeat, Mic Sample,
Drum, Drum Loops, Auto Drum, Sequencer, Chord Hiro, Ear Trainer, Tuner, or
Mixer. Lead is monophonic and a new key replaces its current note; Drone
latches one chord until another chord or mode replaces it. Strum has 120, 80,
and 40 ms spacing. Arp patterns are Up/Down/Up-Down/Down-Up/Random/Fingerpick
with Arp-only, Chord+Arp, or Rhythm+Arp layers. Arp and Repeat offer 1/1,
1/2, 1/4, 1/8, 1/16, 1/16-triplet, 1/32, swing-8 and swing-16 rates. In arp
mode `tab` advances rate (`m+tab` advances layer); in repeat mode it advances
rate. The sequence length is selectable at 4/8/12/16 steps. Leaving a playing
chord sequence, Drum Loop, or Drone arms the first empty audio loop and
bounces it at the exact loop boundary.

Drum mode selects seven kits; Drum Loops expose seven styles × eight
variations (56 grooves), while Auto Drum uses held direction keys for quarter,
eighth, sixteenth, thirty-second, triplet and swing triggers. Tuner uses a
held AUX mic capture and displays Hz, MIDI note, and cents. Mic Sample records
up to three seconds, detects the root, maps it chromatically, and enters Lead.
Chord Hiro starts with PLAY and grades timing at the documented difficulty
windows. Ear Trainer auditions one chord on levels 1/3 or a four-chord
progression on levels 2/4; levels 3/4 also require the directional variant.
Tap AUX for a root hint. Mixer chord keys control L1–L6 mute and metronome.
Pattern keys `4`–`7` recall four presets; hold them to store.

The serial protocol additionally exposes exact chord locks, inversion, octave
shift, arp/layer/rate, and remote chord note control.

## KO page (PO-33 performance workflow)

Pattern keys `4`–`-` engage effects 1–8 immediately; switch the A/B bank for
effects 9–16. Effects are momentary and return dry when released. While PLAY
or REC is active, the effect is written to the same quantized 16-step position
as note/sample recording. The 15 effects are loop lengths, unison variants,
octave shifts, stutters, scratches, 6/8 quantize, retrigger, and reverse.
`n` cycles swing and `z` clears the selected step effect.

## SAMPLE page

This page combines the original sample browser with the new 16-slot
SD-streamed instrument.

### Browser mode

- `v/c` selects a WAV in `/groovebox/samples`.
- `x/b` selects streamed slot 1–16.
- Tap `n` toggles melodic/sliced mode and updates an occupied slot.
- `/` assigns the highlighted file to the streamed slot.
- Tap `.` previews the streamed slot or highlighted legacy file.
- Tap pattern keys `4`–`-` to load the highlighted file into drum lane 1–8;
  the firmware selects RAM or streamed playback according to available memory
  (the inherited workflow).
- Hold `.` and keep holding to stream the built-in mic into the current slot;
  release to stop/finalize.
- Hold `n` to start/stop a streamed master-bus recording into the current slot.
- `z` clears the current streamed slot and its sequence events.
- Tap LOAD to copy the current whole sound (or selected slice); tap SAVE on a
  destination to paste. A same-slot slice paste is metadata-only; a cross-slot
  paste asynchronously rebuilds a real destination WAV from PCM. Source and
  destination assets must currently have the same rate; a mismatch rejects
  explicitly instead of creating a wrong hidden reference.

Slots share 40 seconds after normalization to the 22.05 kHz engine rate.
Assignment rejects an asset that would exceed the remaining quota.

### Performance

Sixteen keys trigger the current slot:

```text
fn shift a s d f g h j k l ; ' enter q w
```

In melodic mode they are chromatic keys; in sliced mode they trigger slices
1–16. Up to four streamed voices overlap. With global REC armed, performance
is recorded into the 16-step sampler sequence.

### Sound and step-lock editing

Tap `tab` to cycle BROWSER → SOUND → LOCK.

- In SOUND/LOCK, `v/c` selects pitch, gain, cutoff, resonance, trim start, or
  trim length.
- `x/b` adjusts the value.
- Hold `m` while pressing `x/b` to select pattern step 1–16.
- LOCK changes write only the selected slot/pattern/step.
- `/` clears the current lock.
- `,` clears the sampler event at the current slot/pattern/step.

Trim changes re-slice sliced slots. Locks are sparse and bounded; the UI reports
when the lock table is full.

## LOOPS page

Six independent WAV tracks live at `/groovebox/loops/L1.wav`–`L6.wav`.

- `v/c` or pattern keys 1–6 select a track.
- `/` arms recording; press again to stop/finalize free-length L1 early.
- L1 is free length up to 20 seconds or fixed to 1–8 bars. Hold `m` and press
  `x/b` to choose FREE/1–8 bars. Fixed mode plays a four-beat count-in and
  auto-stops at the exact frame count.
- L2–L6 wait for the next L1 boundary and automatically stop at L1's frame
  count. An early stop request means “finish at that boundary,” never a short
  rejected take. After finalize, the cursor advances to the next empty track.
- `x/b` changes volume in 5% steps.
- `.` mutes/unmutes without losing phase.
- `tab` solos/unsolos the selected track; `n` pauses/resumes the loop timeline;
  `,` toggles the phase-locked metronome.
- `z` clears a track. Clearing L1 clears the shared timeline/all loop tracks.

An SD underrun is counted; the track stays silent and re-primes for a later
boundary so delayed storage never shifts its phase.

## EVENT page

Five event tracks can each span 1–128 bars.

- `v/c` or pattern keys 1–5 select a track.
- `x/b` changes its bar length.
- `/` arms/disarms recording.
- `.` mutes/unmutes playback.
- `z` clears that track.

Live synth notes, drum hits, streamed sample triggers, and mapped motion/control
values are timestamped into the armed track. Capacity is 2,048 events total;
overflow is rejected and counted rather than overwriting data.

The event clock is 24 PPQN (96 ticks/bar), so a 128-bar take has real
sub-step timing rather than stretching 128 bars across only 2,048 sixteenth
positions. GBX v5–v8 event positions migrate from the legacy 16-unit scale.

## MEDO page

Five role tracks map to Drum, Bass, Chord, Lead, and Sample. `v/c` selects
role/quantize/volume/octave/shared bars/scale/arp; `x/b` edits. `/` arms the
role for additive overdub and `z` clears it. The first/shared performance
length is 1–128 bars and applies to every role. Quantize choices are As Played,
Snap 16, and MEDO Groove. Natural, major-pentatonic, and minor-pentatonic note
maps and per-role octave/level are persisted.

`tab` toggles the Chord-role arpeggiator. Its Up/Down/Up-Down/Random direction
and ×1/×2/×4/×8 rate are real note scheduling, not display-only state. Role
levels are stored as dynamic mixer gain flags in newly recorded events, so
changing a role volume also changes existing playback rather than baking the
old level into every event.

Click/Press/Slide are represented by keys and pressure/modifier controls;
Slap/Tilt/Shake/Wiggle/Move come from the BMI270 motion layer. All eight have
defined MIDI messages, and decimated controls can be recorded into event
automation. Physical gesture thresholds remain part of Cardputer calibration.

## MOTION page

Four mappings translate BMI270 motion into synth parameters and MIDI CC.

- `v/c` selects mapping 1–4.
- `x/b` cycles tilt X, tilt Y, acceleration, gyro, shake, and slap sources.
- `.` cycles synth 1/2/3 cutoff/resonance targets.
- `z` clears the mapping.

Defaults map tilt X to synth-1 cutoff and tilt Y to synth-2 cutoff. Meaningful
changes emit MIDI channel 16 CC 16–19. When an event track is armed, decimated
control values are recorded and replayed at musical steps.

## SONG page, projects, master, and stems

The song chain contains 128 entries, displayed 64 at a time. Arrows move;
pattern keys place the selected A/B-bank pattern; `z` clears; tap `.` sets the
loop start; tap `n` toggles song/pattern mode. Hold `opt/alt` to select project
P1–P8, then hold LOAD/SAVE.

GBX v9 saves patterns, chain, all three selectable synth-engine patches,
synth/drums, legacy sample references, all 16
streamed slots/parameters/slices/events/locks, five event tracks, motion
mappings, PO effect locks/swing, HiChord harmony/modes/presets/practice,
MEDO settings, master effects/vocoder, and six-loop volume/mute state. GBX
v1–v8 files still load and select the original `MG/303` engine where needed.

On SONG:

- hold `.` to start/stop a long master WAV;
- hold `n` to start/stop a five-bus stem container.

Master and stem capture are mutually exclusive. Status shows elapsed seconds
and dropped frames. Copy `.mss` to a computer and run:

```bash
python tools/split_stems.py STEM001.mss exported-stems
```

The original short resampler remains available by holding `n` while a pattern
is playing on non-SONG/non-SAMPLE pages, then tapping a drum pad to commit.

## SD TEST page

Press `/` to run sequential write/read and six-file round-robin diagnostics
while audio continues. Save the screen and serial `SDDIAG` line. The test is a
screening tool; it does not replace the long validation in
`CARDPUTER_TESTING.md`.

## MIDI

The normal image advertises BLE MIDI as `Mini Studio 16` and presents composite
USB CDC+MIDI to a computer. Inputs handle notes, CC, clock, song position,
start, continue, and stop. Keyboard/motion/transport events are mirrored out
over connected BLE/USB device transports.

Synth input mapping:

- MIDI channels 1–3 play synth tracks 1–3; channel 10 notes 36–43 play drums.
- On channels 1–3, CC74 controls cutoff, CC71 resonance, and CC7 volume.
- On any channel, CC20–22 control synth 1–3 cutoff and CC23–25 control synth
  1–3 resonance.
- Cutoff/resonance CC changes are recordable into an armed event track.
- On `FM4`, cutoff mappings control modulation index and resonance mappings
  control feedback; `MGX` uses its filter cutoff/resonance.
- Internal transport sends bounded 24-PPQN clock plus start/continue/stop; a
  late main loop drops/counts excess catch-up pulses instead of flooding MIDI.
- Cardputer synth-key releases emit note-off so a connected DAW does not retain
  stuck notes.

The combined image stores Normal and USB Host as separate applications because
the S3 PHY cannot serve both roles simultaneously. At the common startup
screen, press `Tab` to validate/select the other role and reboot, or any other
key to continue. The USB-host application accepts a direct class-compliant
USB-MIDI controller and is input-only for its first hardware pass; it disables
CDC. Standalone images remain available for recovery/debugging.

## Serial CLI

The normal image accepts bounded `MS16/1` commands for every major subsystem,
including project status/save/load, so an agent can create a take and publish
the complete instrument state without simulating keyboard holds.
Examples:

```bash
python tools/ministudio_cli.py ports
python tools/ministudio_cli.py --port /dev/ttyACM0 status
python tools/ministudio_cli.py --port /dev/ttyACM0 loop-status
python tools/ministudio_cli.py --port /dev/ttyACM0 sample-status
python tools/ministudio_cli.py --port /dev/ttyACM0 event-status
python tools/ministudio_cli.py --port /dev/ttyACM0 midi-status
python tools/ministudio_cli.py --port /dev/ttyACM0 synth-engine 1 fm4
python tools/ministudio_cli.py --port /dev/ttyACM0 synth-set 1 fm.algorithm 5
python tools/ministudio_cli.py --port /dev/ttyACM0 synth-set 1 fm.op2.ratio 200
python tools/ministudio_cli.py --port /dev/ttyACM0 synth-status
python tools/ministudio_cli.py --port /dev/ttyACM0 boot-status
python tools/ministudio_cli.py --port /dev/ttyACM0 boot-mode host
```

See `CONTROL_PROTOCOL.md` for the full command table.

## Optional solderless Audio Cap

The optional cap adds 3.5 mm stereo line input (summed to Mini Studio's mono
engine) and Bluetooth Classic output to ordinary headphones/speakers. It is
powered entirely through its one 14-pin Cardputer plug. Monitoring is off at
boot to prevent feedback. Set a low level first, then pair:

```bash
python tools/ministudio_cli.py --port /dev/ttyACM0 cap-status
python tools/ministudio_cli.py --port /dev/ttyACM0 cap-monitor 20
python tools/ministudio_cli.py --port /dev/ttyACM0 cap-pair
```

You may also press the cap's protected pair button. Purple means discovery,
green means A2DP connected, blue means the line ADC/bridge is ready, and red
means stop and inspect diagnostics. Removing the cap leaves the normal
speaker, mic, headphone and MIDI workflows unchanged. See
`AUDIO_CAP_BUILD_GUIDE.md` before assembly or first power.

## SD card layout

```text
/groovebox/
  projects/       P1.gbx–P8.gbx and backups
  samples/        legacy and streamed WAV assets
  wavetables/     optional single-cycle WAVs
  loops/          L1.wav–L6.wav
  recordings/     master WAVs and stem containers
  diag/           temporary diagnostic files
```

Use FAT32 for the first hardware-validation pass. Back up the card before
intentional power-loss tests.

## License and credits

Mini Studio 16 remains MIT licensed and retains the complete Microgroove
copyright/permission notice. Microgroove is by lebiro.studio/matoslav and
credits Cardputer-Adv-Tracker by qwertyuu. The factory sample pack is CC0 by
lebiro.studio. Launcher by bmorcelli informed SD chunking/failure research; no
Launcher code is included.
