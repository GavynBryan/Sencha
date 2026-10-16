#include <graphics/PresentHistory.h>

namespace
{
    double SecondsBetween(PresentHistory::Clock::time_point from, PresentHistory::Clock::time_point to)
    {
        return std::chrono::duration<double>(to - from).count();
    }
}

void PresentHistory::Queued(std::uint64_t id, Clock::time_point queuedAt, const Frame& frame)
{
    if (FirstId == 0)
        FirstId = id;
    NewestId = id;
    if (Count == kTracked)
    {
        Head = (Head + 1) % kTracked;
        --Count;
    }
    Ring[(Head + Count) % kTracked] = Entry{ .Id = id, .QueuedAt = queuedAt, .Producer = frame };
    ++Count;
}

void PresentHistory::Clear()
{
    Head = 0;
    Count = 0;
    FirstId = 0;
    NewestId = 0;
}

std::uint64_t PresentHistory::IdBehindNext(std::uint32_t depth) const
{
    if (depth == 0 || NewestId == 0 || NewestId + 1 < FirstId + depth)
        return 0;
    return NewestId + 1 - depth;
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
                .LatencyFromFrameStart = SecondsBetween(entry.Producer.StartedAt, now),
                .ProductionSeconds = SecondsBetween(entry.Producer.StartedAt, entry.QueuedAt),
                .HeldForDisplay = entry.Producer.HeldForDisplay,
            };
        Head = (Head + 1) % kTracked;
        --Count;
    }
    return displayed;
}
