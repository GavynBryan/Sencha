#pragma once

#include "BrushMesh.h"

#include <core/assets/AssetRef.h>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

// Walls generated around a volume: one slab per face, extruded outward by a
// thickness and bounded at each edge by a join plane. The rules are in
// docs/plans/level-spatial-model.md, section 5.2.

struct ShellSlab
{
    BrushMesh                  Mesh;           // in the volume's frame
    std::uint32_t              SourceFace = 0; // the volume face it stands on
    std::vector<std::uint32_t> InnerFaces;     // its faces on that volume face, looking into the space
};

enum class ShellProblemKind : std::uint8_t
{
    // Refusals: the volume gets no slabs at all.
    OpenVolume,
    NonPlanarFace,
    InvertedSlab,
    // Notes. More than three planes meet at a corner, so the bisector cuts may
    // leave a small gap or overlap there:
    VertexJoin,
    // A face shared with a neighbour across the gap is not convex, so both walls
    // stand there (version 1 shares convex faces only):
    SharedWallNotConvex,
};

struct ShellProblem
{
    ShellProblemKind Kind;
    // A face for the refusals, a vertex for VertexJoin.
    std::uint32_t    Element = 0;
    std::string      Message;
};

struct ShellResult
{
    std::vector<ShellSlab>    Slabs;
    std::vector<ShellProblem> Problems;
    bool                      Refused = false;
};

struct ShellRules
{
    float    Thickness = 0.25f;
    // Faces painted with it get no slab; empty leaves every face closed.
    AssetRef OpenFaceMaterial;
    // World up in the volume's frame: a face whose normal is within 45 degrees
    // of it, either way, is a floor or ceiling; any other is a wall.
    Vec3d    Up = Vec3d{ 0.0f, 1.0f, 0.0f };
};

// Another shelled volume, in the same frame. Where one of its faces lies
// exactly a wall thickness outside one of ours, facing back, the two share one
// wall over the overlap: the owner builds it, its outside in the neighbour's
// material, and the other leaves it out. A face split this way yields several
// slabs, meeting square along the seams.
struct ShellNeighbour
{
    const BrushMesh* Volume = nullptr;
    float            Thickness = 0.0f;
    bool             OwnsShared = false; // whether this volume's slab is the one kept
};

// Pure. A volume that is not closed, has a face off its plane, or would invert
// a slab at this thickness is refused, never approximated.
[[nodiscard]] ShellResult GenerateShell(const BrushMesh& volume, const ShellRules& rules,
                                        std::span<const ShellNeighbour> neighbours = {});
