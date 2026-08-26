#pragma once

#include <math/Mat.h>
#include <math/geometry/3d/Aabb3d.h>
#include <render/Material.h>
#include <render/skinned_mesh/SkinnedMeshHandle.h>
#include <render/static_mesh/StaticMeshHandle.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

// The forward opaque pipeline family, three independent binary axes packed into
// the id: bit 0 double-sided, bit 1 unlit, bit 2 alpha-masked. The bits are the
// contract -- the pass builds each variant from them, and the debug families
// select their own narrower axes by masking, rather than by a parallel table
// that would have to be kept in step.
enum class OpaquePipelineId : uint8_t
{
    StandardLitBack = 0,
    StandardLitDoubleSided = 1,
    UnlitBack = 2,
    UnlitDoubleSided = 3,
    StandardLitBackMasked = 4,
    StandardLitDoubleSidedMasked = 5,
    UnlitBackMasked = 6,
    UnlitDoubleSidedMasked = 7,
};

inline constexpr std::size_t kOpaquePipelineCount = 8;

inline constexpr uint8_t kOpaquePipelineDoubleSidedBit = 1u;
inline constexpr uint8_t kOpaquePipelineUnlitBit = 2u;
inline constexpr uint8_t kOpaquePipelineMaskedBit = 4u;

[[nodiscard]] constexpr OpaquePipelineId SelectOpaquePipeline(const Material& material)
{
    uint8_t id = 0;
    if (material.DoubleSided)
        id |= kOpaquePipelineDoubleSidedBit;
    if (material.Shading == MaterialShading::Unlit)
        id |= kOpaquePipelineUnlitBit;
    // This picks the pipeline within a pass, not the pass: Blend routes to the
    // transparent phase through ResolveMaterialPass, and only Mask changes the
    // pipeline, because it cuts fragments.
    if (material.AlphaMode == MaterialAlphaMode::Mask)
        id |= kOpaquePipelineMaskedBit;
    return static_cast<OpaquePipelineId>(id);
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
    // Valid instead of Mesh for a skinned item (rest geometry resolved through
    // SkinnedMeshCache). Part of the run-merge identity: the two handle spaces
    // alias slot indices, so without it a static and a skinned item could
    // merge into one run and draw the wrong geometry.
    SkinnedMeshHandle SkinnedMesh;
    MaterialHandle Material;
    uint32_t SectionIndex = 0;
    Mat4 WorldMatrix = Mat4::Identity();
    Aabb3d WorldBounds = Aabb3d::Empty();
    float CameraDepth = 0.0f;
    ShaderPassId Pass = ShaderPassId::ForwardOpaque;
    OpaquePipelineId Pipeline = OpaquePipelineId::StandardLitBack;
    // Bindless slot of the zone's baked-lighting atlas, or UINT32_MAX when the
    // item has none. Uniform per draw, so it joins the run-merge equality test
    // (the same mesh resident in two zones must not share one run).
    uint32_t LightmapTextureIndex = UINT32_MAX;
    // The zone's baked ambient-occlusion plane (same layout and UVs as the
    // atlas); UINT32_MAX leaves the ambient term unmodulated. Uniform per
    // draw, run-merge identity like the atlas index.
    uint32_t AoTextureIndex = UINT32_MAX;
    // Per-instance remap from the mesh's lightmap UVs into its atlas rect;
    // identity for cooked cells (their UVs are absolute atlas coordinates).
    // Varies freely within a run: per-instance data, never merge criteria.
    Vec4 LightmapScaleBias = Vec4{ 1.0f, 1.0f, 0.0f, 0.0f };
    // Index into the frame's SkinnedPoseFrameData for a posed skinned item;
    // UINT32_MAX draws the rest geometry. Run-merge identity: two entities
    // sharing one skinned mesh pose independently and must not merge into
    // one instanced draw.
    uint32_t PoseSlot = UINT32_MAX;
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

    // Blended items are stored unsorted: their draw order is decided per view
    // by BuildTransparentOrder, because a host may replay one queue under
    // several cameras and back-to-front is a property of the camera.
    void AddTransparent(const RenderQueueItem& item);
    [[nodiscard]] const std::vector<RenderQueueItem>& Transparent() const
    {
        return TransparentItems;
    }

private:
    std::vector<RenderQueueItem> OpaqueItems;
    std::vector<RenderQueueItem> TransparentItems;
    std::vector<uint32_t> OpaqueOrderIndices;
    std::vector<RenderQueueRun> OpaqueRunList;
    // Reused (key, index) scratch for SortOpaque, kept across frames so the
    // per-frame sort does not reallocate.
    std::vector<std::pair<uint64_t, uint32_t>> OpaqueSortScratch;
};

// Fills `order` with indices into `items`, farthest bounds centre from
// `viewPosition` first. Blending is order-dependent, so this is a correctness
// sort, not a state optimization: the opaque path may reorder freely for bind
// efficiency, this one may not. Distance rather than a projected view depth
// because it needs nothing but the camera position -- no forward axis to get
// wrong per host -- and the failure cases (large coplanar surfaces crossing at
// grazing angles) are shared by both metrics. Ties break by queue index, so
// the order is deterministic whenever extraction order is.
//
// `order` is caller-retained scratch: cleared and refilled, never shrunk.
void BuildTransparentOrder(std::span<const RenderQueueItem> items,
                           const Vec3d& viewPosition,
                           std::vector<uint32_t>& order);
