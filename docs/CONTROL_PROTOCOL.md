# Mini Studio 16 USB serial control

The normal firmware exposes a bounded line protocol over USB CDC at 115200
baud. Cardputer keyboard/UI control remains active while a computer or agent
sends commands. The separately flashed USB-host profile has CDC disabled.

Request and response framing:

```text
MS16/1 REQUEST_ID COMMAND [ARGUMENTS...]\n
MS16/1 REQUEST_ID OK key=value ...
MS16/1 REQUEST_ID ERR error_name
```

Request IDs contain 1–15 ASCII letters, digits, `_`, or `-`. Lines are limited
to 127 bytes, parsing does not allocate, and firmware consumes at most 64 input
bytes per main-loop iteration.

## Commands

| Request | Effect |
| --- | --- |
| `ping` | Liveness/protocol check |
| `status` | Transport, recorder, stem, MIDI queue, and SD-arbiter summary |
| `transport start\|continue\|stop` | Internal transport |
| `tempo BPM` | Set 40–300 BPM |
| `note TRACK NOTE VELOCITY` | Synth track 1–3, MIDI note 24–107, velocity 1–127 |
| `drum LANE` | Trigger drum lane 1–8 |
| `sd_test` | Start the SD diagnostic when storage is free |
| `master start\|stop` | Start or finalize long master WAV capture |
| `stems start\|stop` | Start or finalize five-bus stem capture |
| `loop status` | Six loop states, frames, ring fill, drops, underruns, and volume |
| `loop TRACK record\|stop\|mute\|unmute\|clear` | Control loop 1–6 |
| `loop TRACK volume PERCENT` | Set loop volume 0–100 |
| `sample status` | Slot quota, record state, voices, drops, underruns, and errors |
| `sample SLOT assign FILE melodic\|sliced` | Assign a mono 16-bit WAV under `/groovebox/samples` |
| `sample SLOT trigger KEY` | Trigger melodic key/slice 1–16 |
| `sample SLOT clear` | Clear slot and its sequence events |
| `sample SLOT record bus\|mic melodic\|sliced` | Stream input into the slot up to remaining 40-second quota |
| `sample SLOT stop` | Stop/finalize the active slot recording |
| `event status` | Five event-track states, bar lengths, and event count |
| `event TRACK arm\|disarm\|mute\|unmute\|clear` | Control event track 1–5 |
| `event TRACK bars BARS` | Set 1–128 bars |
| `motion status` | IMU values, gesture flags, mappings, and sample count |
| `motion MAP map SOURCE TARGET` | Configure mapping 1–4 |
| `motion MAP clear` | Clear mapping 1–4 |
| `midi status` | MIDI queue, BLE, USB role/mount, byte/message/error, and dropped output-clock counters |

Motion sources are `tilt_x`, `tilt_y`, `accel`, `gyro`, `shake`, and `slap`.
Targets are `synth1_cutoff`, `synth2_cutoff`, `synth3_cutoff`,
`synth1_resonance`, `synth2_resonance`, and `synth3_resonance`.

## Storage outputs

- Master: `/groovebox/recordings/MASTERnnn.wav`, 22.05 kHz mono 16-bit.
- Stems: `/groovebox/recordings/STEMnnn.mss`, sequential five-bus container.
- Loops: `/groovebox/loops/L1.wav` through `L6.wav`.
- Recorded slots: unique WAVs under `/groovebox/samples`.

Split a copied stem container on the computer:

```bash
python tools/split_stems.py STEM001.mss exported-stems
```

This emits `master.wav`, `synth1.wav`, `synth2.wav`, `synth3.wav`, and
`drums.wav` with identical frame counts. Interrupted temporary captures are
repaired on boot when structurally complete, otherwise quarantined as `.bad`.

## CLI examples

```bash
python -m pip install pyserial
python tools/ministudio_cli.py ports
python tools/ministudio_cli.py --port /dev/ttyACM0 ping
python tools/ministudio_cli.py --port /dev/ttyACM0 status
python tools/ministudio_cli.py --port /dev/ttyACM0 transport start
python tools/ministudio_cli.py --port /dev/ttyACM0 loop-status
python tools/ministudio_cli.py --port /dev/ttyACM0 loop 1 record
python tools/ministudio_cli.py --port /dev/ttyACM0 loop 1 volume 75
python tools/ministudio_cli.py --port /dev/ttyACM0 sample-assign 1 KICK.wav sliced
python tools/ministudio_cli.py --port /dev/ttyACM0 sample-trigger 1 4
python tools/ministudio_cli.py --port /dev/ttyACM0 sample-record 2 bus melodic
python tools/ministudio_cli.py --port /dev/ttyACM0 event 1 bars 128
python tools/ministudio_cli.py --port /dev/ttyACM0 event 1 arm
python tools/ministudio_cli.py --port /dev/ttyACM0 motion-map 1 tilt_x synth1_cutoff
python tools/ministudio_cli.py --port /dev/ttyACM0 master start
python tools/ministudio_cli.py --port /dev/ttyACM0 master stop
python tools/ministudio_cli.py --port /dev/ttyACM0 --json midi-status
python tools/ministudio_cli.py --port /dev/ttyACM0 monitor --seconds 30
python tools/protocol_soak.py --port /dev/ttyACM0 --count 10000
```

Linux commonly uses `/dev/ttyACM0`, macOS `/dev/cu.usbmodem*`, and Windows a
`COM` port. When exactly one serial device exists, `--port` may be omitted.

## Concurrency rules

Commands that need exclusive SD ownership return an error while an incompatible
recorder/diagnostic/project operation is active. `stop` requests drain their
rings and publish headers asynchronously; status/monitor output is the source
of final filenames and zero-drop/error evidence. A synchronous `OK` means the
request was accepted, not that hardware validation passed.
