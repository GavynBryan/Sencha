#pragma once

#include <world/transform/TransformComponents.h>
#include <zone/WorldConnectionComponents.h>
#include <zone/WorldPartitionManifest.h>

#include <algorithm>
#include <utility>
#include <vector>

// Dock semantic command kernel. The caller wraps the before/after pair in one
// command so zone references, directionality, and the physical plane rotate
// atomically.
inline void SwapWorldDockSides(WorldDock& dock, Transform3f& transform)
{
    constexpr float pi = 3.14159265358979323846f;
    std::swap(dock.ZoneA, dock.ZoneB);
    dock.Directions = ((dock.Directions & DockDirectionAToB)
                       ? DockDirectionBToA : 0u)
        | ((dock.Directions & DockDirectionBToA)
           ? DockDirectionAToB : 0u);
    transform.Rotation = transform.Rotation
        * Quatf::FromAxisAngle(Vec3d::Up(), pi);
}

// A bounded-plane edge edit is stored as center + half extents. Moving its
// center by half the dragged-edge delta preserves the opposite physical edge.
inline void ApplyWorldDockPlaneResize(WorldDock& dock, Transform3f& transform,
                                      Vec2d localCenterOffset,
                                      Vec2d halfExtents)
{
    transform.Position += transform.Right() * localCenterOffset.X
        + transform.Up() * localCenterOffset.Y;
    dock.HalfExtents = halfExtents;
}

// Coarse editor suggestion only. Overlap is legal, so every containing AABB is
// returned and callers must require explicit choice when size() > 1.
inline std::vector<ZoneId> WorldDockZoneCandidates(
    const WorldPartitionManifest& manifest, Vec3d point, ZoneId exclude = {})
{
    std::vector<ZoneId> candidates;
    for (const ZoneHeader& zone : manifest.Zones)
        if (zone.Id != exclude && zone.Bounds.IsValid()
            && zone.Bounds.Contains(point))
            candidates.push_back(zone.Id);
    std::sort(candidates.begin(), candidates.end(),
              [](ZoneId a, ZoneId b) { return a.Value < b.Value; });
    return candidates;
}
