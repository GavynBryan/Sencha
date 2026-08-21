#pragma once

#include <render/IrradianceVolumeComponent.h>
#include <render/PointLightComponent.h>
#include <render/SpotLightComponent.h>
#include <render/StaticMeshComponent.h>
#include <render/skinned_mesh/SkinnedMeshComponent.h>
#include <render/ZoneLightmapComponent.h>
#include <world/ComponentRegistrar.h>

// What a zone draws: the meshes and the lights, plus the baked products a zone
// carries for them. All authored, none replicated -- a light that never moves
// is content both machines already loaded.
inline void RegisterRenderComponents(ComponentRegistrar& registrar)
{
    registrar.Add<StaticMeshComponent>();
    registrar.Add<SkinnedMeshComponent>();
    registrar.Add<ZoneLightmapComponent>();
    registrar.Add<IrradianceVolumeComponent>();
    registrar.Add<PointLightComponent>();
    registrar.Add<SpotLightComponent>();
}
