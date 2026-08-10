# Cardputer-ADV hardware validation guide

Use this guide when the physical device arrives. Record the firmware commit,
board revision, SD make/model/capacity/filesystem/cluster size, exact command,
duration, and complete serial output for every pass or failure.

## 1. Prepare and flash

1. Back up the SD card and use FAT32 for the first pass.
   `Mini-Studio-16_SD.zip` in the CI artifact contains the starter `groovebox/`
   tree; extract that folder at the card root.
2. Build or download a CI artifact only after host, sanitizer, and firmware jobs
   all pass. Run `sha256sum -c SHA256SUMS.txt` from the extracted artifact
   directory and retain `BUILD_INFO.txt` with the test evidence.
3. Flash the normal merged image at offset `0x0`:

   ```bash
   esptool.py --chip esp32s3 write_flash 0x0 microgroove-v3-alpha.bin
   ```

4. Open a 115200-baud serial monitor, press any key to dismiss the inherited
   splash screen, and save the full boot log.
5. Confirm `BOOT_READY` reports `sd=1`, sensible `heapFree`/`heapLargest`, and
   `BOOT_SUBSYSTEM` reports loop, sampler, motion, BLE, and USB availability.
6. Save all recovery and SD-arbiter lines. Do not accept a boot loop, watchdog,
   allocation failure, or subsystem silently unavailable.

After the manual boot check, capture the read-only subsystem probe and SD test
as one JSON evidence file:

```bash
python tools/hardware_smoke.py --port /dev/ttyACM0 --sd-test \
  --output cardputer-smoke.json
```

The harness correlates every response, preserves asynchronous boot/diagnostic
lines, rejects any subsystem error response, and requires terminal `SDDIAG
state=PASS`. It does not replace the audible, timing, power-cut, or USB/BLE
checks below.

Direct source build/upload:

```bash
pio run -e m5stack-cardputer-adv
pio run -e m5stack-cardputer-adv -t upload --upload-port /dev/ttyACM0
```

## 2. Inherited-function regression

Before stressing new systems, verify the original instrument:

- display/splash and every keyboard key;
- all three synth tracks, mono/poly/chords/accent/slide;
- all eight drum lanes, mute, tune, decay, choke, and RAM samples;
- speaker and headphone output;
- built-in mic short sample and short master resample;
- patterns, song chain, project save/load, and v1/v2 migration sample if
  available;
- ten consecutive cold boots with SD mount.

Any regression is a blocker even if the new feature works.

## 3. SD diagnostic

1. Start the demo or a dense pattern and leave it playing.
2. Tap `ctrl` to the SD TEST page and press `/`.
3. Do not remove the card or power while RUNNING.
4. Save the final screen and complete `SDDIAG` line.
5. Repeat three times after cold boots, then run the longer soak/loop tests.

The quick diagnostic writes/reads generated data under `/groovebox/diag`,
checks sequential and six-file round-robin traffic, validates content, and
reports throughput, maximum operation latency, minimum heap, and errors.

Initial quick-screen gates:

- write at least 500 KiB/s;
- sequential and six-file reads at least 1,000 KiB/s;
- no measured read/write/flush stall above 75 ms;
- no data, open, read, write, seek, or cleanup errors.

Preserve failures: maximum stall and minimum heap matter more than PASS/FAIL.

## 4. Six-track looper

Standalone controls: LOOPS page, `v/c` select, `x/b` volume, `/` record/stop,
`.` mute, `z` clear. CLI equivalents:

```bash
python tools/ministudio_cli.py --port /dev/ttyACM0 loop 1 record
python tools/ministudio_cli.py --port /dev/ttyACM0 loop 1 stop
python tools/ministudio_cli.py --port /dev/ttyACM0 loop 2 record
python tools/ministudio_cli.py --port /dev/ttyACM0 loop 1 volume 75
python tools/ministudio_cli.py --port /dev/ttyACM0 loop-status
```

Verify:

- L1 free-stops and fixes the exact timeline, capped at 20 seconds;
- L2–L6 wait for an L1 boundary and stop at exactly L1's frame count;
- mute consumes audio and unmute returns at the correct phase;
- volume changes are click-free enough for use and persist through GBX v7;
- six tracks play for 30 minutes without phase drift or audible underrun;
- an injected/real stall increments underrun and the track returns only at the
  next boundary, never late/off-phase;
- recording while six tracks play produces no ring drops or corrupt WAV;
- clearing L1 clears the shared timeline/all tracks as documented.

## 5. Streamed sampler

Fill all 16 slots with mono 16-bit WAVs. Exercise melodic and sliced modes,
all 16 performance keys, trim, pitch, gain, cutoff, resonance, four overlapping
voices, pattern events, and step locks. Then record both bus and mic sources:

```bash
python tools/ministudio_cli.py --port /dev/ttyACM0 sample-record 1 bus melodic
python tools/ministudio_cli.py --port /dev/ttyACM0 sample-stop 1
python tools/ministudio_cli.py --port /dev/ttyACM0 sample-record 2 mic sliced
python tools/ministudio_cli.py --port /dev/ttyACM0 sample-stop 2
python tools/ministudio_cli.py --port /dev/ttyACM0 sample-status
```

The total normalized duration must stop at 40 seconds. Save a project, reboot,
reload, and confirm all slots, modes, trims, parameters, events, and locks.
Require zero unexplained underruns/drops and no audio-task stalls.

## 6. Event looper and motion

Set each of five tracks to a different length including 128 bars. Arm, perform
synth/drum/sample parts, mute/unmute, save/reboot, and confirm musical tick
alignment. Fill toward the bounded 2,048-event capacity and verify graceful
rejection rather than corruption.

On MOTION, exercise tilt X/Y, acceleration, gyro, shake, and slap. Calibrate
neutral position, useful range, noise, cooldown, and false triggers. Record
motion automation into an armed event track and confirm repeatable playback.
Verify outgoing mapping CCs on channel 16, controllers 16–19.

## 7. Master and stems

Standalone SONG-page controls: hold `.` toggles master; hold `n` toggles stems.
CLI:

```bash
python tools/ministudio_cli.py --port /dev/ttyACM0 master start
python tools/ministudio_cli.py --port /dev/ttyACM0 status
python tools/ministudio_cli.py --port /dev/ttyACM0 master stop
python tools/ministudio_cli.py --port /dev/ttyACM0 stems start
python tools/ministudio_cli.py --port /dev/ttyACM0 stems stop
```

Run 1-, 10-, and 30-minute master and stem captures with dense audio plus SD
streams. Require exact duration/frame count, valid headers, `dropped=0`, and
`errors=0`. Copy `.mss` and split it:

```bash
python tools/split_stems.py STEM001.mss exported-stems
```

All five WAVs must have identical frame counts; isolated buses must contain the
expected audio/silence. Master and stems are mutually exclusive.

On a disposable card, power off during active master, stem, loop, and sampler
recording. On reboot, require a structurally valid recovery or `.bad`
quarantine, preservation of the last published take, and ability to record
again. Repeat ten cycles.

## 8. Serial and composite USB MIDI

Run the CLI soak while dense audio, six loops, and motion are active:

```bash
python tools/protocol_soak.py --port /dev/ttyACM0 --count 10000
```

Require correlated request IDs, no reboot/UI/audio stall, and bounded error
counters. Then connect a computer/DAW and verify that CDC and MIDI enumerate
together. Test notes, CC, clock, song position, start/continue/stop, output
mirroring, unplug/replug 20 times, and a 30-minute external-clock session.

## 9. BLE MIDI

Pair `Mini Studio 16`. Test notes, CC, clock/transport, outgoing motion CC, and
twenty disconnect/reconnect cycles while audio plays. Require no stuck notes,
queue growth, reboot, or render stall. Record malformed/dropped packet counters.

## 10. Direct USB-MIDI host profile

Before connecting anything, document the OTG adapter and safe VBUS/powered-hub
arrangement. Never connect two powered hosts together.

Flash:

```bash
esptool.py --chip esp32s3 write_flash 0x0 microgroove-v3-alpha-usb-host.bin
```

Test Yamaha, CYD, disconnect during traffic, twenty reconnects, a non-MIDI
device, and 30 minutes of clock. This profile is input-only and lacks CDC, so
use UI/audio behavior plus any available hardware debug path. Document how the
normal image is restored.

## 11. Built-in full duplex

The inherited high-level path switches between mic and speaker. Test the
current behavior first. A later lower-level ES8311 experiment may attempt one
I2S owner for simultaneous ADC/DAC; monitor for driver conflicts, feedback,
clock errors, noise, CPU load, and audio drops. Do not claim full duplex from
compile success.

## Results required for each bug-fix pass

- firmware commit and image SHA-256;
- board revision and power/cable setup;
- SD card identity/filesystem/cluster size;
- exact reproduction steps and duration;
- full boot plus relevant `MS16/1`, `SDDIAG`, recorder, loop, sampler, MIDI,
  heap, and SD-arbiter telemetry;
- produced file(s), or at least sizes/frame counts/checksums;
- whether audio clicked, phase-shifted, muted, froze, rebooted, or recovered.
