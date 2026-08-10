# Build the optional Mini Studio Audio Cap

The beginner path is **order the assembled board, flash it over USB-C, place it
in the printed base, close the lid, and plug it into the Cardputer**. Hand
soldering the fine-pitch ADC/USB parts is not the beginner path.

> Rev A has not yet touched physical hardware. Order only if you understand
> that the first board is a prototype. Young builders need an adult for every
> step in this guide.

## Before ordering

1. Read [design/status](AUDIO_CAP_DESIGN.md).
2. Use [regional sourcing](../hardware/audio-cap/SOURCING.md) to check every MPN.
3. Give the controlled BOM and `hardware/audio-cap/pcb/` reference source to a
   PCB layout/PCBA service. The checked-in reference deliberately does not
   pretend proxy footprints are production Gerbers.
4. Ask the reviewer to sign off the unique WROOM, USB-C, jack, 2×7 header, buck,
   ADC, oscillator, RF keep-out, analogue return, and ESD footprints against
   the named current datasheets. This first-article sign-off needs the physical
   connector/board and is the remaining hardware gate.
5. Export the reviewed Gerber/BOM/pick-and-place package; select 1.6 mm FR-4,
   2-layer, 1 oz copper, lead-free finish, and assembled SMT parts on the top
   side. Do not approve substitutions for U1, U2, U3, Y1, the jack, or the
   connector without a fresh review.
6. Ask the assembler to fit the keyed 2×7 connector last and inspect its
   orientation against the assembly drawing.

## Print the shell

Follow [Printing](PRINTING.md). Do an empty fit check before putting the PCBA in
the shell.

## Flash the cap

Build the cap image:

```sh
python -m pip install platformio==6.1.19 esptool==4.8.1
pio run -e mini-studio-audio-cap
```

Connect the cap's own USB-C port. Replace `YOUR_PORT`:

```sh
python -m esptool --chip esp32 --port YOUR_PORT erase_flash
python -m esptool --chip esp32 --port YOUR_PORT --baud 460800 write_flash \
  0x1000 .pio/build/mini-studio-audio-cap/bootloader.bin \
  0x8000 .pio/build/mini-studio-audio-cap/partitions.bin \
  0x10000 .pio/build/mini-studio-audio-cap/firmware.bin
```

At 115200 baud the cap prints `MS16-CAP/1 READY`. If it does not, stop before
plugging it into the Cardputer.

## Assemble without soldering

1. Unplug USB and power off the Cardputer.
2. Put the assembled PCBA into the printed base; USB-C, line jack, button, and
   LED must line up without bending.
3. Press the lid straight down until both side clips engage.
4. Check that no wire, metal shaving, or loose screw is inside.
5. Align the keyed 2×7 cap plug and press evenly. Never shift it sideways.
6. Power on. The normal Mini Studio features must work even before pairing.

## Pair Bluetooth audio

1. Put one Bluetooth headphone/speaker in pairing mode. Move other pairing
   devices away for this first prototype.
2. Press the cap's **PAIR** button once.
3. A blinking LED means scanning; solid means connected.
4. Wired headphones remain the low-latency choice. Bluetooth delay must be
   measured on hardware and is not promised for live monitoring.

## Use line input

1. Set the source device volume low.
2. Connect its **headphone or line output** to the cap's LINE IN. Never use a
   speaker/amplifier output.
3. Monitoring starts off to prevent feedback. Enable it over serial:

```text
MS16/1 cap1 cap monitor 20
```

Raise from 20 slowly. `cap monitor 0` turns monitoring off. Recording and
sampler routing can use the returned cap input after hardware validation.

## First-prototype test order

Do not skip ahead: shorts/unpowered continuity → regulator rails → cap-only USB
boot → ADC clock/data → unpowered Cardputer pin continuity → SPI at low volume
→ line sweep/noise → A2DP → 30-minute soak → enclosure/thermal test. Record all
results in the mechanical checklist and Cardputer test log.
