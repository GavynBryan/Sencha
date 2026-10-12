#include "document/WorldMap.h"

#include "document/WorldDocument.h"

#include <algorithm>
#include <limits>

namespace
{
bool HasFootprint(const Aabb3d& bounds)
{
    return bounds.IsValid() && bounds.Max.X > bounds.Min.X && bounds.Max.Z > bounds.Min.Z;
}

float Area(const Rect2d& rect)
{
    return rect.Width() * rect.Height();
}
} // namespace

WorldMap LayOutWorldMap(const WorldDocument& world, std::span<const ZoneGraphEdge> edges, const WorldMapRequest& request)
{
    WorldMap map;
    if (!world.IsWorld())
        return map;
    const WorldPartitionManifest& manifest = world.Manifest();

    Vec2d low(std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
    Vec2d high(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest());
    for (const ZoneHeader& zone : manifest.Zones)
    {
        if (!HasFootprint(zone.Bounds))
        {
            ++map.Unplaced;
            continue;
        }
        low = Vec2d(std::min(low.X, zone.Bounds.Min.X), std::min(low.Y, zone.Bounds.Min.Z));
        high = Vec2d(std::max(high.X, zone.Bounds.Max.X), std::max(high.Y, zone.Bounds.Max.Z));
    }
    if (map.Unplaced == manifest.Zones.size())
        return map;

    // One scale for both axes, so the world keeps its shape, centred in the area.
    const Vec2d room = request.Area.Size - Vec2d(request.Margin * 2.0f, request.Margin * 2.0f);
    const Vec2d span = high - low;
    const float scale = std::max(0.0f, std::min(room.X / std::max(span.X, 1e-3f), room.Y / std::max(span.Y, 1e-3f)));
    const Vec2d drawn = span * scale;
    const Vec2d origin = request.Area.Position + Vec2d(request.Margin, request.Margin) + (room - drawn) * 0.5f;
    const auto toMap = [&](float x, float z) { return Vec2d(origin.X + (x - low.X) * scale, origin.Y + (high.Y - z) * scale); };

    const ZoneHeader* here = request.Camera.has_value() ? InnermostZoneAt(manifest, *request.Camera) : nullptr;
    for (const ZoneHeader& zone : manifest.Zones)
    {
        if (!HasFootprint(zone.Bounds))
            continue;
        const Vec2d a = toMap(zone.Bounds.Min.X, zone.Bounds.Max.Z);
        const Vec2d b = toMap(zone.Bounds.Max.X, zone.Bounds.Min.Z);
        Rect2d plate = Rect2d::FromMinMax(a, b);
        if (plate.Width() < request.MinimumPlate || plate.Height() < request.MinimumPlate)
            plate = Rect2d::FromCenterSize(plate.Center(), Vec2d(std::max(plate.Width(), request.MinimumPlate),
                                                                 std::max(plate.Height(), request.MinimumPlate)));
        WorldMapZone entry{ .Id = zone.Id, .Name = ZoneDisplayName(manifest, zone.Id), .Plate = plate };
        entry.State = zone.Id == world.FocusZone() ? WorldMapZoneState::Focus
                    : world.IsZoneOpen(zone.Id)    ? WorldMapZoneState::Open
                                                   : WorldMapZoneState::Closed;
        entry.Problem = std::ranges::find(request.Problems, zone.Id) != request.Problems.end();
        entry.Here = here == &zone;
        map.Zones.push_back(std::move(entry));
    }
    for (const ZoneGraphEdge& edge : edges)
        map.Links.push_back({ .From = toMap(edge.From.X, edge.From.Z),
                              .To = toMap(edge.To.X, edge.To.Z),
                              .AToB = edge.AToB,
                              .BToA = edge.BToA,
                              .Warned = edge.Warned,
                              .Failed = edge.Failed });
    if (request.Camera.has_value())
        map.Camera = toMap(request.Camera->X, request.Camera->Z);
    return map;
}

std::optional<ZoneId> ZoneAt(const WorldMap& map, Vec2d point)
{
    const WorldMapZone* best = nullptr;
    for (const WorldMapZone& zone : map.Zones)
        if (zone.Plate.Contains(point) && (best == nullptr || Area(zone.Plate) < Area(best->Plate)))
            best = &zone;
    return best != nullptr ? std::optional<ZoneId>(best->Id) : std::nullopt;
}
