#pragma once

#include <authored/AuthoredEventId.h>
#include <authored/BindingDependencyStamp.h>
#include <core/json/JsonValue.h>
#include <ecs/EntityId.h>
#include <logic/ResponseSetKey.h>
#include <logic/VerbSequenceKey.h>
#include <logic/VerbSequenceStore.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

class World;

// Response bindings as content spells them, shared by every entity authored
// alike, and what one World made of them. docs/gameplay/responses.md.

enum class ResponseCooldownScope : std::uint8_t
{
    // One cooldown for the binding, whoever's occurrence ran it.
    Binding,
    // One per instigator, the entity the occurrence's declaration names.
    Instigator,
};

struct ResponseBindingDesc
{
    // The binding's identity: what overrides, verbs and remembered state name.
    std::string Key;
    std::uint64_t KeyHash = 0;
    // The hash of the binding's whole authored text. State survives a change
    // to the set only for bindings whose text did not change.
    std::uint64_t DefinitionHash = 0;
    // The declared event it answers.
    std::string On;
    bool StartsEnabled = true;
    bool Once = false;
    float CooldownSeconds = 0.0f;
    ResponseCooldownScope CooldownScope = ResponseCooldownScope::Binding;
    VerbSequenceKey Sequence{};
};

struct CompiledResponseBinding
{
    // Invalid when `On` names no declared event: the binding never matches.
    AuthoredEventId Event{};
    AuthoredEventRevision Revision{};
    // Which port carries the event's instigator, or none.
    std::uint8_t InstigatorPort = kNoSequencePort;
};

// A set's bindings in key order, and what the World made of each.
struct ResponseSetView
{
    std::span<const ResponseBindingDesc> Bindings;
    std::span<const CompiledResponseBinding> Compiled;
};

// One World's response sets by content key. An entry outlives every component
// that names it, as the sequence store's do.
class ResponseSetStore
{
public:
    // The bindings object: {"<key>": {"on": ..., "sequence": {...}, ...}, ...}.
    // Each binding's sequence is interned into `sequences` under its event, so
    // it compiles against that event's ports.
    [[nodiscard]] ResponseSetKey Intern(const JsonValue& bindings, VerbSequenceStore& sequences);

    [[nodiscard]] const JsonValue* Source(ResponseSetKey set) const;

    // In key order; empty for a key this store never interned.
    [[nodiscard]] std::span<const ResponseBindingDesc> Bindings(ResponseSetKey set) const;

    // Bit i: binding i is authored to start disabled.
    [[nodiscard]] std::uint32_t StartsDisabled(ResponseSetKey set) const;

    // Reads the event catalog's stamp once; Resolve compares against it. Call at
    // a pass boundary, before resolving.
    void Revalidate(const World& world);

    // The bindings and their compiled form, in key order, in one lookup; compiled
    // again only when the catalog moved since. Empty for a key this store never
    // interned.
    [[nodiscard]] ResponseSetView Resolve(ResponseSetKey set, const World& world);

    [[nodiscard]] std::span<const std::string> Diagnostics(ResponseSetKey set) const;
    [[nodiscard]] std::size_t Size() const { return Entries.size(); }

private:
    struct Entry
    {
        std::string Text;
        JsonValue Source;
        std::vector<ResponseBindingDesc> Bindings;
        std::vector<CompiledResponseBinding> Compiled;
        std::vector<std::string> ReadErrors;
        std::vector<std::string> Diagnostics;
        CatalogStamp CompiledAgainst;
        std::uint32_t StartsDisabled = 0;
        bool Built = false;
    };

    void Compile(Entry& entry, const World& world);

    std::unordered_map<ResponseSetKey, Entry> Entries;
    CatalogStamp Current;
    bool Revalidated = false;
};

// Every response sequence reads the entity it belongs to first, then the
// payload its event declares.
inline constexpr std::uint8_t kResponseSelfPort = 0;
inline constexpr std::array<SequencePort, 1> kResponsePorts = { {
    { "self", DataFieldKind::Entity },
} };

// The World's response definitions, created on first use.
struct ResponseDefinitions
{
    VerbSequenceStore Sequences{ kResponsePorts };
    ResponseSetStore Sets;
};

[[nodiscard]] ResponseDefinitions& ResponseDefinitionsOf(World& world);

// {} -- a set with no bindings.
[[nodiscard]] JsonValue EmptyResponseBindings();

// Replaces the entity's bindings with the ones `bindings` spells. A binding
// whose key and text both survive keeps its state; one that is new or whose
// text changed starts as authored; one that is gone takes its state with it.
// False when the entity carries no Responses.
bool SetResponseBindings(World& world, EntityId entity, const JsonValue& bindings);

// The authored text of the entity's bindings, or null.
[[nodiscard]] const JsonValue* ResponseBindingsSource(const World& world, EntityId entity);

// The index of the binding keyed `key` in `bindings`, or -1.
[[nodiscard]] int FindResponseBinding(std::span<const ResponseBindingDesc> bindings, std::uint64_t keyHash);
[[nodiscard]] std::uint64_t ResponseKeyHash(std::string_view key);
