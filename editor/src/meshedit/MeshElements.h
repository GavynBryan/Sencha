#pragma once

#include "../level/brush/BrushMesh.h"

#include <math/geometry/3d/Transform3d.h>

#include <cstdint>
#include <optional>
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

struct MeshElements
{
    [[nodiscard]] static std::vector<FaceElement> Faces(const BrushMesh& mesh,
                                                        const Transform3f& transform);
    [[nodiscard]] static std::vector<EdgeElement> Edges(const BrushMesh& mesh,
                                                        const Transform3f& transform);
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
};
