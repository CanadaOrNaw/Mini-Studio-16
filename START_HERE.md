# Start here: build Mini Studio 16

This is the shortest safe path from an unopened Cardputer-ADV to a working
Mini Studio 16. You do not need to know programming. Read each checkbox, do one
thing, then move to the next one.

> **Young builders:** ask an adult to help with downloads, the USB cable, the
> microSD card, and every Audio Cap/PCB step. Never connect a speaker output to
> the Audio Cap line input. The normal Cardputer build has no soldering.

## What to buy for the normal instrument

- M5Stack **Cardputer-ADV** (not the older Cardputer)
- USB-C **data** cable
- 8–32 GB microSD card, FAT32 (a larger card can work after FAT32 formatting)
- Optional: wired headphones with a 3.5 mm plug
- Optional: access to a 3D printer for the bench cradle

You do **not** need the Audio Cap to use the synths, drums, sampler, six audio
loops, event looper, motion, BLE/USB MIDI, master recording, stems, mic,
speaker, headphones, battery, serial control, or dual USB roles.

## Five small jobs

1. [Download the latest GitHub Actions firmware artifact](docs/FLASHING.md#1-download-the-files).
2. [Prepare the microSD card](docs/FLASHING.md#2-prepare-the-microsd-card).
3. [Flash the one-file dual-role image](docs/FLASHING.md#3-flash-the-cardputer-adv).
4. Insert the card and follow the [five-minute first-song lesson](docs/USER_MANUAL.md#five-minute-first-song).
5. If you have a printer, follow [Print the cradle and Audio Cap shell](docs/PRINTING.md).

## Optional Audio Cap

The removable Audio Cap adds a real 3.5 mm line input and conventional
Bluetooth-headphone/speaker output. Rev A is fully specified in source, has its
own firmware, BOM, printable enclosure, and manufacturing package, but is
marked **NOT HARDWARE VERIFIED** until the first physical prototype is tested.

- [What to buy in Canada, the United States, or Europe](hardware/audio-cap/SOURCING.md)
- [How to order, flash, assemble, and test it](docs/AUDIO_CAP_BUILD.md)
- [Why it is designed this way](docs/AUDIO_CAP_DESIGN.md)

## If anything goes wrong

Do not guess. Use the symptom table in [Flashing and recovery](docs/FLASHING.md#troubleshooting),
then run the [Cardputer hardware test checklist](docs/CARDPUTER_TESTING.md). A
failed optional cap must never stop the stock instrument from booting.
