#include <anim/AnimationClipPlayerComponent.h>

#include <anim/AnimationClipCache.h>
#include <anim/AnimationClipComponentAssets.h>
#include <ecs/World.h>

void ComponentTraits<AnimationClipPlayerComponent>::OnAdd(
    AnimationClipPlayerComponent& component, World& world, EntityId)
{
    auto* assets = world.TryGetResource<AnimationClipComponentAssets>();
    if (assets != nullptr && assets->Clips != nullptr)
        assets->Clips->Retain(component.Clip);
}

void ComponentTraits<AnimationClipPlayerComponent>::OnRemove(
    const AnimationClipPlayerComponent& component, World& world, EntityId)
{
    auto* assets = world.TryGetResource<AnimationClipComponentAssets>();
    if (assets != nullptr && assets->Clips != nullptr)
        assets->Clips->Release(component.Clip);
}
