#pragma once

#include <core/hash/Fnv1a.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

// One World's definitions of one kind, named by the asset path that declares
// them. A name is given an id the first time anything refers to it, so a
// reference read before its definition is declared, or one whose definition
// failed to compile, keeps its id and its name; the definition arrives, and is
// replaced on reload, under that id.
template<typename Id, typename Definition>
class NamedDefinitionRegistry
{
public:
    NamedDefinitionRegistry() { Entries.push_back(Entry{}); }

    // The id for `name`, minted if new. Invalid only for an empty name.
    Id Reserve(std::string_view name)
    {
        if (name.empty())
            return Id{};
        if (const auto existing = IdsByName.find(std::string(name)); existing != IdsByName.end())
            return existing->second;
        const Id id{ static_cast<decltype(Id{}.Value)>(Entries.size()) };
        Entries.push_back(Entry{ std::string(name), nullptr, 0 });
        IdsByName.emplace(Entries.back().Name, id);
        if (const auto [key, inserted] = IdsByWireKey.emplace(NameWireKey(name), id); !inserted)
            key->second = Id{};
        return id;
    }

    // Replaces whatever `name` held and keeps its id.
    Id Define(std::string_view name, Definition definition)
    {
        const Id id = Reserve(name);
        if (!id.IsValid())
            return id;
        Entries[id.Value].Defined = std::make_unique<const Definition>(std::move(definition));
        ++Entries[id.Value].Revision;
        ++Generation_;
        return id;
    }

    // Leaves the name reserved and the definition gone.
    void Undefine(Id id)
    {
        if (!IsKnown(id) || Entries[id.Value].Defined == nullptr)
            return;
        Entries[id.Value].Defined = nullptr;
        ++Entries[id.Value].Revision;
        ++Generation_;
    }

    [[nodiscard]] Id Find(std::string_view name) const
    {
        const auto it = IdsByName.find(std::string(name));
        return it == IdsByName.end() ? Id{} : it->second;
    }

    // Null for an unknown id or a reserved name nothing has defined.
    [[nodiscard]] const Definition* Get(Id id) const { return IsKnown(id) ? Entries[id.Value].Defined.get() : nullptr; }
    [[nodiscard]] std::string_view GetName(Id id) const
    {
        return IsKnown(id) ? std::string_view(Entries[id.Value].Name) : std::string_view{};
    }
    [[nodiscard]] bool IsKnown(Id id) const { return id.IsValid() && id.Value < Entries.size(); }

    // Moves each time `id` is defined or undefined, so a user can tell that the
    // definition it started from has gone.
    [[nodiscard]] std::uint32_t Revision(Id id) const { return IsKnown(id) ? Entries[id.Value].Revision : 0; }

    // Its name's wire key, which names the same definition on every machine;
    // zero for an unknown id. A key two names share finds nothing.
    [[nodiscard]] std::uint32_t WireKey(Id id) const { return IsKnown(id) ? NameWireKey(Entries[id.Value].Name) : 0u; }
    [[nodiscard]] Id FindByWireKey(std::uint32_t key) const
    {
        const auto it = IdsByWireKey.find(key);
        return it != IdsByWireKey.end() ? it->second : Id{};
    }

    [[nodiscard]] std::size_t Size() const { return Entries.size() - 1; }

    // Moves whenever a definition is defined or undefined.
    [[nodiscard]] std::uint64_t Generation() const { return Generation_; }

private:
    struct Entry
    {
        std::string Name;
        // Boxed, so a definition's address survives later names being reserved.
        std::unique_ptr<const Definition> Defined;
        std::uint32_t Revision = 0;
    };

    std::vector<Entry> Entries;
    std::unordered_map<std::string, Id> IdsByName;
    std::unordered_map<std::uint32_t, Id> IdsByWireKey;
    std::uint64_t Generation_ = 0;
};
