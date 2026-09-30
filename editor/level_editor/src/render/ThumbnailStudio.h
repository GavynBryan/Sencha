#pragma once

#include "ViewportTargetCache.h"

#include <render/extract/Camera.h>

#include <span>

class MaterialCache;
class MeshForwardPass;
class RenderQueue;
class SkinnedMeshCache;
class SkyGradientPass;
class StaticMeshCache;
struct Aabb3d;
struct FrameContext;

//=============================================================================
// ThumbnailStudio
//
// The one offscreen studio every asset preview renders in: a fixed-size
// target from the shared viewport target cache under a synthetic id, a
// three-quarter camera fitted tightly to the subject's bounds, full-bright
// neutral lighting with the sky gradient behind, drawn through the same
// forward pass the Solid viewport uses. Scene previews render their document
// queues here today; a mesh or material preview is the same studio pointed at
// a different queue, which is the asset-browser consolidation this exists
// ahead of.
//
// The studio owns how a preview looks and where it renders; what is being
// previewed -- loading it, bounding it, caching it -- belongs to the caller.
//=============================================================================
class ThumbnailStudio
{
public:
    ThumbnailStudio(ViewportTargetCache& targets,
                    MeshForwardPass& forward,
                    SkyGradientPass& sky,
                    StaticMeshCache& meshes,
                    MaterialCache& materials,
                    const SkinnedMeshCache* skinnedMeshes,
                    VkFormat depthFormat);

    // A fresh synthetic target id, distinct from every layout-minted viewport:
    // the layout counts up from one, the studio counts up from the top half.
    [[nodiscard]] ViewportId AllocateTarget();

    // The target's current texture for the UI, registering its size; 0 until a
    // render has filled the displayed slot.
    [[nodiscard]] ImTextureID Display(ViewportId target);

    // How many single-frame renders a target needs before every in-flight slot
    // shows the image. The authority is the engine's frames-in-flight bound; a
    // private count here is exactly the private maximum that header forbids.
    [[nodiscard]] static int PassesPerTarget();

    // The three-quarter framing: a tight fit of the box's projected corners
    // against the square frustum, so every subject fills the same fraction of
    // the cell whatever its shape.
    [[nodiscard]] static CameraRenderData FrameSubject(const Aabb3d& bounds);

    // Records one render of the queues into the target. False when the target
    // has no acquirable slot this frame.
    bool RenderPass(const FrameContext& frame, ViewportId target,
                    const CameraRenderData& camera,
                    std::span<const RenderQueue* const> queues);

private:
    ViewportTargetCache& Targets;
    MeshForwardPass& Forward;
    SkyGradientPass& Sky;
    StaticMeshCache& Meshes;
    MaterialCache& Materials;
    const SkinnedMeshCache* SkinnedMeshes = nullptr;
    VkFormat DepthFormat = VK_FORMAT_UNDEFINED;
    std::uint32_t NextTargetId = 0;
};
