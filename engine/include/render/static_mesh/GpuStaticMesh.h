#pragma once

#include <graphics/vulkan/VulkanBufferService.h>
#include <render/static_mesh/MeshGeometry.h>
#include <render/static_mesh/StaticMeshSection.h>

#include <vector>

class Logger;

// The GPU residency of a mesh's geometry — the immutable vertex/index
// buffers plus the section table. Shared by static and skinned meshes: both
// upload their rest geometry identically (a skinned mesh's posed buffers,
// when the animation runtime adds them, are a separate, per-instance
// resource — Decision N).
struct GpuStaticMesh
{
    BufferHandle VertexBuffer;
    BufferHandle IndexBuffer;

    uint32_t VertexCount = 0;
    uint32_t IndexCount = 0;

    Aabb3d LocalBounds = Aabb3d::Empty();

    std::vector<StaticMeshSection> Sections;
};

// Validates and uploads `geometry` into GPU buffers, filling `out`. Returns
// false (logging the reason) on validation failure or a buffer/upload error,
// leaving no buffers leaked. Shared by StaticMeshCache and SkinnedMeshCache.
[[nodiscard]] bool UploadMeshGeometryToGpu(VulkanBufferService& buffers,
                                           const MeshGeometry& geometry,
                                           GpuStaticMesh& out,
                                           Logger& log);

// Destroys the GPU buffers `mesh` holds and clears them. Safe on an
// already-empty mesh.
void DestroyGpuMesh(VulkanBufferService& buffers, GpuStaticMesh& mesh);
