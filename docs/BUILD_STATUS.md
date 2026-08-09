# Microgroove v3 alpha build status

Branch: `codex/v3-alpha-sd-streaming`

This checkpoint is the hardware-validation alpha, not the completed six-loop
instrument. It adds the lowest-risk product changes and the measurements that
must pass before long-audio streaming is enabled.

## Verified in the local sandbox

- Host syntax/integration compilation covers the sketch, audio engine,
  sequencer, sampler, storage, UI/input and SD diagnostics.
- The single-producer/single-consumer PCM ring preserves one million ordered
  values across concurrent producer/consumer threads.
- GBX v1, v2 and v3 serialized layouts are pinned by compile-time assertions.
- The v3 loader compiles with fixed v1/v2 layouts, migration paths and backup
  fallback for an absent, truncated or corrupt primary. Functional project-I/O
  validation remains part of the on-device test pass.
- The merged-firmware command generator is unit tested for the ESP32-S3 image
  offsets.
- `tests/run_host_tests.sh` completes without errors.

## Requires external verification

- A real PlatformIO ESP32-S3 compile/link. The current sandbox cannot download
  the pinned Espressif platform/toolchain packages.
- Flashing and booting on a Cardputer-ADV.
- SD throughput/stall results from the exact card using the on-device `SD TEST`
  page.
- Full-duplex ES8311 input/output and every later long-audio milestone.

Do not describe this checkpoint as hardware-verified or as implementing the six
loop tracks. Continue with `CARDPUTER_TESTING.md` after CI produces a merged
binary.
