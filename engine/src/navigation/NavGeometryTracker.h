#pragma once

#include <ecs/EntityId.h>
#include <ecs/Query.h>
#include <math/geometry/3d/Aabb3d.h>
#include <math/geometry/3d/Transform3d.h>
#include <navigation/NavigationGeometry.h>
#include <physics/components/Collider.h>
#include <world/transform/TransformComponents.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

class World;

// A collider tagged NavigationGeometry, as navigation last saw it.
struct NavGeometryContributor
{
    EntityId Entity;
    std::uint64_t ShapeHash = 0;
    CollisionShape Shape;
    Transform3f Transform;
    Aabb3d Bounds;

    // Appends the collider's world-space triangles.
    void AppendTriangles(std::vector<Vec3d>& positions, std::vector<std::uint32_t>& indices) const;
};

// Tracks tagged colliders from tick to tick and reports where walkable space
// may have changed: the bounds of every collider added, removed, reshaped, or
// moved past a threshold. A collider that moves less keeps its recorded pose,
// so small moves accumulate until they cross the threshold.
class NavGeometryTracker
{
public:
    struct ScanResult
    {
        std::size_t Contributors = 0;
        // Colliders referencing a mesh shape; they cannot contribute triangles yet.
        std::size_t SkippedMeshColliders = 0;
    };

    // Appends to `changed` the bounds, before and after, of every change since
    // the previous scan.
    ScanResult Scan(World& world, float moveThreshold, std::vector<Aabb3d>& changed);

    // Current contributors, ordered by entity.
    [[nodiscard]] std::span<const NavGeometryContributor> Contributors() const { return Current; }

private:
    using ColliderQuery = Query<Read<Collider>, Read<WorldTransform>, With<NavigationGeometry>>;

    void Collect(World& world, ScanResult& result);

    std::unique_ptr<ColliderQuery> Colliders;
    std::vector<NavGeometryContributor> Current;
    std::vector<NavGeometryContributor> Scanned;
    std::vector<Vec3d> Scratch;
};
