"""Build the app's hero images from assets/source (see fetch.py).

Outputs:
  out/p4/hero_<name>.rgb565   800x640 RGB565 little-endian, faded into the
                              background at the bottom (text sits on top)
  out/p4/hero_<name>.png      preview of the same
  mac/Sources/ESP32VAGScanner/Resources/hero_<name>.jpg
                              1800x760 banner, car on the right fading in from the left

Run with a Python that has Pillow:  python make_assets.py
"""
import os
import shutil
from PIL import Image, ImageDraw, ImageFilter, ImageFont, ImageOps

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "source")
OUT = os.path.join(HERE, "..", "out")

BG = (13, 16, 20)          # app background (#0D1014)
SURFACE = (22, 27, 34)     # card surface (#161B22)
TEXT = (232, 236, 241)
MUTED = (139, 149, 163)
ACCENT = (61, 139, 255)

# Per-photo framing: horizontal/vertical centre of the car (0-1) and how much
# of the image height the crop keeps (smaller = tighter).
FRAMING = {
    "eurovan": (0.47, 0.50, 0.92),
    "harlequin": (0.50, 0.58, 0.62),
    "golf1": (0.50, 0.55, 0.90),
    "golf2": (0.50, 0.52, 0.92),
    "golf3": (0.52, 0.55, 0.88),
    "golf4": (0.50, 0.55, 0.88),
    "golf5": (0.50, 0.55, 0.90),
    "golf6": (0.52, 0.56, 0.86),
    "golf7": (0.47, 0.60, 0.70),
    "golf8": (0.50, 0.58, 0.85),
}
MIRROR = {"golf8"}
GENERATIONS = [("Mk1", "1974"), ("Mk2", "1983"), ("Mk3", "1991"), ("Mk4", "1997"),
               ("Mk5", "2003"), ("Mk6", "2008"), ("Mk7", "2012"), ("Mk8", "2019")]


def font(size, bold=False):
    path = "/System/Library/Fonts/HelveticaNeue.ttc"
    return ImageFont.truetype(path, size, index=1 if bold else 0)


def load(name):
    im = ImageOps.exif_transpose(Image.open(os.path.join(SRC, name + ".jpg"))).convert("RGB")
    return ImageOps.mirror(im) if name in MIRROR else im


def frame(name, w, h):
    """Crops `name` to w:h around its framing point and resizes to w x h."""
    im = load(name)
    cx, cy, keep = FRAMING[name]
    ch = im.height * keep
    cw = ch * w / h
    if cw > im.width:  # too wide for the photo: keep full width instead
        cw = im.width
        ch = cw * h / w
    x0 = min(max(cx * im.width - cw / 2, 0), im.width - cw)
    y0 = min(max(cy * im.height - ch / 2, 0), im.height - ch)
    return im.crop((int(x0), int(y0), int(x0 + cw), int(y0 + ch))).resize((w, h), Image.LANCZOS)


def fade(im, start, top=0.0):
    """Fades the bottom (from `start`, 0-1) into BG; optionally darkens the top edge."""
    w, h = im.size
    mask = Image.new("L", (1, h))
    for y in range(h):
        t = y / (h - 1)
        a = 0.0
        if t > start:
            u = (t - start) / (1 - start)
            a = u * u * (3 - 2 * u)  # smoothstep
        if top and t < 0.18:
            a = max(a, top * (1 - t / 0.18))
        mask.putpixel((0, y), int(a * 255))
    return Image.composite(Image.new("RGB", im.size, BG), im, mask.resize(im.size))


def edge_mask(size, top=0.0, bottom=0.0, left=0.0):
    """Alpha mask that is opaque in the middle and smoothly clear over the given edge fractions."""
    w, h = size
    col = Image.new("L", (1, h))
    for y in range(h):
        t = y / (h - 1)
        a = 1.0
        if top and t < top:
            a = min(a, t / top)
        if bottom and t > 1 - bottom:
            a = min(a, (1 - t) / bottom)
        col.putpixel((0, y), int(255 * a * a * (3 - 2 * a)))
    mask = col.resize(size)
    if left:
        row = Image.new("L", (w, 1))
        for x in range(w):
            u = min(1.0, x / (w * left))
            row.putpixel((x, 0), int(255 * u * u * (3 - 2 * u)))
        mask = Image.composite(mask, Image.new("L", size, 0), row.resize(size))
    return mask


def showcase_portrait(name, w, h, zoom=1.06, centre_y=0.46):
    """Whole car across the width, edges dissolving into BG (P4 hero)."""
    im = load(name)
    cx = FRAMING[name][0]
    pw = int(w * zoom)
    ph = int(im.height * pw / im.width)
    photo = im.resize((pw, ph), Image.LANCZOS)
    x0 = int(min(max(cx * pw - w / 2, 0), pw - w))
    photo = photo.crop((x0, 0, x0 + w, ph))
    canvas = Image.new("RGB", (w, h), BG)
    canvas.paste(photo, (0, int(h * centre_y - ph / 2)), edge_mask(photo.size, top=0.22, bottom=0.30))
    return canvas


def showcase_banner(name, w, h):
    """Car on the right, its left edge dissolving into BG for the title (Mac hero)."""
    im = load(name)
    _, cy, keep = FRAMING[name]
    ch = im.height * keep  # trim sky/ground so the car fills the banner height
    y0 = min(max(cy * im.height - ch / 2, 0), im.height - ch)
    im = im.crop((0, int(y0), im.width, int(y0 + ch)))
    ph = h
    pw = min(int(im.width * ph / im.height), w)
    photo = im.resize((pw, ph), Image.LANCZOS)
    canvas = Image.new("RGB", (w, h), BG)
    canvas.paste(photo, (w - pw, 0), edge_mask(photo.size, bottom=0.12, left=0.16))
    return canvas


def rounded(im, r):
    m = Image.new("L", im.size, 0)
    ImageDraw.Draw(m).rounded_rectangle((0, 0, im.width - 1, im.height - 1), r, fill=255)
    out = Image.new("RGBA", im.size)
    out.paste(im, (0, 0), m)
    return out


def family(w, h, cols, tile_w, tile_h, top, gap_x, gap_y, label_size):
    """Mk1-Mk8 timeline: rows of photos joined by a line with a dot per generation."""
    im = Image.new("RGB", (w, h), BG)
    dr = ImageDraw.Draw(im)
    rows = (len(GENERATIONS) + cols - 1) // cols
    left = (w - cols * tile_w - (cols - 1) * gap_x) // 2
    bold, regular = font(label_size, True), font(int(label_size * 0.8))
    for i, (mk, year) in enumerate(GENERATIONS):
        r, c = divmod(i, cols)
        x = left + c * (tile_w + gap_x)
        y = top + r * (tile_h + gap_y)
        # timeline rail under each row, dots under each tile
        rail_y = y + tile_h + int(label_size * 0.9)
        if c == 0:
            dr.line((left, rail_y, left + cols * tile_w + (cols - 1) * gap_x, rail_y), fill=(48, 56, 66), width=2)
        dr.ellipse((x + tile_w // 2 - 5, rail_y - 5, x + tile_w // 2 + 5, rail_y + 5), fill=ACCENT)
        photo = rounded(frame(f"golf{i + 1}", tile_w, tile_h), 14)
        im.paste(photo, (x, y), photo)
        ty = rail_y + 12
        tw = dr.textlength(mk, font=bold)
        dr.text((x + (tile_w - tw) / 2, ty), mk, font=bold, fill=TEXT)
        yw = dr.textlength(year, font=regular)
        dr.text((x + (tile_w - yw) / 2, ty + label_size + 4), year, font=regular, fill=MUTED)
    return im


def save_rgb565(im, path):
    data = bytearray()
    for r, g, b in im.getdata():
        v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        data += bytes((v & 0xFF, v >> 8))
    open(path, "wb").write(data)


def dither565(im):
    """Light ordered dither so the fade doesn't band once quantised to RGB565."""
    bayer = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]
    px = im.load()
    for y in range(im.height):
        for x in range(im.width):
            d = bayer[y & 3][x & 3] / 16.0 - 0.5
            r, g, b = px[x, y]
            px[x, y] = (min(255, max(0, int(r + d * 8))), min(255, max(0, int(g + d * 4))),
                        min(255, max(0, int(b + d * 8))))
    return im


def main():
    p4 = os.path.join(OUT, "p4")
    mac = os.path.join(HERE, "..", "..", "mac", "Sources", "ESP32VAGScanner", "Resources")
    os.makedirs(p4, exist_ok=True)
    os.makedirs(mac, exist_ok=True)

    heroes = {
        "eurovan": showcase_portrait("eurovan", 800, 640, zoom=1.0),
        "harlequin": showcase_portrait("harlequin", 800, 640, zoom=1.0),
        "family": fade(family(800, 640, 4, 172, 116, 78, 16, 92, 22), 0.80),
    }
    for name, im in heroes.items():
        im.save(os.path.join(p4, f"hero_{name}.png"))
        save_rgb565(dither565(im.copy()), os.path.join(p4, f"hero_{name}.rgb565"))

    showcase_banner("eurovan", 1800, 760).save(os.path.join(mac, "hero_eurovan.jpg"), quality=88)
    showcase_banner("harlequin", 1800, 760).save(os.path.join(mac, "hero_harlequin.jpg"), quality=88)
    # timeline in the top half: the title sits over the bottom of the banner
    family(1800, 760, 8, 196, 132, 60, 18, 0, 26).save(os.path.join(mac, "hero_family.jpg"), quality=90)
    shutil.copy(os.path.join(HERE, "..", "CREDITS.md"), os.path.join(mac, "CREDITS.md"))
    shutil.copy(os.path.join(HERE, "..", "labels.csv"), os.path.join(mac, "labels.csv"))
    print("done")


if __name__ == "__main__":
    main()
