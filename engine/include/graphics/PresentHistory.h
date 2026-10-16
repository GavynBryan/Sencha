#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>

// The presents one swapchain has queued, in order, with when each was queued
// and when its frame began, until each is seen displayed. Present ids rise per
// present, so seeing one displayed retires every earlier one. Cleared when the
// swapchain is rebuilt: an id belongs to its chain.
class PresentHistory
{
public:
    using Clock = std::chrono::steady_clock;

    // `frameStartedAt` is when the frame that produced the present began.
    void Queued(std::uint64_t id, Clock::time_point queuedAt, Clock::time_point frameStartedAt);
    void Clear();

    // The oldest present not yet seen displayed, or 0.
    [[nodiscard]] std::uint64_t OldestPending() const;
    [[nodiscard]] std::size_t PendingCount() const { return Count; }

    struct Latency
    {
        double FromQueue = 0.0;
        double FromFrameStart = 0.0;
    };

    // Every present up to and including `id` has been displayed by `now`.
    // Returns `id`'s latency when it was still pending.
    std::optional<Latency> Displayed(std::uint64_t id, Clock::time_point now);

private:
    struct Entry
    {
        std::uint64_t Id = 0;
        Clock::time_point QueuedAt{};
        Clock::time_point FrameStartedAt{};
    };

    // Pending presents are bounded by the swapchain's images; a chain that
    // somehow queues more loses the oldest timestamps, never the ordering.
    static constexpr std::size_t kCapacity = 16;

    std::array<Entry, kCapacity> Ring{};
    std::size_t Head = 0;
    std::size_t Count = 0;
};
