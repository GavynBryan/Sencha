#pragma once

#include <ecs/ComponentTypeId.h>
#include <input/InputAction.h>

#include <algorithm>
#include <cstdint>
#include <string_view>
#include <tuple>
#include <type_traits>

//=============================================================================
// LookOrientation
//
// Where an entity is aiming, as accumulated yaw and pitch. This is simulation
// state owned by the thing doing the aiming, not by whatever happens to be
// watching it: a camera reads it to place itself, a character reads it to steer
// along it, and a turret has one with no camera anywhere near.
//
// The pitch limits live here for the same reason. How far something can look up
// is a property of the thing aiming -- a human neck and a tank turret differ --
// not of the lens pointed at it.
//
// Input contributes displacement; this holds the running total. Keeping the two
// apart is what lets the same orientation be driven by a player's look action,
// by an AI turning toward a target, or by a replayed command.
//=============================================================================
struct LookOrientation
{
    float Yaw = 0.0f;
    float Pitch = 0.0f;
    float MinPitch = -1.4f;
    float MaxPitch = 1.4f;
};

static_assert(std::is_trivially_copyable_v<LookOrientation>,
              "LookOrientation must be trivially copyable to live in ECS chunks");

// The one rule for writing an aim: pitch never leaves the limits of the thing
// doing the aiming. Every writer goes through here -- the local look pass, an
// authority applying the aim a peer's command was framed with -- because a
// second copy of the rule is a limit that can differ between the machine that
// enforced it and the machine that did not.
inline void ApplyLook(LookOrientation& look, float yaw, float pitch)
{
    look.Yaw = yaw;
    look.Pitch = std::clamp(pitch, look.MinPitch, look.MaxPitch);
}

SENCHA_DECLARE_COMPONENT_TYPE(LookOrientation, "sencha.look_orientation");
SENCHA_COMPONENT_DECLARES_SCHEMA(LookOrientation);

//=============================================================================
// LocalLookControl
//
// Marks the entity whose LookOrientation the local player's look action drives.
// An AI-aimed entity carries a LookOrientation without this, and its own
// controller writes it instead.
//=============================================================================
struct LocalLookControl
{
};

static_assert(std::is_empty_v<LocalLookControl>,
              "LocalLookControl is a tag: it carries no data");

SENCHA_DECLARE_COMPONENT_TYPE(LocalLookControl, "sencha.local_look_control");

//=============================================================================
// AimFacing
//
// Marks an entity whose transform rotation is its aim: the body turns to face
// where its LookOrientation points, yaw only.
//
// Opt-in per entity, because facing is not always aim. A first-person character
// and a fixed gun turn with the look; a third-person character usually faces
// the way it is moving while the camera orbits somewhere else entirely. The
// camera does not decide this and is not consulted -- it reads the same
// orientation to place itself, and neither reader knows the other exists.
//
// The tag claims the entity's rotation. Nothing else may write the same
// transform's rotation while it is present: whichever ran last would win, and
// which that is would depend on schedule order. An authored rotation is
// overwritten from the first tick, so an entity that must begin facing a
// particular way is placed with the matching LookOrientation.Yaw rather than
// with a rotation.
//=============================================================================
struct AimFacing
{
};

static_assert(std::is_empty_v<AimFacing>,
              "AimFacing is a tag: it carries no data");

SENCHA_DECLARE_COMPONENT_TYPE(AimFacing, "sencha.aim_facing");
SENCHA_COMPONENT_DECLARES_SCHEMA(AimFacing);

//=============================================================================
// LookInputBinding
//
// Which action turns a locally controlled entity. The game names the action, so
// no engine type carries a game's input vocabulary; the action's value is
// angular displacement for the pass, already conditioned by the binding's scale
// and inversion.
//=============================================================================
struct LookInputBinding
{
    InputActionId Look;
};

//=============================================================================
// PendingLookInput
//
// The presentation clock's view of look input the simulation has not caught up
// to, in the two mechanical kinds look input comes in.
//
// Yaw/Pitch is accumulated displacement from latched controls (pointer,
// wheel): amounts that arrived on frames no tick has consumed yet. Cleared by
// every tick that runs, so it spans at most the gap between two ticks --
// never a running total.
//
// RateYaw/RatePitch is the current turn rate from sampled controls (sticks,
// held keys), in radians per second. It is not accumulated at all: a sample
// covers time, so the presentation lead it owes is rate times the wall time
// the next tick has yet to absorb -- which is alpha, and is derived where the
// view is placed. Accumulating it per frame while ticks consumed it per tick
// is a mismatch that stepped the view backward at tick rate.
//
// The aim a tick simulates under advances only when simulated time advances,
// or a frame that ran no tick would turn the heading for free and a frame that
// ran two would turn it once for both. A view that only moved on ticks would
// visibly step at any refresh rate above the tick rate, so presentation adds
// this on top of the simulation aim and tracks the player's input at frame
// rate.
//=============================================================================
struct PendingLookInput
{
    float Yaw = 0.0f;
    float Pitch = 0.0f;
    float RateYaw = 0.0f;
    float RatePitch = 0.0f;
};
