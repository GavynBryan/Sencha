#pragma once

#include <ecs/ComponentAnnotations.h>
#include <ecs/ComponentTraits.h>
#include <ecs/ComponentTypeId.h>
#include <ecs/EntityId.h>
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

// The scene form is the profile's path (see MovementTuningSourceSerializer);
// the schema describes the same member for an authoring surface, which resolves
// the handle through the asset system rather than reading its bytes as a number.
struct SENCHA_COMPONENT("sencha.movement_tuning_source")
       SENCHA_SCHEMA("MovementTuning")
MovementTuningSource
{
    SENCHA_FIELD("profile")
    SENCHA_DATA_ASSET(kMovementProfileTypeName)
    SENCHA_LABEL("Movement profile")
    SENCHA_TOOLTIP("Authored acceleration, friction, and jump coefficients. "
                   "None leaves the character on engine defaults plus the "
                   "MoveSpeed attribute.")
    MovementProfileHandle Profile{};
};

// The component owns one reference to its profile for as long as it carries
// it. Whoever produced the handle owns their own and lets it go; this is what
// keeps the profile resident afterwards, and what frees it when the last
// character naming it is destroyed.
template <>
struct ComponentTraits<MovementTuningSource>
{
    static void OnAdd(MovementTuningSource& component, World& world, EntityId);
    static void OnRemove(const MovementTuningSource& component, World& world, EntityId);
};

// The handle wrapper is addressed by the asset-field editors at the member's
// own offset, the way a mesh handle is: they copy handle bytes without naming
// the wrapper. That only works while the wrapper is exactly its handle.
static_assert(sizeof(MovementProfileHandle) == sizeof(DataAssetHandle));
static_assert(offsetof(MovementProfileHandle, Value) == 0);

// This tick's coefficients, resolved from the profile's layers against the
// current facts. Written by the tuning resolution system and read by
// locomotion; nothing else writes it.
struct SENCHA_COMPONENT("sencha.resolved_movement_tuning") ResolvedMovementTuning
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

#if !defined(SENCHA_CODEGEN)
#  include <movement/components/MovementTuning.sencha.h>
#endif
