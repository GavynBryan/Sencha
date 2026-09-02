#include <audio/AudioSourceComponent.h>

#include <audio/AudioService.h>
#include <audio/AudioSourceRuntime.h>
#include <ecs/World.h>

void ComponentTraits<AudioSourceComponent>::OnRemove(
    const AudioSourceComponent& component, World& world, EntityId entity)
{
    auto* runtime = world.TryGetResource<AudioSourceRuntime>();
    if (runtime != nullptr && runtime->Audio != nullptr)
        runtime->Audio->Stop(component.Voice);

    SchemaAssetOwnership<AudioSourceComponent>::OnRemove(component, world, entity);
}
