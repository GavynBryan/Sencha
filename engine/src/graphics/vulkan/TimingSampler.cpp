#include <graphics/vulkan/TimingSampler.h>

namespace
{
    TimingFrameSample BuildBaseSample(const RuntimeFrameSnapshot& frame,
                                      const SwapchainState& swapchain,
                                      uint64_t swapchainRecreateCount)
    {
        return TimingFrameSample{
            .RawDtSeconds = frame.WallTime.UnscaledDt,
            .TickDtSeconds = frame.TickDtSeconds,
            .PresentationDtSeconds = frame.Presentation.DeltaSeconds,
            .InterpolationAlpha = frame.Presentation.Alpha,
            .FixedTicks = frame.FixedTicks,
            .LifecycleState = static_cast<int>(frame.State),
            .TemporalDiscontinuityReason = static_cast<int>(frame.DiscontinuityReason),
            .RuntimeEvents = static_cast<uint32_t>(frame.Events),
            .LifecycleOnly = frame.LifecycleOnly,
            .SwapchainGeneration = swapchain.Generation,
            .SwapchainRecreateCount = swapchainRecreateCount,
            .SwapchainImageCount = swapchain.ImageCount,
            .PresentMode = static_cast<int>(swapchain.PresentMode),
            .SwapchainRecreated = HasRuntimeFrameEvent(
                frame.Events, RuntimeFrameEventFlags::SwapchainRecreated),
            .PresentationReset =
                frame.DiscontinuityReason != TemporalDiscontinuityReason::None,
        };
    }
}

void TimingSampler::PushRenderFrame(TimingHistory& history,
                                    const RuntimeFrameSnapshot& frame,
                                    const RendererFrameTiming& rendererTiming,
                                    const VulkanFrameTiming& vulkanTiming,
                                    const SwapchainState& swapchain,
                                    uint64_t swapchainRecreateCount,
                                    RenderFrameResult renderResult)
{
    TimingFrameSample sample =
        BuildBaseSample(frame, swapchain, swapchainRecreateCount);
    sample.RenderRecordSeconds = rendererTiming.RecordSeconds;
    sample.AcquireSeconds = vulkanTiming.AcquireSeconds;
    sample.SubmitSeconds = vulkanTiming.SubmitSeconds;
    sample.PresentSeconds = vulkanTiming.PresentSeconds;
    sample.TotalFrameSeconds = rendererTiming.TotalSeconds;
    sample.RenderResult = static_cast<int>(renderResult);
    sample.Presented = renderResult == RenderFrameResult::Presented;
    sample.SwapchainImageIndex = vulkanTiming.ImageIndex;
    history.Push(sample);
}

void TimingSampler::PushLifecycleFrame(TimingHistory& history,
                                       const RuntimeFrameSnapshot& frame,
                                       const SwapchainState& swapchain,
                                       uint64_t swapchainRecreateCount)
{
    TimingFrameSample sample =
        BuildBaseSample(frame, swapchain, swapchainRecreateCount);
    sample.RenderResult = static_cast<int>(RenderFrameResult::SkippedMinimized);
    sample.Presented = false;
    sample.LifecycleOnly = true;
    history.Push(sample);
}
