#pragma once

#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/VulkanFrameService.h>
#include <graphics/vulkan/VulkanSwapchainService.h>
#include <runtime/RuntimeFrameLoop.h>
#include <time/TimingHistory.h>

class GpuTimestampPool;

class TimingSampler
{
public:
    // `gpuTimestamps` may be null (mode below Gpu, or profiling compiled
    // out); the sample's scope spans then stay invalid. `cpuScopes` may be
    // null (mode below Counters); the sample's CPU scopes then stay
    // not-measured.
    static void PushRenderFrame(TimingHistory& history,
                                const RuntimeFrameSnapshot& frame,
                                const RendererFrameTiming& rendererTiming,
                                const VulkanFrameTiming& vulkanTiming,
                                const SwapchainState& swapchain,
                                uint64_t swapchainRecreateCount,
                                RenderFrameResult renderResult,
                                const GpuTimestampPool* gpuTimestamps = nullptr,
                                const CpuScopeTimings* cpuScopes = nullptr);

    static void PushLifecycleFrame(TimingHistory& history,
                                   const RuntimeFrameSnapshot& frame,
                                   const SwapchainState& swapchain,
                                   uint64_t swapchainRecreateCount);
};
