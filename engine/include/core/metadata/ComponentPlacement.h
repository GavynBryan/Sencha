#pragma once

#include <core/metadata/ComponentDefinition.h>

#include <string_view>

//=============================================================================
// ComponentPlacement
//
// Whether a tool (the editor) may put an entity carrying just this component,
// at its defaults, somewhere in a world: under what name, pictured by which
// editor icon. A light, a player start. Pure
// tooling metadata the runtime ignores, surfaced through IComponentSerializer
// so the editor never names a component type, and declared the way
// ComponentEditorVisual<T> is, so a game module's components offer themselves
// the same way the engine's do. An empty label: not placeable.
//=============================================================================
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
