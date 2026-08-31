#pragma once

#include <camera/CameraRig.h>
#include <core/metadata/EnumSchema.h>
#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <core/serialization/FourCC.h>
#include <ecs/ComponentTypeId.h>

#include <array>
#include <cstdint>
#include <string_view>
#include <tuple>
#include <type_traits>

//=============================================================================
// CameraSeat
//
// Where a body is watched from, authored on the camera itself.
//
// A pawn prefab places its own camera, and possession then has to find it. What
// it must not do is guess: "the first child carrying a camera" is an answer
// that changes when someone adds a second camera for a scope, a mirror, or a
// cutscene, and it changes silently -- the player ends up looking through the
// wrong one with nothing to indicate why. So the seat says which it is, and the
// prefab is where that gets decided.
//
// It also says how that seat watches, which is the difference between the games
// built on one pawn: first person for a shooter, third for a platformer. That
// is a property of the seat rather than of possession, so the same pawn prefab
// with a different camera child is a different game's player.
//=============================================================================

enum class CameraSeatRole : std::uint8_t
{
    // The one possession takes. Exactly one per pawn; a second is a content
    // error the possession path reports rather than picks between.
    Primary,
    // Present, placed, and not what a player looks through until something
    // chooses it -- a scope, a security monitor, a cutscene angle.
    Secondary,
};

template <>
struct EnumSchema<CameraSeatRole>
{
    static constexpr std::array Values = {
        EnumValue{ CameraSeatRole::Primary, "primary", "Primary",
                   "The seat a player looks through when they possess this "
                   "body." },
        EnumValue{ CameraSeatRole::Secondary, "secondary", "Secondary",
                   "Placed but not taken: a view something else has to choose "
                   "before anyone looks through it." },
    };
};

struct CameraSeat
{
    CameraSeatRole Role = CameraSeatRole::Primary;
    // How this seat watches what it is attached to. Possession copies it into
    // the rig it provisions, so the authored answer is the one that runs.
    CameraRigMode Mode = CameraRigMode::FirstPerson;
    // How far back a third-person seat sits. Ignored in first person, where the
    // seat is the eyes.
    float Distance = 4.0f;
};

static_assert(std::is_trivially_copyable_v<CameraSeat>,
              "CameraSeat must be trivially copyable to live in ECS chunks");

template <>
struct TypeSchema<CameraSeat>
{
    static constexpr std::string_view Name = "CameraSeat";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('C', 'S', 'E', 'T');

    static auto Fields()
    {
        const CameraSeat defaults;
        return std::tuple{
            MakeField("role", &CameraSeat::Role).Default(defaults.Role),
            MakeField("mode", &CameraSeat::Mode)
                .Default(defaults.Mode)
                .Label("Watches from")
                .Tooltip("First person puts the view at the seat; third person "
                         "orbits the body at the distance below."),
            MakeField("distance", &CameraSeat::Distance)
                .Default(defaults.Distance)
                .Tooltip("Third person only: how far back the seat sits."),
        };
    }
};

SENCHA_DECLARE_COMPONENT_TYPE(CameraSeat, "sencha.camera_seat");
