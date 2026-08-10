# Flashing and recovery (beginner guide)

These instructions are for **M5Stack Cardputer-ADV**. Keep the device on a
non-metal table. Use a USB-C cable that carries data; charge-only cables are the
most common cause of “nothing happens.”

## 1. Download the files

1. Open the repository's **Actions** tab.
2. Open the newest green **Build v3 alpha** run.
3. Download `microgroove-v3-alpha-cardputer-adv` from **Artifacts**.
4. Unzip it into a new folder.

Use these two files:

| File | Job |
| --- | --- |
| `mini-studio-16-dual-role.bin` | One complete 8 MB image containing Normal and USB Host apps |
| `Mini-Studio-16_SD.zip` | Ready-made folders, samples, and starter project for the microSD card |

`SHA256SUMS.txt` lets an advanced user check that no file was damaged.

## 2. Prepare the microSD card

1. Put the card in your computer.
2. Format it as **FAT32** with an MBR partition table.
3. Unzip `Mini-Studio-16_SD.zip`.
4. Copy the `groovebox` folder to the top level of the card.
5. Eject the card safely and insert it into the powered-off Cardputer-ADV.

After copying, this folder must exist: `/groovebox/samples/`.

## 3. Flash the Cardputer-ADV

### Friendly command-line method

Install Python 3, open Terminal/PowerShell in the unzipped artifact folder, and
install Espressif's flasher:

```sh
python -m pip install esptool==4.8.1
```

Plug in the Cardputer, then find its serial port:

- Windows: Device Manager → Ports, such as `COM6`
- macOS: `/dev/cu.usbmodem...`
- Linux: `/dev/ttyACM0` or `/dev/ttyUSB0`

Replace `YOUR_PORT` below:

```sh
python -m esptool --chip esp32s3 --port YOUR_PORT erase_flash
python -m esptool --chip esp32s3 --port YOUR_PORT --baud 460800 write_flash 0x0 mini-studio-16-dual-role.bin
```

Unplug USB, wait two seconds, and plug it in again.

### If the port does not appear

1. Unplug USB.
2. Hold **G0** on the Cardputer-ADV.
3. Plug USB in while holding G0.
4. Release G0 after two seconds and retry the command.

## 4. First boot and USB role

- Press **BtnA** to continue into the selected firmware.
- Hold **Tab** on the startup screen to switch between:
  - **Normal** — USB serial + USB MIDI device for a computer/DAW.
  - **USB Host** — the Cardputer controls a class-compliant USB MIDI keyboard.
- Switching roles reboots; it does not require reflashing.

## 5. Check that the flash worked

Open a serial terminal at 115200 baud. A healthy boot prints `BOOT_READY` and
`MS16/1 READY`. Send:

```text
MS16/1 hello ping
```

The reply contains `OK pong=1`. The optional CLI can do the same:

```sh
python tools/ministudio_cli.py --port YOUR_PORT ping
```

## Flashing the optional Audio Cap

The cap is a different original-ESP32 image. Follow
[Audio Cap build](AUDIO_CAP_BUILD.md#flash-the-cap) and never write the cap image
to the Cardputer.

## Troubleshooting

| Symptom | Try this |
| --- | --- |
| Port missing | Try another known data cable, then use the G0 recovery steps |
| “Wrong chip” | You selected the Audio Cap port/image; reconnect the Cardputer |
| Boots but samples are missing | Recheck FAT32 and `/groovebox/samples/` |
| Reboots during flash | Charge the device, disconnect accessories, lower baud to `115200` |
| USB MIDI keyboard does nothing | Boot the **USB Host** role; Normal role is a USB device |
| Cap is absent/faulty | Remove it and reboot; the stock instrument must still work |

The official board pinout and recovery information remain available in the
[M5Stack Cardputer-ADV documentation](https://docs.m5stack.com/en/core/Cardputer-Adv).
