#!/usr/bin/env python3
"""Run a correlated command soak against Mini Studio 16 over USB serial."""

from __future__ import annotations

import argparse
import time
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ministudio_cli import build_request, parse_response  # noqa: E402


def command_for(index: int) -> list[object]:
    choices = (["ping"], ["status"], ["tempo", 128 + (index & 1)])
    return list(choices[index % len(choices)])


def run_soak(device, count: int, response_timeout: float = 2.0) -> tuple[int, int]:
    if count <= 0:
        raise ValueError("count must be positive")
    completed = 0
    asynchronous = 0
    for index in range(count):
        request_id = f"s{index:014d}"
        device.write(build_request(request_id, command_for(index)))
        device.flush()
        deadline = time.monotonic() + response_timeout
        while time.monotonic() < deadline:
            raw = device.readline()
            if not raw:
                continue
            line = raw.decode("utf-8", errors="replace").strip()
            try:
                response = parse_response(line)
            except ValueError:
                asynchronous += 1
                continue
            if response.request_id != request_id:
                asynchronous += 1
                continue
            if not response.ok:
                raise RuntimeError(f"request {request_id} failed: {line}")
            completed += 1
            break
        else:
            raise TimeoutError(f"no correlated response for {request_id}")
    return completed, asynchronous


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--count", type=int, default=10000)
    parser.add_argument("--timeout", type=float, default=2.0)
    args = parser.parse_args()
    try:
        import serial  # type: ignore
    except ImportError:
        print("pyserial is required: python -m pip install pyserial", file=sys.stderr)
        return 2
    with serial.Serial(args.port, args.baud, timeout=0.05) as device:
        completed, asynchronous = run_soak(device, args.count, args.timeout)
    print(f"SOAK PASS completed={completed} asynchronous={asynchronous}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
