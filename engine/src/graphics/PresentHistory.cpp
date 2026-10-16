#include <graphics/PresentHistory.h>

namespace
{
    double SecondsBetween(PresentHistory::Clock::time_point from, PresentHistory::Clock::time_point to)
    {
        return std::chrono::duration<double>(to - from).count();
    }
}

void PresentHistory::Queued(std::uint64_t id, Clock::time_point queuedAt, Clock::time_point frameStartedAt)
{
    if (FirstId == 0)
        FirstId = id;
    NewestId = id;
    if (Count == kTracked)
    {
        Head = (Head + 1) % kTracked;
        --Count;
    }
    Ring[(Head + Count) % kTracked] = Entry{ .Id = id, .QueuedAt = queuedAt, .FrameStartedAt = frameStartedAt };
    ++Count;
}

void PresentHistory::Clear()
{
    Head = 0;
    Count = 0;
    FirstId = 0;
    NewestId = 0;
}

std::uint64_t PresentHistory::OldestPending() const
{
    return Count == 0 ? 0 : Ring[Head].Id;
}

std::optional<PresentHistory::DisplayHold> PresentHistory::HoldToQueue(std::uint32_t maxQueued,
                                                                       double refreshSeconds) const
{
    if (NewestId == 0 || refreshSeconds <= 0.0 || NewestId < FirstId + maxQueued)
        return std::nullopt;
    // The awaited present is the oldest of `maxQueued + 1`; a display taking one
    // per refresh shows them all within that many refreshes, plus one for the path.
    const std::chrono::duration<double> timeout{ refreshSeconds * (maxQueued + 2) };
    return DisplayHold{
        .Awaited = NewestId - maxQueued,
        .Timeout = std::chrono::duration_cast<std::chrono::nanoseconds>(timeout),
    };
}

std::optional<PresentHistory::DisplayedPresent> PresentHistory::Displayed(std::uint64_t id, Clock::time_point now)
{
    std::optional<DisplayedPresent> displayed;
    while (Count > 0 && Ring[Head].Id <= id)
    {
        const Entry& entry = Ring[Head];
        if (entry.Id == id)
            displayed = DisplayedPresent{
                .LatencyFromQueue = SecondsBetween(entry.QueuedAt, now),
                .LatencyFromFrameStart = SecondsBetween(entry.FrameStartedAt, now),
            };
        Head = (Head + 1) % kTracked;
        --Count;
    }
    return displayed;
}
