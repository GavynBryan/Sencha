#pragma once

#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <core/serialization/FourCC.h>
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

// Saved and sent both. It is sent because where something aims is one of the
// few facts other machines must see; it is saved because the pitch limits are
// a property of the thing and because an authored yaw is how a placed body
// states which way it begins facing -- the only way to state it for a body
// carrying AimFacing, whose rotation is overwritten from the first tick.
template <>
struct TypeSchema<LookOrientation>
{
    static constexpr std::string_view Name = "LookOrientation";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('L', 'O', 'O', 'K');
    static constexpr bool Replicated = true;

    static auto Fields()
    {
        const LookOrientation defaults;
        return std::tuple{
            // Yaw accumulates without bound -- it is a running total, not an
            // angle folded into a circle -- so a fixed quantization range would
            // clamp a player who kept turning one way. It ships at full width
            // until the codec can carry a wrapping angle.
            MakeField("yaw", &LookOrientation::Yaw)
                .Default(defaults.Yaw)
                .OwnerLocal()
                .Tooltip("Which way this body starts out aiming, in radians."),
            // Pitch is bounded by the limits below, which are stricter than
            // this range, so nothing here can clamp.
            MakeField("pitch", &LookOrientation::Pitch)
                .Default(defaults.Pitch)
                .Quantize(-1.5707964f, 1.5707964f, 16)
                .OwnerLocal(),
            // How far this thing can look is a property of the thing, identical
            // on every machine that loaded it. Sending it every tick would be
            // sending a constant.
            MakeField("min_pitch", &LookOrientation::MinPitch)
                .Default(defaults.MinPitch).LocalOnly(),
            MakeField("max_pitch", &LookOrientation::MaxPitch)
                .Default(defaults.MaxPitch).LocalOnly(),
        };
    }
};

SENCHA_DECLARE_COMPONENT_TYPE(LookOrientation, "sencha.look_orientation");

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

// The tag's presence is its whole value, so the schema has no fields; it is
// here so content can opt a placed body in without any code naming it.
template <>
struct TypeSchema<AimFacing>
{
    static constexpr std::string_view Name = "AimFacing";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('A', 'I', 'M', 'F');

    static auto Fields() { return std::tuple{}; }
};

SENCHA_DECLARE_COMPONENT_TYPE(AimFacing, "sencha.aim_facing");

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
