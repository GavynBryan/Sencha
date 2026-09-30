#include <graphics/vulkan/PresentationFramePlan.h>

#include <algorithm>

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
    return presentationsRecorded == 0 ? RenderFrameResult::SkippedMinimized : RenderFrameResult::Presented;
}
