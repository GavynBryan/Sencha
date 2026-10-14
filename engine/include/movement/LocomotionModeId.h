#pragma once

#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>

#include <compare>
#include <cstdint>
#include <string_view>
#include <tuple>

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
