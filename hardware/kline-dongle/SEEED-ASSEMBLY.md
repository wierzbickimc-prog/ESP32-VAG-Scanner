# Seeed Fusion PCBA upload — revision 3

Order a **fully populated PCB**: Seeed purchases/solders all BOM components,
mounts the XIAO and connects its external antenna.
You only wire J1 to the OBD plug and install the board/antenna in your enclosure.
J1 is deliberately DNP: its four plated holes are your solder terminals.

The generated package is ready for a **prototype quotation/file review**. It is
not a tested product release. C3 application firmware and its programming image
are still pending; this package specifies hardware assembly and static testing.

## Generate the package

From `hardware/kline-dongle/`:

```sh
python3 gen/build_sch.py
/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3 gen/build_pcb.py
python3 gen/check_export.py
```

Skip the first two commands if you have intentionally edited the KiCad files by
hand. The last command checks the actual schematic and board, including parity,
then exports the purchasing fields stored in the schematic. It rejects missing
MPNs, quantity mismatches, and BOM/placement mismatches.

Outputs are in `build/fabrication/`:

| File | Where it goes |
|---|---|
| `seeed-gerbers.zip` | PCB/Gerber upload |
| `seeed-bom.csv` | PCBA BOM upload; quantity is **per one board** |
| `seeed-cpl.csv` | SMT pick-and-place/CPL upload or assembly attachments |
| `seeed-assembly-support.zip` | Supporting assembly files / engineering review |
| `assembly-F.pdf`, `assembly-B.pdf`, `pinmap-F.pdf` | Component locations, polarity, and numbered front pads |
| `FACTORY-NOTES.md` | Assembly, accessory procurement, static tests and shipment state |
| `accessories-to-quote.csv` | Included antenna; explicitly include its attachment in the quote |
| `bom-detail.csv` | Manufacturer, electrical value, MPN, package, and notes |
| `placements-all-kicad.csv` | Native positions including the U1 through-hole part |
| `manifest.json`, `erc.json`, `drc.json` | Source hashes and check results |

The Gerber archive contains RS-274X layers, outline `.gko`, Excellon drill `.txt`
files, and top solder paste. No bottom parts are purchased; TP1–TP8 are copper.
The BOM matches the headers in Seeed's downloadable template. Seeed supports
CSV BOMs with manufacturer part numbers or Seeed SKUs; do not substitute reseller
order numbers. [Seeed file guide](https://wiki.seeedstudio.com/Service_for_Fusion_PCB/),
[BOM preparation and template](https://support.seeedstudio.com/knowledgebase/articles/1886734-how-do-i-prepare-the-bill-of-materials-bom-file).

## Upload and quote instructions

1. Open [Seeed Fusion PCB Assembly](https://www.seeedstudio.com/pcb-assembly.html).
   Upload `seeed-gerbers.zip`. Check the viewer detects one **58 × 24 mm** board.
2. Select 2 layers, FR-4, 1.6 mm thickness, 1 oz finished copper, lead-free ENIG,
   and the desired quantity. Use a single-board design; request factory panelization,
   assembly rails and fiducials as needed, with finished boards depanelized.
3. Enable component sourcing and full PCB assembly. Upload `seeed-bom.csv`.
   Resolve every unmatched item, including XIAO SKU **113991054** and U1.
   The XIAO must be the **unsoldered-header** version, mounted flat.
4. Provide `seeed-cpl.csv` and `seeed-assembly-support.zip` through assembly-file
   uploads or the order's engineering attachment/review process. If an attachment
   slot is absent, use Seeed's quotation/support workflow before accepting the order.
5. Explicitly request **SMT plus through-hole assembly**, XIAO mounting, antenna
   and antenna attachment (listed in `accessories-to-quote.csv`). Those
   operations are written instructions, not extra fake PCB designators in the BOM.
   Ask for static test results from `FACTORY-NOTES.md`. Standard PCBA assembly
   should not be assumed to include those additional operations automatically.
6. Have Seeed confirm XIAO/pin-1 orientations and the CPL coordinate convention.
   `seeed-cpl.csv` uses millimetres, lower-left board origin, +X right, +Y up,
   KiCad counterclockwise rotations, Top side. It contains SMT only; U1 THT
   coordinates are in `placements-all-kicad.csv`, whose origin is upper-left and
   Y values are negative. Do not mix these two conventions.
7. Review the final sourcing/assembly quote and engineering drawings. Keep U2,
   U3, U6, D2 and F1 exact unless a replacement has been reviewed. Capacitor
   substitutes must preserve package, capacitance, voltage, X7R dielectric and
   effective capacitance; resistor substitutes must preserve tolerance/power.
   Stock and pricing are established by Seeed's quote, not by this repository.

Seeed asks for assembly drawings and placement information, and can source parts
outside its OPL. Its programming/test service needs an actual image and test
procedure. [PCBA specification](https://support.seeedstudio.com/knowledgebase/articles/457797-fusion-pcba-specification),
[assembly/programming file guide](https://wiki.seeedstudio.com/Service_for_Fusion_PCB/).

## Paste into the order notes

> Revision 3 K-line dongle: turnkey populated PCBA, all soldered parts including
> horizontal RECOM U1 (R-78HB5.0-0.5L, lies flat) and castellated XIAO ESP32C3 U4
> (Seeed SKU 113991054, no fitted headers). J1 is bare plated wire holes, no header. Attach the XIAO's included
> external antenna after soldering; no enclosure/wire installation is requested.
> Include accessory supply/antenna attachment, THT assembly and static tests in the
> quote. Follow FACTORY-NOTES.md and the assembly drawings. No firmware image is
> supplied; do not flash P4 firmware. Customer handles only OBD wiring/enclosure.
> Confirm component availability and polarity/rotation before assembly. Deliver
> depanelized boards with static test results; preserve access to USB.

## Your work after delivery

With USB and vehicle power disconnected, solder
insulated wires from **J1 pad 1 → OBD 16**, **pad 2 → OBD 4**, **pad 3 → OBD 5**,
**pad 4 → OBD 7**. Confirm plug numbering from its mating-face drawing; do not
infer it from a mirrored solder-side view. Both ground wires are required.
Fit strain relief and insulation. Position the already-connected antenna in the
plastic enclosure without pinching its cable or placing it against metal.

USB can stay connected while the dongle is plugged into the car: D3 keeps USB
power out of the converter. U1 lies flat; see `README.md` for the enclosure
envelope. Firmware/bench validation is still required
before normal vehicle use.
