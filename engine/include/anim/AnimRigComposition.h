#pragma once

#include <ecs/ComponentAnnotations.h>

#include <cstdint>

// The parts of an entity's animation state a rig may or may not need. Every rig
// has a request set and content state; these are the rest.
enum class AnimRigPart : std::uint8_t
{
    Facts = 1u << 0,
    FactsLarge = 1u << 1,
    History = 1u << 2,
    Selection = 1u << 3,
    Flows = 1u << 4,
    Pose = 1u << 5,
};
using AnimRigParts = std::uint8_t;

[[nodiscard]] constexpr bool HasAnimRigPart(AnimRigParts parts, AnimRigPart part)
{
    return (parts & static_cast<AnimRigParts>(part)) != 0;
}

// What an entity's parts were last composed to. Brought by AnimRig; nothing but
// AnimRigCompositionSystem writes it.
struct SENCHA_COMPONENT("sencha.anim_rig_composition") AnimRigComposition
{
    AnimRigParts Parts = 0;
    bool Composed = false;
};

// Carried by whatever consumes an entity's pose, such as a skinned mesh, so posing
// happens only where something draws the result.
struct SENCHA_COMPONENT("sencha.anim_pose_consumer") AnimPoseConsumer
{
};

#if !defined(SENCHA_CODEGEN)
#  include <anim/AnimRigComposition.sencha.h>
#endif
