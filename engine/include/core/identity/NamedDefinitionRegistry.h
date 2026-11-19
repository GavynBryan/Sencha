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

template<typename Id, typename Definition>
class NamedDefinitionRegistry
{
public:
    NamedDefinitionRegistry() { Entries.push_back(Entry{}); }

    // Invalid for a name whose wire key another name holds: the wire could not tell them apart.
    Id Reserve(std::string_view name)
    {
        if (name.empty())
            return Id{};
        if (const auto existing = IdsByName.find(std::string(name)); existing != IdsByName.end())
            return existing->second;
        if (IdsByWireKey.contains(NameWireKey(name)))
            return Id{};
        const Id id{ static_cast<decltype(Id{}.Value)>(Entries.size()) };
        Entries.push_back(Entry{ std::string(name), nullptr, 0 });
        IdsByName.emplace(Entries.back().Name, id);
        IdsByWireKey.emplace(NameWireKey(name), id);
        return id;
    }

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

    [[nodiscard]] const Definition* Get(Id id) const { return IsKnown(id) ? Entries[id.Value].Defined.get() : nullptr; }
    [[nodiscard]] std::string_view GetName(Id id) const
    {
        return IsKnown(id) ? std::string_view(Entries[id.Value].Name) : std::string_view{};
    }
    [[nodiscard]] bool IsKnown(Id id) const { return id.IsValid() && id.Value < Entries.size(); }

    [[nodiscard]] std::uint32_t Revision(Id id) const { return IsKnown(id) ? Entries[id.Value].Revision : 0; }

    [[nodiscard]] std::uint32_t WireKey(Id id) const { return IsKnown(id) ? NameWireKey(Entries[id.Value].Name) : 0u; }
    [[nodiscard]] Id FindByWireKey(std::uint32_t key) const
    {
        const auto it = IdsByWireKey.find(key);
        return it != IdsByWireKey.end() ? it->second : Id{};
    }

    [[nodiscard]] std::string_view WireKeyHolder(std::string_view name) const
    {
        const std::string_view holder = GetName(FindByWireKey(NameWireKey(name)));
        return holder == name ? std::string_view{} : holder;
    }

    [[nodiscard]] std::size_t Size() const { return Entries.size() - 1; }

    template<typename Fn>
    void ForEachName(Fn&& visit) const
    {
        for (std::size_t i = 1; i < Entries.size(); ++i)
            visit(Id{ static_cast<decltype(Id{}.Value)>(i) }, std::string_view(Entries[i].Name));
    }

    template<typename Fn>
    void ForEachDefined(Fn&& visit) const
    {
        for (std::size_t i = 1; i < Entries.size(); ++i)
            if (Entries[i].Defined != nullptr)
                visit(Id{ static_cast<decltype(Id{}.Value)>(i) }, *Entries[i].Defined);
    }

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
