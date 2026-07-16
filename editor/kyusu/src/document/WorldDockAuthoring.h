#pragma once

#include <world/transform/TransformComponents.h>
#include <zone/WorldConnectionComponents.h>

#include <utility>

[[nodiscard]] inline Aabb3d ReflectDockLocalXZ(const Aabb3d& bounds)
{
    return Aabb3d::FromMinMax(
        Vec3d{ -bounds.Max.X, bounds.Min.Y, -bounds.Max.Z },
        Vec3d{ -bounds.Min.X, bounds.Max.Y, -bounds.Min.Z });
}

// Dock semantic command kernel. Rotating the entity and reflecting the local
// arm boxes preserves their world placement while reversing the authored side
// frame. The caller wraps the before/after pair in one command.
inline void SwapWorldDockSides(WorldDock& dock, Transform3f& transform)
{
    constexpr float pi = 3.14159265358979323846f;
    const Aabb3d oldA = dock.SideAArmBounds;
    const Aabb3d oldB = dock.SideBArmBounds;
    std::swap(dock.ZoneA, dock.ZoneB);
    dock.SideAArmBounds = ReflectDockLocalXZ(oldB);
    dock.SideBArmBounds = ReflectDockLocalXZ(oldA);
    dock.Directions = ((dock.Directions & DockDirectionAToB)
                       ? DockDirectionBToA : 0u)
        | ((dock.Directions & DockDirectionBToA)
           ? DockDirectionAToB : 0u);
    transform.Rotation = transform.Rotation
        * Quatf::FromAxisAngle(Vec3d::Up(), pi);
}
