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

    // A chain that queues more loses the oldest timestamps, never the ordering.
    static constexpr std::size_t kTracked = 16;

    void Queued(std::uint64_t id, Clock::time_point queuedAt, Clock::time_point frameStartedAt);
    void Clear();

    [[nodiscard]] std::uint64_t OldestPending() const;
    [[nodiscard]] std::size_t PendingCount() const { return Count; }

    struct DisplayHold
    {
        std::uint64_t Awaited = 0;
        // A display that stops taking frames (hidden, locked) must not hold the loop.
        std::chrono::nanoseconds Timeout{};
    };

    // The present a frame must see displayed so that at most `maxQueued`
    // presents wait for the display when it begins; none until the chain has
    // queued more than that, or without a refresh period.
    [[nodiscard]] std::optional<DisplayHold> HoldToQueue(std::uint32_t maxQueued, double refreshSeconds) const;

    struct DisplayedPresent
    {
        double LatencyFromQueue = 0.0;
        double LatencyFromFrameStart = 0.0;
    };

    // Retires every present through `id`; what is known of `id` if it was pending.
    std::optional<DisplayedPresent> Displayed(std::uint64_t id, Clock::time_point now);

private:
    struct Entry
    {
        std::uint64_t Id = 0;
        Clock::time_point QueuedAt{};
        Clock::time_point FrameStartedAt{};
    };

    std::array<Entry, kTracked> Ring{};
    std::size_t Head = 0;
    std::size_t Count = 0;
    std::uint64_t FirstId = 0;
    std::uint64_t NewestId = 0;
};
