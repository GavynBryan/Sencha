#include <physics/PhysicsWorld.h>

#include "CollisionShapeCacheImpl.h"
#include "JoltStartup.h"
#include "PhysicsWorldImpl.h"

#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/EActivation.h>

#include <physics/CollisionShapeCache.h>

#include <cassert>
#include <cmath>

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
    Impl->MaxDriveClosingSpeed = config.MaxDriveClosingSpeed;
    Impl->MaxDriveClosingAngularSpeed = config.MaxDriveClosingAngularSpeed;
}

PhysicsWorld::~PhysicsWorld() = default;
PhysicsWorld::PhysicsWorld(PhysicsWorld&&) noexcept = default;
PhysicsWorld& PhysicsWorld::operator=(PhysicsWorld&&) noexcept = default;

void PhysicsWorld::SetShapeCache(const CollisionShapeCache* cache)
{
    ShapeCache = cache;
}

namespace
{
float DeltaAxisAngle(const Quatf& from, const Quatf& to, Vec3d& axisOut)
{
    Quatf delta = (to * from.Inverse()).Normalized();
    if (delta.W < 0.0f)
        delta = Quatf(-delta.X, -delta.Y, -delta.Z, -delta.W);

    const float w = delta.W > 1.0f ? 1.0f : delta.W;
    const float angle = 2.0f * std::acos(w);
    const float s = std::sqrt(1.0f - w * w);
    if (s < 1e-6f || angle < 1e-6f)
    {
        axisOut = Vec3d::Zero();
        return 0.0f;
    }
    axisOut = Vec3d(delta.X / s, delta.Y / s, delta.Z / s);
    return angle;
}
} // namespace

void PhysicsWorld::Step(float dt, int collisionSteps)
{
    DriveConstraints(dt);
    Impl->System.Update(dt, collisionSteps, &Impl->Temp, &Impl->Jobs);
}

// Locked velocity-drive prototype. The target pose is the frame at the start of
// this step; its supplied velocity predicts motion through the step. The error
// correction and feed-forward terms therefore each appear exactly once.
void PhysicsWorld::DriveConstraints(float dt)
{
    if (dt <= 0.0f)
        return;

    JPH::BodyInterface& bodies = Impl->System.GetBodyInterface();

    for (DrivenPoseSlot& slot : Impl->Constraints)
    {
        if (!slot.Alive)
            continue;

        if (!slot.Refreshed)
        {
            assert(false && "Driven pose constraint not refreshed since the last step: "
                            "orchestration must SetDrivenPoseTarget every step.");
            ++Impl->StaleRefreshes;
            continue;
        }
        slot.Refreshed = false;

        const JPH::BodyID body = ToJph(slot.Follower);

        const Quatf desiredRot =
            (slot.Target.WorldFrame.Rotation * slot.FollowerLocalFrame.Rotation.Inverse())
                .Normalized();
        const Vec3d desiredPos =
            slot.Target.WorldFrame.Position - desiredRot.RotateVector(slot.FollowerLocalFrame.Position);

        if (slot.Target.Teleported)
        {
            bodies.SetPositionAndRotation(body, ToJphR(desiredPos), ToJph(desiredRot),
                                          JPH::EActivation::Activate);
            bodies.SetLinearVelocity(body, ToJph(slot.Target.LinearVelocity));
            bodies.SetAngularVelocity(body, ToJph(slot.Target.AngularVelocity));
            slot.Telemetry = PhysicsConstraintTelemetry{};
            continue;
        }

        const BodyTransform pose{ FromJphR(bodies.GetPosition(body)),
                                  FromJph(bodies.GetRotation(body)) };

        const Vec3d positionError = desiredPos - pose.Position;
        Vec3d axis;
        const float angularError = DeltaAxisAngle(pose.Rotation, desiredRot, axis);

        Vec3d closing = positionError * (1.0f / dt);
        const float closingSpeed = closing.Magnitude();
        if (closingSpeed > Impl->MaxDriveClosingSpeed)
            closing = closing * (Impl->MaxDriveClosingSpeed / closingSpeed);

        float closingAngular = angularError / dt;
        if (closingAngular > Impl->MaxDriveClosingAngularSpeed)
            closingAngular = Impl->MaxDriveClosingAngularSpeed;

        const Vec3d velocity = closing + slot.Target.LinearVelocity;
        const Vec3d angularVelocity = axis * closingAngular + slot.Target.AngularVelocity;

        bodies.SetLinearVelocity(body, ToJph(velocity));
        bodies.SetAngularVelocity(body, ToJph(angularVelocity));

        slot.Telemetry.PositionError = positionError.Magnitude();
        slot.Telemetry.AngularError = angularError;
        slot.Telemetry.CommandedLinearSpeed = velocity.Magnitude();
        slot.Telemetry.CommandedAngularSpeed = angularVelocity.Magnitude();
    }
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
    settings.mGravityFactor = desc.GravityScale;
    settings.mLinearDamping = desc.LinearDamping;
    settings.mAngularDamping = desc.AngularDamping;
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

    for (uint32_t i = 0; i < Impl->Constraints.size(); ++i)
    {
        DrivenPoseSlot& slot = Impl->Constraints[i];
        if (slot.Alive && slot.Follower == id)
        {
            slot.Alive = false;
            ++slot.Generation;
            Impl->FreeConstraintSlots.push_back(i);
            --Impl->AliveConstraints;
        }
    }

    JPH::BodyInterface& bodies = Impl->System.GetBodyInterface();
    const JPH::BodyID bid = ToJph(id);

    bool hadBounds = false;
    JPH::AABox bounds;
    {
        JPH::BodyLockRead lock(Impl->System.GetBodyLockInterface(), bid);
        if (lock.Succeeded())
        {
            bounds = lock.GetBody().GetWorldSpaceBounds();
            hadBounds = true;
        }
    }

    bodies.RemoveBody(bid);
    bodies.DestroyBody(bid);

    if (hadBounds)
    {
        bounds.ExpandBy(JPH::Vec3::sReplicate(0.1f));
        const JPH::BroadPhaseLayerFilter allBroadPhase;
        const JPH::ObjectLayerFilter allObjects;
        bodies.ActivateBodiesInAABox(bounds, allBroadPhase, allObjects);
    }
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

Vec3d PhysicsWorld::GetAngularVelocity(PhysicsBodyId id) const
{
    const JPH::BodyInterface& bodies = Impl->System.GetBodyInterface();
    return FromJph(bodies.GetAngularVelocity(ToJph(id)));
}

void PhysicsWorld::SetAngularVelocity(PhysicsBodyId id, const Vec3d& velocity)
{
    JPH::BodyInterface& bodies = Impl->System.GetBodyInterface();
    bodies.SetAngularVelocity(ToJph(id), ToJph(velocity));
}

float PhysicsWorld::GetGravityScale(PhysicsBodyId id) const
{
    const JPH::BodyInterface& bodies = Impl->System.GetBodyInterface();
    return bodies.GetGravityFactor(ToJph(id));
}

void PhysicsWorld::SetGravityScale(PhysicsBodyId id, float scale)
{
    JPH::BodyInterface& bodies = Impl->System.GetBodyInterface();
    bodies.SetGravityFactor(ToJph(id), scale);
}

bool PhysicsWorld::IsBodyActive(PhysicsBodyId id) const
{
    const JPH::BodyInterface& bodies = Impl->System.GetBodyInterface();
    return bodies.IsActive(ToJph(id));
}

void PhysicsWorld::WakeBody(PhysicsBodyId id)
{
    JPH::BodyInterface& bodies = Impl->System.GetBodyInterface();
    bodies.ActivateBody(ToJph(id));
}

PhysicsConstraintId PhysicsWorld::AddDrivenPoseConstraint(const DrivenPoseConstraintDesc& desc)
{
    const JPH::BodyInterface& bodies = Impl->System.GetBodyInterface();
    if (!desc.Follower.IsValid() || !bodies.IsAdded(ToJph(desc.Follower))
        || bodies.GetMotionType(ToJph(desc.Follower)) != JPH::EMotionType::Dynamic)
    {
        assert(false && "AddDrivenPoseConstraint: follower must be a live dynamic body");
        return PhysicsConstraintId{};
    }

    const bool supported =
        desc.LinearDrive.Response == PoseDriveResponse::Locked
        && desc.AngularDrive.Response == PoseDriveResponse::Locked
        && std::isinf(desc.LinearDrive.MaxForce)
        && std::isinf(desc.AngularDrive.MaxTorque);
    if (!supported)
    {
        assert(false && "AddDrivenPoseConstraint: spring and force/torque limits require P3");
        return PhysicsConstraintId{};
    }

    uint32_t index;
    if (!Impl->FreeConstraintSlots.empty())
    {
        index = Impl->FreeConstraintSlots.back();
        Impl->FreeConstraintSlots.pop_back();
    }
    else
    {
        index = static_cast<uint32_t>(Impl->Constraints.size());
        Impl->Constraints.emplace_back();
    }

    DrivenPoseSlot& slot = Impl->Constraints[index];
    slot.Alive = true;
    slot.Follower = desc.Follower;
    slot.FollowerLocalFrame = desc.FollowerLocalFrame;
    slot.LinearDrive = desc.LinearDrive;
    slot.AngularDrive = desc.AngularDrive;
    slot.UserData = desc.UserData;
    slot.Target = DrivenPoseTarget{};
    slot.Refreshed = false;
    slot.Telemetry = PhysicsConstraintTelemetry{};
    ++Impl->AliveConstraints;

    return PhysicsConstraintId{ index, slot.Generation };
}

void PhysicsWorld::RemoveConstraint(PhysicsConstraintId id)
{
    if (!IsConstraintValid(id))
        return;

    DrivenPoseSlot& slot = Impl->Constraints[id.Index];
    slot.Alive = false;
    ++slot.Generation;
    Impl->FreeConstraintSlots.push_back(id.Index);
    --Impl->AliveConstraints;
}

bool PhysicsWorld::IsConstraintValid(PhysicsConstraintId id) const
{
    return id.IsValid() && id.Index < Impl->Constraints.size()
        && Impl->Constraints[id.Index].Alive
        && Impl->Constraints[id.Index].Generation == id.Generation;
}

void PhysicsWorld::SetDrivenPoseTarget(PhysicsConstraintId id, const DrivenPoseTarget& target)
{
    if (!IsConstraintValid(id))
        return;
    DrivenPoseSlot& slot = Impl->Constraints[id.Index];
    slot.Target = target;
    slot.Refreshed = true;
}

PhysicsConstraintTelemetry PhysicsWorld::GetConstraintTelemetry(PhysicsConstraintId id) const
{
    if (!IsConstraintValid(id))
        return PhysicsConstraintTelemetry{};
    return Impl->Constraints[id.Index].Telemetry;
}

uint32_t PhysicsWorld::ConstraintCount() const
{
    return Impl->AliveConstraints;
}

uint64_t PhysicsWorld::StaleRefreshCount() const
{
    return Impl->StaleRefreshes;
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
