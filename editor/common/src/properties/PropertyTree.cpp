#include "properties/PropertyTree.h"

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
