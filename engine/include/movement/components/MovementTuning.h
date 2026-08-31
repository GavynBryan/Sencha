#pragma once

#include <ecs/ComponentTypeId.h>
#include <movement/MovementProfileData.h>

#include <cstddef>

//=============================================================================
// Where a character's coefficients come from, and what they resolved to
//
// The source is authored and never travels: which profile a character uses is
// content both machines already loaded, and a component carrying an asset
// handle cannot be replicated at all, because a snapshot overwrites bytes in
// place and would leave the handle unowned.
//
// An invalid handle is a working character on default coefficients plus the
// speed attribute, so a missing profile degrades rather than breaking.
//=============================================================================

struct MovementTuningSource
{
    MovementProfileHandle Profile{};
};
SENCHA_DECLARE_COMPONENT_TYPE(MovementTuningSource, "sencha.movement_tuning_source");

// The handle wrapper is addressed by the asset-field editors at the member's
// own offset, the way a mesh handle is: they copy handle bytes without naming
// the wrapper. That only works while the wrapper is exactly its handle.
static_assert(sizeof(MovementProfileHandle) == sizeof(DataAssetHandle));
static_assert(offsetof(MovementProfileHandle, Value) == 0);

// This tick's coefficients, resolved from the profile's layers against the
// current facts. Written by the tuning resolution system and read by
// locomotion; nothing else writes it.
struct ResolvedMovementTuning
{
    float MaxSpeed = 7.0f;
    float Acceleration = 24.0f;
    float Friction = 8.0f;
    float StopSpeed = 1.0f;

    // Caps the speed the acceleration term may reach along the wish direction,
    // independent of MaxSpeed. A low value leaves existing momentum intact
    // while still allowing the character to steer.
    float WishSpeedCap = 0.0f;

    float Drag = 0.0f;
    float GravityScale = 1.0f;
    float JumpSpeed = 5.5f;
    // Seconds a jump locks out the next one. Part of movement feel, so it is
    // resolved with the rest of it rather than authored somewhere else.
    float JumpCooldownSeconds = 0.15f;
};
SENCHA_DECLARE_COMPONENT_TYPE(ResolvedMovementTuning, "sencha.resolved_movement_tuning");
