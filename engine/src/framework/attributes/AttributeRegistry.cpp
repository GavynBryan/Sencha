#include <framework/attributes/AttributeRegistry.h>

#include <algorithm>

AttributeRegistry::AttributeRegistry()
{
    Attrs.push_back(AttributeDef{}); // index 0 reserved so a zero id is invalid
}

AttributeId AttributeRegistry::RegisterAttribute(std::string_view name,
                                                 float min,
                                                 float max,
                                                 float defaultBase)
{
    if (name.empty())
        return AttributeId{};

    auto existing = IdsByName.find(std::string(name));
    if (existing != IdsByName.end())
        return existing->second;

    const AttributeId id{ static_cast<std::uint32_t>(Attrs.size()) };
    Attrs.push_back(AttributeDef{ std::string(name), min, max, defaultBase });
    IdsByName.emplace(Attrs.back().Name, id);
    return id;
}

AttributeId AttributeRegistry::FindAttribute(std::string_view name) const
{
    auto it = IdsByName.find(std::string(name));
    return it == IdsByName.end() ? AttributeId{} : it->second;
}

std::string_view AttributeRegistry::GetName(AttributeId id) const
{
    return IsKnown(id) ? std::string_view(Attrs[id.Value].Name) : std::string_view{};
}

bool AttributeRegistry::IsKnown(AttributeId id) const
{
    return id.IsValid() && id.Value < Attrs.size();
}

float AttributeRegistry::Min(AttributeId id) const
{
    return IsKnown(id) ? Attrs[id.Value].Min : std::numeric_limits<float>::lowest();
}

float AttributeRegistry::Max(AttributeId id) const
{
    return IsKnown(id) ? Attrs[id.Value].Max : std::numeric_limits<float>::max();
}

float AttributeRegistry::DefaultBase(AttributeId id) const
{
    return IsKnown(id) ? Attrs[id.Value].DefaultBase : 0.0f;
}

float AttributeRegistry::Clamp(AttributeId id, float value) const
{
    if (!IsKnown(id))
        return value;
    const AttributeDef& def = Attrs[id.Value];
    return std::min(std::max(value, def.Min), def.Max);
}

std::size_t AttributeRegistry::Size() const
{
    return Attrs.size() - 1;
}
