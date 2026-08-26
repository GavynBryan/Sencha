#pragma once

#include <graphics/vulkan/Renderer.h>
#include <vulkan/vulkan.h>

//=============================================================================
// RenderScope
//
// One dynamic-rendering scope: the attachment structs, vkCmdBeginRendering, and
// the matching vkCmdEndRendering as an RAII pair.
//
// Six sites hand-wrote this. The attachment and VkRenderingInfo fill is thirty
// lines of the same shape each time, and three of the six then hand-built a
// FrameContext for whatever they called inside -- which is the part that
// actually breaks. A hand-built context silently zeroes any field added to
// FrameContext later, and that has already happened once: the fence-anchored
// retirement clock landed and two of those three would have handed their passes
// a clock reading zero, meaning "nothing has ever retired". Context() derives
// the inner context from the outer one, so a new field reaches every nested
// pass without visiting three call sites.
//
// Scope, deliberately narrow: attachments, area, begin, end, and the context.
// Barriers stay with the pass. The six sites transition genuinely different
// things -- a swapchain image, an ImGui-sampled offscreen colour target, a
// shadow atlas transitioned once around all its views, a bloom target that is
// the next sub-pass's sampled source -- and the granularity of those
// transitions is pass policy, not scope boilerplate.
//
// Viewport and scissor are also left to the caller: passes that draw one
// full-target quad and passes that draw into an atlas tile want different
// answers, and both are one call.
//=============================================================================

struct RenderScopeAttachment
{
    VkImageView View = VK_NULL_HANDLE;
    VkAttachmentLoadOp LoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    VkAttachmentStoreOp StoreOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkClearValue Clear{};

    [[nodiscard]] bool IsPresent() const { return View != VK_NULL_HANDLE; }
};

struct RenderScopeDesc
{
    // The scope's render area. Offscreen targets start at the origin; a shadow
    // view is a tile inside its atlas, so the offset is not always zero.
    VkRect2D Area{};

    RenderScopeAttachment Color;
    RenderScopeAttachment Depth;

    // Reported through Context() so pipelines keyed on attachment format pick
    // the right variant. Not derivable from the views.
    VkFormat ColorFormat = VK_FORMAT_UNDEFINED;
    VkFormat DepthFormat = VK_FORMAT_UNDEFINED;

    RenderPhase Phase = RenderPhase::Offscreen;
};

// The inner context a scope publishes, split out so the derivation can be
// tested without a device. This is the part that has actually broken: a
// hand-built context zeroes whatever it forgets, and the fields it must carry
// through from the outer frame grow over time.
[[nodiscard]] FrameContext MakeScopeContext(const FrameContext& outer,
                                            const RenderScopeDesc& desc);

class RenderScope
{
public:
    // Begins the scope on `frame.Cmd`. The scope holds no ownership beyond the
    // command buffer's recording state.
    RenderScope(const FrameContext& frame, const RenderScopeDesc& desc);
    ~RenderScope();

    RenderScope(const RenderScope&) = delete;
    RenderScope& operator=(const RenderScope&) = delete;
    RenderScope(RenderScope&&) = delete;
    RenderScope& operator=(RenderScope&&) = delete;

    // The frame context describing what is bound *inside* this scope: the
    // outer frame's command buffer, slot index, and retirement clock, with the
    // target extent, formats, depth view, and phase taken from the descriptor.
    [[nodiscard]] const FrameContext& Context() const { return Inner; }

private:
    VkCommandBuffer Cmd = VK_NULL_HANDLE;
    FrameContext Inner{};
};
