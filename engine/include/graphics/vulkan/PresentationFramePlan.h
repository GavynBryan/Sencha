#pragma once

#include <graphics/PresentationId.h>
#include <graphics/vulkan/VulkanFrameService.h>
#include <platform/WindowTypes.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
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
    // No image by the acquire deadline: this presentation skips the frame.
    NotReady,
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

// Every acquire of one frame shares one deadline, so presentations that return
// no image cost the frame at most this together (docs/renderer/frame.md, Pacing).
inline constexpr std::uint32_t kAcquireDeadlineRefreshes = 2;

class AcquireDeadline
{
public:
    using Clock = std::chrono::steady_clock;

    // No deadline without a refresh rate.
    [[nodiscard]] static AcquireDeadline ForDisplay(Clock::time_point start, float refreshHz);

    // vkAcquireNextImageKHR's timeout for an acquire starting at `now`: what is
    // left of the deadline, zero once it has passed, UINT64_MAX without one.
    [[nodiscard]] std::uint64_t TimeoutAt(Clock::time_point now) const;

private:
    std::optional<Clock::time_point> End;
};
