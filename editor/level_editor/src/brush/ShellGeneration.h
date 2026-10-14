#pragma once

#include "BrushMesh.h"
#include "CarveShape.h"

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
    Vec3d                      Outward;        // that face's outward normal
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
    // An opening could not be cut through a slab it crosses; Element is its
    // index among the openings:
    OpeningRefused,
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

// A cut through generated walls, in the shell's frame: its sill's middle, and
// unit axes across it, up it and through the wall.
struct ShellOpening
{
    Vec3d            Origin;
    Vec3d            Across;
    Vec3d            Up;
    Vec3d            Through;
    float            Width = 0.0f;
    float            Height = 0.0f;
    CarveShape       Shape = CarveShape::Rectangle;
    CarveShapeParams Params;
};

// Cuts each opening through every slab facing along it whose thickness, give
// or take half a metre, holds the opening's sill. A shared wall is one slab, so
// it is cut once; an outline on a slab's rim becomes a notch, which is how a
// doorway differs from a window. A cut the carve refuses is reported and the
// slab stands uncut there.
void CutOpenings(ShellResult& shell, float thickness, std::span<const ShellOpening> openings);
