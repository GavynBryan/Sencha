#pragma once

#include <graphics/RenderFeature.h>
#include <graphics/vulkan/PostEffectChain.h>
#include <render/OutputTransformParams.h>
#include <render/post/PostEffectContributions.h>

// Drives the effect chain for the runtime. The frame and params outlive the feature
// and are read each frame.
class PostRenderFeature final : public IRenderFeature
{
public:
    PostRenderFeature(const PostEffectCatalog& catalog, const PostEffectFrame& effects,
                      const OutputTransformParams& output)
        : Catalog(&catalog), Effects(&effects), Output(&output)
    {
    }

    [[nodiscard]] RenderPhase GetPhase() const override { return RenderPhase::Post; }
    [[nodiscard]] bool Setup(const RenderFeatureServices& services) override;
    void OnDraw(const RenderFrame& frame) override;
    void Teardown() override;

    [[nodiscard]] std::size_t ResidentPlaneCount() const { return Chain.ResidentPlaneCount(); }

private:
    const PostEffectCatalog* Catalog = nullptr;
    const PostEffectFrame* Effects = nullptr;
    const OutputTransformParams* Output = nullptr;
    PostEffectChain Chain;
};
