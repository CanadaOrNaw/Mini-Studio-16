# Microgroove — User Manual

*A pocket groovebox & sampler for the M5Stack Cardputer-ADV · [lebiro.studio](https://lebiro.studio)*

**Print it. Flash it. Jam.**

Microgroove turns the Cardputer-ADV into a four-track instrument: three synth tracks (each switchable between a mono 303-style voice and 2–3-voice polyphony for chords) and a drum track with eight lanes of 808/909 synthesis or sample playback. It records your mic, resamples its own output, and saves everything to microSD.

Every key name below matches the **printed v6 keycap labels**. Here is the full map — one key, one function, with the orange (hold) and green (sampling) second functions:

![Microgroove v6 keymap](keymap-v6.png)

Running a stock, unlabeled Cardputer? Use this legend once, then read normally:

| Printed label | Stock key | Printed label | Stock key |
|---|---|---|---|
| **T1 T2 T3 TD** | `` ` `` `1` `2` `3` | **PG** | `ctl` |
| **P1 – P8** | `4` … `-` | **BPM− / BPM+** | `opt` / `alt` |
| **LOAD / SAVE** | `=` / `del` | **CLR** | `z` |
| **◀ ▼ ▲ ▶** | `x` `c` `v` `b` | **SONG / ACC / SLD** | `n` / `m` / `,` |
| **AUX / REC / PLAY** | `.` / `/` / `space` | Piano **C D E F…** | home row from `fn` |
| Sharps **C# D#…** | row above, one key right (`q` `w` `r` `t` `y` …) | Dead keys | `tab` `e` `u` `p` |

The first eight white keys (**C** through **C′**) double as **drum pads 1–8** whenever the drum track is selected.

---

## 1. The one rule

**One key = one function. Hold any orange-labeled key for half a second to get its second function.** A progress bar appears in the footer and fills before the hold fires, so nothing ever triggers by surprise — release early and nothing happens. That's the whole interaction model; everything below is just what each key does.

---

## 2. The factory SD card

Your card (or the `Microgroove_SD_card.zip` download, unzipped to the root of any FAT32 microSD) contains one folder:

```
/groovebox/
  samples/      14 CC0 drum & texture sounds + LICENSE.txt
  projects/     P1.gbx — the factory demo project
  wavetables/   (empty — yours to fill)
```

**To play the factory demo:** insert the card, power on, press any key past the splash, then **hold LOAD** until the bar fills — project P1 loads. Tap **SONG** to enable song mode, then **PLAY**. Five pre-chained patterns play as a full arranged track at 128 BPM: classic acid, an escalation with open hats, an 808 electro beat, a halftime groove, and an ambient wavetable outro with a polyphonic chord stab on track 3. It's a tour of every engine — mute tracks, sweep filters, and switch patterns while it runs. Nothing you do is permanent unless you **hold SAVE**.

**The samples folder** holds the factory sound pack (kicks, snares, hats, percussion, textures — all CC0, use them anywhere). They are not loaded automatically; browse them on the SAMPLE page and assign the ones you want to drum lanes. Add your own sounds by copying WAV files (8 or 16-bit PCM, mono or stereo — converted to mono on load) into this folder. Keep them short: the whole sample pool is ~192 KB of RAM, about 4 seconds of audio total.

**The wavetables folder** accepts single-cycle WAVs (AKWF files work perfectly). Up to 8 appear as extra oscillators alongside the 8 built-ins.

**No card?** Everything still works — hold **LOAD+SAVE** together for a built-in demo pattern, and mic sampling falls back to RAM (samples just won't survive a power cycle).

**A note on project files:** current firmware saves projects in the GBX v2 format (which stores chords). It happily loads older v1 files — like the factory demo — but once you re-save, the file becomes v2 and pre-polyphony firmware can no longer read it.

---

## 3. The four tracks

Select a track by tapping **T1 T2 T3 TD**. Hold the same key half a second to mute or unmute it — this is the core of live arrangement.

**Tracks 1–3 are synths.** Each one is a 303-style voice: an oscillator (saw / square / triangle / sine / wavetable) into a resonant filter with envelope modulation, plus **accent** (louder + brighter at once — the classic 303 bark) and **slide** (a 50 ms glide with a re-squelched filter). Each track has a **VOICES** setting from 1 to 3:

- **VOICES 1 (mono)** — the pure 303. Slide and legato playing work exactly as on the real thing.
- **VOICES 2–3 (poly)** — the track plays chords. Slide is ignored (overlapping notes now mean *chord*, not glide).

Track 3 ships set to 3 voices so chords work out of the box; tracks 1–2 ship mono. Change any of them on the SOUND page.

**Track TD is the drum track:** 8 independent lanes, each set to **808 synthesis**, **909 synthesis**, or **sample playback**, with per-lane volume, tune (±12 semitones), decay, and choke group. The default kit is 808 kick/snare/closed-hat/clap on lanes 1–4 and 909 kick/snare/closed-hat/open-hat on lanes 5–8; the 909 hats share a choke group, so the closed hat cuts the open hat like real hardware.

---

## 4. Pages

Tap **PG** to cycle pages; hold it to jump straight back to PATTERN.

**PATTERN** is the grid: three synth rows on top, eight drum lanes below in TR-style order (kick at the bottom, open sounds up top). Move the cursor with **◀ ▼ ▲ ▶**. Synth cells show the note name; chord cells add small dots at the top-right, one per extra note. A slide is a short orange underline; an accented step has a darker orange background.

**SOUND** shows the selected track's parameters next to a live oscilloscope. Synth tracks: OSC, WTABLE, CUTOFF, RESO, ENV AMT, FLT DEC, AMP DEC, VOLUME, **VOICES**. Drum lanes: LANE, ENGINE, TYPE/SAMPLE, VOLUME, TUNE, DECAY, CHOKE. Pick a row with **▲ ▼**, adjust with **◀ ▶**, and **hold ACC for fine steps**. The piano keys audition the synth; the pads audition drum lanes.

**SAMPLE** is the SD browser for `/groovebox/samples/`. **AUX** previews the highlighted file; **P1–P8** assigns it directly to that drum lane. The RAM meter shows how much of the sample pool is in use.

**SONG** is the 128-slot chain grid, shown 64 entries at a time. Place patterns with **P1–P8**, use **TAB** to choose pattern bank A/B, set the loop start with **AUX**, and step through project slots by holding **BPM−/BPM+**.

**HELP** is the key map, on the device itself.

---

## 5. Sequencing and recording

Patterns are 16 steps, and there are 16 of them. **TAB** selects bank A (patterns 1–8) or B (patterns 9–16); tap **P1–P8** to select within the bank. **Hold to clone** the current pattern into that slot — the fastest way to build variations. While playing, pattern switches are quantized to the bar: tap the next pattern and it takes over cleanly at step 1.

Tap **REC** to arm live recording. **While playing**, everything you play quantizes to the nearest step. **While stopped**, you're in step-write mode: each entry lands at the cursor and the cursor advances.

On a mono synth track, hold **ACC** while playing a note to record an accent, and roll from one note into the next — while both keys briefly overlap — to record a **slide**. That's real 303 fingering.

On a poly synth track, overlapping is how you record **chords**: while playing, notes landing on the same step stack up to three deep; while stopped, press keys together and they step-write as a single chord. To edit a chord's slide flag you'll get a reminder that slide is mono-only.

Cleanup: tap **CLR** to clear the cell under the cursor, hold it to wipe the whole pattern, or hold it *while recording and playing* to erase whatever passes under the playhead — a live eraser.

**Song mode:** lay patterns into the 128-slot chain on the SONG page, tap **SONG** to toggle chain playback, and set where the loop restarts with **AUX**.

---

## 6. Live sampling

**Sample the mic:** select the drum track, put the cursor on a lane, and **hold AUX**. After half a second the transport pauses and the footer bar becomes a live level meter — keep holding and speak, beatbox, or hold the device up to anything (max ~2.6 s). Release: the recording is auto-trimmed, written to SD as `MICnn.wav`, loaded onto the lane, and played back immediately. Tune it, shorten it, sequence it like any drum.

**Resample the machine:** with a groove playing, **hold SONG**. The firmware grabs ~1.9 s of the full engine mix, then asks you to *tap a pad* — the first pad you hit receives the capture as `RSMnn.wav`. Clear the pattern and build something new on top of your own bounce, or pitch it down 12 semitones for an instant halftime remix of yourself.

Both write real WAV files, so sampled sounds reload with your projects by filename. Without an SD card they live in RAM until power-off.

---

## 7. The keyboard is a piano

White keys sit on the home row, labeled **C D E F G A B C′ D′ E′ F′ G′ A′ B′**. Sharps sit on the row above, shifted one key right so each sharp lands *between* its two white neighbors — and the four keys where a real piano has no black key (the E–F and B–C gaps) are intentionally dead. If you can find notes on a piano, you can find them here. Shift the octave by holding **BPM+** or **BPM−**.

---

## 8. Projects

Eight slots on the card (`P1.gbx`–`P8.gbx`) store everything: patterns, song chain, synth parameters (including VOICES), drum lane setups, and sample references. Tap **LOAD** or **SAVE** for info about the current slot; **hold to actually execute** — the progress bar means you'll never save over something by accident. Switch slots by holding **BPM−/BPM+** on the SONG page.

---

## 9. Five-minute quick starts

**First 60 seconds.** Power on → any key → hold **LOAD+SAVE** together → "DEMO" appears with an acid line and a beat. Tap **PLAY**. Hold **T1**: the bassline drops out. Hold it again: it's back. You're performing.

**Build a groove from scratch.** Tap **TD**, tap **REC**, tap **PLAY**. Finger-drum a four-on-the-floor on **pad 5** (909 kick), offbeat hats on **pad 7**, claps on **pad 4**. Tap **T1** and play a bassline on the white keys — hold **ACC** on the hits that should bark, roll between notes for slides. Tap **T3** and lay a chord: press two or three keys together. Hold **P2** to clone everything, mutate the copy, and bounce between the two.

**Make a voice kit.** Cursor onto a drum lane, hold **AUX**, say something short, release. Repeat on two more lanes. Write a beat using only your mouth.

**Bounce and rebuild.** Get a groove going, hold **SONG**, tap **pad 1** when prompted. Hold **CLR** to wipe the pattern, put the bounce on step 1, and build a second layer over your first idea.

**The 303 squelch.** On a mono synth track: OSC to SAW, CUTOFF ~30%, RESO ~70%, ENV AMT ~80%, FLT DEC short. Accent every third or fourth note of a 16th-note line and slide into the downbeat.

---

## 10. Performance cheat sheet

| Move | Keys |
|---|---|
| Drop / bring back a part | hold **T1 T2 T3 TD** |
| Switch pattern on the bar | choose bank with **TAB**, tap **P1–P8** while playing |
| Restart the phrase | hold **PLAY** |
| Live-erase a lane | REC on + hold **CLR** while playing |
| Filter jam | SOUND page, **◀ ▶** on CUTOFF, hold **ACC** for fine |
| Chord stabs on the fly | poly track + REC + press keys together |
| Punch-in fills | hold-clone to a spare pattern beforehand, bounce between them |
| Tempo ride | **BPM− / BPM+** (±1 each tap) |

---

## Build & flash

Arduino IDE or PlatformIO: install ESP32 board support and the **M5Cardputer** library, select the M5Cardputer board (or ESP32-S3 Dev Module with USB CDC on boot), open `Microgroove.ino`, flash. A pre-built binary is on the Releases page for flashing without the Arduino IDE. Full details in the repo README.

---

*Microgroove is open source (MIT). Synth voice, 808 drums, and the audio task architecture are derived from [Cardputer-Adv-Tracker](https://github.com/qwertyuu/Cardputer-Adv-Tracker) by qwertyuu (MIT). Factory sample pack released CC0 by lebiro.studio. If Microgroove earns a place on your desk, Ko-fi keeps the project moving.*
