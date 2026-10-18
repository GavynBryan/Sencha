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
    // About the up axis, this tick.
    float TurnRadians = 0.0f;
    bool HasTurn = false;
};

// World-space velocity changes, applied after the relative channels and cleared.
struct SENCHA_COMPONENT("sencha.motion_impulse") MotionImpulse
{
    Vec3d DeltaVelocity = Vec3d::Zero();
    Vec3d ReplacementVelocity = Vec3d::Zero();
    bool HasReplacement = false;
};

// The single composed result the character motor consumes. Exactly one system
// writes it.
struct SENCHA_COMPONENT("sencha.motion_request") MotionRequest
{
    Vec3d Velocity = Vec3d::Zero();
    Vec3d UpAxis = Vec3d(0.0f, 1.0f, 0.0f);
    float GravityScale = 1.0f;
    // Turned about UpAxis as the character moves.
    float TurnRadians = 0.0f;
    // An action, root motion or held motion replaced the locomotion mode's
    // planar velocity, so the character went where it was carried rather than
    // where its intent asked.
    bool PlanarOverridden = false;
};

static_assert(std::is_trivially_copyable_v<MotionRequest>);

#if !defined(SENCHA_CODEGEN)
#  include <movement/components/MotionChannels.sencha.h>
#endif
