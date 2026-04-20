#pragma once

#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <graphics/vulkan/VulkanBufferService.h>
#include <math/Vec.h>
#include <math/geometry/3d/Aabb3d.h>

#include <cstdint>
#include <string_view>
#include <tuple>
#include <vector>

// Interleaved vertex layout expected by the mesh forward shader (position, normal, uv0).
struct StaticMeshVertex
{
    Vec3d Position;
    Vec3d Normal;
    Vec2d Uv0;
};

// A contiguous slice of the shared index buffer belonging to one material slot.
struct SubmeshRange
{
    uint32_t IndexOffset = 0;
    uint32_t IndexCount = 0;
    uint32_t MaterialSlot = 0;
};

//=============================================================================
// MeshData
//
// CPU-side mesh description passed to MeshService::Create(). After upload
// this data is no longer needed; the GPU representation lives in GpuMesh.
//
// All submeshes share the same vertex buffer; each SubmeshRange indexes into
// the shared index buffer.
//=============================================================================
struct MeshData
{
    std::vector<StaticMeshVertex> Vertices;
    std::vector<uint32_t> Indices;
    Aabb3d LocalBounds = Aabb3d::Empty();
    std::vector<SubmeshRange> Submeshes;
};

// Versioned handle to a mesh owned by MeshService. Generation 0 is null.
struct MeshHandle
{
    uint32_t Index = UINT32_MAX;
    uint32_t Generation = 0;

    [[nodiscard]] bool IsValid() const { return Index != UINT32_MAX && Generation != 0; }
    bool operator==(const MeshHandle&) const = default;
};

template <>
struct TypeSchema<MeshHandle>
{
    static constexpr std::string_view Name = "MeshHandle";

    static auto Fields()
    {
        return std::tuple{
            MakeField("index", &MeshHandle::Index),
            MakeField("generation", &MeshHandle::Generation),
        };
    }
};

//=============================================================================
// GpuMesh
//
// Resident GPU representation of a mesh. Holds BufferHandles to the vertex
// and index buffers allocated via VulkanBufferService, plus a copy of the
// submesh ranges and local-space AABB used for culling.
//=============================================================================
struct GpuMesh
{
    BufferHandle VertexBuffer;
    BufferHandle IndexBuffer;
    uint32_t VertexCount = 0;
    uint32_t IndexCount = 0;
    Aabb3d LocalBounds = Aabb3d::Empty();
    std::vector<SubmeshRange> Submeshes;
};

namespace MeshPrimitives
{
    MeshData BuildCube(float size = 1.0f);
}
