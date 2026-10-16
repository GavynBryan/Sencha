#include <graphics/vulkan/PresentationFramePlan.h>

#include <algorithm>
#include <limits>

PresentationAvailability ClassifyPresentation(bool minimized, WindowExtent extent, bool needsRebuild)
{
    if (minimized)
        return PresentationAvailability::Minimized;
    if (extent.Width == 0 || extent.Height == 0)
        return PresentationAvailability::ZeroExtent;
    if (needsRebuild)
        return PresentationAvailability::NeedsRebuild;
    return PresentationAvailability::Acquirable;
}

std::vector<PresentationId> PlanAcquisitions(std::span<const PresentationFrameState> presentations)
{
    std::vector<PresentationId> plan;
    for (const PresentationFrameState& presentation : presentations)
        if (presentation.Availability == PresentationAvailability::Acquirable)
            plan.push_back(presentation.Id);
    return plan;
}

SurfaceOutcome ClassifySurfaceResult(VkResult result)
{
    switch (result)
    {
    case VK_SUCCESS: return SurfaceOutcome::Ok;
    case VK_SUBOPTIMAL_KHR: return SurfaceOutcome::Suboptimal;
    case VK_TIMEOUT:
    case VK_NOT_READY: return SurfaceOutcome::NotReady;
    case VK_ERROR_OUT_OF_DATE_KHR: return SurfaceOutcome::OutOfDate;
    case VK_ERROR_DEVICE_LOST: return SurfaceOutcome::DeviceLost;
    default: return SurfaceOutcome::Failed;
    }
}

bool IsFatal(SurfaceOutcome outcome)
{
    return outcome == SurfaceOutcome::DeviceLost || outcome == SurfaceOutcome::Failed;
}

RenderFrameResult SummarizeFrame(SurfaceOutcome primary, std::size_t presentationsRecorded)
{
    if (IsFatal(primary))
        return RenderFrameResult::Failed;
    if (primary == SurfaceOutcome::OutOfDate)
        return RenderFrameResult::SwapchainOutOfDate;
    if (primary == SurfaceOutcome::Suboptimal)
        return RenderFrameResult::SurfaceSuboptimal;
    if (primary == SurfaceOutcome::NotReady)
        return RenderFrameResult::SkippedNotReady;
    return presentationsRecorded == 0 ? RenderFrameResult::SkippedMinimized : RenderFrameResult::Presented;
}

AcquireDeadline AcquireDeadline::ForDisplay(Clock::time_point start, float refreshHz)
{
    AcquireDeadline deadline;
    if (refreshHz > 0.0f)
        deadline.End = start + std::chrono::duration_cast<Clock::duration>(
                                   std::chrono::duration<double>(kAcquireDeadlineRefreshes / static_cast<double>(refreshHz)));
    return deadline;
}

std::uint64_t AcquireDeadline::TimeoutAt(Clock::time_point now) const
{
    if (!End)
        return std::numeric_limits<std::uint64_t>::max();
    if (now >= *End)
        return 0;
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(*End - now).count());
}
