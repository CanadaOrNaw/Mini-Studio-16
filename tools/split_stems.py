#!/usr/bin/env python3
"""Split a Mini Studio 16 five-channel .mss capture into mono WAV stems."""

from __future__ import annotations

import argparse
import struct
import wave
from contextlib import ExitStack
from pathlib import Path
from typing import Dict


HEADER = struct.Struct("<8sHHII12s")
CHANNELS = ("master", "synth1", "synth2", "synth3", "drums")


def read_header(source) -> tuple[int, int]:
    raw = source.read(HEADER.size)
    if len(raw) != HEADER.size:
        raise ValueError("truncated stem header")
    magic, version, channels, sample_rate, frames, reserved = HEADER.unpack(raw)
    if magic != b"MS16STEM" or version != 1 or channels != len(CHANNELS):
        raise ValueError("unsupported stem file")
    if sample_rate <= 0 or reserved[:5] != b"M123D":
        raise ValueError("invalid stem metadata")
    return sample_rate, frames


def split_stem_file(input_path: Path, output_dir: Path, chunk_frames: int = 4096) -> Dict[str, Path]:
    if chunk_frames <= 0:
        raise ValueError("chunk_frames must be positive")
    outputs = {name: output_dir / f"{name}.wav" for name in CHANNELS}

    with input_path.open("rb") as source, ExitStack() as stack:
        sample_rate, frames = read_header(source)
        frame_bytes = len(CHANNELS) * 2
        declared_end = HEADER.size + frames * frame_bytes
        source.seek(0, 2)
        actual_end = source.tell()
        if actual_end < declared_end:
            raise ValueError("truncated stem payload")
        if actual_end - declared_end >= frame_bytes:
            raise ValueError("unexpected complete frames after declared stem frames")
        source.seek(HEADER.size)

        output_dir.mkdir(parents=True, exist_ok=True)
        writers = []
        for name in CHANNELS:
            writer = stack.enter_context(wave.open(str(outputs[name]), "wb"))
            writer.setnchannels(1)
            writer.setsampwidth(2)
            writer.setframerate(sample_rate)
            writers.append(writer)

        remaining = frames
        while remaining:
            requested = min(remaining, chunk_frames)
            raw = source.read(requested * frame_bytes)
            if len(raw) != requested * frame_bytes:
                raise ValueError("truncated stem payload")
            samples = struct.unpack(f"<{requested * len(CHANNELS)}h", raw)
            for channel, writer in enumerate(writers):
                mono = samples[channel::len(CHANNELS)]
                writer.writeframesraw(struct.pack(f"<{requested}h", *mono))
            remaining -= requested

        # Recovery may leave fewer than one complete interleaved frame after
        # the repaired payload. Those bytes were never declared and are ignored.

    return outputs


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output_dir", type=Path)
    args = parser.parse_args()
    for name, path in split_stem_file(args.input, args.output_dir).items():
        print(f"{name}={path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
