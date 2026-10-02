#include "properties/PropertyTree.h"

#include <charconv>

std::optional<PropertyPlace> FindPropertyNode(std::vector<PropertyNode>& nodes, std::string_view key)
{
    for (std::size_t index = 0; index < nodes.size(); ++index)
    {
        if (nodes[index].Key == key)
            return PropertyPlace{ &nodes, index };
        if (std::optional<PropertyPlace> inside = FindPropertyNode(nodes[index].Children, key))
            return inside;
    }
    return std::nullopt;
}

const PropertyNode* FindProperty(const std::vector<PropertyNode>& nodes, std::string_view key)
{
    for (const PropertyNode& node : nodes)
    {
        if (node.Key == key)
            return &node;
        if (const PropertyNode* inside = FindProperty(node.Children, key))
            return inside;
    }
    return nullptr;
}

std::optional<PropertyKeyIndex> ParsePropertyKeyIndex(std::string_view key, std::string_view prefix)
{
    if (!key.starts_with(prefix))
        return std::nullopt;
    key.remove_prefix(prefix.size());
    PropertyKeyIndex parsed;
    const auto [end, error] = std::from_chars(key.data(), key.data() + key.size(), parsed.Index);
    if (error != std::errc{})
        return std::nullopt;
    parsed.Rest = key.substr(static_cast<std::size_t>(end - key.data()));
    if (parsed.Rest.starts_with('.'))
        parsed.Rest.remove_prefix(1);
    return parsed;
}
