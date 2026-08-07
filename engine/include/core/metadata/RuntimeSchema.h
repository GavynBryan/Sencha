#pragma once

#include <core/assets/AssetRef.h>
#include <core/identity/StrongId.h>
#include <core/metadata/EnumSchema.h>
#include <core/metadata/Field.h>
#include <core/metadata/ScalarGroup.h>
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
    Color3,      // three contiguous floats edited as one RGB swatch (field tagged AsColor)
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
    // Contiguous same-typed scalars edited as one N-wide row (Vec/Quat); 1 is a
    // plain scalar. The leaf spans [Offset, Offset + Count*Size).
    std::uint8_t Count = 1;
    // A leaf the editor shows but does not let the user edit (an identity id).
    bool         ReadOnly = false;
    // Non-empty for an enum member with an EnumSchema: the named choices for
    // this leaf, so an editor draws a selector instead of a number field.
    // Scalar stays the underlying integer kind and the bytes at Offset are
    // unchanged, so a consumer without enum awareness still works.
    std::span<const EnumOption> Enum{};
    // Replication metadata, inherited from the nearest annotated ancestor: a
    // range tagged on a Vec3 member applies to each of its floats, and a
    // subtree excluded from the wire excludes every leaf under it. A consumer
    // that does not replicate ignores all three.
    FieldQuantization Quantization{};
    bool OwnerOnly = false;
    bool LocalOnly = false;
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

    // Detects StrongId<Tag, U> members so a flattened leaf can address the
    // underlying integer (StrongId stores a single U at offset 0).
    template <typename T>
    struct IsStrongId : std::false_type
    {
    };

    template <typename Tag, typename U>
    struct IsStrongId<StrongId<Tag, U>> : std::true_type
    {
        using Underlying = U;
    };

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
        // Carried down from the enclosing member so an annotation on a
        // composite (a Vec3 position, a whole Transform) reaches the scalars it
        // was meant to describe. A member's own annotation wins over what it
        // inherits; otherwise the ancestor's stands.
        FieldQuantization          Quantization{};
        bool                       OwnerOnly = false;
        bool                       LocalOnly = false;

        template <typename FieldT, typename M>
        void Field(const FieldT& field, const M& member)
        {
            const std::size_t offset = static_cast<std::size_t>(
                reinterpret_cast<const std::byte*>(&member) -
                reinterpret_cast<const std::byte*>(&Base));
            std::string name =
                Prefix.empty() ? std::string(field.Name)
                               : Prefix + "." + std::string(field.Name);

            const FieldQuantization quantization =
                field.Quantization.IsQuantized() ? field.Quantization : Quantization;
            const bool ownerOnly = OwnerOnly || field.IsOwnerOnly;
            const bool localOnly = LocalOnly || field.IsLocalOnly;
            const auto annotate = [&](RuntimeField& out) {
                out.Quantization = quantization;
                out.OwnerOnly = ownerOnly;
                out.LocalOnly = localOnly;
            };

            using MemberType = std::remove_cvref_t<M>;
            // A field tagged AsColor() that is exactly three floats stops the
            // recursion and becomes one Color3 leaf, so the editor draws a single
            // swatch instead of x/y/z drags. The sizeof guard keeps the leaf's
            // reinterpret to float[3] safe; serialization still uses the member's
            // own schema (AsColor is editor-only).
            if constexpr (sizeof(MemberType) == 3 * sizeof(float))
            {
                if (field.IsColor)
                {
                    RuntimeField f{
                        std::move(name), offset, 3 * sizeof(float),
                        FieldScalar::Color3, AssetType::Unknown, AssetArity::Single };
                    annotate(f);
                    Out.push_back(std::move(f));
                    return;
                }
            }

            // A packed scalar group (Vec/Quat) stops the recursion and becomes one
            // N-wide leaf, so the editor draws a single row instead of x/y/z drags.
            // Checked before HasTypeSchema because these types have a schema too.
            if constexpr (PackedScalarGroup<MemberType>::Count > 0)
            {
                using Comp = typename PackedScalarGroup<MemberType>::Component;
                RuntimeField f{ std::move(name), offset, sizeof(Comp), ScalarKindOf<Comp>() };
                f.Count = static_cast<std::uint8_t>(PackedScalarGroup<MemberType>::Count);
                annotate(f);
                Out.push_back(std::move(f));
                return;
            }

            if constexpr (HasTypeSchema<MemberType>)
            {
                Collector<Root> sub{ Out, Base, name, quantization, ownerOnly, localOnly };
                VisitSchema<TypeSchema<MemberType>>(member, sub);
            }
            else if constexpr (IsStrongId<MemberType>::value)
            {
                using U = typename IsStrongId<MemberType>::Underlying;
                RuntimeField f{ std::move(name), offset, sizeof(U), ScalarKindOf<U>() };
                f.ReadOnly = true;
                annotate(f);
                Out.push_back(std::move(f));
            }
            else
            {
                RuntimeField f{ std::move(name), offset, sizeof(MemberType),
                                ScalarKindOf<MemberType>(), field.Asset, field.Arity };
                if constexpr (HasEnumSchema<MemberType>)
                    f.Enum = EnumOptionsOf<MemberType>();
                annotate(f);
                Out.push_back(std::move(f));
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
