#pragma once

#include <render/IrradianceVolumeComponent.h>
#include <render/MeshOverrides.h>
#include <render/PointLightComponent.h>
#include <render/PresentedWith.h>
#include <render/SpotLightComponent.h>
#include <render/StaticMeshComponent.h>
#include <render/ZoneLightmapComponent.h>
#include <render/skinned_mesh/SkinnedMeshComponent.h>
#include <world/ComponentRegistrar.h>

// What a zone draws: meshes, lights, the baked products a zone carries for them,
// and the overrides drawn over a mesh's authored look. None replicated: each
// machine loads the content and presents its own overrides.
using RenderComponents = ComponentSet<
    StaticMeshComponent,
    SkinnedMeshComponent,
    ZoneLightmapComponent,
    IrradianceVolumeComponent,
    PointLightComponent,
    SpotLightComponent,
    MeshOverrides,
    PresentedWith>;

inline void RegisterRenderComponents(ComponentRegistrar& registrar)
{
    registrar.AddAll<RenderComponents>();
}
