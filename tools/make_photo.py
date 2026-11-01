#!/usr/bin/env python3
"""Usage: make_photo.py photos/bild.jpg [focus_x focus_y]
Cover-crops to 240x280 (focus 0..1, default 0.5 0.4 so faces stay up) and writes src/photo.h (RGB565)."""
import sys
from PIL import Image, ImageOps
W, H = 240, 280
src = sys.argv[1]
fx = float(sys.argv[2]) if len(sys.argv) > 2 else 0.5
fy = float(sys.argv[3]) if len(sys.argv) > 3 else 0.4
im = ImageOps.exif_transpose(Image.open(src)).convert("RGB")
s = max(W / im.width, H / im.height)
im = im.resize((round(im.width * s), round(im.height * s)), Image.LANCZOS)
l = int((im.width - W) * fx); t = int((im.height - H) * fy)
im = im.crop((l, t, l + W, t + H))
im.save("tools/photo_preview.png")
px = list(im.getdata())
vals = [((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3) for r, g, b in px]
with open("src/photo.h", "w") as f:
    f.write("#pragma once\n#include <Arduino.h>\n#define HAS_PHOTO 1\nstatic const uint16_t PHOTO[%d] PROGMEM = {\n" % len(vals))
    for i in range(0, len(vals), 16):
        f.write(",".join("0x%04X" % v for v in vals[i:i+16]) + ",\n")
    f.write("};\n")
print("wrote src/photo.h, preview in tools/photo_preview.png")
