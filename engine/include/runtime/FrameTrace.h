#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

//=============================================================================
// FrameTrace
//
// Phase-boundary tracing interface. FrameDriver calls BeginPhase / EndPhase
// around each registered phase. Implementations:
//
//   NullFrameTrace           — zero-cost default, drop all markers.
//   ChromeJsonFrameTrace     — writes chrome://tracing-compatible JSON.
//   (Tracy/PIX adapters can be added without touching the driver.)
//
// Markers are also emitted for fixed ticks so CPU-bound sim blow-ups show
// up as distinct regions. This interface is intentionally synchronous and
// runs on the loop thread — if you need cross-thread capture, buffer
// internally and flush from a worker.
//=============================================================================
class FrameTrace
{
public:
    virtual ~FrameTrace() = default;

    virtual void BeginFrame(uint64_t frameIndex) = 0;
    virtual void EndFrame(uint64_t frameIndex) = 0;

    virtual void BeginPhase(const char* name) = 0;
    virtual void EndPhase(const char* name) = 0;

    // Point-in-time marker, no matching EndPhase required.
    virtual void Mark(const char* name) = 0;
};

class NullFrameTrace final : public FrameTrace
{
public:
    void BeginFrame(uint64_t) override {}
    void EndFrame(uint64_t) override {}
    void BeginPhase(const char*) override {}
    void EndPhase(const char*) override {}
    void Mark(const char*) override {}
};

//=============================================================================
// ChromeJsonFrameTrace
//
// Buffers phase events and writes them as chrome://tracing JSON on flush.
// Use for offline perf investigation. NOT intended for long-running sessions;
// memory grows linearly with captured frames.
//=============================================================================
class ChromeJsonFrameTrace final : public FrameTrace
{
public:
    void BeginFrame(uint64_t frameIndex) override;
    void EndFrame(uint64_t frameIndex) override;
    void BeginPhase(const char* name) override;
    void EndPhase(const char* name) override;
    void Mark(const char* name) override;

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
