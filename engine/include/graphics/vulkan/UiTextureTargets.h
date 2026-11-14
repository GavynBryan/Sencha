#pragma once

#include <graphics/RenderFeature.h>
#include <graphics/RenderTargetId.h>
#include <graphics/vulkan/RenderTargetStore.h>
#include <graphics/vulkan/UiDrawPass.h>
#include <render/TargetTextures.h>
#include <render/ui/UiDrawFrame.h>

#include <vector>

class Logger;
class TextureCache;

//=============================================================================
// UiTextureTargets
//
// The recording behind UiTextureRenderFeature: draws each Texture-destined UI
// surface into the render target behind its target texture, and publishes that
// target to the table materials resolve through (render/TargetTextures.h).
//
// One image per texture, written in the Offscreen phase and sampled by the
// forward pass in the same frame (RenderTargetLifetime::WithinFrame,
// docs/renderer/resources.md). A texture is redrawn only on a frame its
// recording differs from what the image holds (SameUiRecording); otherwise the
// image keeps its pixels and nothing is recorded. A texture whose surface is
// gone samples black at once, and its images are freed after a quiet interval,
// so a screen that comes straight back is not reallocated.
//
// Cleared to transparent black, so the colour it holds is the document
// premultiplied over black: exactly what an emissive screen shows.
//=============================================================================
class UiTextureTargets
{
public:
    // Without `textures` content images do not draw. `log` may be null.
    [[nodiscard]] bool Setup(const RendererServices& services, TextureCache* textures, Logger* log);
    // Withdraws everything it published from `targets`.
    void Teardown(TargetTextureTable& targets);

    void Record(const RenderFrame& frame, const std::vector<UiTextureFrame>& surfaces,
                TargetTextureTable& targets);

    // How many textures hold images now: what says one nothing shows was freed.
    [[nodiscard]] std::size_t ResidentCount() const { return Store.ResidentCount(); }

private:
    struct Bound
    {
        TargetTextureId Texture;
        RenderTargetId Image;
        // What the image holds, while Holds is set.
        UiDrawFrame Drawn;
        bool Holds = false;
        bool Seen = false;
        QuietInterval Quiet;
    };

    [[nodiscard]] Bound& BindingFor(TargetTextureId texture);
    void Draw(const FrameContext& context, const RenderTargetView& target, const UiDrawFrame& ui,
              RenderStats* stats);

    RendererServices Services{};
    Logger* Log = nullptr;
    RenderTargetStore Store;
    UiDrawPass Pass;
    std::vector<Bound> Bindings;
    bool WarnedShared = false;
};
