#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>

// One swapchain's presents, from queueing until seen displayed. Ids rise per
// present, so a displayed id retires every earlier one; a rebuilt chain clears it.
class PresentHistory
{
public:
    using Clock = std::chrono::steady_clock;

    // Pending presents are bounded by the presentation path's depth; a chain
    // that queues more loses the oldest timestamps, never the ordering.
    static constexpr std::size_t kTracked = 16;

    struct Frame
    {
        // When the frame that produced the present began, after any wait.
        Clock::time_point StartedAt{};
        // Whether that frame began only once an earlier present was displayed.
        bool HeldForDisplay = false;
    };

    void Queued(std::uint64_t id, Clock::time_point queuedAt, const Frame& frame);
    void Clear();

    // The id queued `depth` presents before the one about to be queued, or 0
    // when this chain has none.
    [[nodiscard]] std::uint64_t IdBehindNext(std::uint32_t depth) const;
    [[nodiscard]] std::uint64_t OldestPending() const;
    [[nodiscard]] std::size_t PendingCount() const { return Count; }

    struct DisplayedPresent
    {
        double LatencyFromQueue = 0.0;
        double LatencyFromFrameStart = 0.0;
        // From the frame's start to its present being queued.
        double ProductionSeconds = 0.0;
        bool HeldForDisplay = false;
    };

    // Retires every present through `id`; what is known of `id` if it was pending.
    std::optional<DisplayedPresent> Displayed(std::uint64_t id, Clock::time_point now);

private:
    struct Entry
    {
        std::uint64_t Id = 0;
        Clock::time_point QueuedAt{};
        Frame Producer;
    };

    std::array<Entry, kTracked> Ring{};
    std::size_t Head = 0;
    std::size_t Count = 0;
    std::uint64_t FirstId = 0;
    std::uint64_t NewestId = 0;
};
