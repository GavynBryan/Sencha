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

# Skin palette (sRGB 0-255) — EXACTLY the user's editor palette (EditorUiStyle.h).
PANEL_BG    = np.array([10, 13, 15])    # #0A0D0F  window/child
FRAME_BG    = np.array([13, 18, 20])    # #0D1214  panel interior / inset
HEADER_BG   = np.array([15, 26, 28])    # #0F1A1C  title / band
BUTTON_BG   = np.array([17, 32, 34])    # #112022  button face
BORDER      = np.array([28, 58, 58])    # #1C3A3A  steel border
TEAL        = np.array([0, 229, 204])   # #00E5CC  primary accent (glow)
TEAL_DIM    = np.array([0, 90, 80])     # dimmed teal for the soft edge glow

INTERIOR_HI = FRAME_BG + 4              # subtle interior gradient
INTERIOR_LO = FRAME_BG - 3

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


def edge_rect(rgb, inset, color):
    """Draw a 1px rectangle outline at the given inset from the edges."""
    rgb[inset, inset:-inset, :] = color
    rgb[-1 - inset, inset:-inset, :] = color
    rgb[inset:-inset, inset, :] = color
    rgb[inset:-inset, -1 - inset, :] = color


def corner_brackets(rgb, color, length=10, edge=2, thick=2):
    """Glowing L-shaped accent brackets in each corner (the mockup's panel detail)."""
    e, t, L = edge, thick, length
    # top-left
    rgb[e:e + t, e:e + L, :] = color;        rgb[e:e + L, e:e + t, :] = color
    # top-right
    rgb[e:e + t, -e - L:-e, :] = color;      rgb[e:e + L, -e - t:-e, :] = color
    # bottom-left
    rgb[-e - t:-e, e:e + L, :] = color;      rgb[-e - L:-e, e:e + t, :] = color
    # bottom-right
    rgb[-e - t:-e, -e - L:-e, :] = color;    rgb[-e - L:-e, -e - t:-e, :] = color


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
    """48x48, 12px inset. Dark teal-black panel: steel border, soft teal edge glow,
    and bright teal corner brackets — the mockup's sci-fi panel look."""
    n = 48
    rgb = brushed(vgrad(n, n, INTERIOR_HI, INTERIOR_LO), 3.0)
    edge_rect(rgb, 0, BORDER)        # 1px steel outer border
    edge_rect(rgb, 2, TEAL_DIM)      # soft teal halo
    edge_rect(rgb, 3, TEAL)          # crisp teal edge line
    corner_brackets(rgb, TEAL, length=10, edge=2, thick=2)
    save("frame.png", rgb)


def gen_button():
    """32x32, 8px inset. Flat dark button face (tinted teal per-state at draw)."""
    n = 32
    rgb = brushed(vgrad(n, n, BUTTON_BG + 6, BUTTON_BG - 4), 2.0)
    edge_rect(rgb, 0, BORDER)        # thin steel outline
    save("button.png", rgb)


def gen_band():
    """24x24, 6px inset. Header-toned band: top highlight + teal bottom lip."""
    n = 24
    rgb = brushed(vgrad(n, n, HEADER_BG + 6, HEADER_BG - 3), 2.0)
    rgb[0, :, :] = HEADER_BG + 26    # top highlight
    rgb[-1, :, :] = TEAL             # teal bottom lip
    rgb[-2, :, :] = TEAL_DIM
    save("band.png", rgb)


def main():
    print("Generating editor skin into", os.path.relpath(OUT_DIR))
    gen_frame()
    gen_button()
    gen_band()
    print("done.")


if __name__ == "__main__":
    main()
