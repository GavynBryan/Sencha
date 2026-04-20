#pragma once

//=============================================================================
// TypeSchema
//
// Specialize for a type T to make it schema-driven. The specialization must
// provide:
//   static constexpr std::string_view Name  — JSON key / human label
//   static auto Fields()                    — std::tuple of Field descriptors
//
// Usage:
//   template <>
//   struct TypeSchema<MyStruct>
//   {
//       static constexpr std::string_view Name = "MyStruct";
//       static auto Fields()
//       {
//           return std::tuple{
//               MakeField("x", &MyStruct::X),
//               MakeField("y", &MyStruct::Y),
//           };
//       }
//   };
//=============================================================================

template <typename T, typename = void>
struct TypeSchema;

// Satisfied when TypeSchema<T>::Fields() is well-formed.
template <typename T>
concept HasTypeSchema = requires { TypeSchema<T>::Fields(); };
