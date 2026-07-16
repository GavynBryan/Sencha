#pragma once

#include <cstdint>
#include <string_view>

struct RenderStats;
class RenderStatsHistory;
class GpuTimestampPool;
class RenderCapture;

// The instrumentation ladder. Each mode includes everything below it; Off
// means absent: no timestamp writes, no query resets or readbacks, no
// history or capture writes, no label commands, no allocations on behalf of
// profiling.
enum class RenderProfileMode : std::uint8_t
{
    Off,
    Counters,
    Gpu,
    Capture,
};

[[nodiscard]] const char* ToString(RenderProfileMode mode);
// Case-sensitive cvar-string form ("off", "counters", "gpu", "capture");
// returns false and leaves `mode` untouched on an unknown name.
[[nodiscard]] bool ParseRenderProfileMode(std::string_view name,
                                          RenderProfileMode& mode);

// The fixed GPU timestamp scope set: one per render phase plus the engine
// passes worth timing on their own. A closed enum instead of runtime
// registration, so scope identity is compile-time and no per-frame string
// work exists anywhere. New scopes are added here with their producer.
enum class GpuScope : std::uint8_t
{
    PhaseOffscreen,
    PhaseMainColor,
    ShadowSpotViews,
    ForwardOpaque,
    Count
};

inline constexpr std::uint32_t kGpuScopeCount =
    static_cast<std::uint32_t>(GpuScope::Count);

[[nodiscard]] const char* ToString(GpuScope scope);

// One collected scope span. Valid only when both timestamps of the pair
// were written and available for the frame slot being read back.
struct GpuScopeSpan
{
    float Milliseconds = 0.0f;
    bool Valid = false;
};

// Owned by the Engine beside TimingHistory and handed by pointer to the
// renderer services and the render pipeline. Members are non-null exactly
// while their mode is active: Stats and StatsHistory in Counters and above,
// GpuTimestamps in Gpu and above, Capture in Capture. Consumers cache the
// bundle pointer once and re-read the members each frame; the mode latch
// rewrites them only at the top of the extract phase, so a frame sees one
// consistent mode.
struct RenderInstrumentation
{
    RenderStats* Stats = nullptr;
    RenderStatsHistory* StatsHistory = nullptr;
    GpuTimestampPool* GpuTimestamps = nullptr;
    RenderCapture* Capture = nullptr;
};
