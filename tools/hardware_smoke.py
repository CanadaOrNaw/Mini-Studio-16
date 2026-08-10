#!/usr/bin/env python3
"""Run the non-destructive Mini Studio 16 Cardputer arrival smoke pass."""

from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path
from typing import Iterable

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ministudio_cli import Response, build_request, parse_response  # noqa: E402


READ_ONLY_PROBES: tuple[tuple[str, ...], ...] = (
    ("ping",),
    ("status",),
    ("project", "status"),
    ("loop", "status"),
    ("sample", "status"),
    ("event", "status"),
    ("motion", "status"),
    ("midi", "status"),
)


def send_correlated(device, request_id: str, words: Iterable[object], timeout: float,
                    events: list[str]) -> Response:
    device.write(build_request(request_id, words))
    device.flush()
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        raw = device.readline()
        if not raw:
            continue
        line = raw.decode("utf-8", errors="replace").strip()
        try:
            response = parse_response(line)
        except ValueError:
            events.append(line)
            continue
        if response.request_id != request_id:
            events.append(line)
            continue
        return response
    raise TimeoutError(f"no correlated response for {request_id}")


def wait_for_sd_result(device, timeout: float, events: list[str]) -> dict[str, str]:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        raw = device.readline()
        if not raw:
            continue
        line = raw.decode("utf-8", errors="replace").strip()
        events.append(line)
        if not line.startswith("SDDIAG state="):
            continue
        values: dict[str, str] = {}
        for token in line.split()[1:]:
            if "=" in token:
                key, value = token.split("=", 1)
                values[key] = value
        if values.get("state") not in ("PASS", "FAIL"):
            continue
        return values
    raise TimeoutError("no terminal SDDIAG line")


def run_smoke(device, command_timeout: float = 3.0, run_sd_test: bool = False,
              sd_timeout: float = 180.0) -> dict[str, object]:
    events: list[str] = []
    probes: list[dict[str, object]] = []
    for index, words in enumerate(READ_ONLY_PROBES, start=1):
        response = send_correlated(device, f"smoke{index:02d}", words,
                                   command_timeout, events)
        record = {
            "command": " ".join(words),
            "ok": response.ok,
            "values": response.values,
            "message": response.message,
        }
        probes.append(record)
        if not response.ok:
            raise RuntimeError(f"probe failed: {record}")

    sd_result: dict[str, str] | None = None
    if run_sd_test:
        response = send_correlated(device, "smokesd", ("sd_test",),
                                   command_timeout, events)
        if not response.ok:
            raise RuntimeError(f"SD diagnostic rejected: {response.message}")
        sd_result = wait_for_sd_result(device, sd_timeout, events)
        if sd_result.get("state") != "PASS":
            raise RuntimeError(f"SD diagnostic failed: {sd_result}")

    return {
        "result": "PASS",
        "probes": probes,
        "sd_diagnostic": sd_result,
        "asynchronous_lines": events,
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--sd-test", action="store_true",
                        help="run the destructive-temporary /groovebox/diag benchmark")
    parser.add_argument("--sd-timeout", type=float, default=180.0)
    parser.add_argument("--output", type=Path,
                        help="also write the JSON evidence to this path")
    args = parser.parse_args(argv)

    try:
        import serial  # type: ignore
    except ImportError:
        print("pyserial is required: python -m pip install pyserial", file=sys.stderr)
        return 2

    try:
        with serial.Serial(args.port, args.baud, timeout=0.1) as device:
            report = run_smoke(device, args.timeout, args.sd_test, args.sd_timeout)
    except (OSError, RuntimeError, TimeoutError) as error:
        print(f"SMOKE FAIL: {error}", file=sys.stderr)
        return 1

    rendered = json.dumps(report, indent=2, sort_keys=True)
    if args.output:
        args.output.write_text(rendered + "\n", encoding="utf-8")
    print(rendered)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
