#pragma once

// Cooked dock endpoints for manifest fixtures.
//
// WorldPartitionRuntime accepts only migrated manifests: authored transitions
// are compiled to reciprocal dock endpoints during the world cook, and a
// manifest that still carries transitions is refused. Fixtures that describe
// their topology as transition JSON therefore clear it after parsing and add
// the dock pairs the cook would have produced.

#include <zone/WorldPartitionManifest.h>

#include <cstdint>

inline ZoneHeader* FindFixtureZone(WorldPartitionManifest& manifest, ZoneId id)
{
    for (ZoneHeader& zone : manifest.Zones)
        if (zone.Id == id)
            return &zone;
    return nullptr;
}

// One dock, both sides. Side A faces +X from zoneA; side B is its reciprocal.
// Origin is the dock plane's center in world space, so callers place it on the
// boundary the two zones share.
inline void AddFixtureDockPair(WorldPartitionManifest& manifest, std::uint64_t id,
                               ZoneId zoneA, ZoneId zoneB, Vec3d origin)
{
    ZoneHeader* a = FindFixtureZone(manifest, zoneA);
    ZoneHeader* b = FindFixtureZone(manifest, zoneB);
    if (a == nullptr || b == nullptr)
        return;
    DockEndpoint endpoint{
        .Id = DockId{ id },
        .OwnerZone = zoneA,
        .OtherZone = zoneB,
        .Side = DockSide::A,
        .Origin = origin,
        .Normal = Vec3d{ 1, 0, 0 },
        .Right = Vec3d{ 0, 0, 1 },
        .Up = Vec3d{ 0, 1, 0 },
        .HalfExtents = Vec2d{ 8, 8 },
        .Directions = 3,
    };
    a->Docks.push_back(endpoint);
    endpoint.OwnerZone = zoneB;
    endpoint.OtherZone = zoneA;
    endpoint.Side = DockSide::B;
    endpoint.Normal = Vec3d{ -1, 0, 0 };
    endpoint.Right = Vec3d{ 0, 0, -1 };
    b->Docks.push_back(endpoint);
}
