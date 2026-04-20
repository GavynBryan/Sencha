#pragma once

#include <graphics/vulkan/VulkanBufferService.h>
#include <math/Vec.h>
#include <math/geometry/3d/Aabb3d.h>

#include <cstdint>
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
// CPU-side mesh description passed to MeshCache::Create(). After upload
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

// Packed versioned handle to a mesh owned by MeshCache. Id 0 is null.
struct MeshHandle
{
    uint32_t Id = 0;

    [[nodiscard]] bool IsValid() const { return Id != 0; }
    [[nodiscard]] bool IsNull() const { return Id == 0; }
    [[nodiscard]] uint32_t SlotIndex() const { return Id & ((1u << 20u) - 1u); }
    [[nodiscard]] uint32_t Generation() const { return Id >> 20u; }
    bool operator==(const MeshHandle&) const = default;
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
