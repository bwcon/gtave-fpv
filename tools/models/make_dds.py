"""An image as an uncompressed 32-bit DDS (A8R8G8B8) with mipmaps, for Sollumz to embed.

    python -I tools/models/make_dds.py <image> <size> <out.dds> [brightness]

brightness scales the colours (0.35: the goggles' grey texture to the Avata's
dark grey body). "r,g,b" scales each channel, to tint (3,0.5,0.45: red goggles).
"""
import struct
import sys

from PIL import Image

src, size, out = sys.argv[1], int(sys.argv[2]), sys.argv[3]
scale = [float(v) for v in sys.argv[4].split(",")] if len(sys.argv) > 4 else [1.0]
scale = scale * 3 if len(scale) == 1 else scale
img = Image.open(src).convert("RGBA").resize((size, size), Image.LANCZOS)
if scale != [1.0] * 3:
    *rgb, a = img.split()
    rgb = [c.point(lambda v, k=k: min(255, round(v * k))) for c, k in zip(rgb, scale)]
    img = Image.merge("RGBA", (*rgb, a))
levels = []
while True:
    r, g, b, a = img.split()
    levels.append(Image.merge("RGBA", (b, g, r, a)).tobytes())  # B G R A in memory
    if img.width == 1:
        break
    img = img.resize((max(1, img.width // 2), max(1, img.height // 2)), Image.LANCZOS)
flags = 0x1 | 0x2 | 0x4 | 0x8 | 0x1000 | 0x20000  # caps, height, width, pitch, pixel format, mipmap count
header = struct.pack("<4s7I44x", b"DDS ", 124, flags, size, size, size * 4, 0, len(levels))
pf = struct.pack("<8I", 32, 0x41, 0, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
caps = struct.pack("<5I", 0x1000 | 0x8 | 0x400000, 0, 0, 0, 0)  # texture, complex, mipmap
with open(out, "wb") as f:
    f.write(header + pf + caps + b"".join(levels))
print(out, size, "levels", len(levels))
