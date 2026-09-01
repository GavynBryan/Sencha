#pragma once

#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <ecs/ComponentTypeId.h>

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
// declared with its schema (movement/MovementComponentSchemas.h).
//=============================================================================

// Registration-order id from the LocomotionModeRegistry. Zero is the sentinel;
// these are runtime values and are never serialized into content.
struct LocomotionModeId
{
    uint32_t Value = 0;

    [[nodiscard]] bool IsValid() const { return Value != 0; }
    auto operator<=>(const LocomotionModeId&) const = default;
};

// Beside its type rather than in the schema unit: this describes a value, not a
// component, and every schema with a mode field needs it in scope.
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

struct CharacterMovement
{
    LocomotionModeId Mode{};
};
SENCHA_DECLARE_COMPONENT_TYPE(CharacterMovement, "sencha.character_movement");
SENCHA_COMPONENT_DECLARES_SCHEMA(CharacterMovement);
SENCHA_COMPONENT_DECLARES_TRAITS(CharacterMovement);

// How hard a mode request pushes. Automatic loses to Explicit, which loses to
// Forced; a Forced request also bypasses the target mode's entry check.
enum class ModeRequestClass : uint8_t
{
    Automatic,
    Explicit,
    Forced,
};

struct ModeTransitionRequest
{
    LocomotionModeId Target;
    ModeRequestClass Class = ModeRequestClass::Automatic;
    bool Pending = false;
};
SENCHA_DECLARE_COMPONENT_TYPE(ModeTransitionRequest, "sencha.mode_transition_request");

static_assert(std::is_trivially_copyable_v<CharacterMovement>);
