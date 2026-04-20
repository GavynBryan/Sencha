#pragma once

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
