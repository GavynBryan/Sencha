#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

//=============================================================================
// UiValue
//
// One presentation value: what a document is told, never what the application
// holds. Copied across the boundary by construction -- there is no reference
// form, so a document cannot end up aliasing gameplay or editor state.
//
// The set is deliberately small. It covers what a surface actually presents: a
// number, a flag, an already-localised string, and a stable identity for
// something the host will recognise when it comes back in an action. Adding a
// kind is a claim that documents need to present something this cannot express,
// which is a design question rather than a convenience.
//
// Arrays and value structs are not here yet. They need the document engine's
// array registration rather than this variant, and nothing presents one, so
// they land with the surface that first needs them.
//=============================================================================

enum class UiValueKind : std::uint8_t
{
    None = 0,
    Bool,
    Int,
    Float,
    String,
    // A stable identity the host minted -- an entity, an asset, a row. Opaque
    // here on purpose: a document presents it and hands it back in an action,
    // and never learns what it refers to.
    Id,
};

class UiValue
{
public:
    UiValue() = default;
    UiValue(bool value) : Storage(value) {}
    UiValue(std::int64_t value) : Storage(value) {}
    UiValue(int value) : Storage(static_cast<std::int64_t>(value)) {}
    UiValue(double value) : Storage(value) {}
    UiValue(float value) : Storage(static_cast<double>(value)) {}
    UiValue(std::string value) : Storage(std::move(value)) {}
    UiValue(std::string_view value) : Storage(std::string(value)) {}
    UiValue(const char* value) : Storage(std::string(value)) {}

    // Distinct from Int so a document formatting a number cannot accidentally
    // print an identity, and so a host can tell them apart coming back.
    [[nodiscard]] static UiValue MakeId(std::uint64_t id)
    {
        UiValue value;
        value.Storage = Identity{ id };
        return value;
    }

    [[nodiscard]] UiValueKind Kind() const
    {
        return static_cast<UiValueKind>(Storage.index());
    }
    [[nodiscard]] bool IsNone() const { return Kind() == UiValueKind::None; }

    // Typed reads. A mismatched kind returns the fallback rather than throwing:
    // a document asking for the wrong shape is an authoring error that should
    // show up as a wrong-looking screen, not as a dead process.
    [[nodiscard]] bool AsBool(bool fallback = false) const;
    [[nodiscard]] std::int64_t AsInt(std::int64_t fallback = 0) const;
    [[nodiscard]] double AsFloat(double fallback = 0.0) const;
    [[nodiscard]] std::string_view AsString(std::string_view fallback = {}) const;
    [[nodiscard]] std::uint64_t AsId(std::uint64_t fallback = 0) const;

    // Equality is what decides whether a set marks the property dirty, so it
    // has to be exact: a value that compares equal when it is not would stop
    // the screen updating, which is invisible until someone notices a stale
    // number.
    friend bool operator==(const UiValue&, const UiValue&) = default;

private:
    struct Identity
    {
        std::uint64_t Value = 0;
        friend bool operator==(Identity, Identity) = default;
    };

    // Order matches UiValueKind, which is what Kind() relies on.
    std::variant<std::monostate, bool, std::int64_t, double, std::string, Identity> Storage;
};
