"""3D-printable two-part case for the K-line dongle: base.stl + lid.stl.

The board sits on two ledges in the base; the lid's lip fits inside the walls and
its posts hold the board, the XIAO and the converter down. The car connects
through an OBD-II male pigtail cable entering the left end; the USB-C port is open
at the right end, and a hole over D5 shows the status LED.

Positions come from ../gen/place.py, so the openings follow the PCB if parts move.

  python3 -m pip install manifold3d numpy trimesh
  python3 enclosure.py            # writes base.stl and lid.stl, printable orientation
"""
import sys
from pathlib import Path

import numpy as np
import trimesh
from manifold3d import Manifold

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "gen"))
from place import PLACE, BOARD_W, BOARD_H  # noqa: E402

# ---------------------------------------------------------------- parameters (mm)
PCB_T = 1.6        # board thickness
CLR = 0.5          # board edge to wall
WALL = 2.0
FLOOR = 1.6
LID = 1.6
BELOW = 3.0        # under the board: converter leads stick out 2.4 mm
ABOVE = 9.6        # board top to ceiling: converter body is 8.6 mm tall
LIP = 1.2          # lid lip thickness
LIP_DEPTH = 1.6
LIP_CLR = 0.2      # per side; loosen for a tighter printer, tighten for PETG
LEDGE = 0.8        # how far the ledges reach under the board edge
CABLE_D = 6.5      # OBD pigtail cable; measure yours
USB_W, USB_H = 12.5, 7.0   # room for a USB-C plug's overmould
LED_D = 2.5

Z_FLOOR = -BELOW                    # board bottom is z = 0
Z_TOP = PCB_T + ABOVE               # ceiling = top of the base walls
X0, X1 = -CLR, BOARD_W + CLR        # cavity, board coordinates
Y0, Y1 = -CLR, BOARD_H + CLR


def box(x0, x1, y0, y1, z0, z1):
    """Axis box in board coordinates (KiCad: y down) -> model (y up)."""
    return Manifold.cube([x1 - x0, y1 - y0, z1 - z0]).translate([x0, -y1, z0])


def cyl_x(x0, x1, y, z, d):
    """Cylinder along x."""
    c = Manifold.cylinder(x1 - x0, d / 2, d / 2, 48)
    return c.rotate([0, 90, 0]).translate([x0, -y, z])


def cyl_z(x, y, z0, z1, d):
    return Manifold.cylinder(z1 - z0, d / 2, d / 2, 48).translate([x, -y, z0])


def rounded_slot_x(x0, x1, yc, zc, w, h):
    """Stadium-shaped hole through an end wall (USB-C)."""
    r = h / 2
    body = box(x0, x1, yc - w / 2 + r, yc + w / 2 - r, zc - r, zc + r)
    return body + cyl_x(x0, x1, yc - w / 2 + r, zc, h) + cyl_x(x0, x1, yc + w / 2 - r, zc, h)


# ---------------------------------------------------------------- base
outer = box(X0 - WALL, X1 + WALL, Y0 - WALL, Y1 + WALL, Z_FLOOR - FLOOR, Z_TOP)
cavity = box(X0, X1, Y0, Y1, Z_FLOOR, Z_TOP + 1)
base = outer - cavity
# ledges under the long edges (nothing on the underside comes within 3 mm of them)
base += box(0, BOARD_W, Y0, LEDGE, Z_FLOOR, 0)
base += box(0, BOARD_W, BOARD_H - LEDGE, Y1, Z_FLOOR, 0)

# USB-C: XIAO centre line, receptacle 1.0-4.2 mm above the carrier
ux, uy, _ = PLACE["U4"]
usb_z = PCB_T + 2.6
base -= rounded_slot_x(X1 - 0.1, X1 + WALL + 0.1, uy, usb_z, USB_W, USB_H)

# OBD pigtail: enters beside J1 (pins at x 2.5, y 11.9-19.5), above the board
jx, jy, _ = PLACE["J1"]
cable_y = jy - 3 * 2.54 / 2
cable_z = PCB_T + 4.5
base -= cyl_x(X0 - WALL - 0.1, X0 + 0.1, cable_y, cable_z, CABLE_D)

# ---------------------------------------------------------------- lid (assembled position)
lid = box(X0 - WALL, X1 + WALL, Y0 - WALL, Y1 + WALL, Z_TOP, Z_TOP + LID)
lo = (X0 + LIP_CLR, X1 - LIP_CLR, Y0 + LIP_CLR, Y1 - LIP_CLR)
lip = box(lo[0], lo[1], lo[2], lo[3], Z_TOP - LIP_DEPTH, Z_TOP) - \
      box(lo[0] + LIP, lo[1] - LIP, lo[2] + LIP, lo[3] - LIP, Z_TOP - LIP_DEPTH - 1, Z_TOP + 1)
# the converter body comes within 0.8 mm of the bottom edge: no lip over it
cx, cy, _ = PLACE["U1"]
lip -= box(cx - 1.5, cx + 20.5, BOARD_H - 3, Y1 + 1, Z_TOP - LIP_DEPTH - 1, Z_TOP + 1)
lid += lip

# hold-downs: two posts onto bare board corners, one onto the XIAO shield,
# one pad over the converter body
lid += box(0.2, 1.8, BOARD_H - 1.8, BOARD_H - 0.2, PCB_T, Z_TOP)
lid += box(31.2, 32.8, 0.2, 1.8, PCB_T, Z_TOP)
shield_x, shield_y = ux - 3.5, uy        # RF shield centre on the rotated XIAO
lid += box(shield_x - 1.5, shield_x + 1.5, shield_y - 1.5, shield_y + 1.5, PCB_T + 3.3 + 0.1, Z_TOP)
lid += box(cx + 4, cx + 14, cy - 1, cy + 5, PCB_T + 8.6 + 0.2, Z_TOP)

# status LED window over D5
lx, ly, _ = PLACE["D5"]
lid -= cyl_z(lx, ly, Z_TOP - 1, Z_TOP + LID + 1, LED_D)


# ---------------------------------------------------------------- export
def to_trimesh(m):
    mesh = m.to_mesh()
    return trimesh.Trimesh(vertices=np.asarray(mesh.vert_properties)[:, :3],
                           faces=np.asarray(mesh.tri_verts), process=True)


def export():
    base_t, lid_t = to_trimesh(base), to_trimesh(lid)
    for name, t in [("base", base_t), ("lid", lid_t)]:
        assert t.is_watertight, f"{name} is not watertight"
    # print orientation: base floor down; lid flipped so its plate is on the bed
    b = base_t.copy(); b.apply_translation(-b.bounds[0])
    l = lid_t.copy(); l.apply_transform(trimesh.transformations.rotation_matrix(np.pi, [1, 0, 0]))
    l.apply_translation(-l.bounds[0])
    b.export(HERE / "base.stl"); l.export(HERE / "lid.stl")
    ext = base_t.bounds[1] - base_t.bounds[0]
    print(f"outside {ext[0]:.1f} x {ext[1]:.1f} x {ext[2] + LID:.1f} mm; wrote base.stl, lid.stl")
    return base_t, lid_t


if __name__ == "__main__":
    export()
