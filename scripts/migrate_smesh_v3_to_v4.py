#!/usr/bin/env python3
"""Migrate v3 .smesh files to v4 in place: append a neutral (zero) baked-direct
word to each vertex (48 -> 52 bytes) and fix the version, stride, and index
offset. Geometry is byte-identical to a recook, since unbaked content carries a
zero channel. Static (non-skinned) meshes only; skinned .skmesh are rejected.

Usage: migrate_smesh_v3_to_v4.py <file.smesh> [<file.smesh> ...]
"""

import struct
import sys

HDR = 88
FLAG_SKINNED = 1 << 0


def migrate(path):
    with open(path, "rb") as f:
        data = bytearray(f.read())
    if data[0:4] != b"SMSH":
        raise SystemExit(f"{path}: not a .smesh")
    version = struct.unpack_from("<I", data, 4)[0]
    if version == 4:
        print(f"{path}: already v4, skipped")
        return
    if version != 3:
        raise SystemExit(f"{path}: unexpected version {version}")
    flags = struct.unpack_from("<I", data, 8)[0]
    if flags & FLAG_SKINNED:
        raise SystemExit(f"{path}: skinned mesh, not handled")

    vcount = struct.unpack_from("<I", data, 24)[0]
    stride = struct.unpack_from("<I", data, 36)[0]
    if stride != 48:
        raise SystemExit(f"{path}: unexpected v3 stride {stride}")
    vertex_off = struct.unpack_from("<I", data, 80)[0]
    index_off = struct.unpack_from("<I", data, 84)[0]

    # Rebuild the vertex blob with 4 zero bytes appended per 48-byte vertex.
    old_v = data[vertex_off:vertex_off + vcount * 48]
    new_v = bytearray()
    for i in range(vcount):
        new_v += old_v[i * 48:(i + 1) * 48]
        new_v += b"\x00\x00\x00\x00"
    tail = data[index_off:]   # index blob (and nothing after, for static)

    struct.pack_into("<I", data, 4, 4)                     # Version -> 4
    struct.pack_into("<I", data, 36, 52)                   # VertexStride -> 52
    struct.pack_into("<I", data, 84, vertex_off + len(new_v))  # IndexDataOffset

    out = data[:vertex_off] + new_v + tail
    with open(path, "wb") as f:
        f.write(out)
    print(f"{path}: v3 -> v4 ({vcount} verts, +{vcount * 4} bytes)")


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    for p in sys.argv[1:]:
        migrate(p)
    return 0


if __name__ == "__main__":
    sys.exit(main())
