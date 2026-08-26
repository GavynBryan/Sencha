#pragma once

#include <graphics/GpuFrameRetirement.h>

#include <cstdint>
#include <string>
#include <vulkan/vulkan.h>

// Declared, not included: Renderer.h is where both of these live and it holds a
// capture by value, so including it here would close a cycle. The three things
// capture actually needs out of RendererServices are cached in Setup.
struct RendererServices;
struct FrameContext;
class Logger;
class LoggingProvider;

//=============================================================================
// FrameImageCapture
//
// Writes a presented frame to a PNG on disk.
//
// The renderer's test suite is entirely headless, which is right for policy and
// structurally blind to the thing a renderer produces. Every defect of one
// shape -- a feature wired everywhere except the shader, two paths that should
// agree and quietly do not, a pass that draws the right geometry through the
// wrong state -- leaves every counter correct and every assertion green, and is
// visible only in the image. Those were found by a person looking at captures,
// which means they were found at the rate someone happened to look.
//
// So this is the missing detector rather than a convenience: with it, a scene
// rendered today can be compared against the same scene rendered before the
// change, and the comparison is exact. The renderer is bit-deterministic frame
// to frame on one build, measured rather than assumed -- two runs of the same
// scene at the same frame produced byte-identical images.
//
// Cost when nothing is armed: one pointer comparison per frame. The swapchain
// asks for TRANSFER_SRC usage when the surface offers it, which is the standard
// screenshot combination and is not known to cost anything on desktop drivers;
// capture reports itself unavailable rather than failing when a surface does
// not offer it.
//=============================================================================
class FrameImageCapture
{
public:
    void Setup(const RendererServices& services);
    void Teardown();

    // Arm for the first recorded frame at or after `atFrame`. Waiting for a
    // frame number rather than taking the next one is what an unattended run
    // needs: the first frames of a process are a window appearing, a swapchain
    // being recreated, and assets still streaming in, so a comparison against a
    // reference image has to name a frame by which the scene has settled.
    void Request(std::string path, std::uint64_t atFrame);

    [[nodiscard]] bool IsArmed() const { return !PendingPath.empty(); }

    // Copies `image` if armed and `frameNumber` has arrived. Call inside the
    // frame's command buffer with the image still in COLOR_ATTACHMENT_OPTIMAL;
    // it is left in that layout.
    void Record(const FrameContext& frame, std::uint64_t frameNumber, VkImage image,
                VkExtent2D extent, VkFormat format);

    // Writes a pending capture once its frame has retired. Call once per frame.
    //
    // The clock is passed in rather than stored, which is the whole contract:
    // GpuFrameRetirement is a snapshot of how far the GPU has got, so a copy
    // kept from the frame the capture was recorded on reports that same instant
    // forever and the capture never retires. Only the stamp is worth keeping.
    void Drain(GpuFrameRetirement retirement);

private:
    void Release();
    // Maps, converts, and encodes. The caller decides when that is safe.
    void Write();

    LoggingProvider* Logging = nullptr;
    VkDevice Device = VK_NULL_HANDLE;
    VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;

    // Requested but not yet recorded, and the frame it waits for.
    std::string PendingPath;
    std::uint64_t PendingFrame = 0;

    // Recorded and waiting on the GPU.
    VkBuffer Buffer = VK_NULL_HANDLE;
    VkDeviceMemory Memory = VK_NULL_HANDLE;
    VkDeviceSize Size = 0;
    std::uint32_t Width = 0;
    std::uint32_t Height = 0;
    // Whether the recorded copy needs its red and blue channels swapped before
    // it is a PNG. The swapchain is usually BGRA.
    bool SwapRedBlue = false;
    std::string WritePath;
    std::uint64_t Stamp = 0;
};
