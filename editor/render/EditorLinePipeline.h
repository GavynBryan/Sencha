#pragma once

#include "../viewport/EditorViewport.h"

#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/VulkanShaderCache.h>

#include <math/Mat.h>
#include <math/Vec.h>

#include <span>

class VulkanBufferService;
class VulkanFrameScratch;
class VulkanPipelineCache;
class VulkanShaderCache;

// One world-space colored-line vertex. The single vertex format every editor
// overlay (brush wireframe, selection highlight, manipulators, component visuals)
// draws with.
struct EditorLineVertex
{
    Vec3d Position;
    Vec4 Color;
};

// The one line pipeline shared by all editor overlay renderers. Owns the editor
// line shaders, layout, and format-cached pipeline, and submits a vertex span for
// a viewport. Producers just gather EditorLineVertex lists and call Submit —
// collapsing what used to be an identical pipeline copied into three renderers.
// (docs/architecture/hardening-and-consolidation.md W5.)
class EditorLinePipeline
{
public:
    void Setup(const RendererServices& services);
    // onTop = true draws without depth-testing, so manipulators/selection are
    // visible through geometry; false depth-tests against the scene.
    void Submit(const FrameContext& frame,
                const EditorViewport& viewport,
                std::span<const EditorLineVertex> vertices,
                bool onTop = false);
    void Teardown();

private:
    struct PushConstants
    {
        Mat4 ViewProjection;
    };

    [[nodiscard]] VkPipeline EnsurePipeline(const FrameContext& frame, bool onTop);

    VulkanBufferService* Buffers = nullptr;
    VulkanShaderCache* Shaders = nullptr;
    VulkanPipelineCache* Pipelines = nullptr;
    VulkanFrameScratch* Scratch = nullptr;
    VkDevice Device = VK_NULL_HANDLE;
    ShaderHandle VertexShader;
    ShaderHandle FragmentShader;
    VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
    VkPipeline Pipeline = VK_NULL_HANDLE;       // depth-tested
    VkPipeline PipelineOnTop = VK_NULL_HANDLE;  // depth-test disabled
    VkFormat CachedColor = VK_FORMAT_UNDEFINED;
    VkFormat CachedDepth = VK_FORMAT_UNDEFINED;
};
