#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <utility>

// The clock a private-World preview runs on: fixed ticks from a rebuilt tick zero,
// one Record kept per tick, and rewind as a replay from zero. See
// docs/editor/fixed-tick-preview.md.
template <typename Record>
class FixedTickPreview
{
public:
    using Tick = std::uint64_t;

    struct Steps
    {
        // A fresh World at tick zero; false when it has nothing to run.
        std::function<bool()> Rebuild;
        // Whether the World can take another tick, which an edit can take away.
        std::function<bool()> CanRun;
        std::function<Record(Tick)> Run;
    };

    static constexpr std::size_t kHistoryCapacity = 3600;
    static constexpr int kMaxTicksPerAdvance = 8;

    explicit FixedTickPreview(Steps steps)
        : Hooks(std::move(steps))
    {
    }

    void Restart()
    {
        Records.clear();
        Current = 0;
        BranchedAt.reset();
        Pending = 0.0;
        Built = true;
        if (Hooks.Rebuild())
            RunTick(0);
    }

    void Close()
    {
        Built = false;
        Playing = false;
        Pending = 0.0;
        Current = 0;
        BranchedAt.reset();
        Records.clear();
    }

    void Step()
    {
        if (!Built || !Hooks.CanRun())
            return;
        BranchedAt.reset();
        RunTick(Current + 1);
    }

    // Restarts first when `tick` is behind the current tick.
    void RunTo(Tick tick)
    {
        if (!Built)
            return;
        if (tick < Current)
            Restart();
        while (Hooks.CanRun() && Current < tick)
            Step();
    }

    // Rebuilt from zero and run back to the current tick, after a change the past depends on.
    void Replay()
    {
        if (!Built)
            return;
        const Tick at = Current;
        Restart();
        RunTo(at);
    }

    // Speed schedules ticks faster or slower, never changes their length; ticks past
    // the cap are dropped rather than caught up after a stall.
    void Advance(double wallSeconds, std::uint32_t tickRate)
    {
        if (!Playing || !Built || !Hooks.CanRun())
            return;
        Pending += wallSeconds * Rate * static_cast<double>(tickRate);
        Pending = std::min(Pending, static_cast<double>(kMaxTicksPerAdvance));
        while (Pending >= 1.0)
        {
            Pending -= 1.0;
            Step();
        }
    }

    void Play() { Playing = true; }
    void Pause()
    {
        Playing = false;
        Pending = 0.0;
    }
    [[nodiscard]] bool SetSpeed(double speed)
    {
        if (!(speed > 0.0) || speed > 8.0)
            return false;
        Rate = speed;
        return true;
    }

    // Edits made while paused on one tick share one branch: true for the first,
    // whose caller cuts its script after Now() before scheduling at NextTick().
    [[nodiscard]] bool Branch()
    {
        if (BranchedAt == Current)
            return false;
        BranchedAt = Current;
        return true;
    }
    // A replaced script starts no branch.
    void ForgetBranch() { BranchedAt.reset(); }

    [[nodiscard]] bool IsBuilt() const { return Built; }
    [[nodiscard]] bool IsPlaying() const { return Playing; }
    [[nodiscard]] double Speed() const { return Rate; }
    [[nodiscard]] Tick Now() const { return Current; }
    [[nodiscard]] Tick NextTick() const { return Current + 1; }
    [[nodiscard]] const std::deque<Record>& History() const { return Records; }

private:
    void RunTick(Tick tick)
    {
        Current = tick;
        Records.push_back(Hooks.Run(tick));
        while (Records.size() > kHistoryCapacity)
            Records.pop_front();
    }

    Steps Hooks;
    std::deque<Record> Records;
    std::optional<Tick> BranchedAt;
    Tick Current = 0;
    double Pending = 0.0;
    double Rate = 1.0;
    bool Playing = false;
    bool Built = false;
};
