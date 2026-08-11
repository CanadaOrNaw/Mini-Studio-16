# Factory SD card

Copy the `groovebox/` folder to the root of a FAT32 microSD card:

- `groovebox/projects/P1.gbx` — the factory demo project (5 patterns,
  pre-chained in song mode, 128 BPM). Insert card → power on → hold `=`
  (LOAD) until the load completes → tap `n` (SONG) → press space (PLAY).
- `groovebox/samples/` — 14 CC0 drum & texture sounds by lebiro.studio.
  Browse and assign them on the SAMPLE page; they are yours to use anywhere.
- `groovebox/wavetables/` — empty; drop single-cycle WAVs (AKWF) here and
  they appear as extra oscillators.

Mini Studio 16 creates `groovebox/loops`, `groovebox/recordings`, and
`groovebox/diag` as needed. The inherited P1 project loads through the legacy
GBX migration path; saving writes the current GBX v8 format while retaining a
backup of the prior project.

The demo project uses only the built-in 808/909 engines, so it plays even
without the sample pack.
