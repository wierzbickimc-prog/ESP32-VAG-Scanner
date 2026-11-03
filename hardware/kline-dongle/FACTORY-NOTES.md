# Factory assembly notes — K-line dongle revision 3

## Scope and deliverables

Supply a completely soldered, depanelized PCBA with the XIAO module and antenna.
Customer performs OBD wiring and enclosure installation only. Include component
sourcing, SMT, through-hole insertion/soldering, module mounting, antenna
attachment in the quotation. Provide static
test results per board. This is an unbuilt prototype; report any assembly or test
failure for engineering review rather than silently changing the circuit.

No application firmware binary is supplied. Do not program the ESP32-P4 firmware
from the parent repository into the ESP32-C3. Protocol, radio and ADC functional
validation remain separate from these static acceptance tests.

## Population and assembly

- Populate every reference in `seeed-bom.csv`, using the exact MPN/Seeed SKU.
  Quantities are per single board; request factory panelization if necessary.
- **J1: do not fit a header, connector or wires.** Leave all four holes clear for
  customer soldering. TP1–TP8 are back-side probe pads, not purchased components.
- U1 is the horizontal RECOM R-78HB5.0-0.5L: it lies flat over the board, body
  toward the XIAO. It is the only through-hole part; everything else is top-side SMT.
  Keep U1's body flat on the board and clear of the XIAO.
- U4 is the unsoldered-header XIAO ESP32C3, Seeed SKU 113991054. Solder it flat to
  the carrier's castellated landing pads, with USB-C facing the board's right
  edge. Do not socket it or fit its optional pin headers. USB overhang is intended.
  Consult Seeed's module soldering/rework guidance; use hand/selective assembly
  if a further reflow cycle is unsuitable. Confirm access and carrier land pattern.
- The underside XIAO islands are electrically floating on the carrier. Do not
  bridge them to surrounding GND copper or add wires to battery/debug pads.
- Solder U1 after the small parts/module as appropriate. Keep USB-C and
  XIAO reset/boot buttons usable. Verify part pin 1 against `assembly-F.pdf`.
- D1 and D3: pad 1 is cathode. D5: pad 1 is LED cathode. D2 is bidirectional.
  Use the manufacturer pinout as well as markings; do not infer polarity from
  the rotation column alone. U2 pin 1 is RX; U3 pin 1 is /OE; U6 pin 1 is NC.
- Q1 2N7002: pad 1 gate, pad 2 source/GND, pad 3 drain/TX_OE_N. Q1/R10/R11 keep
  U3 disabled when the MCU is off or reset. Do not bridge U3 /OE to ground.
- After soldering, attach the external U.FL antenna included with U4. Confirm it
  is included in the quoted module supply; do not charge/source it twice. Pack
  it connected and loose for the customer's enclosure installation, protecting
  the antenna cable and U.FL connection against pulling or crushing.
- No battery, enclosure, OBD plug or OBD harness is part of this assembly order.

## Drawings and manufacturing data

`assembly-F.pdf` shows front assembly/polarity; `pinmap-F.pdf` adds numbered pads. The back drawing
shows probe pads; there are no back-side purchased parts. `seeed-cpl.csv` is SMT
only, millimetres, bottom-left origin and KiCad counterclockwise angles.
`placements-all-kicad.csv` also includes U1 but uses native KiCad origin and
negative Y. Review part orientation against package drawings before machine setup.

Finished board: 58 × 24 mm; 2-layer FR-4, 1.6 mm, 1 oz finished copper, lead-free
ENIG. Minimum design track/clearance 0.2 mm; via drill/diameter 0.3/0.6 mm;
copper-to-edge 0.5 mm. Add temporary panel rails/fiducials as required for the
assembly process, preserving the finished outline and module USB overhang.
Use the exported top paste layer; this is not a bare-PCB-only order.

## Static acceptance tests to quote

Perform tests on the bench, with **no vehicle/KKL cable connected**. Do not inject
surges or perform load-dump tests as part of these checks. Record rail voltages,
input current and pass/fail per board; retain a photo of the populated top side.

1. Inspect all joints/polarities and check for supply shorts before power.
2. USB unplugged. Apply 12.0 V at J1 pad 1; return to pads 2/3, with a 250 mA
   current limit (the module boots). TP2 must be 4.75–5.25 V and TP3 3.20–3.40 V.
   TP8 must read TP1/11 within 3% (about 1.09 V at 12.0 V). TP4/K-line should be
   at least 90% of TP1/VBAT with TX disabled; Q1 gate/TX_EN should be near GND.
   No sustained current-limit operation or overheating is allowed.
3. Remove bench 12 V and connect USB. TP3 must be 3.20–3.40 V and TP2 (converter
   +5 V) must stay below 0.30 V: USB must not power the converter through D3.
   USB enumeration may depend on the module's existing firmware/boot state;
   it is not a substitute for voltage measurements or an application test.
4. Reapply 12 V with USB still connected. TP2 and TP3 must stay in range.
5. Disconnect all power. Ship with the antenna attached. Confirm J1 holes remain
   clear and no pin headers were added to the XIAO.

The supply tests do not prove surge tolerance, calibration, RF performance,
protocol operation, or enclosure fit. Factory static tests must be explicitly
quoted and performed; the local KiCad checks are not physical test results.
