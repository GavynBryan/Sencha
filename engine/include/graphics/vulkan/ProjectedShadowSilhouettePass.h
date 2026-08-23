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
// rendered into the tile matching its position in the caster order. The
// occluder range is the caster's receiver set rendered depth-only into the
// occluder tile -- the same surfaces that can catch the shadow are the ones
// that block it.
struct ProjectedSilhouetteCasterDraw
{
    Mat4 Mvp;
    std::uint32_t FirstSection = 0;
    std::uint32_t SectionCount = 0;
    std::uint32_t FirstOccluder = 0;
    std::uint32_t OccluderCount = 0;
};

// One receiver rendered as an occluder into a caster's depth tile, with its
// light-space MVP precomputed by the assembler.
struct ProjectedSilhouetteOccluderDraw
{
    BufferHandle Vertex;
    BufferHandle Index;
    std::uint32_t IndexCount = 0;
    std::uint32_t IndexOffset = 0;
    Mat4 Mvp;
};

struct ProjectedSilhouetteInput
{
    std::uint32_t TilesPerRow = 1;
    std::uint32_t TilePixels = 256;
    // Separable-blur reach in atlas texels, applied to the rendered tiles
    // before projection samples them; 0 leaves the coverage sharp.
    float SoftnessTexels = 3.0f;
    std::span<const ProjectedSilhouetteCasterDraw> Casters;
    std::span<const ProjectedSilhouetteSectionDraw> Sections;
    std::span<const ProjectedSilhouetteOccluderDraw> Occluders;
};

class ProjectedShadowSilhouettePass
{
public:
    // `vertexStride` is the stride of the vertex layout every caster buffer
    // uses (the caller's geometry vertex size); passed in because the backend
    // must not include render-domain geometry headers.
    void Setup(const RendererServices& services, std::uint32_t vertexStride);
    void Teardown();

    // Renders every caster tile, softens the coverage by the input's blur
    // reach, and leaves the atlas SHADER_READ_ONLY. Returns false when the
    // atlas could not be built this frame (capture consumers then skip
    // projection rather than sampling garbage).
    bool Draw(const FrameContext& frame, const ProjectedSilhouetteInput& input);

    // The bindless slot of this frame's atlas; UINT32_MAX until a Draw has
    // built it. Valid for the frame Draw ran in.
    [[nodiscard]] std::uint32_t AtlasBindlessIndex() const { return BindlessIndex; }

    // The occluder-depth atlas for the same tiles: per texel, the nearest
    // receiver depth along the shadow ray, 1.0 where nothing blocks.
    // UINT32_MAX when the frame produced none (no occluders, or the format
    // cannot blend on this device); projection then skips the occlusion term.
    [[nodiscard]] std::uint32_t OccluderBindlessIndex() const { return OccluderIndex; }

private:
    [[nodiscard]] bool EnsurePipeline(VkFormat colorFormat);

    // Ping target for the separable blur: pass one reads the atlas into it,
    // pass two reads it back into the atlas, so the reported bindless index
    // is the atlas's whether or not the frame blurred.
    [[nodiscard]] bool BlurAtlas(const FrameContext& frame,
                                 const RenderTargetView& atlas,
                                 VkExtent2D extent,
                                 float softnessTexels);

    [[nodiscard]] bool EnsureOccluderPipeline();
    [[nodiscard]] bool DrawOccluders(const FrameContext& frame,
                                     const ProjectedSilhouetteInput& input,
                                     VkExtent2D extent);

    RendererServices Services{};
    RenderTargetStore Store;
    RenderTargetId Atlas;
    RenderTargetId BlurScratch;
    RenderTargetId OccluderAtlas;
    std::uint32_t BindlessIndex = UINT32_MAX;
    std::uint32_t OccluderIndex = UINT32_MAX;
    bool OccluderBlendCapable = false;

    ShaderHandle VertexShader;
    ShaderHandle FragmentShader;
    VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
    ShaderHandle BlurVertexShader;
    ShaderHandle BlurFragmentShader;
    VkPipelineLayout BlurPipelineLayout = VK_NULL_HANDLE;
    ShaderHandle OccluderVertexShader;
    ShaderHandle OccluderFragmentShader;
    VkPipelineLayout OccluderPipelineLayout = VK_NULL_HANDLE;
    std::uint32_t VertexStride = 0;
    PipelineVariantSet<1, AttachmentFormatKey> Pipeline;
    PipelineVariantSet<1, AttachmentFormatKey> BlurPipeline;
    PipelineVariantSet<1, AttachmentFormatKey> OccluderPipeline;

    [[nodiscard]] bool EnsureBlurPipeline(VkFormat colorFormat);
};
