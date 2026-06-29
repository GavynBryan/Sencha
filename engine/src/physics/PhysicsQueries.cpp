#include <physics/PhysicsQueries.h>

#include "PhysicsWorldImpl.h"

#include <algorithm>

#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Body/BodyLockInterface.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/ShapeCast.h>

#include <physics/PhysicsScene.h> // UnpackEntity
#include <physics/PhysicsWorld.h>

namespace
{
EntityId EntityOf(const JPH::PhysicsSystem& system, JPH::BodyID body)
{
    JPH::BodyLockRead lock(system.GetBodyLockInterface(), body);
    if (lock.Succeeded())
        return UnpackEntity(lock.GetBody().GetUserData());
    return EntityId{};
}
} // namespace

RaycastHit PhysicsQueries::Raycast(const Vec3d& origin, const Vec3d& direction, float maxDistance) const
{
    const JPH::PhysicsSystem& system = Simulation->Internal().System;
    const JPH::NarrowPhaseQuery& query = system.GetNarrowPhaseQuery();

    const JPH::RRayCast ray{ ToJphR(origin), ToJph(direction) * maxDistance };
    JPH::RayCastResult result;
    if (!query.CastRay(ray, result))
        return RaycastHit{};

    RaycastHit hit;
    hit.Hit = true;
    hit.Distance = result.mFraction * maxDistance;
    const JPH::RVec3 point = ray.GetPointOnRay(result.mFraction);
    hit.Point = FromJphR(point);

    JPH::BodyLockRead lock(system.GetBodyLockInterface(), result.mBodyID);
    if (lock.Succeeded())
    {
        const JPH::Body& body = lock.GetBody();
        hit.Normal = FromJph(body.GetWorldSpaceSurfaceNormal(result.mSubShapeID2, point));
        hit.Entity = UnpackEntity(body.GetUserData());
    }
    return hit;
}

ShapeSweepHit PhysicsQueries::SweepShape(
    const CollisionShape& shape,
    const Vec3d& origin,
    const Quatf& rotation,
    const Vec3d& direction,
    float maxDistance) const
{
    const JPH::ShapeRefC jShape = BuildShape(shape);
    if (jShape == nullptr)
        return ShapeSweepHit{};

    const JPH::PhysicsSystem& system = Simulation->Internal().System;
    const JPH::NarrowPhaseQuery& query = system.GetNarrowPhaseQuery();

    const JPH::RMat44 start = JPH::RMat44::sRotationTranslation(ToJph(rotation), ToJphR(origin));
    const JPH::RShapeCast cast(jShape, JPH::Vec3::sReplicate(1.0f), start, ToJph(direction) * maxDistance);

    JPH::ShapeCastSettings settings;
    JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
    query.CastShape(cast, settings, JPH::RVec3::sZero(), collector);
    if (!collector.HadHit())
        return ShapeSweepHit{};

    ShapeSweepHit hit;
    hit.Hit = true;
    hit.Fraction = collector.mHit.mFraction;
    hit.Point = FromJph(collector.mHit.mContactPointOn2);
    // mPenetrationAxis points from the sweeping shape into the hit body; the
    // surface normal facing the sweeper is its negation.
    hit.Normal = FromJph(-collector.mHit.mPenetrationAxis.Normalized());
    hit.Entity = EntityOf(system, collector.mHit.mBodyID2);
    return hit;
}

void PhysicsQueries::OverlapShape(
    const CollisionShape& shape,
    const Vec3d& position,
    const Quatf& rotation,
    std::vector<EntityId>& out) const
{
    out.clear();

    const JPH::ShapeRefC jShape = BuildShape(shape);
    if (jShape == nullptr)
        return;

    const JPH::PhysicsSystem& system = Simulation->Internal().System;
    const JPH::NarrowPhaseQuery& query = system.GetNarrowPhaseQuery();

    const JPH::RMat44 transform = JPH::RMat44::sRotationTranslation(ToJph(rotation), ToJphR(position));
    JPH::CollideShapeSettings settings;
    JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
    query.CollideShape(jShape, JPH::Vec3::sReplicate(1.0f), transform, settings, JPH::RVec3::sZero(), collector);

    // Dedup in place using the caller's vector: gather every hit's entity, then
    // sort + unique. One body maps to one entity, so this matches a by-body dedup
    // without a per-call hash set. Order is unspecified by the contract.
    for (const JPH::CollideShapeResult& result : collector.mHits)
        out.push_back(EntityOf(system, result.mBodyID2));

    std::sort(out.begin(), out.end(), [](EntityId a, EntityId b)
    {
        return a.Index != b.Index ? a.Index < b.Index : a.Generation < b.Generation;
    });
    out.erase(std::unique(out.begin(), out.end()), out.end());
}
