#pragma once

#include "ViewportTargetCache.h"

#include <render/extract/Camera.h>

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class EditorDocument;
class LoggingProvider;
class MaterialCache;
class MeshForwardPass;
class SceneRenderQueueBuilder;
class SkyGradientPass;
class StaticMeshCache;
struct FrameContext;
struct RuntimeAssets;

//=============================================================================
// SceneThumbnailCache
//
// Offscreen previews of .sscene sources for the browser: each requested scene
// loads into a scratch document, frames its bounds from a three-quarter angle,
// and renders once through the same forward pass the Solid viewport uses --
// real materials, full-bright neutral ambient, sky gradient behind. Targets
// and their ImGui bindings ride the ViewportTargetCache under synthetic
// viewport ids, so the presenter, per-frame slots, and teardown are the ones
// the viewports already trust.
//
// A thumbnail renders once per in-flight slot and then holds; requesting it
// again is free. The scratch document and queue builder stay alive with the
// entry because the recorded commands reference their GPU buffers and asset
// handles.
//=============================================================================
class SceneThumbnailCache
{
public:
    SceneThumbnailCache(ViewportTargetCache& targets,
                        MeshForwardPass& forward,
                        SkyGradientPass& sky,
                        RuntimeAssets& assets,
                        StaticMeshCache& meshes,
                        MaterialCache& materials,
                        LoggingProvider& logging,
                        VkFormat depthFormat);
    ~SceneThumbnailCache();

    void SetContentRoots(std::vector<std::filesystem::path> roots);

    // UI side: the thumbnail texture for the source, 0 while it has not
    // rendered (the caller draws a placeholder). Requests it if new and marks
    // it used this frame.
    [[nodiscard]] ImTextureID Thumbnail(const std::string& assetPath);

    // Render side, inside the feature's offscreen recording: renders at most
    // one pending thumbnail pass this frame.
    void RenderPending(const FrameContext& frame);

    // Keeps this cache's synthetic ids out of the viewport prune.
    void AppendLiveViewports(std::vector<ViewportId>& live) const;

    // Drops entries not used recently, keeping at most `budget`.
    void TrimToBudget(std::size_t budget);
    void Clear();

private:
    struct Entry
    {
        ViewportId Target{};
        std::unique_ptr<EditorDocument> Document;
        std::unique_ptr<SceneRenderQueueBuilder> Queues;
        CameraRenderData Camera;
        int RemainingPasses = 0;
        bool Loaded = false;
        bool Failed = false;
        std::uint64_t LastUsedFrame = 0;
    };

    [[nodiscard]] bool LoadEntry(const std::string& assetPath, Entry& entry);

    ViewportTargetCache& Targets;
    MeshForwardPass& Forward;
    SkyGradientPass& Sky;
    RuntimeAssets& Assets;
    StaticMeshCache& Meshes;
    MaterialCache& Materials;
    LoggingProvider& Logging;
    VkFormat DepthFormat = VK_FORMAT_UNDEFINED;
    std::vector<std::filesystem::path> ContentRoots;
    std::unordered_map<std::string, Entry> Entries;
    std::uint32_t NextTargetId = 0;
    std::uint64_t FrameClock = 0;
};
