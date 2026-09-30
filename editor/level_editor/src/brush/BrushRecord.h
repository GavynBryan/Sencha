#pragma once

#include "BrushMesh.h"
#include "BrushModifier.h"

#include <cstdint>

// Everything authored about one brush: its polygon mesh and the modifier stack
// evaluated on top of it. This is the unit the store keys by BrushId and the
// unit that snapshots, duplication, and the scene sidecar carry, so a mesh can
// never travel without its stack.
struct BrushRecord
{
    BrushMesh          Mesh;
    BrushModifierStack Modifiers;
    // Store metadata, not authoring data; never serialized, and a snapshot's
    // copy means nothing once restored (the store assigns its own). Revision
    // bumps on every mutation and is what the evaluation cache compares: the
    // correctness sledgehammer. The three domain revisions bump only when the
    // signature of their domain changed, so a retained fact keyed on the domain
    // it depends on survives edits to the others:
    //   Topology  — source vertices, loops, soft edges, and the parameters of
    //               modifiers that mint meshes (Mirror). Baked meshes, edge
    //               topology, element maps, source elements.
    //   Placement — anything that moves pieces: the above, plus placement-only
    //               modifier parameters (Array count, spacing, axis...).
    //               Piece placements, bounds, instance runs.
    //   Material  — face materials and UV projections. Material leases.
    std::uint64_t      Revision = 1;
    std::uint64_t      TopologyRevision = 1;
    std::uint64_t      PlacementRevision = 1;
    std::uint64_t      MaterialRevision = 1;
};
