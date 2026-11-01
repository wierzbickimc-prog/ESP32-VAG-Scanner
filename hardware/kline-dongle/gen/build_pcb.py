"""Build kline-dongle.kicad_pcb: place footprints, route on a grid (A*), pour GND.

Run with KiCad's Python:
  /Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3 build_pcb.py
"""
import heapq, itertools, math, os, sys, uuid, tempfile
sys.path.insert(0, os.path.dirname(__file__))
import pcbnew, wx
app = wx.App(False)  # KiCad 10 geometry / font services need a wx application
from parts import PARTS, SYMBOLS, FP, HERE, DATASHEETS, DNP, PROCUREMENT
from place import PLACE, REF_OFFSET, BOARD_W, BOARD_H, GND_STITCH

OUT = os.path.join(os.path.dirname(__file__), "..", "kline-dongle.kicad_pcb")
NS = uuid.UUID("6b1c3a52-9a51-4c8e-9f0e-2f1f0b0d0e11")

GRID = 0.2          # router grid, mm
CLR = 0.2           # copper clearance, mm
EDGE = 0.5          # copper to board edge, mm
VIA_D, VIA_DRILL = 0.6, 0.3
WIDTH = {"VBAT_IN": 0.6, "VBAT_F": 0.6, "VBAT": 0.4, "+5V": 0.4, "XIAO_5V": 0.4,
         "GND": 0.25, "KLINE": 0.3}
SIGNAL_W = 0.25
# routing order: hardest / most constrained first
ORDER = ["KLINE", "VBAT_IN", "VBAT_F", "VBAT", "+5V", "TXD", "TX_OE_N", "TX_EN", "KTX", "KRX", "RXD", "VSENSE",
         "+3V3", "LED", "LED_A", "XIAO_5V", "GND"]

mm = pcbnew.FromMM
def V(x, y): return pcbnew.VECTOR2I(mm(x), mm(y))
def sym_uuid(ref): return str(uuid.uuid5(NS, "sym-" + ref))


# ---------------------------------------------------------------- board + parts
board = pcbnew.BOARD()
ds = board.GetDesignSettings()
ds.SetCopperLayerCount(2)
ds.m_MinClearance = mm(CLR)
ds.m_TrackMinWidth = mm(0.2)
ds.m_ViasMinSize = mm(VIA_D)
ds.m_MinThroughDrill = mm(VIA_DRILL)
ds.m_CopperEdgeClearance = mm(EDGE)

def edge(x1, y1, x2, y2):
    s = pcbnew.PCB_SHAPE(board)
    s.SetShape(pcbnew.SHAPE_T_SEGMENT)
    s.SetStart(V(x1, y1)); s.SetEnd(V(x2, y2))
    s.SetLayer(pcbnew.Edge_Cuts); s.SetWidth(mm(0.1))
    board.Add(s)
for a, b in [((0, 0), (BOARD_W, 0)), ((BOARD_W, 0), (BOARD_W, BOARD_H)),
             ((BOARD_W, BOARD_H), (0, BOARD_H)), ((0, BOARD_H), (0, 0))]:
    edge(*a, *b)

nets = {}
def net(name):
    if name not in nets:
        n = pcbnew.NETINFO_ITEM(board, name)
        board.Add(n)
        nets[name] = n
    return nets[name]

footprints = {}
for ref, sym, val, fp, pins, _, desc in PARTS:
    if not fp:  # schematic-only power-source declarations
        continue
    lib, name = fp.split(":")
    libpath = str(HERE / "dongle.pretty") if lib == "dongle" else f"{FP}/{lib}.pretty"
    m = pcbnew.FootprintLoad(libpath, name)
    if m is None:
        raise FileNotFoundError(f"Cannot load footprint {fp} from {libpath}")
    m.SetFPID(pcbnew.LIB_ID(lib, name))
    m.SetReference(ref)
    m.SetValue(val)
    m.SetField("Description", desc)
    m.SetField("Datasheet", DATASHEETS.get(ref, ""))
    if ref in PROCUREMENT:
        manufacturer, mpn, part_link = PROCUREMENT[ref]
        for key, value in [("Manufacturer", manufacturer), ("MPN", mpn), ("Part Link", part_link)]:
            m.SetField(key, value)
            m.GetField(key).SetVisible(False)
    if ref in DNP:
        m.Models().clear()  # bare wire holes: no header in renders
        m.SetAttributes(m.GetAttributes() | pcbnew.FP_DNP | pcbnew.FP_EXCLUDE_FROM_BOM | pcbnew.FP_EXCLUDE_FROM_POS_FILES)
    if ref.startswith("TP"):
        m.SetAttributes(m.GetAttributes() | pcbnew.FP_EXCLUDE_FROM_BOM | pcbnew.FP_EXCLUDE_FROM_POS_FILES)
    m.SetPath(pcbnew.KIID_PATH("/" + sym_uuid(ref)))
    x, y, rot = PLACE[ref]
    board.Add(m)  # Flip needs a parent board in KiCad 10
    if ref.startswith("TP"):
        m.Flip(V(0, 0), False)
    m.SetPosition(V(x, y))
    m.SetOrientationDegrees(rot)
    m.Reference().SetTextSize(V(0.8, 0.8))
    m.Reference().SetTextThickness(mm(0.12))
    m.Reference().SetTextAngleDegrees(0)
    dx, dy = REF_OFFSET[ref]
    m.Reference().SetPosition(V(x + dx, y + dy))
    for g in m.GraphicalItems():
        if g.GetClass() == "PCB_TEXT" and g.GetText() == "${REFERENCE}":
            g.SetPosition(V(x + dx, y + dy))  # same readable labels as the checked silkscreen
            g.SetTextSize(V(0.8, 0.8))
            g.SetTextAngleDegrees(0)
    m.Value().SetVisible(False)
    for p in m.Pads():
        n = pins.get(p.GetNumber())
        if n:
            p.SetNet(net(n))
    footprints[ref] = m


# ---------------------------------------------------------------- copper model
NX, NY = int(BOARD_W / GRID) + 1, int(BOARD_H / GRID) + 1
LAYERS = (pcbnew.F_Cu, pcbnew.B_Cu)

class Shape:
    """Copper on one layer: an axis-aligned rect, or a segment/circle with half width."""
    def __init__(self, layer, netname, rect=None, seg=None, hw=0.0):
        self.layer, self.net, self.rect, self.seg, self.hw = layer, netname, rect, seg, hw
    def bbox(self):
        if self.rect:
            return self.rect
        (x1, y1), (x2, y2) = self.seg
        return (min(x1, x2) - self.hw, min(y1, y2) - self.hw, max(x1, x2) + self.hw, max(y1, y2) + self.hw)
    def dist(self, x, y):
        if self.rect:
            x1, y1, x2, y2 = self.rect
            dx = max(x1 - x, 0, x - x2); dy = max(y1 - y, 0, y - y2)
            return math.hypot(dx, dy)
        (x1, y1), (x2, y2) = self.seg
        vx, vy = x2 - x1, y2 - y1
        L = vx * vx + vy * vy
        t = 0 if L == 0 else max(0, min(1, ((x - x1) * vx + (y - y1) * vy) / L))
        return math.hypot(x - (x1 + t * vx), y - (y1 + t * vy)) - self.hw

shapes = []
pad_cells = {}   # net -> list of (pad, set of (layer_idx, i, j))

def pad_rect(p):
    bb = p.GetBoundingBox()
    return (pcbnew.ToMM(bb.GetX()), pcbnew.ToMM(bb.GetY()),
            pcbnew.ToMM(bb.GetRight()), pcbnew.ToMM(bb.GetBottom()))

for ref, m in footprints.items():
    for p in m.Pads():
        n = p.GetNetname() or None
        r = pad_rect(p)
        ls = [li for li, L in enumerate(LAYERS) if p.IsOnLayer(L)]
        for li in ls:
            shapes.append(Shape(li, n, rect=r))
        if n:
            cells = set()
            i0, i1 = math.ceil(r[0] / GRID), math.floor(r[2] / GRID)
            j0, j1 = math.ceil(r[1] / GRID), math.floor(r[3] / GRID)
            # keep only cells that lie well inside the pad
            for i in range(i0, i1 + 1):
                for j in range(j0, j1 + 1):
                    x, y = i * GRID, j * GRID
                    if r[0] + 0.1 <= x <= r[2] - 0.1 and r[1] + 0.1 <= y <= r[3] - 0.1:
                        for li in ls:
                            cells.add((li, i, j))
            if not cells:  # tiny pad: use the nearest cell to its centre
                c = p.GetPosition()
                i, j = round(pcbnew.ToMM(c.x) / GRID), round(pcbnew.ToMM(c.y) / GRID)
                cells = {(li, i, j) for li in ls}
            pad_cells.setdefault(n, []).append((p, cells))


def blocked_grid(netname, hw):
    """Cells where a track of half width hw of netname may not be centred."""
    blk = [bytearray(NX * NY) for _ in LAYERS]
    margin = CLR + hw
    em = int(math.ceil((EDGE + hw) / GRID))
    for b in blk:
        for i in range(NX):
            for j in range(NY):
                if i < em or j < em or i > NX - 1 - em or j > NY - 1 - em:
                    b[i * NY + j] = 1
    for s in shapes:
        if s.net == netname:
            continue
        x1, y1, x2, y2 = s.bbox()
        i0 = max(0, int((x1 - margin) / GRID)); i1 = min(NX - 1, int((x2 + margin) / GRID) + 1)
        j0 = max(0, int((y1 - margin) / GRID)); j1 = min(NY - 1, int((y2 + margin) / GRID) + 1)
        b = blk[s.layer]
        for i in range(i0, i1 + 1):
            for j in range(j0, j1 + 1):
                if not b[i * NY + j] and s.dist(i * GRID, j * GRID) < margin:
                    b[i * NY + j] = 1
    return blk


def via_ok(blk_via, i, j):
    return not blk_via[0][i * NY + j] and not blk_via[1][i * NY + j]


DIRS = [(1, 0, 1.0), (-1, 0, 1.0), (0, 1, 1.0), (0, -1, 1.0),
        (1, 1, 1.414), (1, -1, 1.414), (-1, 1, 1.414), (-1, -1, 1.414)]
LAYER_COST = (1.0, 1.6)   # keep the bottom mostly for the ground pour
VIA_COST = 8.0
BEND_COST = 0.3


def astar(sources, targets, blk, blk_via, tcenter):
    tx, ty = tcenter
    def h(i, j): return math.hypot(i - tx, j - ty) * 0.99
    openq, came, g = [], {}, {}
    tick = itertools.count()
    for s in sources:
        g[(s, None)] = 0.0
        heapq.heappush(openq, (h(s[1], s[2]), 0.0, next(tick), s, None))
        came[(s, None)] = None
    while openq:
        f, gc, _, node, d = heapq.heappop(openq)
        if g.get((node, d), 1e18) < gc:
            continue
        if node in targets:
            path, key = [], (node, d)
            while key:
                path.append(key[0]); key = came[key]
            return path[::-1]
        li, i, j = node
        steps = []
        for di, dj, c in DIRS:
            ni, nj = i + di, j + dj
            if 0 <= ni < NX and 0 <= nj < NY and (not blk[li][ni * NY + nj] or (li, ni, nj) in targets):
                # diagonal moves must not cut corners past blocked cells
                if di and dj and blk[li][(i + di) * NY + j] and blk[li][i * NY + j + dj]:
                    continue
                cost = c * LAYER_COST[li] + (BEND_COST if d and d != (di, dj) else 0)
                steps.append(((li, ni, nj), (di, dj), cost))
        if via_ok(blk_via, i, j):
            steps.append(((1 - li, i, j), d, VIA_COST))
        for nn, nd, c in steps:
            ng = gc + c
            if ng < g.get((nn, nd), 1e18):
                g[(nn, nd)] = ng
                came[(nn, nd)] = (node, d)
                heapq.heappush(openq, (ng + h(nn[1], nn[2]), ng, next(tick), nn, nd))
    return None


def add_track(netname, li, a, b, w):
    t = pcbnew.PCB_TRACK(board)
    t.SetStart(V(*a)); t.SetEnd(V(*b))
    t.SetWidth(mm(w)); t.SetLayer(LAYERS[li]); t.SetNet(net(netname))
    board.Add(t)
    shapes.append(Shape(li, netname, seg=(a, b), hw=w / 2))

def add_via(netname, xy):
    v = pcbnew.PCB_VIA(board)
    v.SetPosition(V(*xy)); v.SetDrill(mm(VIA_DRILL)); v.SetWidth(mm(VIA_D))
    v.SetViaType(pcbnew.VIATYPE_THROUGH); v.SetNet(net(netname))
    board.Add(v)
    for li in range(2):
        shapes.append(Shape(li, netname, seg=(xy, xy), hw=VIA_D / 2))


def emit_path(netname, path, w, start_xy, end_xy):
    """Turn a cell path into tracks/vias; snap the ends onto the pad centres."""
    pts = [(li, i * GRID, j * GRID) for li, i, j in path]
    if start_xy:
        pts.insert(0, (pts[0][0], *start_xy))
    if end_xy:
        pts.append((pts[-1][0], *end_xy))
    # split at layer changes, merge collinear runs
    run = [pts[0]]
    def flush(run):
        if len(run) < 2:
            return
        verts = [run[0]]
        for k in range(1, len(run) - 1):
            a, b, c = verts[-1], run[k], run[k + 1]
            cross = (b[1] - a[1]) * (c[2] - b[2]) - (b[2] - a[2]) * (c[1] - b[1])
            if abs(cross) > 1e-9:
                verts.append(b)
        verts.append(run[-1])
        for a, b in zip(verts, verts[1:]):
            if (a[1], a[2]) != (b[1], b[2]):
                add_track(netname, a[0], (a[1], a[2]), (b[1], b[2]), w)
    for p in pts[1:]:
        if p[0] != run[-1][0]:
            flush(run)
            add_via(netname, (run[-1][1], run[-1][2]))
            run = [p]
        else:
            run.append(p)
    flush(run)


def pad_center(p):
    c = p.GetPosition()
    return (pcbnew.ToMM(c.x), pcbnew.ToMM(c.y))


# Reserve the short decoupler/receiver ground returns before signal routing.
# These pads connect through their vias and the back GND pour; DRC verifies it.
gnd_returns = {}
for ref, (dx, dy) in GND_STITCH.items():
    pad = next(p for p in footprints[ref].Pads() if p.GetNetname() == "GND")
    x, y, _ = PLACE[ref]
    via_xy = (x + dx, y + dy)
    add_track("GND", 0, pad_center(pad), via_xy, 0.25)
    add_via("GND", via_xy)
    gnd_returns[ref] = via_xy

failed = []
for netname in ORDER:
    pads = pad_cells.get(netname, [])
    if netname == "GND":
        pads = [pc for pc in pads if pc[0].GetParentFootprint().GetReference() not in GND_STITCH]
    if len(pads) < 2:
        continue
    w = WIDTH.get(netname, SIGNAL_W)
    tree_pads = [pads[0]]
    tree_cells = set(pads[0][1])
    todo = pads[1:]
    while todo:
        # connect the pad nearest to the tree first
        def dist(pc):
            cx, cy = pad_center(pc[0])
            return min(math.hypot(cx - pad_center(tp[0])[0], cy - pad_center(tp[0])[1]) for tp in tree_pads)
        todo.sort(key=dist)
        target = todo.pop(0)
        blk = blocked_grid(netname, w / 2)
        blk_via = blocked_grid(netname, VIA_D / 2)
        # no vias inside pads of this net either (keeps solder off the via)
        for tp, cells in pads:
            for li, i, j in cells:
                for b in blk_via:
                    b[i * NY + j] = 1
        tc = pad_center(target[0])
        path = astar(tree_cells, set(target[1]), blk, blk_via, (tc[0] / GRID, tc[1] / GRID))
        if not path:
            failed.append((netname, target[0].GetParentFootprint().GetReference(), target[0].GetNumber()))
            continue
        # snap: start onto the source pad centre if the path starts inside a pad
        start_xy = None
        for tp, cells in tree_pads:
            if path[0] in cells:
                start_xy = pad_center(tp)
        emit_path(netname, path, w, start_xy, tc)
        for li, i, j in path:
            tree_cells.add((li, i, j))
        tree_cells |= target[1]
        tree_pads.append(target)
    print(f"routed {netname}")

# ---------------------------------------------------------------- GND pour, text
for L in LAYERS:
    z = pcbnew.ZONE(board)
    z.SetLayer(L)
    z.SetNet(net("GND"))
    o = z.Outline()
    o.NewOutline()
    for x, y in [(0, 0), (BOARD_W, 0), (BOARD_W, BOARD_H), (0, BOARD_H)]:
        o.Append(mm(x), mm(y))
    z.SetLocalClearance(mm(0.3))
    z.SetMinThickness(mm(0.25))
    z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
    z.SetThermalReliefGap(mm(0.3))
    z.SetThermalReliefSpokeWidth(mm(0.4))
    board.Add(z)

def text(s, x, y, size=0.8, layer=pcbnew.F_SilkS, just=None):
    t = pcbnew.PCB_TEXT(board)
    t.SetText(s); t.SetPosition(V(x, y)); t.SetLayer(layer)
    t.SetTextSize(V(size, size)); t.SetTextThickness(mm(size * 0.15))
    if layer == pcbnew.B_SilkS:
        t.SetMirrored(True)
    board.Add(t)

def line(x1, y1, x2, y2, layer=pcbnew.F_SilkS):
    s = pcbnew.PCB_SHAPE(board)
    s.SetShape(pcbnew.SHAPE_T_SEGMENT)
    s.SetStart(V(x1, y1)); s.SetEnd(V(x2, y2))
    s.SetLayer(layer); s.SetWidth(mm(0.12))
    board.Add(s)

# XIAO outline (left edge only: the pads cover the long edges) and J1 pin labels
line(37.9, 6.5, 37.9, 17.5)
text("USB-C", 54.0, 16.0, 0.8)
for k, lbl in enumerate(["16", "4", "5", "7"]):
    text(lbl, 4.7, 19.5 - 2.54 * k, 0.8)
text("OBD-II pins: 16 = +12V, 4/5 = GND, 7 = K-line", 29, 1.6, 0.8, pcbnew.B_SilkS)
text("K-LINE DONGLE v3", 29, 22.6, 1.0, pcbnew.B_SilkS)
text("ESP32-VAG-Scanner", 29, 21.0, 0.8, pcbnew.B_SilkS)

# Match the schematic's net names: labels are sheet-local ("/VBAT"), and each
# unused pin gets its own "unconnected-(REF-PIN-PadN)" net.
for name, n in nets.items():
    n.SetNetname("/" + name)
for ref, sym, val, fp, pins, _, _ in PARTS:
    if not fp:
        continue
    names = {num: nm for num, nm, _ in SYMBOLS[sym].get("pins", [])}
    for num, n in pins.items():
        if n is None:
            nc = net(f"unconnected-({ref}-{names[num].replace('/', '{slash}')}-Pad{num})")
            for p in footprints[ref].Pads():
                if p.GetNumber() == num:
                    p.SetNet(nc)

if failed and os.environ.get("KLINE_DEBUG_PCB"):
    board.Save(os.environ["KLINE_DEBUG_PCB"])  # partial route, for inspection only
if failed:
    raise SystemExit(f"Unrouted pads: {failed}; existing PCB has not been overwritten")

filler = pcbnew.ZONE_FILLER(board)
filler.Fill(board.Zones())
# Keep the native extension and basename so KiCad's companion project files
# remain inside the temporary directory, never beside the real project.
with tempfile.TemporaryDirectory(prefix=".route-", dir=HERE) as staging:
    staged = os.path.join(staging, os.path.basename(OUT))
    board.Save(staged)
    os.replace(staged, OUT)
print("saved", os.path.abspath(OUT))

