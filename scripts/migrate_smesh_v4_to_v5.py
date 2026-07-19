#!/usr/bin/env python3
"""Migrate v4 .smesh files to v5 in place: zero the 4-byte word at vertex
offset 48 (the retired baked-direct RGBM channel; v5 reads it as two unorm16
lightmap UVs) and fix the version. Stride is unchanged (52 bytes). The result
matches an unbaked recook; a level with baked lights must recook to get its
lightmap atlas and real UVs. Static (non-skinned) meshes only.

Usage: migrate_smesh_v4_to_v5.py <file.smesh> [<file.smesh> ...]
"""

import struct
import sys

FLAG_SKINNED = 1 << 0
FLAG_CHANNEL = 1 << 1   # v4: baked direct present; v5: lightmap UVs present


def migrate(path):
    with open(path, "rb") as f:
        data = bytearray(f.read())
    if data[0:4] != b"SMSH":
        raise SystemExit(f"{path}: not a .smesh")
    version = struct.unpack_from("<I", data, 4)[0]
    if version == 5:
        print(f"{path}: already v5, skipped")
        return
    if version != 4:
        raise SystemExit(f"{path}: unexpected version {version}")
    flags = struct.unpack_from("<I", data, 8)[0]
    if flags & FLAG_SKINNED:
        raise SystemExit(f"{path}: skinned mesh, not handled")

    vcount = struct.unpack_from("<I", data, 24)[0]
    stride = struct.unpack_from("<I", data, 36)[0]
    if stride != 52:
        raise SystemExit(f"{path}: unexpected v4 stride {stride}")
    vertex_off = struct.unpack_from("<I", data, 80)[0]

    for i in range(vcount):
        struct.pack_into("<I", data, vertex_off + i * 52 + 48, 0)

    struct.pack_into("<I", data, 4, 5)                    # Version -> 5
    struct.pack_into("<I", data, 8, flags & ~FLAG_CHANNEL)  # channel now neutral

    with open(path, "wb") as f:
        f.write(data)
    print(f"{path}: v4 -> v5 ({vcount} verts, channel zeroed)")


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    for p in sys.argv[1:]:
        migrate(p)
    return 0


if __name__ == "__main__":
    sys.exit(main())
