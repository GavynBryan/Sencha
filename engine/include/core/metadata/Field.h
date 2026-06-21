#pragma once

#include <core/assets/AssetRef.h>

#include <cstdint>
#include <optional>
#include <string_view>

//=============================================================================
// Field
//
// Descriptor for a named member of Class. Carries the member pointer and
// serialization metadata (optional flag, default value) consumed by archives
// and schema visitors. Build instances with MakeField(), then chain Optional()
// or Default() to annotate them.
//
// Usage:
//   MakeField("health", &Actor::Health)
//   MakeField("speed",  &Actor::Speed).Default(1.0f)
//   MakeField("tag",    &Actor::Tag).Optional()
//   MakeField("mesh",   &Actor::Mesh).AsAsset(AssetType::StaticMesh)
//   MakeField("mats",   &Actor::Mats).AsAsset(AssetType::Material, AssetArity::List)
//=============================================================================

template <typename Class, typename Member>
struct Field
{
    std::string_view Name;
    Member Class::* Ptr = nullptr;
    bool IsOptional = false;
    std::optional<Member> DefaultValue{};
    std::uint32_t StableId = 0;
    // Asset-reference shape of this member, the two co-varying together: Asset is
    // the kind (Unknown means "not an asset field"), Arity is how it is stored
    // (one handle, or an ordered list). Tooling resolves the handle to/from an
    // asset:// path (the editor renders a picker; the runtime carries plain
    // handles). Mechanism, not a per-component branch: any handle member tagged
    // here gets it.
    AssetType  Asset = AssetType::Unknown;
    AssetArity Arity = AssetArity::Single;

    Field& Optional()
    {
        IsOptional = true;
        return *this;
    }

    Field& Default(Member value)
    {
        DefaultValue = value;
        IsOptional = true;
        return *this;
    }

    Field& AsAsset(AssetType type, AssetArity arity = AssetArity::Single)
    {
        Asset = type;
        Arity = arity;
        return *this;
    }
};

template <typename Class, typename Member>
Field<Class, Member> MakeField(std::string_view name, Member Class::* ptr)
{
    return { name, ptr };
}
