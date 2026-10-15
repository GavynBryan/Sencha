#pragma once

#include <ecs/ComponentAnnotations.h>
#include <ecs/EntityId.h>
#include <ecs/LeaseOwner.h>
#include <math/Vec.h>
#include <movement/LocomotionModeId.h>

#include <cstddef>
#include <type_traits>

//=============================================================================
// Motion a holder keeps a character in while it holds it: a burst of velocity,
// a tether to an anchor, a locomotion mode. Every character carries the slots;
// a slot whose Owner is unset holds nothing. Holders acquire and release them
// through the movement leases, and a slot whose holder is gone ends when the
// character next steps (docs/gameplay/movement.md, "Held motion").
//=============================================================================

inline constexpr std::size_t kMotionBurstCapacity = 2;

// A velocity the character is driven at: a lunge, a dash, a launch.
struct MotionBurst
{
    LeaseOwner Owner;
    Vec3d Velocity = Vec3d::Zero();
    // Actor seconds left, when it Expires; otherwise held until let go.
    float RemainingSeconds = 0.0f;
    bool Expires = false;
    // The velocity's part along the up axis drives the up channel too.
    bool Rises = false;
};

struct SENCHA_COMPONENT("sencha.motion_bursts") MotionBursts
{
    MotionBurst Slots[kMotionBurstCapacity] = {};
};

// A rope from the character to an anchor: longer than Length, it pulls the
// character toward the anchor at Speed. An anchor entity is followed while it
// lives, and the tether ends when it does; with none, Point is the anchor.
struct SENCHA_COMPONENT("sencha.tether_session") TetherSession
{
    LeaseOwner Owner;
    EntityId Anchor;
    Vec3d Point = Vec3d::Zero();
    float Length = 0.0f;
    float Speed = 0.0f;
};

// The mode a holder put the character in, which letting go returns to free.
struct SENCHA_COMPONENT("sencha.locomotion_mode_lease") LocomotionModeLease
{
    LeaseOwner Owner;
    LocomotionModeId Mode;
};

static_assert(std::is_trivially_copyable_v<MotionBursts>);
static_assert(std::is_trivially_copyable_v<TetherSession>);
static_assert(std::is_trivially_copyable_v<LocomotionModeLease>);

#if !defined(SENCHA_CODEGEN)
#  include <movement/components/HeldMotion.sencha.h>
#endif
