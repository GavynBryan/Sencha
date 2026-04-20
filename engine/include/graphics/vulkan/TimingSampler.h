#pragma once

#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/VulkanFrameService.h>
#include <graphics/vulkan/VulkanSwapchainService.h>
#include <runtime/RuntimeFrameLoop.h>
#include <time/TimingHistory.h>

class TimingSampler
{
public:
    static void PushRenderFrame(TimingHistory& history,
                                const RuntimeFrameSnapshot& frame,
                                const RendererFrameTiming& rendererTiming,
                                const VulkanFrameTiming& vulkanTiming,
                                const SwapchainState& swapchain,
                                uint64_t swapchainRecreateCount,
                                RenderFrameResult renderResult);

    static void PushLifecycleFrame(TimingHistory& history,
                                   const RuntimeFrameSnapshot& frame,
                                   const SwapchainState& swapchain,
                                   uint64_t swapchainRecreateCount);
};
