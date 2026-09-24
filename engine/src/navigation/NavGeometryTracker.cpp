#include "NavGeometryTracker.h"

#include "NavColliderTriangles.h"

#include <ecs/World.h>

#include <algorithm>
#include <bit>
#include <tuple>

namespace
{
    std::uint64_t HashShape(const Collider& collider)
    {
        std::uint64_t hash = 1469598103934665603ull;
        const auto mix = [&hash](std::uint64_t value)
        {
            hash ^= value;
            hash *= 1099511628211ull;
        };
        const CollisionShape& shape = collider.Shape;
        mix(static_cast<std::uint64_t>(shape.Type));
        for (const float value : { shape.HalfExtents.X, shape.HalfExtents.Y, shape.HalfExtents.Z,
                                   shape.Radius, shape.HalfHeight })
            mix(std::bit_cast<std::uint32_t>(value));
        return hash;
    }

    bool EntityBefore(const NavGeometryContributor& a, const NavGeometryContributor& b)
    {
        return std::tie(a.Entity.Index, a.Entity.Generation)
             < std::tie(b.Entity.Index, b.Entity.Generation);
    }

    bool Reshaped(const NavGeometryContributor& before, const NavGeometryContributor& now)
    {
        return before.ShapeHash != now.ShapeHash
            || !(before.Transform.Rotation == now.Transform.Rotation)
            || !(before.Transform.Scale == now.Transform.Scale);
    }
}

void NavGeometryContributor::AppendTriangles(std::vector<Vec3d>& positions,
                                             std::vector<std::uint32_t>& indices) const
{
    std::vector<Vec3d> local;
    TriangulateCollider(Shape, local);
    for (const Vec3d& vertex : local)
    {
        indices.push_back(static_cast<std::uint32_t>(positions.size()));
        positions.push_back(Transform.TransformPoint(vertex));
    }
}

void NavGeometryTracker::Collect(World& world, ScanResult& result)
{
    Scanned.clear();
    if (!world.IsRegistered<NavigationGeometry>() || !world.IsRegistered<Collider>()
        || !world.IsRegistered<WorldTransform>())
        return;
    if (Colliders == nullptr)
        Colliders = std::make_unique<ColliderQuery>(world);

    Colliders->ForEachChunk([&](auto& view)
    {
        const auto colliders = view.template Read<Collider>();
        const auto transforms = view.template Read<WorldTransform>();
        for (std::uint32_t i = 0; i < view.Count(); ++i)
        {
            if (colliders[i].IsTrigger)
                continue;
            if (colliders[i].Mesh.IsValid())
            {
                ++result.SkippedMeshColliders;
                continue;
            }
            NavGeometryContributor& contributor = Scanned.emplace_back();
            contributor.Entity = view.Entity(i);
            contributor.ShapeHash = HashShape(colliders[i]);
            contributor.Shape = colliders[i].Shape;
            contributor.Transform = transforms[i].Value;
            Scratch.clear();
            TriangulateCollider(contributor.Shape, Scratch);
            contributor.Bounds = Aabb3d::Empty();
            for (const Vec3d& vertex : Scratch)
                contributor.Bounds.ExpandToInclude(contributor.Transform.TransformPoint(vertex));
        }
    });
    std::ranges::sort(Scanned, EntityBefore);
    result.Contributors = Scanned.size();
}

NavGeometryTracker::ScanResult NavGeometryTracker::Scan(World& world, float moveThreshold,
                                                        std::vector<Aabb3d>& changed)
{
    ScanResult result;
    Collect(world, result);

    // Merge the previous table with this scan, both ordered by entity.
    std::vector<NavGeometryContributor> next;
    next.reserve(Scanned.size());
    auto before = Current.begin();
    auto now = Scanned.begin();
    while (before != Current.end() || now != Scanned.end())
    {
        if (now == Scanned.end() || (before != Current.end() && EntityBefore(*before, *now)))
        {
            changed.push_back((before++)->Bounds); // removed
            continue;
        }
        if (before == Current.end() || EntityBefore(*now, *before))
        {
            changed.push_back(now->Bounds); // added
            next.push_back(*now++);
            continue;
        }
        const float moved = (now->Transform.Position - before->Transform.Position).Magnitude();
        if (Reshaped(*before, *now) || moved > moveThreshold)
        {
            changed.push_back(before->Bounds);
            changed.push_back(now->Bounds);
            next.push_back(*now);
        }
        else
        {
            next.push_back(*before);
        }
        ++before;
        ++now;
    }
    Current = std::move(next);
    return result;
}
