#pragma once

#include <profiling/CpuScopeTimings.h>
#include <profiling/RenderInstrumentation.h>

#include <cstddef>
#include <cstdint>
#include <vector>

struct TimingFrameSample
{
    double RawDtSeconds = 0.0;
    // RawDt as the cadence lock conditioned it: the delta the simulation and
    // interpolation actually consumed. Raw minus locked is the measurement
    // noise the lock removed that frame.
    double LockedDtSeconds = 0.0;
    double TickDtSeconds = 0.0;
    double PresentationDtSeconds = 0.0;
    double InterpolationAlpha = 0.0;
    double EventPollSeconds = 0.0;
    double RenderRecordSeconds = 0.0;
    double AcquireSeconds = 0.0;
    double SubmitSeconds = 0.0;
    double PresentSeconds = 0.0;
    double TotalFrameSeconds = 0.0;
    uint32_t FixedTicks = 0;
    // Ticks this frame owed but the catch-up cap refused. Sustained non-zero
    // values mean simulation cannot keep up with wall time.
    uint32_t TicksDropped = 0;
    int LifecycleState = 0;
    int TemporalDiscontinuityReason = 0;
    uint32_t RuntimeEvents = 0;
    int RenderResult = 0;
    bool Presented = false;
    bool LifecycleOnly = false;
    uint64_t SwapchainGeneration = 0;
    uint64_t SwapchainRecreateCount = 0;
    uint32_t SwapchainImageIndex = 0;
    uint32_t SwapchainImageCount = 0;
    // The drawable size this frame rendered at. Per frame rather than in the
    // capture envelope because a resize mid-capture changes it.
    uint32_t SwapchainWidth = 0;
    uint32_t SwapchainHeight = 0;
    int PresentMode = 0;
    bool SwapchainRecreated = false;
    bool PresentationReset = false;
    // GPU scope spans collected for this slot's previous submission; all
    // invalid while render.profile.mode is below Gpu.
    GpuScopeSpan GpuScopes[kGpuScopeCount] = {};
    // This frame's CPU scope milliseconds; every scope reads not-measured
    // while render.profile.mode is below Counters.
    CpuScopeTimings CpuScopes{};
};

class TimingHistory
{
public:
    explicit TimingHistory(std::size_t capacity = 512)
        : Samples(capacity)
    {
    }

    void Push(const TimingFrameSample& sample)
    {
        if (Samples.empty())
            return;

        Samples[Head] = sample;
        Head = (Head + 1) % Samples.size();
        if (Count < Samples.size())
            ++Count;
    }

    [[nodiscard]] std::size_t Size() const { return Count; }
    [[nodiscard]] std::size_t Capacity() const { return Samples.size(); }

    [[nodiscard]] const TimingFrameSample& GetChronological(std::size_t index) const
    {
        const std::size_t start = Count == Samples.size() ? Head : 0;
        return Samples[(start + index) % Samples.size()];
    }

    [[nodiscard]] const TimingFrameSample* Latest() const
    {
        if (Count == 0 || Samples.empty())
            return nullptr;

        const std::size_t index = Head == 0 ? Samples.size() - 1 : Head - 1;
        return &Samples[index];
    }

private:
    std::vector<TimingFrameSample> Samples;
    std::size_t Head = 0;
    std::size_t Count = 0;
};
