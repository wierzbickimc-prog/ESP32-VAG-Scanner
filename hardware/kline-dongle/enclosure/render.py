"""Render the case with the populated board: exploded, open and closed views.

  kicad-cli pcb export glb --subst-models -o board.glb ../kline-dongle.kicad_pcb
  python3 -m pip install manifold3d numpy trimesh matplotlib
  python3 render.py board.glb        # writes renders/*.png
"""
import sys
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import trimesh
from mpl_toolkits.mplot3d.art3d import Poly3DCollection

import enclosure

OUT = enclosure.HERE / "renders"
CASE = np.array([0.93, 0.93, 0.95])     # light grey PETG
LIGHT = np.array([0.35, -0.55, 0.75]); LIGHT /= np.linalg.norm(LIGHT)


def board_meshes(glb):
    """KiCad GLB (metres, Y up, Z = board y) -> mm in case coordinates, with colours."""
    scene = trimesh.load(glb)
    out = []
    for node in scene.graph.nodes_geometry:
        transform, name = scene.graph[node]
        g = scene.geometry[name].copy()
        g.apply_transform(transform)
        v = g.vertices * 1000.0
        g.vertices = np.column_stack([v[:, 0], -v[:, 2], v[:, 1]])  # x, -board_y, up
        g.invert()  # the axis swap mirrors winding
        colour = np.array([0.5, 0.5, 0.5])
        mat = getattr(g.visual, "material", None)
        if mat is not None and getattr(mat, "baseColorFactor", None) is not None:
            colour = np.asarray(mat.baseColorFactor[:3], float) / (255.0 if np.max(mat.baseColorFactor) > 1 else 1)
        out.append((g, colour))
    return out


def fine(mesh, edge=1.2):
    """Split long triangles: matplotlib sorts whole faces by depth."""
    v, f = trimesh.remesh.subdivide_to_size(mesh.vertices, mesh.faces, max_edge=edge, max_iter=12)
    return trimesh.Trimesh(v, f, process=False)


def shade(mesh, colour):
    n = mesh.face_normals
    k = 0.45 + 0.55 * np.clip(n @ LIGHT, 0, 1)
    return np.clip(np.asarray(colour, float)[None, :] * k[:, None], 0, 1)


def draw(ax, items):
    polys, colours = [], []
    for mesh, colour, offset in items:
        mesh = fine(mesh)
        tris = mesh.vertices[mesh.faces] + np.asarray(offset)
        polys.append(tris); colours.append(shade(mesh, colour))
    fc = np.concatenate(colours)
    pc = Poly3DCollection(np.concatenate(polys), facecolors=fc, edgecolors=fc, linewidths=0.25)
    ax.add_collection3d(pc)
    allv = np.concatenate([p.reshape(-1, 3) for p in polys])
    lo, hi = allv.min(0), allv.max(0)
    mid, span = (lo + hi) / 2, (hi - lo).max() / 2
    ax.set_xlim(mid[0] - span, mid[0] + span); ax.set_ylim(mid[1] - span, mid[1] + span)
    ax.set_zlim(mid[2] - span, mid[2] + span)
    ax.set_box_aspect((1, 1, 1)); ax.set_axis_off()


def render(name, items, elev=28, azim=-60, title=None):
    fig = plt.figure(figsize=(10, 7), dpi=150)
    ax = fig.add_subplot(projection="3d", computed_zorder=False)
    draw(ax, items)
    ax.view_init(elev=elev, azim=azim)
    if title:
        fig.text(0.5, 0.92, title, ha="center", fontsize=13)
    fig.subplots_adjust(0, 0, 1, 1)
    OUT.mkdir(exist_ok=True)
    fig.savefig(OUT / f"{name}.png", facecolor="white")
    plt.close(fig)
    print("wrote", OUT / f"{name}.png")


if __name__ == "__main__":
    base, lid = (enclosure.to_trimesh(m) for m in (enclosure.base, enclosure.lid))
    board = board_meshes(sys.argv[1] if len(sys.argv) > 1 else "board.glb")
    pcb = [(m, c, (0, 0, 0)) for m, c in board]
    render("exploded", [(base, CASE, (0, 0, 0))] + [(m, c, (0, 0, 22)) for m, c, _ in pcb]
           + [(lid, CASE, (0, 0, 48))], title="K-line dongle case: exploded")
    render("open", [(base, CASE, (0, 0, 0))] + pcb, elev=38, azim=-55, title="Board seated in the base")
    render("closed", [(base, CASE, (0, 0, 0)), (lid, CASE, (0, 0, 0))], elev=24, azim=-35,
           title="Closed: OBD pigtail enters left, USB-C right, LED window on top")
    render("closed-usb", [(base, CASE, (0, 0, 0)), (lid, CASE, (0, 0, 0))] + pcb, elev=12, azim=20,
           title="USB-C end")
