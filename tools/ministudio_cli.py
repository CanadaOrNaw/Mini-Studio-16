#!/usr/bin/env python3
"""Desktop client for the Mini Studio 16 MS16/1 USB serial protocol."""

from __future__ import annotations

import argparse
import json
import re
import sys
import time
from dataclasses import dataclass
from typing import Dict, Iterable, List


PREFIX = "MS16/1"
ID_RE = re.compile(r"^[A-Za-z0-9_-]{1,15}$")


@dataclass(frozen=True)
class Response:
    request_id: str
    ok: bool
    values: Dict[str, str]
    message: str


def build_request(request_id: str, words: Iterable[object]) -> bytes:
    if not ID_RE.fullmatch(request_id):
        raise ValueError("request ID must be 1-15 letters, digits, '_' or '-'")
    tokens = [str(word) for word in words]
    if not tokens or any(not token or any(ch.isspace() for ch in token) for token in tokens):
        raise ValueError("command tokens must be non-empty and contain no whitespace")
    return f"{PREFIX} {request_id} {' '.join(tokens)}\n".encode("ascii")


def parse_response(line: str) -> Response:
    tokens = line.strip().split()
    if len(tokens) < 4 or tokens[0] != PREFIX or tokens[2] not in ("OK", "ERR"):
        raise ValueError("not an MS16/1 response")
    values: Dict[str, str] = {}
    messages: List[str] = []
    for token in tokens[3:]:
        if "=" in token:
            key, value = token.split("=", 1)
            values[key] = value
        else:
            messages.append(token)
    return Response(tokens[1], tokens[2] == "OK", values, " ".join(messages))


def response_json(response: Response) -> str:
    return json.dumps(
        {
            "protocol": PREFIX,
            "request_id": response.request_id,
            "ok": response.ok,
            "values": response.values,
            "message": response.message,
        },
        sort_keys=True,
        separators=(",", ":"),
    )


def resolve_port(explicit: str | None, candidates: Iterable[object]) -> str:
    if explicit:
        return explicit
    devices = [str(getattr(candidate, "device")) for candidate in candidates]
    if len(devices) == 1:
        return devices[0]
    if not devices:
        raise ValueError("no serial ports found; pass --port")
    raise ValueError("multiple serial ports found; pass --port: " + ", ".join(devices))


def command_words(args: argparse.Namespace) -> List[object]:
    if args.command == "ping":
        return ["ping"]
    if args.command == "status":
        return ["status"]
    if args.command == "transport":
        return ["transport", args.action]
    if args.command == "tempo":
        return ["tempo", args.bpm]
    if args.command == "note":
        return ["note", args.track, args.midi_note, args.velocity]
    if args.command == "note-off":
        return ["note_off", args.track, args.midi_note]
    if args.command == "drum":
        return ["drum", args.lane]
    if args.command == "sd-test":
        return ["sd_test"]
    if args.command == "master":
        return ["master", args.action]
    if args.command == "stems":
        return ["stems", args.action]
    if args.command == "loop-status":
        return ["loop", "status"]
    if args.command == "loop":
        if args.action == "volume" and args.volume is None:
            raise ValueError("loop volume requires 0..100")
        if args.action != "volume" and args.volume is not None:
            raise ValueError("volume value is only valid with loop volume")
        words: List[object] = ["loop", args.track, args.action]
        if args.volume is not None:
            words.append(args.volume)
        return words
    if args.command == "loop-transport":
        return ["loop", args.action]
    if args.command == "sample-status":
        return ["sample", "status"]
    if args.command == "sample-assign":
        return ["sample", args.slot, "assign", args.filename, args.mode]
    if args.command == "sample-trigger":
        return ["sample", args.slot, "trigger", args.key]
    if args.command == "sample-clear":
        return ["sample", args.slot, "clear"]
    if args.command == "sample-record":
        return ["sample", args.slot, "record", args.input, args.mode]
    if args.command == "sample-stop":
        return ["sample", args.slot, "stop"]
    if args.command == "event-status":
        return ["event", "status"]
    if args.command == "event":
        if args.action == "bars" and args.bars is None:
            raise ValueError("event bars requires a bar count")
        if args.action != "bars" and args.bars is not None:
            raise ValueError("bar count is only valid with event bars")
        words: List[object] = ["event", args.track, args.action]
        if args.bars is not None:
            words.append(args.bars)
        return words
    if args.command == "motion-status":
        return ["motion", "status"]
    if args.command == "motion-map":
        return ["motion", args.mapping, "map", args.source, args.target]
    if args.command == "motion-clear":
        return ["motion", args.mapping, "clear"]
    if args.command == "midi-status":
        return ["midi", "status"]
    if args.command == "project-status":
        return ["project", "status"]
    if args.command == "project":
        return ["project", args.slot, args.action]
    if args.command == "boot-status":
        return ["boot", "status"]
    if args.command == "boot-mode":
        return ["boot", args.mode]
    if args.command == "synth-status":
        return ["synth", "status"]
    if args.command == "synth-engine":
        return ["synth", args.track, "engine", args.engine]
    if args.command == "synth-set":
        return ["synth", args.track, "set", args.parameter, args.value]
    if args.command == "synth-dsp-reset":
        return ["synth", "dsp_reset"]
    if args.command == "chord-status":
        return ["chord", "status"]
    if args.command == "chord-play":
        return ["chord", "play", args.degree, args.direction]
    if args.command == "chord-off":
        return ["chord", "off"]
    if args.command == "chord-set":
        return ["chord", "set", args.parameter, args.value]
    if args.command == "chord-lock":
        return ["chord", "lock", args.degree, args.chord_type]
    if args.command == "chord-unlock":
        return ["chord", "unlock", args.degree]
    if args.command == "chord-inversion":
        return ["chord", "inversion", args.degree, args.value]
    if args.command == "chord-octave-shift":
        return ["chord", "octave_shift", args.degree, args.value]
    if args.command == "po-effect":
        return ["po", "effect", args.effect]
    if args.command == "po-lock":
        return ["po", "lock", args.pattern, args.step, args.effect]
    if args.command == "medo-status":
        return ["medo", "status"]
    if args.command == "medo-role":
        return ["medo", "role", args.role]
    if args.command == "medo-quantize":
        return ["medo", "quantize", args.role, args.mode]
    if args.command == "medo-set":
        return ["medo", "set", args.parameter, args.value]
    if args.command == "cap-status":
        return ["cap", "status"]
    if args.command == "cap-pair":
        return ["cap", "pair"]
    if args.command == "cap-disconnect":
        return ["cap", "disconnect"]
    if args.command == "cap-monitor":
        return ["cap", "monitor", args.percent]
    if args.command == "cap-clear":
        return ["cap", "clear"]
    raise ValueError(f"unsupported command: {args.command}")


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument("--port", help="USB serial port; auto-selected when exactly one exists")
    result.add_argument("--baud", type=int, default=115200)
    result.add_argument("--timeout", type=float, default=3.0)
    result.add_argument("--id", dest="request_id", default=None)
    result.add_argument("--json", action="store_true", help="emit machine-readable JSON")
    sub = result.add_subparsers(dest="command", required=True)
    sub.add_parser("ping")
    sub.add_parser("status")
    transport = sub.add_parser("transport")
    transport.add_argument("action", choices=("start", "continue", "stop"))
    tempo = sub.add_parser("tempo")
    tempo.add_argument("bpm", type=int, choices=range(40, 301), metavar="40..300")
    note = sub.add_parser("note")
    note.add_argument("track", type=int, choices=range(1, 4))
    note.add_argument("midi_note", type=int, choices=range(24, 108))
    note.add_argument("velocity", type=int, choices=range(1, 128))
    note_off = sub.add_parser("note-off", help="release a MGX/FM note")
    note_off.add_argument("track", type=int, choices=range(1, 4))
    note_off.add_argument("midi_note", type=int, choices=range(24, 108))
    drum = sub.add_parser("drum")
    drum.add_argument("lane", type=int, choices=range(1, 9))
    sub.add_parser("sd-test")
    master = sub.add_parser("master")
    master.add_argument("action", choices=("start", "stop"))
    stems = sub.add_parser("stems")
    stems.add_argument("action", choices=("start", "stop"))
    sub.add_parser("loop-status", help="show six-track streaming state and counters")
    loop = sub.add_parser("loop")
    loop.add_argument("track", type=int, choices=range(1, 7))
    loop.add_argument("action", choices=("record", "stop", "mute", "unmute", "clear",
                                         "volume", "solo", "unsolo"))
    loop.add_argument("volume", type=int, nargs="?", choices=range(0, 101))
    loop_transport = sub.add_parser("loop-transport", help="pause/resume loops or control metronome")
    loop_transport.add_argument("action", choices=("pause", "resume", "metronome_on", "metronome_off"))
    sub.add_parser("sample-status", help="show streamed sampler quota, voices and counters")
    sample_assign = sub.add_parser("sample-assign")
    sample_assign.add_argument("slot", type=int, choices=range(1, 17))
    sample_assign.add_argument("filename", help="mono 16-bit WAV in /groovebox/samples")
    sample_assign.add_argument("mode", choices=("melodic", "sliced"))
    sample_trigger = sub.add_parser("sample-trigger")
    sample_trigger.add_argument("slot", type=int, choices=range(1, 17))
    sample_trigger.add_argument("key", type=int, choices=range(1, 17))
    sample_clear = sub.add_parser("sample-clear")
    sample_clear.add_argument("slot", type=int, choices=range(1, 17))
    sample_record = sub.add_parser("sample-record",
                                   help="stream the master bus or microphone into a slot")
    sample_record.add_argument("slot", type=int, choices=range(1, 17))
    sample_record.add_argument("input", choices=("bus", "mic"))
    sample_record.add_argument("mode", choices=("melodic", "sliced"))
    sample_stop = sub.add_parser("sample-stop")
    sample_stop.add_argument("slot", type=int, choices=range(1, 17))
    sub.add_parser("event-status", help="show five-part 128-bar event looper")
    event = sub.add_parser("event")
    event.add_argument("track", type=int, choices=range(1, 6),
                       help="1=drum 2=bass 3=chord 4=lead 5=sample/control")
    event.add_argument("action",
                       choices=("arm", "disarm", "mute", "unmute", "clear", "bars"))
    event.add_argument("bars", type=int, nargs="?", choices=range(1, 129))
    sub.add_parser("motion-status", help="show BMI270 motion values and mappings")
    motion_sources = ("tilt_x", "tilt_y", "accel", "gyro", "shake", "slap")
    motion_targets = (
        "synth1_cutoff", "synth2_cutoff", "synth3_cutoff",
        "synth1_resonance", "synth2_resonance", "synth3_resonance",
    )
    motion_map = sub.add_parser("motion-map")
    motion_map.add_argument("mapping", type=int, choices=range(1, 5))
    motion_map.add_argument("source", choices=motion_sources)
    motion_map.add_argument("target", choices=motion_targets)
    motion_clear = sub.add_parser("motion-clear")
    motion_clear.add_argument("mapping", type=int, choices=range(1, 5))
    sub.add_parser("midi-status", help="show USB and BLE MIDI transport counters")
    sub.add_parser("project-status", help="show current and occupied project slots")
    project = sub.add_parser("project", help="save or load a complete GBX project")
    project.add_argument("slot", type=int, choices=range(1, 9))
    project.add_argument("action", choices=("save", "load"))
    sub.add_parser("boot-status", help="show installed USB-role images and selected slot")
    boot_mode = sub.add_parser("boot-mode", help="select a USB role and reboot")
    boot_mode.add_argument("mode", choices=("normal", "host"))
    sub.add_parser("synth-status", help="show engines and audio render deadlines")
    synth_engine = sub.add_parser("synth-engine", help="select a per-track synth engine")
    synth_engine.add_argument("track", type=int, choices=range(1, 4))
    synth_engine.add_argument("engine", choices=("mg", "mgx", "fm4"))
    synth_set = sub.add_parser("synth-set", help="set a named integer synth parameter")
    synth_set.add_argument("track", type=int, choices=range(1, 4))
    synth_set.add_argument("parameter", help="for example fm.op2.ratio or mgx.amp.attack")
    # P3: metavar keeps a bad value from printing all 5,001 choices (~48 KB
    # of usage text) — range checking is unchanged.
    synth_set.add_argument("value", type=int, choices=range(0, 5001),
                           metavar="0..5000")
    sub.add_parser("synth-dsp-reset", help="reset audio render timing counters")
    sub.add_parser("chord-status", help="show HiChord performance state")
    chord_play = sub.add_parser("chord-play", help="play degree 1..7 with direction 0..8")
    chord_play.add_argument("degree", type=int, choices=range(1, 8))
    chord_play.add_argument("direction", type=int, choices=range(0, 9))
    sub.add_parser("chord-off", help="release a remotely held chord")
    chord_set = sub.add_parser("chord-set", help="set a HiChord performance parameter")
    chord_set.add_argument("parameter")
    chord_set.add_argument("value", type=int, choices=range(0, 256))
    chord_lock = sub.add_parser("chord-lock", help="lock one degree to chord type 0..27")
    chord_lock.add_argument("degree", type=int, choices=range(1, 8))
    chord_lock.add_argument("chord_type", type=int, choices=range(0, 28))
    chord_unlock = sub.add_parser("chord-unlock")
    chord_unlock.add_argument("degree", type=int, choices=range(1, 8))
    # A2-P3: these two shipped in the firmware and in CONTROL_PROTOCOL.md but
    # had no CLI path, so a hardware-day agent following the docs could not
    # drive them. Wire values match the protocol table exactly.
    chord_inversion = sub.add_parser(
        "chord-inversion", help="set inversion -2..+3 as wire value 0..5")
    chord_inversion.add_argument("degree", type=int, choices=range(1, 8))
    chord_inversion.add_argument("value", type=int, choices=range(0, 6),
                                 metavar="0..5")
    chord_octave_shift = sub.add_parser(
        "chord-octave-shift", help="set per-degree octave -2..+2 as wire value 0..4")
    chord_octave_shift.add_argument("degree", type=int, choices=range(1, 8))
    chord_octave_shift.add_argument("value", type=int, choices=range(0, 5),
                                    metavar="0..4")
    po_effect = sub.add_parser("po-effect", help="engage PO punch effect 0..15")
    po_effect.add_argument("effect", type=int, choices=range(0, 16))
    po_lock = sub.add_parser("po-lock", help="write a PO effect to a pattern step")
    po_lock.add_argument("pattern", type=int, choices=range(1, 17))
    po_lock.add_argument("step", type=int, choices=range(1, 17))
    po_lock.add_argument("effect", type=int, choices=range(0, 16))
    sub.add_parser("medo-status", help="show MEDO role and looper state")
    medo_role = sub.add_parser("medo-role")
    medo_role.add_argument("role", type=int, choices=range(1, 6))
    medo_quantize = sub.add_parser("medo-quantize")
    medo_quantize.add_argument("role", type=int, choices=range(1, 6))
    medo_quantize.add_argument("mode", type=int, choices=range(0, 3))
    medo_set = sub.add_parser("medo-set", help="set scale, arpeggiator, or shared bars")
    medo_set.add_argument("parameter", choices=("scale", "arp_enabled", "arp_direction", "arp_rate", "bars"))
    medo_set.add_argument("value", type=int, choices=range(0, 256))
    sub.add_parser("cap-status", help="show optional Audio Cap connection and stream counters")
    sub.add_parser("cap-pair", help="discover and pair conventional Bluetooth audio output")
    sub.add_parser("cap-disconnect", help="disconnect the current Bluetooth audio output")
    cap_monitor = sub.add_parser("cap-monitor", help="set line-input monitor level")
    cap_monitor.add_argument("percent", type=int, choices=range(0, 101))
    sub.add_parser("cap-clear", help="clear Audio Cap diagnostics")
    sub.add_parser("ports", help="list serial ports without opening one")
    monitor = sub.add_parser("monitor", help="print asynchronous device output")
    monitor.add_argument("--seconds", type=float, default=0.0, help="0 means run until interrupted")
    return result


def main(argv: List[str] | None = None) -> int:
    args = parser().parse_args(argv)

    try:
        import serial  # type: ignore
        from serial.tools import list_ports  # type: ignore
    except ImportError:
        print("pyserial is required: python -m pip install pyserial", file=sys.stderr)
        return 2

    candidates = list(list_ports.comports())
    if args.command == "ports":
        for item in candidates:
            record = {"device": item.device, "description": item.description, "hwid": item.hwid}
            print(json.dumps(record, sort_keys=True) if args.json else
                  f"{item.device}\t{item.description}\t{item.hwid}")
        return 0

    try:
        port = resolve_port(args.port, candidates)
    except ValueError as error:
        print(str(error), file=sys.stderr)
        return 2

    if args.command == "monitor":
        deadline = time.monotonic() + args.seconds if args.seconds > 0 else None
        try:
            with serial.Serial(port, args.baud, timeout=0.1) as device:
                while deadline is None or time.monotonic() < deadline:
                    raw = device.readline()
                    if not raw:
                        continue
                    line = raw.decode("utf-8", errors="replace").strip()
                    print(json.dumps({"event": "serial", "line": line}, sort_keys=True)
                          if args.json else line)
        except KeyboardInterrupt:
            pass
        return 0

    request_id = args.request_id or f"cli{int(time.time() * 1000) % 1000000}"
    try:
        words = command_words(args)
    except ValueError as error:
        # P3 (reconciliation report): argument-combination mistakes (for
        # example `loop 1 volume` with no value) used to surface as raw
        # Python tracebacks; print the message and the correct usage.
        print(f"error: {error}", file=sys.stderr)
        return 2
    payload = build_request(request_id, words)

    deadline = time.monotonic() + args.timeout
    with serial.Serial(port, args.baud, timeout=0.1) as device:
        device.write(payload)
        device.flush()
        while time.monotonic() < deadline:
            raw = device.readline()
            if not raw:
                continue
            line = raw.decode("utf-8", errors="replace").strip()
            try:
                response = parse_response(line)
            except ValueError:
                print(json.dumps({"event": "async", "line": line}, sort_keys=True)
                      if args.json else line)
                continue
            if response.request_id != request_id:
                print(json.dumps({"event": "async", "line": line}, sort_keys=True)
                      if args.json else line)
                continue
            print(response_json(response) if args.json else line)
            return 0 if response.ok else 1

    print(f"timed out waiting for response {request_id}", file=sys.stderr)
    return 3


if __name__ == "__main__":
    raise SystemExit(main())
