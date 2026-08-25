#pragma once

#include <vulkan/vulkan.h>

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
    RenderTargetSession(VkCommandBuffer cmd, VkImage color, VkImageLayout* layout,
                        VkImage depth = VK_NULL_HANDLE);

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
    bool Ended = false;
};
