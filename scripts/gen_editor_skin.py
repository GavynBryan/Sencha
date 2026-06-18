#!/usr/bin/env python3
"""Procedurally generate the editor's 9-slice skin PNGs.

This is an OFFLINE dev tool, not part of the build or runtime. It bootstraps the
texture-skin look the runtime loads from editor/skin/*.png; hand-authored or
AI-generated art can replace these files later with no code change. The runtime
9-slice insets in EditorSkin must match the insets baked here.

Run:  python3 scripts/gen_editor_skin.py
Deps: Pillow + numpy.
"""

import os
import numpy as np
from PIL import Image

OUT_DIR = os.path.join(os.path.dirname(__file__), "..", "editor", "skin")

# Skin palette (sRGB 0-255), kept in step with editor/ui/EditorUiStyle.h.
GUNMETAL_HI = np.array([46, 62, 74])    # raised metal, top of a gradient
GUNMETAL_LO = np.array([14, 20, 26])    # recessed metal, bottom
INTERIOR_HI = np.array([20, 27, 34])    # panel interior, top
INTERIOR_LO = np.array([10, 14, 19])    # panel interior, bottom
BEVEL_LIGHT = np.array([92, 116, 132])  # top/left highlight
BEVEL_DARK  = np.array([6, 9, 12])      # bottom/right shadow
CYAN_LIP    = np.array([43, 190, 216])  # signature accent edge

rng = np.random.default_rng(0x5E11CA)  # deterministic noise


def vgrad(h, w, top, bot):
    """Vertical gradient RGB array of shape (h, w, 3), top->bot."""
    t = np.linspace(0.0, 1.0, h)[:, None]                  # (h, 1)
    col = top[None, :] * (1.0 - t) + bot[None, :] * t      # (h, 3)
    return np.repeat(col[:, None, :], w, axis=1)           # (h, w, 3)


def brushed(rgb, amount=6.0):
    """Add subtle horizontal brushed-metal noise."""
    h, w = rgb.shape[:2]
    streak = rng.normal(0.0, amount, size=(h, 1, 1))   # per-row
    grain = rng.normal(0.0, amount * 0.4, size=(h, w, 1))
    return rgb + streak + grain


def bevel(rgb, light=BEVEL_LIGHT, dark=BEVEL_DARK, px=2):
    """Raised bevel: light top/left edges, dark bottom/right edges."""
    out = rgb.copy()
    for i in range(px):
        a = 1.0 - i / max(px, 1) * 0.5
        out[i, :, :]      = light * a + out[i, :, :] * (1 - a)        # top
        out[:, i, :]      = light * a + out[:, i, :] * (1 - a)        # left
        out[-1 - i, :, :] = dark * a + out[-1 - i, :, :] * (1 - a)    # bottom
        out[:, -1 - i, :] = dark * a + out[:, -1 - i, :] * (1 - a)    # right
    return out


def save(name, rgb, alpha=255):
    rgb = np.clip(rgb, 0, 255).astype(np.uint8)
    h, w = rgb.shape[:2]
    a = np.full((h, w, 1), alpha, dtype=np.uint8) if np.isscalar(alpha) else alpha
    img = Image.fromarray(np.concatenate([rgb, a], axis=2))  # RGBA inferred
    os.makedirs(OUT_DIR, exist_ok=True)
    path = os.path.join(OUT_DIR, name)
    img.save(path)
    print(f"  wrote {os.path.relpath(path)}  ({w}x{h})")


def gen_frame():
    """48x48, 12px inset. Beveled metal panel frame; center = dark interior."""
    n = 48
    rgb = brushed(vgrad(n, n, INTERIOR_HI, INTERIOR_LO), 4.0)
    rgb = bevel(rgb, px=2)
    # Thin cyan inner lip framing all four sides, just inside the bevel.
    rgb[3, 3:-3, :] = CYAN_LIP
    rgb[-4, 3:-3, :] = CYAN_LIP * 0.65 + rgb[-4, 3:-3, :] * 0.35
    rgb[3:-3, 3, :] = CYAN_LIP * 0.85 + rgb[3:-3, 3, :] * 0.15
    rgb[3:-3, -4, :] = CYAN_LIP * 0.65 + rgb[3:-3, -4, :] * 0.35
    save("frame.png", rgb)


def gen_button():
    """32x32, 8px inset. Convex neutral metal button (tinted per-state at draw)."""
    n = 32
    rgb = brushed(vgrad(n, n, GUNMETAL_HI, GUNMETAL_LO), 4.0)
    rgb = bevel(rgb, px=2)
    save("button.png", rgb)


def gen_band():
    """24x24, 6px inset. Horizontal metal band; top highlight + cyan bottom lip."""
    n = 24
    rgb = brushed(vgrad(n, n, GUNMETAL_HI * 1.05, GUNMETAL_LO), 3.0)
    rgb[0, :, :] = BEVEL_LIGHT            # top highlight
    rgb[-1, :, :] = CYAN_LIP             # bottom accent lip
    rgb[-2, :, :] = CYAN_LIP * 0.4 + rgb[-2, :, :] * 0.6
    save("band.png", rgb)


def main():
    print("Generating editor skin into", os.path.relpath(OUT_DIR))
    gen_frame()
    gen_button()
    gen_band()
    print("done.")


if __name__ == "__main__":
    main()
