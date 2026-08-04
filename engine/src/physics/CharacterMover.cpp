#include <physics/CharacterMover.h>

#include "PhysicsWorldImpl.h"

#include <algorithm>

#include <Jolt/Physics/Body/BodyLockInterface.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>

#include <physics/PhysicsWorld.h>
#include <physics/RigidBodyBinding.h> // UnpackEntity

namespace
{
constexpr float kDegreesToRadians = 0.0174532925199432958f;
constexpr float kMinimumAxisLengthSquared = 1.0e-8f;

Vec3d SafeUpAxis(const Vec3d& candidate)
{
    if (candidate.SqrMagnitude() <= kMinimumAxisLengthSquared)
        return Vec3d(0.0f, 1.0f, 0.0f);
    return candidate.Normalized();
}

CharacterSupportKind ClassifySupport(const JPH::CharacterVirtual& character)
{
    switch (character.GetGroundState())
    {
    case JPH::CharacterBase::EGroundState::OnGround:
        return CharacterSupportKind::Stable;
    case JPH::CharacterBase::EGroundState::OnSteepGround:
    case JPH::CharacterBase::EGroundState::NotSupported:
        return CharacterSupportKind::Steep;
    case JPH::CharacterBase::EGroundState::InAir:
        break;
    }
    return CharacterSupportKind::None;
}

// The ground body carries its owning entity in its user data, the same packing
// the rigid-body binding writes.
EntityId ResolveGroundEntity(const JPH::PhysicsSystem& system,
                             const JPH::CharacterVirtual& character)
{
    const JPH::BodyID body = character.GetGroundBodyID();
    if (body.IsInvalid())
        return {};

    const JPH::BodyLockRead lock(system.GetBodyLockInterface(), body);
    if (!lock.Succeeded())
        return {};

    return UnpackEntity(lock.GetBody().GetUserData());
}
} // namespace

struct CharacterMoverImpl
{
    JPH::PhysicsSystem* System = nullptr;
    JPH::TempAllocator* Temp = nullptr;
    JPH::Ref<JPH::CharacterVirtual> Character;
    float StepHeight = 0.35f;
    float GroundSnapDistance = 0.25f;
};

CharacterMover::CharacterMover(PhysicsWorld& world, const CharacterMoverConfig& config, const Vec3d& position)
    : Impl(std::make_unique<CharacterMoverImpl>())
{
    Impl->System = &world.Internal().System;
    Impl->Temp = &world.Internal().Temp;
    Impl->StepHeight = std::max(0.0f, config.StepHeight);
    Impl->GroundSnapDistance = std::max(0.0f, config.GroundSnapDistance);

    const float radius = config.Radius;
    const float cylinderHalfHeight = std::max(0.0f, config.Height * 0.5f - radius);

    JPH::Ref<JPH::CharacterVirtualSettings> settings = new JPH::CharacterVirtualSettings();
    settings->mShape = new JPH::CapsuleShape(cylinderHalfHeight, radius); // centered: position is capsule center
    settings->mMaxSlopeAngle = config.SlopeLimitDegrees * kDegreesToRadians;
    settings->mMass = config.Mass;
    settings->mUp = JPH::Vec3::sAxisY();
    settings->mCharacterPadding = std::max(0.0f, config.SkinWidth);
    // Keep the character out of geometry by its own margin, like a body skin.
    settings->mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -radius);

    Impl->Character = new JPH::CharacterVirtual(settings, ToJphR(position), JPH::Quat::sIdentity(), 0, Impl->System);
}

CharacterMover::~CharacterMover() = default;
CharacterMover::CharacterMover(CharacterMover&&) noexcept = default;
CharacterMover& CharacterMover::operator=(CharacterMover&&) noexcept = default;

CharacterMoveResult CharacterMover::Move(const CharacterMoveRequest& request)
{
    JPH::CharacterVirtual& character = *Impl->Character;

    const Vec3d up = SafeUpAxis(request.UpAxis);

    // Gravity reaches the backend as a direction for its stair and floor-stick
    // passes only. Integrating it is locomotion's job: doing it here as well
    // would apply it twice, and the request that arrives is already the full
    // composed velocity.
    const Vec3d gravity = request.Gravity * request.GravityScale;

    character.SetLinearVelocity(ToJph(request.Velocity));
    character.SetUp(ToJph(up));

    JPH::CharacterVirtual::ExtendedUpdateSettings updateSettings;
    updateSettings.mWalkStairsStepUp = ToJph(up * Impl->StepHeight);
    if (request.AllowGroundSnap)
    {
        updateSettings.mStickToFloorStepDown = ToJph(up * -Impl->GroundSnapDistance);
    }
    else
    {
        updateSettings.mStickToFloorStepDown = JPH::Vec3::sZero();
    }

    character.ExtendedUpdate(
        request.DeltaSeconds,
        ToJph(gravity),
        updateSettings,
        Impl->System->GetDefaultBroadPhaseLayerFilter(PhysicsObjectLayers::Character),
        Impl->System->GetDefaultLayerFilter(PhysicsObjectLayers::Character),
        {},
        {},
        *Impl->Temp);

    CharacterMoveResult result;
    result.Position = FromJphR(character.GetPosition());
    // The achieved velocity, not the requested one: sliding off a slope or
    // hitting a ceiling is exactly what the caller needs to see.
    result.Velocity = FromJph(character.GetLinearVelocity());

    result.Support.Kind = ClassifySupport(character);
    if (result.Support.Kind != CharacterSupportKind::None)
    {
        result.Support.Surface = ResolveGroundEntity(*Impl->System, character);
        result.Support.ContactPoint = FromJphR(character.GetGroundPosition());
        result.Support.Normal = FromJph(character.GetGroundNormal());
        result.Support.Velocity = FromJph(character.GetGroundVelocity());
    }

    return result;
}

Vec3d CharacterMover::GetPosition() const
{
    return FromJphR(Impl->Character->GetPosition());
}

void CharacterMover::SetPosition(const Vec3d& position)
{
    Impl->Character->SetPosition(ToJphR(position));
}

Vec3d CharacterMover::GetVelocity() const
{
    return FromJph(Impl->Character->GetLinearVelocity());
}
