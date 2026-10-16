#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

// How many presents may be waiting for the display when a display-paced frame
// begins. Deeper costs latency; shallower than the presentation path's own
// latency starves the display. The path's latency is the platform's and cannot
// be read off a present that was held to this limit, so the limit starts at the
// frames in flight and grows only on evidence that it is starving the display
// (docs/renderer/frame.md, Pacing).
class DisplayQueueLimit
{
public:
    using Clock = std::chrono::steady_clock;

    DisplayQueueLimit(std::uint32_t initialDepth, std::uint32_t maxDepth);

    [[nodiscard]] std::uint32_t Depth() const { return Current; }
    void Reset();

    struct Observation
    {
        std::uint64_t Id = 0;
        // Exact: the frame waited for this present and was released by it.
        Clock::time_point DisplayedAt{};
        // Whether the frame that produced this present was itself held by the limit.
        bool HeldForDisplay = false;
        double ProductionSeconds = 0.0;
    };

    void Displayed(const Observation& present, double refreshSeconds);

private:
    // Two starved refreshes this close together are a pattern, not a hitch; a
    // limit too shallow starves the display many times a second.
    static constexpr std::chrono::seconds kStarvationPairWindow{ 1 };

    std::uint32_t Initial = 1;
    std::uint32_t Max = 1;
    std::uint32_t Current = 1;
    std::uint64_t LastId = 0;
    Clock::time_point LastDisplayedAt{};
    std::optional<Clock::time_point> LastStarvedAt;
};
