#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

//=============================================================================
// ChromeJsonFrameTrace
//
// Phase-boundary tracing. FrameDriver calls BeginPhase / EndPhase around each
// registered phase, and emits markers for fixed ticks too, so CPU-bound sim
// blow-ups show up as distinct regions. Events buffer in memory and write as
// chrome://tracing JSON on flush.
//
// For offline perf investigation, not long-running sessions: memory grows
// linearly with captured frames. Tracing is off unless the frame.trace.output
// cvar names a path, in which case Engine constructs one and hands the driver
// a pointer; a null pointer is the untraced path.
//
// Synchronous, and runs on the loop thread. Cross-thread capture would need to
// buffer internally and flush from a worker.
//=============================================================================
class ChromeJsonFrameTrace
{
public:
    void BeginFrame(uint64_t frameIndex);
    void EndFrame(uint64_t frameIndex);
    void BeginPhase(const char* name);
    void EndPhase(const char* name);

    // Write buffered events to path in chrome tracing format. Returns false
    // on I/O error. Does not clear the buffer; call Clear() if needed.
    [[nodiscard]] bool WriteTo(const std::string& path) const;
    void Clear() { Events.clear(); }
    [[nodiscard]] std::size_t EventCount() const { return Events.size(); }

private:
    using Clock = std::chrono::steady_clock;

    struct Event
    {
        std::string Name;
        char Phase;
        uint64_t Micros;
    };

    [[nodiscard]] uint64_t NowMicros() const;

    std::vector<Event> Events;
    Clock::time_point Epoch = Clock::now();
};
