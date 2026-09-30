#pragma once

#include <graphics/PresentationId.h>
#include <graphics/vulkan/VulkanFrameService.h>
#include <platform/WindowTypes.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// The per-frame decisions of multi-presentation rendering, free of any device
// so they test headless: which presentations a frame acquires and what a
// swapchain result means for the one target it came from.

enum class PresentationAvailability : std::uint8_t
{
    Acquirable,
    Minimized,
    ZeroExtent,
    NeedsRebuild,
};

[[nodiscard]] PresentationAvailability ClassifyPresentation(bool minimized, WindowExtent extent,
                                                           bool needsRebuild);

struct PresentationFrameState
{
    PresentationId Id;
    PresentationAvailability Availability = PresentationAvailability::Acquirable;
};

// The presentations a frame acquires, in the order given.
[[nodiscard]] std::vector<PresentationId> PlanAcquisitions(std::span<const PresentationFrameState> presentations);

// What an acquire or present reported for one swapchain.
enum class SurfaceOutcome : std::uint8_t
{
    Ok,
    Suboptimal,
    OutOfDate,
    DeviceLost,
    Failed,
};

[[nodiscard]] SurfaceOutcome ClassifySurfaceResult(VkResult result);
[[nodiscard]] bool IsFatal(SurfaceOutcome outcome);

// The frame as the frame loop sees it. The primary's outcome decides it, since
// the loop owns the primary's resize lifecycle; another presentation's outcome
// stays with that presentation. A frame that recorded no presentation at all
// is a skipped one, like a minimized single window.
[[nodiscard]] RenderFrameResult SummarizeFrame(SurfaceOutcome primary, std::size_t presentationsRecorded);
