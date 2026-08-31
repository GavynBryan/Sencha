#pragma once

#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <core/serialization/FourCC.h>
#include <ecs/ComponentTypeId.h>

#include <cstdint>
#include <string_view>
#include <tuple>

//=============================================================================
// CharacterController
//
// Authored physical shape of a kinematic capsule character. Configuration
// only: the per-tick motion comes from MotionRequest and the achieved facts go
// back out as SupportState and KinematicState, so this component is read by
// the mover pool and never used as an in/out mailbox.
//=============================================================================
struct CharacterController
{
    float Radius = 0.3f;
    float Height = 1.8f;
    float SlopeLimitDegrees = 50.0f;
    float StepHeight = 0.35f;
    float GroundSnapDistance = 0.25f;
    float SkinWidth = 0.02f;
};

// Authored shape, so it saves. Every field defaults to its member initializer,
// which is what lets content state only the dimensions it cares about -- a
// prefab that says nothing but "radius" still gets a whole capsule.
template <>
struct TypeSchema<CharacterController>
{
    static constexpr std::string_view Name = "CharacterController";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('C', 'H', 'C', 'T');

    static auto Fields()
    {
        const CharacterController defaults;
        return std::tuple{
            MakeField("radius", &CharacterController::Radius)
                .Default(defaults.Radius),
            MakeField("height", &CharacterController::Height)
                .Default(defaults.Height),
            MakeField("slope_limit_degrees", &CharacterController::SlopeLimitDegrees)
                .Default(defaults.SlopeLimitDegrees)
                .Label("Slope limit")
                .Tooltip("Steepest ground the body will walk up, in degrees."),
            MakeField("step_height", &CharacterController::StepHeight)
                .Default(defaults.StepHeight)
                .Tooltip("Tallest ledge the body steps over instead of stopping at."),
            MakeField("ground_snap_distance", &CharacterController::GroundSnapDistance)
                .Default(defaults.GroundSnapDistance)
                .Label("Ground snap")
                .Tooltip("How far below the feet ground still counts as ground, "
                         "which is what keeps a body on a downward slope instead "
                         "of launching off it."),
            MakeField("skin_width", &CharacterController::SkinWidth)
                .Default(defaults.SkinWidth)
                .Tooltip("Gap kept between the capsule and what it touches."),
        };
    }
};

SENCHA_DECLARE_COMPONENT_TYPE(CharacterController, "sencha.physics.character_controller");
