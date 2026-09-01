#include <render/ZoneLightmapComponent.h>

#include <assets/texture/TextureCache.h>
#include <ecs/World.h>
#include <render/ZoneLightmapComponentAssets.h>

void ComponentTraits<ZoneLightmapComponent>::OnAdd(
    ZoneLightmapComponent& component, World& world, EntityId)
{
    auto* assets = world.TryGetResource<ZoneLightmapComponentAssets>();
    if (assets != nullptr && assets->Textures != nullptr)
    {
        assets->Textures->Retain(component.Texture);
        assets->Textures->Retain(component.Ao);
    }
}

void ComponentTraits<ZoneLightmapComponent>::OnRemove(
    const ZoneLightmapComponent& component, World& world, EntityId)
{
    auto* assets = world.TryGetResource<ZoneLightmapComponentAssets>();
    if (assets != nullptr && assets->Textures != nullptr)
    {
        assets->Textures->Release(component.Texture);
        assets->Textures->Release(component.Ao);
    }
}
