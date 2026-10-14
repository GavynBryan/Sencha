#pragma once

#include "properties/PropertyNode.h"

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

// Where a property sits in a tree of them: the list holding it, and its place
// in that list, so a caller can change it or put another beside it.
struct PropertyPlace
{
    std::vector<PropertyNode>* Siblings = nullptr;
    std::size_t Index = 0;

    [[nodiscard]] PropertyNode& Node() const { return (*Siblings)[Index]; }
};

[[nodiscard]] std::optional<PropertyPlace> FindPropertyNode(std::vector<PropertyNode>& nodes, std::string_view key);
// The property keyed `key` anywhere in the tree, or null.
[[nodiscard]] const PropertyNode* FindProperty(const std::vector<PropertyNode>& nodes, std::string_view key);

// A key that numbers one of several -- "walls.2.centre" after "walls." -- as
// its number and what follows it, the leading dot dropped; nothing when the key
// does not start with the prefix and a number.
struct PropertyKeyIndex
{
    std::size_t Index = 0;
    std::string_view Rest;
};
[[nodiscard]] std::optional<PropertyKeyIndex> ParsePropertyKeyIndex(std::string_view key, std::string_view prefix);
