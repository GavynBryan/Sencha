#include <effects/EffectRegistry.h>

#include <effects/ActiveEffect.h>

#include <utility>

EffectRegistry::EffectRegistry()
{
    Effects.push_back(Entry{}); // index 0 reserved so a zero id is invalid
}

EffectId EffectRegistry::Register(std::string_view name, EffectDefinition definition)
{
    if (name.empty() || definition.GrantedTags.size() > kEffectGrantedTagCapacity)
        return EffectId{};

    auto existing = IdsByName.find(std::string(name));
    if (existing != IdsByName.end())
        return existing->second;

    const EffectId id{ static_cast<std::uint32_t>(Effects.size()) };
    Effects.push_back(Entry{ std::string(name), std::move(definition) });
    IdsByName.emplace(Effects.back().Name, id);
    return id;
}

EffectId EffectRegistry::Define(std::string_view name, EffectDefinition definition)
{
    if (name.empty() || definition.GrantedTags.size() > kEffectGrantedTagCapacity)
        return EffectId{};
    const EffectId existing = Find(name);
    if (!existing.IsValid())
        return Register(name, std::move(definition));
    Effects[existing.Value].Def = std::move(definition);
    return existing;
}

EffectId EffectRegistry::Find(std::string_view name) const
{
    auto it = IdsByName.find(std::string(name));
    return it == IdsByName.end() ? EffectId{} : it->second;
}

const EffectDefinition* EffectRegistry::Get(EffectId id) const
{
    return IsKnown(id) ? &Effects[id.Value].Def : nullptr;
}

std::string_view EffectRegistry::GetName(EffectId id) const
{
    return IsKnown(id) ? std::string_view(Effects[id.Value].Name) : std::string_view{};
}

bool EffectRegistry::IsKnown(EffectId id) const
{
    return id.IsValid() && id.Value < Effects.size();
}

std::size_t EffectRegistry::Size() const
{
    return Effects.size() - 1;
}
