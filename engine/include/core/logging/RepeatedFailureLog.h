#pragma once

#include <cstdint>
#include <optional>

// Reduces a failure that recurs every period (usually a frame) to one report
// when it starts and one when it stops.
class RepeatedFailureLog
{
public:
    struct Episode
    {
        std::uint64_t Failures = 0;
        std::uint32_t Periods = 0;
    };

    // True for the failure that opens an episode, the one worth logging in detail.
    [[nodiscard]] bool Record()
    {
        ++Current.Failures;
        FailedThisPeriod = true;
        const bool opens = !Open;
        Open = true;
        return opens;
    }

    // Closes the period. Returns the episode once a whole period passes without a failure.
    [[nodiscard]] std::optional<Episode> EndPeriod()
    {
        if (FailedThisPeriod)
        {
            ++Current.Periods;
            FailedThisPeriod = false;
            return std::nullopt;
        }
        if (!Open)
            return std::nullopt;
        const Episode ended = Current;
        Current = {};
        Open = false;
        return ended;
    }

private:
    Episode Current;
    bool Open = false;
    bool FailedThisPeriod = false;
};
