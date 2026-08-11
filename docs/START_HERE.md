# Start here: Mini Studio 16

This page is for someone who has never flashed an ESP32 before. You do not
need to install a DAW or compile source code.

Mini Studio 16 is still a pre-hardware alpha. The downloadable firmware passes
desktop tests and compiles for the Cardputer-ADV, but the first physical-device
test has not happened yet. Keep the original M5Stack factory firmware available
so you can restore it if needed.

## You need

- one **M5Stack Cardputer-ADV** (not the older Cardputer);
- one USB-C **data** cable—some charging-only cables do not carry data;
- one FAT32 microSD card, 32 GB or smaller for the least-friction first test;
- a Windows, macOS or Linux computer;
- the optional printed cradle only if you want it.

The Audio Cap is optional. Do not buy or assemble it for the first Cardputer
test. Its exact shopping list and two-part enclosure are covered later in
[`AUDIO_CAP_BUILD_GUIDE.md`](AUDIO_CAP_BUILD_GUIDE.md).

## The three steps

### 1. Download the verified package

1. Open [Build v3 alpha](https://github.com/CanadaOrNaw/Mini-Studio-16/actions/workflows/build-v3-alpha.yml).
2. Open the newest green run for `agent/v3-alpha-sd-streaming`.
3. At the bottom, download the artifact named
   `microgroove-v3-alpha-cardputer-adv`. GitHub may ask you to sign in.
4. Unzip it into a new empty folder.

The file for a first Cardputer flash is:

`mini-studio-16-dual-role.bin`

It contains both USB modes and boots into Normal mode first. Do not flash the
smaller application-only `firmware.bin` at address zero.

### 2. Flash it

Follow [`FLASHING.md`](FLASHING.md). That guide includes Windows, macOS and
Linux commands, how to find the port, the Cardputer G0 download-mode sequence,
the exact `0x0` address, recovery steps and separate optional Audio Cap flashing.

### 3. Prepare the SD card

1. Format the card as **FAT32** with an MBR partition table.
2. Open `Mini-Studio-16_SD.zip` from the downloaded package.
3. Copy its `groovebox` folder to the root of the SD card.
4. Safely eject the card and insert it into the powered-off Cardputer.
5. Switch the Cardputer on.

The first screen lets you keep Normal USB mode or switch to USB Host mode. Use
Normal mode for the first test. Press any key other than `Tab` to continue.

## Make the first sound

1. Keep headphone volume low or use the built-in speaker.
2. Hold `LOAD`, tap `SONG`, then release `LOAD` to load the factory project.
3. Press `PLAY`.
4. Use the [interactive button map](https://canadaornaw.github.io/MiniStudio.github.io/#controls)
   or the included `mini-studio-16-button-layout.svg` while learning the pages.

The factory project uses built-in drum engines, so it should make sound even if
sample files were copied incorrectly.

## If it does not work

- Nothing appears as a serial port: try another known data cable and USB port.
- Flashing cannot connect: repeat the exact G0 sequence in `FLASHING.md`.
- It boots but the SD card is missing: reformat as FAT32/MBR and copy the
  `groovebox` folder—not the outer ZIP—to the card root.
- It makes no sound: lower then raise the Mini Studio volume, unplug headphones,
  and rerun the factory-project steps.
- It resets, becomes hot, smells unusual or the display flickers: power it off
  and record the symptom before trying again.

Once the basic boot/audio pass works, use
[`CARDPUTER_TESTING.md`](CARDPUTER_TESTING.md) and record measured results. That
physical evidence is the remaining engineering gate.
