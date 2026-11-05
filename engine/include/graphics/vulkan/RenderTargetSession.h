#pragma once

#include <graphics/vulkan/VulkanBarriers.h>
#include <vulkan/vulkan.h>

// What last touched a colour plane. A target consumed within the frame has one
// image across every frame in flight, so its next write names these in the barrier
// that orders it after them. Docs: docs/renderer/resources.md.
struct RenderTargetAccess
{
    VkImageLayout Layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkPipelineStageFlags2 Stage = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2 Access = VK_ACCESS_2_NONE;
};

// The two barriers a WithinFrame target lives by, and what they leave behind. Their
// correctness is an argument about submission order that no validation layer checks
// here, so it is stated in docs/renderer/resources.md and pinned by tests.
[[nodiscard]] constexpr VulkanBarriers::ImageTransition WithinFrameWriteBarrier(VkImage color,
                                                                                const RenderTargetAccess& last)
{
    VulkanBarriers::ImageTransition barrier{};
    barrier.Image = color;
    barrier.OldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.NewLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.SrcStage = last.Stage;
    barrier.DstStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    barrier.SrcAccess = last.Access;
    barrier.DstAccess = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.AspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    return barrier;
}

[[nodiscard]] constexpr VulkanBarriers::ImageTransition WithinFrameReadBarrier(VkImage color)
{
    VulkanBarriers::ImageTransition barrier{};
    barrier.Image = color;
    barrier.OldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.NewLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.SrcStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    barrier.DstStage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    barrier.SrcAccess = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.DstAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    barrier.AspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    return barrier;
}

inline constexpr RenderTargetAccess kAccessAfterWithinFrameRead{ VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                                                 VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                                                 VK_ACCESS_2_SHADER_SAMPLED_READ_BIT };

//=============================================================================
// RenderTargetSession
//
// Brackets recording into an offscreen target: transitions its planes into
// attachment layouts on construction, back to sampled on End (or destruction),
// and commits the layout the store remembers.
//
// The store owns the images and the layout it remembers, but Acquire hands out
// a pointer to that layout and leaves the caller to write it after recording
// its own barriers. Every consumer implemented the same four steps by hand, and
// a caller that got the barrier right but forgot the write left the store
// asserting a layout the command stream never produced -- which is not a
// crash but two silent failures: the next frame's transition reads a false
// OldLayout, and the ImGui presenter refuses to display a target it believes
// was never rendered.
//
// Deliberately narrow. It does not open a rendering scope, choose load ops,
// know pass order, or hide formats -- those are the caller's, and barrier
// granularity beyond this bracket stays pass policy.
//=============================================================================
class RenderTargetSession
{
public:
    // `layout` is the remembered layout the color plane is in and the one this
    // commits at End. `depth`, when given, transitions from UNDEFINED -- its
    // contents never survive a pass. A single-plane session (no depth) is what
    // the bloom chain uses, where each pass's destination becomes the next
    // pass's sampled source.
    //
    // `depthFormat` decides the aspect and layout the depth barrier uses. A
    // combined depth/stencil image must be transitioned with both aspects and
    // into the combined layout; barriering one as depth-only is a validation
    // error and, on some drivers, corruption rather than a warning. Defaulting
    // to UNDEFINED keeps a caller with a depth-only target saying nothing.
    RenderTargetSession(VkCommandBuffer cmd, VkImage color, VkImageLayout* layout,
                        VkImage depth = VK_NULL_HANDLE,
                        VkFormat depthFormat = VK_FORMAT_UNDEFINED);

    // A target consumed within the frame: its contents are discarded, and the
    // write is ordered after whatever `access` says last touched it.
    RenderTargetSession(VkCommandBuffer cmd, VkImage color, RenderTargetAccess& access);

    ~RenderTargetSession() { End(); }

    RenderTargetSession(const RenderTargetSession&) = delete;
    RenderTargetSession& operator=(const RenderTargetSession&) = delete;
    RenderTargetSession(RenderTargetSession&&) = delete;
    RenderTargetSession& operator=(RenderTargetSession&&) = delete;

    // Transitions the color plane to SHADER_READ_ONLY and commits that to the
    // remembered layout. Idempotent; the destructor calls it.
    void End();

private:
    VkCommandBuffer Cmd = VK_NULL_HANDLE;
    VkImage Color = VK_NULL_HANDLE;
    VkImageLayout* Layout = nullptr;
    RenderTargetAccess* LastAccess = nullptr;
    bool Ended = false;
};
