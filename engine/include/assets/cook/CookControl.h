#pragma once

#include <atomic>
#include <cstdint>

// Cross-thread control for one cook execution. The worker publishes only
// numeric progress; editor strings come from the immutable resolved graph.
class CookControl
{
public:
    void RequestCancel() { CancelRequested.store(true); }
    [[nodiscard]] bool IsCancellationRequested() const
    {
        return CancelRequested.load();
    }

    std::atomic<std::uint32_t> CurrentStep{ 0 };
    std::atomic<std::uint32_t> CompletedSteps{ 0 };
    std::atomic<std::uint32_t> TotalSteps{ 0 };

private:
    std::atomic<bool> CancelRequested{ false };
};

