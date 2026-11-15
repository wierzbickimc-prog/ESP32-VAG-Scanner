"""Resize existing 800x640 RGB565 heroes for a smaller P4 board, without Pillow.

Usage: python resize_p4_heroes.py INPUT_DIR OUTPUT_DIR WIDTH
Generated files keep their names so ESP-IDF's embedded symbols stay the same.
"""
from array import array
from pathlib import Path
import sys


def resize(raw, width):
    source_w, source_h = 800, 640
    if len(raw) != source_w * source_h * 2:
        raise ValueError("expected an 800x640 RGB565 hero")
    source = array("H")
    source.frombytes(raw)
    if sys.byteorder != "little":
        source.byteswap()
    height = width * source_h // source_w
    output = array("H")
    for y in range(height):
        sy = y * (source_h - 1) / max(1, height - 1)
        y0 = int(sy)
        y1 = min(y0 + 1, source_h - 1)
        fy = sy - y0
        for x in range(width):
            sx = x * (source_w - 1) / (width - 1)
            x0 = int(sx)
            x1 = min(x0 + 1, source_w - 1)
            fx = sx - x0
            pixels = (source[y0 * source_w + x0], source[y0 * source_w + x1],
                      source[y1 * source_w + x0], source[y1 * source_w + x1])
            weights = ((1 - fx) * (1 - fy), fx * (1 - fy), (1 - fx) * fy, fx * fy)
            value = 0
            for shift, mask in ((11, 31), (5, 63), (0, 31)):
                channel = round(sum(((p >> shift) & mask) * w for p, w in zip(pixels, weights)))
                value |= channel << shift
            output.append(value)
    if sys.byteorder != "little":
        output.byteswap()
    return output.tobytes()


def main():
    source, destination = map(Path, sys.argv[1:3])
    width = int(sys.argv[3])
    if not 2 <= width <= 800:
        raise ValueError("width must be between 2 and 800")
    destination.mkdir(parents=True, exist_ok=True)
    for name in ("eurovan", "harlequin", "family"):
        filename = f"hero_{name}.rgb565"
        data = resize((source / filename).read_bytes(), width)
        path = destination / filename
        if not path.exists() or path.read_bytes() != data:
            path.write_bytes(data)
    print(f"Prepared {width}x{width * 640 // 800} RGB565 heroes")


if __name__ == "__main__":
    main()
