"""Drive the P4's debug console and save screenshots.

  python p4shot.py PORT STEP [STEP...]

Each STEP is a console command ("demo eurovan", "open 0", "faults", ...),
"wait:SECONDS", or "shot:FILE.png". The board is reset first and the script
waits for it to boot. Dialogs (top layer) don't appear in shots. Needs pyserial
and Pillow.
"""
import base64
import sys
import time

import serial
from PIL import Image

BAUD = 115200


def read_shot(port, timeout=150):
    deadline = time.time() + timeout
    header = None
    rows = []
    buf = b""
    while time.time() < deadline:
        buf += port.read(65536)
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            line = line.strip()
            if header is None:
                if line.startswith(b"SHOT "):
                    if line == b"SHOT ERR":
                        raise RuntimeError("device could not take a snapshot")
                    header = [int(v) for v in line.split()[1:4]]
            elif line == b"ENDSHOT":
                w, h, stride = header
                raw = base64.b64decode(b"".join(rows))
                return w, h, stride, raw
            elif len(line) % 4 == 0 and line.strip(b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=") == b"":
                rows.append(line)  # anything else is a log line that slipped in
    raise TimeoutError("no screenshot received")


def to_png(w, h, stride, raw, path):
    im = Image.new("RGB", (w, h))
    px = im.load()
    for y in range(h):
        base = y * stride
        for x in range(w):
            v = raw[base + 2 * x] | raw[base + 2 * x + 1] << 8
            px[x, y] = ((v >> 8) & 0xF8, (v >> 3) & 0xFC, (v << 3) & 0xF8)
    im.save(path)


def main():
    port = serial.Serial(sys.argv[1], BAUD, timeout=0.2)
    # reset the board so every run starts from a clean boot
    port.dtr = False
    port.rts = True
    time.sleep(0.1)
    port.rts = False
    time.sleep(6)
    port.reset_input_buffer()
    for step in sys.argv[2:]:
        if step.startswith("wait:"):
            time.sleep(float(step[5:]))
        elif step.startswith("shot:"):
            port.reset_input_buffer()
            port.write(b"shot\n")
            w, h, stride, raw = read_shot(port)
            to_png(w, h, stride, raw, step[5:])
            print("saved", step[5:], f"{w}x{h}")
        else:
            port.write(step.encode() + b"\n")
            time.sleep(0.5)


if __name__ == "__main__":
    main()
