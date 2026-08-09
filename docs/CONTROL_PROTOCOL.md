# Mini Studio 16 USB serial control

The alpha exposes a bounded, line-oriented protocol over the Cardputer's USB
CDC serial port at 115200 baud. It is additive: Cardputer keyboard controls
continue to work while a computer or automation agent sends commands.

Every request has the form:

```text
MS16/1 REQUEST_ID COMMAND [ARGUMENTS...]\n
```

Every synchronous response repeats the request ID:

```text
MS16/1 REQUEST_ID OK key=value ...
MS16/1 REQUEST_ID ERR error_name
```

Request IDs contain 1–15 ASCII letters, digits, `_` or `-`. Input lines are
limited to 127 bytes. The firmware processes at most 64 serial bytes per main
loop iteration so a noisy host cannot monopolize the loop.

## Commands

| Request | Effect |
| --- | --- |
| `ping` | Protocol/firmware liveness check |
| `status` | Transport, tempo, pattern and master-recorder status |
| `transport start` | Start from the transport beginning |
| `transport continue` | Continue from the current position |
| `transport stop` | Stop transport |
| `tempo BPM` | Set 40–300 BPM |
| `note TRACK MIDI VELOCITY` | Trigger synth track 1–3; MIDI note 24–107 |
| `drum LANE` | Trigger drum lane 1–8 |
| `sd_test` | Start the existing SD benchmark if storage is free |
| `master start` | Start a unique long master WAV on microSD |
| `master stop` | Drain the ring, finalize the WAV header and publish the file |

The master recorder writes 22.05 kHz, mono, 16-bit PCM under
`/groovebox/recordings/MASTERnnn.wav`. It does not replace the existing short
RAM-backed resampling-to-pad feature. `status` reports written and dropped
frames; a valid physical-device run requires `dropped=0`.

## CLI

Install `pyserial`, then run the checked-in client:

```bash
python -m pip install pyserial
python tools/ministudio_cli.py --port /dev/ttyACM0 ping
python tools/ministudio_cli.py --port /dev/ttyACM0 transport start
python tools/ministudio_cli.py --port /dev/ttyACM0 master start
python tools/ministudio_cli.py --port /dev/ttyACM0 status
python tools/ministudio_cli.py --port /dev/ttyACM0 master stop
```

Linux ports are commonly `/dev/ttyACM0`; macOS commonly uses
`/dev/cu.usbmodem*`; Windows uses a `COM` port. This protocol is the first
remote-control layer, not USB MIDI. USB MIDI device/host roles and MIDI clock
remain separate milestones in the implementation plan.

