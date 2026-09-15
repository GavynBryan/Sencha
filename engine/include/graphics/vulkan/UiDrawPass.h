#pragma once

#include <graphics/RenderFeature.h>
#include <graphics/ShaderHandle.h>
#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/VulkanDescriptorCache.h>
#include <render/ui/UiDrawFrame.h>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

class TextureCache;

//=============================================================================
// UiDrawPass
//
// Turns a recorded UiDrawFrame into draw commands.
//
// Takes plain render-domain data and names no render-domain type of its own,
// which is what lets the feature that owns it hold it by value without the
// recording set growing a fourth member -- the SkyGradientPass precedent
// (cmake/CheckRenderIsolation.cmake).
//
// It owns the GPU side of generated textures. The runtime hands over pixels and
// an id; this creates the image, registers a bindless slot, and retires both
// through the frame clock when the document is done with them. That division is
// the point: the document engine produces presentation resources, and the
// renderer is the only thing that creates GPU ones.
//=============================================================================
class UiDrawPass
{
public:
    [[nodiscard]] bool Setup(const RendererServices& services, TextureCache* textures);
    void Teardown();

    // Records one surface's frame. Geometry goes through per-frame scratch:
    // measure before caching anything, because a UI that fits in the ring costs
    // one memcpy and a persistent cache would cost invalidation logic forever.
    void Draw(const FrameContext& frame, const UiDrawFrame& ui);

    // What the last Draw actually submitted. Published by the feature rather
    // than written here, the same way the forward pass reports its own totals:
    // a pass does not know whether anybody is counting.
    struct DrawStats
    {
        std::uint32_t DrawCalls = 0;
        std::uint32_t Triangles = 0;
        // Generated textures materialized this frame -- font atlases and
        // decorator images. Steady state is zero; a number that stays nonzero
        // is a document re-minting what it already had.
        std::uint32_t TextureUploads = 0;
        std::uint64_t TextureUploadBytes = 0;
    };
    [[nodiscard]] const DrawStats& GetLastDrawStats() const { return LastStats; }

private:
    DrawStats LastStats;

    struct GeneratedTexture
    {
        ImageHandle Image{};
        BindlessImageIndex Bindless{};
        RenderExtent Size{};
    };

    // Kept alive until the frames that referenced them have retired, then
    // destroyed. A texture released mid-flight is still named by command
    // buffers that have not finished.
    struct RetiredTexture
    {
        GeneratedTexture Texture;
        std::uint64_t RetireStamp = 0;
    };

    // Which pipeline a command needs. Four rather than one because the clip
    // mask is pipeline state: writing the mask means colour off and a stencil
    // op, testing against it means colour on and a stencil compare.
    enum class Variant : std::uint8_t
    {
        Colour = 0,       // no clip mask in effect
        ColourMasked,     // colour, tested against the current mask
        MaskReplace,      // writes the mask: Set and SetInverse
        MaskIncrement,    // narrows the mask: Intersect
        Count,
    };

    [[nodiscard]] VkPipeline EnsurePipeline(const FrameContext& frame, Variant variant);
    void ClearStencil(const FrameContext& frame, const UiDrawFrame& ui, std::uint32_t value);
    void ApplyUploads(const UiDrawFrame& ui);
    void ApplyReleases(const UiDrawFrame& ui, const FrameContext& frame);
    void CollectRetired(const FrameContext& frame);

    const RendererServices* Services = nullptr;
    TextureCache* Textures = nullptr;

    VkDevice Device = VK_NULL_HANDLE;
    VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
    VkPipeline Pipelines[static_cast<std::size_t>(Variant::Count)]{};
    VkFormat PipelineColorFormat = VK_FORMAT_UNDEFINED;
    // Reported once rather than per frame: a device with no stencil aspect
    // draws every frame, and the log would become the failure.
    bool WarnedNoStencil = false;
    ShaderHandle VertexShader{};
    ShaderHandle FragmentShader{};
    VkSampler Sampler = VK_NULL_HANDLE;

    std::unordered_map<UiGeneratedTextureId, GeneratedTexture> Generated;
    std::vector<RetiredTexture> Retiring;
};
