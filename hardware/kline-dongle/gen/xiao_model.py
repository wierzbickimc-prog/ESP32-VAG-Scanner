"""Write dongle.3dshapes/XIAO_ESP32C3.wrl: a simple box model of the Seeed XIAO ESP32C3.

Approximate outline only (from Seeed's 21 x 17.8 mm drawing): module PCB, RF shield,
USB-C receptacle overhanging the USB end by 1.4 mm, U.FL connector and the two
buttons. Good enough for renders and enclosure clearance, not for mechanical sign-off.
Coordinates are the footprint's (mm, x right, y down, z up from the carrier).
"""
from pathlib import Path

OUT = Path(__file__).resolve().parent.parent / "dongle.3dshapes" / "XIAO_ESP32C3.wrl"
INCH10 = 2.54  # KiCad reads VRML in units of 0.1 inch

PCB = (0.10, 0.20, 0.45)
METAL = (0.78, 0.78, 0.80)
BLACK = (0.08, 0.08, 0.08)
GOLD = (0.85, 0.68, 0.25)

# (x0, x1, y0, y1, z0, z1, colour) in footprint coordinates
BOXES = [
    (-8.9, 8.9, -10.5, 10.5, 0.0, 1.0, PCB),        # module PCB
    (-6.5, 6.5, -2.0, 9.0, 1.0, 3.3, METAL),        # RF shield over the ESP32-C3
    (-4.47, 4.47, -11.9, -4.6, 1.0, 4.2, METAL),    # USB-C receptacle
    (4.4, 6.6, 8.2, 10.2, 1.0, 2.25, GOLD),         # U.FL antenna connector
    (-7.6, -5.4, -9.0, -6.6, 1.0, 2.0, BLACK),      # reset button
    (5.4, 7.6, -9.0, -6.6, 1.0, 2.0, BLACK),        # boot button
]


def box(x0, x1, y0, y1, z0, z1, rgb):
    cx, cy, cz = (x0 + x1) / 2, -(y0 + y1) / 2, (z0 + z1) / 2  # VRML y is up = -footprint y
    sx, sy, sz = x1 - x0, y1 - y0, z1 - z0
    u = lambda v: f"{v / INCH10:.5f}"
    return (f"Transform {{ translation {u(cx)} {u(cy)} {u(cz)} children [ Shape {{\n"
            f"  appearance Appearance {{ material Material {{ diffuseColor {rgb[0]} {rgb[1]} {rgb[2]} "
            f"specularColor 0.2 0.2 0.2 shininess 0.3 }} }}\n"
            f"  geometry Box {{ size {u(sx)} {u(sy)} {u(sz)} }} }} ] }}\n")


OUT.parent.mkdir(exist_ok=True)
OUT.write_text("#VRML V2.0 utf8\n# Seeed XIAO ESP32C3, approximate (gen/xiao_model.py)\n"
               + "".join(box(*b) for b in BOXES))
print("wrote", OUT)
