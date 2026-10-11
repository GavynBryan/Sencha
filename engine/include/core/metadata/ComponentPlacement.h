#pragma once

#include <core/metadata/ComponentDefinition.h>

#include <string_view>

//=============================================================================
// ComponentPlacement
//
// Whether a tool (the editor) may put an entity carrying just this component,
// at its defaults, somewhere in a world, and under what name: a light, a
// player start. Pure tooling metadata the runtime ignores, surfaced through
// IComponentSerializer so the editor never names a component type, and
// declared the way ComponentEditorVisual<T> is, so a game module's components
// offer themselves the same way the engine's do. Empty: not placeable.
//=============================================================================
template <typename Component>
struct ComponentPlacement
{
    static constexpr std::string_view Label = {};
};

template <typename Component>
    requires HasComponentDefinition<Component>
             && requires { ComponentDefinition<Component>::PlaceableLabel; }
struct ComponentPlacement<Component>
{
    static constexpr std::string_view Label = ComponentDefinition<Component>::PlaceableLabel;
};
