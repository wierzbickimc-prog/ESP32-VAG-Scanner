"""Write dongle.kicad_sym, kline-dongle.kicad_sch, the project file and sym-lib-table.

Every connection is a short wire stub ending in a net label, so the schematic
reads as a netlist and stays in step with build_pcb.py (same PARTS, same UUIDs).
"""
import math, os, sys, uuid, datetime
sys.path.insert(0, os.path.dirname(__file__))
from parts import PARTS, SYMBOLS, TWO_PIN_NAMES, PIN_TYPES, DATASHEETS, DNP, PROCUREMENT

HERE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
NS = uuid.UUID("6b1c3a52-9a51-4c8e-9f0e-2f1f0b0d0e11")
ROOT = str(uuid.uuid5(NS, "root-sheet"))
G = 2.54

def sym_uuid(ref): return str(uuid.uuid5(NS, "sym-" + ref))
_n = [0]
def uid():
    _n[0] += 1
    return str(uuid.uuid5(NS, f"item-{_n[0]}"))
def q(s): return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'
def f(v): return f"{v:.4f}".rstrip("0").rstrip(".") if v != int(v) else str(int(v))

FONT = "(effects (font (size 1.27 1.27)))"
FONT_HIDE = "(effects (font (size 1.27 1.27)) (hide yes))"

# Schematic placement (mm, multiples of 2.54) - A3 landscape.
SCH = {
    "J1": (30.48, 45.72), "F1": (60.96, 45.72), "D1": (101.6, 45.72), "D2": (124.46, 45.72),
    "C1": (147.32, 45.72), "C2": (175.26, 45.72), "U1": (205.74, 45.72), "C3": (236.22, 45.72),
    "C4": (269.24, 45.72), "D3": (302.26, 45.72),
    "U2": (55.88, 96.52), "R1": (99.06, 96.52), "U3": (142.24, 96.52), "C5": (172.72, 96.52),
    "R3": (193.04, 96.52),
    "U4": (63.5, 152.4), "U6": (114.3, 147.32), "C8": (144.78, 147.32), "R6": (175.26, 147.32),
    "R7": (198.12, 147.32), "C6": (220.98, 147.32), "R4": (254.0, 147.32), "D5": (274.32, 147.32),
}
SCH.update({"Q1": (241.3, 96.52), "R10": (274.32, 96.52), "R11": (302.26, 96.52)})
for i in range(1, 9):
    SCH[f"TP{i}"] = (30.48 + (i - 1) * 30.48, 248.92)
for i in range(1, 4):
    SCH[f"#FLG0{i}"] = (30.48 + (i - 1) * 45.72, 274.32)

# ------------------------------------------------------------------ symbols
def two_pin_symbol(name, kind):
    """Vertical two-pin symbol: pin 1 on top, pin 2 at the bottom."""
    n1, n2 = TWO_PIN_NAMES.get(kind, ("~", "~"))
    body, plen = [], 1.27
    if kind == "res":
        body.append("(rectangle (start -1.016 2.54) (end 1.016 -2.54) (stroke (width 0.254) (type default)) (fill (type none)))")
    elif kind == "fuse":
        body.append("(rectangle (start -0.762 2.54) (end 0.762 -2.54) (stroke (width 0.254) (type default)) (fill (type none)))")
        body.append("(polyline (pts (xy 0 2.54) (xy 0 -2.54)) (stroke (width 0) (type default)) (fill (type none)))")
    elif kind == "cap":
        plen = 3.048
        for y in (0.762, -0.762):
            body.append(f"(polyline (pts (xy -2.032 {y}) (xy 2.032 {y})) (stroke (width 0.508) (type default)) (fill (type none)))")
    elif kind in ("diode", "led"):
        plen = 2.54
        body.append("(polyline (pts (xy -1.27 -1.27) (xy 1.27 -1.27) (xy 0 1.27) (xy -1.27 -1.27)) (stroke (width 0.254) (type default)) (fill (type none)))")
        body.append("(polyline (pts (xy -1.27 1.27) (xy 1.27 1.27)) (stroke (width 0.254) (type default)) (fill (type none)))")
        body.append("(polyline (pts (xy 0 -1.27) (xy 0 1.27)) (stroke (width 0) (type default)) (fill (type none)))")
        if kind == "diode" and name == "D_Zener":
            body.append("(polyline (pts (xy -1.778 0.762) (xy -1.27 1.27) (xy 1.27 1.27) (xy 1.778 1.778)) (stroke (width 0.254) (type default)) (fill (type none)))")
        if kind == "diode" and name == "D_Schottky":
            body.append("(polyline (pts (xy -1.27 1.27) (xy -1.27 1.778)) (stroke (width 0.254) (type default)) (fill (type none)))")
            body.append("(polyline (pts (xy 1.27 1.27) (xy 1.27 0.762)) (stroke (width 0.254) (type default)) (fill (type none)))")
        if kind == "led":
            for y in (0.0, -1.016):
                body.append(f"(polyline (pts (xy 1.778 {y}) (xy 3.048 {y + 1.016})) (stroke (width 0) (type default)) (fill (type none)))")
    elif kind == "tvs":
        body.append("(polyline (pts (xy -1.27 -2.54) (xy 1.27 -2.54) (xy 0 0) (xy -1.27 -2.54)) (stroke (width 0.254) (type default)) (fill (type none)))")
        body.append("(polyline (pts (xy -1.27 2.54) (xy 1.27 2.54) (xy 0 0) (xy -1.27 2.54)) (stroke (width 0.254) (type default)) (fill (type none)))")
        body.append("(polyline (pts (xy -1.778 0.508) (xy -1.27 0) (xy 1.27 0) (xy 1.778 -0.508)) (stroke (width 0.254) (type default)) (fill (type none)))")
    pins = [("1", n1, 0, 3.81, 270, plen), ("2", n2, 0, -3.81, 90, plen)]
    return body, pins, ("R" if kind == "res" else "C" if kind == "cap" else "F" if kind == "fuse" else "D"), True


def ic_symbol(name, spec):
    pins = spec["pins"]
    left = [p for p in pins if p[2] == "L"]
    right = [p for p in pins if p[2] == "R"]
    bottom = [p for p in pins if p[2] == "B"]
    rows = max(len(left), len(right), 1)
    ln = lambda side: max([len(p[1]) for p in side] or [0])
    w = max(10.16, math.ceil(((ln(left) + ln(right)) * 1.1 + 4) / (2 * G)) * 2 * G)
    h = (rows + 1) * G
    top = h / 2
    out = []
    for k, (num, nm, _) in enumerate(left):
        out.append((num, nm, -w / 2 - G, round((top - G * (k + 1)) / G) * G, 0, G))
    for k, (num, nm, _) in enumerate(right):
        out.append((num, nm, w / 2 + G, round((top - G * (k + 1)) / G) * G, 180, G))
    for k, (num, nm, _) in enumerate(bottom):
        out.append((num, nm, 0, -round(top / G) * G - G, 90, G))
    ytop = math.ceil(top / G) * G
    body = [f"(rectangle (start {f(-w / 2)} {f(ytop)}) (end {f(w / 2)} {f(-ytop)}) "
            "(stroke (width 0.254) (type default)) (fill (type background)))"]
    ref = {"OBD_Wires": "J", "Jumper": "J", "TestPoint": "TP", "PWR_FLAG": "#FLG", "N_MOS": "Q"}.get(name, "U")
    return body, out, ref, False


def build_symbol(name, prefix=""):
    spec = SYMBOLS[name]
    body, pins, ref, two = two_pin_symbol(name, spec["two"]) if "two" in spec else ic_symbol(name, spec)
    full = prefix + name
    lines = [f"(symbol {q(full)}" + (" (pin_numbers (hide yes)) (pin_names (offset 0) (hide yes))" if two
                                     else " (pin_names (offset 1.016))") +
             " (exclude_from_sim no) (in_bom yes) (on_board yes)"]
    for k, v in [("Reference", ref), ("Value", name), ("Footprint", ""), ("Datasheet", ""), ("Description", "")]:
        eff = FONT if k in ("Reference", "Value") else FONT_HIDE
        lines.append(f"  (property {q(k)} {q(v)} (at 0 0 0) {eff})")
    lines.append(f"  (symbol {q(name + '_0_1')}")
    lines += ["    " + b for b in body]
    lines.append("  )")
    lines.append(f"  (symbol {q(name + '_1_1')}")
    for num, nm, x, y, ang, length in pins:
        pin_type = PIN_TYPES.get(name, {}).get(num, "passive")
        lines.append(f"    (pin {pin_type} line (at {f(x)} {f(y)} {ang}) (length {f(length)}) "
                     f"(name {q(nm)} {FONT}) (number {q(num)} {FONT}))")
    lines.append("  )")
    lines.append(")")
    return "\n".join(lines), pins, two


# ------------------------------------------------------------------ library
lib_out = ["(kicad_symbol_lib (version 20231120) (generator \"kline_dongle_gen\") (generator_version \"1\")"]
SYMPINS = {}
for name in SYMBOLS:
    text, pins, two = build_symbol(name)
    lib_out.append(text)
    SYMPINS[name] = (pins, two)
lib_out.append(")")
open(os.path.join(HERE, "dongle.kicad_sym"), "w").write("\n".join(lib_out) + "\n")


# ------------------------------------------------------------------ schematic
sch = [f"(kicad_sch (version 20231120) (generator \"eeschema\") (generator_version \"8.0\")",
       f"  (uuid {q(ROOT)})",
       "  (paper \"A3\")",
       "  (title_block (title \"K-line dongle for ESP32-VAG-Scanner\") "
       f"(date {q(datetime.date.today().isoformat())}) (rev \"2\") "
       "(comment 1 \"OBD-II K-line (KW1281) -> ESP32-C3, wireless firmware planned\") "
       "(comment 2 \"GPL-3.0, see LICENSE\"))",
       "  (lib_symbols"]
used = sorted({p[1] for p in PARTS})
for name in used:
    sch.append(build_symbol(name, "dongle:")[0])
sch.append("  )")

def label(net, x, y, ang):
    just = {0: "left bottom", 180: "right bottom", 90: "left bottom", 270: "right bottom"}[ang]
    sch.append(f"  (label {q(net)} (at {f(x)} {f(y)} {ang}) "
               f"(effects (font (size 1.27 1.27)) (justify {just})) (uuid {q(uid())}))")

def wire(x1, y1, x2, y2):
    sch.append(f"  (wire (pts (xy {f(x1)} {f(y1)}) (xy {f(x2)} {f(y2)})) "
               f"(stroke (width 0) (type default)) (uuid {q(uid())}))")

for ref, symname, val, fp, netmap, _, desc in PARTS:
    X, Y = SCH[ref]
    pins, two = SYMPINS[symname]
    su = sym_uuid(ref)
    # field positions
    if two:
        rpos, vpos, rj = (X + 2.54, Y - 1.27), (X + 2.54, Y + 1.27), "(justify left)"
    else:
        ys = [p[3] for p in pins]
        rpos, vpos, rj = (X, Y - max(ys) - 6.35), (X, Y - min(ys) + 6.35), ""
        if any(side == "B" for _, _, side in SYMBOLS[symname].get("pins", [])):
            vpos = (X + 15.24, vpos[1])
    sch.append(f"  (symbol (lib_id {q('dongle:' + symname)}) (at {f(X)} {f(Y)} 0) (unit 1) "
               f"(exclude_from_sim no) (in_bom {'yes' if fp and ref not in DNP and not ref.startswith('TP') else 'no'}) (on_board {'yes' if fp else 'no'}) (dnp {'yes' if ref in DNP else 'no'}) (uuid {q(su)})")
    for k, v, pos, hide in [("Reference", ref, rpos, False), ("Value", val, vpos, False),
                            ("Footprint", fp, (X, Y), True), ("Datasheet", DATASHEETS.get(ref, ""), (X, Y), True),
                            ("Description", desc, (X, Y), True)]:
        eff = (f"(effects (font (size 1.27 1.27)) {rj})" if not hide else FONT_HIDE)
        sch.append(f"    (property {q(k)} {q(v)} (at {f(pos[0])} {f(pos[1])} 0) {eff})")
    if ref in PROCUREMENT:
        manufacturer, mpn, part_link = PROCUREMENT[ref]
        for key, value in [("Manufacturer", manufacturer), ("MPN", mpn), ("Part Link", part_link)]:
            sch.append(f"    (property {q(key)} {q(value)} (at {f(X)} {f(Y)} 0) {FONT_HIDE})")
    for num, *_ in pins:
        sch.append(f"    (pin {q(num)} (uuid {q(str(uuid.uuid5(NS, su + num)))}))")
    sch.append(f"    (instances (project \"kline-dongle\" (path {q('/' + ROOT)} (reference {q(ref)}) (unit 1))))")
    sch.append("  )")
    # stubs, labels, no-connects
    for num, nm, px, py, ang, length in pins:
        x, y = X + px, Y - py               # symbol y is up, schematic y is down
        dx, dy = {0: (-1, 0), 180: (1, 0), 90: (0, 1), 270: (0, -1)}[ang]
        net = netmap.get(num)
        if net is None:
            sch.append(f"  (no_connect (at {f(x)} {f(y)}) (uuid {q(uid())}))")
            continue
        ex, ey = x + dx * G, y + dy * G
        wire(x, y, ex, ey)
        lang = {(-1, 0): 180, (1, 0): 0, (0, 1): 270, (0, -1): 90}[(dx, dy)]
        label(net, ex, ey, lang)

NOTES = [
    (25.4, 226.06, "TP1-TP8: BACK SIDE probe pads. U1 is the horizontal R-78HB (8.5 mm high). Fit an external XIAO antenna inside the plastic shell."),

    (25.4, 22.86, "Power: OBD pin 16 (always-on battery) -> PTC fuse -> reverse diode -> TVS -> RECOM 5 V converter."),
    (25.4, 27.94, "USB may stay connected in the car: D3 keeps USB VBUS out of the converter (Seeed's recommended 5V-pad input)."),
    (25.4, 73.66, "K-line: L9637D (ISO 9141, no TX dominant timeout - LIN transceivers cut off the 200 ms 5-baud bits)."),
    (25.4, 78.74, "TX: 3.3 V -> 5 V through U3 (R3 holds TX idle; Q1/R10/R11 disable U3 until GPIO7 HIGH). RX: 5 V tolerant, 3.3 V Schmitt buffer U6 (Ioff isolation)."),
    (25.4, 124.46, "ESP32 pins: GPIO4 = K TX (UART1), GPIO5 = K RX (UART1), GPIO3 = battery sense (ADC1 ch3), GPIO10 = LED; GPIO7 = TX enable (active high). GPIO6 is free."),
    (25.4, 129.54, "Battery sense: 100k / 10k, VBAT = ADC volts x 11. Draws 0.11 mA; the 29.2 V TVS clamp reads 2.65 V."),
]
for x, y, s in NOTES:
    sch.append(f"  (text {q(s)} (exclude_from_sim no) (at {f(x)} {f(y)} 0) "
               f"(effects (font (size 1.27 1.27)) (justify left bottom)) (uuid {q(uid())}))")

sch.append("  (sheet_instances (path \"/\" (page \"1\")))")
sch.append(")")
open(os.path.join(HERE, "kline-dongle.kicad_sch"), "w").write("\n".join(sch) + "\n")

open(os.path.join(HERE, "sym-lib-table"), "w").write(
    '(sym_lib_table\n  (version 7)\n  (lib (name "dongle")(type "KiCad")(uri "${KIPRJMOD}/dongle.kicad_sym")(options "")(descr "K-line dongle parts"))\n)\n')
pro = os.path.join(HERE, "kline-dongle.kicad_pro")
if not os.path.exists(pro):
    open(pro, "w").write('{\n  "meta": {\n    "filename": "kline-dongle.kicad_pro",\n    "version": 1\n  }\n}\n')
print("schematic written")
