#include <abilities/AbilityRegistry.h>

#include <utility>

AbilityRegistry::AbilityRegistry()
{
    Abilities.push_back(Entry{});
}

AbilityId AbilityRegistry::Reserve(std::string_view name)
{
    if (name.empty())
        return AbilityId{};
    if (const auto existing = IdsByName.find(std::string(name)); existing != IdsByName.end())
        return existing->second;

    const AbilityId id{ static_cast<std::uint32_t>(Abilities.size()) };
    Abilities.push_back(Entry{ std::string(name), nullptr, 0 });
    IdsByName.emplace(Abilities.back().Name, id);
    return id;
}

AbilityId AbilityRegistry::Define(std::string_view name, AbilityDefinition definition)
{
    const AbilityId id = Reserve(name);
    if (!id.IsValid())
        return id;
    Abilities[id.Value].Definition = std::make_unique<const AbilityDefinition>(std::move(definition));
    ++Abilities[id.Value].Revision;
    ++Generation_;
    return id;
}

void AbilityRegistry::Undefine(AbilityId id)
{
    if (!IsKnown(id) || Abilities[id.Value].Definition == nullptr)
        return;
    Abilities[id.Value].Definition = nullptr;
    ++Abilities[id.Value].Revision;
    ++Generation_;
}

AbilityId AbilityRegistry::Find(std::string_view name) const
{
    const auto it = IdsByName.find(std::string(name));
    return it == IdsByName.end() ? AbilityId{} : it->second;
}

const AbilityDefinition* AbilityRegistry::Get(AbilityId id) const
{
    return IsKnown(id) ? Abilities[id.Value].Definition.get() : nullptr;
}

std::string_view AbilityRegistry::GetName(AbilityId id) const
{
    return IsKnown(id) ? std::string_view(Abilities[id.Value].Name) : std::string_view{};
}

std::uint32_t AbilityRegistry::Revision(AbilityId id) const
{
    return IsKnown(id) ? Abilities[id.Value].Revision : 0;
}

bool AbilityRegistry::IsKnown(AbilityId id) const
{
    return id.IsValid() && id.Value < Abilities.size();
}

std::size_t AbilityRegistry::Size() const
{
    return Abilities.size() - 1;
}
