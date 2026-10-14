#pragma once

#include "ThumbnailStudio.h"
#include "thumbnails/AssetPictures.h"

#include <render/ui/UiDrawFrame.h>

#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

class SceneSourceStage;
class UiService;
class UiTextureResidency;
class LoggingProvider;
struct FrameContext;
struct RuntimeAssets;

// Previews of .sscene sources for the browser: framing and entry lifetime over
// a SceneSourceStage (released once every slot holds the image); how a
// preview looks belongs to the ThumbnailStudio.
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
    // Renders every preview afresh: a released one no longer knows whether it
    // nests one of `assetPaths`. Image names are kept.
    void Invalidate(std::span<const std::string> assetPaths);
    void Clear();

private:
    struct Entry
    {
        ViewportId Target{};
        std::unique_ptr<SceneSourceStage> Stage;
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
