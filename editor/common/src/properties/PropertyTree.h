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
