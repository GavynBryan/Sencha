#include <audio/AudioSourceComponent.h>

#include <audio/AudioClipCache.h>
#include <audio/AudioService.h>
#include <audio/AudioSourceRuntime.h>
#include <ecs/World.h>

void ComponentTraits<AudioSourceComponent>::OnAdd(
    AudioSourceComponent& component, World& world, EntityId)
{
    auto* runtime = world.TryGetResource<AudioSourceRuntime>();
    if (runtime == nullptr || runtime->Clips == nullptr)
        return;

    runtime->Clips->Retain(component.Clip);
}

void ComponentTraits<AudioSourceComponent>::OnRemove(
    const AudioSourceComponent& component, World& world, EntityId)
{
    auto* runtime = world.TryGetResource<AudioSourceRuntime>();
    if (runtime == nullptr)
        return;

    if (runtime->Audio != nullptr)
        runtime->Audio->Stop(component.Voice);
    if (runtime->Clips != nullptr)
        runtime->Clips->Release(component.Clip);
}
