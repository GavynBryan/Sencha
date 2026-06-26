#pragma once

#include <core/assets/AssetRef.h>
#include <core/metadata/SchemaVisit.h>
#include <core/metadata/TypeSchema.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

//=============================================================================
// RuntimeSchema
//
// A type-erased, flattened view of a component's schema: the leaf scalar fields,
// each as a {dotted name, byte offset, scalar kind}. Built once from the
// compile-time TypeSchema<T> (recursing through nested schemas like Vec/Quat/
// Transform down to scalars), then consumed without naming T.
//
// This is the seam that lets the editor's inspector draw and edit ANY component
// — engine or game-module — by reading/writing scalars at offsets in the
// component's raw bytes (World::GetComponentRaw), with NO ImGui dependency in
// the engine or in game modules. The editor owns all widget drawing; the engine
// only describes the data. (docs/plans/sencha-level-editor/02-...md §5.3.)
//=============================================================================

enum class FieldScalar : std::uint8_t
{
    Bool,
    Int32,
    UInt32,
    Float,
    Double,
    Unsupported, // a leaf the descriptor cannot safely express (handle, string, …)
};

struct RuntimeField
{
    std::string  Name;   // dotted path, e.g. "local.position.x" or "length"
    std::size_t  Offset; // bytes from the component's start
    std::size_t  Size;   // bytes of the scalar (so editors never over/under-write)
    FieldScalar  Scalar;
    // Non-Unknown for a member tagged AsAsset: the leaf is an asset handle of
    // this type, so an editor resolves it to/from an asset:// path instead of
    // treating it as an opaque scalar. (Scalar stays Unsupported for these.)
    // Arity says whether it is one handle or an ordered list (per-slot materials).
    AssetType    Asset = AssetType::Unknown;
    AssetArity   Arity = AssetArity::Single;
};

namespace RuntimeSchemaDetail
{
    template <typename T>
    constexpr FieldScalar ScalarKindOf()
    {
        if constexpr (std::is_enum_v<T>)
            return ScalarKindOf<std::underlying_type_t<T>>();
        else if constexpr (std::is_same_v<T, bool>)
            return FieldScalar::Bool;
        else if constexpr (std::is_same_v<T, float>)
            return FieldScalar::Float;
        else if constexpr (std::is_same_v<T, double>)
            return FieldScalar::Double;
        else if constexpr (std::is_integral_v<T> && std::is_signed_v<T> && sizeof(T) <= 4)
            return FieldScalar::Int32;
        else if constexpr (std::is_integral_v<T> && std::is_unsigned_v<T> && sizeof(T) <= 4)
            return FieldScalar::UInt32;
        else
            return FieldScalar::Unsupported;
    }

    // A VisitSchema visitor that flattens fields against a fixed root object:
    // each leaf's offset is its member address minus the root's, so offsets stay
    // absolute through any nesting depth. Schema'd members (Vec/Quat/Transform/…)
    // recurse; scalars are recorded at the leaves.
    template <typename Root>
    struct Collector
    {
        std::vector<RuntimeField>& Out;
        const Root&                Base;
        std::string                Prefix;

        template <typename FieldT, typename M>
        void Field(const FieldT& field, const M& member)
        {
            const std::size_t offset = static_cast<std::size_t>(
                reinterpret_cast<const std::byte*>(&member) -
                reinterpret_cast<const std::byte*>(&Base));
            std::string name =
                Prefix.empty() ? std::string(field.Name)
                               : Prefix + "." + std::string(field.Name);

            using MemberType = std::remove_cvref_t<M>;
            if constexpr (HasTypeSchema<MemberType>)
            {
                Collector<Root> sub{ Out, Base, name };
                VisitSchema<TypeSchema<MemberType>>(member, sub);
            }
            else
            {
                Out.push_back(RuntimeField{
                    std::move(name), offset, sizeof(MemberType),
                    ScalarKindOf<MemberType>(), field.Asset, field.Arity });
            }
        }
    };
}

// The flattened leaf-scalar fields of component type T. Built once per T.
template <typename T>
    requires HasTypeSchema<T>
const std::vector<RuntimeField>& RuntimeFieldsOf()
{
    static const std::vector<RuntimeField> fields = []
    {
        std::vector<RuntimeField> out;
        T root{};
        RuntimeSchemaDetail::Collector<T> collector{ out, root, std::string{} };
        VisitSchema<TypeSchema<T>>(root, collector);
        return out;
    }();
    return fields;
}
