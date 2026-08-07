#pragma once

#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <ecs/ComponentTypeId.h>
#include <input/InputAction.h>

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

// Runtime-only: never scene-serialized (no SceneChunkId, absent from the scene
// manifest). The schema is here because where something aims is one of the few
// facts other machines must see, and the schema is what states which of these
// bytes travel.
template <>
struct TypeSchema<LookOrientation>
{
    static constexpr std::string_view Name = "LookOrientation";

    static auto Fields()
    {
        const LookOrientation defaults;
        return std::tuple{
            // Yaw accumulates without bound -- it is a running total, not an
            // angle folded into a circle -- so a fixed quantization range would
            // clamp a player who kept turning one way. It ships at full width
            // until the codec can carry a wrapping angle.
            MakeField("yaw", &LookOrientation::Yaw),
            // Pitch is bounded by the limits below, which are stricter than
            // this range, so nothing here can clamp.
            MakeField("pitch", &LookOrientation::Pitch)
                .Quantize(-1.5707964f, 1.5707964f, 16),
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
