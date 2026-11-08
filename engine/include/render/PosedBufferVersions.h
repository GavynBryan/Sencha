#pragma once

#include <graphics/FramesInFlight.h>
#include <graphics/GpuFrameRetirement.h>

#include <array>
#include <cstdint>
#include <optional>

// Which of one instance's posed-buffer versions a frame may write and read.
// A version is safe to write once no unfinished frame has touched it. Holding
// a pose re-stamps the version it reads, or a later frame would see it retired
// and overwrite it while this frame still draws from it.
class PosedBufferVersions
{
public:
    // A version no unfinished frame references, preferring one that is not the
    // latest. With `versionCount` frames in flight at most versionCount - 1
    // versions are busy, so this finds one unless the frame clock is behind.
    [[nodiscard]] std::optional<std::uint32_t> FindWritable(const GpuFrameRetirement& clock,
                                                            std::uint32_t versionCount) const
    {
        std::optional<std::uint32_t> latestIfFree;
        for (std::uint32_t version = 0; version < versionCount && version < Stamps.size(); ++version)
        {
            if (Stamps[version] != kNeverUsed && !clock.IsRetired(Stamps[version]))
                continue;
            if (Latest.has_value() && *Latest == version)
                latestIfFree = version;
            else
                return version;
        }
        return latestIfFree;
    }

    // The dispatch writing `version` was recorded; this frame reads its result.
    void MarkWritten(std::uint32_t version, const GpuFrameRetirement& clock)
    {
        Stamps[version] = clock.Stamp();
        Latest = version;
    }

    // The newest finished pose, stamped as read by this frame. Empty before the first pose.
    [[nodiscard]] std::optional<std::uint32_t> ReadLatest(const GpuFrameRetirement& clock)
    {
        if (Latest.has_value())
            Stamps[*Latest] = clock.Stamp();
        return Latest;
    }

    [[nodiscard]] bool HasPose() const { return Latest.has_value(); }

private:
    static constexpr std::uint64_t kNeverUsed = 0;

    std::array<std::uint64_t, kMaxFramesInFlight> Stamps{};
    std::optional<std::uint32_t> Latest;
};
