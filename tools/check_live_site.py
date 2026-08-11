#!/usr/bin/env python3
"""Verify a deployed Mini Studio site and every manifested download."""

from __future__ import annotations

import argparse
import hashlib
import json
import time
from urllib.error import HTTPError, URLError
from urllib.parse import urljoin
from urllib.request import Request, urlopen


DEFAULT_URL = "https://canadaornaw.github.io/MiniStudio.github.io/"


def fetch(url: str, timeout: float) -> bytes:
    request = Request(url, headers={"User-Agent": "Mini-Studio-16-site-check/1"})
    with urlopen(request, timeout=timeout) as response:
        status = getattr(response, "status", 200)
        if status != 200:
            raise RuntimeError(f"HTTP {status}: {url}")
        return response.read()


def verify(base_url: str, expected_source: str | None = None,
           timeout: float = 15.0) -> dict:
    base = base_url.rstrip("/") + "/"
    index = fetch(base, timeout)
    raw_info = fetch(urljoin(base, "BUILD_INFO.json"), timeout)
    try:
        info = json.loads(raw_info)
    except (TypeError, ValueError) as error:
        raise RuntimeError("BUILD_INFO.json is not valid JSON") from error
    if info.get("project") != "Mini Studio 16":
        raise RuntimeError("live BUILD_INFO.json identifies the wrong project")
    source_sha = info.get("source_sha", "")
    if not isinstance(source_sha, str) or not source_sha:
        raise RuntimeError("live BUILD_INFO.json has no source_sha")
    if expected_source and source_sha != expected_source:
        raise RuntimeError(
            f"live source SHA is {source_sha}, expected {expected_source}")
    if source_sha[:12].encode() not in index:
        raise RuntimeError("index.html does not display its BUILD_INFO source SHA")

    assets = info.get("generated_assets")
    if not isinstance(assets, dict) or not assets:
        raise RuntimeError("live BUILD_INFO.json has no generated assets")
    total_bytes = 0
    checked = []
    for relative, expected in sorted(assets.items()):
        if (not isinstance(relative, str) or relative.startswith("/") or
                ".." in relative.split("/")):
            raise RuntimeError(f"unsafe asset path in manifest: {relative!r}")
        payload = fetch(urljoin(base, relative), timeout)
        actual_hash = hashlib.sha256(payload).hexdigest()
        if len(payload) != expected.get("bytes"):
            raise RuntimeError(
                f"size mismatch for {relative}: {len(payload)} != {expected.get('bytes')}")
        if actual_hash != expected.get("sha256"):
            raise RuntimeError(f"SHA-256 mismatch for {relative}")
        total_bytes += len(payload)
        checked.append(relative)
    return {
        "base_url": base,
        "source_sha": source_sha,
        "assets_checked": len(checked),
        "bytes_checked": total_bytes,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-url", default=DEFAULT_URL)
    parser.add_argument("--expected-source")
    parser.add_argument("--timeout", type=float, default=15.0)
    parser.add_argument("--retries", type=int, default=1)
    parser.add_argument("--retry-delay", type=float, default=5.0)
    args = parser.parse_args()
    if args.retries < 1 or args.retry_delay < 0:
        parser.error("retries must be >= 1 and retry-delay must be >= 0")

    last_error: Exception | None = None
    for attempt in range(1, args.retries + 1):
        try:
            result = verify(args.base_url, args.expected_source, args.timeout)
            print(json.dumps(result, indent=2, sort_keys=True))
            return 0
        except (HTTPError, URLError, OSError, RuntimeError, ValueError) as error:
            last_error = error
            if attempt < args.retries:
                time.sleep(args.retry_delay)
    print(f"site verification failed: {last_error}")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
