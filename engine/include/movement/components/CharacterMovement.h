#pragma once

#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <ecs/ComponentAnnotations.h>
#include <ecs/ComponentTraits.h>
#include <ecs/ComponentTypeId.h>
#include <movement/JumpState.h>
#include <movement/MovementIntent.h>
#include <movement/components/CharacterFacts.h>
#include <movement/components/MotionChannels.h>
#include <movement/components/MovementTuning.h>
#include <world/transform/TransformHistory.h>

#include <compare>
#include <cstdint>
#include <string_view>
#include <tuple>
#include <type_traits>

//=============================================================================
// Which rules a character moves under
//
// Carrying CharacterMovement is what makes an entity something the movement
// systems step. The columns that tick reads and writes are the set it owes,
// declared below.
//=============================================================================

// Registration-order id from the LocomotionModeRegistry. Zero is the sentinel;
// these are runtime values and are never serialized into content.
struct LocomotionModeId
{
    uint32_t Value = 0;

    [[nodiscard]] bool IsValid() const { return Value != 0; }
    auto operator<=>(const LocomotionModeId&) const = default;
};

// Beside its type: this describes a value, not a component, and every schema
// with a mode field needs it in scope.
template <>
struct TypeSchema<LocomotionModeId>
{
    static constexpr std::string_view Name = "LocomotionModeId";

    static auto Fields()
    {
        return std::tuple{
            MakeField("value", &LocomotionModeId::Value),
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
struct SENCHA_COMPONENT("sencha.character_movement")
       SENCHA_SCHEMA("CharacterMovement")
       SENCHA_REPLICATED
       SENCHA_PREDICTED
CharacterMovement
{
    SENCHA_FIELD("mode")
    SENCHA_OWNER_ONLY
    LocomotionModeId Mode{};
};

// How hard a mode request pushes. Automatic loses to Explicit, which loses to
// Forced; a Forced request also bypasses the target mode's entry check.
enum class ModeRequestClass : uint8_t
{
    Automatic,
    Explicit,
    Forced,
};

struct SENCHA_COMPONENT("sencha.mode_transition_request") ModeTransitionRequest
{
    LocomotionModeId Target;
    ModeRequestClass Class = ModeRequestClass::Automatic;
    bool Pending = false;
};

static_assert(std::is_trivially_copyable_v<CharacterMovement>);

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
//=============================================================================
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

#if !defined(SENCHA_CODEGEN)
#  include <movement/components/CharacterMovement.sencha.h>
#endif
