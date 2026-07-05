#include "WorldViewSettings.h"

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
