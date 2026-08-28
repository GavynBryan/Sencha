#pragma once

#include "ThumbnailStudio.h"

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class EditorDocument;
class LoggingProvider;
class SceneRenderQueueBuilder;
struct FrameContext;
struct RuntimeAssets;

//=============================================================================
// SceneThumbnailCache
//
// Previews of .sscene sources for the browser: each requested scene loads
// into a scratch document, its renderable bounds pick the framing, and the
// ThumbnailStudio renders it. This cache owns what is scene-specific --
// loading, bounding, queue building, and the entry lifecycle; how a preview
// looks belongs to the studio.
//
// An entry's scratch document and queue builder live only while their
// recorded passes may still be in flight; once every slot holds the image,
// the payload is released and the entry keeps nothing but its target.
//=============================================================================
class SceneThumbnailCache
{
public:
    SceneThumbnailCache(ThumbnailStudio& studio,
                        RuntimeAssets& assets,
                        StaticMeshCache& meshes,
                        LoggingProvider& logging);
    ~SceneThumbnailCache();

    void SetContentRoots(std::vector<std::filesystem::path> roots);

    // Advances the entry clock; call once per frame before any Thumbnail().
    void BeginFrame();

    // UI side: the thumbnail texture for the source, 0 while it has not
    // rendered (the caller draws a placeholder). Requests it if new and marks
    // it used this frame.
    [[nodiscard]] ImTextureID Thumbnail(const std::string& assetPath);

    // Render side, inside the feature's offscreen recording: renders at most
    // one pending thumbnail pass this frame, and releases payloads whose
    // recorded passes are safely behind us.
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
        // The frame after which the recorded passes cannot still be in
        // flight, so the scratch payload may go.
        std::uint64_t ReleasePayloadAfter = UINT64_MAX;
        bool Loaded = false;
        bool Failed = false;
        std::uint64_t LastUsedFrame = 0;
    };

    [[nodiscard]] bool LoadEntry(const std::string& assetPath, Entry& entry);

    ThumbnailStudio& Studio;
    RuntimeAssets& Assets;
    StaticMeshCache& Meshes;
    LoggingProvider& Logging;
    std::vector<std::filesystem::path> ContentRoots;
    std::unordered_map<std::string, Entry> Entries;
    std::uint64_t FrameClock = 0;
};
