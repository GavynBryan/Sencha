#!/usr/bin/env python3
"""Emit a flat NxN-subdivided floor plane as a v3 .smesh (SMSH container).

A controlled-tessellation floor for the baked-direct Phase 0 spike: the same
plane at different subdivision counts shows how per-vertex-interpolated direct
light degrades on sparse geometry. Not a cook artifact; a hand-built test mesh.

Plane lies in XZ at Y=0, centered on the origin, normal +Y, UV 0..1, tangent +X.

Usage: gen_flat_plane_smesh.py <size_world_units> <subdivisions> <out.smesh>
"""

import struct
import sys

SMESH_VERSION = 4
VERTEX_STRIDE = 52   # 48-byte base + 4-byte baked-direct channel (neutral zero)
HEADER_SIZE = 88
SECTION_SIZE = 48


def build(size, subdiv):
    half = size * 0.5
    step = size / subdiv
    verts = []
    for iz in range(subdiv + 1):
        z = -half + iz * step
        for ix in range(subdiv + 1):
            x = -half + ix * step
            u = ix / subdiv
            v = iz / subdiv
            # position, normal(+Y), uv, tangent(+X, w=1)
            verts.append((x, 0.0, z, 0.0, 1.0, 0.0, u, v, 1.0, 0.0, 0.0, 1.0))

    indices = []
    row = subdiv + 1
    for iz in range(subdiv):
        for ix in range(subdiv):
            v00 = iz * row + ix
            v10 = v00 + 1
            v01 = v00 + row
            v11 = v01 + 1
            # CCW seen from +Y (looking down), so the lit top face is front.
            indices += [v00, v11, v10, v00, v01, v11]
    return verts, indices


def write(path, size, subdiv):
    verts, indices = build(size, subdiv)
    half = size * 0.5
    vcount = len(verts)
    icount = len(indices)
    section_table_off = HEADER_SIZE
    vertex_data_off = section_table_off + SECTION_SIZE
    index_data_off = vertex_data_off + vcount * VERTEX_STRIDE

    header = struct.pack(
        "<4s" + "I" * 11 + "f" * 6 + "I" * 4,
        b"SMSH",
        SMESH_VERSION, 0,          # Version, Flags
        0, 0, 0,                   # JointCount, SkinningDataOffset, SkeletonPathOffset
        vcount, icount, 1,         # VertexCount, IndexCount, SectionCount
        VERTEX_STRIDE, 0, 0,       # VertexStride, IndexFormat(UInt32), Topology(TriList)
        -half, 0.0, -half,         # BoundsMin
        half, 0.0, half,           # BoundsMax
        HEADER_SIZE, section_table_off, vertex_data_off, index_data_off,
    )
    section = struct.pack(
        "<6I6f",
        0, icount, 0, vcount, 0, 0,
        -half, 0.0, -half, half, 0.0, half,
    )
    body = bytearray()
    for vtx in verts:
        body += struct.pack("<12f", *vtx)
        body += struct.pack("<I", 0)   # BakedDirect (neutral: unbaked plane)
    for idx in indices:
        body += struct.pack("<I", idx)

    with open(path, "wb") as f:
        f.write(header)
        f.write(section)
        f.write(body)
    print(f"wrote {path}: {vcount} verts, {icount // 3} tris (size {size}, subdiv {subdiv})")


def main():
    if len(sys.argv) != 4:
        print(__doc__)
        return 1
    write(sys.argv[3], float(sys.argv[1]), int(sys.argv[2]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
