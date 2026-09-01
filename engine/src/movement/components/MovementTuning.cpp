#include <movement/components/MovementTuning.h>

#include <assets/data/DataAssetCache.h>
#include <ecs/World.h>
#include <movement/MovementComponentAssets.h>

void ComponentTraits<MovementTuningSource>::OnAdd(
    MovementTuningSource& component, World& world, EntityId)
{
    auto* assets = world.TryGetResource<MovementComponentAssets>();
    if (assets != nullptr && assets->Profiles != nullptr)
        assets->Profiles->Retain(component.Profile.Value);
}

void ComponentTraits<MovementTuningSource>::OnRemove(
    const MovementTuningSource& component, World& world, EntityId)
{
    auto* assets = world.TryGetResource<MovementComponentAssets>();
    if (assets != nullptr && assets->Profiles != nullptr)
        assets->Profiles->Release(component.Profile.Value);
}
