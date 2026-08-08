#pragma once

#include <audio/AudioCaptionComponent.h>
#include <audio/AudioSourceComponent.h>
#include <components/CameraComponent.h>
#include <controller/LookOrientation.h>
#include <net/NetReplicationComponents.h>
#include <net/NetSpawnRecipe.h>
#include <render/IrradianceVolumeComponent.h>
#include <render/PointLightComponent.h>
#include <render/SpotLightComponent.h>
#include <render/StaticMeshComponent.h>
#include <render/ZoneLightmapComponent.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/transform/TransformComponents.h>
#include <zone/WorldConnectionComponents.h>

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
    ZoneLightmapComponent,
    IrradianceVolumeComponent,
    PointLightComponent,
    SpotLightComponent,
    AudioSourceComponent,
    AudioCaptionComponent,
    WorldDock,
    WorldLink,
    DockGateBinding,
    // Appended last: registration order feeds serialized state, so new
    // components extend this list at the end rather than reordering it.
    PersistentIdComponent>;

//=============================================================================
// EngineReplicatedComponents
//
// The engine components whose values an authority sends to its peers. A
// separate list from the scene manifest because the two answer different
// questions: that one is what an author saves, this one is what a spectator
// must see. Plenty of state is one without the other -- a light is authored and
// never changes, a look direction changes constantly and is never authored.
//
// Order is a wire contract: a component's position here is its one-byte key on
// the wire, so entries append at the end and never reorder. Both ends compare
// the compiled table's hash at the handshake, so a build that gets this wrong
// is refused rather than left to misread every snapshot.
//
// A component listed here must have a TypeSchema and no ComponentTraits
// lifecycle hooks; ReplicationLayout::Add enforces both at compile time.
//=============================================================================
using EngineReplicatedComponents = std::tuple<
    LocalTransform,
    LookOrientation,
    NetOwner,
    NetSpawnRecipe>;

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

template <typename Fn>
void ForEachReplicatedComponent(Fn&& fn)
{
    ComponentManifestDetail::ForEach(std::forward<Fn>(fn),
                                     static_cast<EngineReplicatedComponents*>(nullptr));
}
