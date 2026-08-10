# Line-input and conventional Bluetooth-audio expansion contract

The stock Cardputer-ADV already provides speaker, built-in mic, headphone
output, battery, and BLE MIDI. Its ESP32-S3 radio is Bluetooth LE-only, so it
cannot be a conventional Bluetooth Classic A2DP source for ordinary headphones
or speakers. The onboard 3.5 mm connector is output, not a line input.

Official references:

- [M5Stack Cardputer-ADV documentation](https://docs.m5stack.com/en/core/Cardputer-Adv)
- [M5Stack Cap-Bus example pin map](https://docs.m5stack.com/en/cap/Cap_LoRa-1262)
- [Espressif ESP32-S3: Wi-Fi + Bluetooth 5 LE](https://www.espressif.com/en/products/socs/esp32-s3)
- [Espressif original ESP32 A2DP API](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/bluetooth/esp_a2dp.html)
- [ESP32-S3 I2S/full-duplex driver documentation](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/i2s.html)

## Proposed Audio Cap boundary

Use a Cardputer-ADV Cap-Bus board containing:

- an external line-input ADC/codec with input protection, biasing, antialiasing,
  and gain control;
- a Bluetooth Classic-capable coprocessor (original ESP32 or a purpose-built
  certified audio module) providing A2DP source, and optionally A2DP sink;
- local buffering/clock-domain conversion between the codec/Bluetooth side and
  the Cardputer's 22.05 kHz mono engine;
- correct level shifting, reset, power filtering, ESD protection, and jack
  detect as required by the selected parts.

Do not cut the Cardputer microphone/headphone traces for the first prototype.
The cap is independently removable and the stock instrument remains usable.

## Reserved Cap-Bus assignment

The official Cardputer-ADV cap mapping exposes G3, G4, G5, G6, G13, and G15 in
addition to pins already used by SD/I2C. Reserve:

| Cardputer pin | Audio Cap signal | Purpose |
| --- | --- | --- |
| G3 | `CAP_CS` | SPI chip select |
| G4 | `CAP_SCLK` | SPI clock |
| G5 | `CAP_MOSI` | master mix/control to cap |
| G6 | `CAP_MISO` | line/BT input and status to Cardputer |
| G13 | `CAP_IRQ` | cap has capture/status packet ready |
| G15 | `CAP_RESET` | controlled coprocessor reset/boot recovery |
| 5V/GND | power | cap regulator and common reference |

G40/G14/G39 remain untouched because the Cardputer microSD uses them. G8/G9
remain the shared onboard I2C bus. G12 remains SD chip select. Pin ownership
must be checked against the exact production board schematic before PCB layout.

SPI is chosen instead of a second loosely coupled I2S clock domain because it
provides framing, status, sequence/error detection, and two-way PCM without
reassigning the onboard ES8311 path. A dedicated second ESP32-S3 SPI peripheral
can route to these GPIOs; bus speed is deliberately unspecified until signal
integrity is measured on the cap.

## Wire protocol already implemented here

`audio_cap_protocol.h/.cpp` defines version 1:

- fixed 272-byte full-duplex packets;
- 128 mono signed-16 frames at 22,050 Hz (~5.8 ms);
- sequence number, direction/status flags, and bounded frame count;
- CRC-32 over header and PCM;
- flags for valid PCM, selected line input, Bluetooth pair state, underrun, and
  overrun.

Payload bandwidth is about 47 KiB/s in each direction before framing, far below
ordinary SPI capacity. The layout/CRC/bounds have host and sanitizer tests.
The cap-side device/codec choice is intentionally not baked into the protocol.

## Intended audio routing

Host-to-cap PCM is the final master bus for A2DP output. Cap-to-host PCM is a
selectable line/Bluetooth input that enters the same bounded recording/monitor
path as other long inputs. Neither direction may block the audio renderer:

- the renderer pushes/pops local SPSC rings;
- an expansion task performs SPI transactions;
- sequence gaps and CRC failures increment counters and substitute silence;
- cap underrun/overrun flags appear in UI/`MS16/1` telemetry;
- input monitoring has a user-controlled level and defaults off to prevent
  feedback.

## Hardware-dependent decisions

These cannot be responsibly fixed without the actual parts/prototype:

- codec/module choice and availability;
- analog input impedance, maximum level, gain, noise, and protection;
- A2DP codec/latency behavior and certification constraints;
- power draw from 5 V, battery impact, regulator temperature, and RF layout;
- SPI clock, reservoir size, clock drift correction, and acceptable latency;
- whether stereo is worth a v2 protocol (the current engine is mono);
- simultaneous A2DP source/sink feasibility on the selected coprocessor.

## Acceptance gates

- Line input handles the documented level without clipping/damage and meets the
  chosen noise/frequency-response target.
- Ten-minute bidirectional PCM soak has zero unreported sequence/CRC errors.
- Bluetooth pair/reconnect and 30-minute output cause no Cardputer underrun.
- End-to-end latency and clock drift are measured, not estimated.
- Cap removal leaves the stock firmware fully functional.
- A power fault/reset on the cap cannot crash, back-power, or corrupt the
  Cardputer/SD card.

The packet contract and firmware-side architecture are ready; electrical/codec
selection and the production driver are now legitimately gated by expansion
hardware rather than SD capacity or an undefined interface.
