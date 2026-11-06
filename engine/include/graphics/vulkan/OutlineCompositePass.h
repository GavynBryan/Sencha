#pragma once

#include <graphics/vulkan/FullscreenPass.h>
#include <graphics/vulkan/PipelineVariantSet.h>
#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/VulkanShaderCache.h>

// The outer outline, drawn over a frame that already holds the scene: the ring each
// style draws past the objects the mask holds. Docs: docs/renderer/outlines.md.
class OutlineCompositePass
{
public:
    [[nodiscard]] bool Setup(const RendererServices& services);
    void Teardown();

    // Inside the target's rendering scope. Records nothing while the frame has no
    // outlines.
    void Draw(const FrameContext& frame);

private:
    [[nodiscard]] bool EnsurePipeline(const FrameContext& frame);

    VulkanShaderCache* Shaders = nullptr;
    ShaderHandle FragmentShader;
    FullscreenPass Fullscreen;
    PipelineVariantSet<1, AttachmentFormatKey> Pipeline;
};
