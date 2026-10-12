#pragma once

#include <core/metadata/ComponentDefinition.h>

#include <string_view>

// Whether a tool may put an entity carrying just this component, at its
// defaults, somewhere in a world, under what name and with which of the tool's
// icons; an empty label is not placeable (docs/editor/level-design.md).
struct ComponentPlacementFacts
{
    std::string_view Label = {};
    std::string_view Icon = {};
};

template <typename Component>
struct ComponentPlacement
{
    static constexpr ComponentPlacementFacts Value = {};
};

template <typename Component>
    requires HasComponentDefinition<Component>
             && requires { ComponentDefinition<Component>::PlaceableLabel; }
struct ComponentPlacement<Component>
{
    static constexpr ComponentPlacementFacts Value = [] {
        ComponentPlacementFacts facts{ .Label = ComponentDefinition<Component>::PlaceableLabel };
        if constexpr (requires { ComponentDefinition<Component>::PlaceableIcon; })
            facts.Icon = ComponentDefinition<Component>::PlaceableIcon;
        return facts;
    }();
};
