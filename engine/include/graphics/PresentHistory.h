#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>

// The presents one swapchain has queued, in order, with the time each was
// queued, until each is seen displayed. Present ids rise by one per present on
// the swapchain, so "the present n behind the next one" is arithmetic on the
// newest id. Cleared when the swapchain is rebuilt: an id belongs to its chain.
class PresentHistory
{
public:
    using Clock = std::chrono::steady_clock;

    void Queued(std::uint64_t id, Clock::time_point at);
    void Clear();

    // The id queued `depth` presents before the one about to be queued, or 0
    // when this chain has no such present.
    [[nodiscard]] std::uint64_t IdBehindNext(std::uint32_t depth) const;

    // The oldest present not yet seen displayed, or 0.
    [[nodiscard]] std::uint64_t OldestPending() const;
    [[nodiscard]] std::size_t PendingCount() const { return Count; }

    // Every present up to and including `id` has been displayed by `now`.
    // Returns how long `id` took from queue to display when it was still pending.
    std::optional<double> Displayed(std::uint64_t id, Clock::time_point now);

private:
    struct Entry
    {
        std::uint64_t Id = 0;
        Clock::time_point QueuedAt{};
    };

    // Pending presents are bounded by the swapchain's images; a chain that
    // somehow queues more loses the oldest timestamps, never the ordering.
    static constexpr std::size_t kCapacity = 16;

    std::array<Entry, kCapacity> Ring{};
    std::size_t Head = 0;
    std::size_t Count = 0;
    std::uint64_t FirstId = 0;
    std::uint64_t NewestId = 0;
};
