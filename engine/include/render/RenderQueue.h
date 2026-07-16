#pragma once

#include <math/Mat.h>
#include <math/geometry/3d/Aabb3d.h>
#include <render/Material.h>
#include <render/static_mesh/StaticMeshHandle.h>

#include <cstdint>
#include <vector>

enum class OpaquePipelineId : uint8_t
{
    StandardLitBack = 0,
    StandardLitDoubleSided = 1,
    UnlitBack = 2,
    UnlitDoubleSided = 3,
};

[[nodiscard]] constexpr OpaquePipelineId SelectOpaquePipeline(const Material& material)
{
    if (material.Shading == MaterialShading::Unlit)
        return material.DoubleSided
            ? OpaquePipelineId::UnlitDoubleSided
            : OpaquePipelineId::UnlitBack;
    return material.DoubleSided
        ? OpaquePipelineId::StandardLitDoubleSided
        : OpaquePipelineId::StandardLitBack;
}

//=============================================================================
// RenderQueueItem
//
// A single draw call's worth of data extracted from the scene. SortKey is
// computed by BuildOpaqueSortKey() and encodes pass, pipeline, material, and
// depth so sorting produces a state-efficient draw order.
//=============================================================================
struct RenderQueueItem
{
    StaticMeshHandle Mesh;
    MaterialHandle Material;
    uint32_t SectionIndex = 0;
    Mat4 WorldMatrix = Mat4::Identity();
    Aabb3d WorldBounds = Aabb3d::Empty();
    float CameraDepth = 0.0f;
    ShaderPassId Pass = ShaderPassId::ForwardOpaque;
    OpaquePipelineId Pipeline = OpaquePipelineId::StandardLitBack;
    uint64_t SortKey = 0;
};

[[nodiscard]] uint64_t BuildOpaqueSortKey(const RenderQueueItem& item);

// A run of consecutive OpaqueOrder() entries that share pipeline, mesh, section,
// and material: one instanced draw call. Built by SortOpaque() from the actual
// item fields, so truncated sort-key bits cannot compromise correctness.
struct RenderQueueRun
{
    uint32_t First = 0;
    uint32_t Count = 0;
};

//=============================================================================
// RenderQueue
//
// Transient per-frame list of draw calls. Populated by RenderExtractionSystem,
// sorted by SortOpaque(), then consumed by MeshRenderFeature. Call Reset() at
// the start of each frame. Frustum culling is applied during extraction.
//=============================================================================
class RenderQueue
{
public:
    void Reset();
    void AddOpaque(const RenderQueueItem& item);
    [[nodiscard]] std::vector<RenderQueueItem>& Opaque() { return OpaqueItems; }
    [[nodiscard]] const std::vector<RenderQueueItem>& Opaque() const { return OpaqueItems; }
    void SortOpaque();

    [[nodiscard]] const std::vector<uint32_t>& OpaqueOrder() const { return OpaqueOrderIndices; }
    [[nodiscard]] const std::vector<RenderQueueRun>& OpaqueRuns() const { return OpaqueRunList; }

private:
    std::vector<RenderQueueItem> OpaqueItems;
    std::vector<uint32_t> OpaqueOrderIndices;
    std::vector<RenderQueueRun> OpaqueRunList;
};
