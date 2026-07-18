#!/usr/bin/env python3
"""Generate a cooked light-stress scene: a floor of tiled static cells plus N
small static point lights, for measuring the forward renderer's per-frame light
cost. Geometry is constant across N; only the light count changes.

The mesh/material are an existing cooked asset (reused so the scene loads with
no new cook), referenced by global asset:// path. Lights are small-range accent
lights whose pools overlap on the floor (the many-small-lights regime).

Usage: gen_light_stress_scene.py <N> <out.cooked.json>
"""

import json
import math
import sys

# Known-good cooked mesh + material from levels/test (resolve by asset:// path).
MESH = {"id": "5b4527db7d5a5819", "path": "asset://levels/test/cell_-1_0_0.smesh"}
MATERIAL = {"id": "3654820ec38aab53", "path": "asset://materials/dev/gray.smat"}

# Accent light palette (linear rgb), cycled deterministically.
PALETTE = [
    [0.05, 0.10, 1.00],   # blue
    [0.99, 0.65, 0.22],   # amber
    [0.55, 0.15, 0.95],   # purple
    [0.10, 0.85, 0.95],   # cyan
]

# Rig: the SceneViewer default (static) camera sits at (0, 3, 10) looking down
# -Z (run with sceneviewer.camera.scripted 0, no input, so the framing is fixed
# and the run is deterministic). Geometry and lights are placed in front of it.
LIGHT_RANGE = 3.0
LIGHT_INTENSITY = 10.0
# Lights fill a slab in the near frustum ahead of the camera so all N stay
# visible (LightsVisible == N) and their range-3 pools overlap heavily on the
# floor (the many-small-lights worst case).
LIGHT_X_HALF = 5.0
LIGHT_Y_MIN = 0.5
LIGHT_Y_MAX = 4.0
LIGHT_Z_NEAR = 4.0         # just ahead of the camera
LIGHT_Z_FAR = -14.0        # receding into the view
# Floor tiles: a long grid receding from the camera to fill the screen. The cell
# is a floor chunk with an off-center pivot; scaling + tiling covers the frustum.
FLOOR_COLS = 5             # across (x)
FLOOR_ROWS = 5             # depth (z)
TILE_STRIDE = 20.0
TILE_SCALE = 3.0
FLOOR_Z0 = 6.0             # nearest row just ahead of the camera


def transform(pos, scale=(1, 1, 1)):
    return {"local": {"position": list(pos),
                      "rotation": [0, 0, 0, 1],
                      "scale": list(scale)}}


def floor_entities():
    ents = []
    xhalf = (FLOOR_COLS - 1) / 2.0
    for i in range(FLOOR_COLS):
        for j in range(FLOOR_ROWS):
            x = (i - xhalf) * TILE_STRIDE
            z = FLOOR_Z0 - j * TILE_STRIDE      # recede away from the camera
            ents.append({"components": {
                "Transform": transform((x, 0, z),
                                       (TILE_SCALE, TILE_SCALE, TILE_SCALE)),
                "StaticMesh": {
                    "mesh": dict(MESH),
                    "materials": [dict(MATERIAL)],
                    "visible": True,
                    "layer_mask": 4294967295,
                    "section_mask": 4294967295,
                },
            }})
    return ents


def light_entities(n):
    # Fill a slab in the near frustum with N lights on a near-cube grid so all
    # stay visible and their small pools overlap. Deterministic in the index.
    cols = max(1, int(round(math.sqrt(n))))
    rows = math.ceil(n / cols)
    ents = []
    for k in range(n):
        row, col = divmod(k, cols)
        fx = (col / (cols - 1) - 0.5) if cols > 1 else 0.0
        fz = (row / (rows - 1)) if rows > 1 else 0.0
        x = fx * 2.0 * LIGHT_X_HALF
        z = LIGHT_Z_NEAR + fz * (LIGHT_Z_FAR - LIGHT_Z_NEAR)
        y = LIGHT_Y_MIN + (LIGHT_Y_MAX - LIGHT_Y_MIN) * ((k % 3) / 2.0)
        color = PALETTE[k % len(PALETTE)]
        ents.append({"components": {
            "Transform": transform((x, y, z)),
            "PointLight": {
                "color": color,
                "intensity": LIGHT_INTENSITY,
                "range": LIGHT_RANGE,
                "enabled": True,
                # non-shadow-casting accent lights (cast_shadows defaults false)
            },
        }})
    return ents


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 1
    n = int(sys.argv[1])
    out = sys.argv[2]
    scene = {"version": 1, "entities": floor_entities() + light_entities(n)}
    with open(out, "w") as f:
        json.dump(scene, f, indent=1)
    print(f"wrote {out}: {FLOOR_COLS*FLOOR_ROWS} floor tiles + {n} lights")
    return 0


if __name__ == "__main__":
    sys.exit(main())
