#pragma once

#include <graphics/vulkan/PipelineVariantSet.h>
#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/VulkanShaderCache.h>
#include <math/Mat.h>
#include <math/Vec.h>

//=============================================================================
// SkyGradientPass
//
// Fills the view with a vertical gradient before anything else draws into it,
// shaded from the direction each pixel looks along rather than from its screen
// position. It is the same hemisphere the forward pass lights surfaces with, so
// the background agrees with the ambient term by construction instead of
// through a second set of colours somebody has to keep in sync.
//
// Takes a matrix and two colours and nothing else. It does not know about
// cameras, light sets, cvars, or scenes -- which is what keeps it in the
// backend, and what lets the values come from somewhere else later (an authored
// environment record, a zone) without the pass moving.
//
// Drawn first with depth testing off, in every host, rather than last against
// cleared depth. Testing would skip the pixels geometry already covers, but it
// would make correctness depend on each host's draw order, and the editor
// viewport interleaves a backdrop, a grid, bodies, and overlays whose depth
// behaviour differs. The two hosts agreeing is the point; one full-screen fill
// of a ten-instruction shader is the price.
//=============================================================================

struct SkyGradientParams
{
    // Linear RGB. The swapchain is sRGB and encodes on write, so these are not
    // pre-brightened.
    Vec<3> Top;
    Vec<3> Bottom;
};

// Mirrored by the push block in sky_gradient.frag.glsl; offsets are asserted
// against it in the .cpp.
struct SkyPushConstants
{
    Mat4 InverseViewProjection;
    Vec4 Top;
    Vec4 Bottom;
};

class SkyGradientPass
{
public:
    void Setup(const RendererServices& services);

    // `inverseViewProjection` maps clip space back to a world direction; build
    // it with MakeInverseSkyViewProjection so the view translation is stripped
    // and the gradient does not slide with the camera.
    void Draw(const FrameContext& frame,
              const Mat4& inverseViewProjection,
              const SkyGradientParams& sky);

    void Teardown();

private:
    // Compiles the pipeline for whatever `frame` renders into. Separate from
    // Draw so Setup can pay the compile at load rather than on the first
    // visible frame.
    [[nodiscard]] bool EnsurePipeline(const FrameContext& frame);

    VulkanPipelineCache* Pipelines = nullptr;
    VulkanShaderCache* Shaders = nullptr;
    VkDevice Device = VK_NULL_HANDLE;

    ShaderHandle VertexShader;
    ShaderHandle FragmentShader;
    // No descriptor sets: the whole input fits in push constants, so the pass
    // depends on neither the frame uniform nor the lighting bindings.
    VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
    PipelineVariantSet<1, AttachmentFormatKey> Pipeline;
};
