#pragma once

#include <graphics/BufferHandle.h>
#include <graphics/vulkan/PipelineVariantSet.h>
#include <graphics/vulkan/RenderTargetStore.h>
#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/VulkanShaderCache.h>
#include <math/Mat.h>

#include <cstdint>
#include <span>
#include <vector>
#include <vulkan/vulkan.h>

//=============================================================================
// ProjectedShadowSilhouettePass
//
// Renders each grounding caster's silhouette into its tile of one small R8
// atlas. Pure backend: it takes buffer handles, matrices, and tile geometry,
// and knows nothing about components, casters, or how the tiles were
// assigned -- the frame policy in render/ decides all of that.
//
// Owns the atlas through a private RenderTargetStore (the store's first
// consumer outside the editor): per-frame-in-flight slots, bindless
// registration so the projection shader samples it by index, retirement-safe
// destruction. One store per pass rather than one shared engine store on
// purpose -- a second game-side consumer is the recorded trigger for
// promoting ownership, and it has not appeared.
//=============================================================================

// One section of one caster: geometry only, resolved by the caller.
struct ProjectedSilhouetteSectionDraw
{
    BufferHandle Vertex;
    BufferHandle Index;
    std::uint32_t IndexCount = 0;
    std::uint32_t IndexOffset = 0;
};

// One caster: its light-space MVP and the range of section draws it owns,
// rendered into the tile matching its position in the caster order.
struct ProjectedSilhouetteCasterDraw
{
    Mat4 Mvp;
    std::uint32_t FirstSection = 0;
    std::uint32_t SectionCount = 0;
};

struct ProjectedSilhouetteInput
{
    std::uint32_t TilesPerRow = 1;
    std::uint32_t TilePixels = 128;
    std::span<const ProjectedSilhouetteCasterDraw> Casters;
    std::span<const ProjectedSilhouetteSectionDraw> Sections;
};

class ProjectedShadowSilhouettePass
{
public:
    // `vertexStride` is the stride of the vertex layout every caster buffer
    // uses (the caller's geometry vertex size); passed in because the backend
    // must not include render-domain geometry headers.
    void Setup(const RendererServices& services, std::uint32_t vertexStride);
    void Teardown();

    // Renders every caster tile and leaves the atlas SHADER_READ_ONLY.
    // Returns false when the atlas could not be built this frame (capture
    // consumers then skip projection rather than sampling garbage).
    bool Draw(const FrameContext& frame, const ProjectedSilhouetteInput& input);

    // The bindless slot of this frame's atlas; UINT32_MAX until a Draw has
    // built it. Valid for the frame Draw ran in.
    [[nodiscard]] std::uint32_t AtlasBindlessIndex() const { return BindlessIndex; }

private:
    [[nodiscard]] bool EnsurePipeline(VkFormat colorFormat);

    RendererServices Services{};
    RenderTargetStore Store;
    RenderTargetId Atlas;
    std::uint32_t BindlessIndex = UINT32_MAX;

    ShaderHandle VertexShader;
    ShaderHandle FragmentShader;
    VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
    std::uint32_t VertexStride = 0;
    PipelineVariantSet<1, AttachmentFormatKey> Pipeline;
};
