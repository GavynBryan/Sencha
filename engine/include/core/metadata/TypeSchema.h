#pragma once

#include <core/metadata/ComponentDefinition.h>

#include <cstdint>
#include <string_view>

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

//=============================================================================
// Projection from a generated ComponentDefinition
//
// A component whose facts are generated gets its schema from them rather than
// by hand. The optional members really have to be optional: whether a component
// is scene-serialized is decided by asking whether SceneChunkId exists, so a
// component that declares no chunk must not have the member at all. Each such
// fact therefore arrives through a base that is empty when the fact is absent.
//
// This is a partial specialization, so a handwritten TypeSchema<T> still wins.
// Migrating a component is exactly the act of deleting its handwritten one.
//=============================================================================
namespace SchemaProjection
{
    template <typename T> struct SceneChunk {};
    template <typename T> requires DefinitionHasSceneChunk<T>
    struct SceneChunk<T>
    {
        static constexpr std::uint32_t SceneChunkId = ComponentDefinition<T>::SceneChunk;
    };

    template <typename T> struct Replication {};
    template <typename T> requires DefinitionIsReplicated<T>
    struct Replication<T>
    {
        static constexpr bool Replicated = ComponentDefinition<T>::Replicated;
    };

    template <typename T> struct Prediction {};
    template <typename T> requires DefinitionIsPredicted<T>
    struct Prediction<T>
    {
        static constexpr bool Predicted = ComponentDefinition<T>::Predicted;
    };
}

template <typename T>
    requires DefinitionHasSchema<T>
struct TypeSchema<T, void>
    : SchemaProjection::SceneChunk<T>
    , SchemaProjection::Replication<T>
    , SchemaProjection::Prediction<T>
{
    static constexpr std::string_view Name = ComponentDefinition<T>::SchemaName;

    static auto Fields() { return ComponentDefinition<T>::Fields(); }
};
