#include "SensorShapeQuery.h"

#include <Jolt/Physics/Body/Body.h>
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
    SensorCollector(EntityId activator, SensorOverlaps& observations, bool occupied)
        : Activator(activator), Observations(observations), Occupied(occupied) {}

    void OnBody(const JPH::Body& body) override
    {
        Sensor = body.IsSensor() ? UnpackEntity(body.GetUserData()) : EntityId{};
    }

    void AddHit(const typename Collector::ResultType&) override
    {
        if (!Observations.Observe({ Sensor, Activator }, Occupied))
            this->ForceEarlyOut();
    }

private:
    EntityId Sensor;
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
    const JPH::SpecifiedBroadPhaseLayerFilter sensors(PhysicsBroadPhaseLayers::NonMoving);
    const auto& query = system.GetNarrowPhaseQuery();
    SensorCollector<JPH::CollideShapeCollector> overlaps(activator, observations, true);
    query.CollideShape(&shape, JPH::Vec3::sReplicate(1.0f), end,
                       JPH::CollideShapeSettings{}, end.GetTranslation(), overlaps, sensors, SensorLayers{});
    if (displacement.SqrMagnitude() > 1.0e-10f)
    {
        SensorCollector<JPH::CastShapeCollector> crossings(activator, observations, false);
        const auto start = end.PostTranslated(-ToJph(displacement));
        const JPH::RShapeCast cast(&shape, JPH::Vec3::sReplicate(1.0f), start, ToJph(displacement));
        query.CastShape(cast, JPH::ShapeCastSettings{}, start.GetTranslation(), crossings, sensors, SensorLayers{});
    }
}
