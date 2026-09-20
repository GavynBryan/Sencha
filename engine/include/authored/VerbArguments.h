#pragma once

#include <core/assets/AssetRef.h>
#include <core/metadata/DataSchema.h>
#include <ecs/EntityId.h>
#include <gameplay_tags/GameplayTagId.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

//=============================================================================
// VerbValue and VerbArguments
//
// What an operation is actually handed: typed values in the order its argument
// schema declares them, already checked, already resolved, with nothing left to
// parse.
//
// This is a runtime representation, not a second schema language. The shapes
// exist because neither JSON nor UiValue can carry the full contract a
// DataFieldSchema describes -- JSON has one number type and no way to say
// "gameplay tag", and UiValue deliberately presents rather than describes. The
// schema remains the only place a shape is *declared*; this is the place one is
// *held*.
//
// A value owns its storage. An implementation that defers work copies what it
// needs or keeps the whole pack, and never retains a view into a UiAction, a
// schema vector, or an editor document.
//=============================================================================

enum class VerbValueKind : std::uint8_t
{
    // An absent optional, and what a checked read of an empty slot returns.
    None,
    Bool,
    Int,
    Float,
    String,
    // The canonical choice value, not its label.
    Enum,
    Vector,
    Record,
    Array,
    // An asset the content named. Held as a reference rather than a loaded
    // handle: acquiring and releasing stays with the asset owners, and an
    // operation that needs the bytes leases them through its own dependency.
    AssetRef,
    DataAssetRef,
    // Already resolved against the bound World's tag registry.
    GameplayTag,
    // Already resolved against the bound World's persistent entity index. Still
    // revalidated for liveness at execution: a one-shot request whose target
    // has gone fails rather than waiting for a later incarnation.
    Entity,
};

// A 2-, 3- or 4-wide numeric tuple, as DataFieldKind::Vector describes.
struct VerbVectorValue
{
    std::array<double, 4> Components{};
    std::uint8_t Length = 3;

    friend bool operator==(const VerbVectorValue&, const VerbVectorValue&) = default;
};

class VerbValue
{
public:
    VerbValue() = default;

    [[nodiscard]] static VerbValue Bool(bool value);
    [[nodiscard]] static VerbValue Int(std::int64_t value);
    [[nodiscard]] static VerbValue Float(double value);
    [[nodiscard]] static VerbValue String(std::string value);
    [[nodiscard]] static VerbValue Enum(std::string choice);
    [[nodiscard]] static VerbValue Vector(VerbVectorValue value);
    [[nodiscard]] static VerbValue Record(std::vector<VerbValue> members);
    [[nodiscard]] static VerbValue Array(std::vector<VerbValue> elements);
    [[nodiscard]] static VerbValue Asset(AssetRef reference);
    [[nodiscard]] static VerbValue DataAsset(AssetRef reference);
    [[nodiscard]] static VerbValue Tag(GameplayTagId tag);
    [[nodiscard]] static VerbValue Entity(EntityId entity);

    [[nodiscard]] VerbValueKind Kind() const { return Kind_; }
    [[nodiscard]] bool IsNone() const { return Kind_ == VerbValueKind::None; }

    // Checked reads. False on a kind mismatch, leaving `out` untouched: an
    // implementation reading the wrong slot must find out, not silently act on
    // a zero.
    [[nodiscard]] bool TryGetBool(bool& out) const;
    [[nodiscard]] bool TryGetInt(std::int64_t& out) const;
    [[nodiscard]] bool TryGetFloat(double& out) const;
    [[nodiscard]] bool TryGetString(std::string_view& out) const;
    [[nodiscard]] bool TryGetEnum(std::string_view& out) const;
    [[nodiscard]] bool TryGetVector(VerbVectorValue& out) const;
    [[nodiscard]] bool TryGetAsset(const AssetRef*& out) const;
    [[nodiscard]] bool TryGetDataAsset(const AssetRef*& out) const;
    [[nodiscard]] bool TryGetTag(GameplayTagId& out) const;
    [[nodiscard]] bool TryGetEntity(EntityId& out) const;

    // Members of a record, elements of an array. Empty for every other kind.
    [[nodiscard]] std::span<const VerbValue> Children() const { return Children_; }

private:
    using Scalar = std::variant<std::monostate,
                                bool,
                                std::int64_t,
                                double,
                                std::string,
                                VerbVectorValue,
                                AssetRef,
                                GameplayTagId,
                                EntityId>;

    VerbValueKind Kind_ = VerbValueKind::None;
    Scalar Value_;
    // Outside the variant on purpose: a variant alternative has to be complete
    // where the variant is instantiated, and this one is the type being
    // defined. std::vector may name an incomplete element type; std::variant
    // may not.
    std::vector<VerbValue> Children_;
};

// Whether a value is one the field would accept: the right kind, inside the
// declared range, and naming a declared choice. The single place the two
// vocabularies are related, so a new field kind has one edit rather than one
// per consumer.
//
// This is the check a dynamic value gets at invocation. A constant gets more
// than this at compile time, where a diagnostic can still say which argument of
// which binding was wrong.
[[nodiscard]] bool VerbValueSatisfiesField(const VerbValue& value, const DataFieldSchema& field);

//-----------------------------------------------------------------------------
// VerbArguments
//
// One invocation's arguments, indexed by the position of the field in the
// verb's record root. The binding compiler decides the order once; dispatch
// indexes it. An implementation names its own slots as constants beside the
// schema it declared them in.
//-----------------------------------------------------------------------------
class VerbArguments
{
public:
    VerbArguments() = default;
    explicit VerbArguments(std::size_t slots) : Slots(slots) {}

    [[nodiscard]] std::size_t Size() const { return Slots.size(); }

    // Keeps the storage it already has, so a pack reused across invocations
    // stops allocating once it has seen its widest verb.
    void Resize(std::size_t slots);

    void Set(std::size_t slot, VerbValue value);

    // The empty value for an out-of-range slot, so a caller that got its
    // indexing wrong reads None rather than reading past the end.
    [[nodiscard]] const VerbValue& At(std::size_t slot) const;

    // Checked reads, forwarding to the value's own. False when the slot does
    // not exist or does not hold that kind.
    [[nodiscard]] bool TryGetBool(std::size_t slot, bool& out) const;
    [[nodiscard]] bool TryGetInt(std::size_t slot, std::int64_t& out) const;
    [[nodiscard]] bool TryGetFloat(std::size_t slot, double& out) const;
    [[nodiscard]] bool TryGetString(std::size_t slot, std::string_view& out) const;
    [[nodiscard]] bool TryGetEnum(std::size_t slot, std::string_view& out) const;
    [[nodiscard]] bool TryGetVector(std::size_t slot, VerbVectorValue& out) const;
    [[nodiscard]] bool TryGetAsset(std::size_t slot, const AssetRef*& out) const;
    [[nodiscard]] bool TryGetDataAsset(std::size_t slot, const AssetRef*& out) const;
    [[nodiscard]] bool TryGetTag(std::size_t slot, GameplayTagId& out) const;
    [[nodiscard]] bool TryGetEntity(std::size_t slot, EntityId& out) const;

    [[nodiscard]] std::span<const VerbValue> Values() const { return Slots; }

private:
    std::vector<VerbValue> Slots;
};
