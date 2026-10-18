#include "SensorShapeQuery.h"

#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/ShapeCast.h>

#include <physics/RigidBodyBinding.h>
#include <physics/SensorOverlaps.h>

namespace
{
class SensorLayers final : public JPH::ObjectLayerFilter
{
public:
    bool ShouldCollide(JPH::ObjectLayer layer) const override { return layer == PhysicsObjectLayers::Trigger; }
};

template<class Collector>
class SensorCollector final : public Collector
{
public:
    SensorCollector(const JPH::PhysicsSystem& system, EntityId activator,
                    SensorOverlaps& observations, bool occupied)
        : System(system), Activator(activator), Observations(observations), Occupied(occupied) {}

    void AddHit(const typename Collector::ResultType& hit) override
    {
        JPH::BodyLockRead lock(System.GetBodyLockInterface(), hit.mBodyID2);
        if (lock.Succeeded() && lock.GetBody().IsSensor())
            if (!Observations.Observe({ UnpackEntity(lock.GetBody().GetUserData()), Activator }, Occupied))
                this->ForceEarlyOut();
    }

private:
    const JPH::PhysicsSystem& System;
    EntityId Activator;
    SensorOverlaps& Observations;
    bool Occupied;
};
}

void ObserveSensorShape(const JPH::PhysicsSystem& system, const JPH::Shape& shape,
                        JPH::RMat44Arg end, const Vec3d& displacement,
                        EntityId activator, SensorOverlaps& observations)
{
    observations.RecordQueries(displacement.SqrMagnitude() > 1.0e-10f);
    const auto& query = system.GetNarrowPhaseQuery();
    SensorCollector<JPH::CollideShapeCollector> overlaps(system, activator, observations, true);
    query.CollideShape(&shape, JPH::Vec3::sReplicate(1.0f), end,
                       JPH::CollideShapeSettings{}, end.GetTranslation(), overlaps, {}, SensorLayers{});
    if (displacement.SqrMagnitude() > 1.0e-10f)
    {
        SensorCollector<JPH::CastShapeCollector> crossings(system, activator, observations, false);
        const auto start = end.PostTranslated(-ToJph(displacement));
        const JPH::RShapeCast cast(&shape, JPH::Vec3::sReplicate(1.0f), start, ToJph(displacement));
        query.CastShape(cast, JPH::ShapeCastSettings{}, start.GetTranslation(), crossings, {}, SensorLayers{});
    }
}
