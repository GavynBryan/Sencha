#pragma once

#include <core/metadata/ComponentDefinition.h>

#include <cstdint>

// Whether a save holds a component: docs/gameplay/saves.md, "What a save holds of an entity".
enum class ComponentPersistence : std::uint8_t
{
    // Content says it. Not diffed unless an entity's baseline names it.
    Authored,
    // Diffed by default on every entity content made.
    Saved,
    // What an entity is known by. Whoever makes the entity gives it.
    Identity,
};

template <typename Component>
struct ComponentPersistenceOf
{
    static constexpr ComponentPersistence Value = ComponentPersistence::Authored;
};

// SENCHA_SAVED. Partial, so a handwritten specialization still wins.
template <typename Component>
    requires HasComponentDefinition<Component>
             && requires { ComponentDefinition<Component>::Saved; }
struct ComponentPersistenceOf<Component>
{
    static constexpr ComponentPersistence Value =
        ComponentDefinition<Component>::Saved ? ComponentPersistence::Saved : ComponentPersistence::Authored;
};
