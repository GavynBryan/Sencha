#pragma once

#include <graphics/BufferHandle.h>
#include <render/static_mesh/MeshGeometry.h>
#include <render/static_mesh/StaticMeshSection.h>

#include <vector>

class Logger;
class VulkanBufferService;

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

    // Whether any vertex carries a non-zero lightmap UV. The .smesh header
    // records the same fact as kSmeshFlagLightmapUv, but that is a tooling
    // hint the loader never propagates, and meshes built in memory
    // (CreateFromData, cooked brush cells) never pass through a header at all
    // -- so it is derived from the geometry at upload, which covers every
    // source uniformly.
    //
    // Extraction reads it to decide whether an instance may sample its zone's
    // baked atlas. Without it, an unbaked mesh in a baked zone still got the
    // atlas index stamped and landed on the packer's reserved black border
    // texel: the right pixels, by arrangement rather than by decision.
    bool HasLightmapUvs = false;

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
