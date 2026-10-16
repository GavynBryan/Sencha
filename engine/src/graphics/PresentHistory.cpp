#include <graphics/PresentHistory.h>

void PresentHistory::Queued(std::uint64_t id, Clock::time_point queuedAt, Clock::time_point frameStartedAt)
{
    if (Count == kCapacity)
    {
        Head = (Head + 1) % kCapacity;
        --Count;
    }
    Ring[(Head + Count) % kCapacity] = Entry{ .Id = id, .QueuedAt = queuedAt, .FrameStartedAt = frameStartedAt };
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

std::optional<PresentHistory::Latency> PresentHistory::Displayed(std::uint64_t id, Clock::time_point now)
{
    std::optional<Latency> latency;
    while (Count > 0 && Ring[Head].Id <= id)
    {
        if (Ring[Head].Id == id)
            latency = Latency{
                .FromQueue = std::chrono::duration<double>(now - Ring[Head].QueuedAt).count(),
                .FromFrameStart = std::chrono::duration<double>(now - Ring[Head].FrameStartedAt).count(),
            };
        Head = (Head + 1) % kCapacity;
        --Count;
    }
    return latency;
}
