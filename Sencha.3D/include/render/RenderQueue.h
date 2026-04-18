#pragma once

#include <math/Mat.h>
#include <math/geometry/3d/Aabb3d.h>
#include <render/Material.h>
#include <render/MeshTypes.h>

#include <vector>

//=============================================================================
// RenderQueueItem
//
// A single draw call's worth of data extracted from the scene. SortKey is
// computed by BuildOpaqueSortKey() and encodes pass, material, and depth so
// that sorting the queue produces an optimal draw order.
//=============================================================================
struct RenderQueueItem
{
    MeshHandle Mesh;
    MaterialHandle Material;
    uint32_t SubmeshIndex = 0;
    Mat4 WorldMatrix = Mat4::Identity();
    Aabb3d WorldBounds = Aabb3d::Empty();
    float CameraDepth = 0.0f;
    ShaderPassId Pass = ShaderPassId::ForwardOpaque;
    uint64_t SortKey = 0;
};

[[nodiscard]] uint64_t BuildOpaqueSortKey(const RenderQueueItem& item);

//=============================================================================
// RenderQueue
//
// Transient per-frame list of draw calls. Populated by RenderExtractionSystem,
// pruned by FrustumCullingSystem, sorted by SortOpaque(), then consumed by
// MeshRenderFeature. Call Reset() at the start of each frame.
//=============================================================================
class RenderQueue
{
public:
    void Reset();
    void AddOpaque(const RenderQueueItem& item);
    [[nodiscard]] std::vector<RenderQueueItem>& Opaque() { return OpaqueItems; }
    [[nodiscard]] const std::vector<RenderQueueItem>& Opaque() const { return OpaqueItems; }
    void SortOpaque();

private:
    std::vector<RenderQueueItem> OpaqueItems;
};
