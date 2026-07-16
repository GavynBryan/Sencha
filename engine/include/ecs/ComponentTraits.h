#pragma once

#include <ecs/EntityId.h>

class ResourceStore;

// ComponentTraits<T> is the opt-in specialization point for component lifetime edges.
// Hooks receive the registry-scoped resource owner, not entity storage. This keeps
// component lifetime work unable to perform structural ECS mutation.
template <typename T>
struct ComponentTraits
{
};

template <typename T>
concept ComponentHasOnAdd =
    requires(T& component, ResourceStore& resources, EntityId entity)
    {
        ComponentTraits<T>::OnAdd(component, resources, entity);
    };

template <typename T>
concept ComponentHasOnRemove =
    requires(const T& component, ResourceStore& resources, EntityId entity)
    {
        ComponentTraits<T>::OnRemove(component, resources, entity);
    };
