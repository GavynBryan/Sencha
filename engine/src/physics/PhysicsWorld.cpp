#include <physics/PhysicsWorld.h>

#include "CollisionShapeCacheImpl.h"
#include "JoltStartup.h"
#include "PhysicsWorldImpl.h"

#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/EActivation.h>

#include <physics/CollisionShapeCache.h>

PhysicsWorld::PhysicsWorld(const PhysicsWorldConfig& config)
    : Impl(nullptr)
{
    EnsureJoltRegistered();
    Impl = std::make_unique<PhysicsWorldImpl>();
    Impl->System.Init(
        config.MaxBodies,
        0,
        config.MaxBodyPairs,
        config.MaxContactConstraints,
        Impl->BroadPhaseLayers,
        Impl->ObjectVsBroadPhase,
        Impl->ObjectVsObject);
    Impl->System.SetGravity(ToJph(config.Gravity));
}

PhysicsWorld::~PhysicsWorld() = default;
PhysicsWorld::PhysicsWorld(PhysicsWorld&&) noexcept = default;
PhysicsWorld& PhysicsWorld::operator=(PhysicsWorld&&) noexcept = default;

void PhysicsWorld::SetShapeCache(const CollisionShapeCache* cache)
{
    ShapeCache = cache;
}

void PhysicsWorld::Step(float dt, int collisionSteps)
{
    Impl->System.Update(dt, collisionSteps, &Impl->Temp, &Impl->Jobs);
}

PhysicsBodyId PhysicsWorld::AddBody(const BodyDesc& desc)
{
    JPH::ShapeRefC shape;
    if (desc.MeshShape.IsValid() && ShapeCache != nullptr)
        shape = ShapeCache->Internal().Resolve(desc.MeshShape);
    else
        shape = BuildShape(desc.Shape);
    if (shape == nullptr)
        return PhysicsBodyId{};

    JPH::BodyCreationSettings settings(
        shape,
        ToJphR(desc.Position),
        ToJph(desc.Rotation),
        ToJph(desc.Motion),
        ToObjectLayer(desc.Layer));
    settings.mIsSensor = desc.IsTrigger;
    settings.mUserData = desc.UserData;
    if (desc.Motion == BodyMotion::Dynamic && desc.Mass > 0.0f)
    {
        settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        settings.mMassPropertiesOverride.mMass = desc.Mass;
    }

    const JPH::EActivation activation =
        desc.Motion == BodyMotion::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate;

    JPH::BodyInterface& bodies = Impl->System.GetBodyInterface();
    const JPH::BodyID id = bodies.CreateAndAddBody(settings, activation);
    return FromJph(id);
}

void PhysicsWorld::RemoveBody(PhysicsBodyId id)
{
    if (!id.IsValid())
        return;
    JPH::BodyInterface& bodies = Impl->System.GetBodyInterface();
    const JPH::BodyID bid = ToJph(id);
    bodies.RemoveBody(bid);
    bodies.DestroyBody(bid);
}

BodyTransform PhysicsWorld::GetBodyTransform(PhysicsBodyId id) const
{
    const JPH::BodyInterface& bodies = Impl->System.GetBodyInterface();
    const JPH::BodyID bid = ToJph(id);
    return BodyTransform{ FromJphR(bodies.GetPosition(bid)), FromJph(bodies.GetRotation(bid)) };
}

void PhysicsWorld::SetBodyTransform(PhysicsBodyId id, const Vec3d& position, const Quatf& rotation)
{
    JPH::BodyInterface& bodies = Impl->System.GetBodyInterface();
    bodies.SetPositionAndRotation(ToJph(id), ToJphR(position), ToJph(rotation), JPH::EActivation::Activate);
}

Vec3d PhysicsWorld::GetLinearVelocity(PhysicsBodyId id) const
{
    const JPH::BodyInterface& bodies = Impl->System.GetBodyInterface();
    return FromJph(bodies.GetLinearVelocity(ToJph(id)));
}

void PhysicsWorld::SetLinearVelocity(PhysicsBodyId id, const Vec3d& velocity)
{
    JPH::BodyInterface& bodies = Impl->System.GetBodyInterface();
    bodies.SetLinearVelocity(ToJph(id), ToJph(velocity));
}

uint64_t PhysicsWorld::GetUserData(PhysicsBodyId id) const
{
    const JPH::BodyInterface& bodies = Impl->System.GetBodyInterface();
    return bodies.GetUserData(ToJph(id));
}

uint32_t PhysicsWorld::BodyCount() const
{
    return Impl->System.GetNumBodies();
}
