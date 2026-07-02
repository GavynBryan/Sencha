#pragma once

#include <math/Mat.h>
#include <math/geometry/3d/Aabb3d.h>
#include <render/Material.h>
#include <render/static_mesh/StaticMeshHandle.h>

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
    StaticMeshHandle Mesh;
    MaterialHandle Material;
    uint32_t SectionIndex = 0;
    Mat4 WorldMatrix = Mat4::Identity();
    Aabb3d WorldBounds = Aabb3d::Empty();
    float CameraDepth = 0.0f;
    ShaderPassId Pass = ShaderPassId::ForwardOpaque;
    uint64_t SortKey = 0;
};

[[nodiscard]] uint64_t BuildOpaqueSortKey(const RenderQueueItem& item);

// A run of consecutive OpaqueOrder() entries that share mesh, section, and
// material: one instanced draw call. Built by SortOpaque() from the actual item
// fields (never from the truncated sort-key bits, so slot aliasing can only
// cost a merge, not correctness). The runs partition the order; a run of one
// is an ordinary single-instance draw, so there is exactly one draw path.
struct RenderQueueRun
{
    uint32_t First = 0; // index into OpaqueOrder()
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

    // Draw order produced by SortOpaque(): indices into Opaque(), sorted by
    // SortKey. Consumers walk this rather than the items so the sort never moves
    // the 128-byte items themselves. Empty until SortOpaque() has run.
    [[nodiscard]] const std::vector<uint32_t>& OpaqueOrder() const { return OpaqueOrderIndices; }

    // Instanced-draw runs over OpaqueOrder() (see RenderQueueRun). Empty until
    // SortOpaque() has run.
    [[nodiscard]] const std::vector<RenderQueueRun>& OpaqueRuns() const { return OpaqueRunList; }

private:
    std::vector<RenderQueueItem> OpaqueItems;
    std::vector<uint32_t>        OpaqueOrderIndices;
    std::vector<RenderQueueRun>  OpaqueRunList;
};
