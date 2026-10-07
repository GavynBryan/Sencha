#pragma once

#include "ThumbnailStudio.h"
#include "thumbnails/AssetPictures.h"

#include <render/ui/UiDrawFrame.h>

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class EditorDocument;
class UiService;
class UiTextureResidency;
class LoggingProvider;
class BrushBakeCache;
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
class SceneThumbnailCache final : public IAssetPictureSource
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

    // Where the previews are shown: host images registered with `ui`.
    void SetUi(UiService* ui) { Ui = ui; }
    // UI side: the host image an authored document names the preview by, as
    // `host:NAME`; empty without authored UI. Requests it if new and marks it
    // used this frame.
    [[nodiscard]] std::string ImageName(const std::string& assetPath);
    [[nodiscard]] AssetPicture PictureOf(std::string_view assetPath) override
    {
        return { .Image = ImageName(std::string(assetPath)) };
    }
    void Forget() override { Clear(); }
    // Render side, once this frame's passes are recorded: publishes every
    // finished preview's image and withdraws the ones that went.
    void PublishImages(UiTextureResidency& textures);

    // Render side, inside the feature's offscreen recording: renders at most
    // one pending thumbnail pass this frame, and releases payloads whose
    // recorded passes are safely behind us.
    void RenderPending(const FrameContext& frame);

    // Whether RenderPending has a pass to record or a payload to release.
    [[nodiscard]] bool HasPendingWork() const;

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
        // The thumbnail document is never an open document, so its brush
        // meshes are resident exactly as long as this entry's builder is.
        std::unique_ptr<BrushBakeCache> Bakes;
        std::unique_ptr<SceneRenderQueueBuilder> Queues;
        CameraRenderData Camera;
        int RemainingPasses = 0;
        // The frame after which the recorded passes cannot still be in
        // flight, so the scratch payload may go.
        std::uint64_t ReleasePayloadAfter = UINT64_MAX;
        bool Loaded = false;
        bool Failed = false;
        std::uint64_t LastUsedFrame = 0;
        UiHostImageId Image = kInvalidUiHostImage;
    };

    [[nodiscard]] bool LoadEntry(const std::string& assetPath, Entry& entry);

    ThumbnailStudio& Studio;
    RuntimeAssets& Assets;
    StaticMeshCache& Meshes;
    LoggingProvider& Logging;
    std::vector<std::filesystem::path> ContentRoots;
    std::unordered_map<std::string, Entry> Entries;
    std::uint64_t FrameClock = 0;
    UiService* Ui = nullptr;
    // Images of entries that went, withdrawn at the next publish.
    std::vector<UiHostImageId> Gone;
};
