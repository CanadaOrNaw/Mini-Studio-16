# Audio Cap Rev-A PCB source

This folder is a code-defined, reviewable reference layout. It freezes board
outline, placement zones, connector map, core signal netlist, named MPNs, and
manufacturing outputs before hardware arrives.

**Important:** the tscircuit source intentionally uses explicit reference
footprints for several mechanically unique parts and has routing disabled. The
first-article layout is **not released as a blindly orderable production
Gerber**. USB-C shell stakes, the WROOM land pattern/antenna keep-out, the exact
Cardputer connector stack height, analogue ground/filter placement, and every
manufacturer footprint must be checked against current datasheets by the PCB
assembler/layout reviewer. A generated Gerber from unchecked proxy footprints
would look convenient while creating a real risk of an expensive unusable
board.

The beginner build therefore says “order assembled from the reviewed Rev-A
package,” not “hand wire RF/audio circuits.” Once the first physical Cardputer
and board are available, the checklist records footprint and fit sign-off; the
same source then becomes the manufacturing release without changing the
firmware protocol or enclosure boundary.

## Rebuild review artifacts

```sh
npm install
npm run build
npm run check
```

The committed outputs include Circuit JSON, PCB SVG, schematic SVG, and a
machine-readable diagnostic report. They are deterministic review artifacts,
not a waived DRC/manufacturing approval.

`hardware/audio-cap/BOM.csv` is the controlled procurement BOM. The tscircuit
source is the placement/net review aid; it does not override that BOM.
