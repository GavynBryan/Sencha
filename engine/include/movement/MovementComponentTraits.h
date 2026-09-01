#pragma once

#include <assets/data/DataAssetCache.h>
#include <ecs/ComponentTraits.h>
#include <ecs/EntityId.h>
#include <ecs/World.h>
#include <movement/JumpState.h>
#include <movement/MovementComponentAssets.h>
#include <movement/MovementIntent.h>
#include <movement/components/CharacterFacts.h>
#include <movement/components/CharacterMovement.h>
#include <movement/components/MotionChannels.h>
#include <movement/components/MovementTuning.h>
#include <world/transform/TransformHistory.h>

#include <tuple>

//=============================================================================
// What carrying a movement component obliges the world to do
//
// Lifecycle lives here rather than beside the structs, so that a system
// reading a movement value does not acquire the World, the data-asset cache,
// and the transform history along with it. Registration includes this; the
// tick does not.
//=============================================================================

// The component owns one reference to its profile for as long as it carries
// it. Whoever produced the handle owns their own and lets it go; this is what
// keeps the profile resident afterwards, and what frees it when the last
// character naming it is destroyed.
template <>
struct ComponentTraits<MovementTuningSource>
{
    static void OnAdd(MovementTuningSource& component, World& world, EntityId)
    {
        auto* assets = world.TryGetResource<MovementComponentAssets>();
        if (assets != nullptr && assets->Profiles != nullptr)
            assets->Profiles->Retain(component.Profile.Value);
    }

    static void OnRemove(const MovementTuningSource& component, World& world, EntityId)
    {
        auto* assets = world.TryGetResource<MovementComponentAssets>();
        if (assets != nullptr && assets->Profiles != nullptr)
            assets->Profiles->Release(component.Profile.Value);
    }
};

//=============================================================================
// What a moving character owes
//
// These are the columns the movement tick reads and writes: last step's
// physical facts, this tick's request and resolved coefficients, the
// contribution channels, and the composed motor request. None of them is
// authored and none of them means anything on its own -- an entity with a
// CharacterMovement and no MotionRequest is not a character with a missing
// setting, it is a character that quietly stops matching the query that would
// have moved it.
//
// That failure has no error to report and no frame to happen on, which is why
// it is stated once here instead of ensured at every place a character is
// built. Every path that adds a CharacterMovement -- content, code, the editor
// adding it by identity, a command buffer flushing it after a query -- ends in
// one of the World's structural adds, and each of those applies the set.
//
// The transform history is here for the same reason in a different register: a
// body stepped at the tick rate and drawn at the frame rate needs the two poses
// to interpolate between, and having them is not optional for something that
// moves every tick.
template <>
struct ComponentTraits<CharacterMovement>
{
    using DerivedComponents = std::tuple<
        MovementIntent,
        JumpState,
        KinematicState,
        SupportState,
        ResolvedMovementTuning,
        LocomotionOutput,
        MotionAxisOverride,
        MotionImpulse,
        MotionRequest,
        ModeTransitionRequest,
        WorldTransformHistory>;
};
