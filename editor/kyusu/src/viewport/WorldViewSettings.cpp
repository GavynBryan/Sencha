#include "WorldViewSettings.h"

#include <algorithm>

WorldPartitionStreamingConfig
ResolvePreviewStreamingConfig(const WorldPartitionManifest& manifest, ZoneId focus,
                              const WorldViewSettings& view)
{
    WorldPartitionStreamingConfig config =
        ResolveRegionStreamingConfig(manifest, focus, WorldPartitionStreamingConfig{});
    if (view.PreviewHopCount)
        config.HopCount = *view.PreviewHopCount;
    if (view.PreviewRadius)
        config.Radius = static_cast<double>(*view.PreviewRadius);
    if (view.PreviewResidentCap)
        config.ResidentZoneCap = *view.PreviewResidentCap;
    return config;
}

ZoneId NearestPreviewFocusZone(const WorldPartitionManifest& manifest, Vec3d position)
{
    const ZoneHeader* best = nullptr;
    double bestDistanceSq = 0.0;
    double bestVolume = 0.0;
    for (const ZoneHeader& header : manifest.Zones)
    {
        if (!header.Bounds.IsValid())
            continue;
        Vec3d closest;
        for (int axis = 0; axis < 3; ++axis)
            closest[axis] = std::clamp(position[axis], header.Bounds.Min[axis],
                                       header.Bounds.Max[axis]);
        const Vec3d delta = closest - position;
        const double distanceSq = static_cast<double>(delta[0]) * delta[0]
            + static_cast<double>(delta[1]) * delta[1]
            + static_cast<double>(delta[2]) * delta[2];
        const Vec3d extent = header.Bounds.Extent();
        const double volume = static_cast<double>(extent[0]) * extent[1] * extent[2];
        const bool better = best == nullptr || distanceSq < bestDistanceSq
            || (distanceSq == bestDistanceSq
                && (volume < bestVolume
                    || (volume == bestVolume && header.Id.Value < best->Id.Value)));
        if (better)
        {
            best = &header;
            bestDistanceSq = distanceSq;
            bestVolume = volume;
        }
    }
    return best != nullptr ? best->Id : ZoneId{};
}
