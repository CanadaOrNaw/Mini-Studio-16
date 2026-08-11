# Solderless Audio Cap architecture

Mini Studio 16's optional Audio Cap adds a stereo 3.5 mm line input and sends
the instrument master to ordinary Bluetooth Classic A2DP headphones or
speakers. The finished cap has exactly one wired connection: its 14-pin male
plug into the Cardputer-ADV EXT socket.

These are hard design requirements, enforced by
`tests/test_audio_cap_requirements.py`:

| Requirement | Rev A implementation |
| --- | --- |
| Cardputer-powered | EXT pin 6 `5VOUT` and pin 4 `GND` |
| One plug | One keyed 2x7 2.54 mm male header |
| No second cable | No power lead or data lead exits the closed cap |
| No custom PCB | M5Stack ATOM Lite plus a preassembled PCM1808 module |
| No soldering | Factory sockets, precrimped leads and two lever splices |
| Off-the-shelf modules | Both active modules are retail products |
| Plug-in assembly | Dupont/Grove plugs and WAGO 221-413 levers |
| Two printed parts | Base plus compliant snap lid |

The PCM1808 module's small USB power plug, where that module revision requires
one, is a **hidden internal connector** fed from EXT `5VOUT`. It is installed
inside the cap and does not leave the enclosure. The ATOM Lite is also powered
from the same internal 5 V branch. The Cardputer battery or its normal USB-C
charger therefore powers the entire finished instrument.

## Why these modules

The Cardputer's ESP32-S3 supports BLE but not Bluetooth Classic A2DP. The
original ESP32-PICO-D4 inside the 24 x 24 mm ATOM Lite supports Bluetooth
Classic, has factory USB-C, a Grove socket, six factory GPIO sockets, a button,
and an RGB status LED. It runs a separately built cap firmware.

The selected PCM1808 module is already assembled with a 3.5 mm input, ADC,
clocking, regulators and I2S header. It is the I2S clock master at 48 kHz. The
ATOM receives stereo 24-bit samples in 32-bit slots, low-pass filters and
converts them to Mini Studio's 22.05 kHz mono stream. The reverse path converts
22.05 kHz mono to the 44.1 kHz stereo PCM required by A2DP.

Marketplace PCM1808 boards are not perfectly standardized. The purchase guide
therefore requires a photo match and the printable fit gauge before the final
cap is printed. USB-C versus Micro-USB on the module changes only the hidden
internal power plug.

## Exact connections

Cardputer pins used by SD (`G40/G14/G39`) and onboard I2C (`G8/G9`) are never
connected. EXT pin 2 `5VIN` is deliberately empty so the cap cannot back-power
the Cardputer.

| EXT pin | Cardputer | Function | ATOM Lite |
| ---: | --- | --- | --- |
| 1 | G3 | SPI chip select | G19 |
| 3 | G4 | Cap present/wake | G21 |
| 4 | GND | Internal ground branch | GND and ADC GND |
| 5 | G6 | SPI clock | G22 |
| 6 | 5VOUT | Internal +5 V branch | 5V and ADC 5V |
| 12 | G13 | SPI host-to-cap | G23 |
| 13 | G5 | SPI cap-to-host | G33 |

| ATOM Lite | PCM1808 module |
| --- | --- |
| G25 | DOUT |
| G26 / Grove yellow | BCK |
| G32 / Grove white | LRCK |
| GND | GND |
| not connected | MCLK (module is clock master) |

The canonical, machine-checked form is
[`hardware/audio-cap/design.json`](../hardware/audio-cap/design.json).

## Data path and failure behavior

`audio_cap_protocol.*` and `audio_cap_bridge_core.*` implement fixed 272-byte
full-duplex SPI packets containing 128 mono frames, a sequence number, bounded
frame count, commands/status and CRC-32. At 22,050 Hz each packet covers about
5.8 ms. Both sides use fixed 2,048-frame SPSC rings. There is no heap allocation
or blocking device I/O in the Cardputer render loop or A2DP data callback.

The Cardputer sends the dry master before line monitoring is mixed back, so an
A2DP output cannot feed itself. The monitored line signal is then included in
the speaker/headphone output, master WAV and master stem. Monitor level defaults
to zero. Removing or resetting the cap substitutes silence and leaves every
stock Mini Studio/Microgroove path operating.

Host tests cover CRC corruption, bounds, sequence wrap/gaps, overflow,
underrun, chunk-independent rate conversion, non-silent conversion and
high-frequency attenuation. The pinned CI build compiles both Cardputer images
and the ATOM Lite image.

## What still needs the physical parts

The enclosure and firmware are complete pre-hardware prototypes, not a claim
that unmeasured modules are electrically or mechanically proven. Hardware is
required to verify:

- Cardputer `5VOUT` current, voltage sag and temperature under A2DP peaks;
- exact header insertion depth and shell clearances;
- purchased PCM1808 dimensions, I2S format, clock stability, input level,
  noise and channel polarity;
- SPI reliability and clock drift during simultaneous SD recording;
- A2DP discovery, pairing, reconnect, latency and headphone compatibility;
- snap deflection, light pipe/button alignment and print shrinkage.

Use [`AUDIO_CAP_BUILD_GUIDE.md`](AUDIO_CAP_BUILD_GUIDE.md) for purchasing,
printing, assembly, flashing and the required first-power tests.

## Sources and attribution

- [M5Stack Cardputer-ADV](https://docs.m5stack.com/en/core/Cardputer-Adv) for
  EXT pinout, power and dimensions.
- [M5Stack ATOM Lite](https://docs.m5stack.com/en/core/ATOM%20Lite) for module
  dimensions, GPIO and original-ESP32 capabilities.
- [M5Stack Cap LoRa868](https://docs.m5stack.com/en/cap/Cap_LoRa868) as the
  product-form reference for a bus-powered plug-in cap. No M5Stack enclosure
  mesh is copied.
- [Espressif's A2DP source example](https://github.com/espressif/esp-idf/tree/master/examples/bluetooth/bluedroid/classic_bt/a2dp_source)
  for the original ESP32 A2DP source role.
- [ESP32-A2DP](https://github.com/pschatzmann/ESP32-A2DP) by Phil Schatzmann,
  Apache-2.0 licensed, is used by the ATOM firmware and pinned by commit.
- [bmorcelli/Launcher](https://github.com/bmorcelli/Launcher) informed earlier
  Cardputer SD research only. No Launcher code is included in the Audio Cap.
