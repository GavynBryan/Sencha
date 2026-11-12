#pragma once

#include "viewport/ViewportId.h"

#include <graphics/GpuFrameRetirement.h>
#include <graphics/vulkan/PostEffectChain.h>
#include <render/OutputTransformParams.h>
#include <render/PresentationSettings.h>
#include <render/post/PostEffectCatalog.h>

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

// The game's post effects over each viewport's scene, through the engine's own
// chain, so a view previews what Play shows. One chain per viewport keeps each
// view's planes at its own size; a chain whose viewport went is torn down once
// the GPU has finished with it.
class ViewportPostEffects
{
public:
    [[nodiscard]] bool Setup(const RendererServices& services);
    void Teardown();

    // After the scene and outside any scope, as PostEffectChain::Record.
    void Record(const FrameContext& frame, ViewportId viewport, const PresentationSettings& settings,
                const OutputTransformParams& output);
    void Prune(std::span<const ViewportId> live, const GpuFrameRetirement& retirement);

private:
    struct Viewport
    {
        ViewportId Id;
        std::unique_ptr<PostEffectChain> Chain;
    };
    struct Retiring
    {
        std::unique_ptr<PostEffectChain> Chain;
        std::uint64_t Stamp = 0;
    };

    RendererServices Services;
    PostEffectCatalog Catalog;
    PostEffectId Bloom;
    std::vector<Viewport> Chains;
    std::vector<Retiring> Retired;
    bool Ready = false;
};
