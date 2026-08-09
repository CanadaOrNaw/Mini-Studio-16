#!/usr/bin/env python3
"""Desktop client for the Mini Studio 16 MS16/1 USB serial protocol."""

from __future__ import annotations

import argparse
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
    if args.command == "drum":
        return ["drum", args.lane]
    if args.command == "sd-test":
        return ["sd_test"]
    if args.command == "master":
        return ["master", args.action]
    raise ValueError(f"unsupported command: {args.command}")


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument("--port", required=True, help="USB serial port, e.g. /dev/ttyACM0")
    result.add_argument("--baud", type=int, default=115200)
    result.add_argument("--timeout", type=float, default=3.0)
    result.add_argument("--id", dest="request_id", default=None)
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
    drum = sub.add_parser("drum")
    drum.add_argument("lane", type=int, choices=range(1, 9))
    sub.add_parser("sd-test")
    master = sub.add_parser("master")
    master.add_argument("action", choices=("start", "stop"))
    return result


def main(argv: List[str] | None = None) -> int:
    args = parser().parse_args(argv)
    request_id = args.request_id or f"cli{int(time.time() * 1000) % 1000000}"
    payload = build_request(request_id, command_words(args))

    try:
        import serial  # type: ignore
    except ImportError:
        print("pyserial is required: python -m pip install pyserial", file=sys.stderr)
        return 2

    deadline = time.monotonic() + args.timeout
    with serial.Serial(args.port, args.baud, timeout=0.1) as device:
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
                print(line)
                continue
            if response.request_id != request_id:
                print(line)
                continue
            print(line)
            return 0 if response.ok else 1

    print(f"timed out waiting for response {request_id}", file=sys.stderr)
    return 3


if __name__ == "__main__":
    raise SystemExit(main())

