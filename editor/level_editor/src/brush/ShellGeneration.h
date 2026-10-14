#pragma once

#include "BrushMesh.h"

#include <core/assets/AssetRef.h>

#include <cstdint>
#include <string>
#include <vector>

// Walls generated around a volume: one slab per face, extruded outward by a
// thickness and bounded at each edge by a join plane. The rules are in
// docs/plans/level-spatial-model.md, section 5.2.

struct ShellSlab
{
    BrushMesh     Mesh;       // in the volume's frame
    std::uint32_t SourceFace; // the volume face it stands on
};

enum class ShellProblemKind : std::uint8_t
{
    // Refusals: the volume gets no slabs at all.
    OpenVolume,
    NonPlanarFace,
    InvertedSlab,
    // A note: more than three planes meet at a corner, so the bisector cuts may
    // leave a small gap or overlap there.
    VertexJoin,
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

// Pure. A volume that is not closed, has a face off its plane, or would invert
// a slab at this thickness is refused, never approximated.
[[nodiscard]] ShellResult GenerateShell(const BrushMesh& volume, const ShellRules& rules);
