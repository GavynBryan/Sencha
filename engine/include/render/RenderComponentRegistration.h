#pragma once

#include <anim/AnimationClipPlayerComponent.h>
#include <render/IrradianceVolumeComponent.h>
#include <render/PointLightComponent.h>
#include <render/SpotLightComponent.h>
#include <render/StaticMeshComponent.h>
#include <render/ZoneLightmapComponent.h>
#include <render/skinned_mesh/SkinnedMeshComponent.h>
#include <world/ComponentRegistrar.h>

// What a zone draws: the meshes and the lights, plus the baked products a zone
// carries for them. All authored, none replicated -- a light that never moves
// is content both machines already loaded.
using RenderComponents = ComponentSet<
    StaticMeshComponent,
    SkinnedMeshComponent,
    // The pose source for the skinned meshes above; authored beside them
    // and sampled by the same extraction walk.
    AnimationClipPlayerComponent,
    ZoneLightmapComponent,
    IrradianceVolumeComponent,
    PointLightComponent,
    SpotLightComponent>;

inline void RegisterRenderComponents(ComponentRegistrar& registrar)
{
    registrar.AddAll<RenderComponents>();
}
