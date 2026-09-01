#pragma once

#include <ecs/ComponentTypeId.h>
#include <ecs/EntityId.h>
#include <math/Vec.h>

#include <cstdint>
#include <type_traits>

//=============================================================================
// What the world did to a character last step
//
// Facts, not modes. Locomotion reads them and nothing infers a mode from them
// implicitly, which is what keeps one physical fact stored once instead of
// mirrored across marker membership, an arbiter, and a tag set.
//=============================================================================

enum class SupportKind : uint8_t
{
    None,
    Stable, // standable: within the motor's slope limit
    Steep,  // touched, but too steep to stand on
};

// What the motor found under the character last step.
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
SENCHA_COMPONENT_DECLARES_SCHEMA(SupportState);

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
SENCHA_COMPONENT_DECLARES_SCHEMA(KinematicState);

static_assert(std::is_trivially_copyable_v<SupportState>);
