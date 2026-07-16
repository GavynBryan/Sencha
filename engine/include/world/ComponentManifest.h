#pragma once

#include <audio/AudioCaptionComponent.h>
#include <audio/AudioSourceComponent.h>
#include <components/CameraComponent.h>
#include <render/PointLightComponent.h>
#include <render/SpotLightComponent.h>
#include <render/StaticMeshComponent.h>
#include <world/transform/TransformComponents.h>

#include <tuple>
#include <utility>

//=============================================================================
// ComponentManifest
//
// The single authoritative list of the engine's serializable scene components.
// Adding a component to the engine means adding it here. Serializer and storage
// registration both fold over this list so they cannot drift apart.
//=============================================================================
using EngineSceneComponents = std::tuple<
    LocalTransform,
    CameraComponent,
    StaticMeshComponent,
    PointLightComponent,
    SpotLightComponent,
    AudioSourceComponent,
    AudioCaptionComponent>;

template <typename T>
struct ComponentTag
{
    using Type = T;
};

namespace ComponentManifestDetail
{
    template <typename Fn, typename... Ts>
    void ForEach(Fn&& fn, std::tuple<Ts...>*)
    {
        (fn(ComponentTag<Ts>{}), ...);
    }
}

template <typename Fn>
void ForEachSceneComponent(Fn&& fn)
{
    ComponentManifestDetail::ForEach(std::forward<Fn>(fn),
                                     static_cast<EngineSceneComponents*>(nullptr));
}
