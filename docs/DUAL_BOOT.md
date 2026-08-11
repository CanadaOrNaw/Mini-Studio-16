# Dual USB-role boot image

Mini Studio 16 cannot use the ESP32-S3 native USB peripheral as a device and a
host at the same time. The combined Cardputer-ADV image therefore stores the
two already-supported applications in separate OTA slots and changes roles by
validated reboot rather than computer-assisted reflashing.

## Flash layout

| Region | Offset | Size | Purpose |
| --- | ---: | ---: | --- |
| NVS | `0x9000` | `0x5000` | ESP32 calibration/settings |
| OTA data | `0xe000` | `0x2000` | Redundant next-boot selection sectors |
| `normal` / `ota_0` | `0x10000` | `0x2f0000` | USB CDC + MIDI device application |
| `usbhost` / `ota_1` | `0x300000` | `0x2f0000` | USB MIDI controller-host application |

Both application slots provide 3,080,192 bytes. CI separately enforces the
stricter Mini Studio static-RAM and 3,000,000-byte flash-estimate gates. The
remaining flash is deliberately unallocated for future recovery/diagnostic
needs instead of silently introducing another filesystem.

This follows Espressif's pinned ESP-IDF 4.4.7 OTA model: two OTA application
slots plus a two-sector OTA-data partition. `esp_ota_set_boot_partition()`
validates the target application before changing the next boot, and the two
OTA-data sectors provide power-loss tolerance while the selection is written.

- [ESP-IDF 4.4.7 OTA documentation](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32s3/api-reference/system/ota.html)
- [ESP-IDF 4.4.7 partition-table documentation](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32s3/api-guides/partition-tables.html)

## Flashing and selection

The primary artifact is `mini-studio-16-dual-role.bin`. Flash it at offset
`0x0` with an ESP32-S3-compatible flasher. The initial OTA data selects Normal
mode.

Every boot shows the common selector before SD, audio, BLE, motion, or either
USB MIDI stack starts:

- press `Tab` to validate/select the other installed role and reboot;
- press any other key (or BtnA) to continue starting the displayed role;
- if the other image is absent/corrupt or the running binary is in the wrong
  slot, switching is refused and the current working application continues.

The standalone Normal and USB-host merged images remain in the artifact for
recovery and profile-specific debugging. They still work as before, but only
the combined image guarantees that both correctly labelled role slots exist.

## Serial/CLI control

Normal mode can inspect or change the next role over the existing bounded
`MS16/1` protocol:

```text
MS16/1 1 boot status
MS16/1 2 boot host
MS16/1 3 boot normal
```

Desktop equivalents:

```bash
python tools/ministudio_cli.py --port /dev/ttyACM0 boot-status
python tools/ministudio_cli.py --port /dev/ttyACM0 boot-mode host
python tools/ministudio_cli.py --port /dev/ttyACM0 boot-mode normal
```

A role change is rejected while any master, stem, microphone, loop, or streamed
sample recording is active (`boot_recording_busy`), while a microphone commit,
loop clear, or sampler assignment/clear is pending (`boot_storage_busy`), or
while the destructive SD diagnostic owns its temporary files
(`boot_diagnostic_busy`). Read-only loop/sample playback does not block a role
change. On success the response is flushed, transport is stopped, the validated
slot is committed, and the Cardputer reboots. USB Host mode may not expose USB
CDC, so the startup selector is always the recovery path back to Normal mode.

## Automated and physical verification boundary

Host tests cover role decisions, invalid values, missing/corrupt targets,
standalone-layout mismatch, partition bounds/alignment/overlap, common
bootloader/table identity, slot capacity, exact merge offsets, and merged-image
content. CI compiles both applications against the same table and publishes a
layout report with sizes and SHA-256 digests.

Only a physical Cardputer-ADV can verify display/keyboard timing, persistent
OTA selection across real resets and power cuts, twenty bidirectional switches,
and correct USB enumeration/VBUS behavior after each reboot.
