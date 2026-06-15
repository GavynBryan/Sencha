#pragma once

#include "LevelScene.h"
#include "../selection/SelectableRef.h"

#include <math/geometry/3d/Aabb3d.h>

#include <array>
#include <optional>
#include <vector>

// Transient axis-aligned-box view of a brush, used by the create-drag preview and
// whole-brush body bounds/move. Editable geometry is the BrushMesh; this is a
// convenience box derived from its local bounds.
struct BrushState
{
    Transform3f Transform = Transform3f::Identity();
    Vec3d HalfExtents = { 0.5, 0.5, 0.5 };
};

// A single mesh face in world space, for selection, highlight, and picking.
// ElementId in the matching SelectableRef is the face's index in BrushMesh::Faces.
struct BrushFaceGeometry
{
    uint32_t           FaceIndex = 0;
    Vec3d              Normal = {};   // world-space outward normal
    Vec3d              Center = {};   // world-space centroid
    std::vector<Vec3d> Corners;       // world-space loop (N-gon)
};

struct BrushFaceDescriptor
{
    SelectableRef Ref = {};
    BrushFaceGeometry Geometry = {};
};

class BrushGeometry
{
public:
    [[nodiscard]] static std::optional<BrushState> TryGetState(const LevelScene& scene, EntityId entity);

    [[nodiscard]] static Aabb3d ComputeBounds(const BrushState& state);
    [[nodiscard]] static std::array<Vec3d, 8> ComputeCorners(const BrushState& state);

    // Mesh faces in world space (Ref.ElementId = index in BrushMesh::Faces).
    [[nodiscard]] static std::vector<BrushFaceDescriptor> EnumerateFaces(const LevelScene& scene,
                                                                         EntityId entity);
    [[nodiscard]] static std::optional<BrushFaceDescriptor> TryGetFace(const LevelScene& scene,
                                                                       const SelectableRef& ref);

    [[nodiscard]] static BrushState Translate(const BrushState& state, Vec3d delta);
};
