#pragma once

#include <render/static_mesh/StaticMeshData.h>

#include <span>
#include <string>
#include <vector>

struct StaticMeshValidationError
{
    std::string Message;
};

struct StaticMeshValidationResult
{
    std::vector<StaticMeshValidationError> Errors;

    [[nodiscard]] bool IsValid() const
    {
        return Errors.empty();
    }
};

[[nodiscard]] StaticMeshValidationResult ValidateStaticMeshData(const StaticMeshData& mesh);

[[nodiscard]] Aabb3d ComputeStaticMeshBounds(std::span<const StaticMeshVertex> vertices);

[[nodiscard]] Aabb3d ComputeStaticMeshSectionBounds(const StaticMeshData& mesh,
                                                    const StaticMeshSection& section);

void RecomputeStaticMeshBounds(StaticMeshData& mesh);
