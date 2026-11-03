# JLCPCB assembly — revision 3

The low-cost route. JLCPCB makes the board and solders every surface-mount part
from its own stock. You solder three things yourself afterwards:

| Ref | Part | Why it isn't in the JLCPCB order |
|---|---|---|
| U4 | Seeed XIAO ESP32C3, **unsoldered-header** version (SKU 113991054) | Not stocked by JLCPCB. About $5 from Seeed, DigiKey or Mouser |
| U1 | RECOM R-78HB5.0-0.5L 5 V converter | Not stocked; through-hole. About $7 from DigiKey or Mouser |
| J1 | OBD-II pigtail wires | Your four wires, as with the Seeed build |

Parts were chosen from JLCPCB stock on 2026-11-01; `gen/parts.py` lists each one
with its LCSC number. Stock moves, so the BOM step below may flag a part as short.

## Generate the files

From `hardware/kline-dongle/`, the same commands as the Seeed package:

```sh
python3 gen/build_sch.py
/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3 gen/build_pcb.py
python3 gen/check_export.py
```

The JLCPCB files are in `build/fabrication/`:

| File | Upload step |
|---|---|
| `jlc-gerbers.zip` | Gerber upload |
| `jlc-bom.csv` | Assembly → BOM |
| `jlc-cpl.csv` | Assembly → CPL (pick and place) |

## Order

1. Go to [jlcpcb.com](https://jlcpcb.com/) → **Order now** and upload `jlc-gerbers.zip`.
   The viewer should show one 58 × 24 mm, 2-layer board.
2. PCB options: FR-4, 1.6 mm, quantity 5, any colour. **Lead-free HASL** is fine
   and cheaper than ENIG. Leave the rest at the defaults.
3. Turn on **PCB Assembly**: **Economic**, **Top side**, assemble 2 or all 5 boards.
   Tooling holes: "Added by JLCPCB". Confirm parts placement: Yes.
4. Upload `jlc-bom.csv` and `jlc-cpl.csv`. Every line should match its JLCPCB
   part number. U1, U4 and J1 are deliberately absent.
5. **Check the placement preview carefully. This is the step that catches mistakes.**
   Rotations are pre-corrected for JLCPCB's models (SOT-23 180°, SC-70 180°,
   SOIC 270°; checked against JLCPCB's preview), but confirm each part's pin 1 against `assembly-F.pdf`:
   - U2 (SOIC-8), U3 (SOT-23-5), U6 (SC-70): the part's pin-1 dot sits on the
     footprint's pin-1 pad (the one marked in `assembly-F.pdf`).
   - Q1 (SOT-23): single pin (drain) on its own side.
   - D1, D3 and the LED D5: the cathode band on pad 1. D2 has no polarity.
   If a part is wrong, rotate it in the preview, and tell me the correction so I
   can add it to `JLC_ROTATION` in `gen/check_export.py`.
6. Place the order. Expect about $50–55 before shipping for 5 boards; shipping
   and any import duty are extra.

## Finish at home

1. **XIAO (U4):** lay it flat on the castellated pads with USB-C overhanging the
   right edge. Tack two opposite corners, check alignment, then solder all 14 pads.
   The underside islands stay unsoldered.
2. **Converter (U1):** body flat on the board, toward the XIAO, leads through
   the three holes. Pin 1 (VIN) goes in the square pad.
3. **Bench check before the car:** run the static tests in `FACTORY-NOTES.md`
   (12 V at J1 with a 250 mA limit, then USB alone, then both).
4. Wire J1 to the OBD-II pigtail (16 = +12 V, 4 and 5 = GND, 7 = K-line).

### Cheaper converter

LCSC sells K7805-500R3 converters for about $1.15 (C19188491). They use the same
pinout and fit if you bend the leads 90° so the body lies flat. These are
third-party versions (EVISUN, YLPTEC), not genuine Mornsun. They are rated
32 V maximum input rather than the RECOM's 72 V. D2 clamps surges to about 29 V,
so it works on paper, but the RECOM has far more margin for a part left in a car.
