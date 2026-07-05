#include "PortalGeometry.h"

#include <cmath>

int DominantPortalAxis(const Aabb3d& worldBounds)
{
    const Vec3d extent = worldBounds.Extent();
    int axis = 0;
    for (int i = 1; i < 3; ++i)
        if (extent[i] < extent[axis])
            axis = i;
    return axis;
}

ZoneId GuessPortalTargetZone(const WorldPartitionManifest& manifest, ZoneId owner,
                             const Aabb3d& portalWorldBounds)
{
    const int facing = DominantPortalAxis(portalWorldBounds);
    const Vec3d center = portalWorldBounds.Center();
    constexpr double epsilon = 1e-6;

    const ZoneHeader* best = nullptr;
    double bestDistance = 0.0;
    for (const ZoneHeader& zone : manifest.Zones)
    {
        if (zone.Id == owner)
            continue;
        const Vec3d delta = zone.Bounds.Center() - center;
        const double magnitudes[3] = { std::abs(static_cast<double>(delta[0])),
                                       std::abs(static_cast<double>(delta[1])),
                                       std::abs(static_cast<double>(delta[2])) };
        if (magnitudes[0] < epsilon && magnitudes[1] < epsilon && magnitudes[2] < epsilon)
            continue;
        int deltaAxis = 0;
        for (int i = 1; i < 3; ++i)
            if (magnitudes[i] > magnitudes[deltaAxis])
                deltaAxis = i;
        if (deltaAxis != facing)
            continue;
        const double distance = magnitudes[0] * magnitudes[0] + magnitudes[1] * magnitudes[1]
            + magnitudes[2] * magnitudes[2];
        if (best == nullptr || distance < bestDistance
            || (distance == bestDistance && zone.Id.Value < best->Id.Value))
        {
            best = &zone;
            bestDistance = distance;
        }
    }
    return best != nullptr ? best->Id : ZoneId{};
}

ZoneId DerivePortalCounterpart(const WorldPartitionManifest& manifest, ZoneId holder,
                               const Aabb3d& portalWorldBounds)
{
    Aabb3d reach = portalWorldBounds;
    const int facing = DominantPortalAxis(portalWorldBounds);
    reach.Min[facing] -= 1.0f;
    reach.Max[facing] += 1.0f;

    const auto overlapVolume = [&](const Aabb3d& bounds) -> double
    {
        double volume = 1.0;
        for (int i = 0; i < 3; ++i)
        {
            const double lo = std::max(static_cast<double>(reach.Min[i]),
                                       static_cast<double>(bounds.Min[i]));
            const double hi = std::min(static_cast<double>(reach.Max[i]),
                                       static_cast<double>(bounds.Max[i]));
            if (hi <= lo)
                return 0.0;
            volume *= hi - lo;
        }
        return volume;
    };

    const ZoneHeader* best = nullptr;
    double bestVolume = 0.0;
    for (const ZoneHeader& zone : manifest.Zones)
    {
        if (zone.Id == holder || !zone.Bounds.IsValid())
            continue;
        const double volume = overlapVolume(zone.Bounds);
        if (volume <= 0.0)
            continue;
        if (best == nullptr || volume > bestVolume
            || (volume == bestVolume && zone.Id.Value < best->Id.Value))
        {
            best = &zone;
            bestVolume = volume;
        }
    }
    if (best != nullptr)
        return best->Id;
    return GuessPortalTargetZone(manifest, holder, portalWorldBounds);
}

PortalBoxFit FitPortalBoxToFace(std::span<const Vec3d> faceWorldVertices, Vec3d faceNormal,
                                double thickness)
{
    Aabb3d bounds = Aabb3d::Empty();
    for (const Vec3d& vertex : faceWorldVertices)
        bounds.ExpandToInclude(vertex);

    const double magnitudes[3] = { std::abs(static_cast<double>(faceNormal[0])),
                                   std::abs(static_cast<double>(faceNormal[1])),
                                   std::abs(static_cast<double>(faceNormal[2])) };
    int normalAxis = 0;
    for (int i = 1; i < 3; ++i)
        if (magnitudes[i] > magnitudes[normalAxis])
            normalAxis = i;

    PortalBoxFit fit;
    fit.Center = bounds.Center();
    fit.HalfExtents = bounds.HalfExtent();
    fit.HalfExtents[normalAxis] = static_cast<float>(thickness * 0.5);
    return fit;
}
