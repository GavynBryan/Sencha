#pragma once

#include <graphics/vulkan/FullscreenPass.h>
#include <graphics/vulkan/OutlineCompositePass.h>
#include <graphics/vulkan/PipelineVariantSet.h>
#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/VulkanShaderCache.h>
#include <render/OutputTransformParams.h>

// Carries the scene target into the display target through exposure and the
// shoulder: the draw every host owes after its scene. Docs: docs/renderer/frame.md.
class OutputTransformPass
{
public:
    [[nodiscard]] bool Setup(const RendererServices& services);
    void Teardown();

    // Inside the display target's rendering scope. Records nothing when the
    // frame drew no scene. Then draws the frame's outlines over it, so every host
    // that owes this draw gets them.
    void Draw(const FrameContext& frame, const OutputTransformParams& params);

private:
    [[nodiscard]] bool EnsurePipeline(const FrameContext& frame);

    VulkanShaderCache* Shaders = nullptr;
    ShaderHandle FragmentShader;
    FullscreenPass Fullscreen;
    PipelineVariantSet<1, AttachmentFormatKey> Pipeline;
    OutlineCompositePass Composite;
};
