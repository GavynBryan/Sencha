#include <render/static_mesh/StaticMeshValidation.h>

#include <cmath>
#include <limits>

namespace
{
    bool IsFinite(double value)
    {
        return std::isfinite(value);
    }

    bool IsFinite(const Vec2d& value)
    {
        return IsFinite(value.X) && IsFinite(value.Y);
    }

    bool IsFinite(const Vec3d& value)
    {
        return IsFinite(value.X) && IsFinite(value.Y) && IsFinite(value.Z);
    }

    bool IsFinite(const Aabb3d& bounds)
    {
        return IsFinite(bounds.Min) && IsFinite(bounds.Max);
    }

    void AddError(StaticMeshValidationResult& result, std::string message)
    {
        result.Errors.push_back({ .Message = std::move(message) });
    }
}

StaticMeshValidationResult ValidateStaticMeshData(const StaticMeshData& mesh)
{
    StaticMeshValidationResult result;

    if (mesh.Vertices.empty())
        AddError(result, "vertex buffer must not be empty");
    if (mesh.Indices.empty())
        AddError(result, "index buffer must not be empty");
    if ((mesh.Indices.size() % 3) != 0)
        AddError(result, "index count must be divisible by 3");
    if (mesh.Sections.empty())
        AddError(result, "at least one section must exist");

    for (size_t vertexIndex = 0; vertexIndex < mesh.Vertices.size(); ++vertexIndex)
    {
        const StaticMeshVertex& vertex = mesh.Vertices[vertexIndex];
        if (!IsFinite(vertex.Position))
            AddError(result, "vertex " + std::to_string(vertexIndex) + " position must be finite");
        if (!IsFinite(vertex.Normal))
            AddError(result, "vertex " + std::to_string(vertexIndex) + " normal must be finite");
        if (!IsFinite(vertex.Uv0))
            AddError(result, "vertex " + std::to_string(vertexIndex) + " uv0 must be finite");
    }

    for (size_t index = 0; index < mesh.Indices.size(); ++index)
    {
        if (mesh.Indices[index] >= mesh.Vertices.size())
        {
            AddError(result, "index " + std::to_string(index) + " is out of range");
        }
    }

    if (mesh.LocalBounds.IsValid() && !IsFinite(mesh.LocalBounds))
        AddError(result, "mesh local bounds must be finite");

    for (size_t sectionIndex = 0; sectionIndex < mesh.Sections.size(); ++sectionIndex)
    {
        const StaticMeshSection& section = mesh.Sections[sectionIndex];
        const uint64_t endIndex = static_cast<uint64_t>(section.IndexOffset) + section.IndexCount;
        const uint64_t endVertex = static_cast<uint64_t>(section.VertexOffset) + section.VertexCount;

        if (section.IndexCount == 0)
            AddError(result, "section " + std::to_string(sectionIndex) + " index count must be > 0");
        if (endIndex > mesh.Indices.size())
            AddError(result, "section " + std::to_string(sectionIndex) + " index range exceeds index buffer");
        if (section.VertexCount == 0)
            AddError(result, "section " + std::to_string(sectionIndex) + " vertex count must be > 0");
        if (endVertex > mesh.Vertices.size())
            AddError(result, "section " + std::to_string(sectionIndex) + " vertex range exceeds vertex buffer");
        if (section.LocalBounds.IsValid() && !IsFinite(section.LocalBounds))
            AddError(result, "section " + std::to_string(sectionIndex) + " local bounds must be finite");

        if (section.IndexCount > 0 && endIndex <= mesh.Indices.size() && section.VertexCount > 0 && endVertex <= mesh.Vertices.size())
        {
            for (uint64_t index = section.IndexOffset; index < endIndex; ++index)
            {
                const uint32_t vertexIndex = mesh.Indices[static_cast<size_t>(index)];
                if (vertexIndex < section.VertexOffset || vertexIndex >= endVertex)
                {
                    AddError(result,
                             "section " + std::to_string(sectionIndex)
                             + " indices must fall within its vertex range");
                    break;
                }
            }
        }
    }

    return result;
}

Aabb3d ComputeStaticMeshBounds(std::span<const StaticMeshVertex> vertices)
{
    Aabb3d bounds = Aabb3d::Empty();
    for (const StaticMeshVertex& vertex : vertices)
        bounds.ExpandToInclude(vertex.Position);
    return bounds;
}

Aabb3d ComputeStaticMeshSectionBounds(const StaticMeshData& mesh, const StaticMeshSection& section)
{
    Aabb3d bounds = Aabb3d::Empty();

    const size_t indexStart = section.IndexOffset;
    const size_t indexEnd = indexStart + section.IndexCount;
    for (size_t index = indexStart; index < indexEnd; ++index)
    {
        const uint32_t vertexIndex = mesh.Indices[index];
        bounds.ExpandToInclude(mesh.Vertices[vertexIndex].Position);
    }

    return bounds;
}

void RecomputeStaticMeshBounds(StaticMeshData& mesh)
{
    mesh.LocalBounds = ComputeStaticMeshBounds(mesh.Vertices);
    for (StaticMeshSection& section : mesh.Sections)
        section.LocalBounds = ComputeStaticMeshSectionBounds(mesh, section);
}
