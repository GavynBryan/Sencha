#pragma once

#include <anim/AnimationComponentSchemas.h>
#include <render/MeshComponentSchemas.h>
#include <render/ZoneLightmapSchema.h>
#include <render/LightComponentSchemas.h>
#include <world/ComponentRegistrar.h>

// What a zone draws: the meshes and the lights, plus the baked products a zone
// carries for them. All authored, none replicated -- a light that never moves
// is content both machines already loaded.
inline void RegisterRenderComponents(ComponentRegistrar& registrar)
{
    registrar.Add<StaticMeshComponent>();
    registrar.Add<SkinnedMeshComponent>();
    // The pose source for the skinned meshes above; authored beside them
    // and sampled by the same extraction walk.
    registrar.Add<AnimationClipPlayerComponent>();
    registrar.Add<ZoneLightmapComponent>();
    registrar.Add<IrradianceVolumeComponent>();
    registrar.Add<PointLightComponent>();
    registrar.Add<SpotLightComponent>();
}
