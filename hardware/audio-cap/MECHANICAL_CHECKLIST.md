# Audio Cap Rev-A physical acceptance record

Create one copy of this file per prototype and write the board serial/date at
the top. Do not mark an item passed from a render or compiler result.

## Unpowered

- [ ] PCBA dimensions and 2×7 header match the drawing
- [ ] No short between 5V, 3V3, and GND
- [ ] Header orientation/continuity matches Cardputer EXT pins
- [ ] Base seats without force; Cardputer ports remain accessible
- [ ] USB-C, line jack, pair button, and LED align
- [ ] Lid engages/releases ten times without whitening or cracking
- [ ] WROOM antenna keep-out contains no copper/metal/fastener

## Cap powered by its USB-C only

- [ ] 5V input and 3.3V rail are in tolerance
- [ ] Idle and pairing current recorded
- [ ] ESP32 serial `READY` appears and pair button works
- [ ] PCM1808 clock/BCLK/LRCK/data measured
- [ ] No component exceeds safe touch temperature after 30 minutes

## Cardputer connection

- [ ] Cap unpowered/no-USB before insertion
- [ ] Stock Cardputer boots with cap installed and removed
- [ ] SPI starts at 8 MHz with no CRC/sequence error in ten-minute silence soak
- [ ] Fault/reset cannot back-power, crash, or corrupt SD
- [ ] Line input level, clipping threshold, SNR, and frequency response recorded
- [ ] Monitor defaults to zero and feedback protection verified
- [ ] A2DP pair/reconnect/range/latency measured with three devices
- [ ] 30-minute audio + SD recording + BLE/MIDI stress has no audio deadline miss
- [ ] Battery run time and cap regulator temperature recorded

## Result

- Prototype ID:
- Firmware SHA:
- PCB revision:
- Printer/material/settings:
- Pass / rework / reject:
- Required source changes:
