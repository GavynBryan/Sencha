#include <graphics/PresentHistory.h>

void PresentHistory::Queued(std::uint64_t id, Clock::time_point at)
{
    if (FirstId == 0)
        FirstId = id;
    NewestId = id;
    if (Count == kCapacity)
    {
        Head = (Head + 1) % kCapacity;
        --Count;
    }
    Ring[(Head + Count) % kCapacity] = Entry{ .Id = id, .QueuedAt = at };
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

std::optional<double> PresentHistory::Displayed(std::uint64_t id, Clock::time_point now)
{
    std::optional<double> latency;
    while (Count > 0 && Ring[Head].Id <= id)
    {
        if (Ring[Head].Id == id)
            latency = std::chrono::duration<double>(now - Ring[Head].QueuedAt).count();
        Head = (Head + 1) % kCapacity;
        --Count;
    }
    return latency;
}
