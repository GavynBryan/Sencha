#pragma once

#include <graphics/BufferHandle.h>
#include <graphics/vulkan/PipelineVariantSet.h>
#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/VulkanShaderCache.h>
#include <math/Mat.h>
#include <math/Vec.h>

#include <cstdint>
#include <span>
#include <vector>
#include <vulkan/vulkan.h>

//=============================================================================
// ProjectedShadowProjectPass
//
// Applies grounding silhouettes to the scene: re-draws each caster's nearby
// receivers with a projective fragment shader that samples the caster's
// silhouette tile and darkens multiplicatively. Depth-tested LESS_OR_EQUAL
// against the opaque depth already in the attachment, depth writes off --
// the receiver geometry is literally the geometry already there, drawn with
// identical position math, so the test lands on the surface.
//
// Runs inside the host's open rendering scope, after opaque and before
// transparent (a shadow multiplied onto glass is paint, not shadow). Pure
// backend: buffer handles, matrices, and a bindless index in; who receives,
// how far shadows reach, and what the darkness is are the frame policy's
// decisions.
//=============================================================================

// std140 twin of the shader's ProjectedShadowFrame block, one per caster per
// view, bound through the frame set's dynamic offset.
struct ProjectedShadowProjectUniform
{
    Mat4 CameraViewProjection;
    Mat4 ShadowViewProjection;
    Vec4 TileScaleBias;
    // x darkness, y fade start (in shadow-depth [0,1]), z bindless index as a
    // float (exact for every index the 1024-slot table can hold), w unused.
    Vec4 Params;
};

struct ProjectedReceiverDraw
{
    BufferHandle Vertex;
    BufferHandle Index;
    std::uint32_t IndexCount = 0;
    std::uint32_t IndexOffset = 0;
    Mat4 World;
};

// One caster's projection: its uniform block and the receivers it re-draws.
// Plain integers for the scissor rather than VkRect2D, so render-domain code
// can assemble this without naming a Vulkan type.
struct ProjectedShadowProjection
{
    ProjectedShadowProjectUniform Uniform;
    std::uint32_t FirstReceiver = 0;
    std::uint32_t ReceiverCount = 0;
    // Screen-space scissor bounding the shadow's swept volume in this view;
    // fragments outside it cannot be darkened, so they are never shaded.
    // Width zero skips the caster for this view entirely.
    std::int32_t ScissorX = 0;
    std::int32_t ScissorY = 0;
    std::uint32_t ScissorWidth = 0;
    std::uint32_t ScissorHeight = 0;
};

// Everything one frame's projections need, built once by whoever ran the
// frame policy and consumed per view. The spans in
// ProjectedShadowProjectionInput point into these vectors.
struct ProjectedShadowFrameData
{
    bool Ready = false;
    std::uint32_t VertexStride = 0;
    std::vector<ProjectedShadowProjection> Casters;
    std::vector<ProjectedReceiverDraw> Receivers;

    void Reset()
    {
        Ready = false;
        Casters.clear();
        Receivers.clear();
    }
};

struct ProjectedShadowProjectionInput
{
    std::uint32_t VertexStride = 0;
    std::span<const ProjectedShadowProjection> Casters;
    std::span<const ProjectedReceiverDraw> Receivers;
};

class ProjectedShadowProjectPass
{
public:
    void Setup(const RendererServices& services);
    void Teardown();

    // Records the projection draws into the host's open scope. The caller has
    // already filled each projection's CameraViewProjection for this view.
    void Draw(const FrameContext& frame, const ProjectedShadowProjectionInput& input);

private:
    [[nodiscard]] bool EnsurePipeline(const FrameContext& frame);

    RendererServices Services{};
    ShaderHandle VertexShader;
    ShaderHandle FragmentShader;
    VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
    std::uint32_t VertexStride = 0;
    PipelineVariantSet<1, AttachmentFormatKey> Pipeline;
};
