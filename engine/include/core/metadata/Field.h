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
//   MakeField("pos",    &Actor::Pos).Quantize(-4096.0f, 4096.0f, 20)
//   MakeField("cooldown", &Actor::Cooldown).OwnerOnly()
//   MakeField("blend",  &Actor::Blend).LocalOnly()
//=============================================================================

//=============================================================================
// FieldQuantization
//
// Lossy fixed-point encoding for a float leaf: Bits bits spanning [Min, Max].
// Bits == 0 means the leaf is carried at its natural width.
//
// This is a property of the data, not of any one consumer: it states the range
// the value actually occupies and the precision it is meaningful to. A wire
// codec reads it to pack; an editor could read the same range to bound a
// slider.
//=============================================================================
struct FieldQuantization
{
    float Min = 0.0f;
    float Max = 0.0f;
    std::uint8_t Bits = 0;

    [[nodiscard]] constexpr bool IsQuantized() const { return Bits > 0; }
    bool operator==(const FieldQuantization&) const = default;
};

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
    // Editor hint: a 3-float member tagged here is an RGB color, so the inspector
    // shows a swatch + picker instead of three drag fields. View-only: the
    // serialized form is unchanged (still the member's own [x,y,z] schema).
    bool IsColor = false;
    // Replication scope and precision. All three describe the member itself, so
    // they are declared once beside it rather than in a second registry that
    // could drift; a replication writer is the consumer. Tagging a member that
    // has its own schema (a Vec3, a Transform) applies to every leaf beneath it.
    FieldQuantization Quantization{};
    // Sent only to the peer that owns the entity. Private state a spectator has
    // no business seeing, and the cheapest anti-cheat there is: what is never
    // sent cannot be read out of a client's memory.
    bool IsOwnerOnly = false;
    // Never leaves the machine that computes it. Derived caches, presentation
    // smoothing, handles that mean nothing on another process.
    bool IsLocalOnly = false;

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

    Field& AsColor()
    {
        IsColor = true;
        return *this;
    }

    // The range this member actually occupies and the precision worth carrying.
    // A value outside [min, max] clamps rather than wrapping, so a bad range is
    // a visible pin rather than a value that teleports.
    Field& Quantize(float min, float max, std::uint8_t bits)
    {
        Quantization = FieldQuantization{ min, max, bits };
        return *this;
    }

    Field& OwnerOnly()
    {
        IsOwnerOnly = true;
        return *this;
    }

    Field& LocalOnly()
    {
        IsLocalOnly = true;
        return *this;
    }
};

template <typename Class, typename Member>
Field<Class, Member> MakeField(std::string_view name, Member Class::* ptr)
{
    return { name, ptr };
}
