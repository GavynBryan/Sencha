#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// One frame's renderer counters. Reset by the mode latch at the top of the
// extract phase and filled by the subsystem that owns each number: extraction
// and residency publish from the render pipeline, passes publish their
// pass-local totals at pass exit, the renderer publishes frame-service
// numbers at frame end. Plain integers written on one thread; every field
// has a live producer (fields for systems that do not exist yet are added
// with those systems, and the capture schema version moves with them).
struct RenderStats
{
    // Monotonic frame index, for correlating capture records.
    std::uint64_t FrameIndex = 0;

    // Forward opaque pass.
    std::uint32_t VisibleObjects = 0;
    std::uint32_t DrawCalls = 0;
    std::uint32_t SubmittedTriangles = 0;
    std::uint32_t PipelineSwitches = 0;
    std::uint32_t MaterialSwitches = 0;

    // Light extraction.
    std::uint32_t LightsVisible = 0;
    std::uint32_t LightsDroppedAtCap = 0;
    std::uint32_t ShadowCastingLights = 0;

    // Spot shadow residency and depth pass.
    std::uint32_t ShadowViewsRendered = 0;
    std::uint32_t ShadowCasterDraws = 0;
    std::uint32_t ShadowSlotsHeld = 0;
    std::uint32_t ShadowCacheHits = 0;
    std::uint32_t ShadowRequestsDenied = 0;
    std::uint32_t AtlasTiles1024 = 0;
    std::uint32_t AtlasTiles512 = 0;
    std::uint32_t AtlasTiles256 = 0;
    std::uint64_t ShadowTileBytes = 0;
    std::uint32_t CasterDiffEvents = 0;

    // Frame services.
    std::uint64_t ScratchHighWaterBytes = 0;
};

// Ring of finished frames' stats, pushed once per rendered frame while the
// mode is Counters or above. The version counter proves the Off path writes
// nothing: it must not advance while the mode is Off.
class RenderStatsHistory
{
public:
    explicit RenderStatsHistory(std::size_t capacity = 512)
        : Samples(capacity)
    {
    }

    void Push(const RenderStats& sample)
    {
        if (Samples.empty())
            return;

        Samples[Head] = sample;
        Head = (Head + 1) % Samples.size();
        if (Count < Samples.size())
            ++Count;
        ++Version;
    }

    [[nodiscard]] std::size_t Size() const { return Count; }
    [[nodiscard]] std::size_t Capacity() const { return Samples.size(); }
    [[nodiscard]] std::uint64_t GetVersion() const { return Version; }

    [[nodiscard]] const RenderStats& GetChronological(std::size_t index) const
    {
        const std::size_t start = Count == Samples.size() ? Head : 0;
        return Samples[(start + index) % Samples.size()];
    }

    [[nodiscard]] const RenderStats* Latest() const
    {
        if (Count == 0 || Samples.empty())
            return nullptr;

        const std::size_t index = Head == 0 ? Samples.size() - 1 : Head - 1;
        return &Samples[index];
    }

private:
    std::vector<RenderStats> Samples;
    std::size_t Head = 0;
    std::size_t Count = 0;
    std::uint64_t Version = 0;
};
