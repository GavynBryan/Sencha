#pragma once

#include <ecs/ComponentTypeId.h>
#include <ecs/EntityId.h>
#include <math/Vec.h>
#include <movement/MovementProfileData.h>

#include <compare>
#include <cstdint>
#include <type_traits>

//=============================================================================
// Movement components
//
// The split here is the point: a physical fact (what the world did to the
// character) is a different component from a locomotion decision (what the
// character wants), which is different again from the final motor request.
// One physical fact is stored once, not mirrored across marker membership, an
// arbiter, and a tag set.
//=============================================================================

enum class SupportKind : uint8_t
{
    None,
    Stable, // standable: within the motor's slope limit
    Steep,  // touched, but too steep to stand on
};

// What the motor found under the character last step. A fact, not a mode:
// locomotion reads it, and nothing infers a mode from it implicitly.
struct SupportState
{
    EntityId Surface;
    Vec3d ContactPoint = Vec3d::Zero();
    Vec3d Normal = Vec3d(0.0f, 1.0f, 0.0f);

    // World velocity of the surface at the contact point. Locomotion works in
    // velocity relative to this, so a character standing on a moving platform
    // is at rest in its own frame.
    Vec3d SurfaceVelocity = Vec3d::Zero();

    SupportKind Kind = SupportKind::None;
};
SENCHA_DECLARE_COMPONENT_TYPE(SupportState, "sencha.support_state");

// How deeply the character is inside a volume, in [0, 1]. A fact like
// SupportState: profiles condition on it rather than a "swimming" mode being
// switched on somewhere.
struct Immersion
{
    EntityId Volume;
    float Fraction = 0.0f;
};
SENCHA_DECLARE_COMPONENT_TYPE(Immersion, "sencha.immersion");

// Full world velocity carried between ticks. Not planar: the vertical channel
// belongs to locomotion too, so gravity and jumps compose with everything else
// instead of being owned privately by the motor.
struct KinematicState
{
    Vec3d Velocity = Vec3d::Zero();
};
SENCHA_DECLARE_COMPONENT_TYPE(KinematicState, "sencha.kinematic_state");

// Registration-order id from the LocomotionModeRegistry. Zero is the sentinel;
// these are runtime values and are never serialized.
struct LocomotionModeId
{
    uint32_t Value = 0;

    [[nodiscard]] bool IsValid() const { return Value != 0; }
    auto operator<=>(const LocomotionModeId&) const = default;
};

// What this character is in movement terms: the authored profile it resolves
// tuning from, and the locomotion mode it is currently in.
struct CharacterMovement
{
    MovementProfileHandle Profile{};
    LocomotionModeId Mode{};
};
SENCHA_DECLARE_COMPONENT_TYPE(CharacterMovement, "sencha.character_movement");

// This tick's coefficients, resolved from the profile's layers against the
// current facts. Written by the tuning resolution system and read by
// locomotion; nothing else writes it.
struct ResolvedMovementTuning
{
    float MaxSpeed = 7.0f;
    float Acceleration = 24.0f;
    float Friction = 8.0f;
    float StopSpeed = 1.0f;

    // Caps the speed the acceleration term may reach along the wish direction,
    // independent of MaxSpeed. A low value leaves existing momentum intact
    // while still allowing the character to steer.
    float WishSpeedCap = 0.0f;

    float Drag = 0.0f;
    float GravityScale = 1.0f;
    float JumpSpeed = 5.5f;
};
SENCHA_DECLARE_COMPONENT_TYPE(ResolvedMovementTuning, "sencha.resolved_movement_tuning");

// What the active locomotion mode wants, before actions compose over it.
struct LocomotionOutput
{
    Vec3d Velocity = Vec3d::Zero();
    Vec3d UpAxis = Vec3d(0.0f, 1.0f, 0.0f);
    float GravityScale = 1.0f;
};
SENCHA_DECLARE_COMPONENT_TYPE(LocomotionOutput, "sencha.locomotion_output");

// Per-channel writes from action producers this tick, in the support-relative
// frame. Ordinary producers are first-write-wins; a forced write replaces one.
// Cleared every tick by the composition system.
struct MotionAxisOverride
{
    Vec3d PlanarVelocity = Vec3d::Zero();
    float UpVelocity = 0.0f;
    bool HasPlanar = false;
    bool HasUp = false;
    bool ForcedPlanar = false;
    bool ForcedUp = false;
};
SENCHA_DECLARE_COMPONENT_TYPE(MotionAxisOverride, "sencha.motion_axis_override");

// Additive velocity from this tick's impulses (knockback, explosions). Applied
// after the override channels, then cleared.
struct MotionImpulse
{
    Vec3d DeltaVelocity = Vec3d::Zero();
};
SENCHA_DECLARE_COMPONENT_TYPE(MotionImpulse, "sencha.motion_impulse");

// The single composed result the character motor consumes. Exactly one system
// writes it.
struct MotionRequest
{
    Vec3d Velocity = Vec3d::Zero();
    Vec3d UpAxis = Vec3d(0.0f, 1.0f, 0.0f);
    float GravityScale = 1.0f;
};
SENCHA_DECLARE_COMPONENT_TYPE(MotionRequest, "sencha.motion_request");

// How hard a mode request pushes. Automatic loses to Explicit, which loses to
// Forced; a Forced request also bypasses the target mode's entry check.
enum class ModeRequestClass : uint8_t
{
    Automatic,
    Explicit,
    Forced,
};

struct ModeTransitionRequest
{
    LocomotionModeId Target;
    ModeRequestClass Class = ModeRequestClass::Automatic;
    bool Pending = false;
};
SENCHA_DECLARE_COMPONENT_TYPE(ModeTransitionRequest, "sencha.mode_transition_request");

// Session state for the built-in cling mode. A mode's session component is
// added on entry and removed on exit by the registry, so a mode that carries
// state is still just a registration.
struct ClingSession
{
    EntityId Surface;
    Vec3d SurfaceNormal = Vec3d(0.0f, -1.0f, 0.0f);
    Vec3d Anchor = Vec3d::Zero();
};
SENCHA_DECLARE_COMPONENT_TYPE(ClingSession, "sencha.cling_session");

struct FlightSession
{
    uint8_t Active = 1;
};
SENCHA_DECLARE_COMPONENT_TYPE(FlightSession, "sencha.flight_session");

static_assert(std::is_trivially_copyable_v<SupportState>);
static_assert(std::is_trivially_copyable_v<CharacterMovement>);
static_assert(std::is_trivially_copyable_v<MotionRequest>);
