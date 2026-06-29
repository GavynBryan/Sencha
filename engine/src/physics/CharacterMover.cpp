#include <physics/CharacterMover.h>

#include "PhysicsWorldImpl.h"

#include <algorithm>

#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>

#include <physics/PhysicsWorld.h>

namespace
{
constexpr float kDegreesToRadians = 0.0174532925199432958f;
} // namespace

struct CharacterMoverImpl
{
    JPH::PhysicsSystem* System = nullptr;
    JPH::TempAllocator* Temp = nullptr;
    JPH::Ref<JPH::CharacterVirtual> Character;
    float VerticalVelocity = 0.0f;
    bool Grounded = false;
};

CharacterMover::CharacterMover(PhysicsWorld& world, const CharacterMoverConfig& config, const Vec3d& position)
    : Impl(std::make_unique<CharacterMoverImpl>())
{
    Impl->System = &world.Internal().System;
    Impl->Temp = &world.Internal().Temp;

    const float radius = config.Radius;
    const float cylinderHalfHeight = std::max(0.0f, config.Height * 0.5f - radius);

    JPH::Ref<JPH::CharacterVirtualSettings> settings = new JPH::CharacterVirtualSettings();
    settings->mShape = new JPH::CapsuleShape(cylinderHalfHeight, radius); // centered: position is capsule center
    settings->mMaxSlopeAngle = config.SlopeLimitDegrees * kDegreesToRadians;
    settings->mMass = config.Mass;
    settings->mUp = JPH::Vec3::sAxisY();
    // Keep the character out of geometry by its own margin, like a body skin.
    settings->mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -radius);

    Impl->Character = new JPH::CharacterVirtual(settings, ToJphR(position), JPH::Quat::sIdentity(), 0, Impl->System);
}

CharacterMover::~CharacterMover() = default;
CharacterMover::CharacterMover(CharacterMover&&) noexcept = default;
CharacterMover& CharacterMover::operator=(CharacterMover&&) noexcept = default;

void CharacterMover::Move(const Vec3d& horizontalVelocity, float dt, const Vec3d& gravity)
{
    JPH::CharacterVirtual& character = *Impl->Character;

    const bool grounded = character.GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;
    if (grounded && Impl->VerticalVelocity < 0.0f)
        Impl->VerticalVelocity = 0.0f; // planted: do not accumulate downward while standing
    Impl->VerticalVelocity += static_cast<float>(gravity.Y) * dt;

    character.SetLinearVelocity(
        JPH::Vec3(horizontalVelocity.X, Impl->VerticalVelocity, horizontalVelocity.Z));

    JPH::CharacterVirtual::ExtendedUpdateSettings updateSettings;
    character.ExtendedUpdate(
        dt,
        ToJph(gravity),
        updateSettings,
        Impl->System->GetDefaultBroadPhaseLayerFilter(PhysicsObjectLayers::Character),
        Impl->System->GetDefaultLayerFilter(PhysicsObjectLayers::Character),
        {},
        {},
        *Impl->Temp);

    Impl->Grounded = character.GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;
    // The achieved vertical velocity (slide off a slope, hit a ceiling) is what
    // carries to next tick.
    Impl->VerticalVelocity = character.GetLinearVelocity().GetY();
}

void CharacterMover::Jump(float upSpeed)
{
    Impl->VerticalVelocity = upSpeed;
}

bool CharacterMover::IsGrounded() const
{
    return Impl->Grounded;
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
