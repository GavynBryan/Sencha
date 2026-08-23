#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

//=============================================================================
// EnumValue
//
// A single enumerator paired with its string representation. Used as elements
// of EnumSchema<E>::Values.
//=============================================================================

template <typename E>
struct EnumValue
{
    E Value;
    std::string_view Name;
    // Display metadata, editor-only. Display is what a selector shows in
    // place of the humanized Name; Tooltip explains the choice on hover.
    // Archives round-trip Name and never read either.
    std::string_view Display{};
    std::string_view Tooltip{};
};

//=============================================================================
// EnumSchema
//
// Specialize for an enum type E and provide a static constexpr array of
// EnumValue<E> named `Values` to enable string/integer round-tripping through
// archives.
//
// Usage:
//   template <>
//   struct EnumSchema<MyEnum>
//   {
//       static constexpr std::array Values = {
//           EnumValue{ MyEnum::Foo, "foo" },
//           EnumValue{ MyEnum::Bar, "bar" },
//       };
//   };
//=============================================================================

template <typename E>
struct EnumSchema;

// Satisfied when EnumSchema<E>::Values exists.
template <typename E>
concept HasEnumSchema = requires { EnumSchema<E>::Values; };

//=============================================================================
// EnumOption
//
// One enumerator as a type-erased {integer value, name} pair, usable without
// naming the enum type. This is EnumSchema's projection through type-erased
// seams (RuntimeField), so schema-driven tooling can present named choices
// for any EnumSchema'd member instead of raw integers.
//=============================================================================

struct EnumOption
{
    std::int64_t     Value = 0;
    std::string_view Name;
    std::string_view Display{};
    std::string_view Tooltip{};
};

namespace EnumSchemaDetail
{
    template <typename E>
    inline constexpr auto OptionsStorage = []
    {
        constexpr std::size_t count = EnumSchema<E>::Values.size();
        std::array<EnumOption, count> out{};
        for (std::size_t i = 0; i < count; ++i)
        {
            out[i].Value = static_cast<std::int64_t>(EnumSchema<E>::Values[i].Value);
            out[i].Name = EnumSchema<E>::Values[i].Name;
            out[i].Display = EnumSchema<E>::Values[i].Display;
            out[i].Tooltip = EnumSchema<E>::Values[i].Tooltip;
        }
        return out;
    }();
}

// The type-erased option table for E. Backed by constexpr storage, so the
// span (and the names it references) lives for the program's lifetime.
template <typename E>
    requires HasEnumSchema<E>
constexpr std::span<const EnumOption> EnumOptionsOf()
{
    return EnumSchemaDetail::OptionsStorage<E>;
}
