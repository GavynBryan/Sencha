#!/usr/bin/env python3
"""Builds cooked scenes that vary shadow caster entity count on one axis.

The document cook merges brush geometry into one mesh per partition cell, so
authoring N brushes yields a handful of entities rather than N of them. Caster
count is a property of entity count, so these scenes are produced by cloning a
cooked mesh entity: every clone references assets the source scene already
resolved, which is what keeps the result loadable without a second cook.

Usage:
  gen_caster_scale_scene.py --source <cooked.json> --out-dir <dir>
                            --name <stem> --casters N [--shadow-lights M]
"""
import argparse
import copy
import json
import math
import os
import shutil
import sys


def find_entity(entities, component):
    for entity in entities:
        if component in entity.get("components", {}):
            return entity
    return None


def set_position(entity, x, y, z):
    local = entity["components"]["Transform"]["local"]
    local["position"] = [x, y, z]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True, help="cooked scene to clone from")
    parser.add_argument("--out-dir", required=True)
    parser.add_argument("--name", required=True)
    parser.add_argument("--casters", type=int, required=True)
    parser.add_argument("--shadow-lights", type=int, default=1)
    parser.add_argument("--half-extent", type=float, default=14.0)
    args = parser.parse_args()

    with open(args.source) as handle:
        scene = json.load(handle)

    entities = scene["entities"]
    mesh_template = find_entity(entities, "StaticMesh")
    if mesh_template is None:
        print("source scene has no StaticMesh entity to clone", file=sys.stderr)
        return 1
    light_template = find_entity(entities, "SpotLight")
    if light_template is None:
        print("source scene has no SpotLight entity to clone", file=sys.stderr)
        return 1

    # The shell and the original lights stay; clones are added on top so the
    # scene still has a floor to receive the shadows being measured.
    keep = [e for e in entities if "SpotLight" not in e.get("components", {})]
    result = list(keep)

    side = max(1, math.ceil(math.sqrt(args.casters)))
    spacing = (args.half_extent * 2.0) / (side + 1)
    placed = 0
    for gz in range(side):
        for gx in range(side):
            if placed >= args.casters:
                break
            clone = copy.deepcopy(mesh_template)
            x = -args.half_extent + spacing * (gx + 1)
            z = -args.half_extent + spacing * (gz + 1)
            # Lifted clear of the floor so each clone is a distinct occluder
            # rather than z-fighting with the shell it sits on.
            set_position(clone, round(x, 4), 1.5, round(z, 4))
            result.append(clone)
            placed += 1

    for i in range(args.shadow_lights):
        clone = copy.deepcopy(light_template)
        angle = 2.0 * math.pi * i / max(1, args.shadow_lights)
        set_position(clone, round(11.0 * math.cos(angle), 4), 7.0,
                     round(11.0 * math.sin(angle), 4))
        result.append(clone)

    scene["entities"] = result
    # The hierarchy block indexes entities positionally; clones are roots, so
    # anything carried over from the source would point at the wrong entity.
    if "hierarchy" in scene:
        scene["hierarchy"] = []

    os.makedirs(args.out_dir, exist_ok=True)
    out_scene = os.path.join(args.out_dir, args.name + ".cooked.json")
    with open(out_scene, "w") as handle:
        json.dump(scene, handle, indent=1)

    # The manifest and the cell meshes are referenced by the clones, so the
    # sidecars travel with the scene under its new name.
    source_stem = args.source[: -len(".cooked.json")]
    for suffix in (".manifest.json", ".collision.json"):
        src = source_stem + suffix
        if os.path.exists(src):
            shutil.copyfile(src, os.path.join(args.out_dir, args.name + suffix))

    print(f"{out_scene}: {placed} casters, {args.shadow_lights} shadow lights, "
          f"{len(result)} entities")
    return 0


if __name__ == "__main__":
    sys.exit(main())
