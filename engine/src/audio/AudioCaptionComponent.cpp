#include <audio/AudioCaptionComponent.h>

#include <audio/AudioSourceRuntime.h>
#include <audio/CaptionRuntime.h>
#include <ecs/World.h>

void ComponentTraits<AudioCaptionComponent>::OnRemove(
    const AudioCaptionComponent& component, World& world, EntityId)
{
    auto* runtime = world.TryGetResource<AudioSourceRuntime>();
    if (runtime == nullptr || runtime->Captions == nullptr)
        return;

    runtime->Captions->EndCaption(component.Caption);
}
