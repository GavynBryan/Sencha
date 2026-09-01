#pragma once

#include <anim/AnimationClipCache.h>
#include <anim/AnimationClipComponentAssets.h>
#include <anim/AnimationClipPlayerComponent.h>
#include <ecs/ComponentTraits.h>
#include <ecs/EntityId.h>
#include <ecs/World.h>

// Clip lifetime for the animation components. Registration includes this; a
// system that reads a component includes the component and gets its values,
// not the services that own what the values refer to.
template <>
struct ComponentTraits<AnimationClipPlayerComponent>
{
    static void OnAdd(AnimationClipPlayerComponent& component, World& world, EntityId)
    {
        auto* assets = world.TryGetResource<AnimationClipComponentAssets>();
        if (assets != nullptr && assets->Clips != nullptr)
            assets->Clips->Retain(component.Clip);
    }

    static void OnRemove(const AnimationClipPlayerComponent& component, World& world, EntityId)
    {
        auto* assets = world.TryGetResource<AnimationClipComponentAssets>();
        if (assets != nullptr && assets->Clips != nullptr)
            assets->Clips->Release(component.Clip);
    }
};
