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

private:
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

    [[nodiscard]] bool EnsurePipeline(const FrameContext& frame);
    void ApplyUploads(const UiDrawFrame& ui);
    void ApplyReleases(const UiDrawFrame& ui, const FrameContext& frame);
    void CollectRetired(const FrameContext& frame);

    const RendererServices* Services = nullptr;
    TextureCache* Textures = nullptr;

    VkDevice Device = VK_NULL_HANDLE;
    VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
    VkPipeline Pipeline = VK_NULL_HANDLE;
    VkFormat PipelineColorFormat = VK_FORMAT_UNDEFINED;
    ShaderHandle VertexShader{};
    ShaderHandle FragmentShader{};
    VkSampler Sampler = VK_NULL_HANDLE;

    std::unordered_map<UiGeneratedTextureId, GeneratedTexture> Generated;
    std::vector<RetiredTexture> Retiring;
};
