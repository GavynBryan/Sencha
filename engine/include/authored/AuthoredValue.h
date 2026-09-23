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

// The runtime values that cross the authored boundary: verb arguments, query
// arguments and answers, event payloads. DataFieldSchema declares shapes; this
// holds them. Values own their storage.

enum class AuthoredValueKind : std::uint8_t
{
    // Absent.
    None,
    Bool,
    Int,
    Float,
    String,
    Enum,
    Vector,
    Record,
    Array,
    // A reference, not a loaded handle; loading stays with the asset owners.
    AssetRef,
    DataAssetRef,
    GameplayTag,
    Entity,
    // An entity constant, resolved to an Entity by the dispatcher at every
    // invocation. Operations never see this kind.
    PersistentEntity,
};

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

    // False on a kind mismatch, leaving `out` untouched.
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
    // Not a variant alternative: std::variant cannot hold the incomplete type
    // being defined; std::vector can.
    std::vector<AuthoredValue> Children_;
};

// Kind, range and enum choice. The one place values and schemas are related.
[[nodiscard]] bool AuthoredValueSatisfiesField(const AuthoredValue& value, const DataFieldSchema& field);

// Values indexed by their field's position in the declaring record.
class AuthoredArguments
{
public:
    AuthoredArguments() = default;
    explicit AuthoredArguments(std::size_t slots) : Slots(slots) {}

    [[nodiscard]] std::size_t Size() const { return Slots.size(); }

    // Keeps existing storage, so a reused pack stops allocating once warmed.
    void Resize(std::size_t slots);

    void Set(std::size_t slot, AuthoredValue value);

    // None for an out-of-range slot.
    [[nodiscard]] const AuthoredValue& At(std::size_t slot) const;

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
