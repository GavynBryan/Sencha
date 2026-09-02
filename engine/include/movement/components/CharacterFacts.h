#pragma once

#include <ecs/ComponentAnnotations.h>
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
//
// Sent to the peer that owns this character and to nobody else. Everyone can
// see where another player is; only that player's own machine has to be able to
// resume simulating them from it, which is what reconciliation does after every
// snapshot. What the character was standing on is what the ticks after it are
// derived from, so its own machine resumes from the authority's answer.
struct SENCHA_COMPONENT("sencha.support_state")
       SENCHA_SCHEMA("SupportState")
       SENCHA_REPLICATED
       SENCHA_PREDICTED
SupportState
{
    // Where the contact was is re-derived by the first replayed sweep, and the
    // surface is an entity handle that means nothing on another machine.
    SENCHA_FIELD("surface")
    SENCHA_LOCAL_ONLY
    EntityId Surface;

    SENCHA_FIELD("contact_point")
    SENCHA_LOCAL_ONLY
    Vec3d ContactPoint = Vec3d::Zero();

    SENCHA_FIELD("normal")
    SENCHA_LOCAL_ONLY
    Vec3d Normal = Vec3d(0.0f, 1.0f, 0.0f);

    // World velocity of the surface at the contact point. Locomotion works in
    // velocity relative to this, so a character standing on a moving platform
    // is at rest in its own frame.
    SENCHA_FIELD("surface_velocity")
    SENCHA_OWNER_ONLY
    Vec3d SurfaceVelocity = Vec3d::Zero();

    // What locomotion and the jump gate actually read.
    SENCHA_FIELD("kind")
    SENCHA_OWNER_ONLY
    SupportKind Kind = SupportKind::None;
};

// How deeply the character is inside a volume, in [0, 1]. A fact like
// SupportState: profiles condition on it rather than a "swimming" mode being
// switched on somewhere.
struct SENCHA_COMPONENT("sencha.immersion") Immersion
{
    EntityId Volume;
    float Fraction = 0.0f;
};

// Full world velocity carried between ticks. Not planar: the vertical channel
// belongs to locomotion too, so gravity and jumps compose with everything else
// instead of being owned privately by the motor.
//
// Owner-only, and the single most important thing a client gets back. A
// correction that carried position alone put the pawn in the right place still
// travelling at the wrong speed, and the next tick walked it straight back out
// again -- which is what a pawn wedged on a corner at one end and running down
// a hallway at the other looks like.
//
// Full width: how fast something is going is not bounded by anything the
// movement layer promises, and a range that clamped it would silently disagree
// with the machine that sent it.
struct SENCHA_COMPONENT("sencha.kinematic_state")
       SENCHA_SCHEMA("KinematicState")
       SENCHA_REPLICATED
       SENCHA_PREDICTED
KinematicState
{
    SENCHA_FIELD("velocity")
    SENCHA_OWNER_ONLY
    Vec3d Velocity = Vec3d::Zero();
};

static_assert(std::is_trivially_copyable_v<SupportState>);

#if !defined(SENCHA_CODEGEN)
#  include <movement/components/CharacterFacts.sencha.h>
#endif
