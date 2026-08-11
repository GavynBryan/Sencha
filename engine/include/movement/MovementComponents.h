#pragma once

#include <core/metadata/TypeSchema.h>
#include <ecs/ComponentTypeId.h>
#include <ecs/EntityId.h>
#include <math/MathSchemas.h>
#include <math/Vec.h>
#include <movement/MovementProfileData.h>

#include <compare>
#include <cstdint>
#include <tuple>
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

// Sent to the peer that owns this character and to nobody else. Everyone can
// see where another player is; only that player's own machine has to be able to
// resume simulating them from it, which is what reconciliation does after every
// snapshot.
template <>
struct TypeSchema<SupportState>
{
    static constexpr std::string_view Name = "SupportState";
    static constexpr bool Replicated = true;
    // What the character was standing on is what the ticks after it are derived
    // from, so its own machine resumes from the authority's answer.
    static constexpr bool Predicted = true;

    static auto Fields()
    {
        return std::tuple{
            // What locomotion and the jump gate actually read.
            MakeField("kind", &SupportState::Kind).OwnerOnly(),
            MakeField("surface_velocity", &SupportState::SurfaceVelocity).OwnerOnly(),
            // Where the contact was is re-derived by the first replayed sweep,
            // and the surface is an entity handle that means nothing on another
            // machine.
            MakeField("surface", &SupportState::Surface).LocalOnly(),
            MakeField("contact_point", &SupportState::ContactPoint).LocalOnly(),
            MakeField("normal", &SupportState::Normal).LocalOnly(),
        };
    }
};

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

// Owner-only, and the single most important thing a client gets back. A
// correction that carried position alone put the pawn in the right place still
// travelling at the wrong speed, and the next tick walked it straight back out
// again -- which is what a pawn wedged on a corner at one end and running down
// a hallway at the other looks like.
//
// Full width: how fast something is going is not bounded by anything the
// movement layer promises, and a range that clamped it would silently disagree
// with the machine that sent it.
template <>
struct TypeSchema<KinematicState>
{
    static constexpr std::string_view Name = "KinematicState";
    static constexpr bool Replicated = true;
    // Resuming from position alone leaves the pawn in the right place still
    // travelling at the wrong speed, so the owner resumes from this too.
    static constexpr bool Predicted = true;

    static auto Fields()
    {
        return std::tuple{
            MakeField("velocity", &KinematicState::Velocity).OwnerOnly(),
        };
    }
};

// Registration-order id from the LocomotionModeRegistry. Zero is the sentinel;
// these are runtime values and are never serialized into content.
struct LocomotionModeId
{
    uint32_t Value = 0;

    [[nodiscard]] bool IsValid() const { return Value != 0; }
    auto operator<=>(const LocomotionModeId&) const = default;
};

template <>
struct TypeSchema<LocomotionModeId>
{
    static constexpr std::string_view Name = "LocomotionModeId";

    static auto Fields()
    {
        return std::tuple{
            MakeField("value", &LocomotionModeId::Value),
        };
    }
};

// What this character is in movement terms: the authored profile it resolves
// tuning from, and the locomotion mode it is currently in.
struct CharacterMovement
{
    MovementProfileHandle Profile{};
    LocomotionModeId Mode{};
};
SENCHA_DECLARE_COMPONENT_TYPE(CharacterMovement, "sencha.character_movement");

// The mode travels to the owner so its own machine knows which rules it is
// resuming under, and refuses to replay under rules it does not implement
// rather than replaying under the wrong ones.
//
// This is a registration-order id on the wire, which content must never carry.
// A snapshot is not content: it is traffic between two processes the identity
// gate has already proved are the same build, so their registration orders are
// the same order -- the argument the action columns in a player command already
// run on. It is session-transient in both directions and is never written down.
template <>
struct TypeSchema<CharacterMovement>
{
    static constexpr std::string_view Name = "CharacterMovement";
    static constexpr bool Replicated = true;
    // Which rules the character moves under decides whether a replay is even
    // possible, so the owner resumes from the authority's mode.
    static constexpr bool Predicted = true;

    static auto Fields()
    {
        return std::tuple{
            MakeField("mode", &CharacterMovement::Mode).OwnerOnly(),
            // Which profile a character resolves tuning from is content both
            // machines already loaded.
            MakeField("profile", &CharacterMovement::Profile).LocalOnly(),
        };
    }
};

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
    // Seconds a jump locks out the next one. Part of movement feel, so it is
    // resolved with the rest of it rather than authored somewhere else.
    float JumpCooldownSeconds = 0.15f;
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
