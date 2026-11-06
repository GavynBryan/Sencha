#pragma once

#include <graphics/vulkan/FullscreenPass.h>
#include <graphics/vulkan/OutlineCompositePass.h>
#include <graphics/vulkan/OutputTransformPass.h>
#include <graphics/vulkan/RenderTargetStore.h>
#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/VulkanShaderCache.h>
#include <render/OutputTransformParams.h>
#include <render/post/PostEffectContributions.h>

#include <cstdint>
#include <vector>

// Runs a frame's post effects over two within-frame planes, with the output
// transform between the scene and display domains. A linear list by design: the
// inputs are the fixed set in post_effect.glsli. When display effects run, the
// frame's outlines are drawn after the transform and before them, so a fade covers
// them too. Docs: docs/renderer/post-effects.md.
class PostEffectChain
{
public:
    [[nodiscard]] bool Setup(const RendererServices& services, const PostEffectCatalog& catalog);
    void Teardown();

    // After the scene and outside any scope. Publishes what it ended on through
    // frame.ScenePost; an empty frame records nothing and publishes nothing.
    void Record(const FrameContext& frame, const PostEffectFrame& effects, const OutputTransformParams& output);

    [[nodiscard]] std::size_t ResidentPlaneCount() const;

private:
    struct CompiledEffect
    {
        PostEffectId Effect;
        ShaderHandle Fragment;
        VkPipeline Pipeline = VK_NULL_HANDLE;
    };

    [[nodiscard]] const CompiledEffect* Compile(PostEffectId effect);

    const PostEffectCatalog* Catalog = nullptr;
    VulkanShaderCache* Shaders = nullptr;
    FullscreenPass Fullscreen;
    OutputTransformPass Transform;
    OutlineCompositePass Outlines;
    RenderTargetStore Planes;
    RenderTargetId Plane[2];
    std::vector<CompiledEffect> Compiled;
    std::uint32_t QuietFrames = 0;
};
