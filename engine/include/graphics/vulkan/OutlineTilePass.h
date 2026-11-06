#pragma once

#include <graphics/vulkan/FullscreenPass.h>
#include <graphics/vulkan/PipelineVariantSet.h>
#include <graphics/vulkan/RenderTargetStore.h>
#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/VulkanShaderCache.h>

#include <cstdint>

inline constexpr std::uint32_t kOutlineTileSize = 8;
inline constexpr VkFormat kOutlineTileFormat = VK_FORMAT_R8_UNORM;

// A frame's extent in outline tiles.
[[nodiscard]] VkExtent2D OutlineTileExtent(VkExtent2D frame);

// The two tile images the classification writes, each OutlineTileExtent of the mask.
struct OutlineTileTargets
{
    RenderTargetView Marked;
    RenderTargetView Near;
};

// Marks the tiles of a frame the outline composite has work in, from the mask, so
// the composite's cost follows the outline's length rather than its scissor's area.
// Docs: docs/renderer/outlines.md.
class OutlineTilePass
{
public:
    [[nodiscard]] bool Setup(const RendererServices& services);
    void Teardown();

    // After the mask holds this frame's outlines, outside any rendering scope.
    // `scissor` is the composite's, in frame pixels. Returns the bindless index of
    // the tiles the composite reads, UINT32_MAX when it could not record.
    [[nodiscard]] std::uint32_t Record(const FrameContext& frame, std::uint32_t maskBindless,
                                       const OutlineTileTargets& targets, VkRect2D scissor, float maxWidthPixels);

private:
    VulkanShaderCache* Shaders = nullptr;
    ShaderHandle FragmentShader;
    FullscreenPass Fullscreen;
    // Marking, then growing.
    PipelineVariantSet<2, AttachmentFormatKey> Pipeline;
};
