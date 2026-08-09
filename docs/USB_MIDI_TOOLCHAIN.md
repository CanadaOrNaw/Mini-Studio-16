# USB MIDI integration boundary

Mini Studio 16 now has a transport-independent MIDI byte parser, bounded event
queue, note/drum routing, song-position handling, and 24-PPQN clock transport.
Those pieces are host-tested. They do not, by themselves, make the Cardputer's
USB connector enumerate as a MIDI device or host an external USB controller.

## Why the adapter is a separate milestone

The reproducible firmware environment is pinned to `espressif32@6.7.0`, which
uses Arduino-ESP32 2.0.16 / ESP-IDF 4.4.x. That Arduino tag exposes the native
USB CDC path used by the command protocol, but its `libraries/USB` package does
not contain the later Arduino `USBMIDI` class. Enabling USB MIDI in the current
build is therefore not a one-line include.

ESP32-S3 hardware can provide USB MIDI. Espressif's current TinyUSB device stack
lists MIDI among its supported classes and includes a `tusb_midi` example:

- <https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/usb_device.html>
- <https://github.com/espressif/esp-idf/tree/master/examples/peripherals/usb/device/tusb_midi>

The next adapter spike must choose and compile one of these paths:

1. migrate the project to an Arduino-ESP32/ESP-IDF release that exposes the
   required TinyUSB MIDI device support, then regression-test M5Cardputer,
   M5Unified, SD, speaker, microphone, serial control, and the firmware image;
2. keep the pinned environment and integrate a compatible TinyUSB MIDI
   component plus composite CDC+MIDI descriptors explicitly.

The second path avoids a broad framework migration but owns more USB descriptor
and stack integration. The first path is easier to maintain if all existing
hardware libraries continue to build and behave correctly. Neither path should
replace CDC serial: the intended computer-facing device is composite CDC+MIDI
so the CLI and MIDI can coexist.

## Device mode is not host mode

USB device mode lets a computer or DAW see Mini Studio 16 as MIDI equipment.
Directly plugging a class-compliant Yamaha keyboard or another controller into
the Cardputer makes the Cardputer a USB host. Espressif documents host mode as a
separate stack whose class driver and daemon/client tasks must be provided by
the application:

- <https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/usb_host.html>

Host validation also depends on OTG cabling and safe VBUS power. The S3's native
USB controllers share one internal PHY, so USB role, flashing/debugging path,
and simultaneous CDC availability must be tested as one system rather than
assumed independently.

## Existing adapter contract

A future USB-device, USB-host, or BLE adapter only needs to feed received MIDI
bytes into `midiInputFeedByte()` from one producer context. Parsing and musical
dispatch stay outside the USB callback. If more than one physical MIDI input is
active simultaneously, each producer must receive its own queue and the main
loop must merge their drained events; the current queue is intentionally SPSC.

## Acceptance gate

The USB MIDI milestone is complete only when:

- the pinned or migrated target firmware compiles in CI;
- CDC serial and MIDI enumerate together in device mode;
- note, CC, clock, start, continue, stop, and song-position traffic reaches the
  existing MIDI queue without blocking the audio task;
- attach, detach, malformed packets, and queue overflow do not reboot or stall
  the instrument;
- direct-controller host mode is tested separately with documented cable and
  power requirements.
