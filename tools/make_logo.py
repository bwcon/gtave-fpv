"""Turn a black-on-white logo into an OSD boot logo: white art with a black
outline on a transparent background, the way OSD glyphs look.

    python tools/make_logo.py logo.jpg assets/GTAVE.FPV.logo.png
"""
import sys

import numpy as np
from PIL import Image, ImageFilter

WIDTH = 1000    # pixels; the logo is drawn about 0.4 of the screen height
OUTLINE = 3     # outline width in pixels at WIDTH

src, dst = sys.argv[1], sys.argv[2]
img = Image.open(src).convert("L")
img = img.resize((WIDTH, round(img.height * WIDTH / img.width)), Image.LANCZOS)
ink = 255 - np.asarray(img, dtype=np.float32)              # dark lines become ink
ink = np.clip((ink - 40) * 255 / 175, 0, 255)               # drop JPEG noise in the paper
art = Image.fromarray(ink.astype(np.uint8))
outline = art.filter(ImageFilter.MaxFilter(OUTLINE * 2 + 1))
rgba = np.zeros((img.height, WIDTH, 4), np.uint8)
a_art = np.asarray(art, np.float32) / 255
rgba[..., :3] = (a_art * 255).astype(np.uint8)[..., None]   # white over black
rgba[..., 3] = np.asarray(outline)
Image.fromarray(rgba, "RGBA").save(dst, optimize=True)
print(f"{dst}: {WIDTH}x{img.height}")
