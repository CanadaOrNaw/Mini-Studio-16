# Cardputer-ADV alpha test guide

This branch is an engineering alpha. It preserves the existing instrument and
adds the first measurements needed before long-audio streaming is enabled.

## Build and flash

Build with the checked-in PlatformIO environment:

```bash
pio run -e m5stack-cardputer-adv
```

Upload over USB:

```bash
pio run -e m5stack-cardputer-adv -t upload --upload-port /dev/ttyACM0
```

Or flash the CI-produced merged image at offset `0x0`:

```bash
esptool.py --chip esp32s3 write_flash 0x0 microgroove-v3-alpha.bin
```

Do not flash an image until its build job and host-tests job both pass.

## SD test procedure

1. Back up the card. The test only creates files under `/groovebox/diag`, but a
   backup is still appropriate before testing development firmware.
2. Insert the FAT32 card and boot Microgroove.
3. Start the demo or a pattern and keep it playing. The point is to exercise
   storage without stopping the existing audio renderer.
4. Tap `ctrl` until the `SD TEST` page appears.
5. Press `/` once. Do not remove power or the card while the page says RUNNING.
6. Record the final screen and the one-line `SDDIAG` result from a 115200-baud
   USB serial monitor.
7. Repeat at least three times after a cold boot.

This built-in test is a quick screening pass over 3 MiB of generated data. A
passing result permits longer qualification work; it does not satisfy the
10-minute M1 soak gate by itself.

Example serial result:

```text
SDDIAG state=PASS write=1234KB/s read=2456KB/s rr6=1789KB/s maxWrite=18000us maxRead=9000us minHeap=121000 errors=0
```

PASS currently means:

- write throughput at least 500 KiB/s;
- sequential and six-file read throughput at least 1000 KiB/s;
- no individual measured read/write/flush stall above 75 ms;
- no read, write or data-verification errors.

These are starting gates, not marketing specifications. Save FAIL results too;
the maximum stall and minimum heap are more useful than the label.

## New sequencer controls

- `tab` switches pattern bank A/B.
- Bank A maps the eight pattern keys to patterns 1–8.
- Bank B maps them to patterns 9–16.
- Song mode now has 128 entries. Moving the cursor past entry 64 switches the
  visible grid to entries 65–128.
- Projects save as GBX v3. GBX v1/v2 projects still load into the first eight
  patterns and first 64 chain entries.
- Saving uses a temporary file and retains the prior project as `P#.gbx.bak`
  after a successful replacement.

## Results to return for the next iteration

- Card make, model, capacity and FAT32 cluster size.
- All `SDDIAG` lines, including failures.
- Whether audio clicked, paused or rebooted during the six-file read.
- Any screen freeze or keyboard lag.
- Whether the card cold-boots and mounts reliably on ten consecutive starts.
