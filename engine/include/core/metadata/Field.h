#pragma once

#include <core/assets/AssetRef.h>

#include <cstdint>
#include <optional>
#include <type_traits>
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
//   MakeField("profile", &Actor::Profile).AsDataAsset("movement.profile")
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
    // Asset-reference shape of this member, the two co-varying together: Asset is
    // the kind (Unknown means "not an asset field"), Arity is how it is stored
    // (one handle, or an ordered list). Tooling resolves the handle to/from an
    // asset:// path (the editor renders a picker; the runtime carries plain
    // handles). Mechanism, not a per-component branch: any handle member tagged
    // here gets it.
    AssetType  Asset = AssetType::Unknown;
    AssetArity Arity = AssetArity::Single;
    // Which subtype a structured-data reference accepts ("movement.profile").
    // Only meaningful for an AssetType::Data member; empty means any data
    // asset. Tooling narrows a picker to it, so a designer cannot bind a
    // profile field to an input map. Never serialized -- the persisted form is
    // still a path, and the subtype is what the file itself declares.
    std::string_view DataSubtype{};
    // Editor hint: a 3-float member tagged here is an RGB color, so the inspector
    // shows a swatch + picker instead of three drag fields. View-only: the
    // serialized form is unchanged (still the member's own [x,y,z] schema).
    bool IsColor = false;
    // Editor hint: a float member tagged here is an angle in radians that an
    // authoring surface shows and edits in degrees. View-only in the same way
    // AsColor is -- the stored bytes, the scene, and the wire stay radians, and
    // the persisted key keeps whatever the member is called.
    bool IsDegrees = false;
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
    // Sent to everyone except the peer that owns the entity, because that peer
    // computes it themselves and holds a fresher answer than the one coming
    // back. Aim is the case: a player's view must follow their mouse now, not
    // at the end of a round trip.
    bool IsOwnerLocal = false;
    // Display metadata, editor-only: what an inspector row shows instead of
    // the humanized field name, and what its hover explains. Serialization
    // and the wire never read either -- the persisted key stays Name.
    std::string_view DisplayLabel{};
    std::string_view DisplayTooltip{};

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

    // Declares that this member is an owning reference to a resident asset: the
    // component holds one reference for as long as it carries the value, and
    // this is the only statement of which kind it refers to. Serialization and
    // the asset-field editors both read it, and neither names the handle type.
    //
    // The member has to be a handle for that to work -- they address it as the
    // opaque token every handle is, without knowing which one it is.
    Field& AsAsset(AssetType type, AssetArity arity = AssetArity::Single)
    {
        static_assert(sizeof(Member) == sizeof(std::uint64_t)
                          && std::is_trivially_copyable_v<Member>,
                      "an asset field must be a handle: it is carried as an "
                      "8-byte token by code that never names its type");
        Asset = type;
        Arity = arity;
        return *this;
    }

    // A reference to one structured-data asset of a named subtype. Kind and
    // subtype are set together because a subtype without AssetType::Data
    // describes nothing, and the pairing is what lets a picker offer only the
    // assets the field can actually accept. The argument must outlive the
    // schema (string literals do).
    Field& AsDataAsset(std::string_view subtype)
    {
        static_assert(sizeof(Member) == sizeof(std::uint64_t)
                          && std::is_trivially_copyable_v<Member>,
                      "an asset field must be a handle: it is carried as an "
                      "8-byte token by code that never names its type");
        Asset = AssetType::Data;
        Arity = AssetArity::Single;
        DataSubtype = subtype;
        return *this;
    }

    Field& AsColor()
    {
        IsColor = true;
        return *this;
    }

    // An angle stored in radians and authored in degrees. Nobody pictures 1.4;
    // everybody pictures 80 degrees.
    Field& Degrees()
    {
        IsDegrees = true;
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

    Field& OwnerLocal()
    {
        IsOwnerLocal = true;
        return *this;
    }

    Field& LocalOnly()
    {
        IsLocalOnly = true;
        return *this;
    }

    // The label an inspector row shows in place of the humanized field name.
    // The argument must outlive the schema (string literals do).
    Field& Label(std::string_view label)
    {
        DisplayLabel = label;
        return *this;
    }

    Field& Tooltip(std::string_view tooltip)
    {
        DisplayTooltip = tooltip;
        return *this;
    }
};

template <typename Class, typename Member>
Field<Class, Member> MakeField(std::string_view name, Member Class::* ptr)
{
    return { name, ptr };
}
