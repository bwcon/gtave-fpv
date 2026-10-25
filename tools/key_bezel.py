"""Turns a green-screen goggles photo into the bezel overlay.

Usage: python -I tools/key_bezel.py <input.jpg> <output.png>

The green screen becomes transparent with a soft edge, green spill on the
edge pixels is removed, and the opening's bounding box is printed as window
fractions for [View] BezelRect in GTAVE.FPV.ini: the FPV picture, the lens
and the OSD are fitted into that box, and the bezel is drawn over it.
"""
import sys

import numpy as np
from PIL import Image


def main(src, dst):
    rgb = np.asarray(Image.open(src).convert("RGB")).astype(np.float32) / 255.0
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    # How much greener than the other channels: high on the screen, ~0 on the shell.
    green = g - np.maximum(r, b)
    lo, hi = 0.10, 0.30  # below lo opaque, above hi transparent; JPEG noise sits under lo
    alpha = 1.0 - np.clip((green - lo) / (hi - lo), 0.0, 1.0)

    # Despill: no channel of the edge may stay greener than the others.
    g_out = np.minimum(g, np.maximum(r, b))
    out = np.dstack([r, g_out, b, alpha])
    Image.fromarray((out * 255.0 + 0.5).astype(np.uint8), "RGBA").save(dst, optimize=True)

    hole = alpha < 0.5
    ys, xs = np.nonzero(hole)
    h, w = hole.shape
    x0, x1 = xs.min() / w, (xs.max() + 1) / w
    y0, y1 = ys.min() / h, (ys.max() + 1) / h
    print(f"{w}x{h}, opening {hole.mean() * 100:.1f}% of the image")
    print(f"BezelRect={x0:.4f},{y0:.4f},{x1:.4f},{y1:.4f}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
