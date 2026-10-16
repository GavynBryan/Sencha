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
}

std::uint64_t PresentHistory::OldestPending() const
{
    return Count == 0 ? 0 : Ring[Head].Id;
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
