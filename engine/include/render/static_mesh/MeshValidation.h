#pragma once

#include <render/skinned_mesh/SkinnedMeshData.h>
#include <render/static_mesh/MeshGeometry.h>

#include <span>
#include <string>
#include <vector>

struct MeshValidationError
{
    std::string Message;
};

struct MeshValidationResult
{
    std::vector<MeshValidationError> Errors;

    [[nodiscard]] bool IsValid() const
    {
        return Errors.empty();
    }
};

// Geometry invariants every producer must meet (the runtime never fixes
// data): non-empty vertex/index/section streams, finite attributes, tangent
// w of ±1, in-range indices, sections within their buffers.
[[nodiscard]] MeshValidationResult ValidateMeshGeometry(const MeshGeometry& mesh);

// Geometry plus the skinning invariants: a valid skeleton path, joint count
// in 1..kMaxSkeletonJoints, one influence per vertex, joints below the joint
// count, zero-weight slots on joint 0, weights summing to exactly 255.
[[nodiscard]] MeshValidationResult ValidateSkinnedMeshData(const SkinnedMeshData& mesh);

[[nodiscard]] Aabb3d ComputeMeshBounds(std::span<const StaticMeshVertex> vertices);

[[nodiscard]] Aabb3d ComputeMeshSectionBounds(const MeshGeometry& mesh,
                                              const StaticMeshSection& section);

void RecomputeMeshBounds(MeshGeometry& mesh);
