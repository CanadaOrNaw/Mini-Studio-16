# USB MIDI profiles and hardware boundary

Mini Studio 16 implements two separately flashed USB roles on the ESP32-S3's
single native USB PHY.

## Normal profile: computer-facing device

PlatformIO environment: `m5stack-cardputer-adv`

- `ARDUINO_USB_MODE=0`
- `ARDUINO_USB_CDC_ON_BOOT=1`
- Adafruit TinyUSB 3.1.3 provides `Adafruit_USBD_MIDI`
- CDC serial and USB MIDI are intended to enumerate as a composite device
- MIDI input/output feeds the same bounded queue and output mirror as BLE MIDI

Build:

```bash
pio run -e m5stack-cardputer-adv
```

This is the normal standalone/computer/DAW image and preserves the `MS16/1`
CLI over CDC.

## Alternate profile: direct controller host

PlatformIO environment: `m5stack-cardputer-adv-usb-host`

- `ARDUINO_USB_MODE=1`
- CDC on boot is disabled
- `MS16_USB_MIDI_HOST=1` selects the ESP-IDF USB Host implementation
- A client/daemon pair enumerates one Audio-class MIDIStreaming interface,
  claims its bulk/interrupt IN endpoint, decodes four-byte USB-MIDI 1.0 event
  packets, and forwards their MIDI bytes into the bounded input pipeline
- Transfers are canceled, flushed, freed, and the claimed interface released
  on disconnect
- The first hardware pass is intentionally controller-input-only

Build:

```bash
pio run -e m5stack-cardputer-adv-usb-host
```

CI outputs `microgroove-v3-alpha-usb-host.bin`, a merged image flashed at
offset `0x0`. Because CDC is unavailable in this role, on-device MIDI status and
UI/behavior are the first diagnostics; USB-host telemetry over another channel
can be added only if hardware testing proves it necessary.

## Why there are two images

USB device mode lets a computer see Mini Studio 16 as MIDI equipment. Plugging
a Yamaha/CYD controller directly into the Cardputer makes the Cardputer the USB
host. These roles cannot share the S3's internal PHY simultaneously, and host
mode also changes flashing/debugging and VBUS responsibilities.

Do not connect two powered USB hosts together. The hardware pass must document:

- exact Cardputer-ADV USB connector behavior;
- OTG adapter/cable;
- whether the controller needs more current than the Cardputer can safely
  supply;
- whether external powered-hub VBUS is isolated correctly;
- how the board is reflashed after loading the host profile.

## Shared MIDI behavior

USB device, USB host, and BLE all feed the transport-independent MIDI layer.
That layer already handles:

- channel voice running status and note-on velocity zero;
- notes, drum routing, and control changes;
- realtime clock interleaved with other messages;
- 24-PPQN stepping, song position, start, continue, and stop;
- bounded queue overflow counters;
- outgoing note/CC/transport mirroring in device/BLE roles.

Incoming CC maps conventionally per synth channel (CC74 cutoff, CC71
resonance, CC7 volume). CC20–22 address synth 1–3 cutoff and CC23–25 address
synth 1–3 resonance from any channel. Internal transport schedules 24-PPQN
output clock with bounded catch-up and a dropped-pulse counter.

The host descriptor parser and USB-MIDI event packet decoder are pure C++ and
host-tested. The production host driver itself compiles/links against the
pinned ESP-IDF 4.4 USB Host API in CI.

## Physical acceptance gates

### Device profile

- CDC and MIDI enumerate together on Linux, macOS, and at least one DAW.
- The CLI and MIDI operate concurrently without disconnects or audio stalls.
- Note, CC, clock, start/continue/stop, and song position reach the instrument.
- Output notes/CC/transport reach the host without stuck notes.
- Twenty reconnect cycles and a 30-minute clock run cause no reboot/leak.

### Host profile

- Yamaha and CYD enumerate with documented OTG/VBUS hardware.
- Their interface/endpoint descriptors match or are safely rejected.
- Note and realtime traffic reaches the existing queue.
- Attach/detach, transfer cancel, queue overflow, a non-MIDI USB device, and a
  malformed/unsupported descriptor never crash or stall audio.
- A 30-minute external-clock run is repeatable.

Compilation proves API/toolchain compatibility; only these device tests prove
electrical, enumeration, and timing behavior.
