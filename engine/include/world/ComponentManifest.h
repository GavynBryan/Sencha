#pragma once

#include <audio/AudioCaptionComponent.h>
#include <audio/AudioSourceComponent.h>
#include <render/Camera.h>
#include <render/StaticMeshComponent.h>
#include <world/transform/TransformComponents.h>

#include <tuple>
#include <utility>

//=============================================================================
// ComponentManifest
//
// The single authoritative list of the engine's serializable scene components.
// Adding a component to the engine means adding it here — nowhere else. Both
// InitSceneSerializer() and InitializeDefault3DRegistry() fold over this list,
// so serializer registration and storage registration can never drift apart.
//
// Everything else about a component lives in its own header: the struct,
// ComponentTraits lifecycle hooks, and TypeSchema (JSON name, SceneChunkId,
// fields). See docs/ecs/component-registration-plan.md.
//
// This header is deliberately the one cross-module aggregation point in
// world/ — it must name every component, wherever it lives. Do not add
// audio/ or render/ includes to any other world/ header.
//
// Game-specific components are not listed here: games call
// RegisterComponent<T>() after InitSceneSerializer() and register storage in
// their own zone setup.
//=============================================================================
using EngineSceneComponents = std::tuple<
    LocalTransform,
    CameraComponent,
    StaticMeshComponent,
    AudioSourceComponent,
    AudioCaptionComponent>;

// Tag passed to ForEachSceneComponent visitors; carries the component type
// without constructing a component.
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

// Calls fn(ComponentTag<T>{}) for every component in EngineSceneComponents.
template <typename Fn>
void ForEachSceneComponent(Fn&& fn)
{
    ComponentManifestDetail::ForEach(std::forward<Fn>(fn),
                                     static_cast<EngineSceneComponents*>(nullptr));
}
