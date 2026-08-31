#pragma once

#include <assets/data/DataAssetCache.h>
#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <ecs/ComponentTraits.h>
#include <ecs/EntityId.h>
#include <ecs/World.h>
#include <math/MathSchemas.h>
#include <movement/JumpState.h>
#include <movement/MovementComponentAssets.h>
#include <movement/MovementIntent.h>
#include <movement/components/CharacterFacts.h>
#include <movement/components/CharacterMovement.h>
#include <movement/components/MotionChannels.h>
#include <movement/components/MovementTuning.h>
#include <world/transform/TransformHistory.h>

#include <string_view>
#include <tuple>

//=============================================================================
// What the movement components mean to the systems that carry them
//
// Authoring shape, replication policy, and lifecycle live here rather than
// beside the structs, so that a system reading a movement value does not
// acquire the World, the data-asset cache, and the transform history along
// with it. Registration and the serializers include this; the tick does not.
//=============================================================================

// Sent to the peer that owns this character and to nobody else. Everyone can
// see where another player is; only that player's own machine has to be able to
// resume simulating them from it, which is what reconciliation does after every
// snapshot.
template <>
struct TypeSchema<SupportState>
{
    static constexpr std::string_view Name = "SupportState";
    static constexpr bool Replicated = true;
    // What the character was standing on is what the ticks after it are derived
    // from, so its own machine resumes from the authority's answer.
    static constexpr bool Predicted = true;

    static auto Fields()
    {
        return std::tuple{
            // What locomotion and the jump gate actually read.
            MakeField("kind", &SupportState::Kind).OwnerOnly(),
            MakeField("surface_velocity", &SupportState::SurfaceVelocity).OwnerOnly(),
            // Where the contact was is re-derived by the first replayed sweep,
            // and the surface is an entity handle that means nothing on another
            // machine.
            MakeField("surface", &SupportState::Surface).LocalOnly(),
            MakeField("contact_point", &SupportState::ContactPoint).LocalOnly(),
            MakeField("normal", &SupportState::Normal).LocalOnly(),
        };
    }
};

// Owner-only, and the single most important thing a client gets back. A
// correction that carried position alone put the pawn in the right place still
// travelling at the wrong speed, and the next tick walked it straight back out
// again -- which is what a pawn wedged on a corner at one end and running down
// a hallway at the other looks like.
//
// Full width: how fast something is going is not bounded by anything the
// movement layer promises, and a range that clamped it would silently disagree
// with the machine that sent it.
template <>
struct TypeSchema<KinematicState>
{
    static constexpr std::string_view Name = "KinematicState";
    static constexpr bool Replicated = true;
    // Resuming from position alone leaves the pawn in the right place still
    // travelling at the wrong speed, so the owner resumes from this too.
    static constexpr bool Predicted = true;

    static auto Fields()
    {
        return std::tuple{
            MakeField("velocity", &KinematicState::Velocity).OwnerOnly(),
        };
    }
};

// The mode travels to the owner so its own machine knows which rules it is
// resuming under, and refuses to replay under rules it does not implement
// rather than replaying under the wrong ones.
//
// This is a registration-order id on the wire, which content must never carry.
// A snapshot is not content: it is traffic between two processes the identity
// gate has already proved are the same build, so their registration orders are
// the same order -- the argument the action columns in a player command already
// run on. It is session-transient in both directions and is never written down.
template <>
struct TypeSchema<CharacterMovement>
{
    static constexpr std::string_view Name = "CharacterMovement";
    static constexpr bool Replicated = true;
    // Which rules the character moves under decides whether a replay is even
    // possible, so the owner resumes from the authority's mode.
    static constexpr bool Predicted = true;

    static auto Fields()
    {
        return std::tuple{
            MakeField("mode", &CharacterMovement::Mode).OwnerOnly(),
        };
    }
};

// The scene form is the profile's path (see MovementTuningSourceSerializer);
// this describes the same member for an authoring surface, which resolves the
// handle through the asset system rather than reading its bytes as a number.
template <>
struct TypeSchema<MovementTuningSource>
{
    static constexpr std::string_view Name = "MovementTuning";

    static auto Fields()
    {
        return std::make_tuple(
            MakeField("profile", &MovementTuningSource::Profile)
                .AsDataAsset(kMovementProfileTypeName)
                .Label("Movement profile")
                .Tooltip("Authored acceleration, friction, and jump coefficients. "
                         "None leaves the character on engine defaults plus the "
                         "MoveSpeed attribute."));
    }
};

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
