#include <abilities/AbilityRegistry.h>

#include <utility>

AbilityRegistry::AbilityRegistry()
{
    Abilities.push_back(Entry{}); // index 0 reserved so a zero id is invalid
}

AbilityId AbilityRegistry::Register(std::string_view name, AbilityDefinition definition)
{
    if (name.empty())
        return AbilityId{};

    auto existing = IdsByName.find(std::string(name));
    if (existing != IdsByName.end())
        return existing->second;

    const AbilityId id{ static_cast<std::uint32_t>(Abilities.size()) };
    Abilities.push_back(Entry{ std::string(name), std::move(definition) });
    IdsByName.emplace(Abilities.back().Name, id);
    return id;
}

AbilityId AbilityRegistry::Find(std::string_view name) const
{
    auto it = IdsByName.find(std::string(name));
    return it == IdsByName.end() ? AbilityId{} : it->second;
}

const AbilityDefinition* AbilityRegistry::Get(AbilityId id) const
{
    return IsKnown(id) ? &Abilities[id.Value].Def : nullptr;
}

std::string_view AbilityRegistry::GetName(AbilityId id) const
{
    return IsKnown(id) ? std::string_view(Abilities[id.Value].Name) : std::string_view{};
}

bool AbilityRegistry::IsKnown(AbilityId id) const
{
    return id.IsValid() && id.Value < Abilities.size();
}

std::size_t AbilityRegistry::Size() const
{
    return Abilities.size() - 1;
}
