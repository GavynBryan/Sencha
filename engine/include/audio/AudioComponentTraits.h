#pragma once

#include <audio/AudioCaptionComponent.h>
#include <audio/AudioClipCache.h>
#include <audio/AudioService.h>
#include <audio/AudioSourceComponent.h>
#include <audio/AudioSourceRuntime.h>
#include <audio/CaptionRuntime.h>
#include <ecs/ComponentTraits.h>
#include <ecs/EntityId.h>
#include <ecs/World.h>

//=============================================================================
// Clip lifetime and voice teardown for the audio components.
//
// Registration includes this. A system that reads one of these components
// includes the component and gets its values, not the services that own what
// the values refer to.
//=============================================================================

template <>
struct ComponentTraits<AudioSourceComponent>
{
    // OnAdd retains the clip and nothing more: deserialization is not
    // activation, and the zone may be dormant. AudioSystem starts playback.
    static void OnAdd(AudioSourceComponent& component, World& world, EntityId)
    {
        auto* runtime = world.TryGetResource<AudioSourceRuntime>();
        if (runtime == nullptr || runtime->Clips == nullptr)
            return;

        runtime->Clips->Retain(component.Clip);
    }

    // OnRemove enforces the slice's one invariant (docs/audio/runtime.md,
    // Decision C): a voice never outlives the clip reference that feeds it.
    // Stop first, then release — in that order, in this hook, which fires on
    // both entity destruction and zone detach.
    static void OnRemove(const AudioSourceComponent& component, World& world, EntityId)
    {
        auto* runtime = world.TryGetResource<AudioSourceRuntime>();
        if (runtime == nullptr)
            return;

        if (runtime->Audio != nullptr)
            runtime->Audio->Stop(component.Voice);
        if (runtime->Clips != nullptr)
            runtime->Clips->Release(component.Clip);
    }
};

template <>
struct ComponentTraits<AudioCaptionComponent>
{
    // No OnAdd: there is no asset edge to retain, and deserialization is not
    // activation — CaptionSystem begins captions.

    // OnRemove ends the active caption (entity destruction and zone detach
    // both fire it). Stale-safe: an already-retired caption is a no-op, and
    // ordering against the sibling source's own OnRemove does not matter.
    static void OnRemove(const AudioCaptionComponent& component, World& world, EntityId)
    {
        auto* runtime = world.TryGetResource<AudioSourceRuntime>();
        if (runtime == nullptr || runtime->Captions == nullptr)
            return;

        runtime->Captions->EndCaption(component.Caption);
    }
};
