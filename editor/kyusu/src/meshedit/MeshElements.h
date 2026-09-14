#pragma once

#include "MeshElementKind.h"

#include "brush/BrushMesh.h"
#include "selection/SelectableRef.h"

#include <math/geometry/3d/Transform3d.h>

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

struct FaceElement
{
    std::uint32_t Index = 0;
    Vec3d Normal = {};
    Vec3d Center = {};
    std::vector<Vec3d> Corners;
};

struct EdgeElement
{
    std::uint32_t Index = 0;
    std::uint32_t VertexA = 0;
    std::uint32_t VertexB = 0;
    Vec3d A = {};
    Vec3d B = {};
    Vec3d Mid = {};
};

struct VertexElement
{
    std::uint32_t Index = 0;
    Vec3d Position = {};
};

// The authored mesh's elements in world space, all three kinds, as the
// scene's placement facts retain them per entity. Consumers that resolve an
// element ref read these; only the facts layer builds them.
struct SourceWorldElements
{
    std::vector<EdgeElement>   Edges;    // BrushEdgePairs order
    std::vector<VertexElement> Vertices; // source vertex order
    std::vector<FaceElement>   Faces;    // source face order
};

struct MeshElements
{
    [[nodiscard]] static std::vector<FaceElement> Faces(const BrushMesh& mesh,
                                                        const Transform3f& transform);
    [[nodiscard]] static std::vector<EdgeElement> Edges(const BrushMesh& mesh,
                                                        const Transform3f& transform);
    // The edge topology alone, in Edges() order, as canonical (min, max)
    // vertex pairs: what a caller placing one mesh many times enumerates once
    // and transforms per placement.
    [[nodiscard]] static std::vector<std::pair<std::uint32_t, std::uint32_t>> UniqueEdgeVertexPairs(
        const BrushMesh& mesh);
    [[nodiscard]] static std::vector<VertexElement> Vertices(const BrushMesh& mesh,
                                                             const Transform3f& transform);

    [[nodiscard]] static std::optional<FaceElement> TryGetFace(const BrushMesh& mesh,
                                                               const Transform3f& transform,
                                                               std::uint32_t index);
    [[nodiscard]] static std::optional<EdgeElement> TryGetEdge(const BrushMesh& mesh,
                                                               const Transform3f& transform,
                                                               std::uint32_t index);
    [[nodiscard]] static std::optional<VertexElement> TryGetVertex(const BrushMesh& mesh,
                                                                   const Transform3f& transform,
                                                                   std::uint32_t index);

    // Every element of one brush as selection refs (whole-mesh select: the
    // double-click expansion and select-all). Object kind yields the entity ref
    // itself so callers need no special case.
    [[nodiscard]] static std::vector<SelectableRef> AllRefs(const BrushMesh& mesh,
                                                            const Transform3f& transform,
                                                            RegistryId registry,
                                                            EntityId entity,
                                                            MeshElementKind kind);
};
