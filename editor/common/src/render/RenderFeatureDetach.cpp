#include "render/RenderFeatureDetach.h"

#include <app/Engine.h>
#include <graphics/vulkan/GraphicsServices.h>
#include <graphics/vulkan/Renderer.h>

#include <cstdio>
#include <cstdlib>
#include <utility>

IRenderFeature* AttachRenderFeature(Engine& engine, std::unique_ptr<IRenderFeature> feature, std::string_view id)
{
    return engine.Graphics().MainRenderer.AddFeature(std::move(feature), FeatureRegistration{ .Id = id });
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
