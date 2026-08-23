#pragma once

#include <chrono>
#include <cstdint>

//=============================================================================
// CpuScopeTimings
//
// The CPU counterpart to GpuScope: a fixed set of named spans measured on the
// frame's own thread, accumulated per frame, and carried into the timing
// sample beside the GPU spans. A closed enum for the same reason the GPU set
// is closed -- scope identity is compile-time, so no per-frame string work
// exists anywhere. New scopes are added here with their producer.
//
// RenderRecordSeconds already covers whole-frame command recording; these
// scopes exist to attribute it, and to time the extract-phase work that no
// GPU scope can see.
//=============================================================================

enum class CpuScope : std::uint8_t
{
    // Walking visible meshes into the render queue, across every registry.
    Extraction,
    // Gathering light candidates, scoring, sorting, and packing to the cap.
    LightSelection,
    // Gathering shadow casters and diffing them against the previous frame.
    ShadowGather,
    // Resolving shadow slot residency and stamping the grants onto the lights.
    ShadowResidency,
    // Recording shadow depth views: per-view culling and draw submission.
    ShadowRecord,
    // Recording the forward opaque pass.
    ForwardRecord,
    Count
};

inline constexpr std::uint32_t kCpuScopeCount =
    static_cast<std::uint32_t>(CpuScope::Count);

[[nodiscard]] const char* ToString(CpuScope scope);

// Milliseconds per scope for one frame. A scope that did not run reads
// negative rather than zero: "not measured" and "measured as free" are
// different facts, and a mode below Counters produces the former for all of
// them. Accumulating (rather than assigning) is what lets a scope that runs
// once per registry or once per view report its frame total.
class CpuScopeTimings
{
public:
    static constexpr float kNotMeasured = -1.0f;

    CpuScopeTimings() { ResetFrame(); }

    void ResetFrame()
    {
        for (float& value : Milliseconds)
            value = kNotMeasured;
    }

    void Add(CpuScope scope, double milliseconds)
    {
        float& slot = Milliseconds[static_cast<std::uint32_t>(scope)];
        slot = (slot < 0.0f ? 0.0f : slot) + static_cast<float>(milliseconds);
    }

    [[nodiscard]] float Get(CpuScope scope) const
    {
        return Milliseconds[static_cast<std::uint32_t>(scope)];
    }

private:
    float Milliseconds[kCpuScopeCount];
};

// Times its enclosing block into one scope. A null sink makes every operation
// a no-op, which is how the instrumentation-off path pays nothing: the
// bundle hands out nullptr below Counters.
class CpuScopeTimer
{
public:
    CpuScopeTimer(CpuScopeTimings* sink, CpuScope scope)
        : Sink(sink)
        , Scope(scope)
        , Start(sink != nullptr ? Clock::now() : Clock::time_point{})
    {
    }

    ~CpuScopeTimer()
    {
        if (Sink == nullptr)
            return;
        const std::chrono::duration<double, std::milli> elapsed =
            Clock::now() - Start;
        Sink->Add(Scope, elapsed.count());
    }

    CpuScopeTimer(const CpuScopeTimer&) = delete;
    CpuScopeTimer& operator=(const CpuScopeTimer&) = delete;

private:
    using Clock = std::chrono::steady_clock;

    CpuScopeTimings* Sink = nullptr;
    CpuScope Scope = CpuScope::Extraction;
    Clock::time_point Start;
};
