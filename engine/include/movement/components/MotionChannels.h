#pragma once

#include <ecs/ComponentAnnotations.h>
#include <math/Vec.h>

#include <type_traits>

//=============================================================================
// What the character wants to do this tick, in the order it is decided
//
// The active locomotion mode proposes, action producers write their channels
// over it, and one composed request comes out the end. Each stage is its own
// column so the stage that writes it is the only thing that writes it.
//=============================================================================

// What the active locomotion mode wants, before actions compose over it.
struct SENCHA_COMPONENT("sencha.locomotion_output") LocomotionOutput
{
    Vec3d Velocity = Vec3d::Zero();
    Vec3d UpAxis = Vec3d(0.0f, 1.0f, 0.0f);
    float GravityScale = 1.0f;
};

// Per-channel writes from action producers this tick, in the support-relative
// frame. Ordinary producers are first-write-wins; a forced write replaces one.
// Cleared every tick by the composition system.
struct SENCHA_COMPONENT("sencha.motion_axis_override") MotionAxisOverride
{
    Vec3d PlanarVelocity = Vec3d::Zero();
    float UpVelocity = 0.0f;
    bool HasPlanar = false;
    bool HasUp = false;
    bool ForcedPlanar = false;
    bool ForcedUp = false;
};

// Additive velocity from this tick's impulses (knockback, explosions). Applied
// after the override channels, then cleared.
struct SENCHA_COMPONENT("sencha.motion_impulse") MotionImpulse
{
    Vec3d DeltaVelocity = Vec3d::Zero();
};

// The single composed result the character motor consumes. Exactly one system
// writes it.
struct SENCHA_COMPONENT("sencha.motion_request") MotionRequest
{
    Vec3d Velocity = Vec3d::Zero();
    Vec3d UpAxis = Vec3d(0.0f, 1.0f, 0.0f);
    float GravityScale = 1.0f;
};

static_assert(std::is_trivially_copyable_v<MotionRequest>);

#if !defined(SENCHA_CODEGEN)
#  include <movement/components/MotionChannels.sencha.h>
#endif
