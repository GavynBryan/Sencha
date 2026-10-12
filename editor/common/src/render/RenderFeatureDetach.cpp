#include "render/RenderFeatureDetach.h"

#include <app/Engine.h>
#include <graphics/vulkan/GraphicsServices.h>
#include <graphics/vulkan/Renderer.h>

#include <cstdio>
#include <cstdlib>
#include <utility>
#include <vector>

IRenderFeature* AttachRenderFeature(Engine& engine, std::unique_ptr<IRenderFeature> feature, std::string_view id)
{
    Renderer& renderer = engine.Graphics().MainRenderer;
    IRenderFeature* staged = renderer.StageFeature(std::move(feature), FeatureRegistration{ .Id = id });
    std::vector<std::string_view> failed;
    if (!renderer.CommitStagedFeatures(&failed) || !failed.empty())
        return nullptr;
    return staged;
}

void DetachRenderFeature(Engine& engine, IRenderFeature*& feature)
{
    if (feature == nullptr)
        return;
    GraphicsServices* graphics = engine.TryGraphics();
    if (graphics != nullptr && !graphics->MainRenderer.RemoveFeature(feature))
    {
        std::fprintf(stderr, "[editor] a render feature could not be removed while the state it "
                             "borrows is being destroyed\n");
        std::abort();
    }
    feature = nullptr;
}
