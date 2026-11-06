#pragma once

#include <graphics/vulkan/OutlineTilePass.h>
#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/RenderTargetSession.h>
#include <graphics/vulkan/RenderTargetStore.h>
#include <graphics/vulkan/VulkanDescriptorCache.h>
#include <graphics/vulkan/VulkanImageService.h>
#include <render/outline/OutlineContributions.h>

#include <cstdint>
#include <optional>

inline constexpr VkFormat kOutlineMaskFormat = VK_FORMAT_R8_UNORM;

// Where a frame's outline mask draws: its own target, and the scene's depth.
struct OutlineMaskAttachments
{
    VkImage MaskImage = VK_NULL_HANDLE;
    VkImageView MaskView = VK_NULL_HANDLE;
    RenderTargetAccess* MaskAccess = nullptr;
    VkExtent2D Extent{};
    VkImageView DepthView = VK_NULL_HANDLE;
    VkFormat DepthFormat = VK_FORMAT_UNDEFINED;
    VkFormat StencilFormat = VK_FORMAT_UNDEFINED;
};

// Where the composite can land for this camera's screen bounds, in `extent` pixels.
[[nodiscard]] VkRect2D OutlineScissorFor(const OutlineFrame& outlines, VkExtent2D extent);

// The style table where the composite can read it: a small bindless image, so a
// frame with nothing highlighted carries no extra descriptor or push data.
class OutlineStyleImage
{
public:
    void Setup(const RendererServices& services);
    void Teardown();

    // Uploads when `version` moved. UINT32_MAX when the image cannot be made.
    [[nodiscard]] std::uint32_t Update(const OutlineStyleTable& styles, std::uint32_t version);

private:
    RendererServices Services{};
    ImageHandle Image;
    BindlessImageIndex Bindless;
    std::uint32_t UploadedVersion = 0;
};

// One presentation's mask and tile images, held only while something is highlighted,
// and the style table the composite reads.
class OutlineMaskTargets
{
public:
    void Setup(const RendererServices& services);
    void Teardown();

    // A frame with nothing highlighted; the image goes after a quiet interval, so
    // moving between objects does not reallocate it.
    void NoteQuietFrame();

    // Nullopt when this frame has no scene to outline or an image cannot be made.
    [[nodiscard]] std::optional<OutlineMaskAttachments> Begin(const FrameContext& frame, const OutlineFrame& outlines);
    // Marks the tiles with outline work and hands the mask to the output pass, once it
    // holds this frame's outlines.
    void Publish(const FrameContext& frame, const OutlineFrame& outlines);

    [[nodiscard]] bool HoldsMaskImage();

private:
    RenderTargetStore Targets;
    RenderTargetId Mask;
    RenderTargetId MarkedTiles;
    RenderTargetId NearTiles;
    OutlineTileTargets Tiles;
    OutlineTilePass TilePass;
    OutlineStyleImage StyleImage;
    std::uint32_t StyleBindless = UINT32_MAX;
    std::uint32_t MaskBindless = UINT32_MAX;
    std::uint32_t QuietFrames = 0;
};
