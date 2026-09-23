#pragma once

#include <core/assets/AssetRef.h>
#include <core/identity/Id.h>
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
// AuthoredValue and AuthoredArguments
//
// What crosses the authored boundary at runtime: a verb's arguments, a query's
// arguments and answer, an event's payload. Typed values in the order their
// schema declares them, already checked, already resolved, with nothing left to
// parse. One vocabulary for all three, so a bool is carried the same way
// whichever contract it belongs to.
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

enum class AuthoredValueKind : std::uint8_t
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
    // A live generational handle. What a producer supplies for an entity
    // input, and what an operation reads for an entity argument.
    Entity,
    // An authored persistent identity, not yet a handle. What an entity
    // constant compiles to: it is resolved against the World's persistent
    // entity index at every invocation, so a binding compiled while its target
    // was absent, or before the target streamed out and back with a new
    // generation, still reaches the entity the author named. An operation
    // never sees this kind -- the dispatcher resolves it or refuses.
    PersistentEntity,
};

// A 2-, 3- or 4-wide numeric tuple, as DataFieldKind::Vector describes.
struct AuthoredVectorValue
{
    std::array<double, 4> Components{};
    std::uint8_t Length = 3;

    friend bool operator==(const AuthoredVectorValue&, const AuthoredVectorValue&) = default;
};

class AuthoredValue
{
public:
    AuthoredValue() = default;

    [[nodiscard]] static AuthoredValue Bool(bool value);
    [[nodiscard]] static AuthoredValue Int(std::int64_t value);
    [[nodiscard]] static AuthoredValue Float(double value);
    [[nodiscard]] static AuthoredValue String(std::string value);
    [[nodiscard]] static AuthoredValue Enum(std::string choice);
    [[nodiscard]] static AuthoredValue Vector(AuthoredVectorValue value);
    [[nodiscard]] static AuthoredValue Record(std::vector<AuthoredValue> members);
    [[nodiscard]] static AuthoredValue Array(std::vector<AuthoredValue> elements);
    [[nodiscard]] static AuthoredValue Asset(AssetRef reference);
    [[nodiscard]] static AuthoredValue DataAsset(AssetRef reference);
    [[nodiscard]] static AuthoredValue Tag(GameplayTagId tag);
    [[nodiscard]] static AuthoredValue Entity(EntityId entity);
    [[nodiscard]] static AuthoredValue PersistentEntity(PersistentEntityId identity);

    [[nodiscard]] AuthoredValueKind Kind() const { return Kind_; }
    [[nodiscard]] bool IsNone() const { return Kind_ == AuthoredValueKind::None; }

    // Checked reads. False on a kind mismatch, leaving `out` untouched: an
    // implementation reading the wrong slot must find out, not silently act on
    // a zero.
    [[nodiscard]] bool TryGetBool(bool& out) const;
    [[nodiscard]] bool TryGetInt(std::int64_t& out) const;
    [[nodiscard]] bool TryGetFloat(double& out) const;
    [[nodiscard]] bool TryGetString(std::string_view& out) const;
    [[nodiscard]] bool TryGetEnum(std::string_view& out) const;
    [[nodiscard]] bool TryGetVector(AuthoredVectorValue& out) const;
    [[nodiscard]] bool TryGetAsset(const AssetRef*& out) const;
    [[nodiscard]] bool TryGetDataAsset(const AssetRef*& out) const;
    [[nodiscard]] bool TryGetTag(GameplayTagId& out) const;
    [[nodiscard]] bool TryGetEntity(EntityId& out) const;
    [[nodiscard]] bool TryGetPersistentEntity(PersistentEntityId& out) const;

    // Members of a record, elements of an array. Empty for every other kind.
    [[nodiscard]] std::span<const AuthoredValue> Children() const { return Children_; }

private:
    using Scalar = std::variant<std::monostate,
                                bool,
                                std::int64_t,
                                double,
                                std::string,
                                AuthoredVectorValue,
                                AssetRef,
                                GameplayTagId,
                                EntityId,
                                PersistentEntityId>;

    AuthoredValueKind Kind_ = AuthoredValueKind::None;
    Scalar Value_;
    // Outside the variant on purpose: a variant alternative has to be complete
    // where the variant is instantiated, and this one is the type being
    // defined. std::vector may name an incomplete element type; std::variant
    // may not.
    std::vector<AuthoredValue> Children_;
};

// Whether a value is one the field would accept: the right kind, inside the
// declared range, and naming a declared choice. The single place the two
// vocabularies are related, so a new field kind has one edit rather than one
// per consumer.
//
// This is the check a dynamic value gets at invocation. A constant gets more
// than this at compile time, where a diagnostic can still say which argument of
// which binding was wrong.
[[nodiscard]] bool AuthoredValueSatisfiesField(const AuthoredValue& value, const DataFieldSchema& field);

//-----------------------------------------------------------------------------
// AuthoredArguments
//
// One invocation's arguments, or one event's payload, indexed by the position
// of the field in the record root that declared them. The binding compiler
// decides the order once; dispatch indexes it. Generated adapters read slots by
// that position, which is the order of the annotated parameters or members.
//-----------------------------------------------------------------------------
class AuthoredArguments
{
public:
    AuthoredArguments() = default;
    explicit AuthoredArguments(std::size_t slots) : Slots(slots) {}

    [[nodiscard]] std::size_t Size() const { return Slots.size(); }

    // Keeps the storage it already has, so a pack reused across invocations
    // stops allocating once it has seen its widest verb.
    void Resize(std::size_t slots);

    void Set(std::size_t slot, AuthoredValue value);

    // The empty value for an out-of-range slot, so a caller that got its
    // indexing wrong reads None rather than reading past the end.
    [[nodiscard]] const AuthoredValue& At(std::size_t slot) const;

    // Checked reads, forwarding to the value's own. False when the slot does
    // not exist or does not hold that kind.
    [[nodiscard]] bool TryGetBool(std::size_t slot, bool& out) const;
    [[nodiscard]] bool TryGetInt(std::size_t slot, std::int64_t& out) const;
    [[nodiscard]] bool TryGetFloat(std::size_t slot, double& out) const;
    [[nodiscard]] bool TryGetString(std::size_t slot, std::string_view& out) const;
    [[nodiscard]] bool TryGetEnum(std::size_t slot, std::string_view& out) const;
    [[nodiscard]] bool TryGetVector(std::size_t slot, AuthoredVectorValue& out) const;
    [[nodiscard]] bool TryGetAsset(std::size_t slot, const AssetRef*& out) const;
    [[nodiscard]] bool TryGetDataAsset(std::size_t slot, const AssetRef*& out) const;
    [[nodiscard]] bool TryGetTag(std::size_t slot, GameplayTagId& out) const;
    [[nodiscard]] bool TryGetEntity(std::size_t slot, EntityId& out) const;
    [[nodiscard]] bool TryGetPersistentEntity(std::size_t slot, PersistentEntityId& out) const;

    [[nodiscard]] std::span<const AuthoredValue> Values() const { return Slots; }

private:
    std::vector<AuthoredValue> Slots;
};
