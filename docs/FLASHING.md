# Flash Mini Studio 16 without compiling it

This guide writes one already-built image to a Cardputer-ADV. It does not
require PlatformIO, Arduino IDE or a DAW.

## Before you start

You need the unzipped green Actions artifact described in
[`START_HERE.md`](START_HERE.md), a USB-C **data** cable and Python 3.

Install the pinned flashing tool:

| Computer | Command |
| --- | --- |
| Windows PowerShell | `py -m pip install esptool==4.8.1` |
| macOS Terminal | `python3 -m pip install esptool==4.8.1` |
| Linux terminal | `python3 -m pip install --user esptool==4.8.1` |

In the commands below, Windows users type `py` where the guide says `python3`.

## 1. Find the Cardputer port

1. Set the Cardputer side power switch to **OFF**.
2. Hold the side `G0` button.
3. While holding `G0`, connect the USB-C data cable to the computer.
4. Release `G0`. The screen may stay blank; that is normal download mode.

M5Stack documents this same sequence in its
[Cardputer-ADV download-mode guide](https://docs.m5stack.com/en/core/Cardputer-Adv).

Find the new port:

- Windows: open **Device Manager → Ports (COM & LPT)** and note a name such as
  `COM5`.
- macOS: run `ls /dev/cu.usb*` and note the new path.
- Linux: run `ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null` and note the new path.

Use that exact value in place of `PORT` below.

## 2. Flash the combined Cardputer image

Open a terminal in the unzipped artifact folder. First erase the old flash:

```bash
python3 -m esptool --chip esp32s3 --port PORT erase_flash
```

Then write the combined image at address `0x0`:

```bash
python3 -m esptool --chip esp32s3 --port PORT --baud 460800 write_flash 0x0 mini-studio-16-dual-role.bin
```

Wait for `Hash of data verified`. Disconnect USB, move the side switch to ON,
then reconnect USB only if you want charging/serial. The first boot selects
Normal USB mode. `Tab` switches between Normal and USB Host after a validated
reboot; it does not require another flash.

Erasing replaces the factory firmware and settings stored in flash. It does not
erase the removable microSD card.

## 3. Verify the downloaded files (recommended)

The artifact includes `SHA256SUMS.txt`. On macOS or Linux:

```bash
sha256sum -c SHA256SUMS.txt
```

macOS may provide `shasum -a 256` instead of `sha256sum`; compare the result for
`mini-studio-16-dual-role.bin` with its line in `SHA256SUMS.txt`. On Windows:

```powershell
certutil -hashfile mini-studio-16-dual-role.bin SHA256
```

Compare the printed value with the same filename in `SHA256SUMS.txt`. Do not
flash a file whose hash differs.

## Optional: flash the ATOM Lite Audio Cap controller

Do this **before** assembling the cap. The ATOM must be by itself—not connected
to the Cardputer, PCM1808 or power harness.

1. Connect the ATOM Lite to the computer with its USB-C data cable. M5Stack says
   the original ATOM Lite enters download mode when connected for flashing.
2. Find its port using the same operating-system steps above.
3. In the artifact folder run:

```bash
python3 -m esptool --chip esp32 --port PORT erase_flash
python3 -m esptool --chip esp32 --port PORT --baud 460800 write_flash 0x0 mini-studio-audio-cap-atom-lite/mini-studio-audio-cap-atom-lite.bin
```

4. Wait for hash verification and unplug the programming cable.
5. Continue with [`AUDIO_CAP_BUILD_GUIDE.md`](AUDIO_CAP_BUILD_GUIDE.md).

The ATOM's USB cable is only used while programming. It is not present in the
finished one-plug Cardputer-powered cap.

## Recovery and common errors

| Message or symptom | What to do |
| --- | --- |
| `Failed to connect` | Repeat OFF → hold G0 → connect USB → release G0; then retry. |
| Port disappears | Try a different data cable/USB port and avoid an unpowered hub. |
| `Permission denied` on Linux | Add your user to the serial-port group (often `dialout`), sign out/in, then retry. Do not run random commands as root. |
| Write fails at high baud | Repeat the write command with `--baud 115200`. |
| Cardputer boot-loops after a completed write | Re-enter G0 mode, erase, verify the image hash, and write again at `0x0`. |
| Need the factory firmware | Follow M5Stack's [Cardputer-ADV restore guide](https://docs.m5stack.com/en/guide/restore_factory/cardputer_adv). |

Developers who intentionally want to compile or upload from source can use the
PlatformIO commands in the main README. Beginners should use the merged images
above because they contain the bootloader, partition table and application at
the correct addresses.
