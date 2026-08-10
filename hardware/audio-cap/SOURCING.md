# Audio Cap parts and regional sourcing

**Checked 2026-08-10.** Stock and prices change. Search by the exact
manufacturer part number (MPN); the MPN matters more than the shop link. Do not
buy similarly named ESP32-S3 modules—the cap needs the original ESP32's
Bluetooth Classic radio.

## Core parts

| Ref | MPN | Canada | United States | European Union |
| --- | --- | --- | --- | --- |
| U1 | `ESP32-WROOM-32E-N4` | [DigiKey CA](https://www.digikey.ca/en/products/detail/espressif-systems/ESP32-WROOM-32E-N4/11613125), [Mouser CA search](https://www.mouser.ca/c/?q=ESP32-WROOM-32E-N4) | [DigiKey US](https://www.digikey.com/en/products/detail/espressif-systems/ESP32-WROOM-32E-N4/11613125), [Mouser US search](https://www.mouser.com/c/?q=ESP32-WROOM-32E-N4) | [DigiKey DE search](https://www.digikey.de/en/products?keywords=ESP32-WROOM-32E-N4), [Mouser EU search](https://eu.mouser.com/c/?q=ESP32-WROOM-32E-N4) |
| U2 | `PCM1808PWR` | [DigiKey CA search](https://www.digikey.ca/en/products?keywords=PCM1808PWR), [Mouser CA search](https://www.mouser.ca/c/?q=PCM1808PWR) | [DigiKey US search](https://www.digikey.com/en/products?keywords=PCM1808PWR), [TI](https://www.ti.com/product/PCM1808) | [DigiKey AT](https://www.digikey.at/en/products/detail/texas-instruments/PCM1808PWR/1573355), [Mouser EU](https://eu.mouser.com/c/?q=PCM1808PWR) |
| U3 | `CP2102N-A02-GQFN24` | [DigiKey CA](https://www.digikey.ca/en/products/detail/silicon-labs/CP2102N-A02-GQFN24/9863476) | [DigiKey US search](https://www.digikey.com/en/products?keywords=CP2102N-A02-GQFN24) | [Mouser EU](https://eu.mouser.com/c/?exact=true&q=CP2102N-A02-GQFN24) |
| U4 | `AP63203WU-7` | [DigiKey CA search](https://www.digikey.ca/en/products?keywords=AP63203WU-7) | [DigiKey US search](https://www.digikey.com/en/products?keywords=AP63203WU-7) | [Mouser EU search](https://eu.mouser.com/c/?q=AP63203WU-7) |
| Y1 | `KC3225Z11.2896C16X00` | [DigiKey CA search](https://www.digikey.ca/en/products?keywords=KC3225Z11.2896C16X00) | [DigiKey US](https://www.digikey.com/en/products/detail/kyocera-avx/KC3225Z11-2896C16X00/22614856) | [Mouser EU search](https://eu.mouser.com/c/?q=KC3225Z11.2896C16X00) |
| J1 | `SJ-3523-SMT-TR` | [DigiKey CA search](https://www.digikey.ca/en/products?keywords=SJ-3523-SMT-TR) | [DigiKey US](https://www.digikey.com/en/products/detail/same-sky-formerly-cui-devices/SJ-3523-SMT-TR/281297) | [TME EU](https://www.tme.eu/en/details/sj3523smttr/jack-connectors/same-sky/sj-3523-smt-tr/), [Mouser EU search](https://eu.mouser.com/c/?q=SJ-3523-SMT-TR) |
| J2 | `USB4105-GF-A` | [DigiKey CA search](https://www.digikey.ca/en/products?keywords=USB4105-GF-A) | [DigiKey US](https://www.digikey.com/en/products/detail/gct/USB4105-GF-A/11198441) | [Mouser EU search](https://eu.mouser.com/c/?q=USB4105-GF-A) |
| D1 | `SP0503BAHTG` | [DigiKey CA search](https://www.digikey.ca/en/products?keywords=SP0503BAHTG) | [DigiKey US search](https://www.digikey.com/en/products?keywords=SP0503BAHTG) | [TME EU](https://www.tme.eu/en/details/sp0503bahtg-lf/protection-diodes-arrays/littelfuse/sp0503bahtg/), [Mouser EU](https://eu.mouser.com/c/?q=SP0503BAHTG) |

The complete quantities, passives, footprints, manufacturer names, and
substitution policy are in [`BOM.csv`](BOM.csv). A PCBA manufacturer can source
those lines directly; an individual builder should not have to hand-buy every
0603 resistor.

## Main instrument and printing

| Item | Canada | United States | European Union |
| --- | --- | --- | --- |
| M5Stack Cardputer-ADV | [M5Stack distributors](https://docs.m5stack.com/en/products/where-to-buy), DigiKey CA/Mouser CA search | M5Stack official/DigiKey/Mouser | M5Stack distributors, DigiKey/Mouser/TME search |
| 8–32 GB microSD | Any reputable local electronics/office retailer | Same | Same |
| PETG filament | Local printer supplier; 1 kg is far more than needed | Same | Same |

## Substitution rules

- **Do not substitute U1 with ESP32-S3/C3/C6.** They do not provide the same
  Classic Bluetooth A2DP source capability.
- PCM1808`PW` and `PWR` describe the same IC/package with different reel
  packing; confirm TSSOP-14 and assembly-house handling.
- The oscillator must be 11.2896 MHz at 3.3 V with compatible enable/padout.
- Any regulator substitute needs at least 1 A continuous headroom, radio-burst
  transient performance, the correct feedback/fixed voltage, and a fresh layout
  review.
- Jack, USB-C, switches, and header substitutions require a footprint and shell
  opening check—not just electrical equivalence.
