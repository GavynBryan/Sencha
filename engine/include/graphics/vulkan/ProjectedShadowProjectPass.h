#pragma once

#include <graphics/BufferHandle.h>
#include <graphics/vulkan/PipelineVariantSet.h>
#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/RenderTargetStore.h>
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
// Applies grounding silhouettes to the scene through a screen-space mask so
// overlapping casters resolve to their union rather than multiplying:
//
//   mask[p]  = max over casters of (silhouette * fade * inside) at p
//   scene[p] *= 1 - darkness * mask[p]
//
// DrawMask re-draws each caster's nearby receivers into a private R8 mask
// target with blend op MAX, depth-tested LESS_OR_EQUAL read-only against the
// host's opaque depth -- the receiver geometry is literally the geometry
// already there, drawn with identical position math, so the test lands on
// the surface. It runs while the host's rendering instance is suspended
// (RenderScopeInterruption), after opaque. Composite then runs inside the
// resumed instance, before transparent (a shadow multiplied onto glass is
// paint, not shadow): one draw scissored to the union of the casters' screen
// rects, applying darkness exactly once.
//
// The mask target may be larger than the view (the editor sizes one shared
// target to its largest viewport); every view renders at the origin, so the
// composite maps view UVs with a scale and never an offset.
//
// Pure backend: buffer handles, matrices, and bindless indices in; who
// receives, how far shadows reach, and what the darkness is are the frame
// policy's decisions. Owns its own image transitions; the suspend/resume
// dependencies belong to RenderScopeInterruption.
//=============================================================================

// std140 twin of the shader's ProjectedShadowFrame block, one per caster per
// view, bound through the frame set's dynamic offset.
struct ProjectedShadowProjectUniform
{
    Mat4 CameraViewProjection;
    Mat4 ShadowViewProjection;
    Vec4 TileScaleBias;
    // x unused, y fade start (in shadow-depth [0,1]), z silhouette bindless
    // index as a float (exact for every index the 1024-slot table can hold),
    // w unused. Darkness deliberately absent: it is applied once by the
    // composite, which is what keeps overlap from multiplying it.
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
    float Darkness = 0.0f;
    // Union of the casters' scissors: the composite's reach this frame.
    std::int32_t UnionX = 0;
    std::int32_t UnionY = 0;
    std::uint32_t UnionWidth = 0;
    std::uint32_t UnionHeight = 0;
    std::vector<ProjectedShadowProjection> Casters;
    std::vector<ProjectedReceiverDraw> Receivers;

    void Reset()
    {
        Ready = false;
        Darkness = 0.0f;
        UnionWidth = 0;
        UnionHeight = 0;
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

    // Mask half. Records the receiver re-draws into the pass's own mask
    // scope; call while the host's instance is suspended. `frame` is the
    // suspended instance's context (its DepthView is tested read-only).
    // `maskExtent` sizes the shared target; the view renders at the origin
    // with frame.TargetExtent. Returns false when no mask was written this
    // view (the composite must then be skipped).
    [[nodiscard]] bool DrawMask(const FrameContext& frame,
                                const ProjectedShadowProjectionInput& input,
                                VkExtent2D maskExtent);

    // Composite half, inside the resumed instance: one draw scissored to the
    // union rect, multiplying the scene by (1 - darkness * mask).
    void Composite(const FrameContext& frame,
                   float darkness,
                   std::int32_t unionX, std::int32_t unionY,
                   std::uint32_t unionWidth, std::uint32_t unionHeight);

private:
    [[nodiscard]] bool EnsureMaskPipeline(VkFormat depthFormat);
    [[nodiscard]] bool EnsureCompositePipeline(const FrameContext& frame);

    RendererServices Services{};
    RenderTargetStore Store;
    RenderTargetId Mask;
    std::uint32_t MaskBindlessIndex = UINT32_MAX;
    // The extent the mask was last written at, for the composite's UV scale.
    VkExtent2D LastMaskExtent{};
    VkExtent2D LastViewExtent{};
    bool BlendCapable = false;

    ShaderHandle VertexShader;
    ShaderHandle FragmentShader;
    VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
    ShaderHandle CompositeVertexShader;
    ShaderHandle CompositeFragmentShader;
    VkPipelineLayout CompositePipelineLayout = VK_NULL_HANDLE;
    std::uint32_t VertexStride = 0;
    PipelineVariantSet<1, AttachmentFormatKey> Pipeline;
    PipelineVariantSet<1, AttachmentFormatKey> CompositePipeline;
};
